#include "../../test/framework.hpp"
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
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
	TransitPadWidget* mw = Test::createWidget<TransitPadWidget>("TransitPad");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	REQUIRE(m->currentSet == 0);
	REQUIRE(m->snapshotsUsed == 4);
	REQUIRE(m->setCvMode == SETCVMODE::TRIG_FWD);

	Test::destroyWidget(mw);
}


TEST_CASE("Preset JSON null-guards", "[TransitPad][JSON]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	auto module = mods.create("TransitPad");

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

// XyScreenNodes::dataToJson()/dataFromJson() write "radius"/"amount"
// unconditionally — they are only ever called for nodes now (Stage 3/4 of
// the refactor deleted the cursor persistence calls entirely, rather than
// keeping an always-false branch). This pins the exact JSON produced for a
// distinctive snapshot state so any future change that moves or renames
// those keys fails loudly.

TEST_CASE("Golden JSON: snapshot (node) radius/amount round-trip byte-identically", "[TransitPad][JSON]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

	m->nodes.setRadiusImmediate(0, 0.125f);
	m->nodes.setRadius(0, 0.125f);
	m->nodes.setAmountImmediate(0, 0.875f);
	m->nodes.setAmount(0, 0.875f);

	json_t* dataJ = json_object();
	m->Sc::nodes.dataToJson(dataJ, 0);

	char* dumped = json_dumps(dataJ, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
	std::string actual(dumped);
	free(dumped);
	json_decref(dataJ);

	REQUIRE(actual == "{\"amount\":0.875,\"radius\":0.125}");

}

TEST_CASE("Golden JSON: full module dataToJson is byte-identical for a distinctive snapshot state", "[TransitPad][JSON]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

	m->snapshots[0][0].id = 3;
	m->nodes.setRadiusImmediate(0, 0.25f);
	m->nodes.setRadius(0, 0.25f);
	m->nodes.setAmountImmediate(0, 0.5f);
	m->nodes.setAmount(0, 0.5f);

	json_t* rootJ = m->dataToJson();
	json_t* setsJ = json_object_get(rootJ, "sets");
	json_t* set0J = json_array_get(setsJ, 0);
	json_t* snapshotsJ = json_object_get(set0J, "snapshots");
	json_t* snapshot0J = json_array_get(snapshotsJ, 0);
	json_t* outputJ = json_object_get(rootJ, "output");

	char* snapshotDumped = json_dumps(snapshot0J, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
	std::string snapshotActual(snapshotDumped);
	free(snapshotDumped);

	REQUIRE(snapshotActual == "{\"amount\":0.5,\"id\":3,\"radius\":0.25}");

	// "output" never carries "radius"/"amount" — the cursor has no
	// persistence method at all; only Seq::dataToJson writes into it.
	REQUIRE(json_object_get(outputJ, "radius") == nullptr);
	REQUIRE(json_object_get(outputJ, "amount") == nullptr);

	json_decref(rootJ);
}


TEST_CASE("Regression: 'sets' array longer than SETS is bounded", "[TransitPad][JSON]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	// BUG-1: dataFromJson() iterated the full length of "sets", writing past
	// the fixed-size snapshots[SETS]/setColor[SETS]/setLabel[SETS] members.
	// Loading a hand-edited patch with >8 entries crashed (ASan: SEGV).
	TransitPadModule<>* m = mods.create("TransitPad");

	json_t* rootJ = m->dataToJson();
	REQUIRE(rootJ != nullptr);

	// Label each of the 8 real sets, then pad the array out to 40 entries with
	// duplicates of set 0. All values stay well-typed, isolating the missing
	// outer-loop bound from any type confusion.
	json_t* setsJ = json_object_get(rootJ, "sets");
	REQUIRE(json_is_array(setsJ));
	for (size_t s = 0; s < json_array_size(setsJ); s++) {
		json_object_set_new(json_array_get(setsJ, s), "label", json_string(("S" + std::to_string(s)).c_str()));
	}
	json_t* firstJ = json_array_get(setsJ, 0);
	while (json_array_size(setsJ) < 40) {
		REQUIRE(json_array_append(setsJ, firstJ) == 0);
	}

	REQUIRE_NOTHROW(m->dataFromJson(rootJ));

	// The first SETS labels must land on their own set; entries beyond SETS
	// must be ignored entirely.
	for (size_t s = 0; s < m->getSetCount(); s++) {
		REQUIRE(m->setLabel[s] == "S" + std::to_string(s));
	}

	json_decref(rootJ);
}


TEST_CASE("Regression: non-string 'color'/'label' values are ignored", "[TransitPad][JSON]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	// BUG-2: json_string_value() returns NULL for non-string values; assigning
	// it to std::string was UB (ASan: SEGV in _platform_strlen).
	TransitPadModule<>* m = mods.create("TransitPad");

	// Distinctive state that must survive loading malformed color/label keys
	NVGcolor color0 = m->setColor[0];
	m->setLabel[1] = "keep";

	json_t* rootJ = m->dataToJson();
	REQUIRE(rootJ != nullptr);

	json_t* setsJ = json_object_get(rootJ, "sets");
	REQUIRE(json_is_array(setsJ));
	size_t s;
	json_t* setJ;
	json_array_foreach(setsJ, s, setJ) {
		json_object_set_new(setJ, "color", json_integer(42));
		json_object_set_new(setJ, "label", json_real(3.14));
	}

	REQUIRE_NOTHROW(m->dataFromJson(rootJ));

	// Wrong-typed keys are skipped: existing colors and labels are preserved
	REQUIRE(m->setColor[0].r == color0.r);
	REQUIRE(m->setColor[0].g == color0.g);
	REQUIRE(m->setColor[0].b == color0.b);
	REQUIRE(m->setColor[0].a == color0.a);
	REQUIRE(m->setLabel[1] == "keep");

	json_decref(rootJ);
}


TEST_CASE("SET_PARAM buttons change currentSet", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

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

}


TEST_CASE("SET_CV_INPUT TRIG_FWD advances currentSet on each trigger", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
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

}


TEST_CASE("SET_CV_INPUT VOLT mode maps 0-10V to set index", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
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

}


TEST_CASE("SET_CV_INPUT C4 mode maps V/oct to set index", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
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

}


TEST_CASE("SET_CV_INPUT OFF mode: input has no effect", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
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

}


TEST_CASE("CV input sets currentSet when connected", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
	m->setCvMode = SETCVMODE::VOLT;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	// No buttons pressed — verify CV takes effect
	m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(2.5f); // 2.5/10 * 8 = 2 -> set 2
	runFrames(m, 5);

	REQUIRE(m->currentSet == 2);

}


TEST_CASE("Buttons work when CV is disconnected", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
	m->setCvMode = SETCVMODE::VOLT;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 0; // disconnected

	m->params[TransitPadModule<>::SET_PARAM + 4].setValue(1.f);
	runFrames(m, 100);

	REQUIRE(m->currentSet == 4);

}


TEST_CASE("JSON round-trip preserves setCvMode", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

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

}


TEST_CASE("JSON round-trip preserves snapshotsUsed", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

	m->snapshotsUsed = 6;
	json_t* j = m->dataToJson();
	m->snapshotsUsed = 4;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->snapshotsUsed == 6);

}


TEST_CASE("JSON round-trip preserves currentSet", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	SECTION("Non-zero currentSet survives save/load") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->currentSet = 5;
		json_t* j = m->dataToJson();
		m->currentSet = 0;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 5);
	}

	SECTION("Out-of-range currentSet is clamped to valid range on load") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->currentSet = 0;
		// Hand-craft a JSON document with a bogus currentSet value to exercise the clamp
		json_t* j = json_pack("{s:i}", "currentSet", 999);
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == (int)m->getSetCount() - 1);
	}

	SECTION("Negative currentSet is clamped to 0 on load") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->currentSet = 4;
		json_t* j = json_pack("{s:i}", "currentSet", -1);
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Missing currentSet key leaves currentSet unchanged (back-compat with old patches)") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->currentSet = 3;
		json_t* j = json_object();
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 3);
	}
}


TEST_CASE("JSON round-trip preserves setLabel", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	SECTION("Non-empty label survives save/load") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->setLabel[2] = "Verse";
		json_t* j = m->dataToJson();
		m->setLabel[2] = "";
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->setLabel[2] == "Verse");
	}

	SECTION("Empty label is not written; missing key on load leaves label unchanged") {
		TransitPadModule<>* m = mods.create("TransitPad");
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
	}

	SECTION("getSetLabel returns custom label when set") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->setLabel[3] = "Chorus";
		REQUIRE(m->getSetLabel(3) == "Chorus");
	}

	SECTION("getSetLabel falls back to 'Set #N' when empty") {
		TransitPadModule<>* m = mods.create("TransitPad");
		REQUIRE(m->getSetLabel(0) == "Set #1");
		REQUIRE(m->getSetLabel(4) == "Set #5");
	}

	SECTION("'label' key is omitted from JSON when no label is set") {
		TransitPadModule<>* m = mods.create("TransitPad");
		// Default state: no labels
		json_t* j = m->dataToJson();
		json_t* setsJ = json_object_get(j, "sets");
		REQUIRE(setsJ != NULL);
		json_t* set0J = json_array_get(setsJ, 0);
		REQUIRE(json_object_get(set0J, "label") == NULL);
		json_decref(j);
	}

	SECTION("'label' key is present in JSON only for sets that have one") {
		TransitPadModule<>* m = mods.create("TransitPad");
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
	}
}


TEST_CASE("onReset clears setLabel", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
	m->setLabel[0] = "Intro";
	m->setLabel[3] = "Bridge";
	m->onReset();
	REQUIRE(m->setLabel[0] == "");
	REQUIRE(m->setLabel[3] == "");
	REQUIRE(m->getSetLabel(0) == "Set #1");
}


TEST_CASE("onReset restores defaults", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

	m->currentSet = 6;
	m->snapshotsUsed = 8;
	m->onReset();

	REQUIRE(m->currentSet == 0);
	REQUIRE(m->snapshotsUsed == 4);
	REQUIRE(m->isLocked() == false);

}


TEST_CASE("Locked state", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	SECTION("Default state is unlocked") {
		TransitPadModule<>* m = mods.create("TransitPad");
		REQUIRE(m->isLocked() == false);
	}

	SECTION("onReset clears lock") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->locked = true;
		m->onReset();
		REQUIRE(m->isLocked() == false);
	}

	SECTION("Lock survives save/load") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->locked = true;
		json_t* j = m->dataToJson();
		m->locked = false;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == true);
	}

	SECTION("Unlock survives save/load") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->locked = false;
		json_t* j = m->dataToJson();
		m->locked = true;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == false);
	}

	SECTION("Missing 'locked' key on load leaves lock state unchanged (back-compat)") {
		TransitPadModule<>* m = mods.create("TransitPad");
		m->locked = true;
		json_t* j = json_object();
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == true);
	}
}


TEST_CASE("getCursorXFinal/getCursorYFinal track CV-driven Out position, not the UI shadow", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	// Regression: the cursor drag widget must draw from the param-backed
	// "final" position (what process() writes from CV/sequencer/ParamHandle
	// inputs), not from outUiX/outUiY, which is only ever written by a mouse
	// drag or setCursorXyImmediate/Filtered and does not move with CV.
	TransitPadModule<>* m = mods.create("TransitPad");

	m->inputs[TransitPadModule<>::OUT_X_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::OUT_X_INPUT].setVoltage(3.f); // → x = 3/10 + 0.5 = 0.8
	m->inputs[TransitPadModule<>::OUT_Y_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::OUT_Y_INPUT].setVoltage(-2.f); // → y = -2/10 + 0.5 = 0.3

	float outUiXBefore = m->outUiX;
	float outUiYBefore = m->outUiY;

	runFrames(m, 5);

	REQUIRE(m->getCursorXFinal(0) == Catch::Approx(0.8f).margin(0.01f));
	REQUIRE(m->getCursorYFinal(0) == Catch::Approx(0.3f).margin(0.01f));
	// The UI shadow is untouched by CV — proves it would be the wrong read source.
	REQUIRE(m->outUiX == Catch::Approx(outUiXBefore));
	REQUIRE(m->outUiY == Catch::Approx(outUiYBefore));

}


TEST_CASE("Snapshot weights: point inside radius gets nonzero weight", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
	m->snapshotsUsed = 1;

	// Default positions: snapshot 0 at (0.1, 0.1), mix point at (0.5, 0.5)
	// Distance = sqrt(0.4^2 + 0.4^2) ≈ 0.566, default radius = 1.0 → inside
	runFrames(m, 5);

	REQUIRE(m->snapshots[0][0].weight > 0.f);

}


TEST_CASE("Snapshot weights: point outside radius gets zero weight", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
	m->snapshotsUsed = 1;

	// Move mix point to (0.9, 0.9) via the filter state so process() respects it.
	// Snapshot 0 defaults to (0.1, 0.1).
	// Distance = sqrt(0.8^2 + 0.8^2) ≈ 1.131, default radius = 1.0 → outside
	m->setCursorXyImmediate(0, 0.9f, 0.9f);
	runFrames(m, 5);

	REQUIRE(m->snapshots[0][0].weight == 0.f);

}


TEST_CASE("Snapshot weights are written to the active set", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");
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

// Helper: run N frames through BOTH modules — pad first (computes snapshot
// weights from the mix-position inputs), then Transit (applies the weights to
// the bound parameters). Mirrors the Rack engine ticking both modules.
static void runPadAndTransit(TransitPadModule<>* pad, TransitModule<12>* transit, int n, int64_t startFrame) {
	for (int i = 0; i < n; i++) {
		pad->process(Test::makeProcessArgs(startFrame + i));
		transit->process(Test::makeProcessArgs(startFrame + i));
	}
}

// Standard rig for the end-to-end tests: Transit + TransitPad expander + a
// target module whose PARAM_A Transit binds and drives. Call connectPad()
// after saving presets (same setup order as the tests below).
struct PadRig {
	TransitModule<12>* transit;
	TransitPadModule<>* pad;
	TestParamModule* target;

	static PadRig make() {
		PadRig r;
		r.transit = Test::createModule<TransitModule<12>>("Transit");
		r.pad = Test::createModule<TransitPadModule<>>("TransitPad");
		r.target = new TestParamModule();
		Test::registerModule(r.transit);
		Test::registerModule(r.pad);
		Test::registerModule(r.target);
		return r;
	}
	void bind(int64_t frame = 1) { bindParam(transit, target->id, TestParamModule::PARAM_A, frame); }
	void save(int slot, float value) {
		target->params[TestParamModule::PARAM_A].setValue(value);
		transit->presetSave(slot);
	}
	float paramValue() { return target->params[TestParamModule::PARAM_A].getValue(); }
	void run(int n, int64_t startFrame) { runPadAndTransit(pad, transit, n, startFrame); }
	void destroy() {
		Test::unregisterModule(target);
		delete target;
		Test::unregisterModule(pad);
		Test::destroyModule(pad);
		Test::unregisterModule(transit);
		Test::destroyModule(transit);
	}
};

// Connect the mix-position CV inputs (simulates cables)
static void connectMixInputs(TransitPadModule<>* pad) {
	pad->inputs[TransitPadModule<>::OUT_X_INPUT].channels = 1;
	pad->inputs[TransitPadModule<>::OUT_Y_INPUT].channels = 1;
}

// Drive the mix point to ((x+5)/10, (y+5)/10) — ±5V maps to the pad corners
static void setMixVoltage(TransitPadModule<>* pad, float xVolt, float yVolt) {
	pad->inputs[TransitPadModule<>::OUT_X_INPUT].setVoltage(xVolt);
	pad->inputs[TransitPadModule<>::OUT_Y_INPUT].setVoltage(yVolt);
}


TEST_CASE("Transit detects TransitPad as right expander", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


TEST_CASE("Transit sets slotCvMode to OFF when TransitPad is connected", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
	Test::registerModule(transit);
	Test::registerModule(pad);

	transit->slotCvMode = SLOTCVMODE::TRIG_FWD;
	connectPad(transit, pad);

	REQUIRE(transit->slotCvMode == SLOTCVMODE::OFF);

	Test::unregisterModule(pad);
	Test::unregisterModule(transit);
}


TEST_CASE("Transit disconnects from TransitPad when expander is removed", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: single snapshot with full weight applies preset exactly", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: two equal-weight snapshots produce the midpoint", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	// snapshots[0][0] → slot 0, snapshots[0][1] → slot 1 (ids set by initExtra)
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 1.f;

	runTransitFrames(transit, 5);

	// (0.2 * 1 + 0.8 * 1) / (1 + 1) = 0.5
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.5f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: unequal weights produce correctly weighted average", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: snapshot with id=-1 is skipped", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: snapshot pointing to unused slot is skipped", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: all zero weights leave parameters unchanged", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(transit);
	Test::registerModule(pad);
	Test::registerModule(target);

	bindParam(transit, target->id, TestParamModule::PARAM_A);

	target->params[TestParamModule::PARAM_A].setValue(0.3f);
	transit->presetSave(0);

	target->params[TestParamModule::PARAM_A].setValue(0.55f);

	connectPad(transit, pad);
	// All snapshot weights remain 0 (initialized that way in initExtra)

	runTransitFrames(transit, 5);

	// No write should occur → param stays at 0.55
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.55f).margin(0.001f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(pad);
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: switching TransitPad sets changes interpolation output", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


TEST_CASE("presetProcessXyPad: interpolates two bound parameters independently", "[TransitPad][Transit]") {
	Test::ModuleScaffold<TransitModule<12>> mods;
	Test::ModuleScaffold<TransitPadModule<>> mods2;
	TransitModule<12>* transit = mods.create("Transit");
	TransitPadModule<>* pad = mods2.create("TransitPad");
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
	Test::unregisterModule(transit);
}


// ============================================================
// End-to-end signal chain:
// mix position (CV/sequence) → dist[] → radius/amount → weight → Transit param
// Unlike the tests above, the weights are never assigned directly — they are
// computed by pad->process() from the mix-position inputs.
// ============================================================

TEST_CASE("XY-pad chain: mix position CV drives the target parameter between presets", "[TransitPad][Transit]") {
	PadRig r = PadRig::make();
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(r.transit, r.pad);

	// Default layout: snapshot A at (0,0) bound to slot 0, B at (1,0) bound to slot 1
	r.pad->snapshotsUsed = 2;
	connectMixInputs(r.pad);

	// Mix point on corner A → only preset 0 contributes
	setMixVoltage(r.pad, -5.f, -5.f);
	r.run(5, 100);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Mix point on corner B → only preset 1 contributes
	setMixVoltage(r.pad, 5.f, -5.f);
	r.run(5, 200);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	// Mix point halfway between them → equal weights → midpoint
	setMixVoltage(r.pad, 0.f, -5.f);
	r.run(5, 300);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	r.destroy();
}


TEST_CASE("XY-pad chain: amount scales snapshot weight and shifts the blend", "[TransitPad][Transit]") {
	PadRig r = PadRig::make();
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(r.transit, r.pad);

	// Both snapshots equidistant (0.5) from the mix point at (0.5, 0)
	r.pad->snapshotsUsed = 2;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, 0.f, -5.f);

	// Default amount 1.0: equal weights → midpoint blend
	r.run(5, 100);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx(0.55f).margin(0.001f));
	REQUIRE(r.pad->snapshots[0][1].weight == Catch::Approx(0.55f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	// Halving snapshot B's amount halves its weight and pulls the blend toward A:
	// (0 * 0.55 + 1 * 0.275) / (0.55 + 0.275) = 1/3
	r.pad->nodes.setAmountImmediate(1, 0.5f);
	r.run(5, 200);
	REQUIRE(r.pad->snapshots[0][1].weight == Catch::Approx(0.275f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(1.f / 3.f).margin(0.001f));

	r.destroy();
}


TEST_CASE("XY-pad chain: radius cuts off snapshot contribution at the boundary", "[TransitPad][Transit]") {
	PadRig r = PadRig::make();
	r.bind();
	r.save(0, 0.25f);
	connectPad(r.transit, r.pad);

	// Snapshot A at (0,0); the mix point moves along the x-axis so dist == mix.x
	// (X voltage → mix.x = v/10 + 0.5)
	r.pad->snapshotsUsed = 1;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, 0.f, -5.f);

	// Default radius 1.0: dist 0.5 is well inside
	r.run(5, 100);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx(0.55f).margin(0.001f));

	// Shrinking the radius to 0.6 shrinks the weight at the same point
	r.pad->nodes.setRadiusImmediate(0, 0.6f);
	r.run(5, 200);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx((0.6f - 0.5f) / 0.6f * 1.1f).margin(0.001f));

	// Outside the radius the weight is exactly zero and nothing is written
	setMixVoltage(r.pad, 2.f, -5.f);
	r.run(5, 300);
	REQUIRE(r.pad->snapshots[0][0].weight == 0.f);
	r.target->params[TestParamModule::PARAM_A].setValue(0.9f);
	r.run(5, 400);
	REQUIRE(r.paramValue() == Catch::Approx(0.9f).margin(0.001f));

	// Back inside the radius the preset value takes over again
	setMixVoltage(r.pad, 0.f, -5.f);
	r.run(5, 500);
	REQUIRE(r.paramValue() == Catch::Approx(0.25f).margin(0.001f));

	r.destroy();
}


TEST_CASE("XY-pad chain: switching sets via button and CV changes the Transit output", "[TransitPad][Transit]") {
	PadRig r = PadRig::make();
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(r.transit, r.pad);

	// Snapshot A sits near the mix point with a nonzero weight in every set;
	// which preset it reaches depends on the per-set binding
	r.pad->snapshotsUsed = 1;

	// Set 0 keeps the default binding to slot 0
	r.run(5, 100);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Switch to set 1 via button, then rebind snapshot A to slot 1 there
	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(1.f);
	r.run(100, 200);
	REQUIRE(r.pad->currentSet == 1);
	r.pad->bindSnapshot(0, 1);
	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(0.f);
	r.run(5, 400);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	// Back to set 0 via button
	r.pad->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
	r.run(100, 500);
	REQUIRE(r.pad->currentSet == 0);
	r.pad->params[TransitPadModule<>::SET_PARAM + 0].setValue(0.f);
	r.run(5, 700);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Set selection via CV in VOLT mode: 1.25V → set 1, 0V → set 0
	r.pad->setCvMode = SETCVMODE::VOLT;
	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;
	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(1.25f);
	r.run(5, 800);
	REQUIRE(r.pad->currentSet == 1);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
	r.run(5, 900);
	REQUIRE(r.pad->currentSet == 0);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	r.destroy();
}


TEST_CASE("XY-pad chain: motion sequence drives the mix position", "[TransitPad][Transit]") {
	PadRig r = PadRig::make();

	SECTION("Phase input sweeps the mix point along the sequence") {
		r.bind();
		// Snapshot A at (0,0) → slot 0, C at (1,1) → slot 2; slot 1 stays unused
		r.save(0, 0.0f);
		r.save(2, 1.0f);
		connectPad(r.transit, r.pad);
		r.pad->snapshotsUsed = 3;

		// Two-point linear sequence along the A→C diagonal
		r.pad->seqData[0][0].length = 2;
		r.pad->seqData[0][0].x[0] = 0.f; r.pad->seqData[0][0].y[0] = 0.f;
		r.pad->seqData[0][0].x[1] = 1.f; r.pad->seqData[0][0].y[1] = 1.f;

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].channels = 1;

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].setVoltage(0.f);
		r.run(5, 100);
		REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].setVoltage(10.f);
		r.run(5, 200);
		REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].setVoltage(5.f);
		r.run(5, 300);
		REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));
	}

	SECTION("Sequence-select input advances to the next sequence") {
		// Sequence 0 starts at (0,0), sequence 1 at (1,0)
		r.pad->seqData[0][0].length = 2;
		r.pad->seqData[0][0].x[0] = 0.f; r.pad->seqData[0][0].y[0] = 0.f;
		r.pad->seqData[0][0].x[1] = 1.f; r.pad->seqData[0][0].y[1] = 1.f;
		r.pad->seqData[0][1].length = 2;
		r.pad->seqData[0][1].x[0] = 1.f; r.pad->seqData[0][1].y[0] = 0.f;
		r.pad->seqData[0][1].x[1] = 1.f; r.pad->seqData[0][1].y[1] = 1.f;

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].channels = 1;
		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].setVoltage(0.f);
		runFrames(r.pad, 5);
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(0.f).margin(0.001f));
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(0.f).margin(0.001f));

		fireTrigger(r.pad, TransitPadModule<>::OUT_SEQ_INPUT, 10);
		REQUIRE(r.pad->seqSelected[0] == 1);

		runFrames(r.pad, 5, 20);
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(1.f).margin(0.001f));
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(0.f).margin(0.001f));
	}

	r.destroy();
}


TEST_CASE("XY-pad chain: snapshotsUsed bounds which snapshots contribute weight", "[TransitPad][Transit]") {
	PadRig r = PadRig::make();
	r.bind();
	// Slots 0,1 = 0.0 and slots 2,3 = 1.0; snapshots A–D sit at the four
	// corners, all equidistant from the mix point at the centre (0.5, 0.5)
	r.save(0, 0.0f);
	r.save(1, 0.0f);
	r.save(2, 1.0f);
	r.save(3, 1.0f);
	connectPad(r.transit, r.pad);

	// Limit set BEFORE the first run: snapshots C/D keep their initial weight
	// of 0 and never contribute. (Lowering the count only prevents NEW weight
	// computation — already-computed weights are not reset.)
	r.pad->snapshotsUsed = 2;
	r.run(5, 100);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Raising the count lets C/D join the blend
	r.pad->snapshotsUsed = 4;
	r.run(5, 200);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	r.destroy();
}


TEST_CASE("bindSnapshot binds and unbinds pad points to Transit slots", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	SECTION("Binding semantics") {
		TransitPadModule<>* m = mods.create("TransitPad");

		// Defaults: snapshots A–D bound to slot indexes 0–3, E–H unbound
		REQUIRE(m->snapshots[0][0].id == 0);
		REQUIRE(m->snapshots[0][3].id == 3);
		REQUIRE(m->snapshots[0][4].id == -1);

		m->bindSnapshot(4, 7);
		REQUIRE(m->snapshots[0][4].id == 7);

		// -1 unbinds
		m->bindSnapshot(4, -1);
		REQUIRE(m->snapshots[0][4].id == -1);

		// Binding applies to the current set only
		m->currentSet = 2;
		m->bindSnapshot(0, 6);
		REQUIRE(m->snapshots[2][0].id == 6);
		REQUIRE(m->snapshots[0][0].id == 0);

	}

	SECTION("Bound snapshot drives the Transit output; unbinding stops it") {
		PadRig r = PadRig::make();
		r.bind();
		r.save(5, 0.77f);
		connectPad(r.transit, r.pad);

		// Park the mix point on snapshot A (weight saturates at 1.0)
		r.pad->snapshotsUsed = 1;
		connectMixInputs(r.pad);
		setMixVoltage(r.pad, -5.f, -5.f);

		r.pad->bindSnapshot(0, 5);
		r.run(5, 100);
		REQUIRE(r.paramValue() == Catch::Approx(0.77f).margin(0.001f));

		// Unbinding removes the last contribution → no write happens
		r.target->params[TestParamModule::PARAM_A].setValue(0.42f);
		r.pad->bindSnapshot(0, -1);
		r.run(5, 200);
		REQUIRE(r.paramValue() == Catch::Approx(0.42f).margin(0.001f));

		r.destroy();
	}
}


// Bounds correctness (refactor plan Stage 5, §1c): setCursorXyImmediate/
// setCursorXyFiltered previously had no bound check on the cursor path at
// all. TransitPad has exactly one cursor (the Out point), always at id 0;
// confirm an out-of-range id is a silent no-op rather than silently acting
// as if it addressed Out — the exact failure the plan's example describes
// (a stray id of 7 reaching storage where only id 0 is meaningful).

TEST_CASE("setCursorXyImmediate with an out-of-range id is a silent no-op", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

	m->setCursorXyImmediate(0, 0.2f, 0.3f);
	float xBefore = m->params[TransitPadModule<>::OUT_X_POS].getValue();
	float yBefore = m->params[TransitPadModule<>::OUT_Y_POS].getValue();

	REQUIRE_NOTHROW(m->setCursorXyImmediate(1, 0.9f, 0.9f));

	REQUIRE(m->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(xBefore));
	REQUIRE(m->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(yBefore));

}

TEST_CASE("setCursorXyFiltered with an out-of-range id is a silent no-op", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	TransitPadModule<>* m = mods.create("TransitPad");

	m->setCursorXyImmediate(0, 0.2f, 0.3f);
	float xBefore = m->outUiX;
	float yBefore = m->outUiY;

	REQUIRE_NOTHROW(m->setCursorXyFiltered(1, 0.9f, 0.9f));

	REQUIRE(m->outUiX == Catch::Approx(xBefore));
	REQUIRE(m->outUiY == Catch::Approx(yBefore));

}

TEST_CASE("XyScreenNodes setters with an out-of-range id are a silent no-op", "[TransitPad]") {
	Test::ModuleScaffold<TransitPadModule<>> mods;
	// The node side of the same bound (COUNT, i.e. SNAPSHOTS here) predates
	// this stage — XyScreenNodes has always guarded on its own COUNT — but
	// had no direct test. Cover it alongside the cursor-side fix above.
	TransitPadModule<>* m = mods.create("TransitPad");

	m->nodes.setRadiusImmediate(0, 0.4f);
	m->nodes.setAmountImmediate(0, 0.6f);

	float radius0Before = m->nodes.radiusUi[0];
	float amount0Before = m->nodes.amountUi[0];
	float x0Before = m->nodes.uiX[0];

	REQUIRE_NOTHROW(m->nodes.setXyImmediate(8, 0.9f, 0.9f));
	REQUIRE_NOTHROW(m->nodes.setRadiusImmediate(8, 0.9f));
	REQUIRE_NOTHROW(m->nodes.setAmountImmediate(8, 0.9f));

	REQUIRE(m->nodes.uiX[0] == Catch::Approx(x0Before));
	REQUIRE(m->nodes.radiusUi[0] == Catch::Approx(radius0Before));
	REQUIRE(m->nodes.amountUi[0] == Catch::Approx(amount0Before));

}