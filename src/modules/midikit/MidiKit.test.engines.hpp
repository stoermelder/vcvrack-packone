// ── Behaviour both engines share ─────────────────────────────────────────────
// One Catch leaf per engine. The Lua-only and QuickJs-only sections follow.

static bool scriptLoaded(MidiKitModule* m, Lang lang) {
	return lang == Lang::Js ? m->host.seQuickJs.ctx != nullptr : m->host.seLua.L != nullptr;
}

// Valid script proving the engine recovers after a failed load.
static const char* LUA_RECOVERY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("recovered")
end
)";

static const char* QJS_RECOVERY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("recovered");
};
)";

static const Pair RECOVERY{QJS_RECOVERY, LUA_RECOVERY};

TEST_CASE("Tagged script loads and creates its engine state", "[MidiKit][Engine]") {
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(EMPTY(lang));

	REQUIRE(scriptLoaded(m, lang));
	REQUIRE((lang == Lang::Js ? m->host.isQuickJsEngine() : m->host.isLuaEngine()));
}

TEST_CASE("Script tagged for the other engine is rejected", "[MidiKit][Engine]") {
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// A Lua engine is handed a QuickJs script and the other way round.
	if (lang == Lang::Lua) m->host.seLua.loadScriptOnWorker(EMPTY(Lang::Js), "");
	else m->host.seQuickJs.loadScriptOnWorker(EMPTY(Lang::Lua), "");

	REQUIRE_FALSE(scriptLoaded(m, lang));
}

// A script that loads cleanly must not emit any error noise.
TEST_CASE("Successful load reports no error", "[MidiKit][Engine]") {
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(script(lang, "", "@description test"));
	REQUIRE(scriptLoaded(m, lang));

	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);
	REQUIRE(log.find("script:") == std::string::npos);
	REQUIRE(log.find("Script loaded") != std::string::npos);
}


// A runtime error in a callback carries the script line, in both engines
// (Lua: "script:6: ...", QuickJs: a stack line "at <anonymous> (script:6:...)").
static const Pair RUNTIME_ERROR_ON_LINE{
R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
  let x = null;
  return x.field;
};
)",
R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
  local x = nil
  return x.field
end
)"};

TEST_CASE("A runtime error in onMessage reports the line it happened on", "[MidiKit][Engine]") {
	FOR_EACH_LANG;
	Kit<> kit;
	kit.loadRaw(RUNTIME_ERROR_ON_LINE.get(lang));
	kit.dispatch(msg::noteOn(1, 60, 100));
	std::string log = kit.log();
	CATCH_INFO("log:\n" << log);
	REQUIRE(log.find("onMessage error") != std::string::npos);
	// x.field is on line 6 in both scripts.
	REQUIRE(log.find("script:6:") != std::string::npos);
}


// ── Memory / garbage collection ─────────────────────────────────────────────
// Each midi.onMessage callback allocates scratch garbage (strings, tables) that
// nothing retains; across a large number of callbacks the heap must not grow.
// The engine's automatic GC is what keeps it flat, so the no-growth test does
// not collect manually (QuickJs: it only forces a pass before each reading, to
// measure the live set rather than transient garbage). A retaining script grows
// the heap with the callback count; that test is the sensitivity control, since
// without it the no-growth test could pass vacuously if getMemoryUsage stopped
// reflecting allocations.

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

static const char* QJS_GC_SCRATCH = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	let n = number.toString(midi.getNote(msg));
	let s = n + "_" + n;
	let o = { a: 1, b: "b", c: s };
	let arr = [s, o, "tail"];
	number.toString(arr.length);
}
)";

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

static const char* QJS_GC_RETAIN = R"(/**
 * @engine QuickJs@v1
 */
var leaked = [];
midi.onMessage = function(midiPort, msg) {
	leaked.push(number.toString(midi.getNote(msg)) + "_");
}
)";

// Runs `count` note-ons through the active engine.
static void feedNotes(MidiKitModule* m, int count) {
	midi::Message msg = noteOn(1, 60, 100);
	for (int i = 0; i < count; i++) {
		m->host.getActiveEngine()->processInMessage(0, QueuedMessage(msg));
		m->host.getActiveEngine()->process();
	}
}

// The heap in use, after a forced GC pass on QuickJs.
static size_t heapUsed(MidiKitModule* m, Lang lang, size_t* total = nullptr) {
	if (lang == Lang::Js) {
		JS_RunGC(m->host.seQuickJs.rt);
		m->host.seQuickJs.publishMemoryUsage();   // the snapshot is taken after dispatch, not after the GC
	}
	size_t used = 0, tot = 0;
	REQUIRE(m->host.getActiveEngine()->getMemoryUsage(used, tot));
	if (total) *total = tot;
	return used;
}

TEST_CASE("Garbage-generating callbacks do not grow RAM usage", "[MidiKit][Engine][GC]") {
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(Pair{QJS_GC_SCRATCH, LUA_GC_SCRATCH}.get(lang));
	REQUIRE(scriptLoaded(m, lang));

	feedNotes(m, 1000);
	size_t used0 = heapUsed(m, lang);
	feedNotes(m, 5000);
	size_t total1;
	size_t used1 = heapUsed(m, lang, &total1);

	// The callbacks must have actually run (no load/callback errors), so the
	// allocations really happened rather than the test passing vacuously.
	REQUIRE(drainLog(m).find("rror") == std::string::npos);

	// A per-callback leak would grow the heap by tens of kilobytes over this run;
	// the margin covers the equilibrium noise (a few KB) and allocator rounding.
	if (lang == Lang::Js) {
		REQUIRE(used1 < total1);
		REQUIRE(used1 <= used0 + 64 * 1024);
	}
	else {
		REQUIRE(used1 <= used0 + 16384);
	}
}

TEST_CASE("Retaining callbacks do grow RAM usage", "[MidiKit][Engine][GC]") {
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(Pair{QJS_GC_RETAIN, LUA_GC_RETAIN}.get(lang));
	REQUIRE(scriptLoaded(m, lang));

	feedNotes(m, 20);
	size_t used0 = heapUsed(m, lang);
	feedNotes(m, lang == Lang::Js ? 200 : 3000);
	size_t used1 = heapUsed(m, lang);

	REQUIRE(drainLog(m).find("rror") == std::string::npos);

	// Lua: 3000 retained entries, ~34KB of growth against ~5KB of GC noise.
	// QuickJs: 200 retained strings and their array slots.
	REQUIRE(used1 > used0 + (lang == Lang::Js ? 2048 : 16384));
}


// ── Script execution budget ─────────────────────────────────────────────────
// A runaway loop is aborted (Lua: luaL_error from the count hook, QuickJs: the
// uncatchable "interrupted"); without the guard the sync tests hang and the
// async one trips barrier()'s timeout.

static const Pair WHILE_TRUE_ONMESSAGE{
R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    while (true) {}
};
)",
R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    while true do end
end
)"};

static const Pair WHILE_TRUE_TOPLEVEL{
R"(/**
 * @engine QuickJs@v1
 */
while (true) {}
)",
R"(--[[
@engine minilua@v1
--]]
while true do end
)"};

static const char* budgetMarker(Lang lang) {
	return lang == Lang::Js ? "interrupted" : "exceeded execution budget";
}

TEST_CASE("Infinite loop in onMessage is interrupted, not a hang", "[MidiKit][Engine]") {
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(WHILE_TRUE_ONMESSAGE.get(lang));
	drainLog(m);

	// SyncTaskWorker runs inline: without the guard this would hang.
	feedNotes(m, 1);

	std::string log = drainLog(m);
	REQUIRE(log.find("onMessage error") != std::string::npos);
	REQUIRE(log.find(budgetMarker(lang)) != std::string::npos);

	// Engine recovered: a second message is interrupted again.
	feedNotes(m, 1);
	REQUIRE(drainLog(m).find(budgetMarker(lang)) != std::string::npos);
}

TEST_CASE("Infinite loop in onMessage does not wedge the shared worker", "[MidiKit][Engine][Async]") {
	FOR_EACH_LANG;
	auto worker = asyncWorker();
	Kit<> kit(worker);
	MidiKitModule* m = kit.m;

	m->loadScript(WHILE_TRUE_ONMESSAGE.get(lang));
	barrier(worker, 10.0);        // the LOAD is async; wait for it
	drainLog(m);

	feedNotes(m, 1);              // enqueues the dispatch task

	// Without the guard the worker spins forever and barrier() times out.
	barrier(worker, 10.0);
	REQUIRE(drainLog(m).find(budgetMarker(lang)) != std::string::npos);

	// A second message still dispatches: the worker recovered.
	feedNotes(m, 1);
	barrier(worker, 10.0);
	REQUIRE(drainLog(m).find(budgetMarker(lang)) != std::string::npos);
}

TEST_CASE("Infinite loop at script top level fails the load, and the module recovers", "[MidiKit][Engine]") {
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	// Load runs inline; the guard aborts the top-level evaluation.
	m->loadScript(WHILE_TRUE_TOPLEVEL.get(lang));
	std::string log = drainLog(m);
	REQUIRE(log.find("Error loading script") != std::string::npos);
	REQUIRE(log.find(budgetMarker(lang)) != std::string::npos);

	// The module survives the failed load.
	m->loadScript(RECOVERY.get(lang));
	drainLog(m);

	feedNotes(m, 1);
	REQUIRE(drainLog(m).find("recovered") != std::string::npos);
}


// ═══ Lua ════════════════════════════════════════════════════════════════════

static const char* LUA_INPUT_NAME = R"(--[[
@engine minilua@v1
--]]
input.onTooltip = function(i) return 'CV-' .. i end
)";

TEST_CASE("Script can override input.onTooltip", "[MidiKit][Lua]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(LUA_INPUT_NAME);
	REQUIRE(m->host.seLua.L != nullptr);

	REQUIRE(m->host.seLua.getInputName(0) == "CV-1");
	REQUIRE(m->host.seLua.getInputName(3) == "CV-4");
}



// Each input index runs one json case, so a test reads the result through the tooltip.
static const char* LUA_JSON_CASES = R"LUA(--[[
@engine minilua@v1
--]]
local cases = {
  function() return json.encode({ 1, 2.5, -3, "a", true, false }) end,
  function() return json.encode({ k = { 1, { z = 0 } } }) end,
  function() return json.encode("q\"\\\n\t/") end,
  function() return json.encode({}) end,
  function() local t = json.decode('[null, 1]') return tostring(t[1]) .. ',' .. t[2] end,
  function() return json.decode('"\\u00e9\\u20ac"') end,
  function() local t = json.decode(' { "x" : [ 1e2 , -0.5 ] } ') return t.x[1] .. ',' .. t.x[2] end,
  function() local ok, err = pcall(json.decode, '{"a":') return tostring(ok) .. ':' .. tostring(err ~= nil) end,
  function() local ok = pcall(json.decode, '[1 2]') return tostring(ok) end,
  function() local ok = pcall(json.encode, { 1, nil, 3, x = 1 }) return tostring(ok) end,
  function() local ok = pcall(json.encode, 0 / 0) return tostring(ok) end,
  function() local ok = pcall(json.encode, function() end) return tostring(ok) end,
  function()
    local src = { name = "n", list = { 1, 2, { deep = { true } } }, num = 12.25 }
    local back = json.decode(json.encode(src))
    return back.name .. tostring(back.list[3].deep[1]) .. back.num .. #back.list
  end,
}
input.onTooltip = function(i) return cases[i]() end
)LUA";

static std::string luaJsonCase(MidiKitModule* m, int n) {
	return m->host.seLua.getInputName(n - 1);   // the script's cases are 1-based
}

TEST_CASE("Lua json.encode produces compact JSON for arrays, objects and strings", "[MidiKit][Lua][JSON]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_JSON_CASES);
	REQUIRE(m->host.seLua.L != nullptr);

	REQUIRE(luaJsonCase(m, 1) == "[1,2.5,-3,\"a\",true,false]");
	REQUIRE(luaJsonCase(m, 2) == "{\"k\":[1,{\"z\":0}]}");
	REQUIRE(luaJsonCase(m, 3) == "\"q\\\"\\\\\\n\\t/\"");
	REQUIRE(luaJsonCase(m, 4) == "[]");
}

TEST_CASE("Lua json.decode handles null, unicode escapes, whitespace and exponents", "[MidiKit][Lua][JSON]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_JSON_CASES);
	REQUIRE(m->host.seLua.L != nullptr);

	REQUIRE(luaJsonCase(m, 5) == "nil,1");
	REQUIRE(luaJsonCase(m, 6) == "\xC3\xA9\xE2\x82\xAC");   // é€ as UTF-8
	REQUIRE(luaJsonCase(m, 7) == "100.0,-0.5");
}

TEST_CASE("Lua json raises errors for malformed input and unsupported values", "[MidiKit][Lua][JSON]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_JSON_CASES);
	REQUIRE(m->host.seLua.L != nullptr);

	REQUIRE(luaJsonCase(m, 8) == "false:true");   // truncated document
	REQUIRE(luaJsonCase(m, 9) == "false");         // missing comma
	REQUIRE(luaJsonCase(m, 10) == "false");        // mixed array/object table
	REQUIRE(luaJsonCase(m, 11) == "false");        // NaN
	REQUIRE(luaJsonCase(m, 12) == "false");        // function
}

TEST_CASE("Lua json round-trips a nested value", "[MidiKit][Lua][JSON]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_JSON_CASES);
	REQUIRE(m->host.seLua.L != nullptr);

	REQUIRE(luaJsonCase(m, 13) == "ntrue12.253");
}

TEST_CASE("Lua json is available again after a script reload", "[MidiKit][Lua][JSON]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_JSON_CASES);
	REQUIRE(luaJsonCase(m, 4) == "[]");
	m->loadScript(LUA_INPUT_NAME);   // a script that never touches json
	m->loadScript(LUA_JSON_CASES);
	REQUIRE(m->host.seLua.L != nullptr);
	REQUIRE(luaJsonCase(m, 4) == "[]");
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
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(LUA_BAD_ON_LINE_7);
	REQUIRE(m->host.seLua.L == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("script:7:") != std::string::npos);
	// The old chunk name dumped the script into the message
	REQUIRE(log.find("[string \"") == std::string::npos);
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
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(LUA_RUNTIME_ERROR);
	REQUIRE(m->host.seLua.L != nullptr);
	drainLog(m);  // discard load-time messages

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	m->host.seLua.processInMessage(0, QueuedMessage(msg));
	m->host.seLua.process();

	std::string log = drainLog(m);
	// x.field is on line 7
	REQUIRE(log.find("script:7:") != std::string::npos);
	REQUIRE(log.find("[string \"") == std::string::npos);
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
	Kit<> kit;
	MidiKitModule* m = kit.m;
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
	m->host.seLua.processInMessage(0, QueuedMessage(in));
	m->host.seLua.process();
	REQUIRE(drainLog(m).find("recovered") != std::string::npos);
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
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_MEMORY_BLOWUP_RETAIN);
	REQUIRE(m->host.seLua.L != nullptr);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	// Keep sending until the watchdog has certainly fired and torn the state
	// down (a few hundred callbacks cross 1 MiB; this leaves a wide margin).
	for (int i = 0; i < 2000; i++) {
		m->host.seLua.processInMessage(0, QueuedMessage(in));
		m->host.seLua.process();
	}

	REQUIRE(m->host.seLua.L == nullptr);
	std::string log = drainLog(m);
	REQUIRE(log.find("memory limit and was stopped") != std::string::npos);
}

// A script that stays within the limit must NOT be stopped.
TEST_CASE("Script within the memory limit keeps running", "[MidiKit][Lua][MemoryLimit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_RECOVERY);
	REQUIRE(m->host.seLua.L != nullptr);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	for (int i = 0; i < 50; i++) {
		m->host.seLua.processInMessage(0, QueuedMessage(in));
		m->host.seLua.process();
	}

	REQUIRE(m->host.seLua.L != nullptr);
	REQUIRE(drainLog(m).find("memory limit and was stopped") == std::string::npos);
}

// A script travels inside the patch file, so it must not read files from disk
// or load precompiled bytecode.
static const char* LUA_SANDBOX = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
	rack.log("dofile=" .. type(dofile))
	rack.log("loadfile=" .. type(loadfile))
	rack.log("load=" .. type(load))
	rack.log("dump=" .. type(string.dump))
	rack.log("print=" .. type(print))
end
)";

TEST_CASE("Lua sandbox removes file and bytecode loaders", "[MidiKit][Lua]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(LUA_SANDBOX);
	REQUIRE(m->host.seLua.L != nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("dofile=nil") != std::string::npos);
	REQUIRE(log.find("loadfile=nil") != std::string::npos);
	REQUIRE(log.find("load=nil") != std::string::npos);
	REQUIRE(log.find("dump=nil") != std::string::npos);
	REQUIRE(log.find("print=nil") != std::string::npos);
}


// Each input index runs one string.split case; the tooltip shows count and pieces as "n:a|b|c".
static const char* LUA_SPLIT_CASES = R"LUA(--[[
@engine minilua@v1
--]]
local function show(t)
  return #t .. ":" .. table.concat(t, "|")
end
local cases = {
  function() return show(string.split("a,b,c", ",")) end,
  function() return show(("a,,b"):split(",")) end,
  function() return show(("a,b,"):split(",")) end,
  function() return show((",a"):split(",")) end,
  function() return show((""):split(",")) end,
  function() return show((""):split("")) end,
  function() return show(("abc"):split("")) end,
  function() return show(("abc"):split()) end,
  function() return show(("a, b, c"):split(", ")) end,
  function() return show(("a.b.c"):split(".")) end,
  function() return show(("a1b22c"):split("%d")) end,
  function() return show(("a,b,c,d"):split(",", 2)) end,
  function() return show(("a,b,c"):split(",", 0)) end,
  function() return show(("a,b,c"):split(",", 10)) end,
  function() return show(("abc"):split("", 2)) end,
  function() return show(("nocomma"):split(",")) end,
  function() local ok = pcall(string.split, "a", 5) return tostring(ok) end,
  function() local ok = pcall(string.split, nil, ",") return tostring(ok) end,
  function() local ok = pcall(string.split, "a", ",", "x") return tostring(ok) end,
}
input.onTooltip = function(i) return cases[i]() end
)LUA";

TEST_CASE("Lua string.split follows JavaScript's split", "[MidiKit][Lua][Split]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(LUA_SPLIT_CASES);
	REQUIRE(m->host.seLua.L != nullptr);
	auto c = [&](int n) { return m->host.seLua.getInputName(n - 1); };

	REQUIRE(c(1) == "3:a|b|c");
	REQUIRE(c(2) == "3:a||b");        // empty fields are kept
	REQUIRE(c(3) == "3:a|b|");        // trailing separator gives a final empty piece
	REQUIRE(c(4) == "2:|a");          // leading too
	REQUIRE(c(5) == "1:");            // "".split(",") is [""]
	REQUIRE(c(6) == "0:");            // "".split("") is []
	REQUIRE(c(7) == "3:a|b|c");       // empty separator: single bytes
	REQUIRE(c(8) == "1:abc");         // no separator: the whole string
	REQUIRE(c(9) == "3:a|b|c");       // multi-character separator
	REQUIRE(c(10) == "3:a|b|c");      // "." is plain text, not a pattern
	REQUIRE(c(11) == "1:a1b22c");     // "%d" is plain text too
	REQUIRE(c(12) == "2:a|b");        // limit truncates the result
	REQUIRE(c(13) == "0:");
	REQUIRE(c(14) == "3:a|b|c");
	REQUIRE(c(15) == "2:a|b");
	REQUIRE(c(16) == "1:nocomma");
	REQUIRE(c(17) == "false");        // separator must be a string
	REQUIRE(c(18) == "false");
	REQUIRE(c(19) == "false");        // limit must be a number
}


// ═══ QuickJs ════════════════════════════════════════════════════════════════

static const char* QJS_ONLY_ENGINE = R"(/**
 * @engine QuickJs@v1
 */
)";

TEST_CASE("QuickJs script loads with @engine as the only header tag", "[MidiKit][QuickJs]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(QJS_ONLY_ENGINE);

	REQUIRE(m->host.seQuickJs.ctx != nullptr);
	REQUIRE(m->host.isQuickJsEngine());
}

// Parse-error reporting with a source position
//
// QuickJS's own SyntaxError carries a "stack" property with file/line
// information (e.g. "script:6:10"), which formatError() appends after the
// bare message — a real position.
static const char* QJS_BAD_ON_LINE_6 = R"(/**
 * @engine QuickJs@v1
 */
let a = 1;
let b = 2;
let c = ;
let d = 3;
)";

TEST_CASE("Parse error reports the line it failed on", "[MidiKit][QuickJs]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(QJS_BAD_ON_LINE_6);
	// A failed load tears the state down
	REQUIRE(m->host.seQuickJs.ctx == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("Error loading script") != std::string::npos);
	// QuickJS reports the offending line number in the exception's stack trace
	REQUIRE(log.find(":6:") != std::string::npos);
}


// Same defect one line earlier — pins that the number tracks the error rather
// than being a constant that happens to match.
static const char* QJS_BAD_ON_LINE_5 = R"(/**
 * @engine QuickJs@v1
 */
let a = 1;
let b = ;
let c = 3;
)";

TEST_CASE("Parse error line number tracks the error position", "[MidiKit][QuickJs]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(QJS_BAD_ON_LINE_5);
	REQUIRE(m->host.seQuickJs.ctx == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find(":5:") != std::string::npos);
	REQUIRE(log.find(":6:") == std::string::npos);
}


static const char* QJS_ON_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {}
rack.onUnload = function() {
	rack.log("onUnload ran");
	let msg = midi.create();
	midi.setNoteOff(msg, 1, 60);
	midiOut.send(msg);
}
)";


TEST_CASE("onUnload runs on module destruction without crashing", "[MidiKit][QuickJs]") {
	// Regression guard: writeLog/writeOverlay/input.*/trig.*/param.* live on
	// the MidiScriptEngineHandler (implemented by MidiKitModule). Running
	// onUnload from ~MidiScriptEngineQuickJs() itself would route those
	// callbacks through a handler that is already destroyed — undefined
	// behaviour that crashes as "pure virtual function called". MidiKitModule
	// has its own destructor that calls host.unload() first, while the module
	// (the handler) is still fully alive, specifically to avoid that. This
	// test does not (and cannot) assert a log/message result — it can only
	// prove destroyModule() doesn't crash, which is what it's for.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(QJS_ON_UNLOAD);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);
}


static const char* QJS_MIDI_ROUNDTRIP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, m) {
	if (midi.getType(m) === midi.CC) {
		let out = midi.clone(m);
		midi.setChannel(out, 2);
		midiOut.selectPort(1);
		midiOut.send(out);
		rack.log("got cc " + midi.getValue(m));
	}
}
)";

TEST_CASE("midi.onMessage dispatch round-trips a CC message through midi.*/midiOut.*", "[MidiKit][QuickJs]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(QJS_MIDI_ROUNDTRIP);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);
	REQUIRE(JS_IsFunction(m->host.seQuickJs.ctx, m->host.seQuickJs.onMessageFn));

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	msg.setChannel(0);
	msg.setNote(20);
	msg.setValue(99);

	m->host.seQuickJs.processInMessage(0, QueuedMessage(msg));
	m->host.seQuickJs.process();

	int port, ticks;
	midi::Message out;
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getStatus() == 0xb);
	REQUIRE(out.getChannel() == 1);
	REQUIRE(out.getNote() == 20);
	REQUIRE(out.getValue() == 99);

	std::string log = drainLog(m);
	REQUIRE(log.find("got cc 99") != std::string::npos);
}
