#include "MidiKit.test.hpp"


static const char* LUA_EMPTY = R"(--[[
@engine minilua@v1
--]]
)";

TEST_CASE("Lua-tagged script loads and creates Lua state", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_EMPTY);

	REQUIRE(m->host.seLua.L != nullptr);
	REQUIRE(m->host.isLuaEngine());

	Test::destroyModule(m);
}


static const char* LUA_INPUT_NAME = R"(--[[
@engine minilua@v1
--]]
input.getName = function(i) return 'CV-' .. i end
)";

TEST_CASE("Script can override input.getName", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_INPUT_NAME);
	REQUIRE(m->host.seLua.L != nullptr);

	REQUIRE(m->host.seLua.getInputName(0) == "CV-1");
	REQUIRE(m->host.seLua.getInputName(3) == "CV-4");

	Test::destroyModule(m);
}


static const char* QUICKJS_HEADER = R"(/**
 * @engine QuickJs@v1
 */
)";

TEST_CASE("QuickJs-tagged script is rejected by Lua engine", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->host.seLua.loadScript(QUICKJS_HEADER);

	REQUIRE(m->host.seLua.L == nullptr);

	Test::destroyModule(m);
}


static const char* LUA_BAD_SYNTAX = R"(--[[
@engine minilua@v1
--]]
local x = ?
)";

TEST_CASE("Syntax error is handled gracefully", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->host.seLua.loadScript(LUA_BAD_SYNTAX);

	REQUIRE(m->host.seLua.L == nullptr);

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
@engine minilua@v1
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
	REQUIRE(m->host.seLua.L == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("script:7:") != std::string::npos);
	// The old chunk name dumped the script into the message
	REQUIRE(log.find("[string \"") == std::string::npos);

	Test::destroyModule(m);
}


// Runtime errors inside midi.onMessage carry a position too, and go through the
// same chunk name.
static const char* LUA_RUNTIME_ERROR = R"(--[[
@engine minilua@v1
@description test
--]]
midi.onMessage = function(port, msg)
  local x = nil
  return x.field
end
)";

TEST_CASE("Runtime error reports a clean chunk name and line", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_RUNTIME_ERROR);
	REQUIRE(m->host.seLua.L != nullptr);
	drainLog(m);  // discard load-time messages

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	m->host.seLua.processInMessage(0, msg);
	m->host.seLua.process();

	std::string log = drainLog(m);
	// x.field is on line 7
	REQUIRE(log.find("script:7:") != std::string::npos);
	REQUIRE(log.find("[string \"") == std::string::npos);

	Test::destroyModule(m);
}


// A script that loads cleanly must still load cleanly through luaL_loadbuffer.
TEST_CASE("Successful load reports no error position", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();

	m->loadScript(LUA_EMPTY);
	REQUIRE(m->host.seLua.L != nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("script:") == std::string::npos);
	REQUIRE(log.find("Script loaded") != std::string::npos);

	Test::destroyModule(m);
}


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


TEST_CASE("onUnload runs on module destruction without crashing", "[MidiKit][Lua]") {
	// See the matching QuickJs test for why this can only assert "doesn't crash":
	// MidiKitModule's destructor calls closeState() (which runs onUnload())
	// while the module — the engines' handler — is still fully alive, so that
	// callbacks like writeLog/trig.*/input.*/param.* resolve through the
	// handler. Calling them from ~MidiScriptEngineLua() itself, after the
	// module (and its handler) is already gone, would be undefined behaviour.
	MidiKitModule* m = createModule();
	m->loadScript(LUA_ON_UNLOAD);
	REQUIRE(m->host.seLua.L != nullptr);

	Test::destroyModule(m);
}


// ─── Memory / garbage collection ────────────────────────────────────────────

// RAM-usage tests for the Lua engine. Each midi.onMessage callback allocates
// scratch garbage (strings, tables) that nothing retains; across a large number
// of callbacks the heap must not grow. Under real use the engine's automatic
// incremental GC is what keeps it flat, so the no-growth test below does NOT
// collect manually — it only runs callbacks and checks the heap stayed put. A
// retaining script grows the heap roughly linearly with the callback count,
// because reachable objects are never collected (pinned by the retain test).

static const char* LUA_GC_SCRATCH = R"(--[[
@engine minilua@v1
@description test
--]]
midi.onMessage = function(midiPort, msg)
  local n = number.toString(midi.getNote(msg))
  local s = n .. "_" .. n
  local o = { a = 1, b = "b", c = s }
  local t = { s, o, "tail" }
  number.toString(#t)
end
)";

TEST_CASE("Garbage-generating callbacks do not grow RAM usage", "[MidiKit][Lua][GC]") {
	MidiKitModule* m = createModule();
	m->loadScript(LUA_GC_SCRATCH);
	REQUIRE(m->host.seLua.L != nullptr);

	const int warmup = 200;
	const int run = 5000;

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(1);
	msg.setNote(60);
	msg.setValue(100);

	for (int i = 0; i < warmup; i++) {
		m->host.seLua.processInMessage(0, msg);
		m->host.seLua.process();
	}

	size_t used0, total0;
	REQUIRE(m->host.seLua.getMemoryUsage(used0, total0));

	for (int i = 0; i < run; i++) {
		m->host.seLua.processInMessage(0, msg);
		m->host.seLua.process();
	}

	size_t used1, total1;
	REQUIRE(m->host.seLua.getMemoryUsage(used1, total1));

	// The callbacks must have actually run (no load/callback errors), so the
	// allocations really happened rather than the test passing vacuously.
	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);

	// Only the engine's automatic incremental GC is in play — nothing in this
	// test collects. It keeps the heap at a stable equilibrium for a constant
	// per-callback allocation pattern (measured noise here is a few KB); a
	// per-callback leak would grow the heap by tens of kilobytes over this run.
	REQUIRE(used1 <= used0 + 16384);

	Test::destroyModule(m);
}

// Sensitivity control for the test above: a script that DOES retain its
// per-callback allocation (grows a global table) must show up as clear
// growth. Without this the no-growth test could pass vacuously if
// getMemoryUsage stopped reflecting allocations at all.
static const char* LUA_GC_RETAIN = R"(--[[
@engine minilua@v1
@description test
--]]
leaked = {}
count = 0
midi.onMessage = function(midiPort, msg)
  count = count + 1
  leaked[count] = number.toString(midi.getNote(msg)) .. "_"
end
)";

TEST_CASE("Retaining callbacks do grow RAM usage", "[MidiKit][Lua][GC]") {
	MidiKitModule* m = createModule();
	m->loadScript(LUA_GC_RETAIN);
	REQUIRE(m->host.seLua.L != nullptr);

	const int warmup = 20;
	const int run = 3000;

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(1);
	msg.setNote(60);
	msg.setValue(100);

	for (int i = 0; i < warmup; i++) {
		m->host.seLua.processInMessage(0, msg);
		m->host.seLua.process();
	}

	size_t used0, total0;
	REQUIRE(m->host.seLua.getMemoryUsage(used0, total0));

	for (int i = 0; i < run; i++) {
		m->host.seLua.processInMessage(0, msg);
		m->host.seLua.process();
	}

	size_t used1, total1;
	REQUIRE(m->host.seLua.getMemoryUsage(used1, total1));

	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);

	// 3000 retained entries in the global table must be clearly visible even
	// against the automatic GC's equilibrium noise (measured here ~34KB of
	// growth vs ~5KB of noise).
	REQUIRE(used1 > used0 + 16384);

	Test::destroyModule(m);
}



// ── Lua script execution budget ──────────────────────────────────
// A while true do end is aborted via luaL_error(); without the guard the sync
// test hangs and the async test trips barrier()'s timeout.

// File-local Note-On helper (engine.test.cpp's isn't visible here).
static midi::Message noteOn(int ch, int note, int vel) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(vel);
	return msg;
}

// Valid script proving the engine recovers after a failed load.
static const char* LUA_RECOVERY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("recovered")
end
)";

static const char* LUA_WHILE_TRUE_ONMESSAGE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    while true do end
end
)";

TEST_CASE("Infinite loop in onMessage is interrupted, not a hang", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();
	m->loadScript(LUA_WHILE_TRUE_ONMESSAGE);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	// SyncTaskWorker runs inline — without the count hook this would hang.
	m->host.getActiveEngine()->process();

	std::string log = drainLog(m);
	REQUIRE(log.find("onMessage error") != std::string::npos);
	REQUIRE(log.find("exceeded execution budget") != std::string::npos);

	// Engine recovered: a second message is interrupted again.
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	std::string log2 = drainLog(m);
	REQUIRE(log2.find("exceeded execution budget") != std::string::npos);

	Test::destroyModule(m);
}

TEST_CASE("Infinite loop in onMessage does not wedge the shared worker", "[MidiKit][Lua][Async]") {
	auto worker = asyncWorker();
	MidiKitModule* m = createModule(worker);

	m->loadScript(LUA_WHILE_TRUE_ONMESSAGE);
	barrier(worker, 10.0);        // the LOAD is async; wait for it
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();   // enqueues the dispatch task

	// Without the count hook the worker spins forever and barrier() times out.
	barrier(worker, 10.0);

	std::string log = drainLog(m);
	REQUIRE(log.find("exceeded execution budget") != std::string::npos);

	// A second message still dispatches — the worker recovered.
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	barrier(worker, 10.0);
	std::string log2 = drainLog(m);
	REQUIRE(log2.find("exceeded execution budget") != std::string::npos);

	Test::destroyModule(m);
}

static const char* LUA_WHILE_TRUE_TOPLEVEL = R"(--[[
@engine minilua@v1
--]]
while true do end
)";

TEST_CASE("Infinite loop at script top level fails the load, and the module recovers", "[MidiKit][Lua]") {
	MidiKitModule* m = createModule();
	// Load runs inline; the count hook aborts the top-level lua_pcall.
	m->loadScript(LUA_WHILE_TRUE_TOPLEVEL);
	std::string log = drainLog(m);
	REQUIRE(log.find("Error loading script") != std::string::npos);
	REQUIRE(log.find("exceeded execution budget") != std::string::npos);

	// The module survives the failed load.
	m->loadScript(LUA_RECOVERY);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	std::string reloadLog = drainLog(m);
	REQUIRE(reloadLog.find("recovered") != std::string::npos);

	Test::destroyModule(m);
}

// ── Memory limit ────────────────────────────────────────────────────────────
// A retaining/allocation-heavy script is stopped (state torn down, memory
// freed) once its GC-managed heap passes memoryLimit (1 MiB, like QuickJS).
// Stop happens after the callback that crossed the line, so dispatch after
// that no-ops via a null L. The module stays usable — a fresh load resets the
// watchdog.

// Allocates 2 MiB at top level — a single string.rep, well past the limit.
static const char* LUA_MEMORY_BLOWUP_LOAD = R"(--[[
@engine minilua@v1
--]]
big = string.rep("x", 2 * 1024 * 1024)
)";

TEST_CASE("Script that exceeds the memory limit at load is stopped", "[MidiKit][Lua][MemoryLimit]") {
	MidiKitModule* m = createModule();
	m->loadScript(LUA_MEMORY_BLOWUP_LOAD);

	// The top-level allocation passed the limit; the engine stopped itself.
	REQUIRE(m->host.seLua.L == nullptr);
	std::string log = drainLog(m);
	REQUIRE(log.find("memory limit and was stopped") != std::string::npos);

	// The module recovers: loading a normal script resets the watchdog.
	m->loadScript(LUA_RECOVERY);
	REQUIRE(m->host.seLua.L != nullptr);
	drainLog(m);
	midi::Message in = noteOn(1, 60, 100);
	m->host.seLua.processInMessage(0, in);
	m->host.seLua.process();
	REQUIRE(drainLog(m).find("recovered") != std::string::npos);

	Test::destroyModule(m);
}

// Retains 4 KiB per onMessage in a global table, so the heap crosses the limit
// after a few hundred messages.
static const char* LUA_MEMORY_BLOWUP_RETAIN = R"(--[[
@engine minilua@v1
--]]
leaked = {}
count = 0
midi.onMessage = function(midiPort, msg)
  count = count + 1
  leaked[count] = string.rep("x", 4096)
end
)";

TEST_CASE("Retaining script is stopped when it exceeds the memory limit", "[MidiKit][Lua][MemoryLimit]") {
	MidiKitModule* m = createModule();
	m->loadScript(LUA_MEMORY_BLOWUP_RETAIN);
	REQUIRE(m->host.seLua.L != nullptr);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	// Keep sending until the watchdog has certainly fired and torn the state
	// down (a few hundred callbacks cross 1 MiB; this leaves a wide margin).
	for (int i = 0; i < 2000; i++) {
		m->host.seLua.processInMessage(0, in);
		m->host.seLua.process();
	}

	REQUIRE(m->host.seLua.L == nullptr);
	std::string log = drainLog(m);
	REQUIRE(log.find("memory limit and was stopped") != std::string::npos);

	Test::destroyModule(m);
}

// A script that stays within the limit must NOT be stopped.
TEST_CASE("Script within the memory limit keeps running", "[MidiKit][Lua][MemoryLimit]") {
	MidiKitModule* m = createModule();
	m->loadScript(LUA_RECOVERY);
	REQUIRE(m->host.seLua.L != nullptr);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	for (int i = 0; i < 50; i++) {
		m->host.seLua.processInMessage(0, in);
		m->host.seLua.process();
	}

	REQUIRE(m->host.seLua.L != nullptr);
	REQUIRE(drainLog(m).find("memory limit and was stopped") == std::string::npos);

	Test::destroyModule(m);
}