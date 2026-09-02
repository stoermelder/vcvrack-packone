#pragma once
#include "../../test/framework.hpp"
#include "MidiKit.cpp"

using namespace StoermelderPackOne::MidiKit;
using StoermelderPackOne::MidiScript::MidiScriptEngine;

SYNC_MODEL(modelMidiKit, "MidiKit");
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
static MidiKitModule* createModule(std::shared_ptr<StoermelderPackOne::ITaskWorker> worker = std::make_shared<StoermelderPackOne::SyncTaskWorker>()) {
	MidiKitModule* m = new MidiKitModule(std::move(worker));
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

// A real background worker, for the few tests that must distinguish async
// dispatch from blocking dispatch.
//
// SyncTaskWorker runs every task inline on the calling thread, which makes
// loadScript() (fire-and-forget) and closeState() (blocking) behave
// identically. That is fine for tests about what a script computes, but it
// erases the very property teardown depends on: that closeState() has finished
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

// captureConfig() as a plain value, for tests asserting on the config a script
// produced. REQUIREs a true return: under SyncTaskWorker the task always runs
// inline, so false means the dispatch itself failed and should fail the test
// loudly rather than read as an empty config. Nothing to persist (no script, or
// no onSave()) returns true with an empty string and is fine here.
static std::string captureConfig(MidiScriptEngine* se) {
	std::string out;
	REQUIRE(se->captureConfig(out));
	return out;
}

// Reads one pending message off the module's MIDI out-queue, oldest first —
// the test-side replacement for the removed MidiScriptEngine::processOutMessage().
// The queue moved from the engine to the module so its contents survive engine
// switches and clearScript(); this mirrors the old signature so call sites
// only need m->activeEngine->processOutMessage(...) / engine->processOutMessage(...)
// rewritten to m->processOutMessage(...).
static bool processOutMessage(MidiKitModule* m, int& midiPort, midi::Message& msg, int& ticks) {
	if (m->midiOutQueue.empty()) return false;
	auto t = m->midiOutQueue.shift();
	midiPort = std::get<0>(t);
	msg = std::get<1>(t);
	ticks = (int)std::get<3>(t);
	return true;
}

// Drains the module log and returns it as one string.
static std::string drainLog(MidiKitModule* m) {
	std::string all;
	std::tuple<LOG_FORMAT, float, std::string> t;
	while (m->log.midiLogMessages.try_pop(t)) {
		all += std::get<2>(t) + "\n";
	}
	return all;
}

// Drains the module log and returns (format, text) pairs, preserving order.
// Unlike drainLog(), this keeps the LOG_FORMAT and per-entry structure, which
// the logging tests need to assert on RESET/TIMESTAMP/TEXT and exact counts.
static std::vector<std::tuple<LOG_FORMAT, std::string>> drainLogEntries(MidiKitModule* m) {
	std::vector<std::tuple<LOG_FORMAT, std::string>> out;
	std::tuple<LOG_FORMAT, float, std::string> t;
	while (m->log.midiLogMessages.try_pop(t)) {
		out.push_back(std::make_tuple(std::get<0>(t), std::get<2>(t)));
	}
	return out;
}