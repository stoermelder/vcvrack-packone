#pragma once
#include "../../test/framework.hpp"
#include "MidiKit.cpp"
#include <fstream>
#include <initializer_list>
#include <sstream>

using namespace StoermelderPackOne::MidiKit;
using StoermelderPackOne::MidiScript::MidiScriptEngine;

Test::TestContext<> testContext;

// ── Shared helpers ───────────────────────────────────────────────────────
// The test headers are one TU and must not depend on each other, so anything
// used by more than one header lives here in MidiKit.test.hpp.

// Shared Note-On helper (also used by the perf harness).
static midi::Message noteOn(int ch, int note, int vel) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(vel);
	return msg;
}

// Shared Note-Off helper (also used by the perf harness).
static midi::Message noteOff(int ch, int note) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x8);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(0);
	return msg;
}

// Reads an integer field out of a config JSON string (jansson).
static json_int_t configInt(const std::string& json, const char* key) {
	json_error_t error;
	json_t* j = json_loads(json.c_str(), 0, &error);
	REQUIRE(j != nullptr);
	json_t* v = json_object_get(j, key);
	REQUIRE(v != nullptr);
	json_int_t result = json_integer_value(v);
	json_decref(j);
	return result;
}

// Reads a boolean field out of a config JSON string (jansson).
static bool configBool(const std::string& json, const char* key) {
	json_error_t error;
	json_t* j = json_loads(json.c_str(), 0, &error);
	REQUIRE(j != nullptr);
	json_t* v = json_object_get(j, key);
	REQUIRE(v != nullptr);
	bool result = json_is_true(v);
	json_decref(j);
	return result;
}

// Bypass the dylib factory — create directly so the injected worker is used
// instead of the module's default async TaskWorker. Tests default to a
// synchronous worker; the perf harness passes a real (async) worker.
static MidiKitModule* createModule(std::shared_ptr<StoermelderPackOne::ITaskWorker> worker = std::make_shared<StoermelderPackOne::SyncTaskWorker>(), std::shared_ptr<StoermelderPackOne::MidiScript::BroadcastBus> bus = nullptr) {
	MidiKitModule* m = new MidiKitModule(std::make_shared<StoermelderPackOne::MidiKit::WorkerDomain>(std::move(worker), std::move(bus)));
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

// RAII owner for modules under test (see Test::ModuleScaffold for why bare
// create/destroy is unsafe once an assertion can fail). Binds to this suite's
// createModule() shadow so every scaffolded module gets the injected
// SyncTaskWorker — the dylib factory (Test::createModule) would use the
// module's default async worker instead.
struct ModuleScaffold : Test::ModuleScaffold<MidiKitModule> {
	ModuleScaffold() : Test::ModuleScaffold<MidiKitModule>([]() { return createModule(); }) {}
};

// A real background worker, for the few tests that must distinguish async
// dispatch from blocking dispatch.
//
// SyncTaskWorker runs every task inline on the calling thread, which makes
// loadScript() (fire-and-forget) and host.unload() (blocking) behave
// identically. That is fine for tests about what a script computes, but it
// erases the very property teardown depends on: that host.unload() has finished
// running onUnload() by the time it returns. Tests asserting on teardown
// ordering must use this instead, or they pass against code that never waits.
static std::shared_ptr<StoermelderPackOne::ITaskWorker> asyncWorker() {
	return std::make_shared<StoermelderPackOne::MpmcTaskWorker>("MidiKit test worker");
}

// Pushes a sentinel task onto the worker and spins until it has run. The worker
// drains its queue FIFO, so once the sentinel runs, every earlier task (which
// may capture the engine's `this`) has finished — the only way to know an async
// loadScript() has landed. Without this, destroying a module while the worker is
// still inside one of its tasks is a use-after-free.
//
// work() uses try_push and returns false when the queue is momentarily full
// (capacity 32). We retry until the sentinel is accepted — the worker keeps
// draining, so this always terminates. Without the retry, a full queue would
// leave the sentinel unqueued and this loop would spin forever.
// Both loops are bounded. An unbounded spin here turns any worker stall into a
// silent hang at 100% CPU with no indication of what went wrong; a deadline
// turns the same stall into a test failure that names the phase it stuck in.
static void barrier(std::shared_ptr<StoermelderPackOne::ITaskWorker> worker, double maxWaitSec = 30.0) {
	auto start = std::chrono::steady_clock::now();
	auto expired = [&]() {
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= maxWaitSec;
	};

	auto done = std::make_shared<std::atomic<bool>>(false);
	// The sentinel is shared, not captured by reference: on timeout this
	// function returns while the task may still be queued, and a reference to a
	// dead stack slot would be written by the worker later.
	while (!worker->work([done]() { done->store(true, std::memory_order_release); })) {
		if (expired()) {
			FAIL("barrier(): worker queue stayed full for " << maxWaitSec << "s — the worker is not draining");
			return;
		}
		std::this_thread::yield();
	}
	while (!done->load(std::memory_order_acquire)) {
		if (expired()) {
			FAIL("barrier(): sentinel task never ran within " << maxWaitSec << "s — the worker is wedged or lost a wakeup");
			return;
		}
		std::this_thread::yield();
	}
}

// The engine's last-published config, as a JSON string — what
// dataToJson() would embed as "scriptConfig" if a save happened right now.
// Under SyncTaskWorker every rack.setConfig() call publishes synchronously
// inline, so this always reflects the most recent one with no round-trip.
static std::string publishedConfigJson(MidiScriptEngine* se) {
	const std::shared_ptr<json_t>& cfg = se->peekConfig();
	if (!cfg) return "";
	char* s = json_dumps(cfg.get(), JSON_COMPACT);
	std::string result = s ? s : "";
	if (s) free(s);
	return result;
}

// Reads one pending message off the module's MIDI out-queue, oldest first —
// the test-side replacement for the removed MidiScriptEngine::processOutMessage().
// The queue moved from the engine to the module so its contents survive engine
// switches and clearScript(); this mirrors the old signature so call sites
// only need m->activeEngine->processOutMessage(...) / engine->processOutMessage(...)
// rewritten to m->processOutMessage(...).
//
// A midiOut.cancel() waits in the same queue. With `cancel` null it is skipped,
// so it never shows up as a message; otherwise it is returned with *cancel =
// true, `msg` the pattern (as queued) and `ticks` 0.
static bool processOutMessage(MidiKitModule* m, int& midiPort, midi::Message& msg, int& ticks, bool* cancel = nullptr) {
	while (!m->midiOuts.queue.empty()) {
		auto t = m->midiOuts.queue.shift();
		if (t.cancel && cancel == nullptr) continue;
		midiPort = t.port;
		msg = t.msg;
		ticks = t.cancel ? 0 : (int)t.tick;
		if (cancel != nullptr) *cancel = t.cancel;
		return true;
	}
	return false;
}

// Drains the module log and returns it as one string.
static std::string drainLog(MidiKitModule* m) {
	std::string all;
	ScriptLog::Entry t;
	while (m->log.tryPop(t)) {
		all += std::get<2>(t) + "\n";
	}
	return all;
}

// Drains the module log and returns (format, text) pairs, preserving order.
// Unlike drainLog(), this keeps the LOG_FORMAT and per-entry structure, which
// the logging tests need to assert on RESET/TIMESTAMP/TEXT and exact counts.
static std::vector<std::tuple<LOG_FORMAT, std::string>> drainLogEntries(MidiKitModule* m) {
	std::vector<std::tuple<LOG_FORMAT, std::string>> out;
	ScriptLog::Entry t;
	while (m->log.tryPop(t)) {
		out.push_back(std::make_tuple(std::get<0>(t), std::get<2>(t)));
	}
	return out;
}


// ── Test harness ─────────────────────────────────────────────────────────
// A reusable rig for new tests (var/MidiKit_test_review.md §7). It sits next to
// the helpers above and nothing in the suite uses it yet; the existing files
// keep their local copies until they are migrated. Everything here is inline
// or a template, so an unused piece costs nothing and warns about nothing.
//
// Per-file namespaces in MidiKit.test.cpp hide a same-named local helper from
// this global one, so a file can be migrated one helper at a time.

// ── Files and text ───────────────────────────────────────────────────────

// Repo root, so paths never depend on the test binary's working directory.
// __FILE__ is repo-root-relative (make passes "$<"); strip the known
// "src/modules/midikit/" suffix, and "." (make's cwd) is correct when nothing is left.
inline std::string repoRoot() {
	static const std::string suffix = "src/modules/midikit/";
	std::string f = __FILE__;
	size_t at = f.rfind(suffix);
	std::string root = (at == std::string::npos) ? "" : f.substr(0, at);
	while (root.size() > 1 && root.back() == '/') root.pop_back();
	return root.empty() ? "." : root;
}

inline std::string readFile(const std::string& path) {
	std::ifstream f(path);
	CATCH_INFO("cannot open " << path);
	REQUIRE(f.good());
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// Number of non-overlapping occurrences of `needle` in `text`.
inline size_t countOf(const std::string& text, const std::string& needle) {
	REQUIRE_FALSE(needle.empty());
	size_t n = 0;
	for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) n++;
	return n;
}

// `text` split at '\n'; a trailing newline does not add an empty last line.
inline std::vector<std::string> lines(const std::string& text) {
	std::vector<std::string> out;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		out.push_back(text.substr(pos, nl - pos));
		pos = nl + 1;
	}
	return out;
}

// ── Languages and scripts ────────────────────────────────────────────────

enum class Lang { Js, Lua };

inline const char* langName(Lang l) { return l == Lang::Js ? "QuickJs" : "Lua"; }

// One Catch leaf per engine: a failure names the engine, and the other still runs.
#define FOR_EACH_LANG Lang lang = GENERATE(Lang::Js, Lang::Lua); CATCH_INFO(langName(lang))

// The header comment that selects the engine. `tags` are extra lines such as "@requires messages=64".
inline std::string header(Lang l, const std::string& tags = "") {
	if (l == Lang::Js) {
		std::string h = "/**\n * @engine QuickJs@v1\n";
		if (!tags.empty()) h += " * " + tags + "\n";
		return h + " */\n";
	}
	std::string h = "--[[\n@engine minilua@v1\n";
	if (!tags.empty()) h += tags + "\n";
	return h + "--]]\n";
}

inline std::string script(Lang l, const std::string& body, const std::string& tags = "") {
	return header(l, tags) + body + "\n";
}

// `body` run for every incoming message, with `port` and `msg` in scope.
inline std::string onMessage(Lang l, const std::string& body) {
	if (l == Lang::Js) return script(l, "midi.onMessage = function(port, msg) {\n" + body + "\n};");
	return script(l, "midi.onMessage = function(port, msg)\n" + body + "\nend");
}

// The script with midiOut.enableTiming() inserted after the header comment.
// `report` passes true, which makes the engine log what it sends.
inline std::string withTiming(const std::string& src, bool report = false) {
	size_t at = src.find("*/\n");
	if (at != std::string::npos) at += 3;
	else {
		at = src.find("--]]\n");
		REQUIRE(at != std::string::npos);
		at += 5;
	}
	bool lua = src.find("minilua") != std::string::npos;
	std::string call = std::string("midiOut.enableTiming(") + (report ? "true" : "") + (lua ? ")\n" : ");\n");
	return src.substr(0, at) + call + src.substr(at);
}

// A script that does nothing, for the engine.
inline const char* EMPTY(Lang l) {
	return l == Lang::Js ? "/**\n * @engine QuickJs@v1\n */\n" : "--[[\n@engine minilua@v1\n--]]\n";
}

// The JS_X / LUA_X constants as one value, so existing scripts stay usable.
struct Pair {
	const char* js;
	const char* lua;
	const char* get(Lang l) const { return l == Lang::Js ? js : lua; }
};

// JavaScript statements as Lua: `let`/`const` become `local` and the semicolons go.
// Only for statements that differ in nothing else.
inline std::string stmt(Lang l, const std::string& jsStatements) {
	if (l == Lang::Js) return jsStatements;
	std::string out;
	for (size_t i = 0; i < jsStatements.size();) {
		if (jsStatements.compare(i, 4, "let ") == 0) { out += "local "; i += 4; }
		else if (jsStatements.compare(i, 6, "const ") == 0) { out += "local "; i += 6; }
		else if (jsStatements[i] == ';') { i++; }
		else out += jsStatements[i++];
	}
	return out;
}

// ── Messages ─────────────────────────────────────────────────────────────
// Channels are Rack's 0-based ones; a script sees them as 1-based.

namespace msg {

inline midi::Message raw(std::initializer_list<uint8_t> bytes) {
	midi::Message m;
	m.setSize(int(bytes.size()));
	int i = 0;
	for (uint8_t b : bytes) m.bytes[i++] = b;
	return m;
}

inline midi::Message noteOn(int ch, int note, int vel) { return raw({uint8_t(0x90 | ch), uint8_t(note), uint8_t(vel)}); }
inline midi::Message noteOff(int ch, int note, int vel = 0) { return raw({uint8_t(0x80 | ch), uint8_t(note), uint8_t(vel)}); }
inline midi::Message cc(int ch, int num, int value) { return raw({uint8_t(0xb0 | ch), uint8_t(num), uint8_t(value)}); }
// A 14-bit pitch wheel value: the LSB goes in the first data byte.
inline midi::Message pitchWheel(int ch, int value14) { return raw({uint8_t(0xe0 | ch), uint8_t(value14 & 0x7f), uint8_t((value14 >> 7) & 0x7f)}); }
inline midi::Message chanPressure(int ch, int v) { return raw({uint8_t(0xd0 | ch), uint8_t(v)}); }
inline midi::Message programChange(int ch, int p) { return raw({uint8_t(0xc0 | ch), uint8_t(p)}); }

inline midi::Message clock() { return raw({0xf8}); }
inline midi::Message start() { return raw({0xfa}); }
inline midi::Message cont() { return raw({0xfb}); }
inline midi::Message stop() { return raw({0xfc}); }

// NRPN / RPN as the four controller messages a sender emits: parameter MSB and LSB, then data MSB and LSB.
inline std::vector<midi::Message> nrpn(int ch, int number, int value) {
	return { cc(ch, 99, number >> 7), cc(ch, 98, number & 0x7f), cc(ch, 6, value >> 7), cc(ch, 38, value & 0x7f) };
}
inline std::vector<midi::Message> rpn(int ch, int number, int value) {
	return { cc(ch, 101, number >> 7), cc(ch, 100, number & 0x7f), cc(ch, 6, value >> 7), cc(ch, 38, value & 0x7f) };
}
// A 14-bit controller: the MSB on `msbCc` (0..31), the LSB on msbCc + 32.
inline std::vector<midi::Message> cc14(int ch, int msbCc, int value) {
	return { cc(ch, msbCc, value >> 7), cc(ch, msbCc + 32, value & 0x7f) };
}
// Both encodings of a key release: a Note-Off and, as most keyboards send it, a Note-On with velocity 0.
inline std::vector<midi::Message> releasesOf(int ch, int note) {
	return { noteOff(ch, note), noteOn(ch, note, 0) };
}

} // namespace msg

// ── Output ───────────────────────────────────────────────────────────────

// One message queued by the module or delivered to a device.
struct Out {
	int port = 0;
	uint8_t status = 0;     // status nibble: 0x9 Note-On, 0xb CC; 0xf for system messages
	uint8_t channel = 0;    // 0-based
	uint8_t note = 0;       // first data byte
	uint8_t value = 0;      // second data byte
	int ticks = 0;          // 0 = send now, N = once the trigger tick counter reaches N
	int64_t frame = -1;     // Message::frame as handed over; -1 = "now"
	int64_t releasedAt = 0; // the process() frame it was delivered on (devices only)
	bool cancel = false;    // a midiOut.cancel() with its pattern in the fields above
	std::vector<uint8_t> bytes;

	Out() {}
	// An expectation without a port: Out(0x9, 1, 60, 100, 0) is a Note-On on channel index 1, sent now.
	// A constructor and not an aggregate, so `{status, channel, note, value, ticks}` lists stay valid
	// without -Wmissing-field-initializers firing.
	Out(uint8_t status, uint8_t channel, uint8_t note, uint8_t value, int ticks, bool cancel = false)
		: status(status), channel(channel), note(note), value(value), ticks(ticks), cancel(cancel) {}

	static Out of(const midi::Message& m, int port = 0, int ticks = 0, bool cancel = false) {
		Out o;
		o.port = port;
		o.status = m.getStatus();
		o.channel = m.getChannel();
		o.note = m.getNote();
		o.value = m.getValue();
		o.ticks = ticks;
		o.frame = m.frame;
		o.cancel = cancel;
		o.bytes.assign(m.bytes.begin(), m.bytes.begin() + m.getSize());
		return o;
	}
	// An expectation: what a test writes, e.g. Out::want(0x9, 0, 60, 100).
	static Out want(uint8_t status, uint8_t channel, uint8_t note, uint8_t value, int ticks = 0, int port = 0) {
		Out o;
		o.port = port;
		o.status = status;
		o.channel = channel;
		o.note = note;
		o.value = value;
		o.ticks = ticks;
		return o;
	}
	// Frame, delivery time and bytes are observations, not part of an expectation.
	bool operator==(const Out& o) const {
		return port == o.port && status == o.status && channel == o.channel && note == o.note
			&& value == o.value && ticks == o.ticks && cancel == o.cancel;
	}
	bool operator!=(const Out& o) const { return !(*this == o); }
};

inline std::ostream& operator<<(std::ostream& os, const Out& o) {
	return os << "{port " << o.port << " status " << int(o.status) << " ch " << int(o.channel) << " " << int(o.note)
	          << " " << int(o.value) << " ticks " << o.ticks << (o.cancel ? " cancel" : "") << "}";
}

// What midi::OutputDevice::sendMessage() really receives. midi::Output::sendMessage() forwards to
// `outputDevice`, so attaching one observes the production path end to end.
struct Device : midi::OutputDevice {
	std::vector<Out> sent;
	std::vector<int64_t> frames;   // Message::frame of each sent message
	std::vector<int> statuses;     // status nibble of each sent message
	int64_t now = 0;   // set by DeviceKit::step()

	void sendMessage(const midi::Message& m) override {
		Out o = Out::of(m);
		o.releasedAt = now;
		sent.push_back(o);
		frames.push_back(m.frame);
		statuses.push_back(m.getStatus());
	}
	// First data bytes of the sent messages with `status` (all of them when -1), in send order.
	std::vector<int> notes(int status = -1) const {
		std::vector<int> out;
		for (const Out& o : sent) if (status < 0 || o.status == status) out.push_back(o.note);
		return out;
	}
	// (first data byte, second data byte) of each sent message: (controller, value) for CCs.
	std::vector<std::pair<int, int>> pairs() const {
		std::vector<std::pair<int, int>> out;
		for (const Out& o : sent) out.push_back(std::make_pair(int(o.note), int(o.value)));
		return out;
	}
	std::vector<int> values() const {
		std::vector<int> out;
		for (const Out& o : sent) out.push_back(o.value);
		return out;
	}
	int count(int status) const {
		int n = 0;
		for (const Out& o : sent) if (o.status == status) n++;
		return n;
	}
};

// ── Modules ──────────────────────────────────────────────────────────────

// Every variant the same way: an injected worker (synchronous by default), a random id, 44.1 kHz.
// MidiKitModule, MultiModule and MidiKitMicroModule all accept a WorkerDomain.
template <typename M = MidiKitModule>
inline M* newModule(std::shared_ptr<StoermelderPackOne::ITaskWorker> worker = std::make_shared<StoermelderPackOne::SyncTaskWorker>(),
                    std::shared_ptr<StoermelderPackOne::MidiScript::BroadcastBus> bus = nullptr) {
	M* m = new M(std::make_shared<StoermelderPackOne::MidiKit::WorkerDomain>(std::move(worker), std::move(bus)));
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

// Replacements for a preset's text: the first occurrence of each `first` becomes `second`.
// Each must be found, so a preset edit cannot silently skip a field.
typedef std::vector<std::pair<std::string, std::string>> Edits;

// The module under test, owned. Destroys it after the test, pass or fail.
template <typename M = MidiKitModule>
struct Kit {
	Test::ModuleScaffold<M> mods;
	M* m;
	int64_t frame = 0;   // monotonic across step()/pumpDivider(): never replays frames

	explicit Kit(std::shared_ptr<StoermelderPackOne::ITaskWorker> worker = std::make_shared<StoermelderPackOne::SyncTaskWorker>(),
	             std::shared_ptr<StoermelderPackOne::MidiScript::BroadcastBus> bus = nullptr)
		: mods([worker, bus]() { return newModule<M>(worker, bus); }) {
		m = mods.create();
	}
	virtual ~Kit() {}

	// ── Loading ──
	// loadRaw() loads and returns the load log, which is drained. For tests about failing loads.
	std::string loadRaw(const std::string& src) {
		m->loadScript(src);
		// The audio thread's half of the load, as the first process() would do it.
		m->syncScriptGen();
		return log();
	}
	// load() REQUIREs a clean load: "Script loaded" and no error.
	Kit& load(const std::string& src) {
		std::string log = loadRaw(src);
		CATCH_INFO("load log:\n" << log);
		REQUIRE(log.find("rror") == std::string::npos);
		REQUIRE(log.find("Script loaded") != std::string::npos);
		return *this;
	}
	// A preset from presets/, relative to the repo root.
	Kit& loadPreset(const std::string& relPath, const Edits& edits = Edits()) {
		std::string src = readFile(repoRoot() + "/" + relPath);
		for (const auto& e : edits) {
			size_t at = src.find(e.first);
			CATCH_INFO("preset " << relPath << ": text not found: " << e.first);
			REQUIRE(at != std::string::npos);
			src.replace(at, e.first.size(), e.second);
		}
		CATCH_INFO("preset: " << relPath);
		return load(src);
	}

	StoermelderPackOne::MidiScript::MidiScriptEngine* engine() {
		REQUIRE(m->host.getActiveEngine() != nullptr);
		return m->host.getActiveEngine();
	}
	Kit& param(int i, float v) { m->params[M::PARAM + i].setValue(v); return *this; }
	Kit& cv(int i, float v, int ch = 0) { m->inputs[M::INPUT_CV + i].setVoltage(v, ch); return *this; }

	// ── Engine layer: no decoder, no divider, no frames ──
	// Queues one message on the engine, runs it and returns what it sent.
	std::vector<Out> dispatch(const midi::Message& in, int port = 0) {
		engine()->processInMessage(port, StoermelderPackOne::MidiScript::QueuedMessage(in));
		engine()->process();
		return drain();
	}
	// A message through the port's decoder (NRPN/RPN/14-bit assembly), then the engine. The decoder
	// state is not the script's enables: use dispatchPumped() to honour a data entry mode.
	std::vector<Out> dispatchDecoded(const midi::Message& in, int port = 0) {
		m->midiIns.ports[port].processor.processMessage(in);
		engine()->process();
		return drain();
	}
	// A message through the real input stage: the queue, the decoder with the script's enables and
	// data entry mode, and the dispatch to the script. No divider or frames.
	std::vector<Out> dispatchPumped(const midi::Message& in, int port = 0) {
		m->midiIns.ports[port].processor.getInput().onMessage(in);
		m->midiIns.process(port);
		engine()->process();
		return drain();
	}
	// One trigger-input tick.
	std::vector<Out> dispatchTick(int trigPort = 0, int ch = 0) {
		engine()->processInTick(trigPort, ch);
		engine()->process();
		return drain();
	}
	// The module's out-queue, oldest first, midiOut.cancel() included.
	std::vector<Out> drain() {
		std::vector<Out> out;
		while (!m->midiOuts.queue.empty()) {
			auto t = m->midiOuts.queue.shift();
			out.push_back(Out::of(t.msg, t.port, t.cancel ? 0 : int(t.tick), t.cancel));
		}
		return out;
	}

	// ── Module layer: the real input queue, decoder, divider and frames ──
	// Queues a message as Rack would deliver it: due at `atFrame` (-1: no frame, due now).
	void inject(midi::Message in, int64_t atFrame = -1, int port = 0) {
		if (atFrame >= 0) in.frame = atFrame;
		m->midiIns.ports[port].processor.getInput().onMessage(in);
	}
	void step(int n = 1) {
		for (int i = 0; i < n; i++) {
			beforeStep(frame);
			m->process(Test::makeProcessArgs(frame));
			frame++;
		}
	}
	void runUntil(int64_t untilFrame) {
		while (frame < untilFrame) step();
	}
	// Enough process() calls to pass the divider once (it is 8), so anything queued is decoded and dispatched.
	void pumpDivider() { step(9); }
	// Sets one channel of one trigger input and processes one divider period.
	void trig(int port, int ch, float v) {
		rack::engine::Input& in = m->inputs[M::INPUT_TRIG + port];
		if (in.getChannels() < ch + 1) in.setChannels(ch + 1);
		in.setVoltage(v, ch);
		pumpDivider();
	}
	// One rising edge, then low again so the next one is an edge.
	void pulse(int port = 0, int ch = 0) {
		trig(port, ch, 10.f);
		trig(port, ch, 0.f);
	}

	// ── Log ──
	std::string log() {
		std::string all;
		ScriptLog::Entry t;
		while (m->log.tryPop(t)) all += std::get<2>(t) + "\n";
		return all;
	}
	// The "P:" lines of the log (prefix stripped), in order.
	std::vector<std::string> probes(const char* prefix = "P:") {
		std::vector<std::string> out;
		size_t n = std::strlen(prefix);
		for (const std::string& l : lines(log())) if (l.compare(0, n, prefix) == 0) out.push_back(l.substr(n));
		return out;
	}
	// Drains the log and REQUIREs it has no error. Returns it.
	std::string requireNoError() {
		std::string l = log();
		CATCH_INFO("log:\n" << l);
		REQUIRE(l.find("rror") == std::string::npos);
		return l;
	}

	// ── Menus ──
	std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem> menus() {
		std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem> out;
		engine()->getContextMenus([&out](const std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem>& specs) { out = specs; });
		engine()->process();
		return out;
	}
	// The callback id of the menu entry labelled `label`; fails when there is none.
	int menuId(const std::string& label) {
		for (const auto& item : menus()) if (item.label == label) return item.callbackId;
		FAIL("no menu entry labelled \"" << label << "\"");
		return -1;
	}
	void click(const std::string& label, const StoermelderPackOne::MidiScript::ScriptMenuClick& c = StoermelderPackOne::MidiScript::ScriptMenuClick()) {
		engine()->invokeContextMenuCallback(menuId(label), c);
		engine()->process();
	}

protected:
	virtual void beforeStep(int64_t) {}

	Kit(const Kit&) = delete;
	Kit& operator=(const Kit&) = delete;
};

// A Kit with a Device on every MIDI output (channel -1). Detaches them before the scaffold
// destroys the module, because onRemove() flushes output through the device.
template <typename M = MidiKitModule>
struct DeviceKit : Kit<M> {
	Device dev[M::MIDI_OUTPUTS];

	explicit DeviceKit(std::shared_ptr<StoermelderPackOne::ITaskWorker> worker = std::make_shared<StoermelderPackOne::SyncTaskWorker>(),
	                   std::shared_ptr<StoermelderPackOne::MidiScript::BroadcastBus> bus = nullptr)
		: Kit<M>(std::move(worker), std::move(bus)) {
		for (int i = 0; i < M::MIDI_OUTPUTS; i++) {
			this->m->midiOuts.ports[i].outputDevice = &dev[i];
			this->m->midiOuts.ports[i].channel = -1;
		}
	}
	~DeviceKit() {
		for (int i = 0; i < M::MIDI_OUTPUTS; i++) this->m->midiOuts.ports[i].outputDevice = nullptr;
	}

protected:
	void beforeStep(int64_t f) override {
		for (int i = 0; i < M::MIDI_OUTPUTS; i++) dev[i].now = f;
	}
};

// ── Cross-engine comparison that pins values ─────────────────────────────

// One engine's observable result for one script run.
struct EngineRun {
	std::vector<Out> sent;
	std::string loadLog;
	std::string log;
};

inline EngineRun runOne(const std::string& src, const midi::Message& in) {
	Kit<> k;
	EngineRun r;
	r.loadLog = k.loadRaw(src);
	CATCH_INFO("load log:\n" << r.loadLog);
	REQUIRE(r.loadLog.find("rror") == std::string::npos);
	r.sent = k.dispatch(in);
	r.log = k.log();
	return r;
}

struct Both {
	EngineRun js, lua;
};

inline Both runBoth(const Pair& p, const midi::Message& in = msg::noteOn(1, 60, 100)) {
	CATCH_INFO("JS:\n" << p.js);
	CATCH_INFO("Lua:\n" << p.lua);
	Both b;
	b.js = runOne(p.js, in);
	b.lua = runOne(p.lua, in);
	return b;
}

// Bodies of midi.onMessage(port, msg) in each language.
inline Both runBoth(const std::string& jsBody, const std::string& luaBody, const midi::Message& in = msg::noteOn(1, 60, 100)) {
	std::string js = onMessage(Lang::Js, jsBody);
	std::string lua = onMessage(Lang::Lua, luaBody);
	CATCH_INFO("JS:\n" << js);
	CATCH_INFO("Lua:\n" << lua);
	Both b;
	b.js = runOne(js, in);
	b.lua = runOne(lua, in);
	return b;
}

// Both engines sent the same port, bytes and ticks, in the same order, and were equally silent in the log.
inline void requireSame(const Both& b) {
	REQUIRE(b.js.log.empty() == b.lua.log.empty());
	REQUIRE(b.js.sent.size() == b.lua.sent.size());
	for (size_t i = 0; i < b.js.sent.size(); i++) {
		CATCH_INFO("message " << i);
		REQUIRE(b.js.sent[i].port == b.lua.sent[i].port);
		REQUIRE(b.js.sent[i].bytes == b.lua.sent[i].bytes);
		REQUIRE(b.js.sent[i].ticks == b.lua.sent[i].ticks);
	}
}

// Both engines sent exactly these wire messages, in this order.
inline void requireBytes(const Both& b, const std::vector<std::vector<uint8_t>>& expected) {
	for (const EngineRun* r : { &b.js, &b.lua }) {
		CATCH_INFO((r == &b.js ? "QuickJs" : "Lua"));
		REQUIRE(r->sent.size() == expected.size());
		for (size_t i = 0; i < expected.size(); i++) {
			CATCH_INFO("message " << i);
			REQUIRE(r->sent[i].bytes == expected[i]);
		}
	}
}

// Both engines sent their messages to these output ports (0-based), in this order.
inline void requirePorts(const Both& b, const std::vector<int>& expected) {
	for (const EngineRun* r : { &b.js, &b.lua }) {
		CATCH_INFO((r == &b.js ? "QuickJs" : "Lua"));
		std::vector<int> got;
		for (const Out& o : r->sent) got.push_back(o.port);
		REQUIRE(got == expected);
	}
}

// Both engines sent messages with these tick delays (0 = now), in this order.
inline void requireTicks(const Both& b, const std::vector<int>& expected) {
	for (const EngineRun* r : { &b.js, &b.lua }) {
		CATCH_INFO((r == &b.js ? "QuickJs" : "Lua"));
		std::vector<int> got;
		for (const Out& o : r->sent) got.push_back(o.ticks);
		REQUIRE(got == expected);
	}
}

// Both engines logged these "P:" values, in this order.
inline void requireLogged(const Both& b, const std::vector<std::string>& expected) {
	for (const EngineRun* r : { &b.js, &b.lua }) {
		CATCH_INFO((r == &b.js ? "QuickJs" : "Lua"));
		std::vector<std::string> got;
		for (const std::string& l : lines(r->log)) if (l.compare(0, 2, "P:") == 0) got.push_back(l.substr(2));
		REQUIRE(got == expected);
	}
}

// ── Fakes ────────────────────────────────────────────────────────────────

// A MidiScriptEngine that does nothing; override only what a test observes. It hands output to the
// real module as its handler, so sendMidi() reaches the module's own out-queue.
struct StubEngine : MidiScriptEngine {
	std::shared_ptr<StoermelderPackOne::MidiScript::WorkerDomain> ownedDomain;

	// `handler`: a module, when what the engine reports should reach its log or out-queue.
	explicit StubEngine(StoermelderPackOne::MidiScript::MidiScriptEngineHandler* handler = nullptr,
	                    int cvInputs = 4, int trigInputs = 1, int trigOutputs = 1, int params = 4, int midiInputs = 1, int midiOutputs = 1)
		: MidiScriptEngine(handler, cvInputs, trigInputs, trigOutputs, params, midiInputs, midiOutputs) {
		useBus(nullptr);
	}
	~StubEngine() {
		if (ownedDomain && ownedDomain->bus) ownedDomain->bus->leave(this);
	}

	// Joins the engine to `bus`. Every engine needs a worker before any dispatch path (host.unload()
	// from onRemove() included) can run, so it keeps a synchronous one.
	void useBus(std::shared_ptr<StoermelderPackOne::MidiScript::BroadcastBus> bus) {
		ownedDomain = std::make_shared<StoermelderPackOne::MidiScript::WorkerDomain>(std::make_shared<StoermelderPackOne::SyncTaskWorker>(), std::move(bus));
		setDomain(ownedDomain.get());
	}

	void loadScriptOnWorker(const char*, const std::string&) override {}
	bool testScript(const std::string&) override { return false; }
	void unloadScriptOnWorker() override {}
	void processInMessage(int, const StoermelderPackOne::MidiScript::QueuedMessage&) override {}
	void processInTick(int, uint8_t, int64_t) override {}
	void dispatchMidiMessage(int, midi::Message&) override {}
	void dispatchNrpn(int, const StoermelderPackOne::MidiScript::QueuedMessage&, bool) override {}
	void dispatchCc14bit(int, const StoermelderPackOne::MidiScript::QueuedMessage&) override {}
	void dispatchTrigger(int, uint8_t) override {}
	void dispatchTipsyMessage(const StoermelderPackOne::MidiScript::TipsyMessage&) override {}
	void dispatchBroadcast(const StoermelderPackOne::MidiScript::InboundBroadcast&) override {}
	std::string getInputName(int) override { return ""; }
	std::string getParamName(int) override { return ""; }
	std::string getParamFormatValue(int) override { return ""; }
	void getContextMenus(const std::function<void(const std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem>&)>& callback) override {
		callback(std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem>());
	}
	void invokeContextMenuCallback(int, const StoermelderPackOne::MidiScript::ScriptMenuClick&) override {}
	bool getMemoryUsage(size_t&, size_t&) override { return false; }
};

// Makes `eng` the module's active engine, and detaches it again before it is destroyed. Declare it
// after the module's owner, so it goes first.
template <typename E>
struct AttachedEngine {
	E eng;
	MidiKitModule* m;
	explicit AttachedEngine(MidiKitModule* m) : eng(m), m(m) { m->host.getActiveEngine() = &eng; }
	// Only while it is still the active engine: a loadScript() in between has installed another.
	~AttachedEngine() { if (m->host.getActiveEngine() == &eng) m->host.getActiveEngine() = nullptr; }
	AttachedEngine(const AttachedEngine&) = delete;
	AttachedEngine& operator=(const AttachedEngine&) = delete;
};

// What the engine reports for its frame counter and block, which are zero without a window.
struct EngineMock : StoermelderPackOne::vcv::EngineAccess {
	int64_t frame = 0;
	int64_t blockFrame = 0;
	int64_t blockFrames = 0;
	int64_t getFrame() const override { return frame; }
	int64_t getBlockFrame() const override { return blockFrame; }
	int64_t getBlockFrames() const override { return blockFrames; }
};

// An EngineMock installed for the scope.
struct EngineScope {
	EngineMock mock;
	Test::mock::Guard<StoermelderPackOne::vcv::EngineAccess> guard{StoermelderPackOne::vcv::engineAccess, &mock};
};

// A worker that collects tasks instead of running them, so a test decides when it gets to them.
struct DeferredWorker : StoermelderPackOne::ITaskWorker {
	std::vector<std::function<void()>> tasks;
	std::atomic<bool> cancel{false};
	bool work(std::function<void()> t) override { tasks.push_back(std::move(t)); return true; }
	bool work(std::function<void()> t, Context*) override { tasks.push_back(std::move(t)); return true; }
	bool work(std::function<void(std::atomic<bool>&)> t) override { tasks.push_back([this, t]() { t(cancel); }); return true; }
	bool work(std::function<void(std::atomic<bool>&)> t, Context*) override { tasks.push_back([this, t]() { t(cancel); }); return true; }
	bool isWorkerThread() const override { return true; }
};


using namespace StoermelderPackOne::MidiScript;

// Helpers, scripts and rigs used by more than one test file.

// Minimal QuickJs script header (body can be empty — the engine still loads it)
static constexpr const char* QUICKJS_SCRIPT =
	"/**\n"
	" * @engine QuickJs@v1\n"
	" */\n";

// Minimal Lua script (synchronously loaded, no body needed)
static constexpr const char* LUA_SCRIPT =
	"--[[\n"
	"@engine minilua@v1\n"
	"--]]\n";


// MidiOutput::processTick — tick-scheduled sends
// midi::Output::sendMessage is non-virtual and no-ops without a subscribed
// device, so a send is not directly observable here. These tests assert on
// queue drainage instead: an entry leaves tickQueue exactly when it is sent,
// which is the property the ">= vs ==" bug turned on.

static midi::Message makeCc() {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	msg.setChannel(0);
	msg.setNote(20);
	msg.setValue(100);
	return msg;
}

// process() ordering and the divider boundary
// The engine interface is virtual, so a recording stub can observe exactly
// which process() calls reach the engine and what frame each one saw. That
// makes the trigger/divider interleaving assertable rather than inferred from
// queue side effects.

struct RecordingEngine : StubEngine {
	int processCalls = 0;
	// Messages to emit via handler->sendMidi() on the next process() call, as
	// (ticks) — one per pending entry, all drained in one call.
	std::vector<int> pending;
	// triggerTick observed at the moment the engine emitted each message.
	std::vector<uint64_t> tickAtEmit;
	// Ordered record of which engine callbacks the module made, for asserting
	// the relative order of trigger/inbound/outbound effects in one process()
	// call. Appended by processInTick/processInMessage/process.
	std::vector<int> events;
	enum Event { TICK = 1, MESSAGE, PROCESS };
	MidiKitModule* module;
	// Everything the module handed over, in order, so tests can assert on the
	// decode result the audio thread produced.
	std::vector<StoermelderPackOne::MidiScript::QueuedMessage> received;

	// Uses the real MidiKitModule as its handler rather than a test double, so
	// sendMidi() reaches the same module out-queue process() drains — the queue
	// is module-owned now, so a double would have to reimplement the thing
	// under test.
	explicit RecordingEngine(MidiKitModule* module) : StubEngine(module), module(module) {}

	void process() override {
		processCalls++;
		events.push_back(PROCESS);
		for (int ticks : pending) {
			midi::Message msg = makeCc();
			handler->sendMidi(0, &msg, 1, 0, ticks);
			tickAtEmit.push_back(module->triggerIns.triggerTick[0][0]);
		}
		pending.clear();
	}
	void processInMessage(int midiPort, const StoermelderPackOne::MidiScript::QueuedMessage& msg) override {
		received.push_back(msg);
		events.push_back(MESSAGE);
	}
	void processInTick(int trigPort, uint8_t channel, int64_t frame) override {
		events.push_back(TICK);
	}
};

// Drives one full sample through process() with the trigger input held at the
// given voltage.
static void step(MidiKitModule* m, float trigVoltage, int64_t frame) {
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(trigVoltage);
	m->process(Test::makeProcessArgs(frame));
}

using StoermelderPackOne::MidiScript::ScriptMenuItem;

// Cross-engine equivalence suite
//
// QuickJs and Lua are two independent ~1000-line implementations of the same
// documented midi.*/midiOut.* API, with nothing structurally holding them in
// agreement. Findings #7 (sendAfterTrigger argument order), #11 (SysEx
// whitespace handling) and #13 (header-tag parsing) were all the same class
// of bug — a behaviour that quietly diverged between engines — found and
// fixed separately, three times. This file is the "shared table-driven test
// suite" option recommended in the review: one list of {script_js, script_lua}
// pairs, run against both engines, asserting the observable output (the sent
// MIDI message and the diagnostic log) is identical.
//
// This does not replace the per-engine test files — it only pins the
// contract *between* them, so a future change to one engine that isn't
// mirrored in the other fails here even if both engines individually still
// pass their own suite.

// Loads the script, drains the load-time log (kept separately so a script
// that logs nothing at runtime doesn't get penalized for load-time chatter
// that has nothing to do with the behaviour under test), feeds one incoming
// message, runs the callback, then drains every pending output message. A
// midiOut.cancel() waiting in the queue is not a message, so it is left out.
static EngineRun run(const std::string& script, const midi::Message& in) {
	EngineRun r = runOne(script, in);
	r.sent.erase(std::remove_if(r.sent.begin(), r.sent.end(), [](const Out& o) { return o.cancel; }), r.sent.end());
	return r;
}

// Default-input overload: most cases don't care what the incoming message
// is, only what the script does once midi.onMessage fires, so a plain NoteOn
// is enough to trigger it.
static EngineRun run(const std::string& script) {
	return run(script, noteOn(1, 60, 100));
}

// Runs both scripts and asserts they produced the same sent messages. Log
// text is intentionally not compared verbatim — the two engines' error
// strings differ in wording (see #13's write-up) — but both must be equally
// silent or equally non-silent, since a divergence there ("one engine warns,
// the other doesn't") is exactly the class of bug this file exists to catch.
static void requireEquivalent(EngineRun js, EngineRun lua) {
	Both b;
	b.js = js;
	b.lua = lua;
	requireSame(b);
}

static void requireEquivalent(const std::string& jsScript, const std::string& luaScript) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);
	requireEquivalent(run(jsScript), run(luaScript));
}

// Same as requireEquivalent, but feeds a caller-supplied input message
// instead of the default NoteOn — for scripts whose behaviour depends on
// what kind of message comes in (e.g. a CC-only reroute).
static void requireEquivalent(const std::string& jsScript, const std::string& luaScript,
                               const midi::Message& in) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);
	requireEquivalent(run(jsScript, in), run(luaScript, in));
}

// Same run/compare shape as requireEquivalent, but for scripts that need to
// assert a specific substring is (or isn't) present in the log — e.g. "did
// this fire the outside-callback warning" — rather than "is the log empty".
// Checks the load log and the runtime log together: a script with no
// midi.onMessage runs entirely at load time (see the outside-callback-warning
// cases below), so restricting the check to the runtime log alone would miss
// it.
static void requireEquivalentLog(const std::string& jsScript, const std::string& luaScript,
                                  const std::string& logContains, bool present) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);

	EngineRun js = run(jsScript);
	EngineRun lua = run(luaScript);

	std::string jsAll = js.loadLog + js.log;
	std::string luaAll = lua.loadLog + lua.log;
	CATCH_INFO("JS log:\n" << jsAll);
	CATCH_INFO("Lua log:\n" << luaAll);
	REQUIRE((jsAll.find(logContains) != std::string::npos) == present);
	REQUIRE((luaAll.find(logContains) != std::string::npos) == present);
}

// Many "API getter"-style cases only need to prove a value a script computed
// at top level (or read from a getter) is the same across engines — there is
// no MIDI message to send. Both engines expose an identical log(string)
// global, and both format numbers identically via number.toString (see "API
// number.toString" in the per-engine files), so a script that logs each
// value under test turns "read this internal value" into the same kind of
// comparable, engine-agnostic side channel processInMessage/processOutMessage
// gives requireEquivalent. The script runs at load time (no midi.onMessage
// needed), so this bypasses run()'s incoming-NoteOn feed entirely.
//
// loadScript() itself writes framework chatter to the same log ("Script
// loaded", "No midi.onMessage(...) defined", ...), which would otherwise leak
// into the comparison. Probe scripts prefix every value they log with
// PROBE_PREFIX so loadAndDrainLog can pull out exactly the lines under test
// and nothing else, rather than trying to blacklist framework wording (which
// differs between engines anyway — see #13's write-up).
static const char* PROBE_PREFIX = "PROBE:";

static std::vector<std::string> loadAndDrainLog(const std::string& script) {
	MidiKitModule* m = createModule();
	m->loadScript(script);
	std::string log = drainLog(m);
	Test::destroyModule(m);

	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos < log.size()) {
		size_t nl = log.find('\n', pos);
		if (nl == std::string::npos) break;
		std::string line = log.substr(pos, nl - pos);
		if (line.rfind(PROBE_PREFIX, 0) == 0) {
			lines.push_back(line.substr(strlen(PROBE_PREFIX)));
		}
		pos = nl + 1;
	}
	return lines;
}

// Compares the two scripts' logged lines directly against an expected list —
// not against each other — so a test can assert *what* the value is, not
// merely that both engines agree (two engines agreeing on a wrong answer
// would otherwise pass silently).
static void requireLoggedValues(const std::string& jsScript, const std::string& luaScript,
                                 const std::vector<std::string>& expected) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);

	std::vector<std::string> js = loadAndDrainLog(jsScript);
	std::vector<std::string> lua = loadAndDrainLog(luaScript);

	REQUIRE(js == expected);
	REQUIRE(lua == expected);
}


static const char* JS_NO_ON_LOAD = R"(/**
 * @engine QuickJs@v1
 */
let x = Math.max(3, 7);
)";

static const char* LUA_NO_ON_LOAD = R"(--[[
@engine minilua@v1
--]]
x = math.max(3, 7)
)";


// onUnload

static const char* JS_ON_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.onUnload = function() {
    rack.log("onUnload ran");
    let msg = midi.create();
    midi.setNoteOff(msg, 1, 60);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_UNLOAD = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.onUnload = function()
    rack.log("onUnload ran")
    local msg = midi.create()
    midi.setNoteOff(msg, 1, 60)
    midiOut.send(msg)
end
)";

// Preset metadata.
//
// PRESETS[] is the single table every behavioural preset is listed in: its
// subfolder ("" for top-level, "creative/" for the creative subfolder) and its
// script stem. Both the smoke test and every behavioural test derive their
// paths from this one table via presetPath(), so the creative-subfolder
// location can't drift. The trivial one-branch presets (PassThrough, Filter
// Ch2, ...) are deliberately absent: they have nothing to regress and some
// drop everything by design, so the "produced output" check wouldn't apply.
struct PresetInfo {
	const char* subfolder;  // "" for top-level, "creative/" for the creative subfolder
	const char* name;       // script stem without extension
	bool midiDriven;        // false for trigger-clocked presets (Arpeggiator) that
	                        // produce no output from plain MIDI traffic
};
static const PresetInfo PRESETS[] = {
	{"", "MPE to single channel", true},
	{"", "Clock divider", true},
	{"", "Clock multiplier", false},   // trigger-clocked; emits nothing for MIDI traffic
	{"", "Note length quantiser", true},
	{"", "Velocity curve", true},
	{"", "Scale quantiser", true},
	{"", "Chord harmonizer", true},
	{"", "NRPN to CC", true},
	{"", "NRPN to CC (assembled)", true},
	{"", "NRPN Generator", true},
	{"basic/", "Copy Ch1 CC to Ch2", true},
	{"basic/", "Rewrite Ch1 to Ch2", true},
	{"basic/", "Monitor", true},
	{"basic/", "Delay NoteOn Ch1 for two 1500ms", false},   // channel 1 only; the smoke traffic sends on channels 2 and 3
	{"basic/", "Delay NoteOn Ch1 for two clock ticks", false},   // same
	{"", "Micro scale", true},
	{"", "Arpeggiator", false},   // trigger-clocked; emits nothing for MIDI traffic
	{"", "Volca Sample", true},
	{"", "Program Change Trigger", true},
	{"", "Program Change CV", true},
	{"", "Bank Select (param)", true},
	{"", "Bank Select (menu)", false},   // sends only from menu clicks (own test); no reaction to MIDI traffic
	{"", "Channel router", true},
	{"", "Smart merge", true},
	{"", "Port router", true},
	{"creative/", "Euclidean rhythm generator", true},
	{"creative/", "Keyboard split", true},
	{"creative/", "Bouncing ball delay", true},
	{"creative/", "Gravity well", true},
	{"", "Transport broadcaster", false},   // trigger-clocked; broadcasts instead of sending MIDI
	{"", "Transport follower", false}      // driven by broadcasts, not MIDI
};

// The two engine variants every preset ships in. Behavioural tests iterate
// this once per TEST_CASE; subfolder and stem always come from PRESETS[].
static const char* ENGINES[] = {"JavaScript", "Lua"};

static std::string presetPath(const PresetInfo& p, const char* engine) {
	std::string ext = (std::string(engine) == "JavaScript") ? ".js" : ".lua";
	return std::string("presets/MidiKit/") + engine + "/" + p.subfolder + p.name + ext;
}

// Program Change Trigger: poly trigger channel N (1-16) sends the Program
// Change configured for N. The trigger is dispatched to the engine per
// channel, exactly as the module does for a rising edge on a poly channel.


// @requires params=N: a script that needs more params than the variant has is
// refused with a message instead of loading and failing later.
static std::string requiresScript(bool lua, const std::string& tag) {
	std::string body = lua ? "rack.onLoad = function() rack.log('onload-ran') end"
	                       : "rack.onLoad = function() { rack.log('onload-ran'); };";
	if (lua) return "--[[\n@engine minilua@v1\n@requires " + tag + "\n--]]\n" + body + "\n";
	return "/**\n * @engine QuickJs@v1\n * @requires " + tag + "\n */\n" + body + "\n";
}

// Module variants: MidiKitModuleBase/MidiKitWidgetBase instantiated with other
// port counts than the original MidiKit. The rest of the suite only exercises
// the 1-in/1-out config, so multi-port routing and the MidiKitMicro variant are
// covered here.

// 3 CV inputs, 1 param, 2 trigger in/out, 2 MIDI in/out.
struct MultiConfig {
	static constexpr int cvInputs = 3;
	static constexpr int trigInputs = 2;
	static constexpr int trigOutputs = 2;
	static constexpr int params = 1;
	static constexpr int midiInputs = 2;
	static constexpr int midiOutputs = 2;
};
using MultiModule = MidiKitModuleBase<MultiConfig>;

static const char* QUICKJS_EMPTY =
	"/**\n"
	" * @engine QuickJs@v1\n"
	" */\n";

// ── MIDI input routing ──────────────────────────────────────────────────────

static const char* JS_PORT_PROBE = R"(/**
 * @engine QuickJs@v1
 */
midi.enablePorts(2);
midi.onMessage = function(midiPort, msg) {
    rack.log("P:" + midiPort);
};
)";

static const char* JS_PORTS_OUT = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
midi.onMessage = function(midiPort, msg) {
    midiOut.selectPort(2);
    midiOut.sendAfterTrigger(msg, 5);
};
)";

// Frame-accuracy harness for MIDI-KIT output timing.
//
// These tests answer the question no other suite asks: "on which sample did a
// message leave, and which frame did it carry?". MidiKit.test.module.hpp tests
// queue mechanics, MidiKit.perf.cpp measures wall-clock worker latency.
//
// The seam is a midi::OutputDevice attached to the module's real output.
// midi::Output::sendMessage() is non-virtual and forwards to `outputDevice`, so
// the recorder observes the production path end to end and sees the `frame`
// field exactly as Rack's RtMidiOutputDevice would — a recording subclass of
// MidiOutput would observe the module's intent one call earlier and could not
// see what the last step does to the frame.
//
// STATUS QUO: every test below pins what the module does TODAY, so the suite is
// green and a later step shows up as a deliberate, reviewed flip of one
// assertion. Each such assertion is marked "status quo". Everything is exact integer arithmetic over a
// synthetic frame counter — nothing here is wall-clock.
//
// Run alone: ./build/test/MidiKit.test "[timing]"

// What reaches the device: `frame` is the Message::frame field as the device sees it (-1 = "now"),
// `releasedAt` the engine frame of the process() call that sent it.
typedef Device TimingRecorder;

// Steps one module sample by sample with a synthetic frame counter. Frames
// start at 0 and advance by one per process() call, which is what Rack does
// within a block, so the module's divider phase is a pure function of frame.
struct TimingRig : DeviceKit<> {
	Device& rec;

	explicit TimingRig(const char* script) : rec(dev[0]) {
		m->loadScript(script);
	}

	void run(int64_t untilFrame) { runUntil(untilFrame); }
};

static const char* JS_PASS_THROUGH = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.send(msg);
};
)";

static const char* LUA_PASS_THROUGH = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    midiOut.send(msg)
end
)";

static const Pair TIMING_SCRIPT{JS_PASS_THROUGH, LUA_PASS_THROUGH};

// The module's divider period. A message due at frame N is popped, dispatched
// and drained on the first divider tick at or after N.
static constexpr int64_t DIVIDER = 8;

// The first divider tick at or after `frame`, given the rig starts at frame 0:
// dsp::ClockDivider fires on every 8th call, i.e. frames 7, 15, 23, ...
static int64_t nextDividerTick(int64_t frame) {
	return ((frame + 1 + DIVIDER - 1) / DIVIDER) * DIVIDER - 1;
}

static std::vector<int> controllers(const TimingRecorder& rec) {
	std::vector<int> result;
	for (auto& s : rec.sent) result.push_back(s.note);
	return result;
}

// ── Timing mode (midiOut.enableTiming()) ────────────────────────────────────
// The scripts above with the opt-in prepended; the legacy tests stay as they are.

// Inserts midiOut.enableTiming() after the header comment, for either engine.
static std::string withTiming(const char* script, const char* arg = "") {
	std::string s = script;
	size_t at = s.find("*/\n");
	if (at != std::string::npos) at += 3;
	else {
		at = s.find("--]]\n");
		REQUIRE(at != std::string::npos);
		at += 5;
	}
	bool lua = s.find("minilua") != std::string::npos;
	std::string call = std::string("midiOut.enableTiming(") + arg + (lua ? ")\n" : ");\n");
	return s.substr(0, at) + call + s.substr(at);
}

// What Rack's unstable queue needs: a frame on every message, strictly increasing.
static void requireOrderedFrames(const TimingRecorder& rec) {
	int64_t last = -1;
	for (auto& s : rec.sent) {
		REQUIRE(s.frame >= 0);
		REQUIRE(s.frame > last);
		last = s.frame;
	}
}

// The note numbers of everything the recorder got, in send order.
static std::vector<int> sentNotes(const TimingRecorder& rec) {
	std::vector<int> notes;
	for (const Out& s : rec.sent) notes.push_back(s.note);
	return notes;
}

// Frames to run so that a 10 ms delay scheduled at about frame 20 is long over.
static int64_t cancelRunUntil() {
	return 100 + 2 * int64_t(0.010 * Test::sampleRate());
}
