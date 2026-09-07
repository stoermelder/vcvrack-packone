#include "../../test/framework.hpp"

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

TEST_CASE("Construction and initialization", "[IntermixGate]") {
	Test::ModuleScaffold<IntermixGateModule<8>> mods;
	IntermixGateModule<8>* m = mods.create("IntermixGate");
	IntermixGateWidget* mw = Test::createWidget<IntermixGateWidget>("IntermixGate");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[IntermixGate][JSON]") {
	Test::ModuleScaffold<IntermixGateModule<8>> mods;
	auto module = mods.create("IntermixGate");

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

TEST_CASE("JSON round-trip preserves state", "[IntermixGate]") {
	Test::ModuleScaffold<IntermixGateModule<8>> mods;
	IntermixGateModule<8>* m = mods.create("IntermixGate");
	IntermixGateModule<8>* m2 = mods.create("IntermixGate");

	// Non-default value (default is pluginSettings.panelThemeDefault, usually 0)
	m->panelTheme = 1;

	json_t* j = m->dataToJson();
	// Start m2 at a different value so dataFromJson() is genuinely exercised
	// (otherwise a fresh module's default could mask a broken restore).
	m2->panelTheme = 0;
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->panelTheme == 1);
}


TEST_CASE("Expander connection", "[IntermixGate]") {
	Test::Harness h;
	auto gateModule = h.addModule<IntermixGateModule<8>>("IntermixGate");

	// Should not crash
	h.dspStep();
	
	for (int i = 0; i < 8; i++) {
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + i].getVoltage() == 0.f);
	}
}

TEST_CASE("Gate output generation", "[IntermixGate]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto gateModule = h.addModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Row with active connections outputs high gate") {
		h.connectExpander(intermixModule, gateModule);

		// Set matrix values - row 0 has active connections
		intermixModule->currentMatrix[0][0] = 1.0f;
		intermixModule->currentMatrix[0][1] = 0.5f;
		
		// Row 1 has no active connections
		intermixModule->currentMatrix[1][0] = 0.0f;
		intermixModule->currentMatrix[1][1] = 0.0f;
		
		h.dspSteps(2);
		
		// Row 0 should output high (10V)
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 10.f);
		
		// Rows 2-7 should be low (0V)
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 0.f);
	}

	SECTION("Row with no connections outputs low gate") {
		h.connectExpander(intermixModule, gateModule);

		// All matrix values zero
		for (int i = 0; i < 8; i++) {
			for (int j = 0; j < 8; j++) {
				intermixModule->currentMatrix[i][j] = 0.0f;
			}
		}
		
		h.dspSteps(2);
		
		// All outputs should be low
		for (int i = 0; i < 8; i++) {
			REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + i].getVoltage() == 0.f);
		}
	}

	SECTION("Multiple rows with active connections") {
		h.connectExpander(intermixModule, gateModule);

		// Multiple rows with active connections
		intermixModule->currentMatrix[0][2] = 0.8f;
		intermixModule->currentMatrix[1][3] = 0.3f;
		intermixModule->currentMatrix[2][4] = 1.0f;
		
		h.dspSteps(2);
		
		// Outputs with connections are high
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 3].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 4].getVoltage() == 10.f);
		
		// Others are low
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 0.f);
	}
}

TEST_CASE("Gate logic with varying matrix values", "[IntermixGate]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto gateModule = h.addModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Small positive values trigger gate") {
		h.connectExpander(intermixModule, gateModule);

		// Very small but positive value
		intermixModule->currentMatrix[0][0] = 0.001f;
		
		h.dspSteps(2);
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
	}

	SECTION("Zero values do not trigger gate") {
		h.connectExpander(intermixModule, gateModule);

		intermixModule->currentMatrix[0][0] = 0.0f;
		
		h.dspSteps(2);
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}

	SECTION("Any connection in row triggers gate") {
		h.connectExpander(intermixModule, gateModule);

		// Only one connection in row
		intermixModule->currentMatrix[3][7] = 0.5f;
		
		h.dspSteps(2);
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 7].getVoltage() == 10.f);
	}
}

TEST_CASE("Expander chain with gate module", "[IntermixGate]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto gateModule1 = h.addModule<IntermixGateModule<8>>("IntermixGate");
	auto gateModule2 = h.addModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Multiple gate expanders can chain") {
		// Setup expander chain: Intermix -> Gate1 -> Gate2
		h.connectChain(intermixModule, gateModule1, gateModule2);

		intermixModule->currentMatrix[0][0] = 0.5f;
		intermixModule->currentMatrix[1][1] = 0.5f;

		h.dspStep();
		h.dspStep();
		h.dspStep();

		// Both should detect active connections
		REQUIRE(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 10.f);
		REQUIRE(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 10.f);
		REQUIRE(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 0.f);
		REQUIRE(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 0.f);
	}
}

TEST_CASE("Gate with dynamic matrix changes", "[IntermixGate]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto gateModule = h.addModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Gate updates when matrix changes") {
		h.connectExpander(intermixModule, gateModule);

		// Start with connection
		intermixModule->currentMatrix[0][0] = 1.0f;
		
		h.dspSteps(2);
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		
		// Remove connection
		intermixModule->currentMatrix[0][0] = 0.0f;
		
		h.dspSteps(2);
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
		
		// Add connection again
		intermixModule->currentMatrix[0][0] = 0.7f;
		
		h.dspSteps(2);
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
	}
}

TEST_CASE("All outputs independent", "[IntermixGate]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto gateModule = h.addModule<IntermixGateModule<8>>("IntermixGate");

	SECTION("Each output reflects its own row") {
		h.connectExpander(intermixModule, gateModule);

		// Set various patterns
		intermixModule->currentMatrix[0][0] = 1.0f; // Output 0 high
		intermixModule->currentMatrix[1][1] = 0.0f; // Output 1 low
		intermixModule->currentMatrix[2][2] = 0.5f; // Output 2 high
		intermixModule->currentMatrix[3][3] = 0.0f; // Output 3 low
		intermixModule->currentMatrix[4][4] = 0.1f; // Output 4 high
		intermixModule->currentMatrix[5][5] = 0.0f; // Output 5 low
		intermixModule->currentMatrix[6][6] = 0.9f; // Output 6 high
		intermixModule->currentMatrix[7][7] = 0.0f; // Output 7 low
		
		h.dspSteps(2);
		
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 1].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 2].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 3].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 4].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 5].getVoltage() == 0.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 6].getVoltage() == 10.f);
		REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 7].getVoltage() == 0.f);
	}
}

TEST_CASE("Chain member removal invalidates forwarded expander messages", "[IntermixGate]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto gateModule1 = h.addModule<IntermixGateModule<8>>("IntermixGate");
	auto gateModule2 = h.addModule<IntermixGateModule<8>>("IntermixGate");

	// Chain: MockHead -> Gate1 -> Gate2; both gates forward the head pointer
	h.connectChain(intermixModule, gateModule1, gateModule2);

	intermixModule->currentMatrix[0][0] = 0.5f;
	h.dspStep();

	// Both gates now publish a forwarded head pointer to their right neighbor.
	// h.dspStep() flips every stepped module's own messageFlipRequested immediately
	// (matching Rack's per-engine-step flip), so what gateModule1/gateModule2 just
	// published is already in consumerMessage by the time dspStep() returns — not
	// producerMessage, which is where the hand-rolled version (never flipping the
	// last chain member's own right side) used to find it.
	REQUIRE(gateModule1->rightExpander.consumerMessage != nullptr);
	REQUIRE(gateModule2->rightExpander.consumerMessage != nullptr);
	// Row 0 of the matrix is active, so both gates drove their output high
	REQUIRE(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);
	REQUIRE(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);

	SECTION("Removal notification makes survivors drop forwarded messages") {
		// IntermixChainModule::onRemove() notifies the surviving members.
		StoermelderPackOne::notifyModuleListeners("Intermix");

		// Both gates consume the notification: they unpublish, reset their
		// outputs and skip the sample instead of dereferencing a possibly
		// dangling pointer.
		h.dspStep();

		CHECK(gateModule1->rightExpander.producerMessage == nullptr);
		CHECK(gateModule1->rightExpander.consumerMessage == nullptr);
		CHECK(gateModule2->rightExpander.producerMessage == nullptr);
		CHECK(gateModule2->rightExpander.consumerMessage == nullptr);
		CHECK(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
		CHECK(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}

	SECTION("onRemove() of a middle member unpublishes and notifies survivors") {
		Module::RemoveEvent e;
		gateModule1->onRemove(e);

		CHECK(gateModule1->rightExpander.producerMessage == nullptr);
		CHECK(gateModule1->rightExpander.consumerMessage == nullptr);

		// gateModule2 received the notification: drops its forwarded copy
		// and resets its outputs.
		h.dspStep();
		CHECK(gateModule2->rightExpander.producerMessage == nullptr);
		CHECK(gateModule2->rightExpander.consumerMessage == nullptr);
		CHECK(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}

	SECTION("Disconnected expander resets outputs down the chain") {
		// The engine clears the adjacency of the survivor and dispatches
		// ExpanderChangeEvent when the head is removed or moved away.
		h.disconnectExpander(gateModule1, Test::Harness::SIDE_LEFT);

		h.dspStep();

		// Both gates went low; gate2 dropped the no-longer-refreshed message
		CHECK(gateModule1->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
		CHECK(gateModule2->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}
}

TEST_CASE("ExpanderChangeEvent invalidates forwarded messages", "[IntermixGate]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto gateModule = h.addModule<IntermixGateModule<8>>("IntermixGate");

	h.connectExpander(intermixModule, gateModule);

	intermixModule->currentMatrix[0][0] = 0.5f;
	h.dspSteps(2);

	REQUIRE(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 10.f);

	// The engine dispatches this when the left neighbor is removed, replaced
	// or the rack is rearranged.
	h.disconnectExpander(gateModule, Test::Harness::SIDE_LEFT);

	// Forwarded message dropped and outputs reset immediately at event time
	CHECK(gateModule->rightExpander.producerMessage == nullptr);
	CHECK(gateModule->rightExpander.consumerMessage == nullptr);
	CHECK(gateModule->outputs[IntermixGateModule<8>::OUTPUT + 0].getVoltage() == 0.f);
}