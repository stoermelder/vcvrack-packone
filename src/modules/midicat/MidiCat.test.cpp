#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiCat.hpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne::MidiCat;

SYNC_MODEL(modelMidiCat, "MidiCat");
Test::TestContext<> testContext;

// Helper class to provide a test module with parameters
struct TestModule : rack::Module {
	enum ParamIds {
		TEST_PARAM_1,
		TEST_PARAM_2,
		TEST_PARAM_3,
		TEST_PARAM_4,
		TEST_PARAM_5,
		NUM_PARAMS
	};

	TestModule() {
		config(NUM_PARAMS, 0, 0, 0);
		ParamQuantity* pq;
		configParam(TEST_PARAM_1, 0.f, 1.f, 0.5f, "Test Parameter 1");
		configParam(TEST_PARAM_2, 0.f, 127.f, 0.f, "Test Parameter 2");
		configParam(TEST_PARAM_3, -10.f, 10.f, 0.f, "Test Parameter 3");
		pq = configParam(TEST_PARAM_4, 0.f, 10.f, 0.f, "Test Parameter 4 (Snapped)");
		pq->snapEnabled = true;
		pq = configParam(TEST_PARAM_5, 0.f, 700.f, 0.f, "Test Parameter 5 (Snapped)");
		pq->snapEnabled = true;
	}
};



TEST_CASE("Construction and initialization", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatWidget* mw = Test::createWidget<MidiCatWidget>(module);

	SECTION("CC values are initialized to -1") {
		std::vector<int> v;
		for (int i = 0; i < 128; i++) {
		if (module->valuesCc[i] != -1 || module->valuesNote[i] != -1)
			v.push_back(i);
		}
		REQUIRE(v.size() == 0); // No values should be different
	}

	SECTION("All CC and note adapters are unassigned") {
		std::vector<int> v;
		for (int i = 0; i < MAX_CHANNELS; i++) {
			if (module->ccs[i].getCc() != -1 || module->notes[i].getNote() != -1)
				v.push_back(i);
		}
		REQUIRE(v.size() == 0); // No adapters should be assigned
	}

	Test::destroyWidget(mw);
	Test::destroyModule(module);
}

TEST_CASE("Preset JSON null-guards", "[MidiCat][JSON]") {
	auto module = Test::createModule<MidiCatModule>("MidiCat");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("MIDI learning functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1); // Process every sample for testing
	int j = 1;

	SECTION("Learning CC assigns to correct channel") {
		module->enableLearn(0, true);		
		module->midiCc(Test::makeMidiMessage(0xb, 0, 10, 64));
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->ccs[0].getCc() == 10);
		REQUIRE(module->ccs[0].ccMode == CCMODE::DIRECT);
		REQUIRE(module->notes[0].getNote() == -1); // Note should be unassigned
	}

	SECTION("Learning note assigns to correct channel") {
		module->enableLearn(0, true);
		module->midiNotePress(Test::makeMidiMessage(0x9, 0, 60, 100));
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->notes[0].getNote() == 60);
		REQUIRE(module->notes[0].noteMode == NOTEMODE::MOMENTARY);
		REQUIRE(module->ccs[0].getCc() == -1); // CC should be unassigned
	}

	SECTION("Disable learning") {
		module->enableLearn(0);
		module->disableLearn();
		REQUIRE(module->learningId == -1);
	}

	SECTION("All channels learn CC correctly") {
		std::vector<int> v;
		for (int i = 0; i < MAX_CHANNELS; i++) {
			module->enableLearn(i, true);		
			module->midiCc(Test::makeMidiMessage(0xb, 0, i, 64));
			module->process(Test::makeProcessArgs(j++));
			module->disableLearn();
			 // All CCs should be assigned correctly and Note should be unassigned
			if (module->ccs[i].getCc() != i || module->notes[i].getNote() != -1) {
				v.push_back(i);
			}
		}
		REQUIRE(v.size() == 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("Parameter mapping core functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	
	SECTION("Process increments timestamp") {
		uint64_t tsBefore = module->ts;	
		module->process(Test::makeProcessArgs(0));		
		REQUIRE(module->ts == tsBefore + 1);
	}

	SECTION("Process handles empty MIDI queue without error") {
		// Should not crash or throw
		REQUIRE_NOTHROW(module->process(Test::makeProcessArgs(0)));
	}

	SECTION("Process updates mappings when MIDI received") {
		TestModule* testModule = new TestModule();
		Test::registerModule(testModule);
		
		// Set up a mapping
		module->enableLearn(0, true);
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64)); // Learn CC7
		module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
		module->ccs[0].ccMode = CCMODE::DIRECT;
		module->process(Test::makeProcessArgs(1));
		
		// Send MIDI
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
		module->process(Test::makeProcessArgs(2));
		
		// Verify CC was processed
		REQUIRE(module->valuesCc[7] == 100);

		// Verify parameter was updated
		ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
		REQUIRE(pq->getValue() == Catch::Approx(100.0f / 127.0f).margin(0.01f));

		Test::unregisterModule(testModule);
	}

	Test::destroyModule(module);
}


TEST_CASE("CC basic processing", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");

	SECTION("Message updates internal state") {
		bool result = module->midiCc(Test::makeMidiMessage(0xb, 0, 7, 64)); // CC7, value 64
		REQUIRE(result == true); // First message should trigger update
		REQUIRE(module->valuesCc[7] == 64);
	}

	SECTION("Duplicate CC value does not trigger update") {
		bool result1 = module->midiCc(Test::makeMidiMessage(0xb, 0, 10, 100)); // CC10, value 100
		bool result2 = module->midiCc(Test::makeMidiMessage(0xb, 0, 10, 100)); // Same value again
		REQUIRE(result1 == true);
		REQUIRE(result2 == false); // Same value, no update
	}

	SECTION("Different CC numbers are stored independently") {		
		module->midiCc(Test::makeMidiMessage(0xb, 0, 1, 50)); // CC1, value 50
		module->midiCc(Test::makeMidiMessage(0xb, 0, 2, 75)); // CC2, value 75
		REQUIRE(module->valuesCc[1] == 50);
		REQUIRE(module->valuesCc[2] == 75);
	}

	Test::destroyModule(module);
}

TEST_CASE("CC Mode DIRECT", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	int i = 1;

	// Set up mapping
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127)); // Initialize CC state
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->ccs[0].ccMode = CCMODE::DIRECT;

	// Send CC value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	module->process(Test::makeProcessArgs(i++));
	// Check that CC was received
	REQUIRE(module->valuesCc[7] == 64);	
	// Parameter should be updated (approximately 64/127 = 0.504)
	REQUIRE(pq->getValue() == Catch::Approx(64.0f / 127.0f).margin(0.1f));

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("CC Mode DIRECT for snapEnabled params", "[MidiCat]") {
	SECTION("Regular snapped param") {
		MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
		module->processDivider.setDivision(1);
		TestModule* testModule = new TestModule();
		Test::registerModule(testModule);
		ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_4);
		int i = 1;

		// Set up mapping
		module->enableLearn(0, true);
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64)); // Learn CC7
		module->learnParam(0, testModule->id, TestModule::TEST_PARAM_4);
		module->ccs[0].ccMode = CCMODE::DIRECT;
		module->process(Test::makeProcessArgs(i++));

		// Send CC message to set parameter
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(pq->getValue() == pq->getMinValue()); // Min value
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(pq->getValue() == pq->getMaxValue()); // Max value

		// Mid value should snap to nearest integer
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(pq->getValue() == 5.0f); // Mid value snapped

		// Set the value manually to 1 and check feedback
		module->process(Test::makeProcessArgs(i++));
		pq->setValue(1.f);
		module->process(Test::makeProcessArgs(i++));
		// Recheck the value applied to the parameter
		REQUIRE(pq->getValue() == 1.f);

		Test::unregisterModule(testModule);
		Test::destroyModule(module);
	}

	SECTION("High snap count param") {
		MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
		module->processDivider.setDivision(1);
		TestModule* testModule = new TestModule();
		Test::registerModule(testModule);
		ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_5);
		int i = 1;

		// Set up mapping
		module->enableLearn(0, true);
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64)); // Learn CC7
		module->learnParam(0, testModule->id, TestModule::TEST_PARAM_5);
		module->ccs[0].ccMode = CCMODE::DIRECT;
		module->process(Test::makeProcessArgs(i++));

		// Send CC messages to set parameter
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(pq->getValue() == pq->getMinValue()); // Min value
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(pq->getValue() == pq->getMaxValue()); // Max value

		// Set the value manually to 1 and check feedback
		module->process(Test::makeProcessArgs(i++));
		pq->setValue(1.f);
		module->process(Test::makeProcessArgs(i++));
		// MIDI feedback will be 0, as 127/hugeValue is rounded down
		REQUIRE(module->midiOutput.lastValues[7] == 0);
		module->process(Test::makeProcessArgs(i++));
		// Recheck the value applied to the parameter
		REQUIRE(pq->getValue() == 1.f);

		Test::unregisterModule(testModule);
		Test::destroyModule(module);
	}
}

TEST_CASE("CC Mode TOGGLE", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));
	
	// Set up mapping
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 127)); // Initialize CC state
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0)); // Initialize CC state
	module->process(Test::makeProcessArgs(i++));
	module->ccs[0].ccMode = CCMODE::TOGGLE;

	// First toggle: should go to max
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 127));
	module->process(Test::makeProcessArgs(i++));
	// Check internal state progressed
	REQUIRE(module->lastValueIn[0] == -2);

	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0));
	module->process(Test::makeProcessArgs(i++));
	// Check internal state progressed
	REQUIRE(module->lastValueIn[0] == -3);

	// Check parameter updated
	REQUIRE(pq->getValue() == pq->getMaxValue());
	
	// Second toggle: should go to min
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 127));
	module->process(Test::makeProcessArgs(i++));
	// Check state wrapped around
	REQUIRE(module->lastValueIn[0] == -4);

	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0));
	module->process(Test::makeProcessArgs(i++));
	// Check state wrapped around
	REQUIRE(module->lastValueIn[0] == -1);

	// Check parameter updated
	REQUIRE(pq->getValue() == pq->getMinValue());

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("CC Mode PICKUP1", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_2);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping for CC7 -> TEST_PARAM_2
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0)); // initial CC
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_2);
	module->process(Test::makeProcessArgs(i++));
	module->ccs[0].ccMode = CCMODE::PICKUP1;

	// Initialize parameter to 64
	pq->setValue(64.f);
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	// Send a different CC first -> should not change parameter
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 3));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	// Send CC equal to parameter -> lock onto this value (no change yet)
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	// Send another CC -> should now pick up and change to new value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 100.f);

	// Manual parameter change should unsnap
	pq->setValue(10.f);
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 10.f);

	// Send a non-matching CC -> should not change
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 90));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 10.f);

	// Now send matching CC and then a different one to pick up again
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 10));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 10.f);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 20));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 20.f);

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("CC Mode PICKUP2", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_2);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping for CC7 -> TEST_PARAM_2
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0)); // initial CC
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_2);
	module->process(Test::makeProcessArgs(i++));
	module->ccs[0].ccMode = CCMODE::PICKUP2;

	// Initialize parameter to 64
	pq->setValue(64.f);
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	// Send a different CC first -> should not change parameter
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 3));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	// Send CC equal to parameter -> lock onto this value (no change yet)
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	// Small jump should pick up: send matching value, then a nearby value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 66));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 66.f);

	// Reset to 64 and try a large jump -> should NOT pick up
	pq->setValue(64.f);
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 10)); // big jump
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 64.f);

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("CC Mode SNAPPED", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_4);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));
	
	// Set up mapping
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127)); // Initialize CC state
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_4);
	module->process(Test::makeProcessArgs(i++));
	module->ccs[0].ccMode = CCMODE::SNAPPED;
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++));

	// Should start at min
	pq->setValue(pq->getMinValue());
	REQUIRE(pq->getValue() == pq->getMinValue());

	// First snap: should advance to 1
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 1.0f);

	// Second snap: should advance to 2
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 2.0f);

	// Sending zero should not change the snapped value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 2.0f);

	// Wrapping: set to max and next snap wraps to min
	pq->setValue(pq->getMaxValue());
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == pq->getMinValue());

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("CC Mode SNAPPED_SL", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_4);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127)); // Init CC state
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_4);
	module->process(Test::makeProcessArgs(i++));
	module->ccs[0].ccMode = CCMODE::SNAPPED_SL;

	// Should start at min
	pq->setValue(pq->getMinValue());
	REQUIRE(pq->getValue() == pq->getMinValue());

	// Short press: press and release quickly -> next snapped value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
	module->process(Test::makeProcessArgs(i++)); // set lastTs
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++)); // diffTs small -> next
	REQUIRE(pq->getValue() == 1.0f);

	// Long press: set current to 2, simulate long duration, then release -> previous snapped
	pq->setValue(2.0f);
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
	module->process(Test::makeProcessArgs(i++));
	for (uint64_t ts = 0; ts < module->longPressDuration; ts++) {
		module->process(Test::makeProcessArgs(i++));
	}
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 1.0f);

	// Long press, but too short -> should go to next snapped
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 127));
	module->process(Test::makeProcessArgs(i++));
	for (uint64_t ts = 0; ts < module->longPressDuration / 2; ts++) {
		module->process(Test::makeProcessArgs(i++));
	}
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 2.0f);

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}


TEST_CASE("CC 14-bit", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->process(Test::makeProcessArgs(1));

	SECTION("14-bit mode combines MSB and LSB") {
		module->ccs[0].setCc(7); // CC 7 is MSB
		module->ccs[0].set14bit(true);
		
		// Send MSB (CC7)
		module->midiProcessMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
		module->ccs[0].process();
		// Send LSB (CC39 = CC7 + 32)
		module->midiProcessMessage(Test::makeMidiMessage(0xb, 0, 39, 32));
		module->ccs[0].process();
		// Process to combine values
		module->ccs[0].process();
		// 14-bit value = MSB * 128 + LSB = 64 * 128 + 32 = 8224
		REQUIRE(module->ccs[0].getValue() == 8224);
	}

	Test::destroyModule(module);
}


TEST_CASE("Note basic processing", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	int i = 1;

	SECTION("Message updates internal state") {
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100));	// Note 60, velocity 100
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(module->valuesNote[60] == 100);
	}

	SECTION("Note release sets velocity to 0") {
		// First press the note, Note 60, velocity 100
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
		module->process(Test::makeProcessArgs(i++));
		// Then release it, Note 60 release
		module->midiProcessMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(module->valuesNote[60] == 0);
	}

	SECTION("Multiple note presses tracked independently") {
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Note 60, velocity 100
		module->process(Test::makeProcessArgs(i++));
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 62, 127)); // Note 62, velocity 127
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(module->valuesNote[60] == 100);
		REQUIRE(module->valuesNote[62] == 127);	
	}

	Test::destroyModule(module);
}

TEST_CASE("Note Mode MOMENTARY", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	int i = 1;

	// Set up mapping
	module->enableLearn(0, true);
	module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Initialize note state
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->notes[0].noteMode = NOTEMODE::MOMENTARY;
	
	// Press note
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	// Check parameter went high
	REQUIRE(module->valuesNote[60] == 100);
	
	// Release note
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	// Check parameter went low
	REQUIRE(module->valuesNote[60] == 0);

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("Note Mode SNAPPED", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_4);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping (learn note 60)
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Learn note 60
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_4);
	module->process(Test::makeProcessArgs(i++));
	module->notes[0].noteMode = NOTEMODE::SNAPPED;
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));

	// Should start at min
	pq->setValue(pq->getMinValue());
	REQUIRE(pq->getValue() == pq->getMinValue());

	// First snap: should advance to 1
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 1.0f);

	// Second snap: should advance to 2
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 2.0f);

	// Release should not change the snapped value
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 2.0f);

	// Wrapping: set to max and next snap wraps to min
	pq->setValue(pq->getMaxValue());
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == pq->getMinValue());

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("Note Mode SNAPPED_SL", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_4);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping (learn note 60)
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Learn note 60
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_4);
	module->process(Test::makeProcessArgs(i++));
	module->notes[0].noteMode = NOTEMODE::SNAPPED_SL;
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));

	// Should start at min
	pq->setValue(pq->getMinValue());
	REQUIRE(pq->getValue() == pq->getMinValue());

	// Short press: press and release quickly -> next snapped value
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++)); // set lastTs
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++)); // diffTs small -> next
	REQUIRE(pq->getValue() == 1.0f);

	// Long press: set current to 2, simulate long duration, then release -> previous snapped
	pq->setValue(2.0f);
	module->process(Test::makeProcessArgs(i++));
	// Short press: press and release quickly -> next snapped value
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	for (uint64_t ts = 0; ts < module->longPressDuration; ts++) {
		module->process(Test::makeProcessArgs(i++));
	}
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 1.0f);

	// Long press, but too short -> should go to next snapped
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	for (uint64_t ts = 0; ts < module->longPressDuration / 2; ts++) {
		module->process(Test::makeProcessArgs(i++));
	}
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == 2.0f);

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}


TEST_CASE("MIDI feedback", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	
	SECTION("Output tracks last CC values sent") {
		module->midiOutput.setValue(64, 7, false);
		REQUIRE(module->midiOutput.lastValues[7] == 64);		
		// Sending same value again should be skipped
		module->midiOutput.setValue(64, 7, false);
		REQUIRE(module->midiOutput.lastValues[7] == 64);
	}

	SECTION("Output tracks gate states") {
		module->midiOutput.setGate(100, 60, false, false);
		REQUIRE(module->midiOutput.lastGates[60] == true);		
		module->midiOutput.setGate(0, 60, false, false);
		REQUIRE(module->midiOutput.lastGates[60] == false);
	}

	SECTION("Reset clears output state") {
		module->midiOutput.setValue(64, 7, false);
		module->midiOutput.setGate(100, 60, false, false);		
		module->midiOutput.reset();	
		REQUIRE(module->midiOutput.lastValues[7] == -1);
		REQUIRE(module->midiOutput.lastGates[60] == false);
	}

	Test::destroyModule(module);
}

TEST_CASE("MIDI feedback after preset load", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Map CC7 to TEST_PARAM_1 in DIRECT mode
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0)); // initial CC
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->ccs[0].ccMode = CCMODE::DIRECT;

	// Set parameter and verify initial feedback was sent
	pq->setValue(0.5f);
	REQUIRE(pq->getValue() == 0.5f);

	// Get the current preset
	json_t* preset = module->dataToJson();
	// Reset the module, all mappings clear now
	Module::ResetEvent re;
	module->onReset(re);
	module->process(Test::makeProcessArgs(i++));

	// only empty slot
	REQUIRE(module->mapLen == 1); 

	// Set parameter to different value to detect feedback
	pq->setValue(pq->getMaxValue());
	REQUIRE(pq->getValue() == pq->getMaxValue());

	// Load the preset back
	module->dataFromJson(preset);
	module->processDivider.setDivision(1);
	module->process(Test::makeProcessArgs(i++));
	json_decref(preset);
	
	// The parameter should be unchanged
	REQUIRE(pq->getValue() == 1.0f);
	// Mapping should be restored
	REQUIRE(module->mapLen == 2);
	// The last sent MIDI value should match the parameter
	REQUIRE(module->midiOutput.lastValues[7] == 127);

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("MIDIMODE LOCATE", "[MidiCat]") {
    MidiCatModule* m = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatWidget* mw = Test::createWidget<MidiCatWidget>(m);
    m->processDivider.setDivision(1);
	Test::registerModule(m, mw);

    TestModule* testModule = new TestModule();
	testModule->id = Test::getModuleId();
    Test::registerModule(testModule);

	int j = 1;

    SECTION("Locate indicates CC mappings") {
        // Set up mapping for CC7 -> TEST_PARAM_1, with current CC value 64
		const int paramId = 0;
        m->enableLearn(paramId, true);
        m->midiProcessMessage(Test::makeMidiMessage(0xb, 0, 7, 64)); // set CC value
        m->learnParam(paramId, testModule->id, TestModule::TEST_PARAM_1);
        m->process(Test::makeProcessArgs(j++));

        // Enter locate mode
        m->setMode(MIDIMODE::MIDIMODE_LOCATE);
		// Trigger indication
        m->midiProcessMessage(Test::makeMidiMessage(0xb, 0, 7, 100)); 
        m->process(Test::makeProcessArgs(j++));
		// step the widget to update indicator state
		mw->step();

		// Check for indication
		REQUIRE(m->paramHandles[paramId].indicateCount != 0);
    }

    SECTION("Locate indicates Note mappings") {
        // Set up mapping for note 60 -> TEST_PARAM_1, with current note value 100
		const int paramId = 0;
        m->enableLearn(paramId, true);
        m->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // set note value
        m->learnParam(paramId, testModule->id, TestModule::TEST_PARAM_1);
        m->process(Test::makeProcessArgs(j++));
        m->midiProcessMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
        m->process(Test::makeProcessArgs(j++));

        m->setMode(MIDIMODE::MIDIMODE_LOCATE);
		// Trigger indication
        m->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // set note value
        m->process(Test::makeProcessArgs(j++));
		// step the widget to update indicator state
		mw->step();

		// Check for indication
		REQUIRE(m->paramHandles[paramId].indicateCount != 0);
    }

    Test::unregisterModule(testModule);
	Test::unregisterModule(m, mw);
    Test::destroyModule(m);
}

/*

TEST_CASE("Clear map functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	// Set up a mapping
	module->ccs[0].setCc(10);
	module->notes[0].setNote(60);
	module->textLabel[0] = "Test Label";
	
	SECTION("Clear specific map resets all values") {
		module->clearMap(0);
		
		REQUIRE(module->ccs[0].getCc() == -1);
		REQUIRE(module->notes[0].getNote() == -1);
		REQUIRE(module->textLabel[0] == "");
	}

	SECTION("Clear all maps resets module state") {
		module->clearMaps_NoLock();
		
		REQUIRE(module->learningId == -1);
		REQUIRE(module->mapLen == 1);
		
		for (int i = 0; i < MAX_CHANNELS; i++) {
			REQUIRE(module->ccs[i].getCc() == -1);
			REQUIRE(module->notes[i].getNote() == -1);
		}
	}

	delete module;
}

TEST_CASE("MidiCcAdapter functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->process({});

	SECTION("CC adapter processes new values") {
		module->ccs[0].setCc(10);
		
		// Send a CC message
		auto ccMsg = makeMidiMessage(0xb, 0, 10, 50);
		module->midiCc(ccMsg);
		
		// Process should detect change
		bool changed = module->ccs[0].process();
		REQUIRE(changed == true);
		REQUIRE(module->ccs[0].getValue() == 50);
	}

	SECTION("CC adapter ignores unchanged values") {
		module->ccs[0].setCc(10);
		
		auto ccMsg = makeMidiMessage(0xb, 0, 10, 50);
		module->midiCc(ccMsg);
		module->ccs[0].process();
		
		// Process again without new message
		bool changed = module->ccs[0].process();
		REQUIRE(changed == false);
	}

	SECTION("Reset clears CC assignment") {
		module->ccs[0].setCc(10);
		module->ccs[0].reset();
		
		REQUIRE(module->ccs[0].getCc() == -1);
		REQUIRE(module->ccs[0].current == -1);
	}

	delete module;
}

TEST_CASE("MidiNoteAdapter functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->process({});

	SECTION("Note adapter processes new values") {
		module->notes[0].setNote(60);
		
		// Send a note message
		auto noteMsg = makeMidiMessage(0x9, 0, 60, 100);
		module->midiNotePress(noteMsg);
		
		// Process should detect change
		bool changed = module->notes[0].process();
		REQUIRE(changed == true);
		REQUIRE(module->notes[0].getValue() == 100);
	}

	SECTION("Note adapter tracks releases") {
		module->notes[0].setNote(60);
		
		// Press
		auto noteOn = makeMidiMessage(0x9, 0, 60, 100);
		module->midiNotePress(noteOn);
		module->notes[0].process();
		
		module->process({});

		// Release
		auto noteOff = makeMidiMessage(0x8, 0, 60, 0);
		module->midiNoteRelease(noteOff);
		bool changed = module->notes[0].process();
		
		REQUIRE(changed == true);
		REQUIRE(module->notes[0].getValue() == 0);
	}

	delete module;
}

TEST_CASE("Map length management", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	
	SECTION("Map length updates when mappings added") {
		REQUIRE(module->mapLen == 1); // Default with one empty slot
		
		module->ccs[0].setCc(10);
		module->updateMapLen();
		REQUIRE(module->mapLen == 2); // One mapping + empty slot
		
		module->ccs[1].setCc(11);
		module->updateMapLen();
		REQUIRE(module->mapLen == 3); // Two mappings + empty slot
	}

	SECTION("Map length shrinks when mappings removed") {
		module->ccs[0].setCc(10);
		module->ccs[1].setCc(11);
		module->ccs[2].setCc(12);
		module->updateMapLen();
		
		module->ccs[2].reset();
		module->updateMapLen();
		REQUIRE(module->mapLen == 3); // Two mappings remain + empty slot
		
		module->ccs[1].reset();
		module->updateMapLen();
		REQUIRE(module->mapLen == 2); // One mapping + empty slot
	}

	delete module;
}
*/