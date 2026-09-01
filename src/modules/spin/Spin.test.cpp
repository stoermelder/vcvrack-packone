#include "../../test/framework.hpp"
#include "Spin.cpp"

using namespace StoermelderPackOne::Spin;

SYNC_MODEL(modelSpin, "Spin");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Spin]") {
	SpinModule* m = Test::createModule<SpinModule>("Spin");
	SpinWidget* mw = Test::createWidget<SpinWidget>("Spin");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Spin][JSON]") {
	auto module = Test::createModule<SpinModule>("Spin");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves state", "[Spin][JSON]") {
	SpinModule* m = Test::createModule<SpinModule>("Spin");
	SpinModule* m2 = Test::createModule<SpinModule>("Spin");

	// Distinct, non-default values for every scalar stored to JSON
	m->panelTheme = 1;
	m->mods = 0x0004; // GLFW_MOD_ALT, distinct from the default GLFW_MOD_SHIFT
	m->clickMode = CLICK_MODE::TRIGGER;
	m->clickHigh = true;

	json_t* j = m->dataToJson();
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->panelTheme == 1);
	REQUIRE(m2->mods == 0x0004);
	REQUIRE(m2->clickMode == CLICK_MODE::TRIGGER);
	REQUIRE(m2->clickHigh == true);

	Test::destroyModule(m);
	Test::destroyModule(m2);
}