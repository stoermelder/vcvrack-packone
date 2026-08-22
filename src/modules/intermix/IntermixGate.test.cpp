#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "IntermixGate.cpp"

using namespace StoermelderPackOne::Intermix;

SYNC_MODEL(modelIntermix, "Intermix");
SYNC_MODEL(modelIntermixGate, "IntermixGate");
Test::TestContext<> testContext;

// Forward declare Intermix module type for expander tests
template<int PORTS>
struct IntermixModuleMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	
	IntermixModuleMock() {
		config(0, 0, 0, 0);
		// Set model so expander check passes
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

TEST_CASE("Construction and initialization", "[IntermixGate]") {
	IntermixGateModule<8>* m = Test::createModule<IntermixGateModule<8>>("IntermixGate");
	IntermixGateWidget* mw = Test::createWidget<IntermixGateWidget>("IntermixGate");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[IntermixGate][JSON]") {
	auto module = Test::createModule<IntermixGateModule<8>>("IntermixGate");

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

TEST_CASE("JSON round-trip preserves state", "[IntermixGate]") {
	IntermixGateModule<8>* m = Test::createModule<IntermixGateModule<8>>("IntermixGate");
	IntermixGateModule<8>* m2 = Test::createModule<IntermixGateModule<8>>("IntermixGate");

	// Non-default value (default is pluginSettings.panelThemeDefault, usually 0)
	m->panelTheme = 1;

	json_t* j = m->dataToJson();
	// Start m2 at a different value so dataFromJson() is genuinely exercised
	// (otherwise a fresh module's default could mask a broken restore).
	m2->panelTheme = 0;
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->panelTheme == 1);

	Test::destroyModule(m);
	Test::destroyModule(m2);
}


TEST_CASE("Expander connection", "[IntermixGate]") {
	auto gateModule = Test::createModule<IntermixGateModule<8>>("IntermixGate");

	// Should not crash
	gateModule->process(Test::makeProcessArgs(1));
	
	for (int i = 0; i < 8; i++) {
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + i].getVoltage() == 0.f);
	}

	Test::destroyModule(gateModule);
}

TEST_CASE("Gate output generation", "[IntermixGate]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto gateModule = Test::createModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Row with active connections outputs high gate") {
		// Setup mock expander connection
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		// Set matrix values - row 0 has active connections
		intermixModule->currentMatrix[0][0] = 1.0f;
		intermixModule->currentMatrix[0][1] = 0.5f;
		
		// Row 1 has no active connections
		intermixModule->currentMatrix[1][0] = 0.0f;
		intermixModule->currentMatrix[1][1] = 0.0f;
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		// Row 0 should output high (10V)
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 10.f);
		
		// Rows 2-7 should be low (0V)
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 0.f);
	}

	SECTION("Row with no connections outputs low gate") {
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		// All matrix values zero
		for (int i = 0; i < 8; i++) {
			for (int j = 0; j < 8; j++) {
				intermixModule->currentMatrix[i][j] = 0.0f;
			}
		}
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		// All outputs should be low
		for (int i = 0; i < 8; i++) {
			REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + i].getVoltage() == 0.f);
		}
	}

	SECTION("Multiple rows with active connections") {
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		// Multiple rows with active connections
		intermixModule->currentMatrix[0][2] = 0.8f;
		intermixModule->currentMatrix[1][3] = 0.3f;
		intermixModule->currentMatrix[2][4] = 1.0f;
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		// Outputs with connections are high
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 3].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 4].getVoltage() == 10.f);
		
		// Others are low
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 0.f);
	}

	Test::destroyModule(gateModule);
	delete intermixModule;
}

TEST_CASE("Gate logic with varying matrix values", "[IntermixGate]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto gateModule = Test::createModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Small positive values trigger gate") {
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		// Very small but positive value
		intermixModule->currentMatrix[0][0] = 0.001f;
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
	}

	SECTION("Zero values do not trigger gate") {
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		intermixModule->currentMatrix[0][0] = 0.0f;
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}

	SECTION("Any connection in row triggers gate") {
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		// Only one connection in row
		intermixModule->currentMatrix[3][7] = 0.5f;
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 7].getVoltage() == 10.f);
	}

	Test::destroyModule(gateModule);
	delete intermixModule;
}

TEST_CASE("Expander chain with gate module", "[IntermixGate]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto gateModule1 = Test::createModule<IntermixGateModule<8>>("IntermixGate");
	auto gateModule2 = Test::createModule<IntermixGateModule<8>>("IntermixGate");
	Test::SimpleEngine engine;
	engine.registerModules(intermixModule, gateModule1, gateModule2);

	SECTION("Multiple gate expanders can chain") {
		// Setup expander chain: Intermix -> Gate1 -> Gate2
		intermixModule->rightExpander.module = gateModule1;
		gateModule1->leftExpander.module = intermixModule;
		gateModule1->rightExpander.module = gateModule2;
		gateModule2->leftExpander.module = gateModule1;
		
		intermixModule->currentMatrix[0][0] = 0.5f;
		intermixModule->currentMatrix[1][1] = 0.5f;
	
		engine.step();
		engine.step();
		engine.step();
		
		// Both should detect active connections
		REQUIRE(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 10.f);
		REQUIRE(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 10.f);
		REQUIRE(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 0.f);
		REQUIRE(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 0.f);
	}

	Test::destroyModule(gateModule2);
	Test::destroyModule(gateModule1);
	delete intermixModule;
}

TEST_CASE("Gate with dynamic matrix changes", "[IntermixGate]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto gateModule = Test::createModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Gate updates when matrix changes") {
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		// Start with connection
		intermixModule->currentMatrix[0][0] = 1.0f;
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		
		// Remove connection
		intermixModule->currentMatrix[0][0] = 0.0f;
		
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
		
		// Add connection again
		intermixModule->currentMatrix[0][0] = 0.7f;
		
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
	}

	Test::destroyModule(gateModule);
	delete intermixModule;
}

TEST_CASE("All outputs independent", "[IntermixGate]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto gateModule = Test::createModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Each output reflects its own row") {
		intermixModule->rightExpander.module = gateModule;
		gateModule->leftExpander.module = intermixModule;
		
		// Set various patterns
		intermixModule->currentMatrix[0][0] = 1.0f; // Output 0 high
		intermixModule->currentMatrix[1][1] = 0.0f; // Output 1 low
		intermixModule->currentMatrix[2][2] = 0.5f; // Output 2 high
		intermixModule->currentMatrix[3][3] = 0.0f; // Output 3 low
		intermixModule->currentMatrix[4][4] = 0.1f; // Output 4 high
		intermixModule->currentMatrix[5][5] = 0.0f; // Output 5 low
		intermixModule->currentMatrix[6][6] = 0.9f; // Output 6 high
		intermixModule->currentMatrix[7][7] = 0.0f; // Output 7 low
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		gateModule->process(Test::makeProcessArgs(1));
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 3].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 4].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 5].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 6].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 7].getVoltage() == 0.f);
	}

	Test::destroyModule(gateModule);
	delete intermixModule;
}
