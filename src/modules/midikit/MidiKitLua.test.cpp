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


static const char* LUA_EMPTY = R"(--[[
@engine Lua
--]]
)";

TEST_CASE("Lua-tagged script loads and creates Lua state", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_EMPTY);

	REQUIRE(m->seLua.L != nullptr);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	Test::destroyModule(m);
}


static const char* LUA_MAX = R"(--[[
@engine Lua
--]]
x = number.max(3, 7)
)";

TEST_CASE("Script body runs synchronously on load", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MAX);
	REQUIRE(m->seLua.L != nullptr);

	// number.max(3, 7) evaluated at load time — Lua global x should be 7
	lua_getglobal(m->seLua.L, "x");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(7.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_RESCALE = R"(--[[
@engine Lua
--]]
r = number.rescale(5, 0, 10, 0, 100)
)";

TEST_CASE("number.rescale API works from script body", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_RESCALE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "r");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(50.0).margin(0.01));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_INPUT_NAME = R"(--[[
@engine Lua
--]]
input.getName = function(i) return 'CV-' .. i end
)";

TEST_CASE("Script can override input.getName", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_INPUT_NAME);
	REQUIRE(m->seLua.L != nullptr);

	REQUIRE(m->seLua.getInputName(0) == "CV-1");
	REQUIRE(m->seLua.getInputName(3) == "CV-4");

	Test::destroyModule(m);
}


static const char* ELK_HEADER = R"(/**
 * @engine Elk
 */
)";

TEST_CASE("Elk-tagged script is rejected by Lua engine", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->seLua.loadScript(ELK_HEADER);

	REQUIRE(m->seLua.L == nullptr);

	Test::destroyModule(m);
}


static const char* LUA_BAD_SYNTAX = R"(--[[
@engine Lua
--]]
local x = ?
)";

TEST_CASE("Syntax error is handled gracefully", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->seLua.loadScript(LUA_BAD_SYNTAX);

	REQUIRE(m->seLua.L == nullptr);

	Test::destroyModule(m);
}


// Increments the CC number of every incoming CC message by 1 and forwards it.
static const char* LUA_CC_REROUTE = R"(--[[
@engine Lua
@description CC number +1 passthrough
--]]
processMidi = function(port, msg)
    if midi.isCc(msg) then
        midi.setNote(msg, midi.getNote(msg) + 1)
        midiOut.send(msg)
    end
end
)";

TEST_CASE("Simple CC reroute script", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_CC_REROUTE);
	REQUIRE(m->seLua.L != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);   // CC
	msg.setChannel(0);    // channel 1 (0-based internally)
	msg.setNote(10);      // CC number 10
	msg.setValue(64);     // CC value

	m->seLua.processInMessage(0, msg);
	m->seLua.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->seLua.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0xb);  // still CC
	REQUIRE(outMsg.getChannel() == 0);   // same channel
	REQUIRE(outMsg.getNote() == 11);     // CC number incremented
	REQUIRE(outMsg.getValue() == 64);    // value unchanged

	Test::destroyModule(m);
}


static const char* LUA_ABS = R"(--[[
@engine Lua
--]]
a = number.abs(-5)
b = number.abs(3)
c = number.abs(0)
)";

TEST_CASE("API number.abs", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_ABS);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "a");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "b");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(3.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "c");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_CEIL = R"(--[[
@engine Lua
--]]
a = number.ceil(3.2)
b = number.ceil(-3.2)
c = number.ceil(5)
)";

TEST_CASE("API number.ceil", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_CEIL);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "a");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(4.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "b");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(-3.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "c");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_CROSSFADE = R"(--[[
@engine Lua
--]]
a = number.crossfade(0, 10, 0.5)
b = number.crossfade(100, 200, 0.25)
c = number.crossfade(-5, 5, 0.75)
)";

TEST_CASE("API number.crossfade", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_CROSSFADE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "a");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "b");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(125.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "c");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(2.5));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_FLOOR = R"(--[[
@engine Lua
--]]
a = number.floor(3.8)
b = number.floor(-3.8)
c = number.floor(5)
)";

TEST_CASE("API number.floor", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_FLOOR);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "a");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(3.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "b");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(-4.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "c");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIN = R"(--[[
@engine Lua
--]]
a = number.min(3, 7)
b = number.min(-5, 5)
c = number.min(10, 10)
)";

TEST_CASE("API number.min", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIN);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "a");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(3.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "b");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(-5.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "c");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(10.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_RANDOM = R"(--[[
@engine Lua
--]]
a = number.random()
b = number.random()
c = number.random()
)";

TEST_CASE("API number.random", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_RANDOM);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "a");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	double v = lua_tonumber(m->seLua.L, -1);
	REQUIRE(v >= 0.0);
	REQUIRE(v < 1.0);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "b");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	v = lua_tonumber(m->seLua.L, -1);
	REQUIRE(v >= 0.0);
	REQUIRE(v < 1.0);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "c");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	v = lua_tonumber(m->seLua.L, -1);
	REQUIRE(v >= 0.0);
	REQUIRE(v < 1.0);
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_TOSTRING = R"(--[[
@engine Lua
--]]
a = number.toString(42)
b = number.toString(3.14)
c = number.toString(-100)
)";

TEST_CASE("API number.toString", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_TOSTRING);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "a");
	REQUIRE(lua_isstring(m->seLua.L, -1));
	REQUIRE(std::string(lua_tostring(m->seLua.L, -1)) == "42");
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "b");
	REQUIRE(lua_isstring(m->seLua.L, -1));
	// Lua's %f format produces 6 decimal places
	REQUIRE(std::string(lua_tostring(m->seLua.L, -1)) == "3.140000");
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "c");
	REQUIRE(lua_isstring(m->seLua.L, -1));
	REQUIRE(std::string(lua_tostring(m->seLua.L, -1)) == "-100");
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_CREATE = R"(--[[
@engine Lua
--]]
msg = midi.create()
type = type(msg)
)";

TEST_CASE("API midi.create", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_CREATE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "msg");
	REQUIRE(lua_isnumber(m->seLua.L, -1));  // Returns a message index (number)
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "type");
	REQUIRE(lua_isstring(m->seLua.L, -1));
	REQUIRE(std::string(lua_tostring(m->seLua.L, -1)) == "number");
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_CREATE_NRPN = R"(--[[
@engine Lua
--]]
nrpn = midi.createNRPN()
type = type(nrpn)
)";

TEST_CASE("API midi.createNRPN", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_CREATE_NRPN);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "nrpn");
	REQUIRE(lua_isnumber(m->seLua.L, -1));  // Returns an NRPN index (number)
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "type");
	REQUIRE(lua_isstring(m->seLua.L, -1));
	REQUIRE(std::string(lua_tostring(m->seLua.L, -1)) == "number");
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_GETTERS = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)
ch = midi.getChannel(msg)
note = midi.getNote(msg)
val = midi.getValue(msg)
len = midi.getLength(msg)

msgPitch = midi.create()
midi.setPitchWheel(msgPitch, 1, 8192)
pw = midi.getPitchWheel(msgPitch)
)";

TEST_CASE("API midi getter", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_GETTERS);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(1.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "note");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(60.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "val");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(100.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "len");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(3.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "pw");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	// Pitch wheel is computed from note (LSB) and value (MSB) bytes
	// For pitch wheel value 8192: note = 8192 & 0x7F = 0, value = 8192 >> 7 = 64
	// pw = (64 << 7) | 0 = 8192
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(8192.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_IS_TYPES = R"(--[[
@engine Lua
--]]
msgNoteOn = midi.create()
midi.setNoteOn(msgNoteOn, 1, 60, 100)

msgNoteOff = midi.create()
midi.setNoteOff(msgNoteOff, 1, 60)

msgCc = midi.create()
midi.setCc(msgCc, 1, 10, 64)

msgPitch = midi.create()
midi.setPitchWheel(msgPitch, 1, 8192)

msgProg = midi.create()
midi.setProgramChange(msgProg, 1, 5)

msgChanPress = midi.create()
midi.setChanPressure(msgChanPress, 1, 100)

msgKeyPress = midi.create()
midi.setKeyPressure(msgKeyPress, 1, 60, 100)

msgSysEx = midi.create()
midi.setSysEx(msgSysEx, "43104c0000")

isNoteOn = midi.isNoteOn(msgNoteOn)
isNoteOff = midi.isNoteOff(msgNoteOff)
isCc = midi.isCc(msgCc)
isPitchWheel = midi.isPitchWheel(msgPitch)
isProgramChange = midi.isProgramChange(msgProg)
isChanPressure = midi.isChanPressure(msgChanPress)
isKeyPressure = midi.isKeyPressure(msgKeyPress)
isSysEx = midi.isSysEx(msgSysEx)
isClock = midi.isClock(msgNoteOn)
isStart = midi.isStart(msgNoteOn)
isStop = midi.isStop(msgNoteOn)
isContinue = midi.isContinue(msgNoteOn)
)";

TEST_CASE("API midi.is* type check", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_IS_TYPES);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isNoteOn");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isNoteOff");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isCc");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isPitchWheel");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isProgramChange");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isChanPressure");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isKeyPressure");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isSysEx");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	// These should be false for the messages we created
	lua_getglobal(m->seLua.L, "isClock");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isStart");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isStop");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isContinue");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SETTERS = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)
midi.setChannel(msg, 5)
midi.setNote(msg, 72)
midi.setValue(msg, 80)
ch = midi.getChannel(msg)
note = midi.getNote(msg)
val = midi.getValue(msg)
)";

TEST_CASE("API midi setter", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SETTERS);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "note");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(72.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "val");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(80.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_CC = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setCc(msg, 3, 7, 100)
isCc = midi.isCc(msg)
ch = midi.getChannel(msg)
cc = midi.getNote(msg)
val = midi.getValue(msg)
)";

TEST_CASE("API midi.setCc", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_CC);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isCc");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(3.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "cc");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(7.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "val");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(100.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_PITCH_WHEEL = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setPitchWheel(msg, 2, 12345)
isPitch = midi.isPitchWheel(msg)
ch = midi.getChannel(msg)
pw = midi.getPitchWheel(msg)
)";

TEST_CASE("API midi.setPitchWheel", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_PITCH_WHEEL);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isPitch");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(2.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "pw");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(12345.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_PROGRAM_CHANGE = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setProgramChange(msg, 4, 10)
isProg = midi.isProgramChange(msg)
ch = midi.getChannel(msg)
prog = midi.getNote(msg)
)";

TEST_CASE("API midi.setProgramChange", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_PROGRAM_CHANGE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isProg");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(4.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "prog");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(10.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_CHAN_PRESSURE = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setChanPressure(msg, 5, 80)
isPress = midi.isChanPressure(msg)
ch = midi.getChannel(msg)
pressure = midi.getNote(msg)  -- Pressure value is in note byte for channel pressure
)";

TEST_CASE("API midi.setChanPressure", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_CHAN_PRESSURE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isPress");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "pressure");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(80.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_KEY_PRESSURE = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setKeyPressure(msg, 6, 64, 90)
isPress = midi.isKeyPressure(msg)
ch = midi.getChannel(msg)
note = midi.getNote(msg)
val = midi.getValue(msg)
)";

TEST_CASE("API midi.setKeyPressure", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_KEY_PRESSURE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isPress");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(6.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "note");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(64.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "val");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(90.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_SYSEX = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setSysEx(msg, "43104c0000")
isSysEx = midi.isSysEx(msg)
data = midi.getSysExData(msg)
)";

TEST_CASE("API midi.setSysEx and midi.getSysExData", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_SYSEX);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isSysEx");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "data");
	REQUIRE(lua_isstring(m->seLua.L, -1));
	REQUIRE(std::string(lua_tostring(m->seLua.L, -1)) == "43104c0000");
	lua_pop(m->seLua.L, 1);

	// The payload argument is unframed; setSysEx must add the 0xf0/0xf7 framing
	// itself rather than doubling it (#17).
	auto& stored = m->seLua.msgStore[0];
	REQUIRE(stored.msg.getSize() == 7);
	REQUIRE((int)(uint8_t)stored.msg.bytes[0] == 0xf0);
	REQUIRE((int)(uint8_t)stored.msg.bytes[1] == 0x43);
	REQUIRE((int)(uint8_t)stored.msg.bytes[2] == 0x10);
	REQUIRE((int)(uint8_t)stored.msg.bytes[3] == 0x4c);
	REQUIRE((int)(uint8_t)stored.msg.bytes[4] == 0x00);
	REQUIRE((int)(uint8_t)stored.msg.bytes[5] == 0x00);
	REQUIRE((int)(uint8_t)stored.msg.bytes[6] == 0xf7);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_NOTE_OFF = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOff(msg, 7, 48)
isOff = midi.isNoteOff(msg)
ch = midi.getChannel(msg)
note = midi.getNote(msg)
)";

TEST_CASE("API midi.setNoteOff", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_NOTE_OFF);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isOff");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(7.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "note");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(48.0));
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_CC_14BIT = R"(--[[
@engine Lua
--]]
msg1 = midi.create()
msg2 = midi.create()
midi.setCc14bit(msg1, msg2, 8, 1, 100.5)
isCc1 = midi.isCc(msg1)
isCc2 = midi.isCc(msg2)
ch1 = midi.getChannel(msg1)
ch2 = midi.getChannel(msg2)
cc1 = midi.getNote(msg1)
cc2 = midi.getNote(msg2)
val1 = midi.getValue(msg1)
val2 = midi.getValue(msg2)
)";

TEST_CASE("API midi.setCc14bit", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_CC_14BIT);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "isCc1");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "isCc2");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(8.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "ch2");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(8.0));
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "cc1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(1.0));  // CC number (MSB)
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "cc2");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(33.0));  // CC number + 32 (LSB)
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "val1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(100.0));  // MSB value
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "val2");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(64.0));  // LSB value (0.5 * 128)
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDI_SET_NRPN = R"(--[[
@engine Lua
--]]
nrpn = midi.createNRPN()
midi.setNRPN(nrpn, 9, 1234, 5678)
)";

TEST_CASE("API midi.setNRPN", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDI_SET_NRPN);
	REQUIRE(m->seLua.L != nullptr);

	// Verify the NRPN was created and set correctly
	// NRPN is 4 CC messages: CC98 (LSB of NRPN number), CC99 (MSB of NRPN number), CC38 (LSB of value), CC6 (MSB of value)
	lua_getglobal(m->seLua.L, "nrpn");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	size_t nrpnIdx = lua_tonumber(m->seLua.L, -1);
	lua_pop(m->seLua.L, 1);

	// Helper to get note/value/channel for a message index
	auto getNote = [&](size_t idx) {
		lua_getglobal(m->seLua.L, "midi");
		lua_getfield(m->seLua.L, -1, "getNote");
		lua_pushnumber(m->seLua.L, idx);
		lua_call(m->seLua.L, 1, 1);
		lua_remove(m->seLua.L, -2);  // remove midi table
		double val = lua_tonumber(m->seLua.L, -1);
		lua_pop(m->seLua.L, 1);
		return val;
	};

	auto getValue = [&](size_t idx) {
		lua_getglobal(m->seLua.L, "midi");
		lua_getfield(m->seLua.L, -1, "getValue");
		lua_pushnumber(m->seLua.L, idx);
		lua_call(m->seLua.L, 1, 1);
		lua_remove(m->seLua.L, -2);  // remove midi table
		double val = lua_tonumber(m->seLua.L, -1);
		lua_pop(m->seLua.L, 1);
		return val;
	};

	auto getChannel = [&](size_t idx) {
		lua_getglobal(m->seLua.L, "midi");
		lua_getfield(m->seLua.L, -1, "getChannel");
		lua_pushnumber(m->seLua.L, idx);
		lua_call(m->seLua.L, 1, 1);
		lua_remove(m->seLua.L, -2);  // remove midi table
		double val = lua_tonumber(m->seLua.L, -1);
		lua_pop(m->seLua.L, 1);
		return val;
	};

	// Check message 1: CC98 (NRPN number LSB) - 1234 & 0x7f = 0x52 = 82
	REQUIRE(getNote(nrpnIdx) == Catch::Approx(98.0));
	REQUIRE(getValue(nrpnIdx) == Catch::Approx(82.0));  // 1234 & 0x7f

	// Check message 2: CC99 (NRPN number MSB) - (1234 >> 7) & 0x7f = 9
	REQUIRE(getNote(nrpnIdx + 1) == Catch::Approx(99.0));
	REQUIRE(getValue(nrpnIdx + 1) == Catch::Approx(9.0));  // (1234 >> 7) & 0x7f

	// Check message 3: CC38 (NRPN value LSB) - 5678 & 0x7f = 46
	REQUIRE(getNote(nrpnIdx + 2) == Catch::Approx(38.0));
	REQUIRE(getValue(nrpnIdx + 2) == Catch::Approx(46.0));  // 5678 & 0x7f

	// Check message 4: CC6 (NRPN value MSB) - (5678 >> 7) & 0x7f = 44
	REQUIRE(getNote(nrpnIdx + 3) == Catch::Approx(6.0));
	REQUIRE(getValue(nrpnIdx + 3) == Catch::Approx(44.0));  // (5678 >> 7) & 0x7f

	// Verify channel is set correctly (channel 9)
	REQUIRE(getChannel(nrpnIdx) == Catch::Approx(9.0));

	Test::destroyModule(m);
}


static const char* LUA_INPUT_ENABLE = R"(--[[
@engine Lua
--]]
input.enable(1)
input.enable(2)
)";

TEST_CASE("API input.enable", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_INPUT_ENABLE);
	REQUIRE(m->seLua.L != nullptr);

	// Verify inputs are enabled by checking the module's inputInfos
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[0])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[1])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[2])->enabled == false);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo*>(m->inputInfos[3])->enabled == false);

	Test::destroyModule(m);
}


static const char* LUA_INPUT_GET_VOLTAGE = R"(--[[
@engine Lua
--]]
input.enable(1)
v1 = input.getVoltage(1)
v2 = input.getVoltage(1, 1)
)";

TEST_CASE("API input.getVoltage", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_INPUT_GET_VOLTAGE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "v1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.0));  // Default voltage is 0
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "v2");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.0));
	lua_pop(m->seLua.L, 1);

	// Test with non-zero voltage on input
	m->inputs[MidiKitModule::INPUT + 0].channels = 1;
	m->inputs[MidiKitModule::INPUT + 0].setVoltage(5.0f);

	// Re-evaluate the script to get updated voltage
	m->loadScript(LUA_INPUT_GET_VOLTAGE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "v1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));  // Should read 5V
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "v2");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(5.0));  // Channel 1 should also read 5V
	lua_pop(m->seLua.L, 1);

	// Test with negative voltage
	m->inputs[MidiKitModule::INPUT + 0].setVoltage(-3.0f);
	m->loadScript(LUA_INPUT_GET_VOLTAGE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "v1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(-3.0));  // Should read -3V
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_INPUT_IS_HIGH_LOW = R"(--[[
@engine Lua
--]]
input.enable(1)
high1 = input.isHigh(1)
low1 = input.isLow(1)
high2 = input.isHigh(1, 1)
low2 = input.isLow(1, 1)
)";

TEST_CASE("API input.isHigh and input.isLow", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_INPUT_IS_HIGH_LOW);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "high1");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);  // Default voltage 0V is not high
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "low1");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);  // Default voltage 0V is low
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "high2");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "low2");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_TRIG_GET_TICKS = R"(--[[
@engine Lua
--]]
ticks = trig.getTicks(1)
)";

TEST_CASE("API trig.getTicks", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_TRIG_GET_TICKS);
	REQUIRE(m->seLua.L != nullptr);

	// Initially no triggers
	lua_getglobal(m->seLua.L, "ticks");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.0));  // No triggers yet
	lua_pop(m->seLua.L, 1);

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
	lua_getglobal(m->seLua.L, "trig");
	lua_getfield(m->seLua.L, -1, "getTicks");
	lua_pushinteger(m->seLua.L, 1);
	lua_call(m->seLua.L, 1, 1);
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(1.0));  // One trigger received
	lua_pop(m->seLua.L, 2);  // pop result and trig table

	// Second pulse
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	args.frame = 2;
	m->process(args);

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	args.frame = 3;
	m->process(args);

	lua_getglobal(m->seLua.L, "trig");
	lua_getfield(m->seLua.L, -1, "getTicks");
	lua_pushinteger(m->seLua.L, 1);
	lua_call(m->seLua.L, 1, 1);
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(2.0));  // Two triggers received
	lua_pop(m->seLua.L, 2);

	Test::destroyModule(m);
}


static const char* LUA_TRIG_IS_HIGH_LOW = R"(--[[
@engine Lua
--]]
high1 = trig.isHigh(1)
low1 = trig.isLow(1)
high2 = trig.isHigh(1, 1)
low2 = trig.isLow(1, 1)
)";

TEST_CASE("API trig.isHigh and trig.isLow", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_TRIG_IS_HIGH_LOW);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "high1");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);  // Default voltage 0V is not high
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "low1");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);  // Default voltage 0V is low
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "high2");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 0);
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "low2");
	REQUIRE(lua_toboolean(m->seLua.L, -1) == 1);
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_TRIG_SET_FUNCTIONS = R"(--[[
@engine Lua
--]]
trig.setGate(1, 100)
trig.setHigh(1)
trig.setLow(1)
trig.setTrigger(1)
)";

TEST_CASE("API trig.setGate, setHigh, setLow, setTrigger", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_TRIG_SET_FUNCTIONS);
	REQUIRE(m->seLua.L != nullptr);

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


static const char* LUA_PARAM_ENABLE = R"(--[[
@engine Lua
--]]
param.enable(1)
param.enable(3)
)";

TEST_CASE("API param.enable", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_PARAM_ENABLE);
	REQUIRE(m->seLua.L != nullptr);

	// Verify parameters are enabled by checking the module's paramQuantities
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[0])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[1])->enabled == false);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[2])->enabled == true);
	REQUIRE(reinterpret_cast<StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity*>(m->paramQuantities[3])->enabled == false);

	Test::destroyModule(m);
}


static const char* LUA_PARAM_GET_VALUE = R"(--[[
@engine Lua
--]]
param.enable(1)
v1 = param.getValue(1)
v2 = param.getValue(2)
)";

TEST_CASE("API param.getValue", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_PARAM_GET_VALUE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "v1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.0));  // Default param value is 0
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "v2");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.0));
	lua_pop(m->seLua.L, 1);

	// Test with non-default parameter value
	m->params[MidiKitModule::PARAM + 0].setValue(0.5f);

	// Re-evaluate the script to get updated value
	m->loadScript(LUA_PARAM_GET_VALUE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "v1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.5));  // Should read 0.5
	lua_pop(m->seLua.L, 1);

	lua_getglobal(m->seLua.L, "v2");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(0.0));  // Param 2 still default
	lua_pop(m->seLua.L, 1);

	// Test with another value
	m->params[MidiKitModule::PARAM + 0].setValue(1.0f);
	m->loadScript(LUA_PARAM_GET_VALUE);
	REQUIRE(m->seLua.L != nullptr);

	lua_getglobal(m->seLua.L, "v1");
	REQUIRE(lua_isnumber(m->seLua.L, -1));
	REQUIRE(lua_tonumber(m->seLua.L, -1) == Catch::Approx(1.0));  // Should read 1.0
	lua_pop(m->seLua.L, 1);

	Test::destroyModule(m);
}


static const char* LUA_MIDIOUT_SEND = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

processMidi = function(port, msg)
    midiOut.send(msg)
end
)";

TEST_CASE("API midiOut.send", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDIOUT_SEND);
	REQUIRE(m->seLua.L != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->seLua.processInMessage(0, msg);
	m->seLua.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->seLua.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0x9);  // NoteOn
	REQUIRE(outMsg.getChannel() == 0);   // channel 1 (0-based)
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);

	Test::destroyModule(m);
}


static const char* LUA_MIDIOUT_SEND_WITH_PORT = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

processMidi = function(port, msg)
    midiOut.send(1, msg)  -- Send to port 1 (1-based)
end
)";

TEST_CASE("API midiOut.send with port", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDIOUT_SEND_WITH_PORT);
	REQUIRE(m->seLua.L != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->seLua.processInMessage(0, msg);
	m->seLua.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->seLua.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outPort == 1);  // Port 2 (1-based) = port 1 (0-based)
	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);

	Test::destroyModule(m);
}


static const char* LUA_MIDIOUT_SEND_AFTER_MS = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

processMidi = function(port, msg)
    midiOut.sendAfterMs(msg, 100)  -- Send after 100ms
end
)";

TEST_CASE("API midiOut.sendAfterMs", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDIOUT_SEND_AFTER_MS);
	REQUIRE(m->seLua.L != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->seLua.processInMessage(0, msg);
	m->seLua.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->seLua.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);
	// Frame should be set to future time
	REQUIRE(outMsg.frame > 0);

	Test::destroyModule(m);
}


static const char* LUA_MIDIOUT_SEND_AFTER_TRIGGER = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

processMidi = function(port, msg)
    midiOut.sendAfterTrigger(msg, 10)  -- Send after 10 ticks on trig port 0
end
)";

TEST_CASE("API midiOut.sendAfterTrigger", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MIDIOUT_SEND_AFTER_TRIGGER);
	REQUIRE(m->seLua.L != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->seLua.processInMessage(0, msg);
	m->seLua.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->seLua.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);
	REQUIRE(outMsg.getValue() == 100);
	// Tick should be set to current trig ticks + 10
	REQUIRE(ticks >= 10);

	Test::destroyModule(m);
}

// midi.create() / midi.createNRPN() outside processMidi()
//
// The message store is reset on every callback, so a handle created at top
// level is silently invalidated before it can be used. That reset is documented
// and intended; these tests pin the warning that makes it visible.

static std::string drainLog(MidiKitModule* m) {
	std::string all;
	while (!m->midiLogMessages.empty()) {
		auto t = m->midiLogMessages.shift();
		all += std::get<2>(t) + "\n";
	}
	return all;
}

static const char* OUTSIDE_CALLBACK_WARNING = "called outside processMidi()";

static const char* LUA_TOPLEVEL_CREATE = R"(--[[
@engine Lua
--]]
g = midi.create()
)";

static const char* LUA_TOPLEVEL_CREATE_NRPN = R"(--[[
@engine Lua
--]]
g = midi.createNRPN()
)";

static const char* LUA_CALLBACK_CREATE = R"(--[[
@engine Lua
--]]
processMidi = function(port, msg)
    local m = midi.create()
    midi.setCc(m, 1, 20, 100)
    midiOut.send(m)
end
)";

TEST_CASE("midi.create outside processMidi warns", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_TOPLEVEL_CREATE);
	REQUIRE(m->seLua.L != nullptr);

	REQUIRE(drainLog(m).find(OUTSIDE_CALLBACK_WARNING) != std::string::npos);

	Test::destroyModule(m);
}

TEST_CASE("midi.createNRPN outside processMidi warns", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_TOPLEVEL_CREATE_NRPN);
	REQUIRE(m->seLua.L != nullptr);

	REQUIRE(drainLog(m).find(OUTSIDE_CALLBACK_WARNING) != std::string::npos);

	Test::destroyModule(m);
}

TEST_CASE("midi.create inside processMidi does not warn", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_CALLBACK_CREATE);
	REQUIRE(m->seLua.L != nullptr);
	drainLog(m);  // discard load-time messages

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	m->seLua.processInMessage(0, msg);
	m->seLua.process();

	REQUIRE(drainLog(m).find(OUTSIDE_CALLBACK_WARNING) == std::string::npos);

	Test::destroyModule(m);
}


// Error reporting with a source position
//
// Lua produces "<chunkname>:<line>: message" by itself, so the line number was
// always available — but luaL_dostring names the chunk after the entire script
// text, which rendered as [string "--[[..."]:7: with the source inlined. The
// script is now loaded under an explicit chunk name so the prefix reads
// "script:7:".

static const char* LUA_BAD_ON_LINE_7 = R"(--[[
@engine Lua
@description test
--]]
local a = 1
local b = 2
this is not lua
local c = 3
)";

TEST_CASE("Lua load error reports a clean chunk name and line", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_BAD_ON_LINE_7);
	REQUIRE(m->seLua.L == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("script:7:") != std::string::npos);
	// The old chunk name dumped the script into the message
	REQUIRE(log.find("[string \"") == std::string::npos);

	Test::destroyModule(m);
}


// Runtime errors inside processMidi carry a position too, and go through the
// same chunk name.
static const char* LUA_RUNTIME_ERROR = R"(--[[
@engine Lua
@description test
--]]
processMidi = function(port, msg)
  local x = nil
  return x.field
end
)";

TEST_CASE("Lua runtime error reports a clean chunk name and line", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_RUNTIME_ERROR);
	REQUIRE(m->seLua.L != nullptr);
	drainLog(m);  // discard load-time messages

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	m->seLua.processInMessage(0, msg);
	m->seLua.process();

	std::string log = drainLog(m);
	// x.field is on line 7
	REQUIRE(log.find("script:7:") != std::string::npos);
	REQUIRE(log.find("[string \"") == std::string::npos);

	Test::destroyModule(m);
}


// A script that loads cleanly must still load cleanly through luaL_loadbuffer.
TEST_CASE("Lua successful load reports no error position", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MAX);
	REQUIRE(m->seLua.L != nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("script:") == std::string::npos);
	REQUIRE(log.find("Script loaded") != std::string::npos);

	Test::destroyModule(m);
}
