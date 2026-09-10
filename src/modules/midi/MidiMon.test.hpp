#pragma once

// Shared preamble for the MIDIMON test suite.
// Included by MidiMon.test.cpp, which pulls the test cases in from MidiMon.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiMon.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiMon;

static Test::TestContext<> testContext;

typedef MessageEx::Type MType;

// Build a MessageEx of the given type from a raw MIDI message.
static MessageEx makeEx(MType type, const rack::midi::Message& msg) {
	MessageEx m(msg);
	m.type = type;
	return m;
}

// Pop all pending log entries out of the module's ring buffer.
static std::vector<LogEntry> drain(MidiMonModule* module) {
	std::vector<LogEntry> out;
	while (!module->midiLogMessages.empty()) {
		out.push_back(module->midiLogMessages.shift());
	}
	return out;
}

static std::string textOf(const LogEntry& e) { return std::get<3>(e); }
static LOG_FORMAT formatOf(const LogEntry& e) { return std::get<0>(e); }