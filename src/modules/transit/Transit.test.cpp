#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "TransitBase.hpp"
#include "Transit.cpp"

using namespace StoermelderPackOne::Transit;

SYNC_MODEL(modelTransit, "Transit");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Transit]") {
	TransitModule<12>* m = Test::createModule<TransitModule<12>>("Transit");
	TransitWidget<12>* mw = Test::createWidget<TransitWidget<12>>("Transit");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Transit][JSON]") {
	auto module = Test::createModule<TransitModule<12>>("Transit");

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

TEST_CASE("JSON round-trip preserves state", "[Transit][JSON]") {
	TransitModule<12>* m = Test::createModule<TransitModule<12>>("Transit");

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

	TransitModule<12>* m2 = Test::createModule<TransitModule<12>>("Transit");
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

	Test::destroyModule(m);
	Test::destroyModule(m2);
}

TEST_CASE("JSON serialization preserves boundaries", "[JSON][Transit]") {
	TransitModule<12>* module1 = Test::createModule<TransitModule<12>>("Transit");	Test::registerModule(module1);	
	// Set custom boundaries
	module1->presetSetFirst(2);
	module1->presetSetLast(9);
	module1->preset = 5;
	
	// Serialize
	json_t* rootJ = module1->dataToJson();
	
	// Create new module and deserialize
	TransitModule<12>* module2 = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module2);
	module2->dataFromJson(rootJ);
	
	// Check values preserved
	REQUIRE(module2->presetFirst == 2);
	REQUIRE(module2->presetLast == 9);
	REQUIRE(module2->preset == 5);
	
	json_decref(rootJ);
	Test::unregisterModule(module1);
	Test::unregisterModule(module2);
	Test::destroyModule(module1);
	Test::destroyModule(module2);
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


TEST_CASE("Setting presetFirst and presetLast boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	
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

	Test::destroyModule(module);
}


TEST_CASE("Comprehensive boundary edge cases", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	
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

	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("presetLoad respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	// Set up a mapped parameter
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("Multiple bound parameters save and load correctly", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	// Bind all three parameters
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_2);
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_3);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 100));
		}

		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.1f).margin(0.01f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_2].getValue() == Catch::Approx(3.0f).margin(0.05f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_3].getValue() == Catch::Approx(-2.0f).margin(0.05f));
	}

	SECTION("Switching between presets updates all parameters") {
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);

		module->presetLoad(0);
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 100));
		}

		module->presetLoad(1);
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 1200));
		}

		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.9f).margin(0.01f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_2].getValue() == Catch::Approx(9.0f).margin(0.05f));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_3].getValue() == Catch::Approx(4.0f).margin(0.05f));
	}

	SECTION("Preset stores the correct number of values per slot") {
		auto slot0 = module->getSlot(0);
		REQUIRE(slot0->isUsed());
		REQUIRE(slot0->getPreset()->size() == 3);
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("presetClear resets active preset selection", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("presetCopyPaste copies values correctly", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_2);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("presetShiftFront respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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
		module->process(Test::makeProcessArgs(2));
		
		// Preset 4 should now have the value that was in 5
		auto slot4 = module->getSlot(4);
		float newValue4 = slot4->isUsed() ? (*slot4->getPreset())[0] : -1.0f;
		REQUIRE(newValue4 == Catch::Approx(0.5f).margin(0.01f));
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("presetShiftBack shifts presets correctly", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("AUTO mode captures current values into previous preset", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

	// Save initial presets
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.2f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.8f);
	module->presetSave(1);

	// Switch to AUTO mode and load preset 0
	module->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::AUTO);
	module->presetLoad(0);
	module->process(Test::makeProcessArgs(2));

	SECTION("Switching presets auto-saves current param value into previous preset") {
		// Param is now being transitioned towards 0.2. Let it settle fully.
		// With a short fade: set fade=0 (minimum) to get instant application
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 100));
		}
		// Param value should now be 0.2 (from preset 0)
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.2f).margin(0.01f));

		// Manually change the param value (simulating user editing)
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.6f);

		// Load preset 1 - AUTO mode should save 0.6 into preset 0 first
		module->presetLoad(1);
		module->process(Test::makeProcessArgs(1200));

		// Preset 0 should now store 0.6
		auto slot0 = module->getSlot(0);
		REQUIRE(slot0->isUsed());
		REQUIRE((*slot0->getPreset())[0] == Catch::Approx(0.6f).margin(0.01f));
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("Per-slot fade time overrides global fade parameter", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.0f);
	module->presetSave(0);
	testModule->params[TestModule::TEST_PARAM_1].setValue(1.0f);
	module->presetSave(1);

	// Set global fade to maximum (slow)
	module->params[TransitModule<12>::PARAM_FADE].setValue(1.0f);

	SECTION("Slot with fade time of 0 transitions instantly") {
		// Override slot 1's fade time to 0 (immediate)
		module->getSlot(1)->setFadeTime(0.0f);

		// First, fully settle at preset 0 (value 0.0)
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		module->presetLoad(0);
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 100));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));

		// Now load preset 1 with slot fade=0 (but global fade stays 0 here too)
		module->presetLoad(1);
		// Process enough frames to complete a zero-fade transition
		for (int i = 0; i < 500; i++) {
			module->process(Test::makeProcessArgs(i + 1200));
		}
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
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 100));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));

		// Override slot 1 to use the slow fade (1.0), then load it
		module->getSlot(1)->setFadeTime(1.0f);
		module->presetLoad(1);
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 1200));
		}
		// If global (fast) fade had been used the value would already be 1.0;
		// the slow slot fade keeps it well below that.
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.9f);
	}

	SECTION("Slot with default fade time (-1) uses global PARAM_FADE") {
		// Slot 1 uses default fade time (-1 means use parameter)
		REQUIRE(module->getSlot(1)->getFadeTime() == Catch::Approx(-1.0f));

		// First, fully settle at preset 0 (value 0.0) using zero fade
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		module->presetLoad(0);
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 100));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));

		// Now switch to slow fade and load preset 1 (value 1.0)
		module->params[TransitModule<12>::PARAM_FADE].setValue(1.0f);
		module->presetLoad(1);
		// With global fade = 1.0 (maximum), after only 100 frames the transition
		// should not be complete yet (fade time ≈ 10s at 44100Hz)
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 1200));
		}
		// Param value should still be well below 1.0 (transition partway through)
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.9f);
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("Fade CV input is additive to PARAM_FADE and ignored by per-slot override", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));
	};

	SECTION("Fade CV adds to PARAM_FADE on a default slot") {
		// PARAM_FADE = 0 (fast) but CV = 10V → combined fade = 0 + 10/10 = 1.0 (slow, ~10s).
		// After 1000 frames the transition must still be in progress.
		settleAtZero();
		module->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
		module->inputs[TransitModule<12>::INPUT_FADE].setVoltage(10.0f);
		module->presetLoad(1);
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 1100));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.9f);
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
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(i + 1100));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() < 0.9f);
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("CV VOLT mode respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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
	module->process(Test::makeProcessArgs(1));

	SECTION("0V selects presetFirst") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(100));
		REQUIRE(module->preset == 3); // First usable
	}

	SECTION("10V selects presetLast - 1") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(100));
		REQUIRE(module->preset == 8); // Last usable (exclusive)
	}

	SECTION("Mid voltage maps correctly within range") {
		module->presetSetFirst(2);
		module->presetSetLast(8); // Range 2-7 (6 slots)
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(5.0f); // Middle
		module->process(Test::makeProcessArgs(100));
		// Should map to middle of range: 2 + floor((8-2) * 0.5) = 2 + 3 = 5
		REQUIRE(module->preset == 5);
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("CV C4 mode respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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
	module->process(Test::makeProcessArgs(1));

	SECTION("C4 (0V) selects presetFirst") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f); // C4 = 0 semitones
		module->process(Test::makeProcessArgs(100));
		REQUIRE(module->preset == 4);
	}

	SECTION("Higher note selects next preset in range") {
		module->presetSetFirst(2);
		module->presetSetLast(8);
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(4.0f / 12.0f); // 4 semitones
		module->process(Test::makeProcessArgs(100));
		REQUIRE(module->preset == 4); // Semitone 4 maps to preset 4
	}

	SECTION("Note beyond range is clamped") {
		module->presetSetFirst(1);
		module->presetSetLast(5);
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f); // Very high note
		module->process(Test::makeProcessArgs(100));
		REQUIRE(module->preset == 4); // Clamped to presetLast - 1
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_FWD mode respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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
		module->process(Test::makeProcessArgs(2));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(3));
		REQUIRE(module->preset == 3); // presetFirst
	}

	SECTION("Trigger advances within boundaries") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process enough frames to accumulate resetTimer > 1ms (~44 frames needed)
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}
		// Set preset manually
		module->preset = 3; // At first
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(200));
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(300));
		REQUIRE(module->preset == 4); // Advanced by 1
	}

	SECTION("Trigger wraps from last to first") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process enough frames to accumulate resetTimer > 1ms
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}
		// Set preset manually
		module->preset = 7; // At presetLast - 1
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(200));
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(300));
		REQUIRE(module->preset == 3); // Wrapped to presetFirst
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_REV mode respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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
		module->process(Test::makeProcessArgs(2));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(3));
		REQUIRE(module->preset == 7); // presetLast - 1
	}

	SECTION("Trigger reverses within boundaries") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process to clear any reset state
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}
		// Set preset manually
		module->preset = 7; // At last
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(200));
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(300));
		REQUIRE(module->preset == 6); // Reversed by 1
	}

	SECTION("Trigger wraps from first to last") {
		// Ensure inputs are clear
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		// Process to clear any reset state
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}
		// Set preset manually
		module->preset = 3; // At presetFirst
		// Now trigger
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(200));
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(300));
		REQUIRE(module->preset == 7); // Wrapped to presetLast - 1
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_PINGPONG mode respects boundaries and direction", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
	for (int i = 0; i < 100; i++) {
		module->process(Test::makeProcessArgs(i + 10));
	}

	auto trigger = [&](int frame) {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(frame));
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(frame + 1));
	};

	SECTION("Advances forward from presetFirst") {
		module->preset = 2;
		module->slotCvModeDir = 1;
		trigger(200);
		REQUIRE(module->preset == 3);
		trigger(300);
		REQUIRE(module->preset == 4);
	}

	SECTION("Bounces at presetLast - 1 and reverses direction") {
		module->preset = 4; // one below presetLast-1 = 5
		module->slotCvModeDir = 1;
		trigger(200); // n = 5 >= presetLast-1=5 → dir=-1, load 5
		REQUIRE(module->preset == 5);
		trigger(300); // n = 5+(-1) = 4
		REQUIRE(module->preset == 4);
	}

	SECTION("Bounces at presetFirst and reverses direction") {
		module->preset = 3;
		module->slotCvModeDir = -1;
		trigger(200); // n = 2, n <= presetFirst=2 → dir=1, load 2
		REQUIRE(module->preset == 2);
		trigger(300); // n = 3
		REQUIRE(module->preset == 3);
	}

	SECTION("Reset goes to presetFirst and resets direction") {
		module->preset = 5;
		module->slotCvModeDir = -1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(400));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(401));
		REQUIRE(module->preset == 2);  // presetFirst
		REQUIRE(module->slotCvModeDir == 1); // direction reset to forward
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_ALT mode alternates between presetFirst and an advancing secondary", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
	for (int i = 0; i < 100; i++) {
		module->process(Test::makeProcessArgs(i + 10));
	}

	auto trigger = [&](int frame) {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(frame));
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(frame + 1));
	};

	SECTION("Reset goes to presetFirst and resets direction and alt to 0") {
		module->preset = 4;
		module->slotCvModeDir = -1;
		module->slotCvModeAlt = 3;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(200));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(201));
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
		trigger(300);
		int secondary = module->preset;
		REQUIRE(secondary != 2); // Should not stay at first
		REQUIRE(secondary >= 2);
		REQUIRE(secondary < 6);

		// Second trigger: preset != presetFirst → return to presetFirst
		trigger(400);
		REQUIRE(module->preset == 2); // Back to first
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_RANDOM_WALK mode respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
	for (int i = 0; i < 100; i++) {
		module->process(Test::makeProcessArgs(i + 10));
	}

	SECTION("Reset goes to presetFirst") {
		module->preset = 6;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(200));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(201));
		REQUIRE(module->preset == 3); // presetFirst
	}

	SECTION("Walk always stays within [presetFirst, presetLast - 1]") {
		module->preset = 5; // start in middle of range [3, 7]

		for (int i = 0; i < 100; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			module->process(Test::makeProcessArgs(i * 200 + 300));
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			module->process(Test::makeProcessArgs(i * 200 + 400));

			REQUIRE(module->preset >= 3);
			REQUIRE(module->preset <= 7); // presetLast - 1
		}
	}

	SECTION("Walk moves by at most 1 step per trigger") {
		module->preset = 5;
		int prevPreset = module->preset;

		for (int i = 0; i < 50; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			module->process(Test::makeProcessArgs(i * 200 + 300));
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			module->process(Test::makeProcessArgs(i * 200 + 400));

			int diff = std::abs(module->preset - prevPreset);
			REQUIRE(diff <= 1);
			prevPreset = module->preset;
		}
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_RANDOM mode respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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
		module->process(Test::makeProcessArgs(2));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(3));
		REQUIRE(module->preset == 4); // presetFirst
	}

	SECTION("Random selection stays within boundaries") {
		std::set<int> selected;
		
		// Ensure inputs are clear and accumulate resetTimer > 1ms
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[TransitModule<12>::INPUT_CV].channels = 1;
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}
		// Set initial preset within range
		module->preset = 4;
		
		// Now trigger multiple random selections
		for (int i = 0; i < 50; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			module->process(Test::makeProcessArgs(i * 1000 + 400));
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			module->process(Test::makeProcessArgs(i * 1000 + 600));
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

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_RANDOM_WO_REPEAT never selects the same preset twice in a row", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
	for (int i = 0; i < 100; i++) {
		module->process(Test::makeProcessArgs(i + 10));
	}
	module->preset = 5; // start with a known preset

	SECTION("Reset goes to presetFirst") {
		module->preset = 7;
		module->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(200));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(201));
		REQUIRE(module->preset == 3); // presetFirst
	}

	SECTION("Consecutive triggers always select a different preset") {
		int prevPreset = module->preset;
		for (int i = 0; i < 60; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			module->process(Test::makeProcessArgs(i * 500 + 200));
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			module->process(Test::makeProcessArgs(i * 500 + 300));

			REQUIRE(module->preset != prevPreset);
			REQUIRE(module->preset >= 3);
			REQUIRE(module->preset < 9);
			prevPreset = module->preset;
		}
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("TRIG_SHUFFLE visits all presets in range before repeating", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
	module->process(Test::makeProcessArgs(5));
	module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	module->process(Test::makeProcessArgs(6));

	SECTION("Reset re-shuffles the deck and selects within range") {
		// After the reset above, preset must be in range
		REQUIRE(module->preset >= 2);
		REQUIRE(module->preset < 7);

		// Trigger a second reset - should re-shuffle and pick from range again
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(100));
		module->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(101));
		REQUIRE(module->preset >= 2);
		REQUIRE(module->preset < 7);
	}

	SECTION("One full shuffle cycle visits all presets in range exactly once") {
		std::set<int> visited;
		visited.insert(module->preset); // The one loaded by the reset

		// Trigger remaining 4 times to complete a full cycle of 5 slots
		for (int i = 0; i < 4; i++) {
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
			module->process(Test::makeProcessArgs(i * 500 + 100));
			module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
			module->process(Test::makeProcessArgs(i * 500 + 200));
			visited.insert(module->preset);
		}

		// All 5 presets in range [2,7) must have been visited
		REQUIRE(visited.size() == 5);
		for (int p : visited) {
			REQUIRE(p >= 2);
			REQUIRE(p < 7);
		}
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("ARM mode queues preset and loads on trigger", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));

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
	module->process(Test::makeProcessArgs(2));

	SECTION("presetLoad with isNext=true queues next preset without loading it immediately") {
		int presetBefore = module->preset;
		module->presetLoad(5, true); // Queue preset 5
		module->process(Test::makeProcessArgs(3));

		// Preset should not have changed yet
		REQUIRE(module->preset == presetBefore);
		REQUIRE(module->presetNext == 5);
	}

	SECTION("Trigger in ARM mode loads the queued preset") {
		module->presetLoad(5, true); // Queue preset 5
		module->process(Test::makeProcessArgs(3));
		REQUIRE(module->presetNext == 5);

		// Trigger the ARM CV
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(4));
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(5));

		REQUIRE(module->preset == 5);
		REQUIRE(module->presetNext == -1); // Queue cleared
	}

	SECTION("Queuing a non-used slot does not update presetNext") {
		// Slot 11 is not used
		module->presetLoad(11, true);
		REQUIRE(module->presetNext == -1); // Ignored since slot not used
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}


TEST_CASE("Phase mode respects boundaries", "[Transit]") {
	TransitModule<12>* module = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(module);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	module->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	module->taskProcessorDsp.process();
	module->process(Test::makeProcessArgs(1));
	
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
		for (int frame = 0; frame < 300; frame++) {
			module->process(Test::makeProcessArgs(frame + 100));
		}
		// presetPhaseLast should converge near presetFirst (2.0)
		if (module->presetPhaseLast > 0) {
			REQUIRE(module->presetPhaseLast >= 2.0f);
			REQUIRE(module->presetPhaseLast < 8.0f);
		}
	}

	SECTION("10V processes within boundaries") {
		module->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		// Reset slewLimiter to target value for 10V: target = (8-2-1)*10/10 = 5
		module->slewLimiter.reset(5.0f);
		// Process enough frames for phase mode divider
		for (int frame = 0; frame < 300; frame++) {
			module->process(Test::makeProcessArgs(frame + 100));
		}
		// presetPhaseLast should converge near presetFirst + 5 = 7.0
		if (module->presetPhaseLast > 0) {
			REQUIRE(module->presetPhaseLast >= 2.0f);
			REQUIRE(module->presetPhaseLast <= 8.0f);
		}
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(module);
	Test::destroyModule(module);
}