#pragma once
#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"

using namespace StoermelderPackOne::MidiKit;
using StoermelderPackOne::MidiScript::MidiScriptEngine;

SYNC_MODEL(modelMidiKit, "MidiKit");
Test::TestContext<> testContext;

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

// Drains the module log and returns it as one string.
static std::string drainLog(MidiKitModule* m) {
	std::string all;
	std::tuple<LOG_FORMAT, float, std::string> t;
	while (m->midiLogMessages.try_pop(t)) {
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
	while (m->midiLogMessages.try_pop(t)) {
		out.push_back(std::make_tuple(std::get<0>(t), std::get<2>(t)));
	}
	return out;
}