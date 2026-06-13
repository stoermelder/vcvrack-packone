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

TEST_CASE("MidiKit Lua: Lua-tagged script loads and creates Lua state", "[MidiKit][Lua]") {
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

TEST_CASE("MidiKit Lua: script body runs synchronously on load", "[MidiKit][Lua]") {
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

TEST_CASE("MidiKit Lua: number.rescale API works from script body", "[MidiKit][Lua]") {
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

TEST_CASE("MidiKit Lua: script can override input.getName", "[MidiKit][Lua]") {
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

TEST_CASE("MidiKit Lua: Elk-tagged script is rejected by Lua engine", "[MidiKit][Lua]") {
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

TEST_CASE("MidiKit Lua: syntax error is handled gracefully", "[MidiKit][Lua]") {
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

TEST_CASE("MidiKit Lua: simple CC reroute script", "[MidiKit][Lua]") {
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