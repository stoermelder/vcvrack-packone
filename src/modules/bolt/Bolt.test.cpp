#include "../../test/framework.hpp"
#include "Bolt.cpp"

using namespace StoermelderPackOne::Bolt;

SYNC_MODEL(modelBolt, "Bolt");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Bolt]") {
	Test::ModuleScaffold<BoltModule> mods;
	BoltModule* m = mods.create("Bolt");
	BoltWidget* mw = Test::createWidget<BoltWidget>("Bolt");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Bolt][JSON]") {
	Test::ModuleScaffold<BoltModule> mods;
	auto module = mods.create("Bolt");

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
	
}

TEST_CASE("JSON round-trip preserves module state", "[Bolt]") {
	Test::ModuleScaffold<BoltModule> mods;
	auto module = mods.create("Bolt");

	module->panelTheme = 1;
	module->op = 2;
	module->opCvMode = BOLT_OPCV_MODE_C4;
	module->outCvMode = BOLT_OUTCV_MODE_TRIG_HIGH;
	
	json_t* rootJ = module->dataToJson();
	
	auto moduleNew = mods.create("Bolt");
	moduleNew->dataFromJson(rootJ);
	
	REQUIRE(moduleNew->panelTheme == 1);
	REQUIRE(moduleNew->op == 2);
	REQUIRE(moduleNew->opCvMode == BOLT_OPCV_MODE_C4);
	REQUIRE(moduleNew->outCvMode == BOLT_OUTCV_MODE_TRIG_HIGH);
	
	json_decref(rootJ);
}


TEST_CASE("Processing without connections", "[Bolt]") {
	Test::ModuleScaffold<BoltModule> mods;
	auto module = mods.create("Bolt");

	SECTION("Module processes without crash when no outputs connected") {
		module->op = BOLT_OP_AND;
		
		// Process should not crash
		REQUIRE_NOTHROW(module->process(Test::makeProcessArgs(0)));
	}

}