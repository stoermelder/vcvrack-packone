#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Bolt.cpp"

using namespace StoermelderPackOne::Bolt;

static Test::TestContext<> testContext;

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

TEST_CASE("Reset", "[Bolt]") {
	auto module = Test::createModule<BoltModule>("Bolt");

	SECTION("Reset clears state") {
		module->op = 3;
		for (int c = 0; c < 16; c++) {
			module->out[c] = true;
		}
		
        rack::engine::Module::ResetEvent re;
        module->onReset(re);
		
		REQUIRE(module->op == 0);
		for (int c = 0; c < 16; c++) {
			REQUIRE(module->out[c] == false);
		}
	}

	Test::destroyModule(module);
}

TEST_CASE("State array initialization", "[Bolt]") {
	auto module = Test::createModule<BoltModule>("Bolt");

	SECTION("Output array initialized to false") {
		for (int c = 0; c < 16; c++) {
			REQUIRE(module->out[c] == false);
		}
	}
	
	SECTION("Output array can be modified") {
		module->out[0] = true;
		module->out[5] = true;
		module->out[15] = true;
		
		REQUIRE(module->out[0] == true);
		REQUIRE(module->out[5] == true);
		REQUIRE(module->out[15] == true);
		REQUIRE(module->out[1] == false);
	}

	Test::destroyModule(module);
}

TEST_CASE("Configuration persistence", "[Bolt]") {
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
