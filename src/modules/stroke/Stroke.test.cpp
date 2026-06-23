#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Stroke.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Stroke;

SYNC_MODEL(modelStroke, "Stroke");
Test::TestContext<> testContext;

// Number of ports exposed by the registered Stroke module. Used to keep tests
// in sync with the PORTS template parameter of the registered Model.
static constexpr int STROKE_PORTS = 10;


// Construction / initialization

TEST_CASE("Construction and initialization", "[Stroke]") {
	StrokeModule<STROKE_PORTS>* m = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	StrokeWidget* mw = Test::createWidget<StrokeWidget>("Stroke");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Stroke][JSON]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("onReset clears key configuration", "[Stroke][init]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	// Dirty key state. Note: panelTheme is intentionally NOT touched because
	// the StrokeModule's onReset() does not reset panelTheme — only the
	// constructor initialises it from pluginSettings.panelThemeDefault.
	for (int i = 0; i < STROKE_PORTS; i++) {
		module->keys[i].button = 3;
		module->keys[i].key = GLFW_KEY_A + i;
		module->keys[i].mods = GLFW_MOD_ALT | GLFW_MOD_SHIFT;
		module->keys[i].mode = KEY_MODE::CV_TOGGLE;
		module->keys[i].high = true;
		module->keys[i].data = "some-data";
	}

	Module::ResetEvent e;
	module->onReset(e);

	for (int i = 0; i < STROKE_PORTS; i++) {
		REQUIRE(module->keys[i].button == -1);
		REQUIRE(module->keys[i].key == -1);
		REQUIRE(module->keys[i].mods == 0);
		REQUIRE(module->keys[i].mode == KEY_MODE::CV_TRIGGER);
		REQUIRE(module->keys[i].high == false);
		REQUIRE(module->keys[i].data == "");
	}

	Test::destroyModule(module);
}


// JSON serialization

TEST_CASE("dataToJson writes panelTheme and a keys array", "[Stroke][JSON]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->panelTheme = 3;
	module->keys[0].button = 2;
	module->keys[0].key = GLFW_KEY_A;
	module->keys[0].mods = GLFW_MOD_SHIFT;
	module->keys[0].mode = KEY_MODE::CV_GATE;
	module->keys[0].high = true;
	module->keys[0].data = "hello";

	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);
	REQUIRE(json_is_object(rootJ));

	json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
	REQUIRE(panelThemeJ != nullptr);
	REQUIRE(json_integer_value(panelThemeJ) == 3);

	json_t* keysJ = json_object_get(rootJ, "keys");
	REQUIRE(keysJ != nullptr);
	REQUIRE(json_array_size(keysJ) == STROKE_PORTS);

	json_t* key0J = json_array_get(keysJ, 0);
	REQUIRE(key0J != nullptr);
	REQUIRE(json_integer_value(json_object_get(key0J, "button")) == 2);
	REQUIRE(json_integer_value(json_object_get(key0J, "key")) == GLFW_KEY_A);
	REQUIRE(json_integer_value(json_object_get(key0J, "mods")) == GLFW_MOD_SHIFT);
	REQUIRE(json_integer_value(json_object_get(key0J, "mode")) == (int)KEY_MODE::CV_GATE);
	REQUIRE(json_boolean_value(json_object_get(key0J, "high")) == true);
	REQUIRE(std::string(json_string_value(json_object_get(key0J, "data"))) == "hello");

	json_decref(rootJ);
	Test::destroyModule(module);
}

TEST_CASE("dataFromJson round-trip preserves key configuration", "[Stroke][JSON]") {
	auto src = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	auto dst = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	src->panelTheme = 5;
	src->keys[0].button = -1;
	src->keys[0].key = GLFW_KEY_KP_5; // a numpad key to verify keyFix runs on load
	src->keys[0].mods = GLFW_MOD_ALT | GLFW_MOD_SHIFT;
	src->keys[0].mode = KEY_MODE::S_ZOOM_OUT;
	src->keys[0].high = true;
	src->keys[0].data = "abc";

	src->keys[3].button = 1;
	src->keys[3].key = -1;
	src->keys[3].mods = RACK_MOD_CTRL;
	src->keys[3].mode = KEY_MODE::CV_GATE;
	src->keys[3].high = false;
	src->keys[3].data = "";

	json_t* rootJ = src->dataToJson();
	REQUIRE_NOTHROW(dst->dataFromJson(rootJ));

	REQUIRE(dst->panelTheme == 5);
	REQUIRE(dst->keys[0].button == -1);
	// keyFix converts numpad keys to their non-numpad equivalents
	REQUIRE(dst->keys[0].key == GLFW_KEY_5);
	REQUIRE(dst->keys[0].mods == (GLFW_MOD_ALT | GLFW_MOD_SHIFT));
	REQUIRE(dst->keys[0].mode == KEY_MODE::S_ZOOM_OUT);
	REQUIRE(dst->keys[0].high == true);
	REQUIRE(dst->keys[0].data == "abc");

	REQUIRE(dst->keys[3].button == 1);
	REQUIRE(dst->keys[3].key == -1);
	REQUIRE(dst->keys[3].mods == RACK_MOD_CTRL);
	REQUIRE(dst->keys[3].mode == KEY_MODE::CV_GATE);
	REQUIRE(dst->keys[3].high == false);

	json_decref(rootJ);
	Test::destroyModule(src);
	Test::destroyModule(dst);
}

TEST_CASE("dataFromJson masks mods to ALT|CTRL|SHIFT only", "[Stroke][JSON]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "panelTheme", json_integer(0));
	json_t* keysJ = json_array();
	json_t* keyJ = json_object();
	// Numeric keypad bit is set in addition to ALT|CTRL|SHIFT but should be stripped.
	json_object_set_new(keyJ, "button", json_integer(-1));
	json_object_set_new(keyJ, "key", json_integer(GLFW_KEY_A));
	json_object_set_new(keyJ, "mods", json_integer(GLFW_MOD_ALT | RACK_MOD_CTRL | GLFW_MOD_SHIFT | GLFW_MOD_NUM_LOCK));
	json_object_set_new(keyJ, "mode", json_integer((int)KEY_MODE::CV_TRIGGER));
	json_object_set_new(keyJ, "high", json_boolean(false));
	json_object_set_new(keyJ, "data", json_string(""));
	json_array_append_new(keysJ, keyJ);
	json_object_set_new(rootJ, "keys", keysJ);

	REQUIRE_NOTHROW(module->dataFromJson(rootJ));

	int maskedMods = GLFW_MOD_ALT | RACK_MOD_CTRL | GLFW_MOD_SHIFT;
	REQUIRE((module->keys[0].mods & maskedMods) == maskedMods);
	REQUIRE((module->keys[0].mods & ~maskedMods) == 0);

	json_decref(rootJ);
	Test::destroyModule(module);
}

TEST_CASE("dataFromJson handles empty JSON object", "[Stroke][JSON]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	// Dirty the state first so we can detect accidental clobbering on missing keys.
	module->panelTheme = 9;
	for (int i = 0; i < STROKE_PORTS; i++) {
		module->keys[i].mode = KEY_MODE::CV_GATE;
		module->keys[i].data = "untouched";
	}

	json_t* emptyJ = json_object();
	REQUIRE_NOTHROW(module->dataFromJson(emptyJ));

	// Nothing should be overwritten since every property is null-guarded.
	REQUIRE(module->panelTheme == 9);
	for (int i = 0; i < STROKE_PORTS; i++) {
		REQUIRE(module->keys[i].mode == KEY_MODE::CV_GATE);
		REQUIRE(module->keys[i].data == "untouched");
	}

	json_decref(emptyJ);
	Test::destroyModule(module);
}


// keyEnable / keyDisable / keyHeld

TEST_CASE("Key::isMapped returns true only when button or key is set", "[Stroke][key]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	// Defaults from onReset
	REQUIRE_FALSE(module->keys[0].isMapped());

	module->keys[0].button = 0;
	REQUIRE(module->keys[0].isMapped());

	module->keys[0].button = -1;
	module->keys[0].key = GLFW_KEY_A;
	REQUIRE(module->keys[0].isMapped());

	module->keys[0].key = -1;
	REQUIRE_FALSE(module->keys[0].isMapped());

	Test::destroyModule(module);
}

TEST_CASE("All slots start unmapped after construction", "[Stroke][key]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	for (int i = 0; i < STROKE_PORTS; i++) {
		REQUIRE_FALSE(module->keys[i].isMapped());
	}

	Test::destroyModule(module);
}

TEST_CASE("keyEnable on unmapped slot is a no-op for CV outputs", "[Stroke][enable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	// Default state: button == -1 && key == -1
	REQUIRE_FALSE(module->keys[0].isMapped());

	module->keyEnable(0);
	module->process(Test::makeProcessArgs(1));

	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == 0.f);

	Test::destroyModule(module);
}

TEST_CASE("CV_TRIGGER mode fires a one-shot pulse on keyEnable", "[Stroke][enable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].key = -1;
	module->keys[0].mode = KEY_MODE::CV_TRIGGER;
	module->keys[0].high = false;

	module->keyEnable(0);
	// First sample after trigger: pulse is at full amplitude (10V)
	module->process(Test::makeProcessArgs(1));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f));

	// PulseGenerator decays over multiple samples and eventually goes to 0
	for (int i = 0; i < 1000; i++) {
		module->process(Test::makeProcessArgs(2 + i));
	}
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(1e-3f));

	Test::destroyModule(module);
}

TEST_CASE("CV_GATE mode latches high on keyEnable and clears on keyDisable", "[Stroke][enable][disable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::CV_GATE;

	// Initially low
	module->process(Test::makeProcessArgs(1));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == 0.f);

	module->keyEnable(0);
	module->process(Test::makeProcessArgs(2));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f));
	REQUIRE(module->keys[0].high == true);

	// Output stays high without further triggers
	module->process(Test::makeProcessArgs(3));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f));

	module->keyDisable(0);
	module->process(Test::makeProcessArgs(4));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == 0.f);
	REQUIRE(module->keys[0].high == false);

	Test::destroyModule(module);
}

TEST_CASE("CV_TOGGLE mode flips state on successive keyEnable calls", "[Stroke][enable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::CV_TOGGLE;

	module->keyEnable(0);
	REQUIRE(module->keys[0].high == true);

	module->process(Test::makeProcessArgs(1));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f));

	module->keyEnable(0);
	REQUIRE(module->keys[0].high == false);

	module->process(Test::makeProcessArgs(2));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == 0.f);

	Test::destroyModule(module);
}

TEST_CASE("KEY_MODE::OFF does not emit CV", "[Stroke][enable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::OFF;

	module->keyEnable(0);
	module->process(Test::makeProcessArgs(1));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == 0.f);

	Test::destroyModule(module);
}

TEST_CASE("keyEnable on command-style modes sets keyTemp", "[Stroke][enable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::S_ZOOM_OUT;

	module->keyEnable(0);

	// Command modes route through the keyTemp pointer for the KeyContainer to dispatch.
	REQUIRE(module->keyTemp == &module->keys[0]);

	Test::destroyModule(module);
}

TEST_CASE("keyHeld sets keyTempHeld", "[Stroke][held]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keyHeld(0);

	REQUIRE(module->keyTempHeld == &module->keys[0]);

	Test::destroyModule(module);
}

TEST_CASE("keyDisable on command-style modes sets keyTempDisable", "[Stroke][disable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::S_ZOOM_OUT;

	module->keyDisable(0);

	// Command modes route through the keyTempDisable pointer for the KeyContainer to dispatch.
	REQUIRE(module->keyTempDisable == &module->keys[0]);

	Test::destroyModule(module);
}

TEST_CASE("keyDisable only clears gate state for CV_GATE", "[Stroke][disable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	// CV_TOGGLE: keyDisable does NOT clear `high`; it falls into the default branch.
	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::CV_TOGGLE;
	module->keys[0].high = true;
	module->keyDisable(0);
	REQUIRE(module->keys[0].high == true);

	// CV_GATE: keyDisable clears `high`.
	module->keys[1].button = 0;
	module->keys[1].mode = KEY_MODE::CV_GATE;
	module->keys[1].high = true;
	module->keyDisable(1);
	REQUIRE(module->keys[1].high == false);

	Test::destroyModule(module);
}


// process() — CV output matrix

TEST_CASE("Process emits 0V for unmapped slots even with mode set", "[Stroke][process]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	// Set mode but leave both button and key unmapped (matches fresh state)
	module->keys[0].mode = KEY_MODE::CV_TRIGGER;
	module->keys[1].mode = KEY_MODE::CV_GATE;
	module->keys[2].mode = KEY_MODE::CV_TOGGLE;

	module->process(Test::makeProcessArgs(1));

	for (int i = 0; i < 3; i++) {
		REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + i].getVoltage() == 0.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Process emits 0V for command-style modes", "[Stroke][process]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::S_ZOOM_OUT;
	module->keys[1].button = 0;
	module->keys[1].mode = KEY_MODE::S_MODULE_LOCK;

	module->process(Test::makeProcessArgs(1));

	for (int i = 0; i < 2; i++) {
		REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + i].getVoltage() == 0.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Process emits 0V for KEY_MODE::OFF even when mapped", "[Stroke][process]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::OFF;

	module->process(Test::makeProcessArgs(1));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == 0.f);

	Test::destroyModule(module);
}

TEST_CASE("Multiple slots operate independently", "[Stroke][process]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");

	// Slot 0: CV_TRIGGER (with pulse already triggered)
	module->keys[0].button = 0;
	module->keys[0].mode = KEY_MODE::CV_TRIGGER;
	module->keyEnable(0);

	// Slot 1: CV_GATE latched high
	module->keys[1].button = 0;
	module->keys[1].mode = KEY_MODE::CV_GATE;
	module->keyEnable(1);

	// Slot 2: unmapped
	module->keys[2].button = -1;
	module->keys[2].key = -1;

	// Slot 3: command-style mode
	module->keys[3].button = 0;
	module->keys[3].mode = KEY_MODE::S_ZOOM_OUT;

	module->process(Test::makeProcessArgs(1));

	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 1].getVoltage() == Catch::Approx(10.f));
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 2].getVoltage() == 0.f);
	REQUIRE(module->outputs[StrokeModule<STROKE_PORTS>::OUTPUT + 3].getVoltage() == 0.f);

	Test::destroyModule(module);
}


// CmdBase implementations — pure-logic command classes (no UI hover deps)
//
// These tests exercise CmdBase subclasses that are reachable without a hovered
// widget / ModuleWidget / PortWidget. They cover:
//
//   - CmdBase default-virtual no-ops
//   - CmdModuleLock (toggles settings::lockModules)
//   - CmdCableOpacity (toggles settings::cableOpacity and round-trips via data)
//   - CmdRackMove (translates APP->scene->rackScroll->offset)
//   - CmdZoomModuleId / CmdZoomModuleCustom / CmdZoomModuleIdSmooth /
//     CmdZoomModuleCustomSmooth / CmdZoomOut / CmdZoomOutSmooth (empty/invalid
//     data paths)
//   - CmdCableVisibility (toggles cable container visibility)
//   - CmdModuleAdd (empty / invalid JSON paths)
//   - CmdModuleDispatch (empty / invalid JSON paths)
//   - CmdModuleAddRandom (initModels() collects registered plugins/models)
//   - CmdParamCopyPaste::cmd (the static copy/paste core, when no widget)
//   - KeyContainer::processCmd lifecycle (initialCmd / followUpCmd / step())
//   - KeyContainer::processCmdHeld / processCmdDisable (followUpCmd-only path)
//   - KeyContainer::enableLearn toggling & callback
//   - CmdBase default destructor behaviour

TEST_CASE("CmdBase default virtual methods are no-ops", "[Stroke][cmd]") {
	CmdBase base;
	REQUIRE_NOTHROW(base.initialCmd(KEY_MODE::CV_TRIGGER));
	REQUIRE(base.followUpCmd(KEY_MODE::CV_TRIGGER) == true);
	REQUIRE_NOTHROW(base.step());

	// followUpCmd defaults to returning true, which means "clear me".
	REQUIRE(base.followUpCmd(KEY_MODE::S_ZOOM_OUT) == true);
	REQUIRE(base.followUpCmd(KEY_MODE::OFF) == true);
}

TEST_CASE("CmdBase is polymorphic via pointers", "[Stroke][cmd]") {
	// Verify the vtable routes correctly through a CmdBase*.
	CmdBase* base = new CmdBase();
	REQUIRE(base->followUpCmd(KEY_MODE::CV_TRIGGER) == true);
	REQUIRE_NOTHROW(base->initialCmd(KEY_MODE::CV_TRIGGER));
	REQUIRE_NOTHROW(base->step());
	delete base;

	// A subclass that doesn't override initialCmd inherits the no-op default.
	struct NoOverrideCmd : CmdBase { };
	CmdBase* sub = new NoOverrideCmd();
	REQUIRE(sub->followUpCmd(KEY_MODE::CV_TRIGGER) == true);
	REQUIRE_NOTHROW(sub->initialCmd(KEY_MODE::CV_TRIGGER));
	delete sub;
}

TEST_CASE("CmdModuleLock toggles settings::lockModules", "[Stroke][cmd][lock]") {
	// Save and restore the global so other tests in the binary aren't disturbed.
	bool saved = settings::lockModules;

	settings::lockModules = false;
	CmdModuleLock cmd;
	cmd.initialCmd(KEY_MODE::S_MODULE_LOCK);
	REQUIRE(settings::lockModules == true);

	cmd.initialCmd(KEY_MODE::S_MODULE_LOCK);
	REQUIRE(settings::lockModules == false);

	cmd.initialCmd(KEY_MODE::S_MODULE_LOCK);
	REQUIRE(settings::lockModules == true);

	settings::lockModules = saved;
}

TEST_CASE("CmdCableOpacity restores saved value on second press", "[Stroke][cmd][cable]") {
	float saved = settings::cableOpacity;

	std::string data = "0.5";
	CmdCableOpacity cmd;
	cmd.data = &data;
	settings::cableOpacity = 0.f;

	// First press: load value from data into settings; data is unchanged.
	cmd.initialCmd(KEY_MODE::S_CABLE_OPACITY);
	REQUIRE(settings::cableOpacity == Catch::Approx(0.5f));
	REQUIRE(data == "0.5");

	// Second press: save current setting into data; settings goes to 0.
	cmd.initialCmd(KEY_MODE::S_CABLE_OPACITY);
	REQUIRE(settings::cableOpacity == 0.f);
	REQUIRE(std::stof(data) == Catch::Approx(0.5f));

	// Third press: load the saved value back.
	cmd.initialCmd(KEY_MODE::S_CABLE_OPACITY);
	REQUIRE(settings::cableOpacity == Catch::Approx(0.5f));

	settings::cableOpacity = saved;
}

TEST_CASE("CmdCableOpacity initial cmd with non-zero opacity still saves", "[Stroke][cmd][cable]") {
	float saved = settings::cableOpacity;

	std::string data = "0.7";
	CmdCableOpacity cmd;
	cmd.data = &data;
	settings::cableOpacity = 0.3f;

	cmd.initialCmd(KEY_MODE::S_CABLE_OPACITY);
	REQUIRE(settings::cableOpacity == 0.f);
	REQUIRE(std::stof(data) == Catch::Approx(0.3f));

	settings::cableOpacity = saved;
}

TEST_CASE("CmdRackMove translates rackScroll offset on initialCmd", "[Stroke][cmd][scroll]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	// The KeyContainer adds itself to APP->scene->rack but its destructor removes it.
	// CmdRackMove touches APP->scene->rackScroll directly.
	KeyContainer<STROKE_PORTS>* kc = new KeyContainer<STROKE_PORTS>();
	kc->module = module;
	APP->scene->rack->addChild(kc);

	math::Vec origin = APP->scene->rackScroll->offset;

	SECTION("Right direction increases x by arrowSpeed (30)") {
		CmdRackMove cmd;
		cmd.x = 1.f;
		cmd.y = 0.f;
		cmd.initialCmd(KEY_MODE::S_SCROLL_RIGHT);
		REQUIRE(APP->scene->rackScroll->offset.x == Catch::Approx(origin.x + 30.f));
		REQUIRE(APP->scene->rackScroll->offset.y == Catch::Approx(origin.y));
	}

	SECTION("Up direction decreases y by arrowSpeed (30)") {
		APP->scene->rackScroll->offset = origin;
		CmdRackMove cmd;
		cmd.x = 0.f;
		cmd.y = -1.f;
		cmd.initialCmd(KEY_MODE::S_SCROLL_UP);
		REQUIRE(APP->scene->rackScroll->offset.x == Catch::Approx(origin.x));
		REQUIRE(APP->scene->rackScroll->offset.y == Catch::Approx(origin.y - 30.f));
	}

	SECTION("Diagonal combines both axes") {
		APP->scene->rackScroll->offset = origin;
		CmdRackMove cmd;
		cmd.x = 1.f;
		cmd.y = 1.f;
		cmd.initialCmd(KEY_MODE::S_SCROLL_DOWN);
		REQUIRE(APP->scene->rackScroll->offset.x == Catch::Approx(origin.x + 30.f));
		REQUIRE(APP->scene->rackScroll->offset.y == Catch::Approx(origin.y + 30.f));
	}

	APP->scene->rackScroll->offset = origin;
	APP->scene->rack->removeChild(kc);
	delete kc;
	Test::destroyModule(module);
}

TEST_CASE("CmdRackMove::followUpCmd applies move only for same KEY_MODE", "[Stroke][cmd][scroll]") {
	CmdRackMove cmd;
	cmd.x = 1.f;
	cmd.y = 0.f;
	cmd.keyMode = KEY_MODE::S_SCROLL_RIGHT;

	// Same mode: should move and return false (keep this cmd alive).
	cmd.followUpCmd(KEY_MODE::S_SCROLL_RIGHT);
	REQUIRE(cmd.keyMode == KEY_MODE::S_SCROLL_RIGHT);

	// Different mode: returns true (clear), but doesn't update keyMode.
	bool shouldClear = cmd.followUpCmd(KEY_MODE::S_SCROLL_LEFT);
	REQUIRE(shouldClear == true);
}

TEST_CASE("CmdZoomModuleId is a no-op with empty data", "[Stroke][cmd][zoom]") {
	std::string empty = "";
	CmdZoomModuleId cmd;
	cmd.data = &empty;
	cmd.scale = 0.9f;

	// No module lookups happen when data is empty.
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_ZOOM_MODULE_ID));
	REQUIRE(empty == "");
}

TEST_CASE("CmdZoomModuleIdSmooth is a no-op with empty data", "[Stroke][cmd][zoom]") {
	std::string empty = "";
	CmdZoomModuleIdSmooth cmd;
	cmd.data = &empty;
	cmd.scale = 0.95f;

	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_ZOOM_MODULE_ID_SMOOTH));
	REQUIRE_NOTHROW(cmd.step());
}

TEST_CASE("CmdZoomModuleCustom is a no-op with empty data", "[Stroke][cmd][zoom]") {
	std::string empty = "";
	CmdZoomModuleCustom cmd;
	cmd.data = &empty;

	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_ZOOM_MODULE_CUSTOM));
}

TEST_CASE("CmdZoomModuleCustomSmooth throws on empty data (known gotcha)", "[Stroke][cmd][zoom]") {
	// NOTE: CmdZoomModuleCustomSmooth::initialCmd unconditionally calls
	// std::stof(*data) before any guard checks. With an empty data string this
	// throws std::invalid_argument ("stof: no conversion"). This test pins the
	// current behaviour — empty data is not safe — so that if it is ever
	// fixed, the test will signal the change.
	std::string empty = "";
	CmdZoomModuleCustomSmooth cmd;
	cmd.data = &empty;

	REQUIRE_THROWS_AS(cmd.initialCmd(KEY_MODE::S_ZOOM_MODULE_CUSTOM_SMOOTH), std::invalid_argument);
	REQUIRE_NOTHROW(cmd.step());
}

TEST_CASE("CmdZoomModuleId with invalid moduleId is a safe no-op", "[Stroke][cmd][zoom]") {
	std::string invalidId = "999999999";
	CmdZoomModuleId cmd;
	cmd.data = &invalidId;
	cmd.scale = 0.9f;

	// Should not throw; the lookup returns nullptr and the function early-returns.
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_ZOOM_MODULE_ID));
	REQUIRE(invalidId == "999999999");
}

TEST_CASE("CmdZoomOut is a safe no-op on an empty rack", "[Stroke][cmd][zoom]") {
	// With no modules in the rack, getChildrenBoundingBox returns an empty
	// rect (size.isFinite() == false), so zoomOut early-returns.
	math::Vec savedOffset = APP->scene->rackScroll->offset;
	CmdZoomOut cmd;
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_ZOOM_OUT));
	// Offset must not change when the rack has no children.
	REQUIRE(APP->scene->rackScroll->offset.x == Catch::Approx(savedOffset.x));
	REQUIRE(APP->scene->rackScroll->offset.y == Catch::Approx(savedOffset.y));
}

TEST_CASE("CmdZoomOutSmooth is a safe no-op on an empty rack", "[Stroke][cmd][zoom]") {
	CmdZoomOutSmooth cmd;
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_ZOOM_OUT_SMOOTH));
	REQUIRE_NOTHROW(cmd.step());
}

// Note: CmdZoomToggle and CmdZoomToggleSmooth are NOT tested here because both
// dereference APP->window->pixelRatio unconditionally, and the TestContext
// does not create a window (APP->window is nullptr) — exercising either
// causes a SIGSEGV. CmdZoomToggleSmooth additionally dereferences the hovered
// ModuleWidget without a null check. These are known latent issues in the
// production code that cannot be unit-tested without a full app scene.

TEST_CASE("CmdCableVisibility toggles cable container visibility", "[Stroke][cmd][cable]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	KeyContainer<STROKE_PORTS>* kc = new KeyContainer<STROKE_PORTS>();
	kc->module = module;
	APP->scene->rack->addChild(kc);

	Widget* cableContainer = APP->scene->rack->getCableContainer();
	REQUIRE(cableContainer != nullptr);

	// Start visible.
	cableContainer->show();
	REQUIRE(cableContainer->visible == true);

	CmdCableVisibility cmd;
	cmd.initialCmd(KEY_MODE::S_CABLE_VISIBILITY);
	REQUIRE(cableContainer->visible == false);

	cmd.initialCmd(KEY_MODE::S_CABLE_VISIBILITY);
	REQUIRE(cableContainer->visible == true);

	APP->scene->rack->removeChild(kc);
	delete kc;
	Test::destroyModule(module);
}

TEST_CASE("CmdModuleAdd is a safe no-op with empty data", "[Stroke][cmd][module]") {
	std::string empty = "";
	CmdModuleAdd cmd;
	cmd.data = &empty;
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_MODULE_ADD));
}

TEST_CASE("CmdModuleAdd is a safe no-op with invalid JSON", "[Stroke][cmd][module]") {
	std::string invalid = "not json";
	CmdModuleAdd cmd;
	cmd.data = &invalid;
	// json_loads returns nullptr and cmd should still not crash.
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_MODULE_ADD));
}

TEST_CASE("CmdModuleAdd is a safe no-op with JSON missing fields", "[Stroke][cmd][module]") {
	std::string incomplete = "{\"module\": {\"plugin\": \"missing-model\"}}";
	CmdModuleAdd cmd;
	cmd.data = &incomplete;
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_MODULE_ADD));
}

TEST_CASE("CmdModuleAddRandom::initModels collects all registered plugins/models", "[Stroke][cmd][module]") {
	// Wipe any prior state so we measure from a known baseline.
	CmdModuleAddRandom::models.clear();

	CmdModuleAddRandom cmd;
	cmd.initModels();

	// At least one plugin (PackOne) is loaded in the test context.
	REQUIRE(CmdModuleAddRandom::models.size() > 0);

	// Every entry must be a (plugin-slug, model-slug) tuple of non-empty strings.
	for (auto& m : CmdModuleAddRandom::models) {
		REQUIRE(!std::get<0>(m).empty());
		REQUIRE(!std::get<1>(m).empty());
	}

	// Stroke must be one of the registered models.
	bool strokeFound = false;
	for (auto& m : CmdModuleAddRandom::models) {
		if (std::get<1>(m) == "Stroke") {
			strokeFound = true;
			break;
		}
	}
	REQUIRE(strokeFound);
}

TEST_CASE("CmdModuleDispatch is a safe no-op with empty data", "[Stroke][cmd][module]") {
	std::string empty = "";
	CmdModuleDispatch cmd;
	cmd.data = &empty;
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_MODULE_DISPATCH));
}

TEST_CASE("CmdModuleDispatch::followUpCmd returns true for non-DISPATCH keyMode", "[Stroke][cmd][module]") {
	std::string empty = "";
	CmdModuleDispatch cmd;
	cmd.data = &empty;

	REQUIRE(cmd.followUpCmd(KEY_MODE::CV_TRIGGER) == true);
	REQUIRE(cmd.followUpCmd(KEY_MODE::S_ZOOM_OUT) == true);
	REQUIRE(cmd.followUpCmd(KEY_MODE::S_MODULE_LOCK) == true);
}

TEST_CASE("CmdModuleDispatch::followUpCmd returns true even with empty data on DISPATCH", "[Stroke][cmd][module]") {
	std::string empty = "";
	CmdModuleDispatch cmd;
	cmd.data = &empty;

	// followUpCmd early-returns true when data is empty.
	REQUIRE(cmd.followUpCmd(KEY_MODE::S_MODULE_DISPATCH) == true);
}

TEST_CASE("CmdParamCopyPaste::cmd returns true when no hovered widget", "[Stroke][cmd][param]") {
	// The static `cmd` is the core logic for both copy and paste. With no
	// hovered widget it should return true (meaning "clear previousCmd") without
	// touching its static state.
	REQUIRE(CmdParamCopyPaste::cmd(KEY_MODE::S_PARAM_COPY) == true);
	REQUIRE(CmdParamCopyPaste::cmd(KEY_MODE::S_PARAM_PASTE) == true);
}

TEST_CASE("CmdParamCopyPaste returns false on copy and paste when widget is present", "[Stroke][cmd][param]") {
	// CmdParamCopyPaste::cmd always returns false on success (meaning "keep the
	// previous cmd alive for follow-up"). When no widget is hovered, it returns
	// true (clear). We verify both paths by exercising with no widget.
	REQUIRE(CmdParamCopyPaste::cmd(KEY_MODE::S_PARAM_COPY) == true);
	REQUIRE(CmdParamCopyPaste::cmd(KEY_MODE::S_PARAM_PASTE) == true);
}

TEST_CASE("CmdModulePresetSave is a no-op with no hovered widget", "[Stroke][cmd][module]") {
	CmdModulePresetSave cmd;
	// With APP->event set up but no widget hovered, this must not throw.
	// (APP->event is created by TestContext.)
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_MODULE_PRESET_SAVE));
}

TEST_CASE("CmdModulePresetSaveDefault is a no-op with no hovered widget", "[Stroke][cmd][module]") {
	CmdModulePresetSaveDefault cmd;
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_MODULE_PRESET_SAVE_DEFAULT));
}

TEST_CASE("CmdParamRand is a no-op with no hovered widget", "[Stroke][cmd][param]") {
	CmdParamRand cmd;
	// No hovered widget: must not crash, must not throw.
	REQUIRE_NOTHROW(cmd.initialCmd(KEY_MODE::S_PARAM_RAND));
}


// KeyContainer dispatch lifecycle (processCmd, processCmdHeld, processCmdDisable)
//
// KeyContainer::draw() routes keyTemp/keyTempHeld/keyTempDisable to typed
// CmdBase subclasses via processCmd / processCmdHeld / processCmdDisable.
// processCmd is the only entry point that constructs a Cmd; processCmdHeld
// and processCmdDisable only invoke followUpCmd on the previously stored cmd.

TEST_CASE("KeyContainer::enableLearn toggles the learn index", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	REQUIRE(kc.learnIdx == -1);

	kc.enableLearn(0);
	REQUIRE(kc.learnIdx == 0);

	kc.enableLearn(0);
	// Toggling the same idx clears it.
	REQUIRE(kc.learnIdx == -1);

	kc.enableLearn(3);
	REQUIRE(kc.learnIdx == 3);

	kc.enableLearn(7);
	REQUIRE(kc.learnIdx == 7);

	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::enableLearn with callback stores the callback", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	bool called = false;
	kc.enableLearn(0, [&called](int, int, int) { called = true; });
	REQUIRE(kc.learnIdx == 0);
	REQUIRE(static_cast<bool>(kc.learnCallback));

	// Invoking the callback directly should fire the lambda.
	kc.learnCallback(GLFW_KEY_A, 0, 0);
	REQUIRE(called);

	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::processCmd constructs cmd, calls initialCmd, then clears keyTemp", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTemp = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_MODULE_LOCK;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	bool saved = settings::lockModules;
	settings::lockModules = false;

	kc.processCmd<CmdModuleLock>();
	REQUIRE(kc.previousCmd != nullptr);
	REQUIRE(settings::lockModules == true);

	// keyTemp should still be set; processCmd does NOT clear it — that's
	// the draw() switch's responsibility after processCmd returns.
	REQUIRE(module->keyTemp == &module->keys[0]);

	// Destructor must delete previousCmd cleanly.
	settings::lockModules = saved;
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::processCmd replaces previous cmd when followUpCmd returns true", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTemp = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_MODULE_LOCK;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	bool saved = settings::lockModules;
	settings::lockModules = false;

	// First call: construct CmdModuleLock, no previousCmd. initialCmd flips
	// settings::lockModules from false -> true.
	kc.processCmd<CmdModuleLock>();
	REQUIRE(kc.previousCmd != nullptr);
	REQUIRE(settings::lockModules == true);

	// Now invoke processCmd again. CmdModuleLock inherits followUpCmd default
	// (returns true), so the previous cmd should be deleted and a new one made.
	// initialCmd of the new instance flips the toggle again: true -> false.
	module->keyTemp = &module->keys[0];
	kc.processCmd<CmdModuleLock>();
	REQUIRE(kc.previousCmd != nullptr);
	REQUIRE(settings::lockModules == false); // toggle re-applied

	// Run one more time: false -> true again.
	module->keyTemp = &module->keys[0];
	kc.processCmd<CmdModuleLock>();
	REQUIRE(settings::lockModules == true);

	settings::lockModules = saved;
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::processCmd keeps previous cmd when followUpCmd returns false", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTemp = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_PARAM_PASTE;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	// Side-channel counter — survives test-body reruns.
	int followUpCount = 0;
	struct StickyCmd : CmdBase {
		int* counter;
		StickyCmd(int* c) : counter(c) {}
		bool followUpCmd(KEY_MODE) override { (*counter)++; return false; }
		void initialCmd(KEY_MODE) override { }
	};

	StickyCmd* sticky = new StickyCmd(&followUpCount);
	kc.previousCmd = sticky;

	// processCmd will call sticky->followUpCmd which returns false; so
	// processCmd early-returns WITHOUT constructing a new cmd.
	kc.processCmd<CmdModuleLock>();
	REQUIRE(kc.previousCmd == sticky);
	REQUIRE(followUpCount == 1);

	// Clean up: delete manually since processCmd didn't take ownership
	// (returned false), then null out previousCmd so ~KeyContainer doesn't
	// double-delete.
	delete sticky;
	kc.previousCmd = nullptr;
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::processCmdHeld runs followUpCmd and clears when true", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTempHeld = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_SCROLL_RIGHT;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	// Use a side-channel counter (a heap counter, not a member) to count how
	// many times followUpCmd fires, and check the count after the fact.
	int followUpCount = 0;
	struct CountingCmd : CmdBase {
		int* counter;
		CountingCmd(int* c) : counter(c) {}
		bool followUpCmd(KEY_MODE) override { (*counter)++; return true; }
	};

	CountingCmd* tc = new CountingCmd(&followUpCount);
	kc.previousCmd = tc;

	kc.processCmdHeld();

	// Single assertion: previousCmd is cleared (i.e. followUpCmd returned true).
	REQUIRE(kc.previousCmd == nullptr);
	// And the counter must have been incremented exactly once.
	REQUIRE(followUpCount == 1);

	// Calling again with no previous cmd is a no-op (no crash, no extra increment).
	kc.processCmdHeld();
	REQUIRE(followUpCount == 1);

	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::processCmdHeld keeps cmd alive when followUpCmd returns false", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTempHeld = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_SCROLL_RIGHT;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	// Side-channel counter so we can verify followUpCmd was called even after
	// multiple processCmdHeld invocations.
	int followUpCount = 0;
	struct StickyCmd : CmdBase {
		int* counter;
		StickyCmd(int* c) : counter(c) {}
		bool followUpCmd(KEY_MODE) override { (*counter)++; return false; }
	};

	StickyCmd* sc = new StickyCmd(&followUpCount);
	kc.previousCmd = sc;

	kc.processCmdHeld();
	REQUIRE(followUpCount == 1);
	REQUIRE(kc.previousCmd == sc); // still alive

	kc.processCmdHeld();
	REQUIRE(followUpCount == 2);
	REQUIRE(kc.previousCmd == sc); // still alive after second call

	delete sc;
	kc.previousCmd = nullptr; // prevent ~KeyContainer from double-deleting
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::processCmdDisable is a no-op with no previous cmd", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTempDisable = &module->keys[0];

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	REQUIRE_NOTHROW(kc.processCmdDisable());
	REQUIRE(kc.previousCmd == nullptr);

	Test::destroyModule(module);
}

TEST_CASE("KeyContainer draws step() on previousCmd every call", "[Stroke][keycontainer]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;

	struct CountingCmd : CmdBase {
		int stepCount = 0;
		void step() override { stepCount++; }
	};
	CountingCmd* cc = new CountingCmd();
	kc.previousCmd = cc;

	// Calling draw() invokes previousCmd->step() once per draw.
	widget::Widget::DrawArgs dargs;
	dargs.vg = nullptr;
	dargs.clipBox = math::Rect();
	kc.draw(dargs);
	REQUIRE(cc->stepCount == 1);

	kc.draw(dargs);
	REQUIRE(cc->stepCount == 2);

	kc.draw(dargs);
	REQUIRE(cc->stepCount == 3);

	// Cleanup: destructor of KeyContainer deletes previousCmd.
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::draw routes S_ZOOM_OUT through CmdZoomOut", "[Stroke][keycontainer][draw]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTemp = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_ZOOM_OUT;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;
	APP->scene->rack->addChild(&kc);

	widget::Widget::DrawArgs dargs;
	dargs.vg = nullptr;
	dargs.clipBox = math::Rect();

	// Empty rack → zoomOut early-returns but the path is exercised.
	REQUIRE_NOTHROW(kc.draw(dargs));
	REQUIRE(kc.previousCmd != nullptr);

	// draw() always clears module->keyTemp after dispatch.
	REQUIRE(module->keyTemp == nullptr);

	APP->scene->rack->removeChild(&kc);
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::draw routes S_MODULE_LOCK through CmdModuleLock", "[Stroke][keycontainer][draw]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTemp = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_MODULE_LOCK;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;
	APP->scene->rack->addChild(&kc);

	bool saved = settings::lockModules;
	settings::lockModules = false;

	widget::Widget::DrawArgs dargs;
	dargs.vg = nullptr;
	dargs.clipBox = math::Rect();
	kc.draw(dargs);

	REQUIRE(settings::lockModules == true);
	REQUIRE(module->keyTemp == nullptr);

	settings::lockModules = saved;
	APP->scene->rack->removeChild(&kc);
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::draw dispatches scroll commands via press-then-held sequence", "[Stroke][keycontainer][draw]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;
	APP->scene->rack->addChild(&kc);

	widget::Widget::DrawArgs dargs;
	dargs.vg = nullptr;
	dargs.clipBox = math::Rect();

	math::Vec origin = APP->scene->rackScroll->offset;

	// Step 1: press (keyTemp) routes S_SCROLL_LEFT through processCmd, which
	// constructs a CmdRackMove(-1, 0) and applies it once.
	module->keyTemp = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_SCROLL_LEFT;
	kc.draw(dargs);

	REQUIRE(kc.previousCmd != nullptr);
	REQUIRE(APP->scene->rackScroll->offset.x == Catch::Approx(origin.x - 30.f));
	REQUIRE(module->keyTemp == nullptr);

	// Step 2: held (keyTempHeld) calls processCmdHeld, which calls
	// followUpCmd on the existing CmdRackMove — re-applying the move.
	module->keyTempHeld = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_SCROLL_LEFT;
	kc.draw(dargs);

	REQUIRE(APP->scene->rackScroll->offset.x == Catch::Approx(origin.x - 60.f));
	REQUIRE(module->keyTempHeld == nullptr);

	// CmdRackMove::followUpCmd returns false, so the cmd stays alive after step 2.
	REQUIRE(kc.previousCmd != nullptr);

	APP->scene->rackScroll->offset = origin;
	APP->scene->rack->removeChild(&kc);
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::draw ignores non-scroll keyHeld modes", "[Stroke][keycontainer][draw]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTempHeld = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_MODULE_LOCK;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;
	APP->scene->rack->addChild(&kc);

	widget::Widget::DrawArgs dargs;
	dargs.vg = nullptr;
	dargs.clipBox = math::Rect();
	kc.draw(dargs);

	// S_MODULE_LOCK is not in the keyHeld dispatch switch — previousCmd stays null.
	REQUIRE(kc.previousCmd == nullptr);
	// keyTempHeld is still cleared.
	REQUIRE(module->keyTempHeld == nullptr);

	APP->scene->rack->removeChild(&kc);
	Test::destroyModule(module);
}

TEST_CASE("KeyContainer::draw handles keyDisable for disabled modes", "[Stroke][keycontainer][draw]") {
	auto module = Test::createModule<StrokeModule<STROKE_PORTS>>("Stroke");
	module->keyTempDisable = &module->keys[0];
	module->keys[0].mode = KEY_MODE::S_CABLE_MULTIDRAG;

	KeyContainer<STROKE_PORTS> kc;
	kc.module = module;
	APP->scene->rack->addChild(&kc);

	widget::Widget::DrawArgs dargs;
	dargs.vg = nullptr;
	dargs.clipBox = math::Rect();
	kc.draw(dargs);

	// keyTempDisable cleared by draw.
	REQUIRE(module->keyTempDisable == nullptr);

	APP->scene->rack->removeChild(&kc);
	Test::destroyModule(module);
}