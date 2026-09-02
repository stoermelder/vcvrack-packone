#include "../../test/framework.hpp"
#include "TransitBase.hpp"
#include "Transit.cpp"
#include "TransitEx.cpp"
// NOTE: TransitEx.cpp is NOT included here to avoid duplicate definition of
// modelTransitEx (it is already exported from the plugin dylib and linked in).
// TransitExModule instances are created via the model factory and accessed
// through the TransitBase<12> interface.

using namespace StoermelderPackOne::Transit;

SYNC_MODEL(modelTransit, "Transit");
SYNC_MODEL(modelTransitEx, "TransitEx");
Test::TestContext<> testContext;


// Helper module with test parameters
struct TestModule : rack::Module {
	enum ParamIds {
		TEST_PARAM_1,
		TEST_PARAM_2,
		NUM_PARAMS
	};

	TestModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(TEST_PARAM_1, 0.f, 1.f, 0.5f, "Test Parameter 1");
		configParam(TEST_PARAM_2, 0.f, 10.f, 5.f, "Test Parameter 2");
	}
};

// ----------------------------------------------------------------
// Helper: create a TransitEx module via the plugin factory and set
// its sample-rate event, just like Test::createModule does.
// Returns the raw Module* pointer alongside a TransitBase<12>* view.
// ----------------------------------------------------------------
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

// Helper: wire transitModule -> exModule as right/left expanders and
// trigger a single process() so Transit discovers the expander.
static void connectExpander(TransitModule<12>* transit, Module* exModule) {
	transit->rightExpander.module = exModule;
	exModule->leftExpander.module = transit;
	transit->moduleChangedFlag = true;
	transit->process(Test::makeProcessArgs(0));
}

// Helper: wire two expanders in a chain: transit -> ex1 -> ex2
static void connectTwoExpanders(TransitModule<12>* transit, Module* ex1, Module* ex2) {
	transit->rightExpander.module = ex1;
	ex1->leftExpander.module = transit;
	ex1->rightExpander.module = ex2;
	ex2->leftExpander.module = ex1;
	transit->moduleChangedFlag = true;
	transit->process(Test::makeProcessArgs(0));
}

// Helper: disconnect the expander and re-process so Transit re-scans.
static void disconnectExpander(TransitModule<12>* transit) {
	transit->rightExpander.module = nullptr;
	transit->moduleChangedFlag = true;
	transit->process(Test::makeProcessArgs(1));
}



TEST_CASE("Construction and initialization", "[TransitEx]") {
	TransitExModule<12>* m = Test::createModule<TransitExModule<12>>("TransitEx");
	TransitExWidget<12>* mw = Test::createWidget<TransitExWidget<12>>("TransitEx");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[TransitEx][JSON]") {
	auto module = Test::createModule<TransitExModule<12>>("TransitEx");

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

TEST_CASE("JSON round-trip preserves state", "[TransitEx][JSON]") {
	Module* exModule = createExModule();
	TransitBase<12>* exBase = dynamic_cast<TransitBase<12>*>(exModule);
	REQUIRE(exBase != nullptr);

	// Populate some slots with test data
	exBase->presetSlotUsed[0] = true;
	exBase->preset[0] = {0.1f, 0.5f, 0.9f};
	exBase->textLabel[0] = "MyLabel";
	exBase->fadeTime[0] = 0.3f;

	exBase->presetSlotUsed[3] = true;
	exBase->preset[3] = {0.7f};
	exBase->fadeTime[3] = -1.f;

	// Serialize
	json_t* rootJ = exModule->dataToJson();
	REQUIRE(rootJ != nullptr);

	// Create a new expander and deserialize
	Module* exModule2 = createExModule();
	TransitBase<12>* exBase2 = dynamic_cast<TransitBase<12>*>(exModule2);
	REQUIRE(exBase2 != nullptr);
	exModule2->dataFromJson(rootJ);

	REQUIRE(exBase2->presetSlotUsed[0] == true);
	REQUIRE(exBase2->preset[0].size() == 3);
	REQUIRE(exBase2->preset[0][0] == Catch::Approx(0.1f).margin(0.001f));
	REQUIRE(exBase2->preset[0][1] == Catch::Approx(0.5f).margin(0.001f));
	REQUIRE(exBase2->preset[0][2] == Catch::Approx(0.9f).margin(0.001f));
	REQUIRE(exBase2->textLabel[0] == "MyLabel");
	REQUIRE(exBase2->fadeTime[0] == Catch::Approx(0.3f).margin(0.001f));
	REQUIRE(exBase2->presetSlotUsed[3] == true);
	REQUIRE(exBase2->preset[3].size() == 1);
	REQUIRE(exBase2->preset[3][0] == Catch::Approx(0.7f).margin(0.001f));

	for (int i = 1; i <= 2; i++) {
		REQUIRE(exBase2->presetSlotUsed[i] == false);
	}

	json_decref(rootJ);
	delete exModule2;
	delete exModule;
}


TEST_CASE("Transit discovers a connected TransitEx and updates presetTotal", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* exBase = nullptr;
	Module* exModule = createExModule(&exBase);
	Test::registerModule(exModule);

	SECTION("presetTotal is 12 with no expander") {
		transit->process(Test::makeProcessArgs(0));
		REQUIRE(transit->presetTotal == 12);
	}

	SECTION("presetTotal becomes 24 after connecting one expander") {
		connectExpander(transit, exModule);
		REQUIRE(transit->presetTotal == 24);
	}

	SECTION("Expander ctrlOffset is set to 1") {
		connectExpander(transit, exModule);
		REQUIRE(exBase->ctrlOffset == 1);
	}

	SECTION("Expander ctrlModuleId is set to Transit's id") {
		connectExpander(transit, exModule);
		REQUIRE(exBase->ctrlModuleId == transit->id);
	}

	SECTION("Expander ctrlUniqueId is synchronized with Transit") {
		connectExpander(transit, exModule);
		REQUIRE(exBase->ctrlUniqueId == transit->ctrlUniqueId);
	}

	SECTION("presetTotal drops back to 12 after disconnecting expander") {
		connectExpander(transit, exModule);
		REQUIRE(transit->presetTotal == 24);
		disconnectExpander(transit);
		REQUIRE(transit->presetTotal == 12);
	}

	Test::unregisterModule(exModule);
	delete exModule;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("Transit discovers two chained TransitEx expanders", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* ex1Base = nullptr;
	Module* ex1 = createExModule(&ex1Base);
	Test::registerModule(ex1);
	TransitBase<12>* ex2Base = nullptr;
	Module* ex2 = createExModule(&ex2Base);
	Test::registerModule(ex2);

	connectTwoExpanders(transit, ex1, ex2);

	SECTION("presetTotal is 36 with two expanders") {
		REQUIRE(transit->presetTotal == 36);
	}

	SECTION("First expander ctrlOffset is 1") {
		REQUIRE(ex1Base->ctrlOffset == 1);
	}

	SECTION("Second expander ctrlOffset is 2") {
		REQUIRE(ex2Base->ctrlOffset == 2);
	}

	SECTION("Both expanders ctrlModuleId points to Transit") {
		REQUIRE(ex1Base->ctrlModuleId == transit->id);
		REQUIRE(ex2Base->ctrlModuleId == transit->id);
	}

	Test::unregisterModule(ex2);
	delete ex2;
	Test::unregisterModule(ex1);
	delete ex1;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("getSlot returns expander slots for indices >= 12", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* exBase = nullptr;
	Module* exModule = createExModule(&exBase);
	Test::registerModule(exModule);

	connectExpander(transit, exModule);

	SECTION("getSlot(11) returns the last slot of Transit itself") {
		auto* slot = transit->getSlot(11);
		REQUIRE(slot != nullptr);
		REQUIRE(slot->owner == transit);
		REQUIRE(slot->index == 11);
	}

	SECTION("getSlot(12) returns the first slot of the expander") {
		auto* slot = transit->getSlot(12);
		REQUIRE(slot != nullptr);
		REQUIRE(slot->owner == exBase);
		REQUIRE(slot->index == 0);
	}

	SECTION("getSlot(23) returns the last slot of the expander") {
		auto* slot = transit->getSlot(23);
		REQUIRE(slot != nullptr);
		REQUIRE(slot->owner == exBase);
		REQUIRE(slot->index == 11);
	}

	SECTION("getSlot(24) returns null (out of range)") {
		REQUIRE(transit->getSlot(24) == nullptr);
	}

	Test::unregisterModule(exModule);
	delete exModule;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("Saving a preset to an expander slot stores data in the expander", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* exBase = nullptr;
	Module* exModule = createExModule(&exBase);
	Test::registerModule(exModule);

	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	transit->process(Test::makeProcessArgs(1));
	transit->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	transit->taskProcessorDsp.process();

	connectExpander(transit, exModule);

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.75f);

	SECTION("presetSave(12) marks expander slot 0 as used") {
		transit->presetSave(12);
		REQUIRE(exBase->presetSlotUsed[0] == true);
	}

	SECTION("presetSave(12) stores the correct parameter value in expander slot 0") {
		transit->presetSave(12);
		REQUIRE(exBase->preset[0].size() == 1);
		REQUIRE(exBase->preset[0][0] == Catch::Approx(0.75f).margin(0.001f));
	}

	SECTION("presetSave(12) sets active preset to 12") {
		transit->presetSave(12);
		REQUIRE(transit->preset == 12);
	}

	SECTION("presetSave in Transit slot and expander slot coexist independently") {
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.2f);
		transit->presetSave(5);
		testModule->params[TestModule::TEST_PARAM_1].setValue(0.75f);
		transit->presetSave(12);

		REQUIRE(transit->presetSlotUsed[5] == true);
		REQUIRE((*transit->getSlot(5)->getPreset())[0] == Catch::Approx(0.2f).margin(0.001f));
		REQUIRE(exBase->presetSlotUsed[0] == true);
		REQUIRE(exBase->preset[0][0] == Catch::Approx(0.75f).margin(0.001f));
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(exModule);
	delete exModule;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("Loading a preset from an expander slot transitions parameters correctly", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* exBase = nullptr;
	Module* exModule = createExModule(&exBase);
	Test::registerModule(exModule);

	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	transit->process(Test::makeProcessArgs(1));
	transit->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	transit->taskProcessorDsp.process();

	connectExpander(transit, exModule);

	// Save a preset in Transit slot 0 (value 0.0)
	testModule->params[TestModule::TEST_PARAM_1].setValue(0.0f);
	transit->presetSave(0);
	// Save a preset in expander slot 12 (value 1.0)
	testModule->params[TestModule::TEST_PARAM_1].setValue(1.0f);
	transit->presetSave(12);

	transit->params[TransitModule<12>::PARAM_FADE].setValue(0.0f);
	transit->presetSetLast(24); // extend boundary into expander range

	SECTION("Loading expander slot transitions parameter to its stored value") {
		transit->presetLoad(0);
		for (int i = 0; i < 1000; i++) {
			transit->process(Test::makeProcessArgs(i + 100));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));

		transit->presetLoad(12);
		for (int i = 0; i < 1000; i++) {
			transit->process(Test::makeProcessArgs(i + 1200));
		}
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(1.0f).margin(0.01f));
	}

	SECTION("presetLoad(12) sets transit->preset to 12") {		transit->presetSetLast(24);		transit->presetLoad(12);
		REQUIRE(transit->preset == 12);
	}

	SECTION("Alternating between Transit and expander presets converges correctly") {
		transit->presetSetLast(24);
		transit->presetLoad(12);
		for (int i = 0; i < 1000; i++) transit->process(Test::makeProcessArgs(i + 100));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(1.0f).margin(0.01f));

		transit->presetLoad(0);
		for (int i = 0; i < 1000; i++) transit->process(Test::makeProcessArgs(i + 1200));
		REQUIRE(testModule->params[TestModule::TEST_PARAM_1].getValue() == Catch::Approx(0.0f).margin(0.01f));
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(exModule);
	delete exModule;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("Clearing an expander preset removes it from the expander", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* exBase = nullptr;
	Module* exModule = createExModule(&exBase);
	Test::registerModule(exModule);

	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	transit->process(Test::makeProcessArgs(1));
	transit->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	transit->taskProcessorDsp.process();
	connectExpander(transit, exModule);

	testModule->params[TestModule::TEST_PARAM_1].setValue(0.5f);
	transit->presetSave(12);
	transit->presetSave(13);
	REQUIRE(exBase->presetSlotUsed[0] == true);
	REQUIRE(exBase->presetSlotUsed[1] == true);

	SECTION("presetClear(12) marks expander slot 0 as unused") {
		transit->presetClear(12);
		REQUIRE(exBase->presetSlotUsed[0] == false);
		REQUIRE(exBase->preset[0].empty());
	}

	SECTION("Clearing the active expander preset resets transit->preset to -1") {
		transit->presetSetLast(24);
		transit->presetLoad(12);
		REQUIRE(transit->preset == 12);
		transit->presetClear(12);
		REQUIRE(transit->preset == -1);
	}

	SECTION("Clearing expander slot 12 does not affect slot 13") {
		transit->presetClear(12);
		REQUIRE(exBase->presetSlotUsed[1] == true);
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(exModule);
	delete exModule;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("Boundary settings can span into expander range", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* exBase = nullptr;
	Module* exModule = createExModule(&exBase);
	Test::registerModule(exModule);

	connectExpander(transit, exModule);

	SECTION("presetSetLast can extend into expander range") {
		transit->presetSetLast(20);
		REQUIRE(transit->presetLast == 20);
	}

	SECTION("presetSetFirst can be in the expander range") {
		transit->presetSetLast(20); // first extend last into expander range
		transit->presetSetFirst(14);
		REQUIRE(transit->presetFirst == 14);
	}

	SECTION("presetLoad rejects index beyond expander range") {
		transit->presetSetLast(20);
		transit->presetLoad(15); // valid: index 15 is in expander, presetLast allows it
		REQUIRE(transit->preset == 15);
		int presetBefore = transit->preset;
		transit->presetLoad(20); // at boundary = presetLast (exclusive → rejected)
		REQUIRE(transit->preset == presetBefore);
	}

	Test::unregisterModule(exModule);
	delete exModule;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("TRIG_FWD wraps correctly when presetLast is in expander range", "[TransitEx]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	Test::registerModule(transit);
	TransitBase<12>* exBase = nullptr;
	Module* exModule = createExModule(&exBase);
	Test::registerModule(exModule);

	TestModule* testModule = new TestModule();
	Test::registerModule(testModule);

	transit->process(Test::makeProcessArgs(1));
	transit->bindAddParameterRequest(testModule->id, TestModule::TEST_PARAM_1);
	transit->taskProcessorDsp.process();

	connectExpander(transit, exModule);

	// Save presets across the boundary
	for (int i = 0; i < 24; i++) {
		testModule->params[TestModule::TEST_PARAM_1].setValue(i / 23.0f);
		transit->presetSave(i);
	}

	transit->slotCvMode = SLOTCVMODE::TRIG_FWD;
	transit->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::READ);
	transit->presetSetFirst(10);
	transit->presetSetLast(15); // spans across boundary (12/13 are in expander)

	// Initialize inputs and let resetTimer accumulate
	transit->inputs[TransitModule<12>::INPUT_RESET].channels = 1;
	transit->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
	transit->inputs[TransitModule<12>::INPUT_CV].channels = 1;
	transit->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
	for (int i = 0; i < 100; i++) {
		transit->process(Test::makeProcessArgs(i + 10));
	}

	auto trigger = [&](int frame) {
		transit->inputs[TransitModule<12>::INPUT_CV].setVoltage(10.0f);
		transit->process(Test::makeProcessArgs(frame));
		transit->inputs[TransitModule<12>::INPUT_CV].setVoltage(0.0f);
		transit->process(Test::makeProcessArgs(frame + 1));
	};

	SECTION("Reset goes to presetFirst (within Transit)") {
		transit->preset = 13;
		transit->inputs[TransitModule<12>::INPUT_RESET].setVoltage(10.0f);
		transit->process(Test::makeProcessArgs(200));
		transit->inputs[TransitModule<12>::INPUT_RESET].setVoltage(0.0f);
		transit->process(Test::makeProcessArgs(201));
		REQUIRE(transit->preset == 10); // presetFirst
	}

	SECTION("Trigger advances from Transit slot into expander slot") {
		transit->preset = 11; // last Transit slot in range
		trigger(300);
		REQUIRE(transit->preset == 12); // first expander slot in range
	}

	SECTION("Trigger wraps from last expander slot back to presetFirst") {
		transit->preset = 14; // presetLast - 1 = 14
		trigger(300);
		REQUIRE(transit->preset == 10); // wraps to presetFirst
	}

	Test::unregisterModule(testModule);
	delete testModule;
	Test::unregisterModule(exModule);
	delete exModule;
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}

TEST_CASE("ctrlUniqueId is preserved in TransitEx JSON round-trip", "[TransitEx][JSON]") {
	Module* exModule = createExModule();
	TransitBase<12>* exBase = dynamic_cast<TransitBase<12>*>(exModule);
	REQUIRE(exBase != nullptr);

	// Set a specific ctrlUniqueId (as would be done by Transit when linking)
	exBase->ctrlUniqueId = 42;

	json_t* rootJ = exModule->dataToJson();

	Module* exModule2 = createExModule();
	TransitBase<12>* exBase2 = dynamic_cast<TransitBase<12>*>(exModule2);
	exModule2->dataFromJson(rootJ);

	SECTION("ctrlUniqueId is serialized and restored") {
		REQUIRE(exBase2->ctrlUniqueId == 42);
	}

	json_decref(rootJ);
	delete exModule2;
	delete exModule;
}
