// MidiKitRoundTrip.perf.cpp
//
// Wall-clock round-trip latency of MidiKit scripts measured through the REAL
// TaskWorker (via TaskWorkerAdapter) — the same asynchronous path used in
// production: the module thread enqueues onto a shared background worker and
// the script actually runs on that worker thread.
//
// This is a measurement harness, NOT part of `make test` / `make testrun`:
//   - the Makefile's test discovery is a blind `wildcard *.test.cpp` with no
//     tag filter, so a `.test.cpp` file here would be built and run on every
//     `make test` — this deliberately uses the `.perf.cpp` suffix instead;
//   - build/run with `make perf` / `make perfrun` only.
//
// Measurement style:
//   * Single engine per complexity tier uses Catch2's BENCHMARK_ADVANCED, so
//     the results come out as Catch2's standard mean + bootstrap CI report.
//     There are deliberately NO REQUIREs anywhere in this file: it is a
//     measurement report, not an assertion suite.
//   * The "N engines share one TaskWorker" contention case cannot map onto
//     BENCHMARK (it needs one thread per engine), so it stays hand-rolled and
//     prints min/med/p95/max distributions via std::cout.

#include "MidiKit.test.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using ITaskWorker = StoermelderPackOne::ITaskWorker;

using Clock = std::chrono::steady_clock;

// ── small helpers (same conventions as the MidiKit tests) ───────────────────

static std::string repoRoot() {
	static const std::string suffix = "src/modules/midikit/";
	std::string f = __FILE__;
	size_t at = f.rfind(suffix);
	std::string root = (at == std::string::npos) ? "" : f.substr(0, at);
	while (root.size() > 1 && root.back() == '/') root.pop_back();
	return root.empty() ? "." : root;
}

static std::string readFile(const std::string& path) {
	std::ifstream f(path);
	if (!f.good()) {
		std::cout << "  !! cannot open " << path << std::endl;
		return "";
	}
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static midi::Message noteOn(int ch, int note, int vel) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(vel);
	return msg;
}

static midi::Message noteOff(int ch, int note) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x8);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(0);
	return msg;
}

static void drainOut(MidiScriptEngine* se) {
	int port, ticks;
	midi::Message msg;
	while (se->processOutMessage(port, msg, ticks)) { }
}

// Pushes a sentinel task onto the shared worker and spins until it has run.
// The worker drains its queue FIFO, so once the sentinel runs, every earlier
// task (which may capture the engine's `this`) has finished. Without this,
// destroying a module while the worker is still inside one of its tasks is a
// use-after-free.
//
// work() uses try_push and returns false when the queue is momentarily full
// (capacity 32). We retry until the sentinel is accepted — the worker keeps
// draining, so this always terminates. Without the retry, a full queue would
// leave the sentinel unqueued and this loop would spin forever.
static void barrier(std::shared_ptr<ITaskWorker> worker) {
	std::atomic<bool> done{false};
	while (!worker->work([&done]() { done.store(true, std::memory_order_release); })) {
		std::this_thread::yield();
	}
	while (!done.load(std::memory_order_acquire)) std::this_thread::yield();
}

// ── script workloads ────────────────────────────────────────────────────────

enum class EngineKind { 
	QuickJs,
	Lua
};

// Per-iteration push: enqueue the (tier-specific) traffic then call process(),
// which is what actually dispatches onto the shared worker. Each push must
// guarantee >= 1 outbound message the round-trip poll can observe.
using PushFn = std::function<void(int iteration)>;

// Base class for a script workload. Each concrete script encapsulates all of
// its tier-specific behaviour: the per-iteration push and any one-time
// seeding needed before measurement.
struct Script {
	virtual ~Script() = default;
	virtual const char* name() const = 0;
	// Per-iteration push for the given module's engine.
	virtual PushFn pushFn(MidiKitModule* m) const = 0;
	// One-time setup before measurement (default: none).
	virtual void seed(MidiScriptEngine* se, std::shared_ptr<ITaskWorker> worker) const {}
};

// PassThrough: every incoming message is forwarded unchanged -> exactly 1
// output per push.
struct PassThroughScript : Script {
	const char* name() const override { return "PassThrough"; }
	PushFn pushFn(MidiKitModule* m) const override {
		MidiScriptEngine* se = m->activeEngine;
		return [se](int) {
			midi::Message msg = noteOn(1, 60, 100);
			se->processInMessage(0, msg);
			se->process();
		};
	}
};

// Note length quantiser: Note-On -> the note is forwarded + a scheduled
// Note-Off queued. Alternates two notes so the "already sounding -> cut"
// retrigger branch (heavier) also runs, not only the first-touch branch.
struct NoteLengthScript : Script {
	const char* name() const override { return "Note length quantiser"; }
	PushFn pushFn(MidiKitModule* m) const override {
		MidiScriptEngine* se = m->activeEngine;
		return [se](int i) {
			int note = (i % 2 == 0) ? 60 : 62;
			midi::Message msg = noteOn(1, note, 100);
			se->processInMessage(0, msg);
			se->process();
		};
	}
};

// Arpeggiator: the heaviest tier — 4 octaves + Up-Down play -> a 22-note
// pattern, rebuilt on every held-note change. Each iteration removes one held
// note and adds another (both rebuild the pattern), then steps the clock — the
// step is what produces the output. `held` is copied into the closure so every
// module/thread rotates its own set.
struct ArpeggiatorScript : Script {
	const char* name() const override { return "Arpeggiator"; }
	PushFn pushFn(MidiKitModule* m) const override {
		MidiScriptEngine* se = m->activeEngine;
		m->params[MidiKitModule::PARAM + 1].setValue(0.99f);  // octave range -> 4
		m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note length 50%
		m->params[MidiKitModule::PARAM + 3].setValue(0.99f);  // playmode -> Up
		std::vector<int> held = {60, 64, 67};
		return [se, held](int i) mutable {
			int remove = held[0];
			int add = 48 + (i % 12);          // disjoint from the seed notes
			midi::Message off = noteOff(1, remove);
			midi::Message on = noteOn(1, add, 100);
			se->processInMessage(0, off);
			se->processInMessage(0, on);
			se->processInTick(0);
			se->process();
			held[0] = add;
		};
	}
	// Establishes the held-note set {60,64,67} so the "complex" workload
	// (pattern rebuild across several octaves) is representative from the first
	// measured run instead of a 1-note pattern. The worker barrier guarantees
	// the seed note-ons have been processed (pattern rebuilt) before returning.
	void seed(MidiScriptEngine* se, std::shared_ptr<ITaskWorker> worker) const override {
		for (int note : {60, 64, 67}) {
			midi::Message on = noteOn(1, note, 100);
			se->processInMessage(0, on);
			se->process();
		}
		barrier(worker);
		drainOut(se);   // arp note-ons emit no output, but be tidy
	}
};

// The scripts under test, in report order.
static PassThroughScript passThroughScript;
static NoteLengthScript noteLengthScript;
static ArpeggiatorScript arpeggiatorScript;

static Script* const SCRIPTS[] = { &passThroughScript, &noteLengthScript, &arpeggiatorScript };



// Constructs a script's path for a given engine. The scripts live in a
// per-engine directory (JavaScript/ / Lua/) and share the script's name as the
// filename base, so no per-script paths need to be stored.
static std::string enginePath(const Script* script, EngineKind engine) {
	const char* dir = (engine == EngineKind::QuickJs) ? "JavaScript" : "Lua";
	const char* ext = (engine == EngineKind::QuickJs) ? "js" : "lua";
	return std::string("presets/MidiKit/") + dir + "/" + script->name() + "." + ext;
}

// Human-readable engine label for output.
static const char* engineName(EngineKind engine) {
	return (engine == EngineKind::QuickJs) ? "QuickJs" : "minilua";
}

// ── round-trip timing ───────────────────────────────────────────────────────

// Waits (tight spin + periodic yield) for the first output message to become
// observable in midiOutQueue via processOutMessage(). Returns false on
// timeout. The yield keeps the spin from pegging the core while letting the
// worker thread run.
static bool pollFirst(MidiScriptEngine* se, double maxWaitSec = 10.0) {
	auto start = Clock::now();
	int port = 0, ticks = 0;
	midi::Message msg;
	int spins = 0;
	while (!se->processOutMessage(port, msg, ticks)) {
		if (std::chrono::duration<double>(Clock::now() - start).count() >= maxWaitSec) return false;
		if ((spins++ & 63) == 0) std::this_thread::yield();
	}
	return true;
}

// One timed round trip, in microseconds; -1.0 on timeout. Used by the
// contention section — the single-engine section is timed by BENCHMARK.
static double timeRoundTrip(MidiScriptEngine* se, const PushFn& push, int iteration, double maxWaitSec = 10.0) {
	auto start = Clock::now();
	push(iteration);
	if (!pollFirst(se, maxWaitSec)) return -1.0;
	return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

// Like timeRoundTrip, then drains the whole out-queue so the next (warm-up)
// call starts from an empty queue. Returns false on timeout.
static bool pushAndDrain(MidiScriptEngine* se, const PushFn& push, int iteration, double maxWaitSec = 10.0) {
	push(iteration);
	if (!pollFirst(se, maxWaitSec)) return false;
	drainOut(se);
	return true;
}

// ── reporting (contention section only; single-engine uses BENCHMARK) ───────

static void printDist(const std::string& label, const std::vector<double>& samples) {
	std::vector<double> v;
	for (double s : samples) if (s >= 0) v.push_back(s);
	if (v.empty()) {
		std::cout << std::left << std::setw(36) << label << "  (no samples)" << std::endl;
		return;
	}
	std::sort(v.begin(), v.end());
	double min = v.front();
	double max = v.back();
	double med = (v.size() % 2) ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
	double p95 = v[(size_t)(0.95 * (v.size() - 1))];
	std::cout << std::left << std::setw(36) << label
	          << std::right << std::fixed << std::setprecision(1)
	          << " n=" << std::setw(4) << v.size()
	          << "  min=" << std::setw(8) << min
	          << "us  med=" << std::setw(8) << med
	          << "us  p95=" << std::setw(8) << p95
	          << "us  max=" << std::setw(8) << max
	          << "us" << std::endl;
}

// ── single-engine setup + BENCHMARK body ────────────────────────────────────

// Creates a module, loads the tier's script, and reports whether it loaded.
// On success also seeds the arpeggiator (if applicable) and clears any output
// queued during onLoad so measurements start clean.
static bool setupEngine(const Script* script, EngineKind engine,
	std::shared_ptr<ITaskWorker> worker, MidiKitModule** outM,
	PushFn* outPush, MidiScriptEngine** outSe) {
	std::string path = enginePath(script, engine);
	MidiKitModule* m = createModule(worker);
	m->loadScript(readFile(repoRoot() + "/" + path));
	std::string loadLog = drainLog(m);
	if (loadLog.find("rror") != std::string::npos || loadLog.find("Script loaded") == std::string::npos) {
		std::cout << "  !! " << script->name() << " / " << engineName(engine) << ": failed to load " << path << "\n"
		          << loadLog << std::endl;
		barrier(worker);
		Test::destroyModule(m);
		return false;
	}
	MidiScriptEngine* se = m->activeEngine;
	drainOut(se);   // clear anything queued during onLoad
	*outM = m;
	*outPush = script->pushFn(m);
	*outSe = se;
	script->seed(se, worker);
	return true;
}

// Body shared by every single-engine BENCHMARK_ADVANCED block. Setup and
// teardown happen outside meter.measure() (not timed); only the per-run round
// trip is timed. Each measured run does one push + wait for its first output
// + drain the (possible) leftover scheduled message, then a barrier so the
// worker is idle before the next run.
//
// The trailing barrier is not just belt-and-braces: TaskWorker::workQueue is a
// fixed-size (32) dsp::RingBuffer whose push() silently OVERWRITES the oldest
// slot when full. The script tiers with more than one output per push (e.g.
// the arp: note-off + note-on + clock step in ONE worker task) make pollFirst
// return on the first output while the task is still draining the rest, so the
// loop can enqueue faster than the worker drains and overflow the queue. A
// sentinel pushed into a full queue lands at the FRONT and runs before the
// module's own tasks, letting the block delete the module while the worker is
// still inside one of its tasks — a use-after-free. The per-run barrier keeps
// at most one task in flight, so the block-end barrier always strictly follows
// every module task. It also stops a previous task's leftover output from
// bleeding into the next run's pollFirst (which would otherwise report a bogus
// ~0 latency). Each run therefore measures the full round trip: push -> the
// script has processed the whole push and its output is drained.
static void runBenchBlock(const Script* script, EngineKind engine,
	std::shared_ptr<ITaskWorker> worker, Catch::Benchmark::Chronometer meter) {
	MidiKitModule* m = nullptr;
	PushFn push;
	MidiScriptEngine* se = nullptr;
	if (!setupEngine(script, engine, worker, &m, &push, &se)) return;
	meter.measure([&](int i) {
		push(i);
		bool observed = pollFirst(se);
		drainOut(se);
		barrier(worker);
		return observed;
	});
	barrier(worker);
	Test::destroyModule(m);
}

// ── N-engine contention (hand-rolled: BENCHMARK cannot time threads) ────────

// N modules sharing one worker, each driven from its own thread: per-message
// round trip as the worker's single thread serialises N loads. Returns the
// merged per-thread samples (µs).
static std::vector<double> measureContention(const Script* script, EngineKind engine,
	std::shared_ptr<ITaskWorker> worker, int n, int itersPerThread, int& timeouts) {
	std::string path = enginePath(script, engine);
	std::vector<MidiKitModule*> mods;
	std::vector<PushFn> pushes;
	for (int i = 0; i < n; i++) {
		MidiKitModule* m = createModule(worker);
		m->loadScript(readFile(repoRoot() + "/" + path));
		std::string loadLog = drainLog(m);
		if (loadLog.find("rror") != std::string::npos || loadLog.find("Script loaded") == std::string::npos) {
			std::cout << "  !! failed to load " << path << "\n" << loadLog << std::endl;
			barrier(worker);
			Test::destroyModule(m);
			continue;
		}
		drainOut(m->activeEngine);
		mods.push_back(m);
		pushes.push_back(script->pushFn(m));
		script->seed(m->activeEngine, worker);
	}

	// TaskWorker::workQueue is a dsp::RingBuffer — SPSC (single producer).
	// With n threads pushing concurrently that is a multi-producer race, so
	// the push (not the wait) is serialised with a shared mutex. The
	// contention deliberately measured is the worker's single background
	// thread processing the shared queue serially.
	std::mutex pushMutex;
	std::atomic<int> timeoutsLocal{0};
	std::vector<std::vector<double>> perThread(mods.size());
	std::vector<std::thread> threads;
	for (size_t i = 0; i < mods.size(); i++) {
		threads.emplace_back([&, i]() {
			MidiScriptEngine* se = mods[i]->activeEngine;
			PushFn push = pushes[i];
			PushFn lockedPush = [&pushMutex, push](int k) {
				std::lock_guard<std::mutex> lock(pushMutex);
				push(k);
			};

			// Warm-up (not timed) so each thread's out-queue is empty and the
			// worker has settled before any measured iteration.
			for (int w = 0; w < 10; w++) pushAndDrain(se, lockedPush, w);

			std::vector<double>& out = perThread[i];
			out.reserve(itersPerThread);
			for (int k = 0; k < itersPerThread; k++) {
				double us = timeRoundTrip(se, lockedPush, k + 10);
				out.push_back(us);
				if (us < 0) timeoutsLocal++;
				drainOut(se);
				// Keep the shared queue drained (see runBenchBlock): a full
				// workQueue would let the end-of-run barrier's sentinel
				// overwrite a pending module task and delete a module whose
				// task is still running on the worker.
				barrier(worker);
			}
		});
	}
	for (auto& t : threads) t.join();
	timeouts += timeoutsLocal.load();

	// All pushes happened-before join; a barrier now guarantees every one of
	// the modules' tasks has finished before we destroy them.
	barrier(worker);
	for (auto* m : mods) Test::destroyModule(m);

	std::vector<double> all;
	for (const auto& v : perThread) all.insert(all.end(), v.begin(), v.end());
	return all;
}

// BENCHMARK_ADVANCED registers each benchmark at compile time (every expansion
// needs a unique variable name), so it can't live in a runtime loop. This macro
// is the "loop": one expansion per (script, engine) pair, with the benchmark
// name built from the script name and engine label.
#define BENCH_SCRIPT(script, engine) \
	BENCHMARK_ADVANCED(std::string(script->name()) + " / " + engineName(engine))(Catch::Benchmark::Chronometer meter) { runBenchBlock(script, engine, worker, meter); };

TEST_CASE("MidiKit single-engine round-trip latency (Catch2 BENCHMARK)", "[perf]") {
	// One process-wide real TaskWorker shared by every module — mirrors the
	// production defaultWorker() (one background thread, serial queue).
	auto worker = std::make_shared<StoermelderPackOne::MpmcTaskWorker>("MidiKit perf worker");

	std::cout << "\n=== MidiKit single-engine round-trip latency (real TaskWorker) ===" << std::endl;
	std::cout << "Round trip = push (processInMessage / processInTick + process) until the first\n"
	          << "message is observable in midiOutQueue via processOutMessage().\n"
	          << "Each tier uses Catch2's BENCHMARK (mean + bootstrap CI). No REQUIREs here."
	          << std::endl << std::endl;

	std::cout << "--- Single engine, per complexity tier (Catch2 BENCHMARK) ---" << std::endl;
	BENCH_SCRIPT(SCRIPTS[0], EngineKind::QuickJs)
	BENCH_SCRIPT(SCRIPTS[0], EngineKind::Lua)
	BENCH_SCRIPT(SCRIPTS[1], EngineKind::QuickJs)
	BENCH_SCRIPT(SCRIPTS[1], EngineKind::Lua)
	BENCH_SCRIPT(SCRIPTS[2], EngineKind::QuickJs)
	BENCH_SCRIPT(SCRIPTS[2], EngineKind::Lua)
	std::cout << std::endl;
}

TEST_CASE("MidiKit N-engine contention on one TaskWorker", "[perf]") {
	// One process-wide real TaskWorker shared by every module — mirrors the
	// production defaultWorker() (one background thread, serial queue).
	auto worker = std::make_shared<StoermelderPackOne::MpmcTaskWorker>("MidiKit perf worker");

	const int CONTENTION_ITER = 500;
	const int CONTENTION_NS[] = {1, 2, 5, 10};

	std::cout << "\n=== MidiKit N-engine contention on one TaskWorker ===" << std::endl;
	std::cout << "Round trip = push (processInMessage / processInTick + process) until the first\n"
	          << "message is observable in midiOutQueue via processOutMessage().\n"
	          << "N engines share one TaskWorker; this can't map onto BENCHMARK (one thread\n"
	          << "per engine), so it prints min/med/p95/max distributions instead. No REQUIREs here."
	          << std::endl << std::endl;

	std::cout << "--- N engines sharing one TaskWorker (contention) ---" << std::endl;
	for (const Script* script : SCRIPTS) {
		for (EngineKind engine : {EngineKind::QuickJs, EngineKind::Lua}) {
			std::cout << "  [" << script->name() << " / " << engineName(engine) << "]" << std::endl;
			for (int n : CONTENTION_NS) {
				int timeouts = 0;
				auto samples = measureContention(script, engine, worker, n, CONTENTION_ITER, timeouts);
				if (timeouts > 0) std::cout << "    !! " << timeouts << " timeouts" << std::endl;
				printDist("    N=" + std::to_string(n), samples);
			}
		}
	}
	std::cout << std::endl;
}

// ── idle cost (no MIDI/trigger activity) ────────────────────────────────────
//
// Investigates a reported CPU-usage complaint: MidiKit apparently costs CPU
// even with a script loaded and no MIDI/trigger events arriving. The
// round-trip benchmarks above only measure the active path (message in ->
// message out); these measure the two paths that still run when idle:
//   1. MidiKitModule::process() on the audio thread, every sample, whether or
//      not any message/tick is pending.
//   2. MidiScriptEnginePortInfo::getName() / MidiScriptEngineParamQuantity::
//      getDisplayValueString(), which the Rack UI polls every frame for any
//      hovered port/param tooltip (see ParamTooltip::step()/PortTooltip::
//      step() in Rack's app/ParamWidget.cpp / PortWidget.cpp) -- each poll
//      that finds queryInFlight false dispatches a fresh runAsync() task that
//      calls into the script engine.

static double timeIdleProcess(MidiKitModule* m, int nSamples) {
	Module::ProcessArgs args;
	args.sampleRate = 44100.f;
	args.sampleTime = 1.f / 44100.f;
	args.frame = 0;
	auto start = Clock::now();
	for (int i = 0; i < nSamples; i++) {
		m->process(args);
		args.frame++;
	}
	auto end = Clock::now();
	return std::chrono::duration<double, std::milli>(end - start).count();
}

TEST_CASE("MidiKit idle process() cost: no script vs QuickJs vs Lua", "[perf]") {
	auto worker = std::make_shared<StoermelderPackOne::MpmcTaskWorker>("MidiKit idle-perf worker");
	const int N = 44100 * 5; // 5 seconds of audio at 44.1kHz

	std::cout << "\n=== MidiKit idle process() cost (no MIDI/trigger activity) ===" << std::endl;
	std::cout << "  process() returns immediately via `if (!activeEngine) return;` when no\n"
	          << "  script is loaded, so ANY loaded script (trivial or not) turns on the full\n"
	          << "  per-sample body below, including the 16-channel outputPulseGenerator loop\n"
	          << "  that runs whether or not the script uses triggers at all." << std::endl;

	// No script loaded
	{
		MidiKitModule* m = createModule(worker);
		double ms = timeIdleProcess(m, N);
		std::cout << "  No script:              " << ms << " ms for " << N << " samples ("
		          << (ms * 1000.0 / N) << " us/sample)" << std::endl;
		Test::destroyModule(m);
	}

	// QuickJs PassThrough loaded, idle (no messages pushed)
	{
		MidiKitModule* m = createModule(worker);
		m->loadScript(readFile(repoRoot() + "/" + enginePath(&passThroughScript, EngineKind::QuickJs)));
		std::string loadLog = drainLog(m);
		std::cout << "  QuickJs load log: " << loadLog << std::endl;
		double ms = timeIdleProcess(m, N);
		std::cout << "  QuickJs (PassThrough):  " << ms << " ms for " << N << " samples ("
		          << (ms * 1000.0 / N) << " us/sample)" << std::endl;
		Test::destroyModule(m);
	}

	// Lua PassThrough loaded, idle
	{
		MidiKitModule* m = createModule(worker);
		m->loadScript(readFile(repoRoot() + "/" + enginePath(&passThroughScript, EngineKind::Lua)));
		std::string loadLog = drainLog(m);
		std::cout << "  Lua load log: " << loadLog << std::endl;
		double ms = timeIdleProcess(m, N);
		std::cout << "  Lua (PassThrough):      " << ms << " ms for " << N << " samples ("
		          << (ms * 1000.0 / N) << " us/sample)" << std::endl;
		Test::destroyModule(m);
	}

	std::cout << std::endl;
}

// Isolates the 16-channel outputPulseGenerator/setVoltage loop (the part of
// process() that only runs once a script is loaded, gated by the early
// `if (!activeEngine) return;`) from everything else in process() when idle,
// by timing the trigger-out loop's own cost directly. If this alone accounts
// for most of the no-script -> script-loaded jump above, the finding is:
// "engine dispatch is cheap when idle; the always-on 16-channel voltage loop
// is what turns on."
TEST_CASE("MidiKit idle process() cost: trigger-output loop in isolation", "[perf]") {
	auto worker = std::make_shared<StoermelderPackOne::MpmcTaskWorker>("MidiKit idle-perf worker");
	const int N = 44100 * 5;

	MidiKitModule* m = createModule(worker);
	m->loadScript(readFile(repoRoot() + "/" + enginePath(&passThroughScript, EngineKind::QuickJs)));
	drainLog(m);

	// Directly time just the loop body process() runs unconditionally every
	// sample once activeEngine is set (copied verbatim from MidiKit.cpp).
	Module::ProcessArgs args;
	args.sampleTime = 1.f / 44100.f;
	auto start = Clock::now();
	for (int i = 0; i < N; i++) {
		for (uint8_t ch = 0; ch < PORT_MAX_CHANNELS; ch++) {
			bool s = m->outputPulseGenerator[ch].process(args.sampleTime);
			if (m->outputTriggerActive[ch]) {
				m->outputs[MidiKitModule::OUTPUT_TRIG].setVoltage(s ? 10.f : 0.f, ch);
			}
		}
	}
	auto end = Clock::now();
	double ms = std::chrono::duration<double, std::milli>(end - start).count();
	std::cout << "\n=== Trigger-output loop alone (16 channels x " << N << " samples) ===" << std::endl;
	std::cout << "  " << ms << " ms total (" << (ms * 1000.0 / N) << " us/sample)" << std::endl;

	Test::destroyModule(m);
}

TEST_CASE("MidiKit idle UI polling cost: getDisplayValueString/getName with script loaded", "[perf]") {
	auto worker = std::make_shared<StoermelderPackOne::MpmcTaskWorker>("MidiKit idle-perf worker");
	MidiKitModule* m = createModule(worker);
	m->loadScript(readFile(repoRoot() + "/" + enginePath(&passThroughScript, EngineKind::QuickJs)));
	drainLog(m);

	// Enable input 0 / param 0 the way a script does via input.enable()/param.enable()
	m->enableInput(0);
	m->enableParam(0);

	const int N = 1000; // simulate 1000 UI frames (~16s at 60Hz)
	auto* pi = reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[0]);
	auto* pq = reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[0]);

	auto start = Clock::now();
	for (int i = 0; i < N; i++) {
		volatile std::string s1 = pi->getName();
		volatile std::string s2 = pq->getDisplayValueString();
		(void)s1; (void)s2;
		// Give the (real, async) worker a chance to clear queryInFlight between
		// polls, same as it would between two real 1/60s-spaced UI frames.
		barrier(worker);
	}
	auto end = Clock::now();
	double ms = std::chrono::duration<double, std::milli>(end - start).count();
	std::cout << "\n=== MidiKit UI polling cost (getName/getDisplayValueString via real worker) ===" << std::endl;
	std::cout << "  " << N << " simulated tooltip-step polls: " << ms << " ms total (" << (ms / N) << " ms/poll)" << std::endl;

	Test::destroyModule(m);
}
