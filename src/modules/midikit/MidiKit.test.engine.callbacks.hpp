// trig.onTrigger dispatch and the context-menu API.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.

static const char* JS_ON_TRIGGER = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
trig.onTrigger = function(trigPort) {
    rack.log("onTrigger " + number.toString(trigPort));
    let msg = midi.create();
    midi.setCc(msg, 1, 10, trigPort);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_TRIGGER = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
function trig.onTrigger(trigPort)
    rack.log("onTrigger " .. trigPort)
    local msg = midi.create()
    midi.setCc(msg, 1, 10, trigPort)
    midiOut.send(msg)
end
)";

TEST_CASE("onTrigger fires on a trigger input tick and sends an identical message in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto checkOnTrigger = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		REQUIRE(log.find("onTrigger 1") != std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		auto sent = toSent(port, ticks, out);
		Test::destroyModule(m);
		return sent;
	};

	auto js = checkOnTrigger(JS_ON_TRIGGER);
	auto lua = checkOnTrigger(LUA_ON_TRIGGER);
	REQUIRE(js.port == lua.port);
	REQUIRE(js.bytes == lua.bytes);
}


static const char* JS_ON_TRIGGER_CHANNEL = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(1, 2);
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger " + number.toString(trigPort) + " " + number.toString(channel));
};
)";

static const char* LUA_ON_TRIGGER_CHANNEL = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(1, 2)
function trig.onTrigger(trigPort, channel)
    rack.log("onTrigger " .. trigPort .. " " .. channel)
end
)";

TEST_CASE("onTrigger receives the firing channel, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto logChannels = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		// Channels are 1-based in the callback: index 0 -> "1", index 1 -> "2".
		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();
		m->host.getActiveEngine()->processInTick(0, 1);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		Test::destroyModule(m);
		return log;
	};

	auto js = logChannels(JS_ON_TRIGGER_CHANNEL);
	auto lua = logChannels(LUA_ON_TRIGGER_CHANNEL);
	REQUIRE(js.find("onTrigger 1 1") != std::string::npos);
	REQUIRE(js.find("onTrigger 1 2") != std::string::npos);
	REQUIRE(lua.find("onTrigger 1 1") != std::string::npos);
	REQUIRE(lua.find("onTrigger 1 2") != std::string::npos);
}


TEST_CASE("Script without onTrigger silently ignores trigger ticks, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto checkNoOnTrigger = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		int port, ticks;
		midi::Message out;
		bool sentAnything = processOutMessage(m, port, out, ticks);
		Test::destroyModule(m);
		return std::make_pair(log, sentAnything);
	};

	auto js = checkNoOnTrigger(JS_NO_ON_LOAD);
	auto lua = checkNoOnTrigger(LUA_NO_ON_LOAD);
	REQUIRE(js.second == false);
	REQUIRE(lua.second == false);
}


static const char* JS_ON_TRIGGER_NOT_ENABLED = R"(/**
 * @engine QuickJs@v1
 */
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger fired");
};
)";

static const char* LUA_ON_TRIGGER_NOT_ENABLED = R"(--[[
@engine minilua@v1
--]]
trig.onTrigger = function(trigPort, channel)
    rack.log("onTrigger fired")
end
)";

TEST_CASE("trig.onTrigger is not called until trig.enableIn() is used, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	// trig.onTrigger is unused until the channel is enabled with trig.enableIn().
	auto checkNotEnabled = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
		// Without trig.enableIn(), a rising edge is not processed at all.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		m->process(Test::makeProcessArgs(0));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		m->process(Test::makeProcessArgs(1));
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		Test::destroyModule(m);
		return log;
	};

	REQUIRE(checkNotEnabled(JS_ON_TRIGGER_NOT_ENABLED).find("onTrigger") == std::string::npos);
	REQUIRE(checkNotEnabled(LUA_ON_TRIGGER_NOT_ENABLED).find("onTrigger") == std::string::npos);
}


static const char* JS_ON_TRIGGER_ENABLE_CH1_ONLY = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger " + number.toString(channel));
};
)";

static const char* LUA_ON_TRIGGER_ENABLE_CH1_ONLY = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.onTrigger = function(trigPort, channel)
    rack.log("onTrigger " .. channel)
end
)";

TEST_CASE("trig.enableIn gates trig.onTrigger per channel, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	// Only channel 1 is enabled: a rising edge on channel 1 fires the callback,
	// a rising edge on channel 2 (never enabled) is not processed at all.
	auto checkPerChannel = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;
		// Prime both SchmittTriggers LOW first (a fresh trigger starts
		// uninitialized; the first low call locks it so a later rise is real).
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
		m->process(Test::makeProcessArgs(0));

		// Rising edge on channel 1 (enabled) fires the callback.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
		m->process(Test::makeProcessArgs(1));
		m->host.getActiveEngine()->process();
		std::string log1 = drainLog(m);

		// Rising edge on channel 2 (never enabled) is ignored.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
		m->process(Test::makeProcessArgs(2));
		m->host.getActiveEngine()->process();
		std::string log2 = drainLog(m);

		Test::destroyModule(m);
		return std::make_pair(log1, log2);
	};

	auto js = checkPerChannel(JS_ON_TRIGGER_ENABLE_CH1_ONLY);
	auto lua = checkPerChannel(LUA_ON_TRIGGER_ENABLE_CH1_ONLY);
	REQUIRE(js.first.find("onTrigger 1") != std::string::npos);
	REQUIRE(js.second.find("onTrigger") == std::string::npos);
	REQUIRE(lua.first.find("onTrigger 1") != std::string::npos);
	REQUIRE(lua.second.find("onTrigger") == std::string::npos);
}


// send() order, not handle-creation order
// Regression test for the send-order bug: the engine used to push the
// out-queue in msgStore index (handle-creation) order, so a script that
// created several messages and then sent them in a different order had them
// reordered on the wire. The receiver must observe send() order. This
// creates A, B, C (handle order) but sends C, A, B, and asserts the wire
// order is C, A, B in both engines.

static const char* JS_SEND_ORDER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let a = midi.create();
    midi.setNoteOn(a, 1, 60, 100);
    let b = midi.create();
    midi.setNoteOn(b, 1, 62, 100);
    let c = midi.create();
    midi.setNoteOn(c, 1, 64, 100);
    midiOut.send(c);   // handle 2 sent first
    midiOut.send(a);   // handle 0 sent second
    midiOut.send(b);   // handle 1 sent last
};
)";

static const char* LUA_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local a = midi.create()
    midi.setNoteOn(a, 1, 60, 100)
    local b = midi.create()
    midi.setNoteOn(b, 1, 62, 100)
    local c = midi.create()
    midi.setNoteOn(c, 1, 64, 100)
    midiOut.send(c)
    midiOut.send(a)
    midiOut.send(b)
end
)";

TEST_CASE("out-queue is in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_SEND_ORDER);
	EngineResult lua = run(LUA_SEND_ORDER);

	// Handle order would be 60, 62, 64; send() order is 64, 60, 62. The
	// script's channel argument is 1-based, so channel 1 = internal channel 0
	// = status nibble 0x9 | 0 = 0x90.
	std::vector<uint8_t> expectC = {0x90, 64, 100};
	std::vector<uint8_t> expectA = {0x90, 60, 100};
	std::vector<uint8_t> expectB = {0x90, 62, 100};

	REQUIRE(js.sent.size() == 3);
	REQUIRE(js.sent[0].bytes == expectC);
	REQUIRE(js.sent[1].bytes == expectA);
	REQUIRE(js.sent[2].bytes == expectB);

	REQUIRE(lua.sent.size() == 3);
	REQUIRE(lua.sent[0].bytes == expectC);
	REQUIRE(lua.sent[1].bytes == expectA);
	REQUIRE(lua.sent[2].bytes == expectB);
}


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
	MidiKitModule* m = createModule();
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
	Test::destroyModule(m);
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
	ModuleScaffold mods;
	auto click = [](const std::string& script) {
		MidiKitModule* m = createModule();
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
		auto sent = toSent(port, ticks, out);
		Test::destroyModule(m);
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
rack.registerContextMenu({ type: "file", label: "Import", onChange: function(content, name) { rack.log("import " + content.length + " [" + content + "] " + name + " args=" + arguments.length); } });
rack.registerContextMenu({ type: "boolean", label: "Flag", onChange: function(v) { rack.log("flag " + v); } });
)";

static const char* LUA_ACTION_FILE_MENU = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "action", label = "Go", onGetValue = function() return 1 end, onChange = function(...) rack.log("go args=" .. select('#', ...)) end })
rack.registerContextMenu({ type = "file", label = "Import", onChange = function(content, name, ...) rack.log("import " .. #content .. " [" .. content .. "] " .. name .. " args=" .. (2 + select('#', ...))) end })
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
	REQUIRE(js.specs[1].type == ScriptMenuItem::Type::File);
	REQUIRE(js.specs[1].label == "Import");
	REQUIRE(js.specs[2].type == ScriptMenuItem::Type::Boolean);
}

TEST_CASE("An action item calls onChange without arguments on every click, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	for (const char* script : { JS_ACTION_FILE_MENU, LUA_ACTION_FILE_MENU }) {
		MenuResult r = runMenu(script, 1);   // "Go" is registered first: id 1
		REQUIRE(r.loaded);
		REQUIRE(r.log.find("go args=0") != std::string::npos);

		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);
		for (int i = 0; i < 3; i++) m->host.getActiveEngine()->invokeContextMenuCallback(1, i);
		std::string log = drainLog(m);
		size_t n = 0;
		for (size_t at = log.find("go args=0"); at != std::string::npos; at = log.find("go args=0", at + 1)) n++;
		REQUIRE(n == 3);
		Test::destroyModule(m);
	}
}

TEST_CASE("A file item passes the file's text and name to onChange, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	for (const char* script : { JS_ACTION_FILE_MENU, LUA_ACTION_FILE_MENU }) {
		MidiKitModule* m = createModule();
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
		Test::destroyModule(m);
	}
}

TEST_CASE("Context menu clicks of the wrong kind are ignored, in both engines", "[MidiKit][CrossEngine][ContextMenu]") {
	for (const char* script : { JS_ACTION_FILE_MENU, LUA_ACTION_FILE_MENU }) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);
		auto* e = m->host.getActiveEngine();

		e->invokeContextMenuCallback(2, 0);               // a file item needs a file
		e->invokeContextMenuCallback(1, ScriptMenuClick::file("x", "x.txt"));   // an action item takes none
		e->invokeContextMenuCallback(3, ScriptMenuClick::file("x", "x.txt"));   // nor does a boolean
		e->invokeContextMenuCallback(99, ScriptMenuClick::file("x", "x.txt"));  // unknown id
		REQUIRE(drainLog(m).empty());
		Test::destroyModule(m);
	}
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
	REQUIRE(js.loadLog.find("\"file\"") != std::string::npos);
	REQUIRE(lua.loadLog.find("\"action\"") != std::string::npos);
	REQUIRE(lua.loadLog.find("\"file\"") != std::string::npos);
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
	fileItem.type = ScriptMenuItem::Type::File;
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
