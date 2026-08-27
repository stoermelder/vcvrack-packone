#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "../../test/test_mock.hpp"
#include "MidiCat.hpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiCat;

// A UiAccess mock that records saveDialog and openDialog calls and returns scripted answers.
struct MockUiAccess : StoermelderPackOne::vcv::UiAccess {
	struct SaveCall { std::string filters, dir, filename; };
	std::vector<SaveCall> saveCalls;
	std::vector<std::string> saveResults;  // queue consumed in order
	int saveIndex = 0;

	struct OpenCall { std::string filters, dir; };
	std::vector<OpenCall> openCalls;
	std::vector<std::string> openResults;  // queue consumed in order
	int openIndex = 0;

	struct Message { vcv::MessageType type; vcv::MessageButtons buttons; std::string msg; };
	std::vector<Message> messages;

	std::string saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) override {
		saveCalls.push_back({filters, dir, filename});
		if (saveIndex < (int) saveResults.size()) return saveResults[saveIndex++];
		return "";
	}

	std::string openDialog(const std::string& filters, const std::string& dir) override {
		openCalls.push_back({filters, dir});
		if (openIndex < (int) openResults.size()) return openResults[openIndex++];
		return "";  // cancelled by default
	}

	bool message(vcv::MessageType type, vcv::MessageButtons buttons, const std::string& msg) override {
		messages.push_back({type, buttons, msg});
		return true;
	}
};

// A FileAccess mock that records read()/write() calls and returns scripted content.
struct MockFileAccess : StoermelderPackOne::vcv::FileAccess {
	struct ReadCall { std::string path; };
	std::vector<ReadCall> reads;
	mutable std::vector<std::string> readResults;  // queue consumed in order
	mutable int readIndex = 0;

	struct WriteCall { std::string path, data; };
	std::vector<WriteCall> writes;
	bool failWrites = false;

	bool read(const std::string& path, std::string& data) const override {
		const_cast<MockFileAccess*>(this)->reads.push_back({path});
		if (readIndex < (int) readResults.size()) {
			data = readResults[readIndex++];
			return true;
		}
		return false;
	}

	bool write(const std::string& path, const std::string& data) override {
		if (failWrites) return false;
		writes.push_back({path, data});
		return true;
	}
};

// A recording HistoryAccess. Owns the actions it records (push takes ownership).
struct MockHistoryAccess : StoermelderPackOne::vcv::HistoryAccess {
	std::vector<::rack::history::Action*> pushed;
	void push(::rack::history::Action* a) override { pushed.push_back(a); }
	~MockHistoryAccess() { for (auto* a : pushed) delete a; }
};


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
		if (module->midiInputState.valuesCc[i] != -1 || module->midiInputState.valuesNote[i] != -1)
			v.push_back(i);
		}
		REQUIRE(v.size() == 0); // No values should be different
	}

	SECTION("All CC and note adapters are unassigned") {
		std::vector<int> v;
		for (int i = 0; i < MAX_CHANNELS; i++) {
			if (module->slots[i].cc.getCc() != -1 || module->slots[i].note.getNote() != -1)
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

TEST_CASE("JSON round-trip preserves state", "[MidiCat][JSON]") {
	MidiCatModule* m = Test::createModule<MidiCatModule>("MidiCat");

	SECTION("Top-level scalars round-trip") {
		m->panelTheme = 1;
		m->textScrolling = true;
		m->mappingIndicatorHidden = true;
		m->mappingIndicatorColor = color::fromHexString("#ff8800");
		m->locked = true;
		m->processDivision = 7;
		m->overlayEnabled = true;
		m->clearMapsOnLoad = true;
		m->parameterChangesDirect = true;
		m->midiResendPeriodically = true;
		m->midiIgnoreDevices = true;

		json_t* j = m->dataToJson();

		MidiCatModule* m2 = Test::createModule<MidiCatModule>("MidiCat");
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->textScrolling == true);
		REQUIRE(m2->mappingIndicatorHidden == true);
		REQUIRE(color::toHexString(m2->mappingIndicatorColor) == color::toHexString(m->mappingIndicatorColor));
		REQUIRE(m2->locked == true);
		REQUIRE(m2->processDivision == 7);
		REQUIRE(m2->overlayEnabled == true);
		REQUIRE(m2->clearMapsOnLoad == true);
		REQUIRE(m2->parameterChangesDirect == true);
		REQUIRE(m2->midiResendPeriodically == true);
		REQUIRE(m2->midiIgnoreDevices == true);

		Test::destroyModule(m2);
	}

	SECTION("Mapping slots (maps array) round-trip") {
		// A registered target module so moduleId/paramId resolve during dataFromJson
		TestModule* target = new TestModule();
		Test::registerModule(target);

		// Slot 0: a full set of distinctive values across every serialized field
		m->slots[0].cc.setCc(10);
		m->slots[0].cc.ccMode = CCMODE::TOGGLE;
		m->slots[0].setCc14bit(true);
		m->slots[0].note.setNote(60);
		m->slots[0].note.noteMode = NOTEMODE::TOGGLE;
		m->slots[0].label = "Volume";
		m->slots[0].midiOptions = 1;
		m->learnParam(0, target->id, TestModule::TEST_PARAM_1);
		// learnParam() resets the per-slot param settings, so set them afterwards
		m->slots[0].param.setSlew(0.5f);
		m->slots[0].param.setMin(0.1f);
		m->slots[0].param.setMax(0.9f);
		m->slots[0].param.setCurve(0.3f);
		m->slots[0].param.clockMode = MidiCatParam::CLOCKMODE::ARM;
		m->slots[0].param.clockSource = 2;
		m->slots[0].param.lightFirstId = 3;
		m->slots[0].param.lightNumColors = 4;

		// Slot 3: a second active slot with different values
		m->slots[3].cc.setCc(11);
		m->slots[3].cc.ccMode = CCMODE::PICKUP1;
		m->slots[3].note.setNote(72);
		m->slots[3].note.noteMode = NOTEMODE::MOMENTARY;
		m->slots[3].label = "Mod Wheel";
		m->slots[3].midiOptions = 2;
		m->learnParam(3, target->id, TestModule::TEST_PARAM_2);
		// learnParam() resets the per-slot param settings, so set them afterwards
		m->slots[3].param.setSlew(0.25f);
		m->slots[3].param.setMin(0.2f);
		m->slots[3].param.setMax(0.8f);
		m->slots[3].param.setCurve(0.6f);
		m->slots[3].param.clockMode = MidiCatParam::CLOCKMODE::ARM_DEFERRED_FEEDBACK;
		m->slots[3].param.clockSource = 1;
		m->slots[3].param.lightFirstId = 5;
		m->slots[3].param.lightNumColors = 6;

		m->updateMapLen(); // mapLen == 5 (slots 0..3 active + trailing empty slot)

		json_t* j = m->dataToJson();

		// The maps array must contain exactly mapLen entries
		json_t* mapsJ = json_object_get(j, "maps");
		REQUIRE(mapsJ != nullptr);
		REQUIRE(json_array_size(mapsJ) == (size_t) m->mapLen);

		// Release m's param-handle claims so m2 can take them during dataFromJson():
		// updateParamHandle_NoLock() with overwrite=false resets the new handle when
		// another handle (still held by m) claims the same (moduleId, paramId). This
		// mirrors a real preset load, where the old module instance is destroyed first.
		m->clearMaps_WithLock();

		MidiCatModule* m2 = Test::createModule<MidiCatModule>("MidiCat");
		m2->dataFromJson(j);
		json_decref(j);

		// Slot 0 — every serialized field
		REQUIRE(m2->slots[0].cc.getCc() == 10);
		REQUIRE(m2->slots[0].cc.ccMode == CCMODE::TOGGLE);
		REQUIRE(m2->slots[0].cc.get14bit() == true);
		REQUIRE(m2->slots[0].note.getNote() == 60);
		REQUIRE(m2->slots[0].note.noteMode == NOTEMODE::TOGGLE);
		REQUIRE(m2->paramHandles[0].moduleId == target->id);
		REQUIRE(m2->paramHandles[0].paramId == TestModule::TEST_PARAM_1);
		REQUIRE(m2->slots[0].label == "Volume");
		REQUIRE(m2->slots[0].midiOptions == 1);
		REQUIRE(m2->slots[0].param.getSlew() == Catch::Approx(0.5f));
		REQUIRE(m2->slots[0].param.getMin() == Catch::Approx(0.1f));
		REQUIRE(m2->slots[0].param.getMax() == Catch::Approx(0.9f));
		REQUIRE(m2->slots[0].param.getCurve() == Catch::Approx(0.3f));
		REQUIRE(m2->slots[0].param.clockMode == MidiCatParam::CLOCKMODE::ARM);
		REQUIRE(m2->slots[0].param.clockSource == 2);
		REQUIRE(m2->slots[0].param.lightFirstId == 3);
		REQUIRE(m2->slots[0].param.lightNumColors == 4);

		// Slot 3 — every serialized field
		REQUIRE(m2->slots[3].cc.getCc() == 11);
		REQUIRE(m2->slots[3].cc.ccMode == CCMODE::PICKUP1);
		REQUIRE(m2->slots[3].note.getNote() == 72);
		REQUIRE(m2->slots[3].note.noteMode == NOTEMODE::MOMENTARY);
		REQUIRE(m2->slots[3].label == "Mod Wheel");
		REQUIRE(m2->slots[3].midiOptions == 2);
		REQUIRE(m2->slots[3].param.getSlew() == Catch::Approx(0.25f));
		REQUIRE(m2->slots[3].param.getMin() == Catch::Approx(0.2f));
		REQUIRE(m2->slots[3].param.getMax() == Catch::Approx(0.8f));
		REQUIRE(m2->slots[3].param.getCurve() == Catch::Approx(0.6f));
		REQUIRE(m2->slots[3].param.clockMode == MidiCatParam::CLOCKMODE::ARM_DEFERRED_FEEDBACK);
		REQUIRE(m2->slots[3].param.clockSource == 1);
		REQUIRE(m2->slots[3].param.lightFirstId == 5);
		REQUIRE(m2->slots[3].param.lightNumColors == 6);

		Test::destroyModule(m2);
		Test::unregisterModule(target);
		delete target;
	}

	SECTION("MIDI I/O (midiInput/midiOutput) round-trip") {
		m->midiInput.channel = 5;
		m->midiOutput.channel = 7;

		json_t* j = m->dataToJson();

		MidiCatModule* m2 = Test::createModule<MidiCatModule>("MidiCat");
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->midiInput.channel == 5);
		REQUIRE(m2->midiOutput.channel == 7);

		Test::destroyModule(m2);
	}

	Test::destroyModule(m);
}


TEST_CASE("MIDI learning functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1); // Process every sample for testing
	int j = 1;

	SECTION("Learning CC assigns to correct channel") {
		module->enableLearn(0, true);		
		module->midiCc(Test::makeMidiMessage(0xb, 0, 10, 64));
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->slots[0].cc.getCc() == 10);
		REQUIRE(module->slots[0].cc.ccMode == CCMODE::DIRECT);
		REQUIRE(module->slots[0].note.getNote() == -1); // Note should be unassigned
	}

	SECTION("Learning note assigns to correct channel") {
		module->enableLearn(0, true);
		module->midiNotePress(Test::makeMidiMessage(0x9, 0, 60, 100));
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->slots[0].note.getNote() == 60);
		REQUIRE(module->slots[0].note.noteMode == NOTEMODE::MOMENTARY);
		REQUIRE(module->slots[0].cc.getCc() == -1); // CC should be unassigned
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
			if (module->slots[i].cc.getCc() != i || module->slots[i].note.getNote() != -1) {
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
		uint64_t tsBefore = module->midiInputState.ts;	
		module->process(Test::makeProcessArgs(0));		
		REQUIRE(module->midiInputState.ts == tsBefore + 1);
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
		module->slots[0].cc.ccMode = CCMODE::DIRECT;
		module->process(Test::makeProcessArgs(1));
		
		// Send MIDI
		module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
		module->process(Test::makeProcessArgs(2));
		
		// Verify CC was processed
		REQUIRE(module->midiInputState.valuesCc[7] == 100);

		// Verify parameter was updated
		ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
		REQUIRE(pq->getValue() == Catch::Approx(100.0f / 127.0f).margin(0.01f));

		Test::unregisterModule(testModule);
	}

	Test::destroyModule(module);
}


TEST_CASE("processBypass drains the MIDI queue without updating mappings", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	// Set up a CC7 -> param mapping.
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64)); // Learn CC7
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->slots[0].cc.ccMode = CCMODE::DIRECT;
	module->process(Test::makeProcessArgs(1));

	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	float valueBefore = pq->getValue();

	// Queue a new CC value; a normal process() would update valuesCc and the param.
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));

	module->processBypass(Test::makeProcessArgs(2));

	REQUIRE(module->midiInputState.valuesCc[7] == 64);
	REQUIRE(pq->getValue() == Catch::Approx(valueBefore));

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}


TEST_CASE("CC basic processing", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");

	SECTION("Message updates internal state") {
		bool result = module->midiCc(Test::makeMidiMessage(0xb, 0, 7, 64)); // CC7, value 64
		REQUIRE(result == true); // First message should trigger update
		REQUIRE(module->midiInputState.valuesCc[7] == 64);
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
		REQUIRE(module->midiInputState.valuesCc[1] == 50);
		REQUIRE(module->midiInputState.valuesCc[2] == 75);
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
	module->slots[0].cc.ccMode = CCMODE::DIRECT;

	// Send CC value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	module->process(Test::makeProcessArgs(i++));
	// Check that CC was received
	REQUIRE(module->midiInputState.valuesCc[7] == 64);	
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
		module->slots[0].cc.ccMode = CCMODE::DIRECT;
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
		module->slots[0].cc.ccMode = CCMODE::DIRECT;
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

TEST_CASE("ccModeOverride forces DIRECT mode temporarily", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up a TOGGLE-mode mapping
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 127)); // Initialize CC state
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0)); // Initialize CC state
	module->process(Test::makeProcessArgs(i++));
	module->slots[0].cc.ccMode = CCMODE::TOGGLE;

	// While the override is held (Shift+Ctrl+I), the slot behaves as DIRECT:
	// the parameter follows the CC value and the toggle ladder is not advanced.
	module->ccModeOverride = true;
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 64));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == Catch::Approx(64.0f / 127.0f).margin(0.1f));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::IDLE);

	// Releasing the override restores the configured TOGGLE mode.
	module->ccModeOverride = false;
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 127));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);
	REQUIRE(pq->getValue() == pq->getMaxValue());

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
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
	module->slots[0].cc.ccMode = CCMODE::TOGGLE;

	// First toggle: should go to max
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 127));
	module->process(Test::makeProcessArgs(i++));
	// Check internal state progressed
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);

	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0));
	module->process(Test::makeProcessArgs(i++));
	// Check internal state progressed
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::RELEASED);

	// Check parameter updated
	REQUIRE(pq->getValue() == pq->getMaxValue());
	
	// Second toggle: should go to min
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 127));
	module->process(Test::makeProcessArgs(i++));
	// Check state wrapped around
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::REPRESSED);

	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0));
	module->process(Test::makeProcessArgs(i++));
	// Check state wrapped around
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::IDLE);

	// Check parameter updated
	REQUIRE(pq->getValue() == pq->getMinValue());

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("CC Mode TOGGLE_VALUE", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_2);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));
	
	// Set up mapping
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0)); // Initialize CC state
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_2);
	module->process(Test::makeProcessArgs(i++));
	module->slots[0].cc.ccMode = CCMODE::TOGGLE_VALUE;

	// First non-zero value: parameter takes the CC value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 64));
	module->process(Test::makeProcessArgs(i++));
	// Check internal state progressed
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);
	REQUIRE(pq->getValue() == 64.f);

	// Zero: parameter holds its value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::RELEASED);
	REQUIRE(pq->getValue() == 64.f);

	// Second non-zero value: parameter switches off regardless of the CC value
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 32));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::REPRESSED);
	REQUIRE(pq->getValue() == pq->getMinValue());

	// Zero: parameter stays off, ladder wrapped around
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 10, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::IDLE);
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
	module->slots[0].cc.ccMode = CCMODE::PICKUP1;

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
	module->slots[0].cc.ccMode = CCMODE::PICKUP2;

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
	module->slots[0].cc.ccMode = CCMODE::SNAPPED;
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
	module->slots[0].cc.ccMode = CCMODE::SNAPPED_SL;

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
		module->slots[0].setCc(7); // CC 7 is MSB
		module->slots[0].setCc14bit(true);
		
		// Send MSB (CC7)
		module->midiProcessMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
		module->slots[0].cc.process(module->midiInputState);
		// Send LSB (CC39 = CC7 + 32)
		module->midiProcessMessage(Test::makeMidiMessage(0xb, 0, 39, 32));
		module->slots[0].cc.process(module->midiInputState);
		// Process to combine values
		module->slots[0].cc.process(module->midiInputState);
		// 14-bit value = MSB * 128 + LSB = 64 * 128 + 32 = 8224
		REQUIRE(module->slots[0].cc.getValue() == 8224);
	}

	Test::destroyModule(module);
}


TEST_CASE("Note basic processing", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	int i = 1;

	SECTION("Message updates internal state") {
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100));	// Note 60, velocity 100
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(module->midiInputState.valuesNote[60] == 100);
	}

	SECTION("Note release sets velocity to 0") {
		// First press the note, Note 60, velocity 100
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
		module->process(Test::makeProcessArgs(i++));
		// Then release it, Note 60 release
		module->midiProcessMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(module->midiInputState.valuesNote[60] == 0);
	}

	SECTION("Multiple note presses tracked independently") {
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Note 60, velocity 100
		module->process(Test::makeProcessArgs(i++));
		module->midiProcessMessage(Test::makeMidiMessage(0x9, 0, 62, 127)); // Note 62, velocity 127
		module->process(Test::makeProcessArgs(i++));
		REQUIRE(module->midiInputState.valuesNote[60] == 100);
		REQUIRE(module->midiInputState.valuesNote[62] == 127);	
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
	module->slots[0].note.noteMode = NOTEMODE::MOMENTARY;
	
	// Press note
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	// Check parameter went high
	REQUIRE(module->midiInputState.valuesNote[60] == 100);
	
	// Release note
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	// Check parameter went low
	REQUIRE(module->midiInputState.valuesNote[60] == 0);

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("Note Mode MOMENTARY_VEL", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping (learn note 60 while pressed)
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Learn note 60
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->slots[0].note.noteMode = NOTEMODE::MOMENTARY_VEL;
	// Settle: release so the adapter tracks 0 before starting the momentary cycle
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));

	// Press note with velocity 80: parameter takes the velocity through, not clamped to 127.
	// This is the one-line difference from MOMENTARY and the regression this test guards.
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 80));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == Catch::Approx(80.0f / 127.0f).margin(0.01f));

	// Release note: parameter returns to min
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == pq->getMinValue());

	// Press note with a different velocity: parameter tracks the new velocity
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 40));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(pq->getValue() == Catch::Approx(40.0f / 127.0f).margin(0.01f));

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("Note Mode TOGGLE", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping (learn note 60 while pressed)
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Learn note 60
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->slots[0].note.noteMode = NOTEMODE::TOGGLE;
	// Settle: release so the adapter tracks 0 before starting the toggle cycle
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));

	// First press: should go to max
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	// Check internal state progressed
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);
	REQUIRE(pq->getValue() == pq->getMaxValue());

	// First release: parameter stays at max
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::RELEASED);
	REQUIRE(pq->getValue() == pq->getMaxValue());

	// Second press: should go to min
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::REPRESSED);
	REQUIRE(pq->getValue() == pq->getMinValue());

	// Second release: parameter stays at min, ladder wrapped around
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::IDLE);
	REQUIRE(pq->getValue() == pq->getMinValue());

	Test::unregisterModule(testModule);
	Test::destroyModule(module);
}

TEST_CASE("Note Mode TOGGLE_VEL", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);
	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_1);
	int i = 1;
	module->process(Test::makeProcessArgs(i++));

	// Set up mapping (learn note 60 while pressed)
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100)); // Learn note 60
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_1);
	module->process(Test::makeProcessArgs(i++));
	module->slots[0].note.noteMode = NOTEMODE::TOGGLE_VEL;
	// Settle: release so the adapter tracks 0 before starting the toggle cycle
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));

	// First press: parameter takes the note-on velocity (80, not the learn-time 100)
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 80));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);
	REQUIRE(pq->getValue() == Catch::Approx(80.0f / 127.0f).margin(0.01f));

	// First release: parameter holds the velocity value
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::RELEASED);
	REQUIRE(pq->getValue() == Catch::Approx(80.0f / 127.0f).margin(0.01f));

	// Second press: parameter switches off regardless of velocity
	module->midiInput.onMessage(Test::makeMidiMessage(0x9, 0, 60, 80));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::REPRESSED);
	REQUIRE(pq->getValue() == pq->getMinValue());

	// Second release: parameter stays at min, ladder wrapped around
	module->midiInput.onMessage(Test::makeMidiMessage(0x8, 0, 60, 0));
	module->process(Test::makeProcessArgs(i++));
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::IDLE);
	REQUIRE(pq->getValue() == pq->getMinValue());

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
	module->slots[0].note.noteMode = NOTEMODE::SNAPPED;
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
	module->slots[0].note.noteMode = NOTEMODE::SNAPPED_SL;
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
	module->slots[0].cc.ccMode = CCMODE::DIRECT;

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

TEST_CASE("MIDI feedback does not overwrite a param after midiReset before new MIDI arrives", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->processDivider.setDivision(1);
	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	// Learn CC7 -> TEST_PARAM_2 (range 0..127) in DIRECT mode and receive one MIDI
	// message, so the slot's MidiCatParam caches a value and starts driving the
	// parameter on every process() call via param.process().
	module->enableLearn(0, true);
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 0)); // initial CC
	module->learnParam(0, testModule->id, TestModule::TEST_PARAM_2);
	module->slots[0].cc.ccMode = CCMODE::DIRECT;
	module->process(Test::makeProcessArgs(1));
	module->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 63));
	module->process(Test::makeProcessArgs(2));

	ParamQuantity* pq = testModule->getParamQuantity(TestModule::TEST_PARAM_2);
	REQUIRE(pq->getValue() == Catch::Approx(63.f));

	// A system MIDI reset (Shift+Ctrl+R, or an incoming MIDI reset message) clears
	// tracker.lastValue back to -1 and detached back to false on every mapped slot,
	// without touching the mapping or the parameter itself.
	module->midiReset();
	REQUIRE(module->slots[0].tracker.lastValue == -1);
	REQUIRE(module->slots[0].tracker.detached == false);

	// Now the user edits the parameter manually, before any new MIDI arrives. The first
	// process() call reads this back through MidiCatParam::getValue(), which self-heals
	// from the live parameter -- so it takes a second edit + process() cycle to expose
	// the lagged param.process() write-back that actually clobbers the parameter.
	pq->setValue(20.f);
	module->process(Test::makeProcessArgs(3));
	pq->setValue(30.f);
	module->process(Test::makeProcessArgs(4));

	// The manual edit must stick: with tracker.lastValue < 0 and detached == false,
	// MIDI-CAT has no valid tracked value to write back and must not clobber it.
	REQUIRE(pq->getValue() == Catch::Approx(30.f));

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
	module->slots[0].setCc(10);
	module->slots[0].setNote(60);
	module->slots[0].label = "Test Label";
	
	SECTION("Clear specific map resets all values") {
		module->clearMap(0);
		
		REQUIRE(module->slots[0].cc.getCc() == -1);
		REQUIRE(module->slots[0].note.getNote() == -1);
		REQUIRE(module->slots[0].label == "");
	}

	SECTION("Clear all maps resets module state") {
		module->clearMaps_NoLock();
		
		REQUIRE(module->learningId == -1);
		REQUIRE(module->mapLen == 1);
		
		for (int i = 0; i < MAX_CHANNELS; i++) {
			REQUIRE(module->slots[i].cc.getCc() == -1);
			REQUIRE(module->slots[i].note.getNote() == -1);
		}
	}

	delete module;
}

TEST_CASE("MidiCcAdapter functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->process({});

	SECTION("CC adapter processes new values") {
		module->slots[0].setCc(10);
		
		// Send a CC message
		auto ccMsg = makeMidiMessage(0xb, 0, 10, 50);
		module->midiCc(ccMsg);
		
		// Process should detect change
		bool changed = module->slots[0].cc.process(module->midiInputState);
		REQUIRE(changed == true);
		REQUIRE(module->slots[0].cc.getValue() == 50);
	}

	SECTION("CC adapter ignores unchanged values") {
		module->slots[0].setCc(10);
		
		auto ccMsg = makeMidiMessage(0xb, 0, 10, 50);
		module->midiCc(ccMsg);
		module->slots[0].cc.process(module->midiInputState);
		
		// Process again without new message
		bool changed = module->slots[0].cc.process(module->midiInputState);
		REQUIRE(changed == false);
	}

	SECTION("Reset clears CC assignment") {
		module->slots[0].setCc(10);
		module->slots[0].cc.reset();
		
		REQUIRE(module->slots[0].cc.getCc() == -1);
		REQUIRE(module->slots[0].cc.current == -1);
	}

	delete module;
}

TEST_CASE("MidiNoteAdapter functionality", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	module->process({});

	SECTION("Note adapter processes new values") {
		module->slots[0].setNote(60);
		
		// Send a note message
		auto noteMsg = makeMidiMessage(0x9, 0, 60, 100);
		module->midiNotePress(noteMsg);
		
		// Process should detect change
		bool changed = module->slots[0].note.process(module->midiInputState);
		REQUIRE(changed == true);
		REQUIRE(module->slots[0].note.getValue() == 100);
	}

	SECTION("Note adapter tracks releases") {
		module->slots[0].setNote(60);
		
		// Press
		auto noteOn = makeMidiMessage(0x9, 0, 60, 100);
		module->midiNotePress(noteOn);
		module->slots[0].note.process(module->midiInputState);
		
		module->process({});

		// Release
		auto noteOff = makeMidiMessage(0x8, 0, 60, 0);
		module->midiNoteRelease(noteOff);
		bool changed = module->slots[0].note.process(module->midiInputState);
		
		REQUIRE(changed == true);
		REQUIRE(module->slots[0].note.getValue() == 0);
	}

	delete module;
}

TEST_CASE("Map length management", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	
	SECTION("Map length updates when mappings added") {
		REQUIRE(module->mapLen == 1); // Default with one empty slot
		
		module->slots[0].setCc(10);
		module->updateMapLen();
		REQUIRE(module->mapLen == 2); // One mapping + empty slot
		
		module->slots[1].setCc(11);
		module->updateMapLen();
		REQUIRE(module->mapLen == 3); // Two mappings + empty slot
	}

	SECTION("Map length shrinks when mappings removed") {
		module->slots[0].setCc(10);
		module->slots[1].setCc(11);
		module->slots[2].setCc(12);
		module->updateMapLen();
		
		module->slots[2].cc.reset();
		module->updateMapLen();
		REQUIRE(module->mapLen == 3); // Two mappings remain + empty slot
		
		module->slots[1].cc.reset();
		module->updateMapLen();
		REQUIRE(module->mapLen == 2); // One mapping + empty slot
	}

	delete module;
}
*/



// A real preset file exported by the Core "MIDI-Map" module (verbatim, as saved).
static const char MIDI_MAP_PRESET[] = R"JSON(
{
  "plugin": "Core",
  "model": "MIDI-Map",
  "version": "2.6.6",
  "params": [],
  "data": {
    "maps": [
      {
        "cc": 2,
        "moduleId": 5211596373228541,
        "paramId": 1
      },
      {
        "cc": 1,
        "moduleId": 5211596373228541,
        "paramId": 2
      },
      {
        "cc": -1,
        "moduleId": -1,
        "paramId": 0
      }
    ],
    "smooth": true,
    "midi": {
      "driver": -12,
      "deviceName": "Loopback 3",
      "channel": 3
    }
  }
}
)JSON";

TEST_CASE("loadMidiMapPreset end-to-end reads, parses, and applies the preset", "[MidiCat][ui]") {
	auto mock = Test::makeMockVcv<MockUiAccess, MockFileAccess, MockHistoryAccess>();
	auto module = Test::createModule<MidiCatModule>("MidiCat");
	auto widget = Test::createWidget<MidiCatBaseWidget>(module);

	SECTION("Cancelled dialog reads nothing") {
		// openDialog returns "" (cancelled) → loadMidiMapPreset_dialog returns early.
		widget->loadMidiMapPreset_dialog();

		REQUIRE(mock.ui.openCalls.size() == 1);
		CHECK(mock.ui.openCalls[0].filters == PRESET_FILTERS);
		CHECK(mock.ui.openCalls[0].dir == "");
		REQUIRE(mock.fs.reads.empty());
		REQUIRE(mock.history.pushed.empty());
	}

	SECTION("Selected path is read, parsed, and applied to the module") {
		// Script the dialog to return a path and the fs to return a valid MIDI-MAP preset.
		mock.ui.openResults = { "/tmp/MidiCat.vcvm" };
		mock.fs.readResults = {
			R"({"plugin":"Core","model":"MIDI-Map","data":{)"
			R"("maps":[{"cc":7,"ccMode":0,"cc14bit":false,"note":-1,"noteMode":0,)"
			R"("moduleId":-1,"paramId":0,"label":"","midiOptions":0,"slew":0.0,)"
			R"("min":0.0,"max":1.0,"curve":1.0,"clockMode":0,"clockSource":0,)"
			R"("lightFirstId":-1,"lightNumColors":0}],)"
			R"("midi":{"channels":[0]},"midiOutput":{"channels":[0]}}})"
		};

		widget->loadMidiMapPreset_dialog();

		// Dialog routed through the UI layer.
		REQUIRE(mock.ui.openCalls.size() == 1);
		CHECK(mock.ui.openCalls[0].filters == PRESET_FILTERS);
		// File read routed through the fs layer.
		REQUIRE(mock.fs.reads.size() == 1);
		CHECK(mock.fs.reads[0].path == "/tmp/MidiCat.vcvm");
		// The preset was parsed and applied: CC7 is now mapped on slot 0.
		REQUIRE(module->slots[0].cc.getCc() == 7);
		REQUIRE(module->slots[0].cc.ccMode == CCMODE::DIRECT);
		REQUIRE(module->slots[0].note.getNote() == -1);
		// The load was recorded as an undoable history action.
		REQUIRE(mock.history.pushed.size() == 1);
	}

	SECTION("Real MIDI-Map preset is imported") {
		// A real preset exported by the Core "MIDI-Map" module — a different module
		// than Midi-Cat. loadMidiMapPreset_convert rewrites plugin/model to Midi-Cat
		// and copies data.midi → data.midiInput before fromJson applies it.
		mock.ui.openResults = { "/tmp/Untitled.vcvm" };
		mock.fs.readResults = { MIDI_MAP_PRESET };

		widget->loadMidiMapPreset_dialog();

		REQUIRE(mock.ui.openCalls.size() == 1);
		REQUIRE(mock.fs.reads.size() == 1);
		CHECK(mock.fs.reads[0].path == "/tmp/Untitled.vcvm");
		// Maps from the MIDI-Map preset are applied.
		REQUIRE(module->slots[0].cc.getCc() == 2);
		REQUIRE(module->slots[0].cc.ccMode == CCMODE::DIRECT);
		REQUIRE(module->slots[0].note.getNote() == -1);
		REQUIRE(module->slots[1].cc.getCc() == 1);
		REQUIRE(module->slots[1].cc.ccMode == CCMODE::DIRECT);
		REQUIRE(module->slots[1].note.getNote() == -1);
		REQUIRE(module->slots[2].cc.getCc() == -1);
		// Param handles are bound to the preset's module ids (module not in engine → NULL).
		REQUIRE(module->paramHandles[0].moduleId == 5211596373228541);
		REQUIRE(module->paramHandles[0].paramId == 1);
		REQUIRE(module->paramHandles[1].moduleId == 5211596373228541);
		REQUIRE(module->paramHandles[1].paramId == 2);
		// mapLen covers the two real maps plus the trailing empty "Mapping..." slot.
		REQUIRE(module->mapLen == 3);
		// data.midi was copied to midiInput; channel applied, driver unavailable headless.
		REQUIRE(module->midiInput.getChannel() == 3);
		REQUIRE(module->midiInput.getDriverId() == -1);
		REQUIRE(mock.history.pushed.size() == 1);
	}

	SECTION("Corrupt JSON shows a warning and applies nothing") {
		mock.ui.openResults = { "/tmp/MidiCat.vcvm" };
		mock.fs.readResults = { "not valid json" };

		widget->loadMidiMapPreset_dialog();

		REQUIRE(mock.ui.openCalls.size() == 1);
		REQUIRE(mock.fs.reads.size() == 1);
		// A warning dialog was raised through the UI layer.
		REQUIRE(mock.ui.messages.size() == 1);
		CHECK(mock.ui.messages[0].type == vcv::MessageType::WARNING);
		CHECK(mock.ui.messages[0].msg.find("JSON parsing error") != std::string::npos);
		// Nothing was applied and no history action was pushed.
		REQUIRE(module->slots[0].cc.getCc() == -1);
		REQUIRE(mock.history.pushed.empty());
	}

	Test::destroyWidget(widget);
	Test::destroyModule(module);
}

// moduleBind()/moduleBindExpander() call APP->engine->updateParamHandle(), which takes the
// engine's write lock (a non-recursive pthread_rwlock). They must be called from the test
// thread directly — NOT from inside process() or while holding the engine lock — or the
// same thread would try to re-acquire the write lock and deadlock.
//
// Cleanup must also be exception-safe: a REQUIRE failure skips the trailing cleanup, and a
// leaked registered module stays in the engine. In this headless test the RNG is unseeded,
// so random::u64() returns 0 and every `new TestModule()` (id -1) is assigned id 0 by
// addModule_NoLock; a leaked id-0 module then makes the next registration spin forever in
// addModule_NoLock's id-collision loop. ScopedModules below unregisters+destroys even on
// throw, and the TestModules get explicit ids so they never take the random-id path.
struct ScopedModules {
	std::vector<rack::Module*> mods;
	~ScopedModules() {
		for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
			Test::unregisterModule(*it);
			Test::destroyModule(*it);
		}
	}
};

TEST_CASE("moduleBind binds a module's parameters to the mapping slots", "[MidiCat]") {
	ScopedModules cleanup;
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	TestModule* testModule = new TestModule();
	testModule->id = Test::getModuleId();
	Test::registerModule(module);
	Test::registerModule(testModule);
	cleanup.mods.push_back(module);
	cleanup.mods.push_back(testModule);

	SECTION("BIND_CLEAR clears MIDI bindings and rebinds all params") {
		// Set up an existing CC binding on slot 0.
		module->slots[0].bindCc(7);
		module->updateMapLen();
		REQUIRE(module->slots[0].cc.getCc() == 7);

		// moduleBind runs with learningId == -1 (clearMaps_WithLock resets it). This is
		// the path that used to index slots[-1] in learnParam.
		module->moduleBind(testModule, false);

		// All 5 params are bound to the module.
		for (int i = 0; i < TestModule::NUM_PARAMS; i++) {
			REQUIRE(module->paramHandles[i].module == testModule);
			REQUIRE(module->paramHandles[i].paramId == i);
		}
		// MIDI bindings were cleared (keepCcAndNote=false).
		REQUIRE(module->slots[0].cc.getCc() == -1);
		// mapLen covers the 5 bound slots plus the trailing empty slot.
		REQUIRE(module->mapLen == TestModule::NUM_PARAMS + 1);
	}

	SECTION("BIND_KEEP keeps MIDI bindings and rebinds all params") {
		// Set up an existing CC binding on slot 0.
		module->slots[0].bindCc(7);
		module->updateMapLen();
		REQUIRE(module->slots[0].cc.getCc() == 7);

		module->moduleBind(testModule, true);

		// All 5 params are bound to the module.
		for (int i = 0; i < TestModule::NUM_PARAMS; i++) {
			REQUIRE(module->paramHandles[i].module == testModule);
			REQUIRE(module->paramHandles[i].paramId == i);
		}
		// MIDI bindings were kept (keepCcAndNote=true).
		REQUIRE(module->slots[0].cc.getCc() == 7);
		REQUIRE(module->mapLen == TestModule::NUM_PARAMS + 1);
	}

	SECTION("Null module is a no-op") {
		REQUIRE_NOTHROW(module->moduleBind(nullptr, false));
		REQUIRE_NOTHROW(module->moduleBind(nullptr, true));
	}
}

TEST_CASE("moduleBindExpander binds the left expander's module", "[MidiCat]") {
	ScopedModules cleanup;
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");
	TestModule* testModule = new TestModule();
	testModule->id = Test::getModuleId();
	Test::registerModule(module);
	Test::registerModule(testModule);
	cleanup.mods.push_back(module);
	cleanup.mods.push_back(testModule);

	SECTION("Binds the left expander module (BIND_CLEAR)") {
		module->leftExpander.module = testModule;
		module->leftExpander.moduleId = testModule->id;

		module->moduleBindExpander(false);

		for (int i = 0; i < TestModule::NUM_PARAMS; i++) {
			REQUIRE(module->paramHandles[i].module == testModule);
			REQUIRE(module->paramHandles[i].paramId == i);
		}
		REQUIRE(module->mapLen == TestModule::NUM_PARAMS + 1);
	}

	SECTION("Binds the left expander module (BIND_KEEP)") {
		module->leftExpander.module = testModule;
		module->leftExpander.moduleId = testModule->id;

		module->moduleBindExpander(true);

		for (int i = 0; i < TestModule::NUM_PARAMS; i++) {
			REQUIRE(module->paramHandles[i].module == testModule);
			REQUIRE(module->paramHandles[i].paramId == i);
		}
		REQUIRE(module->mapLen == TestModule::NUM_PARAMS + 1);
	}

	SECTION("No left expander is a no-op") {
		REQUIRE_NOTHROW(module->moduleBindExpander(false));
		REQUIRE_NOTHROW(module->moduleBindExpander(true));
	}
}

// midiReset()/midiResendFeedback() are the Shift+Ctrl+R / Shift+Ctrl+F hotkeys and the
// "Reset" / "Re-send MIDI feedback" menu items. midiReset() changed meaning in the step-6
// refactor: it now calls tracker.reset(), which also resets the toggle ladder — nothing
// else checks that, so it is pinned here.
TEST_CASE("midiReset resets the toggle ladder and tracker state", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");

	// Drive a few slots' toggle ladders into each non-IDLE state and dirty the trackers.
	module->slots[0].tracker.toggle.advance(1);  // IDLE -> PRESSED
	module->slots[0].tracker.lastValue = 64;
	module->slots[0].tracker.detached = true;
	module->slots[1].tracker.toggle.advance(1);  // IDLE -> PRESSED
	module->slots[1].tracker.toggle.advance(0);  // PRESSED -> RELEASED
	module->slots[1].tracker.lastValue = 32;
	module->slots[1].tracker.detached = true;
	module->slots[2].tracker.toggle.advance(1);  // IDLE -> PRESSED
	module->slots[2].tracker.toggle.advance(0);  // PRESSED -> RELEASED
	module->slots[2].tracker.toggle.advance(1);  // RELEASED -> REPRESSED
	module->slots[2].tracker.lastValue = 100;
	module->slots[2].tracker.detached = true;

	// Sanity check the ladders are where we left them.
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);
	REQUIRE(module->slots[1].tracker.toggle.state == ToggleValueLadder::STATE::RELEASED);
	REQUIRE(module->slots[2].tracker.toggle.state == ToggleValueLadder::STATE::REPRESSED);

	module->midiReset();

	// tracker.reset() clears the ladder, the last value, and the detached flag on every slot.
	for (int i = 0; i < MAX_CHANNELS; i++) {
		REQUIRE(module->slots[i].tracker.toggle.state == ToggleValueLadder::STATE::IDLE);
		REQUIRE(module->slots[i].tracker.lastValue == -1);
		REQUIRE(module->slots[i].tracker.detached == false);
	}

	Test::destroyModule(module);
}

TEST_CASE("midiReset is triggered by a system reset MIDI message", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");

	// Drive slot 0's toggle ladder into a non-IDLE state.
	module->slots[0].tracker.toggle.advance(1);  // IDLE -> PRESSED
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);

	// A system reset message (status 0xF, channel 0xF) calls midiReset().
	module->midiProcessMessage(Test::makeMidiMessage(0xf, 0xf, 0, 0));

	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::IDLE);

	Test::destroyModule(module);
}

TEST_CASE("midiResendFeedback resets the feedback state", "[MidiCat]") {
	MidiCatModule* module = Test::createModule<MidiCatModule>("MidiCat");

	// Dirty the feedback state on a few slots.
	module->slots[0].lastValueOut = 64;
	module->slots[0].cc.current = 64;
	module->slots[0].note.current = 100;
	module->slots[1].lastValueOut = 32;
	module->slots[1].cc.current = 32;
	module->slots[1].note.current = 50;
	module->slots[MAX_CHANNELS - 1].lastValueOut = 127;
	module->slots[MAX_CHANNELS - 1].cc.current = 127;
	module->slots[MAX_CHANNELS - 1].note.current = 127;

	module->midiResendFeedback();

	// All slots' feedback state is reset so the next process() re-sends.
	for (int i = 0; i < MAX_CHANNELS; i++) {
		REQUIRE(module->slots[i].lastValueOut == -1);
		REQUIRE(module->slots[i].cc.current == -1);
		REQUIRE(module->slots[i].note.current == -1);
	}

	// Unlike midiReset(), midiResendFeedback() must NOT touch the toggle ladder.
	module->slots[0].tracker.toggle.advance(1);  // IDLE -> PRESSED
	module->midiResendFeedback();
	REQUIRE(module->slots[0].tracker.toggle.state == ToggleValueLadder::STATE::PRESSED);

	Test::destroyModule(module);
}