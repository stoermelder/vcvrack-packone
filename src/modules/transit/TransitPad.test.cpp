#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "TransitBase.hpp"
#include "Transit.cpp"
#include "TransitPad.cpp"

using namespace StoermelderPackOne::Transit;

SYNC_MODEL(modelTransit, "Transit");
SYNC_MODEL(modelTransitPad, "TransitPad");
Test::TestContext<> testContext;

// Helper: run N process frames starting at startFrame
static int64_t runFrames(TransitPadModule<>* m, int n, int64_t startFrame = 0) {
	for (int i = 0; i < n; i++) {
		m->process(Test::makeProcessArgs(startFrame + i));
	}
	return startFrame + n;
}

// Helper: fire a rising-edge trigger on an input port.
// Sends a 0V frame first to move SchmittTrigger from UNINITIALIZED→LOW,
// then 10V (LOW→HIGH, fires), then 0V (HIGH→LOW). Uses 3 frames total.
static void fireTrigger(TransitPadModule<>* m, int inputId, int64_t frame) {
	m->inputs[inputId].channels = 1;
	m->inputs[inputId].setVoltage(0.f);
	m->process(Test::makeProcessArgs(frame));
	m->inputs[inputId].setVoltage(10.f);
	m->process(Test::makeProcessArgs(frame + 1));
	m->inputs[inputId].setVoltage(0.f);
	m->process(Test::makeProcessArgs(frame + 2));
}


TEST_CASE("Construction and initialization", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* mw = Test::createWidget<TransitPadWidget>("TransitPad");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	REQUIRE(m->currentSet == 0);
	REQUIRE(m->snapshotsUsed == 4);
	REQUIRE(m->setCvMode == SETCVMODE::TRIG_FWD);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}


TEST_CASE("Preset JSON null-guards", "[TransitPad][JSON]") {
	auto module = Test::createModule<TransitPadModule<>>("TransitPad");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


TEST_CASE("SET_PARAM buttons change currentSet", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");

	SECTION("Pressing set button 3 changes currentSet to 3") {
		m->params[TransitPadModule<>::SET_PARAM + 3].setValue(1.f);
		runFrames(m, 100);
		REQUIRE(m->currentSet == 3);
	}

	SECTION("Pressing set button 0 keeps currentSet at 0") {
		m->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
		runFrames(m, 100);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Switching between sets") {
		m->params[TransitPadModule<>::SET_PARAM + 5].setValue(1.f);
		runFrames(m, 100);
		REQUIRE(m->currentSet == 5);

		m->params[TransitPadModule<>::SET_PARAM + 5].setValue(0.f);
		m->params[TransitPadModule<>::SET_PARAM + 2].setValue(1.f);
		runFrames(m, 100, 100);
		REQUIRE(m->currentSet == 2);
	}

	Test::destroyModule(m);
}


TEST_CASE("SET_CV_INPUT TRIG_FWD advances currentSet on each trigger", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::TRIG_FWD;
	// Keep buttons unpressed so they don't interfere
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);

	REQUIRE(m->currentSet == 0);

	SECTION("Each trigger advances by one") {
		// fireTrigger uses 3 frames each; space them out
		fireTrigger(m, TransitPadModule<>::SET_CV_INPUT, 0);
		REQUIRE(m->currentSet == 1);

		fireTrigger(m, TransitPadModule<>::SET_CV_INPUT, 10);
		REQUIRE(m->currentSet == 2);

		fireTrigger(m, TransitPadModule<>::SET_CV_INPUT, 20);
		REQUIRE(m->currentSet == 3);
	}

	SECTION("Wraps around from last set back to 0") {
		m->currentSet = 7;
		fireTrigger(m, TransitPadModule<>::SET_CV_INPUT, 0);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("No change without trigger (sustained low voltage)") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
		runFrames(m, 50);
		REQUIRE(m->currentSet == 0);
	}

	Test::destroyModule(m);
}


TEST_CASE("SET_CV_INPUT VOLT mode maps 0-10V to set index", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::VOLT;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	SECTION("0V selects set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("10V selects set 7 (last)") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(10.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 7);
	}

	SECTION("Voltage clamped below 0V selects set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(-5.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Voltage clamped above 10V selects set 7") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(15.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 7);
	}

	SECTION("5V selects set 4") {
		// 5 / 10 * 8 = 4.0 -> int(4.0) = 4
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(5.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 4);
	}

	Test::destroyModule(m);
}


TEST_CASE("SET_CV_INPUT C4 mode maps V/oct to set index", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::C4;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	SECTION("0V selects set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("1/12 V selects set 1 (one semitone)") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(1.f / 12.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 1);
	}

	SECTION("7/12 V selects set 7") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(7.f / 12.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 7);
	}

	SECTION("Negative voltage clamps to set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(-1.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Large positive voltage clamps to set 7") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(5.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 7);
	}

	Test::destroyModule(m);
}


TEST_CASE("SET_CV_INPUT OFF mode: input has no effect", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::OFF;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	SECTION("Trigger has no effect in OFF mode") {
		m->currentSet = 2;
		fireTrigger(m, TransitPadModule<>::SET_CV_INPUT, 0);
		REQUIRE(m->currentSet == 2);
	}

	SECTION("High voltage has no effect in OFF mode") {
		m->currentSet = 3;
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(10.f);
		runFrames(m, 5);
		REQUIRE(m->currentSet == 3);
	}

	SECTION("Buttons still work in OFF mode") {
		m->params[TransitPadModule<>::SET_PARAM + 6].setValue(1.f);
		runFrames(m, 100);
		REQUIRE(m->currentSet == 6);
	}

	Test::destroyModule(m);
}


TEST_CASE("CV input sets currentSet when connected", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::VOLT;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	// No buttons pressed — verify CV takes effect
	m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(2.5f); // 2.5/10 * 8 = 2 -> set 2
	runFrames(m, 5);

	REQUIRE(m->currentSet == 2);

	Test::destroyModule(m);
}


TEST_CASE("Buttons work when CV is disconnected", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::VOLT;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 0; // disconnected

	m->params[TransitPadModule<>::SET_PARAM + 4].setValue(1.f);
	runFrames(m, 100);

	REQUIRE(m->currentSet == 4);

	Test::destroyModule(m);
}


TEST_CASE("JSON round-trip preserves setCvMode", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");

	SECTION("VOLT mode survives save/load") {
		m->setCvMode = SETCVMODE::VOLT;
		json_t* j = m->dataToJson();
		m->setCvMode = SETCVMODE::TRIG_FWD;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->setCvMode == SETCVMODE::VOLT);
	}

	SECTION("C4 mode survives save/load") {
		m->setCvMode = SETCVMODE::C4;
		json_t* j = m->dataToJson();
		m->setCvMode = SETCVMODE::TRIG_FWD;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->setCvMode == SETCVMODE::C4);
	}

	SECTION("OFF mode survives save/load") {
		m->setCvMode = SETCVMODE::OFF;
		json_t* j = m->dataToJson();
		m->setCvMode = SETCVMODE::TRIG_FWD;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->setCvMode == SETCVMODE::OFF);
	}

	SECTION("TRIG_FWD mode survives save/load") {
		m->setCvMode = SETCVMODE::TRIG_FWD;
		json_t* j = m->dataToJson();
		m->setCvMode = SETCVMODE::VOLT;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->setCvMode == SETCVMODE::TRIG_FWD);
	}

	Test::destroyModule(m);
}


TEST_CASE("JSON round-trip preserves snapshotsUsed", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");

	m->snapshotsUsed = 6;
	json_t* j = m->dataToJson();
	m->snapshotsUsed = 4;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->snapshotsUsed == 6);

	Test::destroyModule(m);
}


TEST_CASE("JSON round-trip preserves currentSet", "[TransitPad]") {
	SECTION("Non-zero currentSet survives save/load") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 5;
		json_t* j = m->dataToJson();
		m->currentSet = 0;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 5);
		Test::destroyModule(m);
	}

	SECTION("Out-of-range currentSet is clamped to valid range on load") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 0;
		// Hand-craft a JSON document with a bogus currentSet value to exercise the clamp
		json_t* j = json_pack("{s:i}", "currentSet", 999);
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == (int)m->getSetCount() - 1);
		Test::destroyModule(m);
	}

	SECTION("Negative currentSet is clamped to 0 on load") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 4;
		json_t* j = json_pack("{s:i}", "currentSet", -1);
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 0);
		Test::destroyModule(m);
	}

	SECTION("Missing currentSet key leaves currentSet unchanged (back-compat with old patches)") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 3;
		json_t* j = json_object();
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 3);
		Test::destroyModule(m);
	}
}


TEST_CASE("JSON round-trip preserves setLabel", "[TransitPad]") {
	SECTION("Non-empty label survives save/load") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->setLabel[2] = "Verse";
		json_t* j = m->dataToJson();
		m->setLabel[2] = "";
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->setLabel[2] == "Verse");
		Test::destroyModule(m);
	}

	SECTION("Empty label is not written; missing key on load leaves label unchanged") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->setLabel[0] = "Intro";
		// setLabel[1] is left as default ("")
		json_t* j = m->dataToJson();
		// Simulate a fresh module loading an old patch: clear labels
		m->setLabel[0] = "";
		m->setLabel[1] = "garbage";
		m->dataFromJson(j);
		json_decref(j);
		// Set 0 had a label, so it was persisted and restored
		REQUIRE(m->setLabel[0] == "Intro");
		// Set 1 had no label, so the key was absent in JSON — existing value is preserved
		REQUIRE(m->setLabel[1] == "garbage");
		Test::destroyModule(m);
	}

	SECTION("getSetLabel returns custom label when set") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->setLabel[3] = "Chorus";
		REQUIRE(m->getSetLabel(3) == "Chorus");
		Test::destroyModule(m);
	}

	SECTION("getSetLabel falls back to 'Set #N' when empty") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		REQUIRE(m->getSetLabel(0) == "Set #1");
		REQUIRE(m->getSetLabel(4) == "Set #5");
		Test::destroyModule(m);
	}

	SECTION("'label' key is omitted from JSON when no label is set") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		// Default state: no labels
		json_t* j = m->dataToJson();
		json_t* setsJ = json_object_get(j, "sets");
		REQUIRE(setsJ != NULL);
		json_t* set0J = json_array_get(setsJ, 0);
		REQUIRE(json_object_get(set0J, "label") == NULL);
		json_decref(j);
		Test::destroyModule(m);
	}

	SECTION("'label' key is present in JSON only for sets that have one") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->setLabel[2] = "Verse";
		json_t* j = m->dataToJson();
		json_t* setsJ = json_object_get(j, "sets");
		REQUIRE(json_object_get(json_array_get(setsJ, 0), "label") == NULL);
		REQUIRE(json_object_get(json_array_get(setsJ, 1), "label") == NULL);
		json_t* set2J = json_array_get(setsJ, 2);
		json_t* labelJ = json_object_get(set2J, "label");
		REQUIRE(labelJ != NULL);
		REQUIRE(std::string(json_string_value(labelJ)) == "Verse");
		json_decref(j);
		Test::destroyModule(m);
	}
}


TEST_CASE("onReset clears setLabel", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->setLabel[0] = "Intro";
	m->setLabel[3] = "Bridge";
	m->onReset();
	REQUIRE(m->setLabel[0] == "");
	REQUIRE(m->setLabel[3] == "");
	REQUIRE(m->getSetLabel(0) == "Set #1");
	Test::destroyModule(m);
}


TEST_CASE("onReset restores defaults", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");

	m->currentSet = 6;
	m->snapshotsUsed = 8;
	m->onReset();

	REQUIRE(m->currentSet == 0);
	REQUIRE(m->snapshotsUsed == 4);
	REQUIRE(m->isLocked() == false);

	Test::destroyModule(m);
}


TEST_CASE("Locked state", "[TransitPad]") {
	SECTION("Default state is unlocked") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		REQUIRE(m->isLocked() == false);
		Test::destroyModule(m);
	}

	SECTION("onReset clears lock") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->locked = true;
		m->onReset();
		REQUIRE(m->isLocked() == false);
		Test::destroyModule(m);
	}

	SECTION("Lock survives save/load") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->locked = true;
		json_t* j = m->dataToJson();
		m->locked = false;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == true);
		Test::destroyModule(m);
	}

	SECTION("Unlock survives save/load") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->locked = false;
		json_t* j = m->dataToJson();
		m->locked = true;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == false);
		Test::destroyModule(m);
	}

	SECTION("Missing 'locked' key on load leaves lock state unchanged (back-compat)") {
		TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
		m->locked = true;
		json_t* j = json_object();
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == true);
		Test::destroyModule(m);
	}
}


TEST_CASE("Snapshot weights: point inside radius gets nonzero weight", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Default positions: snapshot 0 at (0.1, 0.1), mix point at (0.5, 0.5)
	// Distance = sqrt(0.4^2 + 0.4^2) ≈ 0.566, default radius = 1.0 → inside
	runFrames(m, 5);

	REQUIRE(m->snapshots[0][0].weight > 0.f);

	Test::destroyModule(m);
}


TEST_CASE("Snapshot weights: point outside radius gets zero weight", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Move mix point to (0.9, 0.9) via the filter state so process() respects it.
	// Snapshot 0 defaults to (0.1, 0.1).
	// Distance = sqrt(0.8^2 + 0.8^2) ≈ 1.131, default radius = 1.0 → outside
	m->scSetItemImmediate(1, 0, 0.9f, 0.9f);
	runFrames(m, 5);

	REQUIRE(m->snapshots[0][0].weight == 0.f);

	Test::destroyModule(m);
}


TEST_CASE("Snapshot weights are written to the active set", "[TransitPad]") {
	TransitPadModule<>* m = Test::createModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Default positions: snapshot 0 at (0.1,0.1), mix at (0.5,0.5) → nonzero weight
	m->currentSet = 0;
	runFrames(m, 5);
	REQUIRE(m->snapshots[0][0].weight > 0.f);
	// Other sets are untouched while set 0 is active
	REQUIRE(m->snapshots[3][0].weight == 0.f);

	// Switch to set 3 — weights are now computed into set 3
	m->currentSet = 3;
	runFrames(m, 5);
	REQUIRE(m->snapshots[3][0].weight > 0.f);

	Test::destroyModule(m);
}


// ============================================================
// Transit + TransitPad integration: process() interpolation
// ============================================================

// Helper module with parameters that Transit can bind and control
struct TestParamModule : rack::Module {
	enum ParamIds { PARAM_A, PARAM_B, NUM_PARAMS };
	TestParamModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(PARAM_A, 0.f, 1.f, 0.5f, "A");
		configParam(PARAM_B, 0.f, 1.f, 0.5f, "B");
	}
};

// Helper: wire Transit → TransitPad as right expander and let Transit discover it.
// Also forces presetProcessDivision=1 so the XY-pad result is written every frame.
static void connectPad(TransitModule<12>* transit, TransitPadModule<>* pad) {
	transit->rightExpander.module = pad;
	pad->leftExpander.module = transit;
	transit->moduleChangedFlag = true;
	transit->setProcessDivision(1);
	transit->process(Test::makeProcessArgs(0));
}

// Helper: bind a parameter and flush the task queue into sourceHandles
static void bindParam(TransitModule<12>* transit, int moduleId, int paramId, int64_t frame = 1) {
	transit->bindAddParameterRequest(moduleId, paramId);
	transit->taskProcessorDsp.process();
	transit->process(Test::makeProcessArgs(frame));
}

// Helper: run N Transit process frames
static void runTransitFrames(TransitModule<12>* transit, int n, int64_t startFrame = 100) {
	for (int i = 0; i < n; i++)
		transit->process(Test::makeProcessArgs(startFrame + i));
}


TEST_CASE("Transit detects TransitPad as right expander", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	Test::registerModule(transit);
	Test::registerModule(pad);

	// Flush initial expandersChanged so transitPad is properly initialised to nullptr
	transit->process(Test::makeProcessArgs(0));
	REQUIRE_FALSE(transit->isXyPadActive());

	connectPad(transit, pad);

	REQUIRE(transit->isXyPadActive());
	// TransitPad sets masterModule back-pointer
	REQUIRE(pad->masterModule == transit);

	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("Transit sets slotCvMode to OFF when TransitPad is connected", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	Test::registerModule(transit);
	Test::registerModule(pad);

	transit->slotCvMode = SLOTCVMODE::TRIG_FWD;
	connectPad(transit, pad);

	REQUIRE(transit->slotCvMode == SLOTCVMODE::OFF);

	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("Transit disconnects from TransitPad when expander is removed", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	Test::registerModule(transit);
	Test::registerModule(pad);

	connectPad(transit, pad);
	REQUIRE(transit->isXyPadActive());

	// Disconnect
	transit->rightExpander.module = nullptr;
	pad->leftExpander.module = nullptr;
	transit->moduleChangedFlag = true;
	transit->process(Test::makeProcessArgs(10));

	REQUIRE_FALSE(transit->isXyPadActive());

	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: single snapshot with full weight applies preset exactly", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	// Bind target parameter and save preset 0 with value 0.25
	bindParam(transit, target->id, TestParamModule::PARAM_A);
	target->params[TestParamModule::PARAM_A].setValue(0.25f);
	transit->presetSave(0);

	// Connect pad, set snapshot 0 → preset slot 0, weight 1.0
	connectPad(transit, pad);
	pad->snapshots[0][0].id = 0;
	pad->snapshots[0][0].weight = 1.f;

	// Drive target param away so we can verify Transit writes it
	target->params[TestParamModule::PARAM_A].setValue(0.99f);
	runTransitFrames(transit, 5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: two equal-weight snapshots produce the midpoint", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A);

	// Save preset 0 = 0.2, preset 1 = 0.8
	target->params[TestParamModule::PARAM_A].setValue(0.2f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(0.8f);
	transit->presetSave(1);

	connectPad(transit, pad);
	// snapshots[0][0] → slot 0, snapshots[0][1] → slot 1 (ids set by scInitItems)
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 1.f;

	runTransitFrames(transit, 5);

	// (0.2 * 1 + 0.8 * 1) / (1 + 1) = 0.5
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.5f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: unequal weights produce correctly weighted average", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A);

	// preset 0 = 0.0, preset 1 = 1.0
	target->params[TestParamModule::PARAM_A].setValue(0.f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(1.f);
	transit->presetSave(1);

	connectPad(transit, pad);
	// Weight 1:3 toward preset 1
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 3.f;

	runTransitFrames(transit, 5);

	// (0.0 * 1 + 1.0 * 3) / (1 + 3) = 0.75
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.75f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: snapshot with id=-1 is skipped", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A);

	// Only preset 1 saved
	target->params[TestParamModule::PARAM_A].setValue(0.7f);
	transit->presetSave(1);

	connectPad(transit, pad);
	// snapshot 0: id=-1 (unbound), snapshot 1: id=1 with full weight
	pad->snapshots[0][0].id = -1;
	pad->snapshots[0][0].weight = 1.f; // weight set but id is -1 → ignored
	pad->snapshots[0][1].weight = 1.f; // this one should take effect

	runTransitFrames(transit, 5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.7f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: snapshot pointing to unused slot is skipped", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A);

	// Only preset 1 saved; preset 0 is empty
	target->params[TestParamModule::PARAM_A].setValue(0.6f);
	transit->presetSave(1);

	// Set a sentinel value to detect if the param gets written
	target->params[TestParamModule::PARAM_A].setValue(0.42f);

	connectPad(transit, pad);
	// snapshot 0 → empty slot 0 (not saved), weight 1.0 → should be skipped
	// snapshot 1 → slot 1 with weight 0 → skipped too
	// Total weight = 0 → no write → param stays at 0.42
	pad->snapshots[0][0].weight = 1.f; // points at slot 0 which is unused

	runTransitFrames(transit, 5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.42f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: all zero weights leave parameters unchanged", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A);

	target->params[TestParamModule::PARAM_A].setValue(0.3f);
	transit->presetSave(0);

	target->params[TestParamModule::PARAM_A].setValue(0.55f);

	connectPad(transit, pad);
	// All snapshot weights remain 0 (initialized that way in scInitItems)

	runTransitFrames(transit, 5);

	// No write should occur → param stays at 0.55
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.55f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: switching TransitPad sets changes interpolation output", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A);

	// preset 0 = 0.1, preset 1 = 0.9
	target->params[TestParamModule::PARAM_A].setValue(0.1f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(0.9f);
	transit->presetSave(1);

	connectPad(transit, pad);

	// Set 0: snapshot 0 → preset 0, weight 1.0
	pad->snapshots[0][0].weight = 1.f;
	// Set 1: snapshot 1 → preset 1, weight 1.0
	pad->snapshots[1][1].weight = 1.f;

	// Activate set 0
	pad->currentSet = 0;
	runTransitFrames(transit, 5, 100);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.1f).margin(0.001f));

	// Activate set 1
	pad->currentSet = 1;
	runTransitFrames(transit, 5, 200);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.9f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}


TEST_CASE("presetProcessXyPad: interpolates two bound parameters independently", "[TransitPad][Transit]") {
	TransitModule<12>* transit = Test::createModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = Test::createModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A, 1);
	bindParam(transit, target->id, TestParamModule::PARAM_B, 2);

	// preset 0: A=0.2, B=0.8 / preset 1: A=0.6, B=0.4
	target->params[TestParamModule::PARAM_A].setValue(0.2f);
	target->params[TestParamModule::PARAM_B].setValue(0.8f);
	transit->presetSave(0);

	target->params[TestParamModule::PARAM_A].setValue(0.6f);
	target->params[TestParamModule::PARAM_B].setValue(0.4f);
	transit->presetSave(1);

	connectPad(transit, pad);
	// Equal weights → midpoint for both params
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 1.f;

	runTransitFrames(transit, 5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.4f).margin(0.001f));
	REQUIRE(target->params[TestParamModule::PARAM_B].getValue() == Catch::Approx(0.6f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::destroyModule(pad);
	Test::unregisterModule(transit);
	Test::destroyModule(transit);
}
