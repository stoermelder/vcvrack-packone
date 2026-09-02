#include "../../test/framework.hpp"
#include "IntermixEnv.cpp"

using namespace StoermelderPackOne::Intermix;

SYNC_MODEL(modelIntermix, "Intermix");
SYNC_MODEL(modelIntermixEnv, "IntermixEnv");
Test::TestContext<> testContext;

// Forward declare Intermix module type for expander tests
template<int PORTS>
struct IntermixModuleMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	
	IntermixModuleMock() {
		config(0, 0, 0, 0);
		// Set model so expander check passes (isIntermixModel(), IntermixBase.hpp) — relies on
		// SYNC_MODEL(modelIntermix, "Intermix") above actually having landed; a missing/wrong
		// sync would make that check silently fail instead of erroring here.
		Test::requireModelSync(modelIntermix, "Intermix");
		model = modelIntermix;
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				currentMatrix[i][j] = 0.f;
			}
		}
	}
	
	typename IntermixBase<PORTS>::IntermixMatrix expGetCurrentMatrix() override {
		return currentMatrix;
	}
	
	int expGetChannelCount() override { return 1; }
	void expSetFade(int i, float* fadeIn, float* fadeOut) override { }
	
	void process(const ProcessArgs& args) override {
		rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
		rightExpander.messageFlipRequested = true;
	}
};


TEST_CASE("Construction and initialization", "[IntermixEnv]") {
	IntermixEnvModule<8>* m = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");
	IntermixEnvWidget* mw = Test::createWidget<IntermixEnvWidget>("IntermixEnv");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[IntermixEnv][JSON]") {
	auto module = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");

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

TEST_CASE("JSON round-trip preserves state", "[JSON][IntermixEnv]") {
	IntermixEnvModule<8>* m = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");
	IntermixEnvModule<8>* m2 = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");

	m->panelTheme = 1;
	m->input = 5;

	json_t* j = m->dataToJson();
	// Start m2 at a different value so dataFromJson() is genuinely exercised
	m2->panelTheme = 0;
	m2->input = 0;
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->panelTheme == 1);
	REQUIRE(m2->input == 5);

	Test::destroyModule(m);
	Test::destroyModule(m2);
}


TEST_CASE("Input selection", "[IntermixEnv]") {
	auto module = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");

	SECTION("Input can be changed") {
		module->input = 3;
		REQUIRE(module->input == 3);
		
		module->input = 7;
		REQUIRE(module->input == 7);
	}

	Test::destroyModule(module);
}

TEST_CASE("Expander connection", "[IntermixEnv]") {
	auto envModule = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");

	SECTION("Module processes without expander") {
		// Should not crash
		envModule->process(Test::makeProcessArgs(1));
		
		for (int i = 0; i < 8; i++) {
			REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + i].getVoltage() == 0.f);
		}
	}

	Test::destroyModule(envModule);
}

TEST_CASE("Envelope output", "[IntermixEnv]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto envModule = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");

	SECTION("Outputs envelope for selected input") {
		// Setup mock expander connection
		intermixModule->rightExpander.module = envModule;
		envModule->leftExpander.module = intermixModule;
		
		// Set matrix values in Intermix
		intermixModule->currentMatrix[0][0] = 0.5f;
		intermixModule->currentMatrix[0][1] = 0.75f;
		intermixModule->currentMatrix[0][2] = 1.0f;
		
		// Select input 0
		envModule->input = 0;
		
		// Process intermix to set producer message
		intermixModule->process(Test::makeProcessArgs(1));
		
		// Manually flip messages: EnvModule reads from leftExpander.module->rightExpander.consumerMessage
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		
		// Now process env module
		envModule->process(Test::makeProcessArgs(1));
		
		// Outputs should be matrix values * 10V
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(5.0f).margin(0.01f));
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(7.5f).margin(0.01f));
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 2].getVoltage() == Catch::Approx(10.0f).margin(0.01f));
	}

	SECTION("Different input selection changes output") {
		intermixModule->rightExpander.module = envModule;
		envModule->leftExpander.module = intermixModule;
		
		intermixModule->currentMatrix[1][0] = 0.3f;
		intermixModule->currentMatrix[1][1] = 0.6f;
		
		envModule->input = 1;
		
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		envModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(3.0f).margin(0.01f));
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(6.0f).margin(0.01f));
	}

	Test::destroyModule(envModule);
	delete intermixModule;
}

TEST_CASE("Expander chain", "[IntermixEnv]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto envModule1 = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");
	auto envModule2 = Test::createModule<IntermixEnvModule<8>>("IntermixEnv");
	Test::SimpleEngine engine;
	engine.addModules(intermixModule, envModule1, envModule2);

	SECTION("Multiple expanders can chain") {
		// Setup expander chain: Intermix -> Env1 -> Env2
		intermixModule->rightExpander.module = envModule1;
		envModule1->leftExpander.module = intermixModule;
		envModule1->rightExpander.module = envModule2;
		envModule2->leftExpander.module = envModule1;
		
		intermixModule->currentMatrix[0][0] = 0.8f;
		intermixModule->currentMatrix[1][0] = 0.4f;
		
		envModule1->input = 0;
		envModule2->input = 1;
		
		engine.step();
		engine.step();
		// Process env2 - it will read from env1's producerMessage
		engine.step();
		
		REQUIRE(envModule1->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(8.0f).margin(0.01f));
		REQUIRE(envModule1->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		REQUIRE(envModule1->outputs[IntermixEnvModule<8>::OUTPUT + 2].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		REQUIRE(envModule2->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(4.0f).margin(0.01f));
		REQUIRE(envModule2->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		REQUIRE(envModule2->outputs[IntermixEnvModule<8>::OUTPUT + 3].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
	}

	Test::destroyModule(envModule2);
	Test::destroyModule(envModule1);
	delete intermixModule;
}