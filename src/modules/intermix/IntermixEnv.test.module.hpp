// INTERMIXENV test cases. Included by IntermixEnv.test.cpp inside namespace __module.
// Not a standalone header: IntermixEnv.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[IntermixEnv]") {
	Test::ModuleScaffold<IntermixEnvModule<8>> mods;
	IntermixEnvModule<8>* m = mods.create("IntermixEnv");
	IntermixEnvWidget* mw = Test::createWidget<IntermixEnvWidget>("IntermixEnv");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[IntermixEnv][JSON]") {
	Test::ModuleScaffold<IntermixEnvModule<8>> mods;
	auto module = mods.create("IntermixEnv");

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

TEST_CASE("JSON round-trip preserves state", "[JSON][IntermixEnv]") {
	Test::ModuleScaffold<IntermixEnvModule<8>> mods;
	IntermixEnvModule<8>* m = mods.create("IntermixEnv");
	IntermixEnvModule<8>* m2 = mods.create("IntermixEnv");

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
}


TEST_CASE("Input selection", "[IntermixEnv]") {
	Test::ModuleScaffold<IntermixEnvModule<8>> mods;
	auto module = mods.create("IntermixEnv");

	SECTION("Input can be changed") {
		module->input = 3;
		REQUIRE(module->input == 3);
		
		module->input = 7;
		REQUIRE(module->input == 7);
	}
}

TEST_CASE("Expander connection", "[IntermixEnv]") {
	Test::Harness h;
	auto envModule = h.addModule<IntermixEnvModule<8>>("IntermixEnv");

	SECTION("Module processes without expander") {
		// Should not crash
		h.dspStep();
		
		for (int i = 0; i < 8; i++) {
			REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + i].getVoltage() == 0.f);
		}
	}
}

TEST_CASE("Envelope output", "[IntermixEnv]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto envModule = h.addModule<IntermixEnvModule<8>>("IntermixEnv");

	SECTION("Outputs envelope for selected input") {
		h.connectExpander(intermixModule, envModule);

		// Set matrix values in Intermix
		intermixModule->currentMatrix[0][0] = 0.5f;
		intermixModule->currentMatrix[0][1] = 0.75f;
		intermixModule->currentMatrix[0][2] = 1.0f;

		// Select input 0
		envModule->input = 0;

		h.dspSteps(2);

		// Outputs should be matrix values * 10V
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(5.0f).margin(0.01f));
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(7.5f).margin(0.01f));
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 2].getVoltage() == Catch::Approx(10.0f).margin(0.01f));
	}

	SECTION("Different input selection changes output") {
		h.connectExpander(intermixModule, envModule);

		intermixModule->currentMatrix[1][0] = 0.3f;
		intermixModule->currentMatrix[1][1] = 0.6f;

		envModule->input = 1;

		h.dspSteps(2);

		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(3.0f).margin(0.01f));
		REQUIRE(envModule->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(6.0f).margin(0.01f));
	}
}

TEST_CASE("Expander chain", "[IntermixEnv]") {
	Test::Harness h;
	auto intermixModule = h.adoptModule(new IntermixModuleMock<8>());
	auto envModule1 = h.addModule<IntermixEnvModule<8>>("IntermixEnv");
	auto envModule2 = h.addModule<IntermixEnvModule<8>>("IntermixEnv");

	SECTION("Multiple expanders can chain") {
		// Setup expander chain: Intermix -> Env1 -> Env2.
		// Goes through the harness so Rack's onExpanderChange fires — IntermixBase overrides it
		// (IntermixBase.hpp:62) and unpublishes/resets on a left-side change, which hand-wiring
		// the pointers skips entirely.
		h.connectChain(intermixModule, envModule1, envModule2);

		intermixModule->currentMatrix[0][0] = 0.8f;
		intermixModule->currentMatrix[1][0] = 0.4f;

		envModule1->input = 0;
		envModule2->input = 1;

		h.dspStep();
		h.dspStep();
		// Process env2 - it will read from env1's producerMessage
		h.dspStep();

		REQUIRE(envModule1->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(8.0f).margin(0.01f));
		REQUIRE(envModule1->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		REQUIRE(envModule1->outputs[IntermixEnvModule<8>::OUTPUT + 2].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		REQUIRE(envModule2->outputs[IntermixEnvModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(4.0f).margin(0.01f));
		REQUIRE(envModule2->outputs[IntermixEnvModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		REQUIRE(envModule2->outputs[IntermixEnvModule<8>::OUTPUT + 3].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
	}
}
