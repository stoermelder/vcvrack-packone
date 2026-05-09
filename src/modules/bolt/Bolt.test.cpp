#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Bolt.cpp"

using namespace StoermelderPackOne::Bolt;

SYNC_MODEL(modelBolt, "Bolt");
Test::TestContext<> testContext;

TEST_CASE("JSON serialization", "[Bolt]") {
	auto module = Test::createModule<BoltModule>("Bolt");

	SECTION("State persists after serialization") {
		module->panelTheme = 1;
		module->op = 2;
		module->opCvMode = BOLT_OPCV_MODE_C4;
		module->outCvMode = BOLT_OUTCV_MODE_TRIG_HIGH;
		
		json_t* rootJ = module->dataToJson();
		
		auto moduleNew = Test::createModule<BoltModule>("Bolt");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->panelTheme == 1);
		REQUIRE(moduleNew->op == 2);
		REQUIRE(moduleNew->opCvMode == BOLT_OPCV_MODE_C4);
		REQUIRE(moduleNew->outCvMode == BOLT_OUTCV_MODE_TRIG_HIGH);
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}

TEST_CASE("Configuration persistence", "[JSON][Bolt]") {
	auto module = Test::createModule<BoltModule>("Bolt");

	SECTION("CV modes persist through JSON") {
		module->opCvMode = BOLT_OPCV_MODE_C4;
		module->outCvMode = BOLT_OUTCV_MODE_TRIG_CHANGE;
		
		json_t* rootJ = module->dataToJson();
		
		auto moduleNew = Test::createModule<BoltModule>("Bolt");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->opCvMode == BOLT_OPCV_MODE_C4);
		REQUIRE(moduleNew->outCvMode == BOLT_OUTCV_MODE_TRIG_CHANGE);
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}

TEST_CASE("Processing without connections", "[Bolt]") {
	auto module = Test::createModule<BoltModule>("Bolt");

	SECTION("Module processes without crash when no outputs connected") {
		module->op = BOLT_OP_AND;
		
		// Process should not crash
		REQUIRE_NOTHROW(module->process(Test::makeProcessArgs(0)));
	}

	Test::destroyModule(module);
}

TEST_CASE("Widget construction", "[UI][Bolt]") {
	BoltWidget* w = Test::createWidget<BoltWidget>("Bolt");
	REQUIRE(w != nullptr);
	REQUIRE(w->module == NULL);
	
	Test::destroyWidget(w);
}
