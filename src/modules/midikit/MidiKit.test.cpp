#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"

using namespace StoermelderPackOne::MidiKit;
using namespace StoermelderPackOne::MidiScript;

SYNC_MODEL(modelMidiKit, "MidiKit");
Test::TestContext<> testContext;

// Minimal Elk script header (body can be empty — the Elk engine still loads it)
static constexpr const char* ELK_SCRIPT =
	"/**\n"
	" * @engine Elk\n"
	" */\n";

// Minimal Lua script (synchronously loaded, no body needed)
static constexpr const char* LUA_SCRIPT =
	"--[[\n"
	"@engine Lua\n"
	"--]]\n";


TEST_CASE("MidiKit: construction and initialization", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 4);
	REQUIRE(m->NUM_INPUTS == 5);   // 4 voltage + 1 trigger
	REQUIRE(m->NUM_OUTPUTS == 1);  // trigger out
	REQUIRE(m->NUM_LIGHTS == 0);
	REQUIRE(m->script == "");
	REQUIRE(m->sample == 0);
	REQUIRE(m->inputTriggerTick == 0);

	Test::destroyModule(m);
}


TEST_CASE("Preset JSON null-guards", "[MidiKit][JSON]") {
	auto module = Test::createModule<MidiKitModule>("MidiKit");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


TEST_CASE("MidiKit: process() does not crash with no script", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	for (int i = 0; i < 20; i++) {
		REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(i + 1)));
	}

	REQUIRE(m->sample == 20);

	Test::destroyModule(m);
}

TEST_CASE("MidiKit: default engine is Elk", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}

TEST_CASE("MidiKit: @engine Lua header selects Lua engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);

	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	Test::destroyModule(m);
}

TEST_CASE("MidiKit: Elk header keeps Elk engine active", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// First switch to Lua, then switch back via an Elk-tagged script
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	m->loadScript(ELK_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}

TEST_CASE("MidiKit: clearScript resets to empty and restores Elk engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	m->clearScript();

	REQUIRE(m->script == "");
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}

TEST_CASE("MidiKit: trigger input increments inputTriggerTick", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Prime the SchmittTrigger to LOW state before the first rising edge
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));

	// Rising edge → tick increments
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->inputTriggerTick == 1);

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(2));
	REQUIRE(m->inputTriggerTick == 1);  // no change on falling edge

	// Second pulse
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(3));
	REQUIRE(m->inputTriggerTick == 2);

	Test::destroyModule(m);
}

TEST_CASE("MidiKit: JSON round-trip preserves panelTheme and script", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->panelTheme = 2;
	m->loadScript(LUA_SCRIPT);

	json_t* j = m->dataToJson();

	m->panelTheme = 0;
	m->clearScript();
	REQUIRE(m->script == "");

	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->script == LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	Test::destroyModule(m);
}

TEST_CASE("MidiKit: process() does not crash with Lua script loaded", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);

	for (int i = 0; i < 20; i++) {
		REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(i + 1)));
	}

	Test::destroyModule(m);
}
