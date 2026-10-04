// rack.setConfig()/getConfig(): types, validation, caps, persistence and patch round-trips.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.

// round-trip of every JSON type

TEST_CASE("setConfig() then getConfig() round-trips every JSON type, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string log = drainLog(m);
		// One "ok:<n>" line per assertion inside the script — the pass/fail
		// itself is evaluated in-script so both engines exercise identical
		// logic rather than the test re-deriving expectations per language.
		REQUIRE(log.find("FAIL") == std::string::npos);
		REQUIRE(log.find("ok:7") != std::string::npos);

		Test::destroyModule(m);
	};

	static const char* JS_ROUNDTRIP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
let n = 0;
function check(cond, name) {
    if (cond) { n++; rack.log("ok:" + n); }
    else { rack.log("FAIL " + name); }
}
rack.setConfig("bool", true);
check(rack.getConfig("bool") === true, "bool");
rack.setConfig("num", 42);
check(rack.getConfig("num") === 42, "num");
rack.setConfig("str", "hello");
check(rack.getConfig("str") === "hello", "str");
rack.setConfig("arr", [1, 2, 3]);
let arr = rack.getConfig("arr");
check(arr.length === 3 && arr[0] === 1 && arr[2] === 3, "arr");
rack.setConfig("obj", { a: 1, b: "two" });
let obj = rack.getConfig("obj");
check(obj.a === 1 && obj.b === "two", "obj");
rack.setConfig("nested", { list: [{ x: 1 }, { x: 2 }] });
let nested = rack.getConfig("nested");
check(nested.list.length === 2 && nested.list[0].x === 1 && nested.list[1].x === 2, "nested");
rack.setConfig("negfloat", -3.5);
check(rack.getConfig("negfloat") === -3.5, "negfloat");
)";
	static const char* LUA_ROUNDTRIP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
local n = 0
local function check(cond, name)
    if cond then n = n + 1; rack.log("ok:" .. n)
    else rack.log("FAIL " .. name) end
end
rack.setConfig("bool", true)
check(rack.getConfig("bool") == true, "bool")
rack.setConfig("num", 42)
check(rack.getConfig("num") == 42, "num")
rack.setConfig("str", "hello")
check(rack.getConfig("str") == "hello", "str")
rack.setConfig("arr", {1, 2, 3})
local arr = rack.getConfig("arr")
check(#arr == 3 and arr[1] == 1 and arr[3] == 3, "arr")
rack.setConfig("obj", { a = 1, b = "two" })
local obj = rack.getConfig("obj")
check(obj.a == 1 and obj.b == "two", "obj")
rack.setConfig("nested", { list = {{ x = 1 }, { x = 2 }} })
local nested = rack.getConfig("nested")
check(#nested.list == 2 and nested.list[1].x == 1 and nested.list[2].x == 2, "nested")
rack.setConfig("negfloat", -3.5)
check(rack.getConfig("negfloat") == -3.5, "negfloat")
)";
	check(JS_ROUNDTRIP);
	check(LUA_ROUNDTRIP);
}


TEST_CASE("getConfig() for an unset key returns the default, or undefined/nil without one, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string log = drainLog(m);
		REQUIRE(log.find("FAIL") == std::string::npos);
		REQUIRE(log.find("ok:3") != std::string::npos);

		Test::destroyModule(m);
	};

	static const char* JS_UNSET = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
let n = 0;
function check(cond, name) {
    if (cond) { n++; rack.log("ok:" + n); }
    else { rack.log("FAIL " + name); }
}
check(rack.getConfig("neverSet") === undefined, "no-default-undefined");
check(rack.getConfig("neverSet", 99) === 99, "with-default");
// Inverted-condition regression: a SET key must return the stored
// value, never the default, even though a default argument was passed.
rack.setConfig("channel", 5);
check(rack.getConfig("channel", 1) === 5, "set-key-ignores-default");
)";
	static const char* LUA_UNSET = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
local n = 0
local function check(cond, name)
    if cond then n = n + 1; rack.log("ok:" .. n)
    else rack.log("FAIL " .. name) end
end
check(rack.getConfig("neverSet") == nil, "no-default-nil")
check(rack.getConfig("neverSet", 99) == 99, "with-default")
rack.setConfig("channel", 5)
check(rack.getConfig("channel", 1) == 5, "set-key-ignores-default")
)";
	check(JS_UNSET);
	check(LUA_UNSET);
}


TEST_CASE("setConfig(key, undefined/nil) deletes the key; a later getConfig sees the default again, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string log = drainLog(m);
		REQUIRE(log.find("FAIL") == std::string::npos);
		REQUIRE(log.find("ok:2") != std::string::npos);
		// The deletion is published too — dataToJson() must not see the key.
		std::string cfg = publishedConfigJson(m->host.getActiveEngine());
		json_error_t error;
		json_t* j = json_loads(cfg.c_str(), 0, &error);
		REQUIRE(j != nullptr);
		REQUIRE(json_object_get(j, "channel") == nullptr);
		json_decref(j);

		Test::destroyModule(m);
	};

	static const char* JS_DELETE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
let n = 0;
function check(cond, name) {
    if (cond) { n++; rack.log("ok:" + n); }
    else { rack.log("FAIL " + name); }
}
rack.setConfig("channel", 3);
check(rack.getConfig("channel") === 3, "set");
rack.setConfig("channel", undefined);
check(rack.getConfig("channel", 1) === 1, "deleted-falls-back-to-default");
)";
	static const char* LUA_DELETE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
local n = 0
local function check(cond, name)
    if cond then n = n + 1; rack.log("ok:" .. n)
    else rack.log("FAIL " .. name) end
end
rack.setConfig("channel", 3)
check(rack.getConfig("channel") == 3, "set")
rack.setConfig("channel", nil)
check(rack.getConfig("channel", 1) == 1, "deleted-falls-back-to-default")
)";
	check(JS_DELETE);
	check(LUA_DELETE);
}


// key validation

TEST_CASE("setConfig()/getConfig() key validation matrix, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	// Rejected: "a.b" (dot reserved for future path addressing), "" (empty),
	// "1abc" (must not start with a digit), "a b" (space), "a-b" (hyphen), a
	// 65-char key (over the length cap), and a non-string key (number).
	// Accepted: "a", "_x", "scale2", and a 64-char key (at the length cap).
	// A rejected setConfig() must leave the config unchanged; a rejected
	// getConfig() is rejected the same way rather than quietly returning the
	// default forever — it returns undefined/nil even when a default
	// argument was given, and it logs too.
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string log = drainLog(m);
		REQUIRE(log.find("FAIL") == std::string::npos);
		REQUIRE(log.find("ok:12") != std::string::npos);

		Test::destroyModule(m);
	};

	static const char* JS_KEYS = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
let n = 0;
function check(cond, name) {
    if (cond) { n++; rack.log("ok:" + n); }
    else { rack.log("FAIL " + name); }
}
let k64 = "a".repeat(64);
let k65 = "a".repeat(65);

// Rejected keys: setConfig() is a no-op, getConfig() returns the default.
rack.setConfig("a.b", 1);
check(rack.getConfig("a.b", "def") === undefined, "dot-rejected");
rack.setConfig("", 1);
check(rack.getConfig("", "def") === undefined, "empty-rejected");
rack.setConfig("1abc", 1);
check(rack.getConfig("1abc", "def") === undefined, "leading-digit-rejected");
rack.setConfig("a b", 1);
check(rack.getConfig("a b", "def") === undefined, "space-rejected");
rack.setConfig("a-b", 1);
check(rack.getConfig("a-b", "def") === undefined, "hyphen-rejected");
rack.setConfig(k65, 1);
check(rack.getConfig(k65, "def") === undefined, "65-char-rejected");
rack.setConfig(42, 1);
check(rack.getConfig(42, "def") === undefined, "non-string-key-rejected");

// Accepted keys: round-trip normally.
rack.setConfig("a", 1);
check(rack.getConfig("a") === 1, "single-char-accepted");
rack.setConfig("_x", 1);
check(rack.getConfig("_x") === 1, "underscore-prefix-accepted");
rack.setConfig("scale2", 1);
check(rack.getConfig("scale2") === 1, "alnum-accepted");
rack.setConfig(k64, 1);
check(rack.getConfig(k64) === 1, "64-char-accepted");

// Nothing rejected made it into the published config.
check(rack.getConfig("a.b") === undefined, "rejected-key-unreachable");
)";
	static const char* LUA_KEYS = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
local n = 0
local function check(cond, name)
    if cond then n = n + 1; rack.log("ok:" .. n)
    else rack.log("FAIL " .. name) end
end
local k64 = string.rep("a", 64)
local k65 = string.rep("a", 65)

rack.setConfig("a.b", 1)
check(rack.getConfig("a.b", "def") == nil, "dot-rejected")
rack.setConfig("", 1)
check(rack.getConfig("", "def") == nil, "empty-rejected")
rack.setConfig("1abc", 1)
check(rack.getConfig("1abc", "def") == nil, "leading-digit-rejected")
rack.setConfig("a b", 1)
check(rack.getConfig("a b", "def") == nil, "space-rejected")
rack.setConfig("a-b", 1)
check(rack.getConfig("a-b", "def") == nil, "hyphen-rejected")
rack.setConfig(k65, 1)
check(rack.getConfig(k65, "def") == nil, "65-char-rejected")
rack.setConfig(42, 1)
check(rack.getConfig(42, "def") == nil, "non-string-key-rejected")

rack.setConfig("a", 1)
check(rack.getConfig("a") == 1, "single-char-accepted")
rack.setConfig("_x", 1)
check(rack.getConfig("_x") == 1, "underscore-prefix-accepted")
rack.setConfig("scale2", 1)
check(rack.getConfig("scale2") == 1, "alnum-accepted")
rack.setConfig(k64, 1)
check(rack.getConfig(k64) == 1, "64-char-accepted")

-- Nothing rejected made it into the published config.
check(rack.getConfig("a.b") == nil, "rejected-key-unreachable")
)";
	check(JS_KEYS);
	check(LUA_KEYS);
}


// rejected values

TEST_CASE("setConfig() with a function value is rejected: key unchanged, one log line, script alive, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string log = drainLog(m);
		REQUIRE(log.find("setConfig") != std::string::npos); // the rejection log line
		REQUIRE(log.find("ok:1") != std::string::npos);       // script kept running

		Test::destroyModule(m);
	};

	static const char* JS_FUNC_VALUE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.setConfig("fn", function() {});
if (rack.getConfig("fn") === undefined) rack.log("ok:1");
)";
	static const char* LUA_FUNC_VALUE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.setConfig("fn", function() end)
if rack.getConfig("fn") == nil then rack.log("ok:1") end
)";
	check(JS_FUNC_VALUE);
	check(LUA_FUNC_VALUE);
}


TEST_CASE("Nesting exactly 4 deep is accepted and round-trips; 5 deep is rejected, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string log = drainLog(m);
		REQUIRE(log.find("FAIL") == std::string::npos);
		REQUIRE(log.find("ok:2") != std::string::npos);

		Test::destroyModule(m);
	};

	// Depth counts from 1 (the value passed to setConfig() itself): in
	// {a:{b:{c:1}}}, the outer object is depth 1, a's value is depth 2, b's
	// value is depth 3, and c's value (the scalar 1) is depth 4 - exactly at
	// the cap, so accepted. One more level of nesting puts the scalar at
	// depth 5, past the cap, so rejected.
	static const char* JS_DEPTH = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
let n = 0;
function check(cond, name) {
    if (cond) { n++; rack.log("ok:" + n); }
    else { rack.log("FAIL " + name); }
}
rack.setConfig("d4", { a: { b: { c: 1 } } });
let d4 = rack.getConfig("d4");
check(d4 && d4.a.b.c === 1, "depth-4-accepted");
rack.setConfig("d5", { a: { b: { c: { d: 1 } } } });
check(rack.getConfig("d5") === undefined, "depth-5-rejected");
)";
	static const char* LUA_DEPTH = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
local n = 0
local function check(cond, name)
    if cond then n = n + 1; rack.log("ok:" .. n)
    else rack.log("FAIL " .. name) end
end
rack.setConfig("d4", { a = { b = { c = 1 } } })
local d4 = rack.getConfig("d4")
check(d4 ~= nil and d4.a.b.c == 1, "depth-4-accepted")
rack.setConfig("d5", { a = { b = { c = { d = 1 } } } })
check(rack.getConfig("d5") == nil, "depth-5-rejected")
)";
	check(JS_DEPTH);
	check(LUA_DEPTH);
}


// This is the single most important test in this suite: a
// self-referencing table/object crashes the process via a stack overflow in
// luaValueToJson()/luaTableToJsonValue() TODAY, reachable through the old
// rack.onSave() path — there was no depth guard before this change. The fix
// threads a depth argument through both converters and returns NULL past
// MidiScriptEngine::configMaxDepth (NULL is already their "unsupported value"
// signal), so setConfig() simply rejects the value instead of recursing
// forever. QuickJS's own jsValueToJson() gets the identical guard for
// contract parity, even though JS_JSONStringify would have thrown on its own.
TEST_CASE("A self-referencing table/object is rejected by setConfig(), not a crash, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		// If the depth guard were missing, this call itself would stack-
		// overflow the process — reaching drainLog() at all is part of the
		// assertion.
		m->loadScript(script);
		std::string log = drainLog(m);
		REQUIRE(log.find("FAIL") == std::string::npos);
		REQUIRE(log.find("ok:1") != std::string::npos);

		Test::destroyModule(m);
	};

	static const char* JS_CYCLE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
let cyclic = {};
cyclic.self = cyclic;
rack.setConfig("cyclic", cyclic);
if (rack.getConfig("cyclic") === undefined) rack.log("ok:1");
else rack.log("FAIL cyclic-accepted");
)";
	static const char* LUA_CYCLE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
local cyclic = {}
cyclic.self = cyclic
rack.setConfig("cyclic", cyclic)
if rack.getConfig("cyclic") == nil then rack.log("ok:1")
else rack.log("FAIL cyclic-accepted") end
)";
	check(JS_CYCLE);
	check(LUA_CYCLE);
}


TEST_CASE("setConfig() rejects a value that would push the config past the size cap; the previous value survives, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string log = drainLog(m);
		REQUIRE(log.find("FAIL") == std::string::npos);
		REQUIRE(log.find("ok:2") != std::string::npos);

		Test::destroyModule(m);
	};

	// configMaxBytes is 64 KB (MidiScriptEngine::configMaxBytes) — a single
	// ~80 KB string value overflows it in one call, so the previous "small"
	// value must still be there afterward.
	static const char* JS_SIZECAP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
let n = 0;
function check(cond, name) {
    if (cond) { n++; rack.log("ok:" + n); }
    else { rack.log("FAIL " + name); }
}
rack.setConfig("small", "kept");
check(rack.getConfig("small") === "kept", "small-set");
let big = "x".repeat(80 * 1024);
rack.setConfig("big", big);
check(rack.getConfig("big") === undefined && rack.getConfig("small") === "kept", "oversize-rejected-previous-survives");
)";
	static const char* LUA_SIZECAP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
local n = 0
local function check(cond, name)
    if cond then n = n + 1; rack.log("ok:" .. n)
    else rack.log("FAIL " .. name) end
end
rack.setConfig("small", "kept")
check(rack.getConfig("small") == "kept", "small-set")
local big = string.rep("x", 80 * 1024)
rack.setConfig("big", big)
check(rack.getConfig("big") == nil and rack.getConfig("small") == "kept", "oversize-rejected-previous-survives")
)";
	check(JS_SIZECAP);
	check(LUA_SIZECAP);
}


// setConfig() from every callback context

TEST_CASE("setConfig() persists when called from top-level, onLoad, onUnload, midi.onMessage, and a context-menu onChange, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		auto cfg = [&]() { return publishedConfigJson(m->host.getActiveEngine()); };
		REQUIRE(configInt(cfg(), "topLevel") == 1);
		REQUIRE(configInt(cfg(), "onLoad") == 1);

		// midi.onMessage.
		midi::Message msg = noteOn(1, 60, 100);
		m->host.queueMessage(0, QueuedMessage(msg));
		m->host.process();
		REQUIRE(configInt(cfg(), "onMessage") == 1);

		// Context-menu onChange.
		std::vector<ScriptMenuItem> specs;
		m->host.getActiveEngine()->getContextMenus([&specs](const std::vector<ScriptMenuItem>& s) { specs = s; });
		m->host.getActiveEngine()->process();   // UI queries run on the engine's next pump
		REQUIRE(specs.size() == 1);
		m->host.getActiveEngine()->invokeContextMenuCallback(specs[0].callbackId, 1);
		REQUIRE(configBool(cfg(), "menu") == true);

		// onUnload — clearScript() tears the script down for real.
		m->clearScript();
		// publishedConfig survives teardown: read it before the module
		// (and its engines) are destroyed.
		Test::destroyModule(m);
	};

	static const char* JS_ALL_CONTEXTS = R"(/**
 * @engine QuickJs@v1
 */
rack.setConfig("topLevel", 1);
rack.onLoad = function() {
    rack.setConfig("onLoad", 1);
};
rack.onUnload = function() {
    rack.setConfig("onUnload", 1);
};
midi.onMessage = function(midiPort, msg) {
    rack.setConfig("onMessage", 1);
};
rack.registerContextMenu({
    type: "boolean",
    label: "Menu",
    onGetValue: function() { return false; },
    onChange: function(checked) {
        rack.setConfig("menu", checked);
    }
});
)";
	static const char* LUA_ALL_CONTEXTS = R"(--[[
@engine minilua@v1
--]]
rack.setConfig("topLevel", 1)
rack.onLoad = function()
    rack.setConfig("onLoad", 1)
end
rack.onUnload = function()
    rack.setConfig("onUnload", 1)
end
midi.onMessage = function(midiPort, msg)
    rack.setConfig("onMessage", 1)
end
rack.registerContextMenu({
    type = "boolean",
    label = "Menu",
    onGetValue = function() return false end,
    onChange = function(checked)
        rack.setConfig("menu", checked)
    end
})
)";
	check(JS_ALL_CONTEXTS);
	check(LUA_ALL_CONTEXTS);

	// onUnload's setConfig() call is verified separately: clearScript()
	// destroys the outgoing engine's workingConfig, so publishedConfig must be
	// read on the SAME engine instance before teardown completes. Re-run with
	// an inline read timed against the unload itself.
	auto checkOnUnload = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);
		MidiScriptEngine* engine = m->host.getActiveEngine();
		m->clearScript();
		// The engine object itself is still alive (owned by ScriptHost, not
		// freed on script switch/clear) — only its interpreter state is torn
		// down — so reading its publishedConfig after clearScript() is safe
		// and is exactly the "racing save" scenario described above.
		REQUIRE(configInt(publishedConfigJson(engine), "onUnload") == 1);
		Test::destroyModule(m);
	};
	checkOnUnload(JS_ALL_CONTEXTS);
	checkOnUnload(LUA_ALL_CONTEXTS);
}


// full patch round-trip

TEST_CASE("Full patch round-trip: setConfig -> dataToJson -> new module -> dataFromJson -> getConfig, in both engines", "[MidiKit][CrossEngine][JSON]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		json_t* rootJ = m->dataToJson();
		json_t* configJ = json_object_get(rootJ, "scriptConfig");
		REQUIRE(configJ != NULL);
		REQUIRE(json_integer_value(json_object_get(configJ, "channel")) == 7);

		MidiKitModule* m2 = createModule();
		m2->dataFromJson(rootJ);
		json_decref(rootJ);
		std::string log2 = drainLog(m2);
		// onLoad() must see the restored value via getConfig(), not an argument.
		REQUIRE(log2.find("onLoad-channel=7") != std::string::npos);

		// And top-level-code-equivalent access after the fact (getContextMenus
		// re-evaluates onGetValue, which reads live config) confirms it's not
		// only visible inside onLoad().
		std::vector<ScriptMenuItem> specs;
		m2->host.getActiveEngine()->getContextMenus([&specs](const std::vector<ScriptMenuItem>& s) { specs = s; });
		m2->host.getActiveEngine()->process();   // UI queries run on the engine's next pump
		REQUIRE(specs.size() == 1);
		REQUIRE(specs[0].selected == 7);

		Test::destroyModule(m);
		Test::destroyModule(m2);
	};

	static const char* JS_ROUNDTRIP_PATCH = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.setConfig("channel", 7);
rack.onLoad = function() {
    rack.log("onLoad-channel=" + rack.getConfig("channel"));
};
rack.registerContextMenu({
    type: "options",
    label: "Channel",
    options: ["0","1","2","3","4","5","6","7","8"],
    onGetValue: function() { return rack.getConfig("channel", 0); },
    onChange: function(idx) { rack.setConfig("channel", idx); }
});
)";
	static const char* LUA_ROUNDTRIP_PATCH = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.setConfig("channel", 7)
rack.onLoad = function()
    rack.log("onLoad-channel=" .. rack.getConfig("channel"))
end
rack.registerContextMenu({
    type = "options",
    label = "Channel",
    options = {"0","1","2","3","4","5","6","7","8"},
    onGetValue = function() return rack.getConfig("channel", 0) end,
    onChange = function(idx) rack.setConfig("channel", idx) end
})
)";
	check(JS_ROUNDTRIP_PATCH);
	check(LUA_ROUNDTRIP_PATCH);
}


// lifecycle: script switch and reset

TEST_CASE("Switching to a new script gets a fresh, empty config, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& scriptA, const std::string& scriptB) {
		MidiKitModule* m = createModule();
		m->loadScript(scriptA);
		drainLog(m);
		REQUIRE(configInt(publishedConfigJson(m->host.getActiveEngine()), "fromA") == 1);

		// Switching scripts (a real ScriptHost::load(), not clearScript())
		// must not carry config across — config belongs to the script that
		// wrote it.
		m->loadScript(scriptB);
		drainLog(m);
		std::string cfg = publishedConfigJson(m->host.getActiveEngine());
		json_error_t error;
		json_t* j = json_loads(cfg.c_str(), 0, &error);
		REQUIRE(j != nullptr);
		REQUIRE(json_object_size(j) == 0);
		json_decref(j);

		Test::destroyModule(m);
	};

	static const char* JS_FROM_A = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.setConfig("fromA", 1);
)";
	static const char* JS_FROM_B = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
)";
	static const char* LUA_FROM_A = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.setConfig("fromA", 1)
)";
	static const char* LUA_FROM_B = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
)";
	check(JS_FROM_A, JS_FROM_B);
	check(LUA_FROM_A, LUA_FROM_B);
	// Cross-engine switch too: Lua -> QuickJs must not leak config either.
	check(LUA_FROM_A, JS_FROM_B);
}


TEST_CASE("onReset() clears the config; a subsequent dataToJson() writes no scriptConfig, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);
		REQUIRE(configInt(publishedConfigJson(m->host.getActiveEngine()), "x") == 1);

		m->onReset();

		json_t* rootJ = m->dataToJson();
		REQUIRE(json_object_get(rootJ, "scriptConfig") == NULL);
		json_decref(rootJ);

		Test::destroyModule(m);
	};

	static const char* JS_RESET = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.setConfig("x", 1);
)";
	static const char* LUA_RESET = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.setConfig("x", 1)
)";
	check(JS_RESET);
	check(LUA_RESET);
}
