// TRANSIT test cases. Included by Transit.test.cpp inside namespace __module.
// Not a standalone header: Transit.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* m = h.addModule<TransitModule<12>>("Transit");
	TransitWidget<12>* mw = Test::createWidget<TransitWidget<12>>("Transit");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Transit][JSON]") {
	Test::Harness h;
	auto module = h.addModule<TransitModule<12>>("Transit");

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

TEST_CASE("JSON round-trip preserves state", "[Transit][JSON]") {
	Test::Harness h;
	TransitModule<12>* m = h.addModule<TransitModule<12>>("Transit");

	// Distinctive label on EVERY slot
	for (int i = 0; i < 12; i++) {
		m->textLabel[i] = "Slot" + std::to_string(i);
	}
	// Slot 0: fully configured; slot 1: label only, no color, not used
	m->presetSlotUsed[0] = true;
	m->fadeTime[0] = 0.35f;
	m->slotColorSet[0] = true;
	m->slotColor[0] = nvgRGBf(1.f, 0.5f, 0.f);
	// qualified: TransitModule declares `int preset` (active slot), shadowing the base array
	m->TransitBase<12>::preset[0] = {0.25f, 0.75f};
	// Slot 5: used with a longer preset vector and a second color
	m->presetSlotUsed[5] = true;
	m->fadeTime[5] = 1.5f;
	m->slotColorSet[5] = true;
	m->slotColor[5] = nvgRGBf(0.f, 1.f, 0.f);
	m->TransitBase<12>::preset[5] = {0.f, 0.5f, 1.f};

	json_t* j = m->dataToJson();

	TransitModule<12>* m2 = h.addModule<TransitModule<12>>("Transit");
	m2->dataFromJson(j);
	json_decref(j);

	SECTION("All slot labels") {
		for (int i = 0; i < 12; i++) {
			REQUIRE(m2->textLabel[i] == "Slot" + std::to_string(i));
		}
	}

	SECTION("Fully configured slot 0") {
		REQUIRE(m2->presetSlotUsed[0] == true);
		REQUIRE(m2->fadeTime[0] == Catch::Approx(0.35f));
		REQUIRE(m2->slotColorSet[0] == true);
		REQUIRE(m2->slotColor[0].r == Catch::Approx(1.f).margin(0.01));
		REQUIRE(m2->slotColor[0].g == Catch::Approx(0.5f).margin(0.01));
		REQUIRE(m2->slotColor[0].b == Catch::Approx(0.f).margin(0.01));
		REQUIRE(m2->TransitBase<12>::preset[0].size() == 2);
		REQUIRE(m2->TransitBase<12>::preset[0][0] == Catch::Approx(0.25f));
		REQUIRE(m2->TransitBase<12>::preset[0][1] == Catch::Approx(0.75f));
	}

	SECTION("Second configured slot 5") {
		REQUIRE(m2->presetSlotUsed[5] == true);
		REQUIRE(m2->fadeTime[5] == Catch::Approx(1.5f));
		REQUIRE(m2->slotColorSet[5] == true);
		REQUIRE(m2->slotColor[5].r == Catch::Approx(0.f).margin(0.01));
		REQUIRE(m2->slotColor[5].g == Catch::Approx(1.f).margin(0.01));
		REQUIRE(m2->TransitBase<12>::preset[5].size() == 3);
		REQUIRE(m2->TransitBase<12>::preset[5][2] == Catch::Approx(1.f));
	}

	SECTION("Unused slot has no color") {
		REQUIRE(m2->textLabel[1] == "Slot1");
		REQUIRE(m2->slotColorSet[1] == false);
	}
}

TEST_CASE("JSON serialization preserves boundaries", "[JSON][Transit]") {
	Test::Harness h;
	TransitModule<12>* module1 = h.addModule<TransitModule<12>>("Transit");
	// Set custom boundaries
	module1->presetSetFirst(2);
	module1->presetSetLast(9);
	module1->preset = 5;

	// Serialize
	json_t* rootJ = module1->dataToJson();

	// Create new module and deserialize
	TransitModule<12>* module2 = h.addModule<TransitModule<12>>("Transit");
	module2->dataFromJson(rootJ);

	// Check values preserved
	REQUIRE(module2->presetFirst == 2);
	REQUIRE(module2->presetLast == 9);
	REQUIRE(module2->preset == 5);

	json_decref(rootJ);
}


// Helper module with test parameters
struct TestModule : rack::Module {
	enum ParamIds {
		TEST_PARAM_1,
		TEST_PARAM_2,
		TEST_PARAM_3,
		NUM_PARAMS
	};

	TestModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(TEST_PARAM_1, 0.f, 1.f, 0.5f, "Test Parameter 1");
		configParam(TEST_PARAM_2, 0.f, 10.f, 5.f, "Test Parameter 2");
		configParam(TEST_PARAM_3, -5.f, 5.f, 0.f, "Test Parameter 3");
	}
};

struct TestSwitchModule : rack::Module {
	enum ParamIds {
		TEST_SWITCH,
		NUM_PARAMS
	};

	TestSwitchModule() {
		config(NUM_PARAMS, 0, 0, 0);
		// isSwitch additionally requires getMaxValue() != 1.f, so this needs three
		// or more positions.
		configSwitch(TEST_SWITCH, 0.f, 2.f, 0.f, "Test Switch", {"A", "B", "C"});
	}
};

TEST_CASE("Setting presetFirst and presetLast boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");

	SECTION("presetSetFirst updates boundary correctly") {
		module->presetSetFirst(3);
		REQUIRE(module->presetFirst == 3);
		REQUIRE(module->presetLast == 12); // Last unchanged
	}

	SECTION("presetSetFirst constrains to presetLast") {
		module->presetSetLast(8);
		module->presetSetFirst(10); // Beyond last
		REQUIRE(module->presetFirst == 7); // Clamped to presetLast - 1
	}

	SECTION("presetSetFirst moves current preset if needed") {
		module->preset = 2;
		module->presetSetFirst(5);
		REQUIRE(module->preset == 5); // Moved up to new minimum
	}

	SECTION("presetSetLast updates boundary correctly") {
		module->presetSetLast(8);
		REQUIRE(module->presetLast == 8);
		REQUIRE(module->presetFirst == 0); // First unchanged
	}

	SECTION("presetSetLast constrains to presetFirst") {
		module->presetSetFirst(5);
		module->presetSetLast(3); // Below first
		REQUIRE(module->presetLast == 5); // Clamped to presetFirst
	}

	SECTION("presetSetLast moves current preset if needed") {
		module->presetFirst = 0;
		module->preset = 10;
		module->presetSetLast(8);
		REQUIRE(module->preset == 7); // Moved down to new maximum - 1
	}
}


TEST_CASE("Comprehensive boundary edge cases", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	
	SECTION("presetFirst == presetLast - 1 (single slot)") {
		module->presetSetFirst(5);
		module->presetSetLast(6);
		REQUIRE(module->presetFirst == 5);
		REQUIRE(module->presetLast == 6);
		
		// Load valid slot 5
		module->presetLoad(5);
		REQUIRE(module->preset == 5);
		
		// Try to load slot 4 (before range)
		int presetBefore = module->preset;
		module->presetLoad(4);
		REQUIRE(module->preset == presetBefore); // Unchanged
		
		// Try to load slot 6 (at or after range)
		module->presetLoad(6);
		REQUIRE(module->preset == presetBefore); // Still unchanged
	}

	SECTION("Setting boundaries in different orders") {
		// Set last first, then first
		module->presetSetLast(7);
		module->presetSetFirst(2);
		REQUIRE(module->presetFirst == 2);
		REQUIRE(module->presetLast == 7);
		
		// Set first first, then last
		module->presetSetFirst(4);
		module->presetSetLast(10);
		REQUIRE(module->presetFirst == 4);
		REQUIRE(module->presetLast == 10);
	}

	SECTION("Boundary at 0 and total") {
		module->presetSetFirst(0);
		module->presetSetLast(12);
		REQUIRE(module->presetFirst == 0);
		REQUIRE(module->presetLast == 12);
	}
}


TEST_CASE("presetLoad respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	// Set up a mapped parameter
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	// Save some presets
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.0f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.5f);
	module->presetSave(5);
	testModule->params[TestModule::TEST_PARAM_1].setValue(1.0f);
	module->presetSave(10);

	SECTION("presetLoad rejects index below presetFirst") {
		module->presetLoad(5); // Load a valid preset first
		int presetBefore = module->preset;
		module->presetSetFirst(3);
		module->presetLoad(2); // Below boundary
		REQUIRE(module->preset == presetBefore); // Unchanged
	}

	SECTION("presetLoad rejects index at or above presetLast") {
		module->presetLoad(5); // Load a valid preset first
		module->presetSetLast(8); // This will adjust preset if needed
		int presetBefore = module->preset; // Now capture the adjusted value
		module->presetLoad(8); // At boundary
		REQUIRE(module->preset == presetBefore); // Unchanged
		module->presetLoad(10); // Above boundary
		REQUIRE(module->preset == presetBefore); // Unchanged
	}

	SECTION("presetLoad accepts index within boundaries") {
		module->presetSetFirst(3);
		module->presetSetLast(8);
		module->presetLoad(5);
		REQUIRE(module->preset == 5); // Loaded successfully
	}
}


TEST_CASE("Multiple bound parameters save and load correctly", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	// Bind all three parameters
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_2);
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_3);
	module->taskProcessorDsp.process();
	h.dspStep();

	// Save distinct multi-parameter snapshots
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.1f);
	testModule->params[TestModule::TEST_PARAM_2].setValue(3.0f);
	testModule->params[TestModule::TEST_PARAM_3].setValue(-2.0f);
	module->presetSave(0);

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.9f);
	testModule->params[TestModule::TEST_PARAM_2].setValue(9.0f);
	testModule->params[TestModule::TEST_PARAM_3].setValue(4.0f);
	module->presetSave(1);

	SECTION("Loading a preset restores all bound parameter values") {
		// Zero fade for instant transition
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);

		module->presetLoad(0);
		h.dspSteps(1000);

		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.1f).margin(0.01f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_2].getValue() == Catch::Approx(3.0f).margin(0.05f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_3].getValue() == Catch::Approx(-2.0f).margin(0.05f));
	}

	SECTION("Switching between presets updates all parameters") {
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);

		module->presetLoad(0);
		h.dspSteps(1000);

		module->presetLoad(1);
		h.dspSteps(1000);

		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.9f).margin(0.01f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_2].getValue() == Catch::Approx(9.0f).margin(0.05f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_3].getValue() == Catch::Approx(4.0f).margin(0.05f));
	}

	SECTION("Preset stores the correct number of values per slot") {
		auto slot0 = module->getSlot(0);
		REQUIRE(slot0->isUsed());
		REQUIRE(slot0->getPreset()->size() == 3);
	}
}


TEST_CASE("presetClear resets active preset selection", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.5f);
	module->presetSave(3);
	module->presetSave(5);
	module->presetLoad(3);

	SECTION("Clearing the active preset sets preset to -1") {
		REQUIRE(module->preset == 3);
		module->presetClear(3);
		REQUIRE(module->preset == -1);
	}

	SECTION("Clearing an inactive preset does not change active preset") {
		module->presetLoad(3);
		module->presetClear(5); // clear a different slot
		REQUIRE(module->preset == 3);
	}

	SECTION("Cleared slot is no longer marked as used") {
		module->presetClear(5);
		REQUIRE(!module->getSlot(5)->isUsed());
		REQUIRE(module->getSlot(5)->getPreset()->empty());
	}
}


TEST_CASE("presetCopyPaste copies values correctly", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_2);
	module->taskProcessorDsp.process();
	h.dspStep();

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.3f);
	testModule->params[TestModule::TEST_PARAM_2].setValue(7.0f);
	module->presetSave(2);

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.9f);
	testModule->params[TestModule::TEST_PARAM_2].setValue(2.0f);
	module->presetSave(5);

	SECTION("Copying slot 2 to slot 7 produces identical values") {
		module->presetCopyPaste(2, 7);

		auto slot7 = module->getSlot(7);
		REQUIRE(slot7->isUsed());
		REQUIRE((*slot7->getPreset())[0] == Catch::Approx(0.3f).margin(0.001f));
		REQUIRE((*slot7->getPreset())[1] == Catch::Approx(7.0f).margin(0.001f));
	}

	SECTION("Copy does not modify the source slot") {
		module->presetCopyPaste(2, 7);

		auto slot2 = module->getSlot(2);
		REQUIRE(slot2->isUsed());
		REQUIRE((*slot2->getPreset())[0] == Catch::Approx(0.3f).margin(0.001f));
	}

	SECTION("Copy over an existing slot overwrites it") {
		module->presetCopyPaste(2, 5);

		auto slot5 = module->getSlot(5);
		REQUIRE((*slot5->getPreset())[0] == Catch::Approx(0.3f).margin(0.001f));
		REQUIRE((*slot5->getPreset())[1] == Catch::Approx(7.0f).margin(0.001f));
	}

	SECTION("Copying an empty slot does not mark target as used") {
		// Slot 9 is empty
		bool wasUsed = module->getSlot(9)->isUsed();
		REQUIRE(!wasUsed);
		module->presetCopyPaste(9, 3); // empty source
		// Nothing should happen - target stays as-is
		REQUIRE(!module->getSlot(3)->isUsed());
	}
}


TEST_CASE("presetShiftFront respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	// Create presets with distinct values
	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i * 0.1f);
		module->presetSave(i);
	}

	module->presetSetFirst(3);
	module->presetSetLast(9);

	SECTION("Shift front affects presets correctly") {
		// Initially, get slot 5's value
		auto slot5 = module->getSlot(5);
		float originalValue5 = slot5->isUsed() ? (*slot5->getPreset())[0] : -1.0f;
		REQUIRE(originalValue5 == Catch::Approx(0.5f).margin(0.01f));
		
		// Shift front from slot 5
		module->presetShiftFrontRequest(5);
		module->taskProcessorDsp.process();
		h.dspStep();
		
		// Preset 4 should now have the value that was in 5
		auto slot4 = module->getSlot(4);
		float newValue4 = slot4->isUsed() ? (*slot4->getPreset())[0] : -1.0f;
		REQUIRE(newValue4 == Catch::Approx(0.5f).margin(0.01f));
	}
}


TEST_CASE("presetShiftBack shifts presets correctly", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	// Save distinct presets at slots 3, 4, 5
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.3f);
	module->presetSave(3);
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.4f);
	module->presetSave(4);
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.5f);
	module->presetSave(5);

	SECTION("ShiftBack from slot 4 moves contents of 4 and 5 to 5 and 6, clears 4") {
		module->presetShiftBack(4);

		// Slot 4 should be cleared
		REQUIRE(!module->getSlot(4)->isUsed());

		// Slot 5 should now have the value that was in slot 4 (0.4)
		REQUIRE(module->getSlot(5)->isUsed());
		REQUIRE((*module->getSlot(5)->getPreset())[0] == Catch::Approx(0.4f).margin(0.001f));

		// Slot 6 should now have the value that was in slot 5 (0.5)
		REQUIRE(module->getSlot(6)->isUsed());
		REQUIRE((*module->getSlot(6)->getPreset())[0] == Catch::Approx(0.5f).margin(0.001f));

		// Slot 3 is unaffected
		REQUIRE(module->getSlot(3)->isUsed());
		REQUIRE((*module->getSlot(3)->getPreset())[0] == Catch::Approx(0.3f).margin(0.001f));
	}
}


TEST_CASE("AUTO mode captures current values into previous preset", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	// Save initial presets
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.2f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.8f);
	module->presetSave(1);

	// Switch to AUTO mode and load preset 0
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::AUTO);
	module->presetLoad(0);
	h.dspStep();

	SECTION("Switching presets auto-saves current param value into previous preset") {
		// Param is now being transitioned towards 0.2. Let it settle fully.
		// With a short fade: set fade=0 (minimum) to get instant application
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		h.dspSteps(1000);
		// Param value should now be 0.2 (from preset 0)
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.2f).margin(0.01f));

		// Manually change the param value (simulating user editing)
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.6f);

		// Load preset 1 - AUTO mode should save 0.6 into preset 0 first
		module->presetLoad(1);
		h.dspStep();

		// Preset 0 should now store 0.6
		auto slot0 = module->getSlot(0);
		REQUIRE(slot0->isUsed());
		REQUIRE((*slot0->getPreset())[0] == Catch::Approx(0.6f).margin(0.01f));
	}
}


TEST_CASE("Per-slot fade time overrides global fade parameter", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.0f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(1.0f);
	module->presetSave(1);

	// Set global fade to maximum (slow)
	module->params[TransitModule<12>::PARAM_FADE].setValue(1.0f);

	SECTION("Slot with fade time of 0 transitions instantly") {
		// Global fade stays at maximum (slow, set above); only the slot override is 0.
		// If the global (slow) fade were used instead, the transition would still be
		// well below 1.0 after 100 frames (see the sibling section below).
		module->getSlot(1)->setFadeTime(0.0f);

		// First, fully settle at preset 0 (value 0.0) using the fast per-slot override
		module->getSlot(0)->setFadeTime(0.0f);
		module->presetLoad(0);
		h.dspSteps(512);
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));

		// Now load preset 1: slot fade=0 must win over the slow global fade
		module->presetLoad(1);
		h.dspSteps(512);
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(1.0f).margin(0.02f));
	}

	SECTION("Slot with custom fade time overrides global PARAM_FADE") {
		// Global fade = 0 (fast: rise ≈ 10ms — would complete in ~450 frames).
		// Slot 1 fade = 1.0 (slow: rise ≈ 10.24s — won't complete in 1000 frames).
		// After 1000 frames the transition must still be in progress, proving the
		// slot-level override was used rather than the fast global value.
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);

		// Settle fully at preset 0 (value 0.0) with the fast global fade
		module->presetLoad(0);
		h.dspSteps(1000);
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));

		// Override slot 1 to use the slow fade (1.0), then load it
		module->getSlot(1)->setFadeTime(1.0f);
		module->presetLoad(1);
		h.dspSteps(1000);
		// If global (fast) fade had been used the value would already be 1.0;
		// the slow slot fade keeps it near its starting point (observed ~0.0021).
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.05f);
	}

	SECTION("Slot with default fade time (-1) uses global PARAM_FADE") {
		// Slot 1 uses default fade time (-1 means use parameter)
		REQUIRE(module->getSlot(1)->getFadeTime() == Catch::Approx(-1.0f));

		// First, fully settle at preset 0 (value 0.0) using zero fade
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		module->presetLoad(0);
		h.dspSteps(1000);
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));

		// Now switch to slow fade and load preset 1 (value 1.0)
		module->params[TransitModule<12>::PARAM_FADE].setValue(1.0f);
		module->presetLoad(1);
		// With global fade = 1.0 (maximum), after only 100 frames the transition
		// should not be complete yet (fade time ≈ 10s at 44100Hz)
		h.dspSteps(100);
		// Param value should still be near its starting point (observed ~0.00014)
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.05f);
	}
}


TEST_CASE("Fade CV input is additive to PARAM_FADE and ignored by per-slot override", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.0f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(1.0f);
	module->presetSave(1);

	// Settle fully at preset 0 (value 0.0) before each section
	auto settleAtZero = [&]() {
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		module->inputs[TransitModule<12>::INPUT_FADE].channels = 1;
		module->inputs[TransitModule<12>::INPUT_FADE].setVoltage(0.0f);
		module->presetLoad(0);
		h.dspSteps(1000);
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));
	};

	SECTION("Fade CV adds to PARAM_FADE on a default slot") {
		// PARAM_FADE = 0 (fast) but CV = 10V → combined fade = 0 + 10/10 = 1.0 (slow, ~10s).
		// After 1000 frames the transition must still be in progress.
		settleAtZero();
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		module->inputs[TransitModule<12>::INPUT_FADE].setVoltage(10.0f);
		module->presetLoad(1);
		h.dspSteps(1000);
		// Observed ~0.0021 at 1000 frames; a tight bound catches a much-faster regression.
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.05f);
	}

	SECTION("Fade CV is additive to a per-slot fade time") {
		// Slot 1 override = 0 (would be instant on its own).
		// CV = 10V adds 1.0, making combined fade = 1.0 (slow, ~10s).
		// After 1000 frames the transition must still be in progress.
		settleAtZero();
		module->getSlot(1)->setFadeTime(0.0f);
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		module->inputs[TransitModule<12>::INPUT_FADE].setVoltage(10.0f);
		module->presetLoad(1);
		h.dspSteps(1000);
		// Observed ~0.0021 at 1000 frames; a tight bound catches a much-faster regression.
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.05f);
	}
}


TEST_CASE("OUTPUT reflects the selected OUTMODE", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.0f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(1.0f);
	module->presetSave(1);

	// Fast (~instant) global fade so a transition completes in a handful of frames.
	module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
	h.connectOutput(module, TransitModule<12>::OUTPUT, 5);

	SECTION("ENV ramps up during the fade and returns to 0 on completion") {
		module->setOutMode(OUTMODE::ENV);
		module->presetLoad(0);
		h.dspSteps(512);
		module->presetLoad(1);
		h.dspSteps(64);
		// One divider tick into the fade the envelope should be rising, away from 0.
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() > 0.0f);
		h.dspSteps(512);
		// Transition complete: envelope returns to 0.
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
	}

	SECTION("GATE is high during the fade and low once it completes") {
		module->setOutMode(OUTMODE::GATE);
		module->presetLoad(0);
		h.dspSteps(512);
		module->presetLoad(1);
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(10.0f).margin(0.01f));
		h.dspSteps(512);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
	}

	SECTION("TRIG_SNAPSHOT fires a pulse whenever a preset is loaded") {
		module->setOutMode(OUTMODE::TRIG_SNAPSHOT);
		module->presetLoad(0);
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(10.0f).margin(0.01f));
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		module->presetLoad(1);
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(10.0f).margin(0.01f));
	}

	SECTION("TRIG_SOC fires a pulse when a fade starts, then falls silent") {
		module->setOutMode(OUTMODE::TRIG_SOC);
		module->presetLoad(0);
		h.dspSteps(512);
		module->presetLoad(1);
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(10.0f).margin(0.01f));
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
	}

	SECTION("TRIG_EOC is silent during the fade and fires once it completes") {
		module->setOutMode(OUTMODE::TRIG_EOC);
		module->presetLoad(0);
		h.dspSteps(512);
		module->presetLoad(1);
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(0.0f).margin(0.01f));
		h.dspSteps(512);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getVoltage() == Catch::Approx(10.0f).margin(0.01f));
	}

	SECTION("POLY sets 5 channels") {
		module->setOutMode(OUTMODE::POLY);
		module->presetLoad(0);
		h.dspSteps(512);
		module->presetLoad(1);
		h.dspSteps(64);
		REQUIRE(module->outputs[TransitModule<12>::OUTPUT].getChannels() == 5);
	}
}


TEST_CASE("CV VOLT mode respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	// Create presets at various slots
	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::VOLT;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(3);
	module->presetSetLast(9);
	// Initialize CV input
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
	h.dspStep();

	SECTION("0V selects presetFirst") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 3); // First usable
	}

	SECTION("10V selects presetLast - 1") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		REQUIRE(module->preset == 8); // Last usable (exclusive)
	}

	SECTION("Mid voltage maps correctly within range") {
		module->presetSetFirst(2);
		module->presetSetLast(8); // Range 2-7 (6 slots)
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(5.0f); // Middle
		h.dspStep();
		// Should map to middle of range: 2 + floor((8-2) * 0.5) = 2 + 3 = 5
		REQUIRE(module->preset == 5);
	}
}


TEST_CASE("CV C4 mode respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::C4;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(4);
	module->presetSetLast(10);
	// Process first to initialize state
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
	h.dspStep();

	SECTION("C4 (0V) selects presetFirst") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f); // C4 = 0 semitones
		h.dspStep();
		REQUIRE(module->preset == 4);
	}

	SECTION("Higher note selects next preset in range") {
		module->presetSetFirst(2);
		module->presetSetLast(8);
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(4.0f / 12.0f); // 4 semitones
		h.dspStep();
		REQUIRE(module->preset == 4); // Semitone 4 maps to preset 4
	}

	SECTION("Note beyond range is clamped") {
		module->presetSetFirst(1);
		module->presetSetLast(5);
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f); // Very high note
		h.dspStep();
		REQUIRE(module->preset == 4); // Clamped to presetLast - 1
	}
}


TEST_CASE("TRIG_FWD mode respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_FWD;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(3);
	module->presetSetLast(8);

	SECTION("Reset goes to presetFirst") {
		module->preset = 5;
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 3); // presetFirst
	}

	SECTION("Trigger advances within boundaries") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process enough frames to accumulate resetTimer > 1ms (~44 frames needed)
		h.dspSteps(100);
		// Set preset manually
		module->preset = 3; // At first
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 4); // Advanced by 1
	}

	SECTION("Trigger wraps from last to first") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process enough frames to accumulate resetTimer > 1ms
		h.dspSteps(100);
		// Set preset manually
		module->preset = 7; // At presetLast - 1
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 3); // Wrapped to presetFirst
	}
}


TEST_CASE("TRIG_REV mode respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_REV;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(3);
	module->presetSetLast(8);

	SECTION("Reset goes to presetLast - 1") {
		module->preset = 5;
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 7); // presetLast - 1
	}

	SECTION("Trigger reverses within boundaries") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process to clear any reset state
		h.dspSteps(100);
		// Set preset manually
		module->preset = 7; // At last
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 6); // Reversed by 1
	}

	SECTION("Trigger wraps from first to last") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process to clear any reset state
		h.dspSteps(100);
		// Set preset manually
		module->preset = 3; // At presetFirst
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 7); // Wrapped to presetLast - 1
	}
}


TEST_CASE("TRIG_PINGPONG mode respects boundaries and direction", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_PINGPONG;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(2);
	module->presetSetLast(6);

	// Initialize CV/Reset inputs, accumulate enough time for resetTimer
	module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
	h.dspSteps(100);

	auto trigger = [&]() {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();
	};

	SECTION("Advances forward from presetFirst") {
		module->preset = 2;
		module->slotCvModeDir = 1;
		trigger();
		REQUIRE(module->preset == 3);
		trigger();
		REQUIRE(module->preset == 4);
	}

	SECTION("Bounces at presetLast - 1 and reverses direction") {
		module->preset = 4; // one below presetLast-1 = 5
		module->slotCvModeDir = 1;
		trigger(); // n = 5 >= presetLast-1=5 → dir=-1, load 5
		REQUIRE(module->preset == 5);
		trigger(); // n = 5+(-1) = 4
		REQUIRE(module->preset == 4);
	}

	SECTION("Bounces at presetFirst and reverses direction") {
		module->preset = 3;
		module->slotCvModeDir = -1;
		trigger(); // n = 2, n <= presetFirst=2 → dir=1, load 2
		REQUIRE(module->preset == 2);
		trigger(); // n = 3
		REQUIRE(module->preset == 3);
	}

	SECTION("Reset goes to presetFirst and resets direction") {
		module->preset = 5;
		module->slotCvModeDir = -1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 2);  // presetFirst
		REQUIRE(module->slotCvModeDir == 1); // direction reset to forward
	}
}


TEST_CASE("TRIG_ALT mode alternates between presetFirst and an advancing secondary", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_ALT;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(2);
	module->presetSetLast(6);

	// Initialize inputs and accumulate resetTimer > 1ms
	module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
	h.dspSteps(100);

	auto trigger = [&]() {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();
	};

	SECTION("Reset goes to presetFirst and resets direction and alt to 0") {
		module->preset = 4;
		module->slotCvModeDir = -1;
		module->slotCvModeAlt = 3;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 2);           // presetFirst
		REQUIRE(module->slotCvModeDir == 1);    // direction reset
		REQUIRE(module->slotCvModeAlt == 0);    // alt counter reset
	}

	SECTION("Alternates: from presetFirst, first trigger advances secondary; second trigger returns to presetFirst") {
		// Start at presetFirst
		module->preset = 2;
		module->slotCvModeAlt = 2; // secondary at presetFirst
		module->slotCvModeDir = 1;

		// First trigger: preset == presetFirst → advance secondary and load it
		trigger();
		REQUIRE(module->preset == 3); // presetFirst + slotCvModeDir

		// Second trigger: preset != presetFirst → return to presetFirst
		trigger();
		REQUIRE(module->preset == 2); // Back to first

		// Third trigger: secondary advances again, pinning the step size
		trigger();
		REQUIRE(module->preset == 4);
	}
}


TEST_CASE("TRIG_RANDOM_WALK mode respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WALK;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(3);
	module->presetSetLast(8);

	module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
	h.dspSteps(100);

	SECTION("Reset goes to presetFirst") {
		module->preset = 6;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 3); // presetFirst
	}

	SECTION("Walk always stays within [presetFirst, presetLast - 1]") {
		module->preset = 5; // start in middle of range [3, 7]

		for (int i = 0; i < 100; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			h.dspStep();
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			h.dspStep();

			REQUIRE(module->preset >= 3);
			REQUIRE(module->preset <= 7); // presetLast - 1
		}
	}

	SECTION("Walk moves by at most 1 step per trigger") {
		module->preset = 5;
		int prevPreset = module->preset;

		for (int i = 0; i < 50; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			h.dspStep();
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			h.dspStep();

			int diff = std::abs(module->preset - prevPreset);
			REQUIRE(diff <= 1);
			prevPreset = module->preset;
		}
	}
}


TEST_CASE("TRIG_RANDOM mode respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_RANDOM;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(4);
	module->presetSetLast(9);

	SECTION("Reset goes to presetFirst") {
		module->preset = 7;
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 4); // presetFirst
	}

	SECTION("Random selection stays within boundaries") {
		std::set<int> selected;
		
		// Ensure inputs are clear and accumulate resetTimer > 1ms
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspSteps(100);
		// Set initial preset within range
		module->preset = 4;
		
		// Now trigger multiple random selections
		for (int i = 0; i < 50; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			h.dspStep();
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			h.dspStep();
			selected.insert(module->preset);
		}
		
		// All selections should be within [4, 8]
		for (int p : selected) {
			REQUIRE(p >= 4);
			REQUIRE(p < 9);
		}
		// With 50 iterations on 5 slots, should see multiple different values
		REQUIRE(selected.size() >= 2);
	}
}


TEST_CASE("TRIG_RANDOM_WO_REPEAT never selects the same preset twice in a row", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WO_REPEAT;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(3);
	module->presetSetLast(9);

	// Accumulate resetTimer > 1ms
	module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
	h.dspSteps(100);
	module->preset = 5; // start with a known preset

	SECTION("Reset goes to presetFirst") {
		module->preset = 7;
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset == 3); // presetFirst
	}

	SECTION("Consecutive triggers always select a different preset") {
		int prevPreset = module->preset;
		for (int i = 0; i < 60; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			h.dspStep();
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			h.dspStep();

			REQUIRE(module->preset != prevPreset);
			REQUIRE(module->preset >= 3);
			REQUIRE(module->preset < 9);
			prevPreset = module->preset;
		}
	}
}


TEST_CASE("TRIG_SHUFFLE visits all presets in range before repeating", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::TRIG_SHUFFLE;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->presetSetFirst(2);
	module->presetSetLast(7); // 5 slots: 2,3,4,5,6

	module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);

	// Reset to initialize the shuffle
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
	h.dspStep();
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	h.dspStep();

	SECTION("Reset re-shuffles the deck and selects within range") {
		// After the reset above, preset must be in range
		REQUIRE(module->preset >= 2);
		REQUIRE(module->preset < 7);

		// Trigger a second reset - should re-shuffle and pick from range again
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
		REQUIRE(module->preset >= 2);
		REQUIRE(module->preset < 7);
	}

	SECTION("One full shuffle cycle visits all presets in range exactly once") {
		std::set<int> visited;
		visited.insert(module->preset); // The one loaded by the reset

		// Trigger remaining 4 times to complete a full cycle of 5 slots
		for (int i = 0; i < 4; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			h.dspStep();
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			h.dspStep();
			visited.insert(module->preset);
		}

		// All 5 presets in range [2,7) must have been visited
		REQUIRE(visited.size() == 5);
		for (int p : visited) {
			REQUIRE(p >= 2);
			REQUIRE(p < 7);
		}
	}
}


TEST_CASE("ARM mode queues preset and loads on trigger", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.2f);
	module->presetSave(2);
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.7f);
	module->presetSave(5);
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.9f);
	module->presetSave(8);

	module->slotCvMode = SLOTCVMODE::ARM;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);

	// Load initial preset
	module->presetLoad(2);
	h.dspStep();

	SECTION("presetLoad with isNext=true queues next preset without loading it immediately") {
		int presetBefore = module->preset;
		module->presetLoad(5, true); // Queue preset 5
		h.dspStep();

		// Preset should not have changed yet
		REQUIRE(module->preset == presetBefore);
		REQUIRE(module->presetNext == 5);
	}

	SECTION("Trigger in ARM mode loads the queued preset") {
		module->presetLoad(5, true); // Queue preset 5
		h.dspStep();
		REQUIRE(module->presetNext == 5);

		// Trigger the ARM CV
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspStep();
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspStep();

		REQUIRE(module->preset == 5);
		REQUIRE(module->presetNext == -1); // Queue cleared
	}

	SECTION("Queuing a non-used slot does not update presetNext") {
		// Slot 11 is not used
		module->presetLoad(11, true);
		REQUIRE(module->presetNext == -1); // Ignored since slot not used
	}
}


TEST_CASE("WRITE mode saves and clears preset slots via front-panel buttons", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	// buttonDivider fires every 128 samples; LongPressButton needs pressedTime >= 1.0f
	// (real seconds) to register LONG_PRESS, so held/released presses are driven in
	// sample counts derived from the harness sample rate, not fixed constants.
	auto shortPress = [&](int slot) {
		module->params[TransitModule<12>::PARAM_PRESET + slot].setValue(10.f);
		h.dspSteps(128); // one buttonDivider tick while held
		module->params[TransitModule<12>::PARAM_PRESET + slot].setValue(0.f);
		h.dspSteps(128); // one tick to detect the release
	};
	auto longPress = [&](int slot) {
		module->params[TransitModule<12>::PARAM_PRESET + slot].setValue(10.f);
		h.dspSteps((int)h.sampleRate() + 256); // hold past the 1s long-press threshold
		module->params[TransitModule<12>::PARAM_PRESET + slot].setValue(0.f);
		h.dspSteps(128);
	};

	SECTION("Short press in WRITE mode saves the current parameter value into the slot") {
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.75f);
		module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::WRITE);
		h.dspStep();

		REQUIRE(module->getSlot(3)->isUsed() == false);
		shortPress(3);

		REQUIRE(module->getSlot(3)->isUsed() == true);
		REQUIRE(module->getSlot(3)->getPreset()->size() == 1);
		REQUIRE(module->getSlot(3)->getPreset()->at(0) == Catch::Approx(0.75f));
		REQUIRE(module->preset == 3);
	}

	SECTION("Short press in WRITE mode overwrites a previously saved slot") {
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.1f);
		module->presetSave(3);
		REQUIRE(module->getSlot(3)->getPreset()->at(0) == Catch::Approx(0.1f));

		testModule->params[TestModule::TEST_PARAM_1].setValue(0.9f);
		module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::WRITE);
		h.dspStep();
		shortPress(3);

		REQUIRE(module->getSlot(3)->getPreset()->at(0) == Catch::Approx(0.9f));
	}

	SECTION("Long press in WRITE mode clears the slot") {
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.4f);
		module->presetSave(6);
		module->getSlot(6)->setLabel("Saved");
		module->getSlot(6)->setFadeTime(0.5f);
		REQUIRE(module->getSlot(6)->isUsed() == true);

		module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::WRITE);
		h.dspStep();
		longPress(6);

		REQUIRE(module->getSlot(6)->isUsed() == false);
		REQUIRE(module->getSlot(6)->getPreset()->empty());
		REQUIRE(module->getSlot(6)->getLabel() == "");
		REQUIRE(module->getSlot(6)->getFadeTime() == Catch::Approx(-1.f));
	}

	SECTION("Long press in WRITE mode on the active preset resets preset to -1") {
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.4f);
		module->presetSave(6);
		module->presetLoad(6);
		REQUIRE(module->preset == 6);

		module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::WRITE);
		h.dspStep();
		longPress(6);

		REQUIRE(module->preset == -1);
	}

	SECTION("Short press in WRITE mode does not fall through to READ-mode preset loading") {
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.3f);
		module->presetSave(2);

		testModule->params[TestModule::TEST_PARAM_1].setValue(0.6f);
		module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::WRITE);
		h.dspStep();
		shortPress(5);

		// Slot 2 must be untouched by the write to slot 5, and preset must reflect
		// the saved slot rather than any READ-mode CV-arm/advance logic.
		REQUIRE(module->getSlot(2)->getPreset()->at(0) == Catch::Approx(0.3f));
		REQUIRE(module->preset == 5);
	}

	SECTION("WRITE mode ignores CV input") {
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.2f);
		module->presetSave(2);
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.7f);
		module->presetSave(5);

		module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::WRITE);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->presetLoad(2);
		h.dspStep();
		REQUIRE(module->preset == 2);

		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		h.dspSteps(500);
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		h.dspSteps(500);

		REQUIRE(module->preset == 2); // unaffected by CV while in WRITE mode
	}
}


TEST_CASE("Switch parameters snap at the fade midpoint instead of crossfading", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestSwitchModule* testModule = h.adoptModule(new TestSwitchModule);

	module->bindAddParameterRequest(testModule->id, TestSwitchModule::TEST_SWITCH);
	module->taskProcessorDsp.process();
	h.dspStep();

	SECTION("isSwitch is set for a bound SwitchQuantity with more than two positions") {
		REQUIRE(module->sourceHandles.size() == 1);
		REQUIRE(module->sourceHandles[0]->isSwitch == true);
	}

	testModule->params[TestSwitchModule::TEST_SWITCH].setValue(0.0f);
	module->presetSave(0);
	testModule->params[TestSwitchModule::TEST_SWITCH].setValue(2.0f);
	module->presetSave(1);

	// Fast-settle at preset 0, then switch to a slow (~1s) linear fade and load
	// preset 1. Verified by probe: the transition crosses the snap threshold
	// (s10 == 0.5) between roughly step 14100 and 14200 of the slow fade, and
	// the switch param is never seen at any value other than the old (0.0) or
	// new (2.0) position throughout.
	module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
	module->presetLoad(0);
	h.dspSteps(512);
	REQUIRE(testModule->params[TestSwitchModule::TEST_SWITCH].getValue() == Catch::Approx(0.0f));

	module->params[TransitModule<12>::PARAM_FADE].setValue(0.6f);
	module->presetLoad(1);

	SECTION("A switch holds its old value for the first half of a slow fade") {
		h.dspSteps(14000);
		REQUIRE(testModule->params[TestSwitchModule::TEST_SWITCH].getValue() == Catch::Approx(0.0f));
	}

	SECTION("A switch jumps directly to the new value without passing through intermediate positions") {
		h.dspSteps(14000);
		for (int i = 0; i < 20; i++) {
			h.dspSteps(64);
			float v = testModule->params[TestSwitchModule::TEST_SWITCH].getValue();
			REQUIRE((v == Catch::Approx(0.0f) || v == Catch::Approx(2.0f)));
		}
	}

	SECTION("A switch reaches the new value once the fade completes") {
		h.dspSteps(30000);
		REQUIRE(testModule->params[TestSwitchModule::TEST_SWITCH].getValue() == Catch::Approx(2.0f));
	}
}

TEST_CASE("A non-switch parameter crossfades smoothly through the same transition", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.0f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(1.0f);
	module->presetSave(1);

	module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
	module->presetLoad(0);
	h.dspSteps(512);

	module->params[TransitModule<12>::PARAM_FADE].setValue(0.6f);
	module->presetLoad(1);

	// At the same point in the fade where the switch in the sibling test case
	// has already snapped straight to its new value, a plain (non-switch)
	// parameter is still mid-crossfade, i.e. strictly between old and new.
	h.dspSteps(14150);
	float v = testModule->params[TestModule::TEST_PARAM_1].getValue();
	REQUIRE(v > 0.05f);
	REQUIRE(v < 0.95f);
}


TEST_CASE("Phase mode respects boundaries", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* module = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule = h.adoptModule(new TestModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	h.dspStep();
	
	for (int i = 0; i < 12; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 11.0f);
		module->presetSave(i);
	}

	module->slotCvMode = SLOTCVMODE::PHASE;
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f); // Minimum fade for fast convergence
	module->presetSetFirst(2);
	module->presetSetLast(8);
	// Initialize CV input before sections
	module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);

	SECTION("0V processes within boundaries") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Reset slewLimiter to target value for 0V: target = 0
		module->slewLimiter.reset(0.0f);
		// Process enough frames for phase mode divider
		h.dspSteps(300);
		// 0V lands exactly on presetFirst (slot 2)
		REQUIRE(module->presetPhaseLast == Catch::Approx(2.0f).margin(0.01f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(2 / 11.0f).margin(0.001f));
	}

	SECTION("10V processes within boundaries") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		// Reset slewLimiter to target value for 10V: target = (8-2-1)*10/10 = 5
		module->slewLimiter.reset(5.0f);
		// Process enough frames for phase mode divider
		h.dspSteps(300);
		// 10V lands exactly on presetFirst + 5 (slot 7)
		REQUIRE(module->presetPhaseLast == Catch::Approx(7.0f).margin(0.01f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(7 / 11.0f).margin(0.001f));
	}

	SECTION("Midpoint between two slots crossfades") {
		// p = (presetLast - presetFirst - 1) * v / 10 + presetFirst = 5*v/10 + 2
		// 1V -> p = 2.5, exactly between slot 2 and slot 3
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(1.0f);
		module->slewLimiter.reset(0.5f);
		h.dspSteps(300);
		REQUIRE(module->presetPhaseLast == Catch::Approx(2.5f).margin(0.01f));
		float slot2 = 2 / 11.0f;
		float slot3 = 3 / 11.0f;
		float midpoint = (slot2 + slot3) / 2.0f;
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(midpoint).margin(0.001f));
	}
}


// bindAddParameterRequest(presetLoading = true) skips
// the back-fill loop that keeps every slot's preset vector in sync with
// sourceHandles, so an older slot's preset can be shorter than sourceHandles.
// presetProcessPhase indexed that short vector by i unguarded
// (heap-buffer-overflow under ASan); presetProcess already had the
// `size() <= i` guard. Drive the real sequence rather than hand-shortening
// the vector, so the test tracks the actual patch-load code path.
// This does NOT reliably abort under ASan here (the 1-past-end read lands in
// a redzone that ASan's shadow marks poisoned, confirmed with
// __asan_address_is_poisoned, but the generated check at this particular
// call site does not trip — a discrepancy from an isolated repro of the same
// vector-overread pattern, which does abort). So this asserts the documented
// contract behaviourally instead: the second (newer) parameter must never be
// written by presetProcessPhase while it lacks a same-sized preset entry.
// Before the fix, a garbage read could crossfade/assign an arbitrary value
// into it; after the fix, the loop breaks at i == 1 and testParam2 is
// untouched for every CV position.

TEST_CASE("presetProcessPhase does not write a param whose preset is shorter than sourceHandles", "[Transit]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestModule* testModule1 = h.adoptModule(new TestModule);
	TestModule* testModule2 = h.adoptModule(new TestModule);

	// Bind one param, save two adjacent slots: sourceHandles.size() == 1
	transit->bindAddParameterRequest(testModule1->id, TestModule::TEST_PARAM_1);
	transit->taskProcessorDsp.process();
	transit->process(Test::makeProcessArgs(0));
	transit->presetSave(0);
	transit->presetSave(1);

	// Bind a second param with presetLoading = true: sourceHandles.size() == 2,
	// but preset[0].size() and preset[1].size() are still 1. Give it a
	// distinctive sentinel value that no crossfade of testModule1's values
	// (0..1 range) could ever produce, so any corruption is detectable.
	transit->bindAddParameterRequest(testModule2->id, TestModule::TEST_PARAM_2, true);
	transit->taskProcessorDsp.process();
	testModule2->params[TestModule::TEST_PARAM_2].setValue(7.5f);

	transit->slotCvMode = SLOTCVMODE::PHASE;
	transit->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	transit->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
	transit->presetSetFirst(0);
	transit->presetSetLast(2);
	transit->inputs[TransitModule<12>::INPUT_CV].channels = 1;

	// Sweep the CV input across the whole phase range, hitting both the
	// crossfade branch (p1 != p2) and the single-slot branch (p1 == p2).
	// Before the fix either branch reads preset[i] out of bounds for i == 1
	// and writes whatever it finds there into testModule2's param.
	for (float v = 0.f; v <= 10.f; v += 1.f) {
		transit->inputs[TransitModule<12>::INPUT_CV].setVoltage(v);
		h.dspSteps(50);
		REQUIRE(testModule2->params[TestModule::TEST_PARAM_2].getValue() == 7.5f);
	}
}
