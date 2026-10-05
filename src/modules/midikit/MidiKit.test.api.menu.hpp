// rack.registerContextMenu()
// Script-registered context-menu items (see SCRIPTING.md). Both engines
// expose the identical rack.registerContextMenu() API; the observable result
// of a script is the extracted ContextMenuSpec list (what the widget builds
// its menu from) plus the log produced when a click fires the onChange
// callback. Each behaviour is pinned as a JS_* / LUA_* script pair and
// asserted identical, exactly like the midi.* cases above.
// Loads the script, drains the load-time log, and returns the registered
// ContextMenuSpecs. When clickId >= 0, also invokes that callback (the
// programmatic equivalent of a menu click) and re-reads the specs so
// presentation-state updates (checked/selected) are observable.
struct MenuResult {
	bool loaded = false;
	std::vector<ScriptMenuItem> specs;
	std::string loadLog;
	std::string log;
};

static MenuResult runMenu(const std::string& script, int clickId = -1, int clickValue = 0) {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);

	MenuResult r;
	r.loaded = (m->host.isQuickJsEngine()) ? (m->host.seQuickJs.ctx != nullptr)
	                                              : (m->host.seLua.L != nullptr);
	r.loadLog = drainLog(m);
	if (r.loaded) {
		// getContextMenus is asynchronous: the worker evaluates onGetValue and
		// then invokes the callback with the evaluated specs. It is a
		// low-priority UI query, which the engine runs on its next pump; the
		// tests use a SyncTaskWorker, so process() runs it inline and r.specs
		// is filled by the time process() returns.
		auto queryMenus = [&]() {
			m->host.getActiveEngine()->getContextMenus([&r](const std::vector<ScriptMenuItem>& specs) {
				r.specs = specs;
			});
			m->host.getActiveEngine()->process();
		};
		queryMenus();
		if (clickId >= 0) {
			m->host.getActiveEngine()->invokeContextMenuCallback(clickId, clickValue);
			r.log = drainLog(m);
			queryMenus();
		}
	}
	return r;
}

// The two engines must expose identical menu models: same count, same order,
// and identical presentation fields. callbackId is engine-internal (both
// assign 1, 2, 3… in registration order), so only its validity is checked,
// not its exact value.
static void requireSameMenus(const std::vector<ScriptMenuItem>& a, const std::vector<ScriptMenuItem>& b) {
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		REQUIRE(a[i].type == b[i].type);
		REQUIRE(a[i].label == b[i].label);
		// checked/selected share one union member; only read the active one.
		if (a[i].type == ScriptMenuItem::Type::Boolean)
			REQUIRE(a[i].checked == b[i].checked);
		else
			REQUIRE(a[i].selected == b[i].selected);
		REQUIRE(a[i].options == b[i].options);
		REQUIRE(a[i].optionValues == b[i].optionValues);
		REQUIRE(a[i].callbackId >= 1);
	}
}

static const char* JS_REGISTER_BOOL = R"(/**
 * @engine QuickJs@v1
 */
let v = false;
rack.registerContextMenu({
	type: "boolean",
	label: "Velocity to CC",
	onGetValue: function() {
		return v;
	},
	onChange: function(checked) {
		v = checked;
		rack.log("onChange: " + (checked ? "true" : "false"));
	}
});
)";

static const char* LUA_REGISTER_BOOL = R"(--[[
@engine minilua@v1
--]]
v = false
rack.registerContextMenu({
	type = "boolean",
	label = "Velocity to CC",
	onGetValue = function()
		return v
	end,
	onChange = function(checked)
		v = checked
		rack.log("onChange: " .. tostring(checked))
	end
})
)";

TEST_CASE("registerContextMenu boolean menu is identical", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_BOOL);
	MenuResult lua = runMenu(LUA_REGISTER_BOOL);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 1);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Boolean);
	REQUIRE(js.specs[0].label == "Velocity to CC");
	REQUIRE(js.specs[0].checked == false);

	// A click fires onChange(true) and flips the presentation state.
	MenuResult jsOn = runMenu(JS_REGISTER_BOOL, js.specs[0].callbackId, 1);
	MenuResult luaOn = runMenu(LUA_REGISTER_BOOL, lua.specs[0].callbackId, 1);
	REQUIRE(jsOn.log.find("onChange: true") != std::string::npos);
	REQUIRE(luaOn.log.find("onChange: true") != std::string::npos);
	REQUIRE(jsOn.specs[0].checked == true);
	REQUIRE(luaOn.specs[0].checked == true);

	// And back to false.
	MenuResult jsOff = runMenu(JS_REGISTER_BOOL, js.specs[0].callbackId, 0);
	MenuResult luaOff = runMenu(LUA_REGISTER_BOOL, lua.specs[0].callbackId, 0);
	REQUIRE(jsOff.log.find("onChange: false") != std::string::npos);
	REQUIRE(luaOff.log.find("onChange: false") != std::string::npos);
}

static const char* JS_REGISTER_OPTIONS = R"(/**
 * @engine QuickJs@v1
 */
let v = 1;
rack.registerContextMenu({
	type: "options",
	label: "Out mode",
	options: ["Internal", "External", "Both"],
	onGetValue: function() {
		return v;
	},
	onChange: function(selectedIndex, selectedLabel) {
		v = selectedIndex;
		rack.log("onChange: " + selectedIndex + " " + selectedLabel);
	}
});
)";

static const char* LUA_REGISTER_OPTIONS = R"(--[[
@engine minilua@v1
--]]
v = 1
rack.registerContextMenu({
	type = "options",
	label = "Out mode",
	options = {"Internal", "External", "Both"},
	onGetValue = function()
		return v
	end,
	onChange = function(selectedIndex, selectedLabel)
		v = selectedIndex
		rack.log("onChange: " .. selectedIndex .. " " .. selectedLabel)
	end
})
)";

TEST_CASE("registerContextMenu options menu is identical", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_OPTIONS);
	MenuResult lua = runMenu(LUA_REGISTER_OPTIONS);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 1);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Options);
	REQUIRE(js.specs[0].label == "Out mode");
	REQUIRE(js.specs[0].options.size() == 3);
	REQUIRE(js.specs[0].options[0] == "Internal");
	REQUIRE(js.specs[0].options[1] == "External");
	REQUIRE(js.specs[0].options[2] == "Both");
	REQUIRE(js.specs[0].selected == 1);

	// Clicking index 2 passes index + label to onChange and updates selection.
	MenuResult jsClick = runMenu(JS_REGISTER_OPTIONS, js.specs[0].callbackId, 2);
	MenuResult luaClick = runMenu(LUA_REGISTER_OPTIONS, lua.specs[0].callbackId, 2);
	REQUIRE(jsClick.log.find("onChange: 2 Both") != std::string::npos);
	REQUIRE(luaClick.log.find("onChange: 2 Both") != std::string::npos);
	REQUIRE(jsClick.specs[0].selected == 2);
	REQUIRE(luaClick.specs[0].selected == 2);
}

static const char* JS_REGISTER_TWO = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "boolean", label: "First", onChange: function() {} });
rack.registerContextMenu({ type: "options", label: "Second", options: ["x", "y"], onChange: function() {} });
)";

static const char* LUA_REGISTER_TWO = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "boolean", label = "First", onChange = function() end })
rack.registerContextMenu({ type = "options", label = "Second", options = {"x", "y"}, onChange = function() end })
)";

TEST_CASE("Multiple registerContextMenu calls keep registration order", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_TWO);
	MenuResult lua = runMenu(LUA_REGISTER_TWO);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 2);
	REQUIRE(js.specs[0].label == "First");
	REQUIRE(js.specs[1].label == "Second");
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Boolean);
	REQUIRE(js.specs[1].type == ScriptMenuItem::Type::Options);
	// callbackIds are assigned monotonically in registration order.
	REQUIRE(js.specs[0].callbackId < js.specs[1].callbackId);
	REQUIRE(lua.specs[0].callbackId < lua.specs[1].callbackId);
}

// Registering a label twice replaces the first item in place: same position and
// callback id, new options/callbacks - the way a script changes a menu at runtime.
static const char* JS_REGISTER_REPLACE = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "options", label: "Pick", options: ["a", "b", "c"], onGetValue: function() { return 2; }, onChange: function(i, l) { rack.log("old ", l); } });
rack.registerContextMenu({ type: "boolean", label: "Other", onChange: function() {} });
rack.registerContextMenu({ type: "options", label: "Pick", options: ["x", "y"], onGetValue: function() { return 1; }, onChange: function(i, l) { rack.log("new ", l); } });
)";

static const char* LUA_REGISTER_REPLACE = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "options", label = "Pick", options = {"a", "b", "c"}, onGetValue = function() return 2 end, onChange = function(i, l) rack.log("old ", l) end })
rack.registerContextMenu({ type = "boolean", label = "Other", onChange = function() end })
rack.registerContextMenu({ type = "options", label = "Pick", options = {"x", "y"}, onGetValue = function() return 1 end, onChange = function(i, l) rack.log("new ", l) end })
)";

TEST_CASE("registerContextMenu with an existing label replaces the item", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_REPLACE);
	MenuResult lua = runMenu(LUA_REGISTER_REPLACE);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	// Still two items, "Pick" keeps its first position with the new content.
	REQUIRE(js.specs.size() == 2);
	REQUIRE(js.specs[0].label == "Pick");
	REQUIRE(js.specs[1].label == "Other");
	REQUIRE(js.specs[0].options == std::vector<std::string>{"x", "y"});
	REQUIRE(js.specs[0].selected == 1);
	REQUIRE(js.specs[0].callbackId < js.specs[1].callbackId);
	REQUIRE(lua.specs[0].callbackId < lua.specs[1].callbackId);

	// A click runs the new onChange, not the replaced one.
	MenuResult jsClick = runMenu(JS_REGISTER_REPLACE, js.specs[0].callbackId, 1);
	MenuResult luaClick = runMenu(LUA_REGISTER_REPLACE, lua.specs[0].callbackId, 1);
	REQUIRE(jsClick.log.find("new y") != std::string::npos);
	REQUIRE(luaClick.log.find("new y") != std::string::npos);
	REQUIRE(jsClick.log.find("old") == std::string::npos);
	REQUIRE(luaClick.log.find("old") == std::string::npos);
}

// Extra arguments after the table are ignored: JS always did, Lua read its fields
// by absolute stack index and failed with "type must be a string".
static const char* JS_REGISTER_EXTRA_ARG = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "boolean", label: "X", onChange: function() {} }, null);
rack.registerContextMenu({ type: "options", label: "Y", options: ["a", "b"], onChange: function() {} }, 1, "z");
)";

static const char* LUA_REGISTER_EXTRA_ARG = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "boolean", label = "X", onChange = function() end }, nil)
rack.registerContextMenu({ type = "options", label = "Y", options = {"a", "b"}, onChange = function() end }, 1, "z")
)";

TEST_CASE("registerContextMenu ignores extra arguments after the table", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_EXTRA_ARG);
	MenuResult lua = runMenu(LUA_REGISTER_EXTRA_ARG);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	REQUIRE(js.specs.size() == 2);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(lua.specs[0].label == "X");
	REQUIRE(lua.specs[1].options == std::vector<std::string>{"a", "b"});
}

// rack.unregisterContextMenu(label) removes the item and reports whether one
// existed; the remaining items keep their order.
static const char* JS_REGISTER_UNREGISTER = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "boolean", label: "A", onChange: function() {} });
rack.registerContextMenu({ type: "boolean", label: "B", onChange: function() {} });
rack.registerContextMenu({ type: "boolean", label: "C", onChange: function() {} });
rack.log("removed B: ", rack.unregisterContextMenu("B"));
rack.log("removed B again: ", rack.unregisterContextMenu("B"));
rack.registerContextMenu({ type: "boolean", label: "B", onChange: function() {} });
)";

static const char* LUA_REGISTER_UNREGISTER = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "boolean", label = "A", onChange = function() end })
rack.registerContextMenu({ type = "boolean", label = "B", onChange = function() end })
rack.registerContextMenu({ type = "boolean", label = "C", onChange = function() end })
rack.log("removed B: ", rack.unregisterContextMenu("B"))
rack.log("removed B again: ", rack.unregisterContextMenu("B"))
rack.registerContextMenu({ type = "boolean", label = "B", onChange = function() end })
)";

TEST_CASE("unregisterContextMenu removes the item and reports whether it existed", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_UNREGISTER);
	MenuResult lua = runMenu(LUA_REGISTER_UNREGISTER);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.loadLog.find("removed B: true") != std::string::npos);
	REQUIRE(lua.loadLog.find("removed B: true") != std::string::npos);
	REQUIRE(js.loadLog.find("removed B again: false") != std::string::npos);
	REQUIRE(lua.loadLog.find("removed B again: false") != std::string::npos);

	// B was removed and registered again: it now sits after C.
	REQUIRE(js.specs.size() == 3);
	REQUIRE(js.specs[0].label == "A");
	REQUIRE(js.specs[1].label == "C");
	REQUIRE(js.specs[2].label == "B");
}

static const char* JS_REGISTER_THROW = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({
	type: "boolean",
	label: "Bad",
	onChange: function(checked) { throw new Error("boom"); }
});
rack.registerContextMenu({
	type: "boolean",
	label: "Good",
	onChange: function(checked) { rack.log("good"); }
});
)";

static const char* LUA_REGISTER_THROW = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({
	type = "boolean",
	label = "Bad",
	onChange = function(checked) error("boom") end
})
rack.registerContextMenu({
	type = "boolean",
	label = "Good",
	onChange = function(checked) rack.log("good") end
})
)";

TEST_CASE("Throwing context-menu callback is logged and the module keeps working", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_THROW);
	MenuResult lua = runMenu(LUA_REGISTER_THROW);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 2);

	// A throwing onChange is reported through the log in both engines.
	MenuResult jsBad = runMenu(JS_REGISTER_THROW, js.specs[0].callbackId, 1);
	MenuResult luaBad = runMenu(LUA_REGISTER_THROW, lua.specs[0].callbackId, 1);
	REQUIRE(jsBad.log.find("Context menu callback error") != std::string::npos);
	REQUIRE(luaBad.log.find("Context menu callback error") != std::string::npos);

	// The other registered item still fires normally.
	MenuResult jsGood = runMenu(JS_REGISTER_THROW, js.specs[1].callbackId, 1);
	MenuResult luaGood = runMenu(LUA_REGISTER_THROW, lua.specs[1].callbackId, 1);
	REQUIRE(jsGood.log.find("good") != std::string::npos);
	REQUIRE(luaGood.log.find("good") != std::string::npos);

	// A callbackId that was never registered is a silent no-op in both.
	MenuResult jsNoop = runMenu(JS_REGISTER_THROW, 9999, 1);
	MenuResult luaNoop = runMenu(LUA_REGISTER_THROW, 9999, 1);
	REQUIRE(jsNoop.log.empty());
	REQUIRE(luaNoop.log.empty());
}

static const char* JS_MENU_SEND = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.registerContextMenu({
    type: "options", label: "Program", options: ["A", "B"],
    onChange: function(idx) {
        let msg = midi.create();
        midi.setProgramChange(msg, 1, 10 + idx);
        midiOut.send(msg);
    }
});
)";

static const char* LUA_MENU_SEND = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.registerContextMenu({
    type = "options", label = "Program", options = {"A", "B"},
    onChange = function(idx)
        local msg = midi.create()
        midi.setProgramChange(msg, 1, 10 + idx)
        midiOut.send(msg)
    end
})
)";

TEST_CASE("Context-menu onChange sends MIDI identically in both engines", "[MidiKit][CrossEngine]") {
	auto click = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);
		m->host.getActiveEngine()->getContextMenus([](const std::vector<ScriptMenuItem>&) {});
		m->host.getActiveEngine()->process();
		m->host.getActiveEngine()->invokeContextMenuCallback(1, 1);
		std::string log = drainLog(m);
		// No "discarded" warning: onChange counts as a callback.
		REQUIRE(log.find("only allowed inside a callback") == std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		auto sent = Out::of(out, port, ticks);
		return sent;
	};
	auto js = click(JS_MENU_SEND);
	auto lua = click(LUA_MENU_SEND);
	REQUIRE(js.port == lua.port);
	REQUIRE(js.bytes == lua.bytes);
	REQUIRE(js.bytes.size() == 2);
	REQUIRE(js.bytes[1] == 11);
}

static const char* JS_REGISTER_BAD_NO_ONCHANGE = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "boolean", label: "X" });
)";

static const char* LUA_REGISTER_BAD_NO_ONCHANGE = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "boolean", label = "X" })
)";

static const char* JS_REGISTER_BAD_TYPE = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "nope", label: "X", onChange: function() {} });
)";

static const char* LUA_REGISTER_BAD_TYPE = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "nope", label = "X", onChange = function() end })
)";

static const char* JS_REGISTER_BAD_OPTIONS = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "options", label: "X", options: ["ok", 42], onChange: function() {} });
)";

static const char* LUA_REGISTER_BAD_OPTIONS = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "options", label = "X", options = {"ok", 42}, onChange = function() end })
)";

TEST_CASE("Malformed registerContextMenu fails the load identically", "[MidiKit][CrossEngine]") {
	// Missing onChange: both engines reject the registration and the load.
	MenuResult js = runMenu(JS_REGISTER_BAD_NO_ONCHANGE);
	MenuResult lua = runMenu(LUA_REGISTER_BAD_NO_ONCHANGE);
	REQUIRE_FALSE(js.loaded);
	REQUIRE_FALSE(lua.loaded);

	// Unknown type: both fail with the same diagnostic wording.
	js = runMenu(JS_REGISTER_BAD_TYPE);
	lua = runMenu(LUA_REGISTER_BAD_TYPE);
	REQUIRE_FALSE(js.loaded);
	REQUIRE_FALSE(lua.loaded);
	REQUIRE(js.loadLog.find("registerContextMenu: type must be") != std::string::npos);
	REQUIRE(lua.loadLog.find("registerContextMenu: type must be") != std::string::npos);

	// Non-string element in options (the Lua lua_isstring-coercion divergence
	// was caught here — see repo notes): both engines reject it.
	js = runMenu(JS_REGISTER_BAD_OPTIONS);
	lua = runMenu(LUA_REGISTER_BAD_OPTIONS);
	REQUIRE_FALSE(js.loaded);
	REQUIRE_FALSE(lua.loaded);
}

static const char* JS_ONGETVALUE_NO_RETURN_BOOL = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({
	type: "boolean",
	label: "No return",
	onGetValue: function() {},
	onChange: function() {}
});
)";

static const char* LUA_ONGETVALUE_NO_RETURN_BOOL = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({
	type = "boolean",
	label = "No return",
	onGetValue = function() end,
	onChange = function() end
})
)";

static const char* JS_ONGETVALUE_NO_RETURN_OPTIONS = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({
	type: "options",
	label: "No return",
	options: ["A", "B", "C"],
	onGetValue: function() {},
	onChange: function() {}
});
)";

static const char* LUA_ONGETVALUE_NO_RETURN_OPTIONS = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({
	type = "options",
	label = "No return",
	options = {"A", "B", "C"},
	onGetValue = function() end,
	onChange = function() end
})
)";

TEST_CASE("onGetValue returning nothing defaults to false/0", "[MidiKit][CrossEngine]") {
	// Boolean: a missing return yields undefined (JS) / nil (Lua), which both
	// engines coerce to false.
	MenuResult js = runMenu(JS_ONGETVALUE_NO_RETURN_BOOL);
	MenuResult lua = runMenu(LUA_ONGETVALUE_NO_RETURN_BOOL);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs.size() == 1);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Boolean);
	REQUIRE(js.specs[0].checked == false);
	REQUIRE(lua.specs[0].checked == false);

	// Options: a missing return yields undefined (JS) / nil (Lua), which both
	// engines coerce to 0 (the first option).
	MenuResult jsOpt = runMenu(JS_ONGETVALUE_NO_RETURN_OPTIONS);
	MenuResult luaOpt = runMenu(LUA_ONGETVALUE_NO_RETURN_OPTIONS);
	REQUIRE(jsOpt.loaded);
	REQUIRE(luaOpt.loaded);
	requireSameMenus(jsOpt.specs, luaOpt.specs);
	REQUIRE(jsOpt.specs.size() == 1);
	REQUIRE(jsOpt.specs[0].type == ScriptMenuItem::Type::Options);
	REQUIRE(jsOpt.specs[0].selected == 0);
	REQUIRE(luaOpt.specs[0].selected == 0);
}


// rack.registerContextMenu() types "action" and "file"

static const char* JS_ACTION_FILE_MENU = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "action", label: "Go", onGetValue: function() { return 1; }, onChange: function() { rack.log("go args=" + arguments.length); } });
rack.registerContextMenu({ type: "fileopen", label: "Import", onChange: function(content, name) { rack.log("import " + content.length + " [" + content + "] " + name + " args=" + arguments.length); } });
rack.registerContextMenu({ type: "boolean", label: "Flag", onChange: function(v) { rack.log("flag " + v); } });
)";

static const char* LUA_ACTION_FILE_MENU = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "action", label = "Go", onGetValue = function() return 1 end, onChange = function(...) rack.log("go args=" .. select('#', ...)) end })
rack.registerContextMenu({ type = "fileopen", label = "Import", onChange = function(content, name, ...) rack.log("import " .. #content .. " [" .. content .. "] " .. name .. " args=" .. (2 + select('#', ...))) end })
rack.registerContextMenu({ type = "boolean", label = "Flag", onChange = function(v) rack.log("flag " .. tostring(v)) end })
)";

TEST_CASE("Action and file context menu items are listed, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	MenuResult js = runMenu(JS_ACTION_FILE_MENU);
	MenuResult lua = runMenu(LUA_ACTION_FILE_MENU);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs.size() == 3);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Action);
	REQUIRE(js.specs[0].label == "Go");
	REQUIRE(js.specs[1].type == ScriptMenuItem::Type::FileOpen);
	REQUIRE(js.specs[1].label == "Import");
	REQUIRE(js.specs[2].type == ScriptMenuItem::Type::Boolean);
}

TEST_CASE("An action item calls onChange without arguments on every click, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	Pair scripts{JS_ACTION_FILE_MENU, LUA_ACTION_FILE_MENU};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	MenuResult r = runMenu(script, 1);   // "Go" is registered first: id 1
	REQUIRE(r.loaded);
	REQUIRE(r.log.find("go args=0") != std::string::npos);

	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	drainLog(m);
	for (int i = 0; i < 3; i++) m->host.getActiveEngine()->invokeContextMenuCallback(1, i);
	std::string log = drainLog(m);
	size_t n = 0;
	for (size_t at = log.find("go args=0"); at != std::string::npos; at = log.find("go args=0", at + 1)) n++;
	REQUIRE(n == 3);
}

TEST_CASE("A file item passes the file's text and name to onChange, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	Pair scripts{JS_ACTION_FILE_MENU, LUA_ACTION_FILE_MENU};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	drainLog(m);
	auto* e = m->host.getActiveEngine();

	e->invokeContextMenuCallback(2, ScriptMenuClick::file("line1\nline2", "notes.txt"));
	std::string log = drainLog(m);
	CATCH_INFO(log);
	REQUIRE(log.find("import 11 [line1\nline2] notes.txt args=2") != std::string::npos);

	// An empty file is a call with an empty string.
	e->invokeContextMenuCallback(2, ScriptMenuClick::file("", "empty.txt"));
	REQUIRE(drainLog(m).find("import 0 [] empty.txt") != std::string::npos);
}

TEST_CASE("Context menu clicks of the wrong kind are ignored, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	Pair scripts{JS_ACTION_FILE_MENU, LUA_ACTION_FILE_MENU};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	drainLog(m);
	auto* e = m->host.getActiveEngine();

	e->invokeContextMenuCallback(2, 0);               // a file item needs a file
	e->invokeContextMenuCallback(1, ScriptMenuClick::file("x", "x.txt"));   // an action item takes none
	e->invokeContextMenuCallback(3, ScriptMenuClick::file("x", "x.txt"));   // nor does a boolean
	e->invokeContextMenuCallback(99, ScriptMenuClick::file("x", "x.txt"));  // unknown id
	REQUIRE(drainLog(m).empty());
}

TEST_CASE("registerContextMenu names all four types when the type is wrong, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	MenuResult js = runMenu(R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "button", label: "X", onChange: function() {} });
)");
	MenuResult lua = runMenu(R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "button", label = "X", onChange = function() end })
)");
	REQUIRE(js.specs.empty());
	REQUIRE(lua.specs.empty());
	REQUIRE(js.loadLog.find("\"action\"") != std::string::npos);
	REQUIRE(js.loadLog.find("\"fileopen\"") != std::string::npos);
	REQUIRE(lua.loadLog.find("\"action\"") != std::string::npos);
	REQUIRE(lua.loadLog.find("\"fileopen\"") != std::string::npos);
}

TEST_CASE("menuCallArgs maps a click to onChange's arguments, or refuses one that does not fit", "[MidiKit][ContextMenu]") {
	std::vector<StoermelderPackOne::MidiScript::ScriptMenuArg> args;
	using Kind = StoermelderPackOne::MidiScript::ScriptMenuArg::Kind;
	using StoermelderPackOne::MidiScript::menuCallArgs;

	ScriptMenuItem boolItem;
	boolItem.type = ScriptMenuItem::Type::Boolean;
	REQUIRE(menuCallArgs(boolItem, ScriptMenuClick(1), args));
	REQUIRE(args.size() == 1);
	REQUIRE(args[0].kind == Kind::Bool);
	REQUIRE(args[0].b);
	REQUIRE(menuCallArgs(boolItem, ScriptMenuClick(0), args));
	REQUIRE_FALSE(args[0].b);

	ScriptMenuItem optItem;
	optItem.type = ScriptMenuItem::Type::Options;
	optItem.options = { "a", "b" };
	REQUIRE(menuCallArgs(optItem, ScriptMenuClick(1), args));
	REQUIRE(args.size() == 2);
	REQUIRE(args[0].kind == Kind::Int);
	REQUIRE(args[0].i == 1);
	REQUIRE(args[1].kind == Kind::String);
	REQUIRE(args[1].s == "b");
	REQUIRE_FALSE(menuCallArgs(optItem, ScriptMenuClick(2), args));
	REQUIRE_FALSE(menuCallArgs(optItem, ScriptMenuClick(-1), args));

	ScriptMenuItem actionItem;
	actionItem.type = ScriptMenuItem::Type::Action;
	REQUIRE(menuCallArgs(actionItem, ScriptMenuClick(), args));
	REQUIRE(args.empty());

	ScriptMenuItem fileItem;
	fileItem.type = ScriptMenuItem::Type::FileOpen;
	REQUIRE(menuCallArgs(fileItem, ScriptMenuClick::file("data", "f.txt"), args));
	REQUIRE(args.size() == 2);
	REQUIRE(args[0].s == "data");
	REQUIRE(args[1].s == "f.txt");

	// A file for any other item, or a plain click for a file item, does not fit.
	REQUIRE_FALSE(menuCallArgs(fileItem, ScriptMenuClick(1), args));
	REQUIRE_FALSE(menuCallArgs(boolItem, ScriptMenuClick::file("x", "x"), args));
	REQUIRE_FALSE(menuCallArgs(optItem, ScriptMenuClick::file("x", "x"), args));
	REQUIRE_FALSE(menuCallArgs(actionItem, ScriptMenuClick::file("x", "x"), args));
}


// Every check of registerContextMenu runs before anything is registered, so a
// bad item raises, registers nothing and leaves the module usable. (The Lua
// binding must not keep C++ values alive across its errors: luaL_error longjmps.)
TEST_CASE("registerContextMenu rejects bad items in both engines and registers only the good one", "[MidiKit][ContextMenu][CrossEngine]") {
	FOR_EACH_LANG;
	const char* label = "A label long enough to need heap storage";
	std::string js = std::string("function t(name, f) { try { f(); rack.log('ok ' + name); } catch (e) { rack.log('err ' + name); } }\n")
		+ "const L = '" + label + "';\n"
		+ "t('nonString', function() { rack.registerContextMenu({ type: 'options', label: L, options: ['one option long enough for the heap', 3], onChange: function() {} }); });\n"
		+ "t('empty', function() { rack.registerContextMenu({ type: 'options', label: L, options: [], onChange: function() {} }); });\n"
		+ "t('noOnChange', function() { rack.registerContextMenu({ type: 'boolean', label: L }); });\n"
		+ "t('badType', function() { rack.registerContextMenu({ type: 'knob', label: L, onChange: function() {} }); });\n"
		+ "t('good', function() { rack.registerContextMenu({ type: 'options', label: L, options: ['a', 'b'], onChange: function() {} }); });\n";
	std::string lua = std::string("local function t(name, f) local ok = pcall(f) rack.log((ok and 'ok ' or 'err ') .. name) end\n")
		+ "local L = '" + label + "'\n"
		+ "t('nonString', function() rack.registerContextMenu({ type = 'options', label = L, options = { 'one option long enough for the heap', 3 }, onChange = function() end }) end)\n"
		+ "t('empty', function() rack.registerContextMenu({ type = 'options', label = L, options = {}, onChange = function() end }) end)\n"
		+ "t('noOnChange', function() rack.registerContextMenu({ type = 'boolean', label = L }) end)\n"
		+ "t('badType', function() rack.registerContextMenu({ type = 'knob', label = L, onChange = function() end }) end)\n"
		+ "t('good', function() rack.registerContextMenu({ type = 'options', label = L, options = { 'a', 'b' }, onChange = function() end }) end)\n";

	MenuResult r = runMenu(script(lang, lang == Lang::Js ? js : lua));
	REQUIRE(r.loaded);
	const std::string& log = r.loadLog;
	CATCH_INFO("log:\n" << log);
	for (const char* bad : { "nonString", "empty", "noOnChange", "badType" }) {
		CATCH_INFO(bad);
		REQUIRE(log.find(std::string("err ") + bad) != std::string::npos);
		REQUIRE(log.find(std::string("ok ") + bad) == std::string::npos);
	}
	REQUIRE(log.find("ok good") != std::string::npos);

	REQUIRE(r.specs.size() == 1);
	REQUIRE(r.specs[0].label == label);
}


// ── options as [label, value] pairs, "separator" and "label" items ───────────

static std::string jsMenu(const std::string& body) {
	return "/**\n * @engine QuickJs@v1\n */\n" + body + "\n";
}
static std::string luaMenu(const std::string& body) {
	return "--[[\n@engine minilua@v1\n--]]\n" + body + "\n";
}

// A pairs item whose onGetValue returns `get` and whose onChange logs the value it got.
static MenuResult runPairs(const std::string& jsOptions, const std::string& luaOptions, const std::string& jsGet,
                           const std::string& luaGet, MenuResult* luaOut, int clickIdx = -1) {
	MenuResult js = runMenu(jsMenu(
		"rack.registerContextMenu({ type: \"options\", label: \"M\", options: " + jsOptions + ",\n"
		"  onGetValue: function() { return " + jsGet + "; },\n"
		"  onChange: function(v, label) { rack.log(\"got \" + typeof v + \" \" + String(v) + \" \" + label); } });"),
		clickIdx < 0 ? -1 : 1, clickIdx);
	*luaOut = runMenu(luaMenu(
		"rack.registerContextMenu({ type = \"options\", label = \"M\", options = " + luaOptions + ",\n"
		"  onGetValue = function() return " + luaGet + " end,\n"
		"  onChange = function(v, label) rack.log(\"got \" .. type(v) .. \" \" .. tostring(v) .. \" \" .. label) end })"),
		clickIdx < 0 ? -1 : 1, clickIdx);
	return js;
}

TEST_CASE("Options pairs: onGetValue selects by value, no match checks nothing", "[MidiKit][CrossEngine]") {
	const std::string jsOpts = "[[\"1x\", 1], [\"2x\", 2], [\"half\", 0.5], [\"Off\", \"off\"], [\"On\", true]]";
	const std::string luaOpts = "{ {\"1x\", 1}, {\"2x\", 2}, {\"half\", 0.5}, {\"Off\", \"off\"}, {\"On\", true} }";
	struct Case { const char* js; const char* lua; int selected; };
	const Case cases[] = {
		{"1", "1", 0}, {"2", "2", 1}, {"0.5", "0.5", 2}, {"\"off\"", "\"off\"", 3}, {"true", "true", 4},
		{"7", "7", -1}, {"\"2\"", "\"2\"", -1}, {"false", "false", -1}, {"null", "nil", -1},
	};
	for (const Case& c : cases) {
		MenuResult lua;
		MenuResult js = runPairs(jsOpts, luaOpts, c.js, c.lua, &lua);
		REQUIRE(js.loaded);
		REQUIRE(lua.loaded);
		requireSameMenus(js.specs, lua.specs);
		REQUIRE(js.specs.size() == 1);
		REQUIRE(js.specs[0].options.size() == 5);
		REQUIRE(js.specs[0].optionValues.size() == 5);
		REQUIRE(js.specs[0].selected == c.selected);
	}
}

TEST_CASE("Options pairs: onGetValue omitted checks the first option", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(jsMenu("rack.registerContextMenu({ type: \"options\", label: \"M\", options: [[\"a\", 5], [\"b\", 6]], onChange: function() {} });"));
	MenuResult lua = runMenu(luaMenu("rack.registerContextMenu({ type = \"options\", label = \"M\", options = { {\"a\", 5}, {\"b\", 6} }, onChange = function() end })"));
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs[0].selected == 0);
}

TEST_CASE("Options pairs: a click passes the value and the label to onChange", "[MidiKit][CrossEngine]") {
	const std::string jsOpts = "[[\"1x\", 1], [\"half\", 0.5], [\"Off\", \"off\"], [\"On\", true]]";
	const std::string luaOpts = "{ {\"1x\", 1}, {\"half\", 0.5}, {\"Off\", \"off\"}, {\"On\", true} }";
	const char* expected[] = {"got number 1 1x", "got number 0.5 half", "got string off Off", "got boolean true On"};
	for (int i = 0; i < 4; i++) {
		MenuResult lua;
		MenuResult js = runPairs(jsOpts, luaOpts, "1", "1", &lua, i);
		REQUIRE(js.log.find(expected[i]) != std::string::npos);
		REQUIRE(lua.log.find(expected[i]) != std::string::npos);
	}
}

TEST_CASE("Options as a plain list of labels is unchanged", "[MidiKit][CrossEngine]") {
	MenuResult lua;
	MenuResult js = runPairs("[\"a\", \"b\", \"c\"]", "{ \"a\", \"b\", \"c\" }", "2", "2", &lua, 1);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs[0].optionValues.empty());
	REQUIRE(js.specs[0].selected == 2);
	REQUIRE(js.log.find("got number 1 b") != std::string::npos);
	REQUIRE(lua.log.find("got number 1 b") != std::string::npos);
}

TEST_CASE("Options pairs: malformed options fail the load in both engines", "[MidiKit][CrossEngine]") {
	struct Case { const char* js; const char* lua; const char* message; };
	const Case cases[] = {
		{"[]", "{}", "non-empty array"},
		{"[\"a\", [\"b\", 1]]", "{ \"a\", {\"b\", 1} }", "mix labels and pairs"},
		{"[[\"a\", 1], \"b\"]", "{ {\"a\", 1}, \"b\" }", "mix labels and pairs"},
		{"[[\"a\", 1, 2]]", "{ {\"a\", 1, 2} }", "must be [label, value]"},
		{"[[\"a\"]]", "{ {\"a\"} }", "must be [label, value]"},
		{"[[1, 1]]", "{ {1, 1} }", "string label"},
		{"[[\"a\", null]]", "{ {\"a\", {}} }", "string label"},
		{"[[\"a\", [1]]]", "{ {\"a\", {1}} }", "string label"},
		{"[[\"a\", NaN]]", "{ {\"a\", 0/0} }", "finite"},
		{"[[\"a\", Infinity]]", "{ {\"a\", math.huge} }", "finite"},
		{"[[\"a\", 1], [\"a\", 2]]", "{ {\"a\", 1}, {\"a\", 2} }", "same label"},
		{"[[\"a\", 1], [\"b\", 1]]", "{ {\"a\", 1}, {\"b\", 1} }", "same value"},
		{"[[\"a\", 1], [\"b\", 1.0]]", "{ {\"a\", 1}, {\"b\", 1.0} }", "same value"},
	};
	for (const Case& c : cases) {
		MenuResult js = runMenu(jsMenu(std::string("rack.registerContextMenu({ type: \"options\", label: \"M\", options: ") + c.js + ", onChange: function() {} });"));
		MenuResult lua = runMenu(luaMenu(std::string("rack.registerContextMenu({ type = \"options\", label = \"M\", options = ") + c.lua + ", onChange = function() end })"));
		REQUIRE_FALSE(js.loaded);
		REQUIRE_FALSE(lua.loaded);
		REQUIRE(js.loadLog.find(c.message) != std::string::npos);
		REQUIRE(lua.loadLog.find(c.message) != std::string::npos);
	}
}

TEST_CASE("Options pairs: equal values of different types are not duplicates", "[MidiKit][CrossEngine]") {
	MenuResult lua;
	MenuResult js = runPairs("[[\"a\", 1], [\"b\", \"1\"], [\"c\", true]]", "{ {\"a\", 1}, {\"b\", \"1\"}, {\"c\", true} }", "\"1\"", "\"1\"", &lua);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs[0].selected == 1);
}

TEST_CASE("Separator and label items", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(jsMenu(
		"rack.registerContextMenu({ type: \"separator\" });\n"
		"rack.registerContextMenu({ type: \"label\", label: \"Clock\" });\n"
		"rack.registerContextMenu({ type: \"action\", label: \"Go\", onChange: function() {} });\n"
		"rack.registerContextMenu({ type: \"separator\" });"));
	MenuResult lua = runMenu(luaMenu(
		"rack.registerContextMenu({ type = \"separator\" })\n"
		"rack.registerContextMenu({ type = \"label\", label = \"Clock\" })\n"
		"rack.registerContextMenu({ type = \"action\", label = \"Go\", onChange = function() end })\n"
		"rack.registerContextMenu({ type = \"separator\" })"));
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	REQUIRE(js.specs.size() == 4);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Separator);
	REQUIRE(js.specs[1].type == ScriptMenuItem::Type::Label);
	REQUIRE(js.specs[1].label == "Clock");
	REQUIRE(js.specs[2].type == ScriptMenuItem::Type::Action);
	// Two separators are two items, in registration order.
	REQUIRE(js.specs[3].type == ScriptMenuItem::Type::Separator);

	// A label needs a text, but neither it nor a separator needs an onChange.
	js = runMenu(jsMenu("rack.registerContextMenu({ type: \"label\" });"));
	lua = runMenu(luaMenu("rack.registerContextMenu({ type = \"label\" })"));
	REQUIRE_FALSE(js.loaded);
	REQUIRE_FALSE(lua.loaded);

	// Unregistering by an empty label does not remove a separator.
	js = runMenu(jsMenu("rack.registerContextMenu({ type: \"separator\" });\nrack.log(\"r=\" + rack.unregisterContextMenu(\"\"));"));
	lua = runMenu(luaMenu("rack.registerContextMenu({ type = \"separator\" })\nrack.log(\"r=\" .. tostring(rack.unregisterContextMenu(\"\")))"));
	REQUIRE(js.specs.size() == 1);
	REQUIRE(lua.specs.size() == 1);
}

// ── "#midichannel" ───────────────────────────────────────────────────────────

TEST_CASE("#midichannel fills in the channels and the label", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(jsMenu(
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannel\",\n"
		"  onGetValue: function() { return 5; },\n"
		"  onChange: function(v, label) { rack.log(\"got \" + v + \" \" + label); } });"), 1, 9);
	MenuResult lua = runMenu(luaMenu(
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannel\",\n"
		"  onGetValue = function() return 5 end,\n"
		"  onChange = function(v, label) rack.log(\"got \" .. v .. \" \" .. label) end })"), 1, 9);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs.size() == 1);
	REQUIRE(js.specs[0].label == "MIDI channel");
	REQUIRE(js.specs[0].options.size() == 16);
	REQUIRE(js.specs[0].options[0] == "1");
	REQUIRE(js.specs[0].options[15] == "16");
	REQUIRE(js.specs[0].optionValues[15] == ScriptMenuArg::ofInt(16));
	REQUIRE(js.specs[0].selected == 4);
	// Option index 9 is channel 10.
	REQUIRE(js.log.find("got 10 10") != std::string::npos);
	REQUIRE(lua.log.find("got 10 10") != std::string::npos);
}

TEST_CASE("#midichannel+all adds an All entry with the value 0", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(jsMenu(
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannel+all\",\n"
		"  onGetValue: function() { return 0; }, onChange: function(v, label) { rack.log(\"got \" + v + \" \" + label); } });"), 1, 0);
	MenuResult lua = runMenu(luaMenu(
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannel+all\",\n"
		"  onGetValue = function() return 0 end, onChange = function(v, label) rack.log(\"got \" .. v .. \" \" .. label) end })"), 1, 0);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs[0].label == "MIDI channel");
	REQUIRE(js.specs[0].options.size() == 17);
	REQUIRE(js.specs[0].options[0] == "All");
	REQUIRE(js.specs[0].options[1] == "1");
	REQUIRE(js.specs[0].optionValues[0] == ScriptMenuArg::ofInt(0));
	REQUIRE(js.specs[0].selected == 0);
	REQUIRE(js.log.find("got 0 All") != std::string::npos);
	REQUIRE(lua.log.find("got 0 All") != std::string::npos);
}

TEST_CASE("#midichannel ignores the options of the script", "[MidiKit][CrossEngine]") {
	// Valid, malformed and wrong-typed options alike: none of them is looked at.
	struct Case { const char* js; const char* lua; };
	const Case cases[] = {
		{"[[\"Ten\", 10]]", "{ {\"Ten\", 10} }"},
		{"[]", "{}"},
		{"[\"a\", 42]", "{ \"a\", 42 }"},
		{"\"nope\"", "\"nope\""},
	};
	for (const Case& c : cases) {
		MenuResult js = runMenu(jsMenu(std::string("rack.registerContextMenu({ type: \"options\", label: \"#midichannel\", options: ") + c.js + ", onChange: function() {} });"));
		MenuResult lua = runMenu(luaMenu(std::string("rack.registerContextMenu({ type = \"options\", label = \"#midichannel\", options = ") + c.lua + ", onChange = function() end })"));
		REQUIRE(js.loaded);
		REQUIRE(lua.loaded);
		requireSameMenus(js.specs, lua.specs);
		REQUIRE(js.specs[0].options.size() == 16);
	}
}

TEST_CASE("#midichannel is a plain label on other item types", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(jsMenu("rack.registerContextMenu({ type: \"action\", label: \"#midichannel\", onChange: function() {} });"));
	MenuResult lua = runMenu(luaMenu("rack.registerContextMenu({ type = \"action\", label = \"#midichannel\", onChange = function() end })"));
	REQUIRE(js.specs[0].label == "#midichannel");
	REQUIRE(lua.specs[0].label == "#midichannel");
}

TEST_CASE("Preset menus are keyed by the registered label, not the display text", "[MidiKit][CrossEngine]") {
	const std::string jsReg =
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannel\", onChange: function() {} });\n"
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannel+all\", onChange: function() {} });\n"
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannel Out\", onChange: function() {} });\n"
		"rack.registerContextMenu({ type: \"boolean\", label: \"MIDI channel\", onChange: function() {} });\n"
		"rack.log(\"u=\" + rack.unregisterContextMenu(\"#midichannel Out\") + rack.unregisterContextMenu(\"#midichannel Out\"));";
	const std::string luaReg =
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannel\", onChange = function() end })\n"
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannel+all\", onChange = function() end })\n"
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannel Out\", onChange = function() end })\n"
		"rack.registerContextMenu({ type = \"boolean\", label = \"MIDI channel\", onChange = function() end })\n"
		"rack.log(\"u=\" .. tostring(rack.unregisterContextMenu(\"#midichannel Out\")) .. tostring(rack.unregisterContextMenu(\"#midichannel Out\")))";
	MenuResult js = runMenu(jsMenu(jsReg));
	MenuResult lua = runMenu(luaMenu(luaReg));
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	// "#midichannel", "#midichannel+all" and the plain "MIDI channel" item stay apart;
	// "#midichannel Out" was removed by the label it was registered with.
	REQUIRE(js.specs.size() == 3);
	REQUIRE(js.specs[0].label == "MIDI channel");
	REQUIRE(js.specs[1].label == "MIDI channel");
	REQUIRE(js.specs[1].options[0] == "All");
	REQUIRE(js.specs[2].type == ScriptMenuItem::Type::Boolean);
	REQUIRE(js.loadLog.find("u=truefalse") != std::string::npos);
	REQUIRE(lua.loadLog.find("u=truefalse") != std::string::npos);
}

TEST_CASE("#midichannel takes a suffix for the label", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(jsMenu(
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannel Output\", onChange: function() {} });\n"
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannel+all Input\", onChange: function() {} });\n"
		"rack.registerContextMenu({ type: \"options\", label: \"#midichannelX\", options: [[\"a\", 1]], onChange: function() {} });"));
	MenuResult lua = runMenu(luaMenu(
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannel Output\", onChange = function() end })\n"
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannel+all Input\", onChange = function() end })\n"
		"rack.registerContextMenu({ type = \"options\", label = \"#midichannelX\", options = { {\"a\", 1} }, onChange = function() end })"));
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs.size() == 3);
	REQUIRE(js.specs[0].label == "MIDI channel (Output)");
	REQUIRE(js.specs[0].options.size() == 16);
	REQUIRE(js.specs[1].label == "MIDI channel (Input)");
	REQUIRE(js.specs[1].options.size() == 17);
	// Not a key followed by a space: an ordinary label.
	REQUIRE(js.specs[2].label == "#midichannelX");
	REQUIRE(js.specs[2].options.size() == 1);
}
