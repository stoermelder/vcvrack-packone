#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"

using namespace StoermelderPackOne::MidiKit;
using StoermelderPackOne::MidiScript::MidiScriptEngine;

SYNC_MODEL(modelMidiKit, "MidiKit");
Test::TestContext<> testContext;

// Bypass the dylib factory — create directly so the injected SyncTaskWorker
// is used instead of the module's default async TaskWorker.
static MidiKitModule* createModule() {
	MidiKitModule* m = new MidiKitModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

// Note: the Elk header parser uses ([^@]*) to capture tag values, so each
// value runs up to the next @ (or the end of the header) and picks up the
// separating whitespace.  loadScript() trims that trailing whitespace, so a
// script with @engine as its only tag loads too — see the ELK_ONLY_ENGINE
// test below.


static const char* ELK_EMPTY = R"(/**
 * @engine Elk
 * @description test
 */
)";

TEST_CASE("Elk-tagged script loads and creates JS context", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_EMPTY);

	REQUIRE(m->se.js != nullptr);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}


static const char* ELK_ONLY_ENGINE = R"(/**
 * @engine Elk
 */
)";

TEST_CASE("Elk script loads with @engine as the only header tag", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_ONLY_ENGINE);

	REQUIRE(m->se.js != nullptr);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}


static const char* ELK_MAX = R"(/**
 * @engine Elk
 * @description test
 */
let x = number.max(3, 7);
)";

TEST_CASE("Script body runs synchronously on load", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MAX);
	REQUIRE(m->se.js != nullptr);

	// number.max(3, 7) evaluated at load time — JS global x should be 7
	jsval_t v = js_eval(m->se.js, "x;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(7.0));

	Test::destroyModule(m);
}


static const char* ELK_RESCALE = R"(/**
 * @engine Elk
 * @description test
 */
let r = number.rescale(5, 0, 10, 0, 100);
)";

TEST_CASE("number.rescale API works from script body", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_RESCALE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "r;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(50.0).margin(0.01));

	Test::destroyModule(m);
}


// Echo script: forwards every message verbatim.
// Used to verify that processMidi is called without needing a second js_eval
// readback (the Elk arena is too small for two consecutive js_eval calls after
// a full API registration + script load).
static const char* ELK_ECHO = R"(/**
 * @engine Elk
 * @description echo passthrough
 */
let processMidi = function(port, msg) {
    midiOut.send(msg);
};
)";

TEST_CASE("processMidi callback is invoked with incoming message", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_ECHO);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	// Callback forwarded the message → output queue has exactly one message
	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));
	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);

	Test::destroyModule(m);
}


static const char* LUA_HEADER = R"(--[[
@engine Lua
--]]
)";

TEST_CASE("Lua-tagged script is rejected by Elk engine", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->se.loadScript(LUA_HEADER);

	REQUIRE(m->se.js == nullptr);

	Test::destroyModule(m);
}


static const char* ELK_BAD_SYNTAX = R"(/**
 * @engine Elk
 * @description test
 */
let ??? = ;
)";

TEST_CASE("JS syntax error is handled gracefully", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->se.loadScript(ELK_BAD_SYNTAX);

	REQUIRE(m->se.js == nullptr);

	Test::destroyModule(m);
}


// Increments the CC number of every incoming CC message by 1 and forwards it.
static const char* ELK_CC_REROUTE = R"(/**
 * @engine Elk
 * @description CC number +1 passthrough
 */
let processMidi = function(port, msg) {
    if (midi.isCc(msg)) {
        midi.setNote(msg, midi.getNote(msg) + 1);
        midiOut.send(msg);
    }
};
)";

TEST_CASE("Simple CC reroute script", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_CC_REROUTE);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);   // CC
	msg.setChannel(0);    // channel 1 (0-based internally)
	msg.setNote(10);      // CC number 10
	msg.setValue(64);     // CC value

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0xb);  // still CC
	REQUIRE(outMsg.getChannel() == 0);   // same channel
	REQUIRE(outMsg.getNote() == 11);     // CC number incremented
	REQUIRE(outMsg.getValue() == 64);    // value unchanged

	Test::destroyModule(m);
}


static const char* ELK_ABS = R"(/**
 * @engine Elk
 * @description test
 */
let a = number.abs(-5);
let b = number.abs(3);
let c = number.abs(0);
)";

TEST_CASE("API number.abs", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_ABS);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "a;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));

	v = js_eval(m->se.js, "b;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(3.0));

	v = js_eval(m->se.js, "c;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));

	Test::destroyModule(m);
}


static const char* ELK_CEIL = R"(/**
 * @engine Elk
 * @description test
 */
let a = number.ceil(3.2);
let b = number.ceil(-3.2);
let c = number.ceil(5);
)";

TEST_CASE("API number.ceil", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_CEIL);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "a;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(4.0));

	v = js_eval(m->se.js, "b;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(-3.0));

	v = js_eval(m->se.js, "c;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));

	Test::destroyModule(m);
}


static const char* ELK_CROSSFADE = R"(/**
 * @engine Elk
 * @description test
 */
let a = number.crossfade(0, 10, 0.5);
let b = number.crossfade(100, 200, 0.25);
let c = number.crossfade(-5, 5, 0.75);
)";

TEST_CASE("API number.crossfade", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_CROSSFADE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "a;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));

	v = js_eval(m->se.js, "b;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(125.0));

	v = js_eval(m->se.js, "c;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(2.5));

	Test::destroyModule(m);
}


static const char* ELK_FLOOR = R"(/**
 * @engine Elk
 * @description test
 */
let a = number.floor(3.8);
let b = number.floor(-3.8);
let c = number.floor(5);
)";

TEST_CASE("API number.floor", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_FLOOR);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "a;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(3.0));

	v = js_eval(m->se.js, "b;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(-4.0));

	v = js_eval(m->se.js, "c;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));

	Test::destroyModule(m);
}


static const char* ELK_MIN = R"(/**
 * @engine Elk
 * @description test
 */
let a = number.min(3, 7);
let b = number.min(-5, 5);
let c = number.min(10, 10);
)";

TEST_CASE("API number.min", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIN);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "a;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(3.0));

	v = js_eval(m->se.js, "b;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(-5.0));

	v = js_eval(m->se.js, "c;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(10.0));

	Test::destroyModule(m);
}


static const char* ELK_RANDOM = R"(/**
 * @engine Elk
 * @description test
 */
let a = number.random();
let b = number.random();
let c = number.random();
)";

TEST_CASE("API number.random", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_RANDOM);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "a;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) >= 0.0);
	REQUIRE(js_getnum(v) < 1.0);

	v = js_eval(m->se.js, "b;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) >= 0.0);
	REQUIRE(js_getnum(v) < 1.0);

	v = js_eval(m->se.js, "c;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) >= 0.0);
	REQUIRE(js_getnum(v) < 1.0);

	Test::destroyModule(m);
}


static const char* ELK_TOSTRING = R"(/**
 * @engine Elk
 * @description test
 */
let a = number.toString(42);
let b = number.toString(3.14);
let c = number.toString(-100);
)";

TEST_CASE("API number.toString", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_TOSTRING);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "a;", ~0U);
	REQUIRE(js_type(v) == JS_STR);
	size_t len;
	char* s = js_getstr(m->se.js, v, &len);
	REQUIRE(std::string(s, len) == "42");

	v = js_eval(m->se.js, "b;", ~0U);
	REQUIRE(js_type(v) == JS_STR);
	s = js_getstr(m->se.js, v, &len);
	REQUIRE(std::string(s, len) == "3.140000");

	v = js_eval(m->se.js, "c;", ~0U);
	REQUIRE(js_type(v) == JS_STR);
	s = js_getstr(m->se.js, v, &len);
	REQUIRE(std::string(s, len) == "-100");

	Test::destroyModule(m);
}


static const char* ELK_MIDI_CREATE = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
let type = typeof(msg);
)";

TEST_CASE("API midi.create", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_CREATE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "msg;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);  // Returns a message index (number)

	v = js_eval(m->se.js, "type;", ~0U);
	REQUIRE(js_type(v) == JS_STR);
	size_t len;
	char* s = js_getstr(m->se.js, v, &len);
	REQUIRE(std::string(s, len) == "number");

	Test::destroyModule(m);
}


static const char* ELK_MIDI_CREATE_NRPN = R"(/**
 * @engine Elk
 * @description test
 */
let nrpn = midi.createNRPN();
let type = typeof(nrpn);
)";

TEST_CASE("API midi.createNRPN", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_CREATE_NRPN);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "nrpn;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);  // Returns an NRPN index (number)

	v = js_eval(m->se.js, "type;", ~0U);
	REQUIRE(js_type(v) == JS_STR);
	size_t len;
	char* s = js_getstr(m->se.js, v, &len);
	REQUIRE(std::string(s, len) == "number");

	Test::destroyModule(m);
}


static const char* ELK_MIDI_GETTERS = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);
let ch = midi.getChannel(msg);
let note = midi.getNote(msg);
let val = midi.getValue(msg);
let len = midi.getLength(msg);

let msgPitch = midi.create();
midi.setPitchWheel(msgPitch, 1, 8192);
let pw = midi.getPitchWheel(msgPitch);
)";

TEST_CASE("API midi getter", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_GETTERS);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(1.0));

	v = js_eval(m->se.js, "note;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(60.0));

	v = js_eval(m->se.js, "val;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(100.0));

	v = js_eval(m->se.js, "len;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(3.0));

	v = js_eval(m->se.js, "pw;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	// Pitch wheel is computed from note (LSB) and value (MSB) bytes
	// For pitch wheel value 8192: note = 8192 & 0x7F = 0, value = 8192 >> 7 = 64
	// pw = (64 << 7) | 0 = 8192
	REQUIRE(js_getnum(v) == Catch::Approx(8192.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_IS_TYPES = R"(/**
 * @engine Elk
 * @description test
 */
let msgNoteOn = midi.create();
midi.setNoteOn(msgNoteOn, 1, 60, 100);

let msgNoteOff = midi.create();
midi.setNoteOff(msgNoteOff, 1, 60);

let msgCc = midi.create();
midi.setCc(msgCc, 1, 10, 64);

let msgPitch = midi.create();
midi.setPitchWheel(msgPitch, 1, 8192);

let msgProg = midi.create();
midi.setProgramChange(msgProg, 1, 5);

let msgChanPress = midi.create();
midi.setChanPressure(msgChanPress, 1, 100);

let msgKeyPress = midi.create();
midi.setKeyPressure(msgKeyPress, 1, 60, 100);

let msgSysEx = midi.create();
midi.setSysEx(msgSysEx, "43104c0000");

let isNoteOn = midi.isNoteOn(msgNoteOn);
let isNoteOff = midi.isNoteOff(msgNoteOff);
let isCc = midi.isCc(msgCc);
let isPitchWheel = midi.isPitchWheel(msgPitch);
let isProgramChange = midi.isProgramChange(msgProg);
let isChanPressure = midi.isChanPressure(msgChanPress);
let isKeyPressure = midi.isKeyPressure(msgKeyPress);
let isSysEx = midi.isSysEx(msgSysEx);
let isClock = midi.isClock(msgNoteOn);
let isStart = midi.isStart(msgNoteOn);
let isStop = midi.isStop(msgNoteOn);
let isContinue = midi.isContinue(msgNoteOn);
)";

TEST_CASE("API midi.is* type check", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_IS_TYPES);
	REQUIRE(m->se.js != nullptr);

	jsval_t v;

	v = js_eval(m->se.js, "isNoteOn;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isNoteOff;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isCc;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isPitchWheel;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isProgramChange;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isChanPressure;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isKeyPressure;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isSysEx;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	// These should be false for the messages we created
	v = js_eval(m->se.js, "isClock;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);

	v = js_eval(m->se.js, "isStart;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);

	v = js_eval(m->se.js, "isStop;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);

	v = js_eval(m->se.js, "isContinue;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SETTERS = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);
midi.setChannel(msg, 5);
midi.setNote(msg, 72);
midi.setValue(msg, 80);
let ch = midi.getChannel(msg);
let note = midi.getNote(msg);
let val = midi.getValue(msg);
)";

TEST_CASE("API midi setter", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SETTERS);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));

	v = js_eval(m->se.js, "note;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(72.0));

	v = js_eval(m->se.js, "val;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(80.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_CC = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setCc(msg, 3, 7, 100);
let isCc = midi.isCc(msg);
let ch = midi.getChannel(msg);
let cc = midi.getNote(msg);
let val = midi.getValue(msg);
)";

TEST_CASE("API midi.setCc", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_CC);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isCc;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(3.0));

	v = js_eval(m->se.js, "cc;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(7.0));

	v = js_eval(m->se.js, "val;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(100.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_CC_CLAMP = R"(/**
 * @engine Elk
 * @description test
 */
let msgHigh = midi.create();
midi.setCc(msgHigh, 3, 7, 200);
let valHigh = midi.getValue(msgHigh);
let msgLow = midi.create();
midi.setCc(msgLow, 3, 7, -1);
let valLow = midi.getValue(msgLow);
)";

TEST_CASE("API midi.setCc clamps out-of-range values instead of wrapping (#A5)", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_CC_CLAMP);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "valHigh;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(127.0));

	v = js_eval(m->se.js, "valLow;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_PITCH_WHEEL = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setPitchWheel(msg, 2, 12345);
let isPitch = midi.isPitchWheel(msg);
let ch = midi.getChannel(msg);
let pw = midi.getPitchWheel(msg);
)";

TEST_CASE("API midi.setPitchWheel", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_PITCH_WHEEL);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isPitch;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(2.0));

	v = js_eval(m->se.js, "pw;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(12345.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_PROGRAM_CHANGE = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setProgramChange(msg, 4, 10);
let isProg = midi.isProgramChange(msg);
let ch = midi.getChannel(msg);
let prog = midi.getNote(msg);
)";

TEST_CASE("API midi.setProgramChange", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_PROGRAM_CHANGE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isProg;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(4.0));

	v = js_eval(m->se.js, "prog;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(10.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_CHAN_PRESSURE = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setChanPressure(msg, 5, 80);
let isPress = midi.isChanPressure(msg);
let ch = midi.getChannel(msg);
let note = midi.getNote(msg);  // Pressure value is in note byte for channel pressure
)";

TEST_CASE("API midi.setChanPressure", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_CHAN_PRESSURE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isPress;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));

	v = js_eval(m->se.js, "note;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(80.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_KEY_PRESSURE = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setKeyPressure(msg, 6, 64, 90);
let isPress = midi.isKeyPressure(msg);
let ch = midi.getChannel(msg);
let note = midi.getNote(msg);
let val = midi.getValue(msg);
)";

TEST_CASE("API midi.setKeyPressure", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_KEY_PRESSURE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isPress;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(6.0));

	v = js_eval(m->se.js, "note;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(64.0));

	v = js_eval(m->se.js, "val;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(90.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_KEY_PRESSURE_CLAMP = R"(/**
 * @engine Elk
 * @description test
 */
let msgHigh = midi.create();
midi.setKeyPressure(msgHigh, 6, 64, 200);
let valHigh = midi.getValue(msgHigh);
let msgLow = midi.create();
midi.setKeyPressure(msgLow, 6, 64, -1);
let valLow = midi.getValue(msgLow);
)";

TEST_CASE("API midi.setKeyPressure clamps out-of-range values instead of wrapping (#A5)", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_KEY_PRESSURE_CLAMP);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "valHigh;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(127.0));

	v = js_eval(m->se.js, "valLow;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_NOTE_ON_CLAMP = R"(/**
 * @engine Elk
 * @description test
 */
let msgHigh = midi.create();
midi.setNoteOn(msgHigh, 1, 60, 200);
let valHigh = midi.getValue(msgHigh);
let msgLow = midi.create();
midi.setNoteOn(msgLow, 1, 60, -1);
let valLow = midi.getValue(msgLow);
)";

TEST_CASE("API midi.setNoteOn clamps out-of-range velocity instead of wrapping (#A5)", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_NOTE_ON_CLAMP);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "valHigh;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(127.0));

	v = js_eval(m->se.js, "valLow;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_SYSEX = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setSysEx(msg, "43104c0000");
let isSysEx = midi.isSysEx(msg);
let data = midi.getSysExData(msg);
)";

TEST_CASE("API midi.setSysEx and midi.getSysExData", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_SYSEX);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isSysEx;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "data;", ~0U);
	REQUIRE(js_type(v) == JS_STR);
	size_t len;
	char* s = js_getstr(m->se.js, v, &len);
	REQUIRE(std::string(s, len) == "43104c0000");

	// The payload argument is unframed; setSysEx must add the 0xf0/0xf7 framing
	// itself rather than doubling it (#17).
	auto& stored = m->se.msgStore[0];
	REQUIRE(stored.msg.getSize() == 7);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[0])) == 0xf0);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[1])) == 0x43);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[2])) == 0x10);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[3])) == 0x4c);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[4])) == 0x00);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[5])) == 0x00);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[6])) == 0xf7);

	Test::destroyModule(m);
}


TEST_CASE("API midi.setSysEx rejects a payload over the length cap", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();
	m->loadScript(ELK_MIDI_SET_SYSEX);
	REQUIRE(m->se.js != nullptr);

	// One byte past the 256-byte cap (#A6).
	std::string longHex(2 * 257, '7');
	std::string expr = "midi.setSysEx(msg, \"" + longHex + "\");";
	jsval_t v = js_eval(m->se.js, expr.c_str(), ~0U);
	REQUIRE(js_type(v) == JS_ERR);

	Test::destroyModule(m);
}

TEST_CASE("API midi.setSysEx rejects a non-7-bit payload byte", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();
	m->loadScript(ELK_MIDI_SET_SYSEX);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "midi.setSysEx(msg, \"ff\");", ~0U);
	REQUIRE(js_type(v) == JS_ERR);

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_RAW = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setRaw(msg, "f11a");
let data = midi.getRaw(msg);
)";

TEST_CASE("API midi.setRaw and midi.getRaw", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_RAW);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "data;", ~0U);
	REQUIRE(js_type(v) == JS_STR);
	size_t len;
	char* s = js_getstr(m->se.js, v, &len);
	REQUIRE(std::string(s, len) == "f11a");

	// setRaw writes the exact bytes with no framing added, unlike setSysEx.
	auto& stored = m->se.msgStore[0];
	REQUIRE(stored.msg.getSize() == 2);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[0])) == 0xf1);
	REQUIRE(static_cast<int>(static_cast<uint8_t>(stored.msg.bytes[1])) == 0x1a);

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_NOTE_OFF = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOff(msg, 7, 48);
let isOff = midi.isNoteOff(msg);
let ch = midi.getChannel(msg);
let note = midi.getNote(msg);
)";

TEST_CASE("API midi.setNoteOff", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_NOTE_OFF);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isOff;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "ch;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(7.0));

	v = js_eval(m->se.js, "note;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(48.0));

	Test::destroyModule(m);
}


static const char* ELK_MIDI_CC_14BIT = R"(/**
 * @engine Elk
 * @description test
 */
let msg1 = midi.create();
let msg2 = midi.create();
midi.setCc14bit(msg1, msg2, 8, 1, 100.5);
let isCc1 = midi.isCc(msg1);
let isCc2 = midi.isCc(msg2);
let ch1 = midi.getChannel(msg1);
let ch2 = midi.getChannel(msg2);
let cc1 = midi.getNote(msg1);
let cc2 = midi.getNote(msg2);
let val1 = midi.getValue(msg1);
let val2 = midi.getValue(msg2);
)";

TEST_CASE("API midi.setCc14bit", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_CC_14BIT);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "isCc1;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "isCc2;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "ch1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(8.0));

	v = js_eval(m->se.js, "ch2;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(8.0));

	v = js_eval(m->se.js, "cc1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(1.0));  // CC number (MSB)

	v = js_eval(m->se.js, "cc2;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(33.0));  // CC number + 32 (LSB)

	v = js_eval(m->se.js, "val1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(100.0));  // MSB value

	v = js_eval(m->se.js, "val2;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(64.0));  // LSB value (0.5 * 128)

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SET_NRPN = R"(/**
 * @engine Elk
 * @description test
 */
let nrpn = midi.createNRPN();
midi.setNRPN(nrpn, 9, 1234, 5678);
)";

TEST_CASE("API midi.setNRPN", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SET_NRPN);
	REQUIRE(m->se.js != nullptr);

	// Verify the NRPN was created and set correctly
	// NRPN is 4 CC messages: CC98 (LSB of NRPN number), CC99 (MSB of NRPN number), CC38 (LSB of value), CC6 (MSB of value)
	jsval_t v = js_eval(m->se.js, "nrpn;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	size_t nrpnIdx = js_getnum(v);

	// Helper lambda to build JS code strings
	auto eval = [&](const char* fmt, size_t idx) -> jsval_t {
		char buf[256];
		snprintf(buf, sizeof(buf), fmt, idx);
		return js_eval(m->se.js, buf, ~0U);
	};

	// Check message 1: CC98 (NRPN number LSB) - 1234 & 0x7f = 0x52 = 82
	v = eval("midi.getNote(%zu);", nrpnIdx);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 98);
	v = eval("midi.getValue(%zu);", nrpnIdx);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 82);  // 1234 & 0x7f = 0x52 = 82

	// Check message 2: CC99 (NRPN number MSB) - (1234 >> 7) & 0x7f = 9
	v = eval("midi.getNote(%zu);", nrpnIdx + 1);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 99);
	v = eval("midi.getValue(%zu);", nrpnIdx + 1);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 9);  // (1234 >> 7) & 0x7f

	// Check message 3: CC38 (NRPN value LSB) - 5678 & 0x7f = 46
	v = eval("midi.getNote(%zu);", nrpnIdx + 2);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 38);
	v = eval("midi.getValue(%zu);", nrpnIdx + 2);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 46);  // 5678 & 0x7f = 46

	// Check message 4: CC6 (NRPN value MSB) - (5678 >> 7) & 0x7f = 44
	v = eval("midi.getNote(%zu);", nrpnIdx + 3);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 6);
	v = eval("midi.getValue(%zu);", nrpnIdx + 3);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 44);  // (5678 >> 7) & 0x7f

	// Verify channel is set correctly (channel 9 = 0-indexed 8)
	v = eval("midi.getChannel(%zu);", nrpnIdx);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == 9);

	Test::destroyModule(m);
}


static const char* ELK_INPUT_ENABLE = R"(/**
 * @engine Elk
 * @description test
 */
input.enable(1);
input.enable(2);
)";

TEST_CASE("API input.enable", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_INPUT_ENABLE);
	REQUIRE(m->se.js != nullptr);

	// Verify inputs are enabled by checking the module's inputInfos
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[0])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[1])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[2])->enabled == false);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[3])->enabled == false);

	Test::destroyModule(m);
}


static const char* ELK_INPUT_GET_VOLTAGE = R"(/**
 * @engine Elk
 * @description test
 */
input.enable(1);
let v1 = input.getVoltage(1);
let v2 = input.getVoltage(1, 1);
)";

TEST_CASE("API input.getVoltage", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_INPUT_GET_VOLTAGE);
	REQUIRE(m->se.js != nullptr);

	// Test default voltage (0V)
	jsval_t v = js_eval(m->se.js, "v1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));  // Default voltage is 0

	v = js_eval(m->se.js, "v2;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));

	// Test with non-zero voltage on input
	m->inputs[MidiKitModule::INPUT + 0].channels = 1;
	m->inputs[MidiKitModule::INPUT + 0].setVoltage(5.0f);

	// Re-evaluate the script to get updated voltage
	m->loadScript(ELK_INPUT_GET_VOLTAGE);
	REQUIRE(m->se.js != nullptr);

	v = js_eval(m->se.js, "v1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));  // Should read 5V

	v = js_eval(m->se.js, "v2;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(5.0));  // Channel 1 should also read 5V

	// Test with negative voltage
	m->inputs[MidiKitModule::INPUT + 0].setVoltage(-3.0f);
	m->loadScript(ELK_INPUT_GET_VOLTAGE);
	REQUIRE(m->se.js != nullptr);

	v = js_eval(m->se.js, "v1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(-3.0));  // Should read -3V

	Test::destroyModule(m);
}


static const char* ELK_INPUT_IS_HIGH_LOW = R"(/**
 * @engine Elk
 * @description test
 */
input.enable(1);
let high1 = input.isHigh(1);
let low1 = input.isLow(1);
let high2 = input.isHigh(1, 1);
let low2 = input.isLow(1, 1);
)";

TEST_CASE("API input.isHigh and input.isLow", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_INPUT_IS_HIGH_LOW);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "high1;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);  // Default voltage 0V is not high

	v = js_eval(m->se.js, "low1;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);  // Default voltage 0V is low

	v = js_eval(m->se.js, "high2;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);

	v = js_eval(m->se.js, "low2;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	Test::destroyModule(m);
}


static const char* ELK_TRIG_GET_TICKS = R"(/**
 * @engine Elk
 * @description test
 */
let ticks = trig.getTicks(1);
)";

TEST_CASE("API trig.getTicks", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_TRIG_GET_TICKS);
	REQUIRE(m->se.js != nullptr);

	// Initially no triggers
	jsval_t v = js_eval(m->se.js, "ticks;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));  // No triggers yet

	// Send a trigger pulse to the input
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	Module::ProcessArgs args;
	args.sampleTime = 1.0f / 44100.0f;
	args.frame = 0;
	args.sampleRate = 44100.0f;
	m->process(args);

	// Rising edge → tick increments
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	args.frame = 1;
	m->process(args);

	// Evaluate trig.getTicks directly (don't reload script - that resets inputTriggerTick)
	v = js_eval(m->se.js, "trig.getTicks(1);", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(1.0));  // One trigger received

	// Second pulse
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	args.frame = 2;
	m->process(args);

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	args.frame = 3;
	m->process(args);

	v = js_eval(m->se.js, "trig.getTicks(1);", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(2.0));  // Two triggers received

	Test::destroyModule(m);
}


static const char* ELK_TRIG_IS_HIGH_LOW = R"(/**
 * @engine Elk
 * @description test
 */
let high1 = trig.isHigh(1);
let low1 = trig.isLow(1);
let high2 = trig.isHigh(1, 1);
let low2 = trig.isLow(1, 1);
)";

TEST_CASE("API trig.isHigh and trig.isLow", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_TRIG_IS_HIGH_LOW);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "high1;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);  // Default voltage 0V is not high

	v = js_eval(m->se.js, "low1;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);  // Default voltage 0V is low

	v = js_eval(m->se.js, "high2;", ~0U);
	REQUIRE(js_type(v) == JS_FALSE);

	v = js_eval(m->se.js, "low2;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	Test::destroyModule(m);
}


static const char* ELK_TRIG_SET_FUNCTIONS = R"(/**
 * @engine Elk
 * @description test
 */
trig.setGate(1, 100);
trig.setHigh(1);
trig.setLow(1);
trig.setTrigger(1);
)";

TEST_CASE("API trig.setGate, setHigh, setLow, setTrigger", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_TRIG_SET_FUNCTIONS);
	REQUIRE(m->se.js != nullptr);

	// Process the module to execute the script and trigger outputs
	Module::ProcessArgs args;
	args.sampleTime = 1.0f / 44100.0f;
	args.frame = 0;
	args.sampleRate = 44100.0f;
	m->process(args);

	// After the script runs, the last call was trig.setTrigger(1) which sets outputTriggerActive[0] = true
	// and triggers the pulse generator for 1ms. At frame 0, the pulse should be high (10V).
	REQUIRE(m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0) == Catch::Approx(10.0f));

	// Verify internal state: outputTriggerActive should be true (from setTrigger)
	REQUIRE(m->outputTriggerActive[0] == true);

	// Verify pulse generator is active
	REQUIRE(m->outputPulseGenerator[0].process(args.sampleTime) == true);

	Test::destroyModule(m);
}


static const char* ELK_PARAM_ENABLE = R"(/**
 * @engine Elk
 * @description test
 */
param.enable(1);
param.enable(3);
)";

TEST_CASE("API param.enable", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_PARAM_ENABLE);
	REQUIRE(m->se.js != nullptr);

	// Verify parameters are enabled by checking the module's paramQuantities
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[0])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[1])->enabled == false);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[2])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[3])->enabled == false);

	Test::destroyModule(m);
}


static const char* ELK_PARAM_GET_VALUE = R"(/**
 * @engine Elk
 * @description test
 */
param.enable(1);
let v1 = param.getValue(1);
let v2 = param.getValue(2);
)";

TEST_CASE("API param.getValue", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_PARAM_GET_VALUE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "v1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));  // Default param value is 0

	v = js_eval(m->se.js, "v2;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));

	// Test with non-default parameter value
	m->params[MidiKitModule::PARAM + 0].setValue(0.5f);

	// Re-evaluate the script to get updated value
	m->loadScript(ELK_PARAM_GET_VALUE);
	REQUIRE(m->se.js != nullptr);

	v = js_eval(m->se.js, "v1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.5));  // Should read 0.5

	v = js_eval(m->se.js, "v2;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(0.0));  // Param 2 still default

	// Test with another value
	m->params[MidiKitModule::PARAM + 0].setValue(1.0f);
	m->loadScript(ELK_PARAM_GET_VALUE);
	REQUIRE(m->se.js != nullptr);

	v = js_eval(m->se.js, "v1;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(1.0));  // Should read 1.0

	Test::destroyModule(m);
}


static const char* ELK_MIDIOUT_SEND = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

let processMidi = function(port, msg) {
    midiOut.send(msg);
};
)";

TEST_CASE("API midiOut.send", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDIOUT_SEND);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0x9);  // NoteOn
	REQUIRE(outMsg.getChannel() == 0);   // channel 1 (0-based)
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);

	Test::destroyModule(m);
}


static std::string drainLog(MidiKitModule* m) {
	std::string all;
	while (!m->midiLogMessages.empty()) {
		auto t = m->midiLogMessages.shift();
		all += std::get<2>(t) + "\n";
	}
	return all;
}


static const char* ELK_MIDI_SELECT_PORT = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

let processMidi = function(port, msg) {
    midi.selectPort(1);  // Select port 1 (1-based); stays selected until changed
    midiOut.send(msg);
};
)";

TEST_CASE("API midi.selectPort", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SELECT_PORT);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outPort == 0);  // Port 1 (1-based) = port 0 (0-based)
	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SELECT_PORT_STICKY = R"(/**
 * @engine Elk
 * @description test
 */
let processMidi = function(port, msg) {
    midi.selectPort(1);
    let msg1 = midi.create();
    midi.setNoteOn(msg1, 1, 60, 100);
    let msg2 = midi.create();
    midi.setNoteOn(msg2, 1, 61, 100);
    midiOut.send(msg1);
    midiOut.send(msg2);  // No selectPort call — stays on port 1
};
)";

TEST_CASE("API midi.selectPort stays selected across calls", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SELECT_PORT_STICKY);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));
	REQUIRE(outPort == 0);
	REQUIRE(outMsg.getNote() == 60);

	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));
	REQUIRE(outPort == 0);
	REQUIRE(outMsg.getNote() == 61);

	Test::destroyModule(m);
}


static const char* ELK_MIDI_SELECT_PORT_INVALID = R"(/**
 * @engine Elk
 * @description test
 */
let processMidi = function(port, msg) {
    midi.selectPort(2);  // Only 1 output port exists
};
)";

TEST_CASE("API midi.selectPort rejects an out-of-range port", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDI_SELECT_PORT_INVALID);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	std::string log = drainLog(m);
	REQUIRE(log.find("processMidi error") != std::string::npos);

	Test::destroyModule(m);
}


static const char* ELK_MIDIOUT_SEND_AFTER_MS = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

let processMidi = function(port, msg) {
    midiOut.sendAfterMs(msg, 100);  // Send after 100ms
};
)";

TEST_CASE("API midiOut.sendAfterMs", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDIOUT_SEND_AFTER_MS);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);
	// Frame should be set to future time
	REQUIRE(outMsg.frame > 0);

	Test::destroyModule(m);
}


static const char* ELK_MIDIOUT_SEND_AFTER_TRIGGER = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

let processMidi = function(port, msg) {
    midiOut.sendAfterTrigger(msg, 10);  // Send after 10 ticks on trig port 0
};
)";

TEST_CASE("API midiOut.sendAfterTrigger", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDIOUT_SEND_AFTER_TRIGGER);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);
	// Tick should be set to current trig ticks + 10
	REQUIRE(ticks >= 10);

	Test::destroyModule(m);
}


// midiOut.sendAfterTrigger no longer takes midiPort as an argument — output
// port comes from midi.selectPort() instead. The 3-arg form here is
// (msg, trigPort, ticks).

static const char* ELK_MIDIOUT_SEND_AFTER_TRIGGER_WITH_SELECTED_PORT = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

let processMidi = function(port, msg) {
    midi.selectPort(1);
    midiOut.sendAfterTrigger(msg, 10);  // no trigPort override
};
)";

TEST_CASE("API midiOut.sendAfterTrigger uses the selected port", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDIOUT_SEND_AFTER_TRIGGER_WITH_SELECTED_PORT);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outPort == 0);
	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(ticks >= 10);

	Test::destroyModule(m);
}


static const char* ELK_MIDIOUT_SEND_AFTER_TRIGGER_WITH_TRIGPORT = R"(/**
 * @engine Elk
 * @description test
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

let processMidi = function(port, msg) {
    midi.selectPort(1);
    midiOut.sendAfterTrigger(msg, 1, 10);  // trigPort 1, 10 ticks
};
)";

TEST_CASE("API midiOut.sendAfterTrigger with explicit trigPort (3 args)", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MIDIOUT_SEND_AFTER_TRIGGER_WITH_TRIGPORT);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outPort == 0);
	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(ticks >= 10);

	Test::destroyModule(m);
}


// midi.create() / midi.createNRPN() outside processMidi()
//
// The message store is reset on every callback, so a handle created at top
// level is silently invalidated before it can be used. That reset is documented
// and intended; these tests pin the warning that makes it visible.

static const char* OUTSIDE_CALLBACK_WARNING = "called outside processMidi()";

static const char* ELK_TOPLEVEL_CREATE = R"(/**
 * @engine Elk
 * @description test
 */
let g = midi.create();
)";

static const char* ELK_TOPLEVEL_CREATE_NRPN = R"(/**
 * @engine Elk
 * @description test
 */
let g = midi.createNRPN();
)";

static const char* ELK_CALLBACK_CREATE = R"(/**
 * @engine Elk
 * @description test
 */
let processMidi = function(port, msg) {
  let m = midi.create();
  midi.setCc(m, 1, 20, 100);
  midiOut.send(m);
};
)";

TEST_CASE("midi.create outside processMidi warns", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_TOPLEVEL_CREATE);
	REQUIRE(m->se.js != nullptr);

	REQUIRE(drainLog(m).find(OUTSIDE_CALLBACK_WARNING) != std::string::npos);

	Test::destroyModule(m);
}

TEST_CASE("midi.createNRPN outside processMidi warns", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_TOPLEVEL_CREATE_NRPN);
	REQUIRE(m->se.js != nullptr);

	REQUIRE(drainLog(m).find(OUTSIDE_CALLBACK_WARNING) != std::string::npos);

	Test::destroyModule(m);
}

TEST_CASE("midi.create inside processMidi does not warn", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_CALLBACK_CREATE);
	REQUIRE(m->se.js != nullptr);
	drainLog(m);  // discard load-time messages

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	m->se.processInMessage(0, msg);
	m->se.process();

	REQUIRE(drainLog(m).find(OUTSIDE_CALLBACK_WARNING) == std::string::npos);

	Test::destroyModule(m);
}


// Parse-error reporting with a source position
//
// Elk raises errors as a bare string with no position, so a failed load used to
// log just "ERROR: parse error". js_mkerr() now records the offset of the token
// it failed on, and the engine turns that into a line, a column and the source
// line itself.

// The `function f() {}` declaration form is a parse error in Elk — it is a JS
// subset that only accepts `let f = function() {}`. This is the single most
// likely mistake a script author makes, and the one that most needs a line.
static const char* ELK_BAD_ON_LINE_7 = R"(/**
 * @engine Elk
 * @description test
 */
let a = 1;
let b = 2;
function broken(x) { return x; }
let c = 3;
)";

TEST_CASE("Elk parse error reports the line it failed on", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_BAD_ON_LINE_7);
	// A failed load tears the state down
	REQUIRE(m->se.js == nullptr);

	std::string log = drainLog(m);
	// `function` is on line 7, at column 10 counting from the `broken` token
	REQUIRE(log.find("line 7:") != std::string::npos);
	// The offending source line is echoed so the author needn't count lines
	REQUIRE(log.find("function broken(x) { return x; }") != std::string::npos);
	// The underlying elk message is still present
	REQUIRE(log.find("parse error") != std::string::npos);

	Test::destroyModule(m);
}


// Same defect one line earlier — pins that the number tracks the error rather
// than being a constant that happens to match.
static const char* ELK_BAD_ON_LINE_6 = R"(/**
 * @engine Elk
 * @description test
 */
let a = 1;
function broken(x) { return x; }
let b = 2;
)";

TEST_CASE("Elk parse error line number tracks the error position", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_BAD_ON_LINE_6);
	REQUIRE(m->se.js == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("line 6:") != std::string::npos);
	REQUIRE(log.find("line 7:") == std::string::npos);

	Test::destroyModule(m);
}


// A script that loads cleanly must not emit any position noise.
TEST_CASE("Elk successful load reports no error position", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MAX);
	REQUIRE(m->se.js != nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("line ") == std::string::npos);
	REQUIRE(log.find("Script loaded") != std::string::npos);

	Test::destroyModule(m);
}
