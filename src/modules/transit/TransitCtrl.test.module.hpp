static const int NUM_CTRL = 16;

// ---------------------------------------------------------------------------
// MockSenderModule — a minimal Module that also implements TransitCtrlMaster.
// Using a real Module gives ParamQuantity::getParam() a valid module+paramId
// pair, which is required by the polling code in TransitCtrlModule::process().
// No engine registration is needed: we pass the pointer directly.
// ---------------------------------------------------------------------------
struct MockSenderModule : rack::Module, TransitCtrlMaster {
	struct Change { int index; float value; };
	std::vector<Change> changes;

	MockSenderModule() {
		config(NUM_CTRL, 0, 0, 0);
		for (int i = 0; i < NUM_CTRL; i++)
			configParam(i, 0.f, 1.f, 0.5f, string::f("Mock %d", i + 1));
	}
	void process(const ProcessArgs&) override {}

	int getCtrlParamCount() override { return NUM_CTRL; }
	ParamQuantity* getCtrlParamQuantity(int index) override {
		return (index >= 0 && index < NUM_CTRL) ? paramQuantities[index] : nullptr;
	}
	void pushCtrlChange(int index, float value) override {
		changes.push_back({index, value});
	}
};

// ---------------------------------------------------------------------------
// Helper module with bound test parameters for integration tests
// ---------------------------------------------------------------------------
struct CtrlTestModule : rack::Module {
	enum ParamIds { PARAM_A, PARAM_B, NUM_PARAMS };
	CtrlTestModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(PARAM_A, 0.f, 1.f, 0.5f, "A");
		configParam(PARAM_B, 0.f, 10.f, 5.f, "B");
	}
};

// ===========================================================================
// Construction
// ===========================================================================

TEST_CASE("Construction and initialization", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	REQUIRE(ctrl != nullptr);

	SECTION("All mappings are -1 (unmapped) after construction") {
		for (int i = 0; i < NUM_CTRL; i++) {
			REQUIRE(ctrl->mapping[i] == -1);
		}
	}

	SECTION("All reverseMap entries are -1 after construction") {
		for (int i = 0; i < NUM_CTRL; i++) {
			REQUIRE(ctrl->reverseMap[i] == -1);
		}
	}

	SECTION("All ppqs are non-null") {
		for (int i = 0; i < NUM_CTRL; i++) {
			REQUIRE(ctrl->ppqs[i] != nullptr);
		}
	}

	SECTION("No transitCtrl set") {
		for (int i = 0; i < NUM_CTRL; i++) {
			REQUIRE(ctrl->ppqs[i]->transitCtrl == nullptr);
		}
	}
}


TEST_CASE("Preset JSON null-guards", "[TransitCtrl][JSON]") {
	Test::Harness h;
	auto module = h.addModule<TransitCtrlModule<16>>("TransitCtrl");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}
}


// ===========================================================================
// setMapping — mapping[], reverseMap[], handleIndex consistency
// ===========================================================================

TEST_CASE("setMapping updates mapping, reverseMap, and handleIndex", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	MockSenderModule* sender = h.adoptModule(new MockSenderModule());
	ctrl->setTransitCtrl(sender);

	SECTION("setMapping(3, 7) records forward and reverse entries") {
		ctrl->setMapping(3, 7);
		REQUIRE(ctrl->mapping[3] == 7);
		REQUIRE(ctrl->reverseMap[7] == 3);
		REQUIRE(ctrl->ppqs[3]->handleIndex == 7);
	}

	SECTION("Remapping a knob clears the old reverseMap entry") {
		ctrl->setMapping(3, 7);
		ctrl->setMapping(3, 5);
		REQUIRE(ctrl->reverseMap[7] == -1);
		REQUIRE(ctrl->reverseMap[5] == 3);
		REQUIRE(ctrl->mapping[3] == 5);
	}

	SECTION("setMapping(k, -1) marks knob as unmapped and clears reverseMap") {
		ctrl->setMapping(3, 7);
		ctrl->setMapping(3, -1);
		REQUIRE(ctrl->mapping[3] == -1);
		REQUIRE(ctrl->reverseMap[7] == -1);
		REQUIRE(ctrl->ppqs[3]->handleIndex == -1);
	}

	SECTION("setMapping syncs knob param value and baseline from target") {
		sender->params[7].setValue(0.3f);
		ctrl->setMapping(3, 7);
		REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 3].getValue() == Catch::Approx(0.3f));
		REQUIRE(ctrl->lastParamValues[3] == Catch::Approx(0.3f));
	}

	SECTION("Different knobs can map to different Transit params independently") {
		ctrl->setMapping(0, 2);
		ctrl->setMapping(1, 5);
		REQUIRE(ctrl->reverseMap[2] == 0);
		REQUIRE(ctrl->reverseMap[5] == 1);
	}

	ctrl->setTransitCtrl(nullptr);
}


// ===========================================================================
// setCtrlParamValue — O(1) reverseMap lookup
// ===========================================================================

TEST_CASE("setCtrlParamValue uses reverseMap for O(1) lookup", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	MockSenderModule* sender = h.adoptModule(new MockSenderModule());
	ctrl->setTransitCtrl(sender);
	ctrl->setMapping(3, 7);

	SECTION("Transit index 7 updates knob 3 param value and baseline") {
		ctrl->setCtrlParamValue(7, 0.8f);
		REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 3].getValue() == Catch::Approx(0.8f));
		REQUIRE(ctrl->lastParamValues[3] == Catch::Approx(0.8f));
	}

	SECTION("Unmapped Transit index leaves all knobs unchanged") {
		float before = ctrl->params[TransitCtrlModule<16>::PARAM + 0].getValue();
		ctrl->setCtrlParamValue(0, 0.99f);  // reverseMap[0] == -1
		REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 0].getValue() == Catch::Approx(before));
	}

	SECTION("Out-of-range Transit index is ignored") {
		float before = ctrl->params[TransitCtrlModule<16>::PARAM + 0].getValue();
		ctrl->setCtrlParamValue(-1, 0.5f);
		ctrl->setCtrlParamValue(NUM_CTRL, 0.5f);
		REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 0].getValue() == Catch::Approx(before));
	}

	ctrl->setTransitCtrl(nullptr);
}


// ===========================================================================
// process() — change detection and forwarding
// ===========================================================================

TEST_CASE("process() forwards knob changes to Transit using handleIndex", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	MockSenderModule* sender = h.adoptModule(new MockSenderModule());
	ctrl->setTransitCtrl(sender);
	// After setMapping(3, 7): lastParamValues[3] = 0.5f (synced from sender's param 7)
	ctrl->setMapping(3, 7);

	SECTION("Changing knob 3 pushes Transit-side index 7") {
		ctrl->params[TransitCtrlModule<16>::PARAM + 3].setValue(0.9f);
		h.dspSteps(500);
		REQUIRE(sender->changes.size() == 1);
		REQUIRE(sender->changes[0].index == 7);
		REQUIRE(sender->changes[0].value == Catch::Approx(0.9f));
	}

	SECTION("Unchanged knob does not push") {
		h.dspStep();
		REQUIRE(sender->changes.empty());
	}

	SECTION("Unmapped knob does not push even when changed") {
		// knob 0: mapping[0] == -1, handleIndex == -1
		ctrl->params[TransitCtrlModule<16>::PARAM + 0].setValue(0.9f);
		h.dspStep();
		REQUIRE(sender->changes.empty());
	}

	ctrl->setTransitCtrl(nullptr);
}


// ===========================================================================
// Oscillation prevention
// ===========================================================================

TEST_CASE("No oscillation: Transit write does not trigger re-push", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	MockSenderModule* sender = h.adoptModule(new MockSenderModule());
	ctrl->setTransitCtrl(sender);
	ctrl->setMapping(3, 7);

	// Simulate Transit mirroring a fade value — updates both raw value and baseline
	ctrl->setCtrlParamValue(7, 0.8f);

	// process() sees no delta so must not push anything back
	h.dspStep();
	REQUIRE(sender->changes.empty());

	ctrl->setTransitCtrl(nullptr);
}


// ===========================================================================
// Target sync polling
// ===========================================================================

TEST_CASE("Target sync polling detects external target changes", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	MockSenderModule* sender = h.adoptModule(new MockSenderModule());
	ctrl->setTransitCtrl(sender);
	ctrl->setMapping(3, 7);
	// After setMapping: lastParamValues[3] = sender->params[7].value = 0.5f

	// External code changes the target parameter directly (not via Transit or TransitCtrl)
	sender->params[7].setValue(0.2f);

	// Prime the divider so it fires on the very next process() call
	ctrl->targetSyncDivider.clock = ctrl->targetSyncDivider.division - 1;
	h.dspStep();

	SECTION("Knob value is synced to the new target value") {
		REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 3].getValue() == Catch::Approx(0.2f));
	}

	SECTION("Baseline is updated to match") {
		REQUIRE(ctrl->lastParamValues[3] == Catch::Approx(0.2f));
	}

	SECTION("No pushCtrlChange is emitted (oscillation prevented)") {
		REQUIRE(sender->changes.empty());
	}

	ctrl->setTransitCtrl(nullptr);
}

TEST_CASE("Target sync polling is silent when target has not changed", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	MockSenderModule* sender = h.adoptModule(new MockSenderModule());
	ctrl->setTransitCtrl(sender);
	ctrl->setMapping(3, 7);
	// target and baseline are both 0.5f — no delta

	ctrl->targetSyncDivider.clock = ctrl->targetSyncDivider.division - 1;
	h.dspStep();

	REQUIRE(sender->changes.empty());
	REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 3].getValue() == Catch::Approx(0.5f));

	ctrl->setTransitCtrl(nullptr);
}


// ===========================================================================
// setTransitCtrl
// ===========================================================================

TEST_CASE("setTransitCtrl wires ppqs and syncs initial values", "[TransitCtrl]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");

	SECTION("Before any connection, all ppqs have null transitCtrl") {
		for (int i = 0; i < NUM_CTRL; i++) {
			REQUIRE(ctrl->ppqs[i]->transitCtrl == nullptr);
		}
	}

	SECTION("After setTransitCtrl, all ppqs reference the sender") {
		MockSenderModule* sender = h.adoptModule(new MockSenderModule());
		ctrl->setTransitCtrl(sender);
		for (int i = 0; i < NUM_CTRL; i++) {
			REQUIRE(ctrl->ppqs[i]->transitCtrl == sender);
		}
		ctrl->setTransitCtrl(nullptr);
	}

	SECTION("setTransitCtrl syncs mapped knob values from targets") {
		MockSenderModule* sender = h.adoptModule(new MockSenderModule());
		ctrl->setMapping(5, 2);  // knob 5 → Transit param 2
		sender->params[2].setValue(0.7f);
		ctrl->setTransitCtrl(sender);
		REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 5].getValue() == Catch::Approx(0.7f));
		REQUIRE(ctrl->lastParamValues[5] == Catch::Approx(0.7f));
		ctrl->setTransitCtrl(nullptr);
	}

	SECTION("setTransitCtrl(nullptr) clears all transitCtrl pointers") {
		MockSenderModule* sender = h.adoptModule(new MockSenderModule());
		ctrl->setTransitCtrl(sender);
		ctrl->setTransitCtrl(nullptr);
		for (int i = 0; i < NUM_CTRL; i++) {
			REQUIRE(ctrl->ppqs[i]->transitCtrl == nullptr);
		}
	}
}


// ===========================================================================
// JSON serialization
// ===========================================================================

TEST_CASE("JSON serialization round-trip preserves mapping", "[TransitCtrl][JSON]") {
	Test::Harness h;
	TransitCtrlModule<16>* ctrl1 = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	MockSenderModule* sender = h.adoptModule(new MockSenderModule());
	ctrl1->setTransitCtrl(sender);
	ctrl1->setMapping(0, 15);
	ctrl1->setMapping(3, 7);
	ctrl1->setMapping(5, 2);
	ctrl1->setTransitCtrl(nullptr);

	json_t* rootJ = ctrl1->dataToJson();
	REQUIRE(rootJ != nullptr);

	TransitCtrlModule<16>* ctrl2 = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	ctrl2->dataFromJson(rootJ);

	SECTION("Forward mappings are preserved") {
		REQUIRE(ctrl2->mapping[0] == 15);
		REQUIRE(ctrl2->mapping[3] == 7);
		REQUIRE(ctrl2->mapping[5] == 2);
	}

	SECTION("Unmapped knobs remain -1") {
		REQUIRE(ctrl2->mapping[1] == -1);
		REQUIRE(ctrl2->mapping[2] == -1);
		REQUIRE(ctrl2->mapping[4] == -1);
	}

	SECTION("reverseMap is rebuilt correctly from the loaded mapping") {
		REQUIRE(ctrl2->reverseMap[15] == 0);
		REQUIRE(ctrl2->reverseMap[7] == 3);
		REQUIRE(ctrl2->reverseMap[2] == 5);
		REQUIRE(ctrl2->reverseMap[0] == -1);
	}

	SECTION("handleIndex is restored from the loaded mapping") {
		REQUIRE(ctrl2->ppqs[0]->handleIndex == 15);
		REQUIRE(ctrl2->ppqs[3]->handleIndex == 7);
		REQUIRE(ctrl2->ppqs[5]->handleIndex == 2);
	}

	json_decref(rootJ);
}


// ===========================================================================
// Integration — Transit ↔ TransitCtrl via expander connection
// ===========================================================================

TEST_CASE("Integration - Transit discovers TransitCtrl as immediate right expander", "[TransitCtrl]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");

	SECTION("Before connection, ctrl has no transitCtrl") {
		REQUIRE(ctrl->ppqs[0]->transitCtrl == nullptr);
	}

	SECTION("After connection, Transit injects its own pointer into ctrl") {
		h.connectExpander(transit, ctrl);
		h.dspStep(); // Transit re-scans and calls ctrl->setTransitCtrl(transit)
		REQUIRE(ctrl->ppqs[0]->transitCtrl == transit);
	}

	SECTION("After disconnection, transitCtrl is cleared") {
		h.connectExpander(transit, ctrl);
		h.dspStep(); // Transit re-scans and calls ctrl->setTransitCtrl(transit)
		h.disconnectExpander(transit, Test::Harness::SIDE_RIGHT);
		h.dspStep(); // Transit re-scans (finds nothing)
		REQUIRE(ctrl->ppqs[0]->transitCtrl == nullptr);
	}
}


// ---------------------------------------------------------------------------
// Helper: create a TransitEx module via the plugin factory (mirrors the helper
// in TransitEx.test.cpp). Returns the raw Module* pointer alongside a
// TransitBase<12>* view if requested.
// ---------------------------------------------------------------------------
static Module* createExModule(TransitBase<12>** baseOut = nullptr) {
	Model* model = pluginInstance->getModel("TransitEx");
	REQUIRE(model != nullptr);
	Module* m = model->createModule();
	m->id = Test::getModuleId();

	Module::SampleRateChangeEvent e;
	e.sampleRate = Test::sampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);

	if (baseOut) {
		*baseOut = dynamic_cast<TransitBase<12>*>(m);
		REQUIRE(*baseOut != nullptr);
	}
	return m;
}

// Helper: wire the chain [Transit] [TransitEx] [TransitCtrl] and trigger
// the listener callbacks so Transit re-scans and picks up the ctrl.
static void connectExThenCtrl(Test::Harness& h, TransitModule<12>* transit, Module* exModule, TransitCtrlModule<16>* ctrl) {
	h.connectChain(transit, exModule, ctrl);
	h.dspStep();           // Transit re-scans: walks TransitEx, then discovers TransitCtrl
}


TEST_CASE("Integration - Transit discovers TransitCtrl placed after TransitEx", "[TransitCtrl]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	TransitBase<12>* exBase = nullptr;
	Module* exModule = h.addModule<Module>(std::function<Module*()>([&]{ return createExModule(&exBase); }));

	SECTION("Before connection, ctrl has no transitCtrl") {
		REQUIRE(ctrl->ppqs[0]->transitCtrl == nullptr);
	}

	SECTION("After connecting [Transit] [TransitEx] [TransitCtrl], Transit discovers the ctrl") {
		connectExThenCtrl(h, transit, exModule, ctrl);
		REQUIRE(ctrl->ppqs[0]->transitCtrl == transit);
	}

	SECTION("TransitEx is still discovered and counted in presetTotal") {
		connectExThenCtrl(h, transit, exModule, ctrl);
		REQUIRE(transit->presetTotal == 24);
		REQUIRE(exBase->ctrlOffset == 1);
	}

	SECTION("Removing TransitEx but keeping TransitCtrl re-binds ctrl to Transit directly") {
		connectExThenCtrl(h, transit, exModule, ctrl);
		REQUIRE(ctrl->ppqs[0]->transitCtrl == transit);

		// Disconnect TransitEx: re-wire transit.rightExpander -> ctrl directly
		h.disconnectExpander(exModule, Test::Harness::SIDE_LEFT);
		h.disconnectExpander(exModule, Test::Harness::SIDE_RIGHT);
		h.connectExpander(transit, ctrl);
		h.dspStep();

		REQUIRE(transit->presetTotal == 12);
		REQUIRE(ctrl->ppqs[0]->transitCtrl == transit);
	}

	SECTION("Removing TransitCtrl from the [Transit][TransitEx][TransitCtrl] chain clears transitCtrl") {
		connectExThenCtrl(h, transit, exModule, ctrl);
		REQUIRE(ctrl->ppqs[0]->transitCtrl == transit);

		// Disconnect TransitCtrl only
		h.disconnectExpander(exModule, Test::Harness::SIDE_RIGHT);
		h.dspStep();

		REQUIRE(ctrl->ppqs[0]->transitCtrl == nullptr);
		REQUIRE(transit->presetTotal == 24);
	}
}


TEST_CASE("Integration - knob change propagates to mapped target parameter", "[TransitCtrl]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	CtrlTestModule* testMod = h.adoptModule(new CtrlTestModule);

	transit->bindAddParameterRequest(testMod->id, CtrlTestModule::PARAM_A);
	transit->taskProcessorDsp.process();

	h.connectExpander(transit, ctrl);
	h.dspStep(); // Transit re-scans and calls ctrl->setTransitCtrl(transit)
	ctrl->setMapping(0, 0);

	transit->params[TransitModule<12>::PARAM_FADE].setValue(0.f);
	h.dspSteps(512);

	// Move knob 0 to 0.8
	ctrl->params[TransitCtrlModule<16>::PARAM + 0].setValue(0.8f);
	h.dspSteps(512);	  // ctrl pushes change; transit drains queue and applies to target

	REQUIRE(testMod->params[CtrlTestModule::PARAM_A].getValue() == Catch::Approx(0.8f).margin(0.01f));
}


TEST_CASE("Integration - Transit fade mirrors value into TransitCtrl knob", "[TransitCtrl]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitCtrlModule<16>* ctrl = h.addModule<TransitCtrlModule<16>>("TransitCtrl");
	CtrlTestModule* testMod = h.adoptModule(new CtrlTestModule);

	transit->bindAddParameterRequest(testMod->id, CtrlTestModule::PARAM_A);
	transit->taskProcessorDsp.process();

	h.connectExpander(transit, ctrl);
	h.dspStep(); // Transit re-scans and calls ctrl->setTransitCtrl(transit)
	ctrl->setMapping(0, 0);

	testMod->params[CtrlTestModule::PARAM_A].setValue(1.0f);
	transit->presetSave(0);
	testMod->params[CtrlTestModule::PARAM_A].setValue(0.0f);
	transit->presetSave(1);
	// Large fade value ensures the transition completes within the first divider fire
	transit->slot[1].setFadeTime(1000.f);
	h.dspSteps(512);

	// Reset current value to 0 so fade goes from 0 → 1 (non-trivial crossfade)
	testMod->params[CtrlTestModule::PARAM_A].setValue(0.0f);
	transit->presetLoad(0);

	// 100 steps covers the first presetProcessDivider fire (division=64),
	// at which point the fade completes and calls setCtrlParamValue(0, 1.0f)
	h.dspSteps(Test::sampleRate());

	// Transit writes the fade result to PARAM_A and mirrors it to ctrl via setCtrlParamValue
	REQUIRE(ctrl->params[TransitCtrlModule<16>::PARAM + 0].getValue() == Catch::Approx(1.0f).margin(0.01f));

	// The mirror write must not have re-queued a change back to Transit
	REQUIRE(transit->ctrlChangeQueue.empty());
}