#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiMon.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiMon;

SYNC_MODEL(modelMidiMon, "MidiMon");
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


TEST_CASE("Construction and reset", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");

	SECTION("Default visibility flags after reset") {
		REQUIRE(module->showNoteMsg == true);
		REQUIRE(module->showKeyPressure == true);
		REQUIRE(module->showCcMsg == true);
		REQUIRE(module->showCcExMsg == true);
		REQUIRE(module->showRpnNrpnMsg == false);
		REQUIRE(module->showProgChangeMsg == true);
		REQUIRE(module->showChannelPressurelMsg == true);
		REQUIRE(module->showPitchWheelMsg == true);
		REQUIRE(module->showSysExMsg == false);
		REQUIRE(module->showSysExData == false);
		REQUIRE(module->showClockMsg == false);
		REQUIRE(module->showSystemMsg == true);
		REQUIRE(module->showFrame == false);
	}

	SECTION("Construction seeds the log with timestamp header lines") {
		// onReset() -> logTimestampReset() pushes two header entries.
		auto entries = drain(module);
		REQUIRE(entries.size() == 2);
		REQUIRE(formatOf(entries[0]) == LOG_FORMAT::TIMESTAMP);
		REQUIRE(textOf(entries[1]).find("sample rate") != std::string::npos);
	}

	Test::destroyModule(module);
}

TEST_CASE("Preset JSON null-guards", "[MidiMon][JSON]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


TEST_CASE("Logs and formats channel messages", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module); // discard header lines

	SECTION("Note on (channel is displayed 1-based)") {
		module->processMidi(makeEx(MType::NOTE_ON, Test::makeMidiMessage(0x9, 4, 60, 100)));
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch05 note on  60 vel 100");
		REQUIRE(formatOf(entries[0]) == LOG_FORMAT::TIMESTAMP);
	}

	SECTION("Note off") {
		module->processMidi(makeEx(MType::NOTE_OFF, Test::makeMidiMessage(0x8, 0, 60, 0)));
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 note off 60 vel 0");
	}

	SECTION("Control change") {
		module->processMidi(makeEx(MType::CC, Test::makeMidiMessage(0xb, 0, 7, 64)));
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 cc7=64");
	}

	SECTION("Program change") {
		module->processMidi(makeEx(MType::PROGRAM_CHANGE, Test::makeMidiMessage(0xc, 2, 9, 0)));
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch03 program=9");
	}

	SECTION("14-bit CC uses indented format") {
		auto m = makeEx(MType::CC_14BIT, Test::makeMidiMessage(0xb, 0, 7, 0));
		m.extraValue = 1234;
		module->processMidi(m);
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(formatOf(entries[0]) == LOG_FORMAT::INDENTED);
		REQUIRE(textOf(entries[0]) == "ch01 14-bit cc7=1234");
	}

	Test::destroyModule(module);
}


TEST_CASE("Logs NRPN messages with full 14-bit value", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module); // discard header lines
	module->showRpnNrpnMsg = true;
	module->showCcExMsg = false; // isolate NRPN output from the 14-bit CC log

	// Select NRPN: CC 99 (MSB) then CC 98 (LSB) -> param = 4*128 + 5 = 517
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 99, 4));
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 98, 5));

	// Data entry: CC 6 (MSB) then CC 38 (LSB) -> value = 20*128 + 2 = 2562
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 6, 20));
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 38, 2));

	auto entries = drain(module);

	// Selection logs "selected"; data entry logs the full 14-bit value.
	REQUIRE(entries.size() == 2);
	REQUIRE(textOf(entries[0]) == "ch04 nrpn param=517 selected");
	REQUIRE(textOf(entries[1]) == "ch04 nrpn param=517 value=2562");
	REQUIRE(formatOf(entries[1]) == LOG_FORMAT::INDENTED);

	Test::destroyModule(module);
}


TEST_CASE("NRPN single-byte data entry", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module);
	module->showRpnNrpnMsg = true;
	module->showCcExMsg = false;

	// Select NRPN: CC 99 (MSB) then CC 98 (LSB)
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 99, 4));
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 98, 5));

	// Data entry as a single CC 6 (data entry MSB only), value 100.
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 6, 100));

	auto entries = drain(module);
	// Only the selection entry is logged; a lone CC 6 does not emit an NRPN value.
	REQUIRE(entries.size() == 1);
	REQUIRE(textOf(entries[0]) == "ch04 nrpn param=517 selected");

	Test::destroyModule(module);
}


TEST_CASE("NRPN LSB-only data entry", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module);
	module->showRpnNrpnMsg = true;
	module->showCcExMsg = false;

	// Select NRPN: CC 99 (MSB) then CC 98 (LSB)
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 99, 4));
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 98, 5));

	// Data entry as a single CC 38 (data entry LSB only), value 100.
	module->midiProcessor.processCc(Test::makeMidiMessage(0xb, 3, 38, 100));

	auto entries = drain(module);
	// LSB-only data entry caps the value at 0-127 (no MSB was sent).
	REQUIRE(entries.size() == 2);
	REQUIRE(textOf(entries[0]) == "ch04 nrpn param=517 selected");
	REQUIRE(textOf(entries[1]) == "ch04 nrpn param=517 value=100");

	Test::destroyModule(module);
}


TEST_CASE("Respects visibility flags", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module);

	SECTION("Disabled note messages are dropped") {
		module->showNoteMsg = false;
		module->processMidi(makeEx(MType::NOTE_ON, Test::makeMidiMessage(0x9, 0, 60, 100)));
		REQUIRE(drain(module).empty());
	}

	SECTION("Disabled CC messages are dropped") {
		module->showCcMsg = false;
		module->processMidi(makeEx(MType::CC, Test::makeMidiMessage(0xb, 0, 7, 64)));
		REQUIRE(drain(module).empty());
	}

	SECTION("Clock is hidden by default but shown when enabled") {
		module->processMidi(makeEx(MType::CLOCK, Test::makeMidiMessage(0xf, 0x8, 0, 0)));
		REQUIRE(drain(module).empty());

		module->showClockMsg = true;
		module->processMidi(makeEx(MType::CLOCK, Test::makeMidiMessage(0xf, 0x8, 0, 0)));
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "clock tick");
	}

	Test::destroyModule(module);
}


TEST_CASE("Logs system real-time messages", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module);
	REQUIRE(module->showSystemMsg == true);

	struct Case { MType type; const char* text; };
	auto c = GENERATE(
		Case{ MType::START, "start" },
		Case{ MType::CONTINUE, "continue" },
		Case{ MType::STOP, "stop" },
		Case{ MType::RESET, "reset" });

	module->processMidi(makeEx(c.type, Test::makeMidiMessage(0xf, 0, 0, 0)));
	auto entries = drain(module);
	REQUIRE(entries.size() == 1);
	REQUIRE(textOf(entries[0]) == std::string(c.text));

	// With system messages disabled nothing is logged.
	module->showSystemMsg = false;
	module->processMidi(makeEx(c.type, Test::makeMidiMessage(0xf, 0, 0, 0)));
	REQUIRE(drain(module).empty());

	Test::destroyModule(module);
}


TEST_CASE("SysEx logging", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module);

	rack::midi::Message sysex;
	sysex.bytes = { 0xf0, 0x7e, 0x7f, 0xf7 }; // 4 bytes -> 2 "data" bytes (size - 2)

	SECTION("Summary only when data display is off") {
		module->showSysExMsg = true;
		module->showSysExData = false;
		module->processMidi(makeEx(MType::SYSEX, sysex));
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "sysex (2 data bytes)");
	}

	SECTION("Summary plus hex dump when data display is on") {
		module->showSysExMsg = true;
		module->showSysExData = true;
		module->processMidi(makeEx(MType::SYSEX, sysex));
		auto entries = drain(module);
		REQUIRE(entries.size() == 2);
		REQUIRE(textOf(entries[0]) == "sysex (2 data bytes)");
		REQUIRE(formatOf(entries[1]) == LOG_FORMAT::TEXT);
		REQUIRE(textOf(entries[1]).find("f0") != std::string::npos);
		REQUIRE(textOf(entries[1]).find("f7") != std::string::npos);
	}

	SECTION("Nothing logged when SysEx display is off") {
		module->showSysExMsg = false;
		module->showSysExData = false;
		module->processMidi(makeEx(MType::SYSEX, sysex));
		REQUIRE(drain(module).empty());
	}

	Test::destroyModule(module);
}


TEST_CASE("processBypass drains the MIDI queue without logging", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	drain(module); // discard header lines

	module->midiProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	REQUIRE(module->midiProcessor.getInput().size() == 1);

	module->processBypass(Test::makeProcessArgs(1));

	REQUIRE(module->midiProcessor.getInput().size() == 0);
	REQUIRE(drain(module).empty());

	Test::destroyModule(module);
}

// Pumps the module until its process divider has certainly fired, so queued
// MIDI is actually decoded. The divider is seeded randomly by setDivision(),
// so one extra full division guarantees at least one tick.
static void pump(MidiMonModule* module, int64_t& frame) {
	for (uint32_t i = 0; i < module->processDivider.getDivision() + 1; i++) {
		module->process(Test::makeProcessArgs(frame++));
	}
}

TEST_CASE("onReset clears NRPN state so data entry cannot resume", "[MidiMon][reset]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	module->showRpnNrpnMsg = true;
	int64_t frame = 1;

	// Arm an NRPN parameter (CC 99 then CC 98) and let it decode.
	module->midiProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 99, 4));
	module->midiProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 98, 5));
	pump(module, frame);
	drain(module);

	Module::ResetEvent re;
	module->onReset(re);
	drain(module); // discard the fresh timestamp header

	// Data entry after the reset must not be attributed to the old parameter.
	module->showRpnNrpnMsg = true;
	module->midiProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 6, 20));
	module->midiProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 38, 2));
	pump(module, frame);

	for (auto& e : drain(module)) {
		CATCH_INFO("logged: " << textOf(e));
		REQUIRE(textOf(e).find("nrpn") == std::string::npos);
	}

	Test::destroyModule(module);
}

TEST_CASE("onReset clears 14-bit CC state so an orphan LSB is not paired", "[MidiMon][reset]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	module->showCcExMsg = true;
	int64_t frame = 1;

	// Store a 14-bit MSB, then reset before the matching LSB arrives.
	module->midiProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 5, 3));
	pump(module, frame);
	drain(module);

	Module::ResetEvent re;
	module->onReset(re);
	drain(module);

	module->showCcExMsg = true;
	module->midiProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 32 + 5, 10));
	pump(module, frame);

	for (auto& e : drain(module)) {
		CATCH_INFO("logged: " << textOf(e));
		REQUIRE(textOf(e).find("14-bit") == std::string::npos);
	}

	Test::destroyModule(module);
}

TEST_CASE("RPN/NRPN select and data entry render differently", "[MidiMon]") {
	// Pins the hasValue() predicate that chooses between the "selected" and
	// "value=" forms. extraValue < 0 means the notification carries a parameter
	// number only; >= 0 means data entry supplied a reading.
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	module->showRpnNrpnMsg = true;
	drain(module);

	SECTION("NRPN select has no value") {
		auto m = makeEx(MType::NRPN, Test::makeMidiMessage(0xb, 0, 98, 5));
		m.paramNumber = 517;
		module->processMidi(m);
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 nrpn param=517 selected");
	}

	SECTION("NRPN data entry reports the value") {
		auto m = makeEx(MType::NRPN, Test::makeMidiMessage(0xb, 0, 38, 2));
		m.paramNumber = 517;
		m.extraValue = 2562;
		module->processMidi(m);
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 nrpn param=517 value=2562");
	}

	SECTION("A zero value is still a value, not a select") {
		// The boundary the predicate turns on: 0 is a legitimate reading.
		auto m = makeEx(MType::NRPN, Test::makeMidiMessage(0xb, 0, 38, 0));
		m.paramNumber = 517;
		m.extraValue = 0;
		module->processMidi(m);
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 nrpn param=517 value=0");
	}

	SECTION("RPN select falls through to the named-parameter text") {
		auto m = makeEx(MType::RPN, Test::makeMidiMessage(0xb, 0, 100, 0));
		m.paramNumber = 0;
		module->processMidi(m);
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 rpn param=0 (Pitch Bend Sensitivity)");
	}

	SECTION("RPN data entry reports the value instead of the name") {
		auto m = makeEx(MType::RPN, Test::makeMidiMessage(0xb, 0, 38, 7));
		m.paramNumber = 0;
		m.extraValue = 1287;
		module->processMidi(m);
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 rpn param=0 value=1287");
	}

	SECTION("The RPN reset notification outranks both") {
		auto m = makeEx(MType::RPN, Test::makeMidiMessage(0xb, 0, 100, 127));
		m.paramNumber = -1;
		module->processMidi(m);
		auto entries = drain(module);
		REQUIRE(entries.size() == 1);
		REQUIRE(textOf(entries[0]) == "ch01 rpn/nrpn reset");
	}

	Test::destroyModule(module);
}

TEST_CASE("processMidi never consumes the message", "[MidiMon]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	// Returning false keeps the message available to other handlers.
	REQUIRE(module->processMidi(makeEx(MType::NOTE_ON, Test::makeMidiMessage(0x9, 0, 60, 1))) == false);
	REQUIRE(module->processMidi(makeEx(MType::CLOCK, Test::makeMidiMessage(0xf, 0x8, 0, 0))) == false);
	Test::destroyModule(module);
}


TEST_CASE("JSON round-trip", "[MidiMon][JSON]") {
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	module->panelTheme = 1;
	module->showNoteMsg = false;
	module->showCcMsg = false;
	module->showCcExMsg = true;
	module->showRpnNrpnMsg = true;
	module->showClockMsg = true;
	module->showSysExMsg = true;
	module->showSysExData = true;
	module->showSystemMsg = false;
	module->showFrame = true;

	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);

	auto restored = Test::createModule<MidiMonModule>("MidiMon");
	restored->dataFromJson(rootJ);

	REQUIRE(restored->panelTheme == 1);
	REQUIRE(restored->showNoteMsg == false);
	REQUIRE(restored->showCcMsg == false);
	REQUIRE(restored->showCcExMsg == true);
	REQUIRE(restored->showRpnNrpnMsg == true);
	REQUIRE(restored->showClockMsg == true);
	REQUIRE(restored->showSysExMsg == true);
	REQUIRE(restored->showSysExData == true);
	REQUIRE(restored->showSystemMsg == false);
	REQUIRE(restored->showFrame == true);

	json_decref(rootJ);
	Test::destroyModule(module);
	Test::destroyModule(restored);
}


TEST_CASE("Legacy preset defaults showCcExMsg to showCcMsg", "[MidiMon][JSON]") {
	// Older presets had no "showCcExMsg" key; it should inherit showCcMsg.
	auto module = Test::createModule<MidiMonModule>("MidiMon");

	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "showCcMsg", json_boolean(false));
	module->dataFromJson(rootJ);
	REQUIRE(module->showCcMsg == false);
	REQUIRE(module->showCcExMsg == false);

	json_decref(rootJ);
	Test::destroyModule(module);
}
