#include "../../test/framework.hpp"
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

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves state", "[MidiMon][JSON]") {
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


// A UiAccess mock that records saveDialog and message calls and returns scripted answers.
struct MockUiAccess : vcv::UiAccess {
	struct SaveCall { std::string filters, dir, filename; };
	std::vector<SaveCall> saveCalls;
	std::vector<std::string> saveResults;  // queue consumed in order
	int saveIndex = 0;

	struct Message { vcv::MessageType type; vcv::MessageButtons buttons; std::string msg; };
	std::vector<Message> messages;

	std::string saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) override {
		saveCalls.push_back({filters, dir, filename});
		if (saveIndex < (int) saveResults.size()) return saveResults[saveIndex++];
		return "";
	}

	bool message(vcv::MessageType type, vcv::MessageButtons buttons, const std::string& msg) override {
		messages.push_back({type, buttons, msg});
		return true;
	}
};

// A FileAccess mock that records write() calls; failWrites forces the failure path.
// Path helpers (getDirectory/getFilename) forward to rack::system so exportLogDialog
// computes the real default filename ("MidiMon.log") for the save dialog.
struct MockFileAccess : Test::mock::MockFileAccess {
	struct WriteCall { std::string path, data; };
	std::vector<WriteCall> writes;
	bool failWrites = false;

	bool write(const std::string& path, const std::string& data) override {
		if (failWrites) return false;
		writes.push_back({path, data});
		return true;
	}
};

TEST_CASE("exportLogDialog routes through the UI save dialog", "[MidiMon][ui]") {
	struct Mock {
		TEST_MOCK_UI(MockUiAccess);
		TEST_MOCK_FS(MockFileAccess);
	} mock;
	auto module = Test::createModule<MidiMonModule>("MidiMon");
	auto widget = Test::createWidget<MidiMonWidget>(module);

	SECTION("Cancelled dialog writes nothing") {
		// saveDialog returns "" (cancelled) → exportLogDialog returns early.
		widget->exportLogDialog();

		REQUIRE(mock.ui.saveCalls.size() == 1);
		CHECK(mock.ui.saveCalls[0].filename == "MidiMon.log");
		// filters must be a valid osdialog filter string ("name:ext...") — passing ""
		// makes osdialog_filters_parse assert and abort in the real UI layer.
		CHECK_FALSE(mock.ui.saveCalls[0].filters.empty());
		CHECK(mock.ui.saveCalls[0].filters.find(':') != std::string::npos);
		CHECK(mock.fs.writes.empty());
	}

	SECTION("Selected path writes the log through the fs layer") {
		mock.ui.saveResults = { "/tmp/MidiMon.log" };
		// The widget display buffer (newest at front) is exported oldest-first.
		widget->buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 1.25f, 0LL, std::string("ch01 note on  60 vel 100")));
		widget->buffer.push_front(std::make_tuple(LOG_FORMAT::TEXT, 0.f, 0LL, std::string("f0 7e 7f f7")));
		widget->buffer.push_front(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, 0LL, std::string("ch01 14-bit cc7=1234")));

		widget->exportLogDialog();

		REQUIRE(mock.ui.saveCalls.size() == 1);
		REQUIRE(mock.fs.writes.size() == 1);
		CHECK(mock.fs.writes[0].path == "/tmp/MidiMon.log");

		const std::string& content = mock.fs.writes[0].data;
		// Header lines (MIDI unconfigured in tests → widget display fallbacks).
		CHECK(content.find(" v") != std::string::npos);
		CHECK(content.find("MIDI driver: (No driver)") != std::string::npos);
		CHECK(content.find("MIDI device: (No device)") != std::string::npos);
		CHECK(content.find("MIDI channel: (All channels)") != std::string::npos);
		CHECK(content.find("--------------------------------------------------------------------") != std::string::npos);
		// Body lines from the display buffer, including the formatted timestamp.
		CHECK(content.find("1.2500] ch01 note on  60 vel 100") != std::string::npos);
		CHECK(content.find("f0 7e 7f f7") != std::string::npos);
		CHECK(content.find("ch01 14-bit cc7=1234") != std::string::npos);
		// No error was surfaced.
		CHECK(mock.ui.messages.empty());
	}

	SECTION("Write failure warns through the UI") {
		mock.fs.failWrites = true;
		widget->exportLog("/tmp/MidiMon.log");

		REQUIRE(mock.ui.messages.size() == 1);
		CHECK(mock.ui.messages[0].type == vcv::MessageType::WARNING);
		CHECK(mock.ui.messages[0].buttons == vcv::MessageButtons::OK);
		CHECK(mock.ui.messages[0].msg.find("Could not write") != std::string::npos);
	}

	Test::destroyWidget(widget);
	Test::destroyModule(module);
}
