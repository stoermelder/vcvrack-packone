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


static std::string drainLog(MidiKitModule* m) {
	std::string all;
	while (!m->midiLogMessages.empty()) {
		auto t = m->midiLogMessages.shift();
		all += std::get<2>(t) + "\n";
	}
	return all;
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

TEST_CASE("Load error reports a clean chunk name and line", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_BAD_ON_LINE_7);
	REQUIRE(m->seLua.L == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("script:7:") != std::string::npos);
	// The old chunk name dumped the script into the message
	REQUIRE(log.find("[string \"") == std::string::npos);

	Test::destroyModule(m);
}


// Runtime errors inside onMidiMessage carry a position too, and go through the
// same chunk name.
static const char* LUA_RUNTIME_ERROR = R"(--[[
@engine Lua
@description test
--]]
onMidiMessage = function(port, msg)
  local x = nil
  return x.field
end
)";

TEST_CASE("Runtime error reports a clean chunk name and line", "[MidiKit][Lua]") {
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
TEST_CASE("Successful load reports no error position", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_MAX);
	REQUIRE(m->seLua.L != nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("script:") == std::string::npos);
	REQUIRE(log.find("Script loaded") != std::string::npos);

	Test::destroyModule(m);
}


static const char* LUA_ON_UNLOAD = R"(--[[
@engine Lua
--]]
onMidiMessage = function(midiPort, msg) end
onUnload = function()
	rack.log("onUnload ran")
	local msg = midi.create()
	midi.setNoteOff(msg, 1, 60)
	midiOut.send(msg)
end
)";


TEST_CASE("onUnload runs on module destruction without crashing", "[MidiKit][Lua]") {
	// See the matching Elk test for why this can only assert "doesn't crash":
	// MidiKitModule's destructor calls closeState() (which runs onUnload())
	// while se/seLua are still fully alive, specifically so that virtuals
	// like writeLog/trig.*/input.*/param.* resolve correctly — calling them
	// from ~MidiScriptEngineLua() itself, after MidiKitScriptEngineLua's part
	// of the object is already gone, would be undefined behaviour.
	MidiKitModule* m = createModule();
	m->loadScript(LUA_ON_UNLOAD);
	REQUIRE(m->seLua.L != nullptr);

	Test::destroyModule(m);
}
