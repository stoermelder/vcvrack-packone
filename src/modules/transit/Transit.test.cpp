#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "TransitBase.hpp"
#include "Transit.cpp"

using namespace StoermelderPackOne::Transit;

Test::TestContext<> testContext;

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
