// Helper: fire a rising-edge trigger on an input port.
// Sends a 0V step first to move SchmittTrigger from UNINITIALIZED→LOW,
// then 10V (LOW→HIGH, fires), then 0V (HIGH→LOW). Uses 3 steps total.
static void fireTrigger(Test::Harness& h, TransitPadModule<>* m, int inputId) {
	m->inputs[inputId].channels = 1;
	m->inputs[inputId].setVoltage(0.f);
	h.dspStep();
	m->inputs[inputId].setVoltage(10.f);
	h.dspStep();
	m->inputs[inputId].setVoltage(0.f);
}


TEST_CASE("Construction and initialization", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	REQUIRE(m != nullptr);
	REQUIRE(m->currentSet == 0);
	REQUIRE(m->snapshotsUsed == 4);
	REQUIRE(m->setCvMode == SETCVMODE::TRIG_FWD);

	// The module browser builds this widget with module == nullptr to render
	// the preview, so construction/destruction must survive a null module.
	TransitPadWidget* mw = nullptr;
	REQUIRE_NOTHROW(mw = Test::createWidget<TransitPadWidget>("TransitPad"));
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);
	REQUIRE_NOTHROW(Test::destroyWidget(mw));
}


TEST_CASE("Preset JSON null-guards", "[TransitPad][JSON]") {
	Test::Harness h;
	auto module = h.addModule<TransitPadModule<>>("TransitPad");

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
// unconditionally — they are only ever called for the live pad-point layout
// now (dataToJson()'s top-level "nodes" array), not per set. This pins the
// exact JSON produced for a distinctive snapshot state so any future change
// that moves or renames those keys fails loudly.

TEST_CASE("Golden JSON: node radius/amount round-trip byte-identically", "[TransitPad][JSON]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

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
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->snapshots[0][0].id = 3;
	m->snapshots[0][0].x = 0.25f;
	m->snapshots[0][0].y = 0.75f;
	m->snapshots[0][0].radius = 0.25f;
	m->snapshots[0][0].amount = 0.5f;
	m->nodes.setRadiusImmediate(0, 0.25f);
	m->nodes.setRadius(0, 0.25f);
	m->nodes.setAmountImmediate(0, 0.5f);
	m->nodes.setAmount(0, 0.5f);

	// The live pad layout is stored once at the top level ("nodes"), not
	// duplicated per set — true regardless of nodePosMode.
	json_t* rootJ = m->dataToJson();
	json_t* nodesJ = json_object_get(rootJ, "nodes");
	json_t* node0J = json_array_get(nodesJ, 0);
	json_t* outputJ = json_object_get(rootJ, "output");

	char* nodeDumped = json_dumps(node0J, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
	std::string nodeActual(nodeDumped);
	free(nodeDumped);
	REQUIRE(nodeActual == "{\"amount\":0.5,\"radius\":0.25}");

	// "output" never carries "radius"/"amount" — the cursor has no
	// persistence method at all; only Seq::dataToJson writes into it.
	REQUIRE(json_object_get(outputJ, "radius") == nullptr);
	REQUIRE(json_object_get(outputJ, "amount") == nullptr);

	// Default (nodePosMode OFF): per-set x/y/radius/amount are omitted to
	// keep the JSON slim, since that data isn't used in this mode.
	{
		json_t* setsJ = json_object_get(rootJ, "sets");
		json_t* snapshotsJ = json_object_get(json_array_get(setsJ, 0), "snapshots");
		json_t* snapshot0J = json_array_get(snapshotsJ, 0);
		char* dumped = json_dumps(snapshot0J, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
		std::string actual(dumped);
		free(dumped);
		REQUIRE(actual == "{\"id\":3}");
	}
	json_decref(rootJ);

	// With nodePosMode enabled, the same fields are written per set.
	m->nodePosMode = NODEPOSMODE::STORE;
	json_t* rootJ2 = m->dataToJson();
	json_t* setsJ2 = json_object_get(rootJ2, "sets");
	json_t* snapshotsJ2 = json_object_get(json_array_get(setsJ2, 0), "snapshots");
	json_t* snapshot0J2 = json_array_get(snapshotsJ2, 0);
	char* snapshotDumped = json_dumps(snapshot0J2, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
	std::string snapshotActual(snapshotDumped);
	free(snapshotDumped);

	REQUIRE(snapshotActual == "{\"amount\":0.5,\"id\":3,\"radius\":0.25,\"x\":0.25,\"y\":0.75}");

	json_decref(rootJ2);
}


TEST_CASE("Regression: 'sets' array longer than SETS is bounded", "[TransitPad][JSON]") {
	Test::Harness h;
	// BUG-1: dataFromJson() iterated the full length of "sets", writing past
	// the fixed-size snapshots[SETS]/setColor[SETS]/setLabel[SETS] members.
	// Loading a hand-edited patch with >8 entries crashed (ASan: SEGV).
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

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
	Test::Harness h;
	// BUG-2: json_string_value() returns NULL for non-string values; assigning
	// it to std::string was UB (ASan: SEGV in _platform_strlen).
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

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
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	SECTION("Pressing set button 3 changes currentSet to 3") {
		m->params[TransitPadModule<>::SET_PARAM + 3].setValue(1.f);
		h.dspSteps(100);
		REQUIRE(m->currentSet == 3);
	}

	SECTION("Pressing set button 0 switches currentSet to 0") {
		// Start from a non-zero set: currentSet defaults to 0, so pressing button 0
		// from there would pass whether or not the button actually works.
		m->currentSet = 4;
		m->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
		h.dspSteps(100);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Switching between sets") {
		m->params[TransitPadModule<>::SET_PARAM + 5].setValue(1.f);
		h.dspSteps(100);
		REQUIRE(m->currentSet == 5);

		m->params[TransitPadModule<>::SET_PARAM + 5].setValue(0.f);
		m->params[TransitPadModule<>::SET_PARAM + 2].setValue(1.f);
		h.dspSteps(100);
		REQUIRE(m->currentSet == 2);
	}
}


TEST_CASE("Snapshot-set node positions", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	SECTION("Off (default): switching sets does not move pad points") {
		m->nodes.setXyImmediate(0, 0.1f, 0.2f);
		REQUIRE(m->nodePosMode == NODEPOSMODE::OFF);

		m->params[TransitPadModule<>::SET_PARAM + 3].setValue(1.f);
		h.dspSteps(100);

		REQUIRE(m->currentSet == 3);
		REQUIRE(m->nodes.getXFinal(0) == 0.1f);
		REQUIRE(m->nodes.getYFinal(0) == 0.2f);
	}

	SECTION("storeNodePositions() captures the live layout into a set") {
		// Only the *Immediate setters, i.e. exactly what a UI drag does. The
		// store must read the same UI-side fields that loadNodePositions writes.
		m->nodes.setXyImmediate(0, 0.3f, 0.4f);
		m->nodes.setRadiusImmediate(0, 0.6f);
		m->nodes.setAmountImmediate(0, 0.7f);

		m->storeNodePositions(2);

		REQUIRE(m->snapshots[2][0].x == 0.3f);
		REQUIRE(m->snapshots[2][0].y == 0.4f);
		REQUIRE(m->snapshots[2][0].radius == 0.6f);
		REQUIRE(m->snapshots[2][0].amount == 0.7f);
		// Untouched sets are unaffected.
		REQUIRE(m->snapshots[0][0].x != 0.3f);
	}

	// Regression: storeNodePositions() used getRadius()/getAmount(), which read
	// the radius[]/amount[] arrays that only process() populates — and only for
	// i < snapshotsUsed. So a capture before the first tick, or of any node above
	// the active count, stored uninitialized memory, and loading that set wrote
	// the garbage straight back into the live pad geometry.
	SECTION("Capturing before the first process() tick stores the real values") {
		m->nodes.setRadiusImmediate(0, 0.25f);
		m->nodes.setAmountImmediate(0, 0.75f);
		// Deliberately no dspStep() here.
		m->storeNodePositions(0);

		REQUIRE(m->snapshots[0][0].radius == 0.25f);
		REQUIRE(m->snapshots[0][0].amount == 0.75f);
	}

	SECTION("Capturing a node above snapshotsUsed stores the real values") {
		// process() refreshes radius[]/amount[] only for j < snapshotsUsed, so
		// node 6 is never refreshed no matter how long the module runs.
		m->snapshotsUsed = 4;
		m->nodes.setRadiusImmediate(6, 0.5f);
		m->nodes.setAmountImmediate(6, 0.6f);
		h.dspSteps(50);

		m->storeNodePositions(0);

		REQUIRE(m->snapshots[0][6].radius == 0.5f);
		REQUIRE(m->snapshots[0][6].amount == 0.6f);
	}

	SECTION("Auto mode's first set change does not corrupt the pad geometry") {
		// The worst case of the same bug: enabling Auto and switching sets before
		// any tick captured garbage into the outgoing set, and switching back
		// applied it, leaving radius/amount permanently wrong.
		m->nodePosMode = NODEPOSMODE::AUTO;
		m->changeSet(1);
		m->changeSet(0);
		h.dspSteps(20);

		REQUIRE(m->nodes.getRadiusRaw(0, 1.f) == Catch::Approx(1.f));
		REQUIRE(m->nodes.getAmountFiltered(0, 1.f) == Catch::Approx(1.f));
	}

	SECTION("loadNodePositions() applies a set's stored layout to the live pad") {
		m->snapshots[1][0].x = 0.15f;
		m->snapshots[1][0].y = 0.85f;
		m->snapshots[1][0].radius = 0.4f;
		m->snapshots[1][0].amount = 0.9f;

		m->loadNodePositions(1);
		// radius[]/amount[] (getRadius/getAmount) only refresh from the UI
		// shadow once per process() tick, same as any other UI-driven change.
		h.dspStep();

		REQUIRE(m->nodes.getXFinal(0) == 0.15f);
		REQUIRE(m->nodes.getYFinal(0) == 0.85f);
		REQUIRE(m->nodes.getRadius(0) == 0.4f);
		REQUIRE(m->nodes.getAmount(0) == 0.9f);
	}

	SECTION("Store mode: switching sets alone does not move pad points") {
		m->nodePosMode = NODEPOSMODE::STORE;
		m->snapshots[3][0].x = 0.9f;
		m->snapshots[3][0].y = 0.1f;
		m->nodes.setXyImmediate(0, 0.1f, 0.2f);

		m->params[TransitPadModule<>::SET_PARAM + 3].setValue(1.f);
		h.dspSteps(100);

		// changeSet() still applies the incoming set's stored layout in Store
		// mode (only the *capture* path is manual-only), so switching to set 3
		// loads its stored geometry.
		REQUIRE(m->currentSet == 3);
		REQUIRE(m->nodes.getXFinal(0) == 0.9f);
		REQUIRE(m->nodes.getYFinal(0) == 0.1f);
	}

	SECTION("Auto mode: switching away from a set captures its current layout") {
		m->nodePosMode = NODEPOSMODE::AUTO;
		m->nodes.setXyImmediate(0, 0.33f, 0.44f);

		m->params[TransitPadModule<>::SET_PARAM + 5].setValue(1.f);
		h.dspSteps(100);

		REQUIRE(m->currentSet == 5);
		// Set 0 (the outgoing set) captured the layout that was live just before switching.
		REQUIRE(m->snapshots[0][0].x == 0.33f);
		REQUIRE(m->snapshots[0][0].y == 0.44f);
	}

	SECTION("Auto mode: switching back restores the earlier set's captured layout") {
		m->nodePosMode = NODEPOSMODE::AUTO;
		m->nodes.setXyImmediate(0, 0.11f, 0.22f);

		m->params[TransitPadModule<>::SET_PARAM + 5].setValue(1.f);
		h.dspSteps(100);
		m->nodes.setXyImmediate(0, 0.77f, 0.88f);

		m->params[TransitPadModule<>::SET_PARAM + 5].setValue(0.f);
		m->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
		h.dspSteps(100);

		REQUIRE(m->currentSet == 0);
		REQUIRE(m->nodes.getXFinal(0) == 0.11f);
		REQUIRE(m->nodes.getYFinal(0) == 0.22f);
	}

	SECTION("clearNodePositions() resets every set's stored layout to defaults") {
		m->snapshots[4][0].x = 0.9f;
		m->snapshots[4][0].y = 0.9f;
		m->snapshots[4][0].radius = 0.9f;
		m->snapshots[4][0].amount = 0.9f;

		m->clearNodePositions();

		REQUIRE(m->snapshots[4][0].x == m->getNodePqX(0)->getDefaultValue());
		REQUIRE(m->snapshots[4][0].y == m->getNodePqY(0)->getDefaultValue());
		REQUIRE(m->snapshots[4][0].radius == m->getNodeRadiusDefault(0));
		REQUIRE(m->snapshots[4][0].amount == m->Sc::getNodeAmountDefault(0));
	}

	SECTION("storeNodePositions()/loadNodePositions() also capture and apply the Mix cursor") {
		m->setCursorXyImmediate(0, 0.2f, 0.8f);
		m->storeNodePositions(2);

		REQUIRE(m->mixX[2] == 0.2f);
		REQUIRE(m->mixY[2] == 0.8f);

		m->setCursorXyImmediate(0, 0.5f, 0.5f);
		m->loadNodePositions(2);

		REQUIRE(m->getCursorXFinal(0) == 0.2f);
		REQUIRE(m->getCursorYFinal(0) == 0.8f);
	}

	SECTION("clearNodePositions() also resets the Mix cursor to defaults") {
		m->mixX[4] = 0.9f;
		m->mixY[4] = 0.9f;

		m->clearNodePositions();

		REQUIRE(m->mixX[4] == m->paramQuantities[TransitPadModule<>::OUT_X_POS]->getDefaultValue());
		REQUIRE(m->mixY[4] == m->paramQuantities[TransitPadModule<>::OUT_Y_POS]->getDefaultValue());
	}

	SECTION("Auto mode: switching sets also carries the Mix cursor along") {
		m->nodePosMode = NODEPOSMODE::AUTO;
		m->setCursorXyImmediate(0, 0.15f, 0.95f);

		m->params[TransitPadModule<>::SET_PARAM + 6].setValue(1.f);
		h.dspSteps(100);

		REQUIRE(m->currentSet == 6);
		REQUIRE(m->mixX[0] == 0.15f);
		REQUIRE(m->mixY[0] == 0.95f);
	}

	SECTION("Store mode: re-pressing the active set's button reloads its stored layout") {
		m->nodePosMode = NODEPOSMODE::STORE;
		m->snapshots[0][0].x = 0.6f;
		m->snapshots[0][0].y = 0.7f;

		// Move the pad away from set 0's stored layout without switching sets.
		m->nodes.setXyImmediate(0, 0.05f, 0.05f);
		REQUIRE(m->currentSet == 0);

		m->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
		h.dspSteps(100);
		m->params[TransitPadModule<>::SET_PARAM + 0].setValue(0.f);
		h.dspSteps(100);

		REQUIRE(m->currentSet == 0);
		REQUIRE(m->nodes.getXFinal(0) == 0.6f);
		REQUIRE(m->nodes.getYFinal(0) == 0.7f);
	}

	SECTION("Auto mode: re-pressing the active set's button reloads without capturing the unsaved edit") {
		m->nodePosMode = NODEPOSMODE::AUTO;
		m->snapshots[0][0].x = 0.6f;
		m->snapshots[0][0].y = 0.7f;

		m->nodes.setXyImmediate(0, 0.05f, 0.05f);

		m->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
		h.dspSteps(100);

		REQUIRE(m->currentSet == 0);
		// The unsaved 0.05/0.05 edit must be discarded, not captured over
		// set 0's stored layout by an unwanted self-store.
		REQUIRE(m->nodes.getXFinal(0) == 0.6f);
		REQUIRE(m->nodes.getYFinal(0) == 0.7f);
		REQUIRE(m->snapshots[0][0].x == 0.6f);
		REQUIRE(m->snapshots[0][0].y == 0.7f);
	}

	SECTION("VOLT CV mode does not repeatedly reload the same set while steady") {
		m->nodePosMode = NODEPOSMODE::STORE;
		m->setCvMode = SETCVMODE::VOLT;
		m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;
		m->snapshots[0][0].x = 0.6f;
		m->snapshots[0][0].y = 0.7f;

		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
		h.dspStep();
		REQUIRE(m->currentSet == 0);

		// A live pad edit while CV holds steady at the same set's voltage must
		// survive: changeSet() is only meant to reload on an actual transition.
		m->nodes.setXyImmediate(0, 0.05f, 0.05f);
		h.dspSteps(50);

		REQUIRE(m->currentSet == 0);
		REQUIRE(m->nodes.getXFinal(0) == 0.05f);
		REQUIRE(m->nodes.getYFinal(0) == 0.05f);
	}
}


TEST_CASE("SET_CV_INPUT TRIG_FWD advances currentSet on each trigger", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::TRIG_FWD;
	// Keep buttons unpressed so they don't interfere
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);

	REQUIRE(m->currentSet == 0);

	SECTION("Each trigger advances by one") {
		// fireTrigger uses 3 frames each; space them out
		fireTrigger(h, m, TransitPadModule<>::SET_CV_INPUT);
		REQUIRE(m->currentSet == 1);

		fireTrigger(h, m, TransitPadModule<>::SET_CV_INPUT);
		REQUIRE(m->currentSet == 2);

		fireTrigger(h, m, TransitPadModule<>::SET_CV_INPUT);
		REQUIRE(m->currentSet == 3);
	}

	SECTION("Wraps around from last set back to 0") {
		m->currentSet = 7;
		fireTrigger(h, m, TransitPadModule<>::SET_CV_INPUT);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("No change without trigger (sustained low voltage)") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
		h.dspSteps(50);
		REQUIRE(m->currentSet == 0);
	}
}


TEST_CASE("SET_CV_INPUT VOLT mode maps 0-10V to set index", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::VOLT;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	SECTION("0V selects set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("10V selects set 7 (last)") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(10.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 7);
	}

	SECTION("Voltage clamped below 0V selects set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(-5.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Voltage clamped above 10V selects set 7") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(15.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 7);
	}

	SECTION("5V selects set 4") {
		// 5 / 10 * 8 = 4.0 -> int(4.0) = 4
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(5.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 4);
	}
}


TEST_CASE("SET_CV_INPUT C4 mode maps V/oct to set index", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::C4;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	SECTION("0V selects set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("1/12 V selects set 1 (one semitone)") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(1.f / 12.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 1);
	}

	SECTION("7/12 V selects set 7") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(7.f / 12.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 7);
	}

	SECTION("Negative voltage clamps to set 0") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(-1.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Large positive voltage clamps to set 7") {
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(5.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 7);
	}
}


TEST_CASE("SET_CV_INPUT OFF mode: input has no effect", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::OFF;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;

	SECTION("Trigger has no effect in OFF mode") {
		m->currentSet = 2;
		fireTrigger(h, m, TransitPadModule<>::SET_CV_INPUT);
		REQUIRE(m->currentSet == 2);
	}

	SECTION("High voltage has no effect in OFF mode") {
		m->currentSet = 3;
		m->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(10.f);
		h.dspSteps(5);
		REQUIRE(m->currentSet == 3);
	}

	SECTION("Buttons still work in OFF mode") {
		m->params[TransitPadModule<>::SET_PARAM + 6].setValue(1.f);
		h.dspSteps(100);
		REQUIRE(m->currentSet == 6);
	}
}


TEST_CASE("Buttons work when CV is disconnected", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->setCvMode = SETCVMODE::VOLT;
	m->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 0; // disconnected

	m->params[TransitPadModule<>::SET_PARAM + 4].setValue(1.f);
	h.dspSteps(100);

	REQUIRE(m->currentSet == 4);
}


TEST_CASE("JSON round-trip preserves setCvMode", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

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
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->snapshotsUsed = 6;
	json_t* j = m->dataToJson();
	m->snapshotsUsed = 4;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->snapshotsUsed == 6);
}


// snapshotsUsed = 0 used to be accepted on load and was unrecoverable: process()
// skips both snapshot loops, so the last weights persist forever, and the
// "Number of snapshots" submenu only offers 1..8, leaving no way back. Reachable
// from a hand-edited or corrupted patch, and from any future build whose
// SNAPSHOTS differs.
// An out-of-range nodePosMode must not be stored verbatim: changeSet() tests
// `!= OFF` and `== AUTO`, so an unknown value silently behaves as Store while
// none of the three context-menu entries shows a checkmark — leaving the user
// unable to see or change the active mode. Matches the isValidInMode() pattern
// INTERMIX uses for the same class of problem.
TEST_CASE("Corrupted nodePosMode falls back to Off on load", "[TransitPad][JSON]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	auto loadWith = [&](json_int_t value) {
		json_t* j = m->dataToJson();
		json_object_set_new(j, "nodePosMode", json_integer(value));
		m->dataFromJson(j);
		json_decref(j);
	};

	SECTION("An unknown positive value falls back to Off") {
		m->nodePosMode = NODEPOSMODE::AUTO;
		loadWith(9999);
		REQUIRE(m->nodePosMode == NODEPOSMODE::OFF);
	}

	SECTION("A negative value falls back to Off") {
		m->nodePosMode = NODEPOSMODE::AUTO;
		loadWith(-3);
		REQUIRE(m->nodePosMode == NODEPOSMODE::OFF);
	}

	SECTION("The three legal values survive the round trip") {
		for (NODEPOSMODE mode : {NODEPOSMODE::OFF, NODEPOSMODE::STORE, NODEPOSMODE::AUTO}) {
			m->nodePosMode = mode;
			json_t* j = m->dataToJson();
			m->nodePosMode = NODEPOSMODE::AUTO;
			m->dataFromJson(j);
			json_decref(j);
			REQUIRE(m->nodePosMode == mode);
		}
	}
}


TEST_CASE("Corrupted snapshotsUsed is clamped into the usable range on load", "[TransitPad][JSON]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	auto loadWith = [&](json_int_t value) {
		json_t* j = m->dataToJson();
		json_object_set_new(j, "snapshotsUsed", json_integer(value));
		m->dataFromJson(j);
		json_decref(j);
	};

	SECTION("Zero is raised to 1, so the pad is never frozen with no active snapshot") {
		loadWith(0);
		REQUIRE(m->snapshotsUsed == 1);
	}

	SECTION("Negative is raised to 1") {
		loadWith(-7);
		REQUIRE(m->snapshotsUsed == 1);
	}

	SECTION("A count above SNAPSHOTS is capped, so process() cannot run off the arrays") {
		loadWith(99);
		REQUIRE(m->snapshotsUsed == 8);
		// And the clamped module still runs.
		REQUIRE_NOTHROW(h.dspSteps(5));
	}

	SECTION("A missing key falls back to the minimum rather than 0") {
		json_t* j = m->dataToJson();
		json_object_del(j, "snapshotsUsed");
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->snapshotsUsed >= 1);
	}
}


TEST_CASE("JSON round-trip preserves currentSet", "[TransitPad]") {
	Test::Harness h;
	SECTION("Non-zero currentSet survives save/load") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 5;
		json_t* j = m->dataToJson();
		m->currentSet = 0;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 5);
	}

	SECTION("Out-of-range currentSet is clamped to valid range on load") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 0;
		// Hand-craft a JSON document with a bogus currentSet value to exercise the clamp
		json_t* j = json_pack("{s:i}", "currentSet", 999);
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == (int)m->getSetCount() - 1);
	}

	SECTION("Negative currentSet is clamped to 0 on load") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 4;
		json_t* j = json_pack("{s:i}", "currentSet", -1);
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 0);
	}

	SECTION("Missing currentSet key leaves currentSet unchanged (back-compat with old patches)") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->currentSet = 3;
		json_t* j = json_object();
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->currentSet == 3);
	}
}


TEST_CASE("JSON round-trip preserves setLabel", "[TransitPad]") {
	Test::Harness h;
	SECTION("Non-empty label survives save/load") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->setLabel[2] = "Verse";
		json_t* j = m->dataToJson();
		m->setLabel[2] = "";
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->setLabel[2] == "Verse");
	}

	SECTION("Empty label is not written; missing key on load leaves label unchanged") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
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
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->setLabel[3] = "Chorus";
		REQUIRE(m->getSetLabel(3) == "Chorus");
	}

	SECTION("getSetLabel falls back to 'Snapshot-set #N' when empty") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		REQUIRE(m->getSetLabel(0) == "Snapshot-set #1");
		REQUIRE(m->getSetLabel(4) == "Snapshot-set #5");
	}

	SECTION("SET_PARAM quantity label agrees with getSetLabel") {
		// TransitPadSetParamQuantity::getLabel() is the live, user-facing path (menu items,
		// tooltips); getSetLabel() is what other production code and tests call directly.
		// They used to disagree on the default string ("Snapshot-set #N" vs "Set #N") because
		// getLabel() duplicated the logic instead of delegating. Pin that they now always match.
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		ParamQuantity* pq = m->paramQuantities[TransitPadModule<>::SET_PARAM + 2];
		REQUIRE(pq->getLabel() == m->getSetLabel(2));

		m->setLabel[2] = "Verse";
		REQUIRE(pq->getLabel() == "Verse");
		REQUIRE(pq->getLabel() == m->getSetLabel(2));
	}

	SECTION("'label' key is omitted from JSON when no label is set") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		// Default state: no labels
		json_t* j = m->dataToJson();
		json_t* setsJ = json_object_get(j, "sets");
		REQUIRE(setsJ != NULL);
		json_t* set0J = json_array_get(setsJ, 0);
		REQUIRE(json_object_get(set0J, "label") == NULL);
		json_decref(j);
	}

	SECTION("'label' key is present in JSON only for sets that have one") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
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
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->setLabel[0] = "Intro";
	m->setLabel[3] = "Bridge";
	Module::ResetEvent re;
	m->onReset(re);
	REQUIRE(m->setLabel[0] == "");
	REQUIRE(m->setLabel[3] == "");
	REQUIRE(m->getSetLabel(0) == "Snapshot-set #1");
}


TEST_CASE("onReset restores defaults", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->currentSet = 6;
	m->snapshotsUsed = 8;
	Module::ResetEvent re;
	m->onReset(re);

	REQUIRE(m->currentSet == 0);
	REQUIRE(m->snapshotsUsed == 4);
	REQUIRE(m->isLocked() == false);
}


// The module overrode the deprecated no-arg onReset()/onRandomize(), which
// *hid* the base's event overloads: `m->onReset(e)` did not compile without an
// explicit upcast, and the `Module::onReset()` call inside the override was a
// no-op (Rack's deprecated bodies are empty). Rack itself always dispatches the
// event form, so these pin that the event form is the one that works.
TEST_CASE("Reset and randomize go through the event-form handlers", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	SECTION("ResetEvent restores defaults") {
		m->currentSet = 5;
		m->snapshotsUsed = 7;
		m->locked = true;
		m->setLabel[2] = "custom";

		Module::ResetEvent e;
		m->onReset(e);

		REQUIRE(m->currentSet == 0);
		REQUIRE(m->snapshotsUsed == 4);
		REQUIRE(m->isLocked() == false);
		REQUIRE(m->setLabel[2] == "");
	}

	SECTION("RandomizeEvent moves the snapshot node positions") {
		// Park every node at a known spot so any randomization is visible.
		for (uint8_t i = 0; i < 8; i++) m->nodes.setXyImmediate(i, 0.5f, 0.5f);

		Module::RandomizeEvent e;
		m->onRandomize(e);

		bool anyMoved = false;
		for (uint8_t i = 0; i < 8; i++) {
			if (m->getNodeXFinal(i) != 0.5f || m->getNodeYFinal(i) != 0.5f) anyMoved = true;
		}
		REQUIRE(anyMoved);
	}

	SECTION("Randomize does not latch the momentary set buttons or change the set") {
		// SET_PARAM switches are momentary; randomizing them would silently jump
		// the active set on the next buttonDivider tick.
		m->currentSet = 3;
		Module::RandomizeEvent e;
		m->onRandomize(e);
		h.dspSteps(5);

		for (uint8_t s = 0; s < 8; s++) {
			REQUIRE(m->params[TransitPadModule<>::SET_PARAM + s].getValue() == 0.f);
		}
		REQUIRE(m->currentSet == 3);
	}
}


TEST_CASE("Locked state", "[TransitPad]") {
	Test::Harness h;
	SECTION("Default state is unlocked") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		REQUIRE(m->isLocked() == false);
	}

	SECTION("onReset clears lock") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->locked = true;
		Module::ResetEvent re;
		m->onReset(re);
		REQUIRE(m->isLocked() == false);
	}

	SECTION("Lock survives save/load") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->locked = true;
		json_t* j = m->dataToJson();
		m->locked = false;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == true);
	}

	SECTION("Unlock survives save/load") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->locked = false;
		json_t* j = m->dataToJson();
		m->locked = true;
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == false);
	}

	SECTION("Missing 'locked' key on load leaves lock state unchanged (back-compat)") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		m->locked = true;
		json_t* j = json_object();
		m->dataFromJson(j);
		json_decref(j);
		REQUIRE(m->isLocked() == true);
	}
}


TEST_CASE("getCursorXFinal/getCursorYFinal track CV-driven Out position, not the UI shadow", "[TransitPad]") {
	Test::Harness h;
	// Regression: the cursor drag widget must draw from the param-backed
	// "final" position (what process() writes from CV/sequencer/ParamHandle
	// inputs), not from outUiX/outUiY, which is only ever written by a mouse
	// drag or setCursorXyImmediate/Filtered and does not move with CV.
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->inputs[TransitPadModule<>::OUT_X_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::OUT_X_INPUT].setVoltage(3.f); // → x = 3/10 + 0.5 = 0.8
	m->inputs[TransitPadModule<>::OUT_Y_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::OUT_Y_INPUT].setVoltage(-2.f); // → y = -2/10 + 0.5 = 0.3

	float outUiXBefore = m->outUiX;
	float outUiYBefore = m->outUiY;

	h.dspSteps(5);

	REQUIRE(m->getCursorXFinal(0) == Catch::Approx(0.8f).margin(0.01f));
	REQUIRE(m->getCursorYFinal(0) == Catch::Approx(0.3f).margin(0.01f));
	// The UI shadow is untouched by CV — proves it would be the wrong read source.
	REQUIRE(m->outUiX == Catch::Approx(outUiXBefore));
	REQUIRE(m->outUiY == Catch::Approx(outUiYBefore));
}


TEST_CASE("Snapshot weights: point inside radius gets nonzero weight", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Default positions: snapshot 0 at (0.1, 0.1), mix point at (0.5, 0.5)
	// Distance = sqrt(0.4^2 + 0.4^2) ≈ 0.566, default radius = 1.0 → inside
	h.dspSteps(5);

	REQUIRE(m->snapshots[0][0].weight > 0.f);
}


TEST_CASE("Snapshot weights: point outside radius gets zero weight", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Move mix point to (0.9, 0.9) via the filter state so process() respects it.
	// Snapshot 0 defaults to (0.1, 0.1).
	// Distance = sqrt(0.8^2 + 0.8^2) ≈ 1.131, default radius = 1.0 → outside
	m->setCursorXyImmediate(0, 0.9f, 0.9f);
	h.dspSteps(5);

	REQUIRE(m->snapshots[0][0].weight == 0.f);
}


TEST_CASE("Snapshot weights are written to the active set", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Default positions: snapshot 0 at (0.1,0.1), mix at (0.5,0.5) → nonzero weight
	m->currentSet = 0;
	h.dspSteps(5);
	REQUIRE(m->snapshots[0][0].weight > 0.f);
	// Other sets are untouched while set 0 is active
	REQUIRE(m->snapshots[3][0].weight == 0.f);

	// Switch to set 3 — weights are now computed into set 3
	m->currentSet = 3;
	h.dspSteps(5);
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
// Wiring only — no stepping: Transit discovers the pad (moduleChangedFlag) on its
// next tick, which is always inside h.dspSteps()/r.run(). Stepping here would
// tick the pad before the test has set it up, computing weights from default
// geometry that snapshotsUsed can never reset (only j < snapshotsUsed are
// recomputed per tick). Expander-discovery tests add their own h.dspStep().
static void connectPad(Test::Harness& h, TransitModule<12>* transit, TransitPadModule<>* pad) {
	h.connectExpander(transit, pad);
	transit->setProcessDivision(1);
}

// Helper: bind a parameter and flush the task queue into sourceHandles.
// taskProcessorDsp.process() applies the bind synchronously, but the trailing
// Transit tick is still required: it runs the moduleChangedFlag discovery block,
// which initializes presetTotal — presetSave() dereferences getSlot() and SEGVs
// without it. The tick is Transit-only and direct (not h.dspStep()): the harness
// steps every registered module, and ticking the pad before the test has set it
// up would compute weights from default geometry that snapshotsUsed can never
// reset. Transit ignores args.frame, so this is behavior-identical to a harness
// step for Transit — same deliberate exception as MidiCatMem's pre-flip asserts.
static void bindParam(Test::Harness& h, TransitModule<12>* transit, int moduleId, int paramId) {
	(void)h;
	transit->bindAddParameterRequest(moduleId, paramId);
	transit->taskProcessorDsp.process();
	transit->process(Test::makeProcessArgs(0));
}

// Standard rig for the end-to-end tests: Transit + TransitPad expander + a
// target module whose PARAM_A Transit binds and drives. Call connectPad()
// after saving presets (same setup order as the tests below).
// The harness is borrowed from the test body (Harness is non-copyable, so the
// rig cannot own it); transit + pad are added pad-first so the harness ticks
// the pad before Transit (it steps in registration order), matching the old
// manual sequence where the pad computed snapshot weights before Transit read
// them. The target is adopted by the harness too, so all three share its
// exception-safe teardown.
struct PadRig {
	Test::Harness& h;
	TransitPadModule<>* pad = nullptr;
	TransitModule<12>* transit = nullptr;
	TestParamModule* target = nullptr;

	explicit PadRig(Test::Harness& h) : h(h) {}

	static PadRig make(Test::Harness& h) {
		PadRig r(h);
		r.pad = r.h.addModule<TransitPadModule<>>("TransitPad");
		r.transit = r.h.addModule<TransitModule<12>>("Transit");
		r.target = r.h.adoptModule(new TestParamModule);
		return r;
	}
	void bind() {
		bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	}
	void save(int slot, float value) {
		target->params[TestParamModule::PARAM_A].setValue(value);
		transit->presetSave(slot);
	}
	float paramValue() {
		return target->params[TestParamModule::PARAM_A].getValue();
	}
	void run(int n) {
		h.dspSteps(n);
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
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	// Flush initial expandersChanged so transitPad is properly initialised to nullptr
	h.dspStep();
	REQUIRE_FALSE(transit->isXyPadActive());

	connectPad(h, transit, pad);
	h.dspStep();

	REQUIRE(transit->isXyPadActive());
	// TransitPad sets masterModule back-pointer
	REQUIRE(pad->masterModule == transit);
}


TEST_CASE("Transit sets slotCvMode to OFF when TransitPad is connected", "[TransitPad][Transit]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	transit->slotCvMode = SLOTCVMODE::TRIG_FWD;
	connectPad(h, transit, pad);
	h.dspStep();

	REQUIRE(transit->slotCvMode == SLOTCVMODE::OFF);
}


TEST_CASE("Transit disconnects from TransitPad when expander is removed", "[TransitPad][Transit]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	connectPad(h, transit, pad);
	h.dspStep();
	REQUIRE(transit->isXyPadActive());

	// Disconnect
	h.disconnectExpander(transit, Test::Harness::SIDE_RIGHT);
	h.dspStep();

	REQUIRE_FALSE(transit->isXyPadActive());
}


TEST_CASE("presetProcessXyPad: single snapshot with full weight applies preset exactly", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	// Bind target parameter and save preset 0 with value 0.25
	bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	target->params[TestParamModule::PARAM_A].setValue(0.25f);
	transit->presetSave(0);

	// Connect pad, set snapshot 0 → preset slot 0, weight 1.0
	connectPad(h, transit, pad);
	pad->snapshots[0][0].id = 0;
	pad->snapshots[0][0].weight = 1.f;

	// Drive target param away so we can verify Transit writes it
	target->params[TestParamModule::PARAM_A].setValue(0.99f);
	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: two equal-weight snapshots produce the midpoint", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Save preset 0 = 0.2, preset 1 = 0.8
	target->params[TestParamModule::PARAM_A].setValue(0.2f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(0.8f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// snapshots[0][0] → slot 0, snapshots[0][1] → slot 1 (ids set by initExtra)
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 1.f;

	h.dspSteps(5);

	// (0.2 * 1 + 0.8 * 1) / (1 + 1) = 0.5
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.5f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: unequal weights produce correctly weighted average", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// preset 0 = 0.0, preset 1 = 1.0
	target->params[TestParamModule::PARAM_A].setValue(0.f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(1.f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// Weight 1:3 toward preset 1
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 3.f;

	h.dspSteps(5);

	// (0.0 * 1 + 1.0 * 3) / (1 + 3) = 0.75
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.75f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: snapshot with id=-1 is skipped", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Only preset 1 saved
	target->params[TestParamModule::PARAM_A].setValue(0.7f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// snapshot 0: id=-1 (unbound), snapshot 1: id=1 with full weight
	pad->snapshots[0][0].id = -1;
	pad->snapshots[0][0].weight = 1.f; // weight set but id is -1 → ignored
	pad->snapshots[0][1].weight = 1.f; // this one should take effect

	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.7f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: snapshot pointing to unused slot is skipped", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Only preset 1 saved; preset 0 is empty
	target->params[TestParamModule::PARAM_A].setValue(0.6f);
	transit->presetSave(1);

	// Set a sentinel value to detect if the param gets written
	target->params[TestParamModule::PARAM_A].setValue(0.42f);

	connectPad(h, transit, pad);
	// snapshot 0 → empty slot 0 (not saved), weight 1.0 → should be skipped
	// snapshot 1 → slot 1 with weight 0 → skipped too
	// Total weight = 0 → no write → param stays at 0.42
	pad->snapshots[0][0].weight = 1.f; // points at slot 0 which is unused

	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.42f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: all zero weights leave parameters unchanged", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	target->params[TestParamModule::PARAM_A].setValue(0.3f);
	transit->presetSave(0);

	target->params[TestParamModule::PARAM_A].setValue(0.55f);

	connectPad(h, transit, pad);
	// All snapshot weights remain 0 (initialized that way in initExtra)

	h.dspSteps(5);

	// No write should occur → param stays at 0.55
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.55f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: switching TransitPad sets changes interpolation output", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// preset 0 = 0.1, preset 1 = 0.9
	target->params[TestParamModule::PARAM_A].setValue(0.1f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(0.9f);
	transit->presetSave(1);

	connectPad(h, transit, pad);

	// Set 0: snapshot 0 → preset 0, weight 1.0
	pad->snapshots[0][0].weight = 1.f;
	// Set 1: snapshot 1 → preset 1, weight 1.0
	pad->snapshots[1][1].weight = 1.f;

	// Activate set 0
	pad->currentSet = 0;
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.1f).margin(0.001f));

	// Activate set 1
	pad->currentSet = 1;
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.9f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: interpolates two bound parameters independently", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	bindParam(h, transit, target->id, TestParamModule::PARAM_B);

	// preset 0: A=0.2, B=0.8 / preset 1: A=0.6, B=0.4
	target->params[TestParamModule::PARAM_A].setValue(0.2f);
	target->params[TestParamModule::PARAM_B].setValue(0.8f);
	transit->presetSave(0);

	target->params[TestParamModule::PARAM_A].setValue(0.6f);
	target->params[TestParamModule::PARAM_B].setValue(0.4f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// Equal weights → midpoint for both params
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 1.f;

	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.4f).margin(0.001f));
	REQUIRE(target->params[TestParamModule::PARAM_B].getValue() == Catch::Approx(0.6f).margin(0.001f));
}


// ============================================================
// End-to-end signal chain:
// mix position (CV/sequence) → dist[] → radius/amount → weight → Transit param
// Unlike the tests above, the weights are never assigned directly — they are
// computed by pad->process() from the mix-position inputs.
// ============================================================

TEST_CASE("XY-pad chain: mix position CV drives the target parameter between presets", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Default layout: snapshot A at (0,0) bound to slot 0, B at (1,0) bound to slot 1
	r.pad->snapshotsUsed = 2;
	connectMixInputs(r.pad);

	// Mix point on corner A → only preset 0 contributes
	setMixVoltage(r.pad, -5.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Mix point on corner B → only preset 1 contributes
	setMixVoltage(r.pad, 5.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	// Mix point halfway between them → equal weights → midpoint
	setMixVoltage(r.pad, 0.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));
}


TEST_CASE("XY-pad chain: amount scales snapshot weight and shifts the blend", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Both snapshots equidistant (0.5) from the mix point at (0.5, 0)
	r.pad->snapshotsUsed = 2;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, 0.f, -5.f);

	// Default amount 1.0: equal weights → midpoint blend
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx(0.55f).margin(0.001f));
	REQUIRE(r.pad->snapshots[0][1].weight == Catch::Approx(0.55f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	// Halving snapshot B's amount halves its weight and pulls the blend toward A:
	// (0 * 0.55 + 1 * 0.275) / (0.55 + 0.275) = 1/3
	r.pad->nodes.setAmountImmediate(1, 0.5f);
	r.run(5);
	REQUIRE(r.pad->snapshots[0][1].weight == Catch::Approx(0.275f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(1.f / 3.f).margin(0.001f));
}


TEST_CASE("XY-pad chain: radius cuts off snapshot contribution at the boundary", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.25f);
	connectPad(h, r.transit, r.pad);

	// Snapshot A at (0,0); the mix point moves along the x-axis so dist == mix.x
	// (X voltage → mix.x = v/10 + 0.5)
	r.pad->snapshotsUsed = 1;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, 0.f, -5.f);

	// Default radius 1.0: dist 0.5 is well inside
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx(0.55f).margin(0.001f));

	// Shrinking the radius to 0.6 shrinks the weight at the same point
	r.pad->nodes.setRadiusImmediate(0, 0.6f);
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx((0.6f - 0.5f) / 0.6f * 1.1f).margin(0.001f));

	// Outside the radius the weight is exactly zero and nothing is written
	setMixVoltage(r.pad, 2.f, -5.f);
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == 0.f);
	r.target->params[TestParamModule::PARAM_A].setValue(0.9f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.9f).margin(0.001f));

	// Back inside the radius the preset value takes over again
	setMixVoltage(r.pad, 0.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.25f).margin(0.001f));
}


TEST_CASE("XY-pad chain: switching sets via button and CV changes the Transit output", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Snapshot A sits near the mix point with a nonzero weight in every set;
	// which preset it reaches depends on the per-set binding
	r.pad->snapshotsUsed = 1;

	// Set 0 keeps the default binding to slot 0
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Switch to set 1 via button, then rebind snapshot A to slot 1 there
	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(1.f);
	r.run(100);
	REQUIRE(r.pad->currentSet == 1);
	r.pad->bindSnapshot(0, 1);
	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(0.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	// Back to set 0 via button
	r.pad->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
	r.run(100);
	REQUIRE(r.pad->currentSet == 0);
	r.pad->params[TransitPadModule<>::SET_PARAM + 0].setValue(0.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Set selection via CV in VOLT mode: 1.25V → set 1, 0V → set 0
	r.pad->setCvMode = SETCVMODE::VOLT;
	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;
	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(1.25f);
	r.run(5);
	REQUIRE(r.pad->currentSet == 1);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
	r.run(5);
	REQUIRE(r.pad->currentSet == 0);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));
}


TEST_CASE("XY-pad chain: motion sequence drives the mix position", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);

	SECTION("Phase input sweeps the mix point along the sequence") {
		r.bind();
		// Snapshot A at (0,0) → slot 0, C at (1,1) → slot 2; slot 1 stays unused
		r.save(0, 0.0f);
		r.save(2, 1.0f);
		connectPad(h, r.transit, r.pad);
		r.pad->snapshotsUsed = 3;

		// Two-point linear sequence along the A→C diagonal
		r.pad->seqData[0][0].length = 2;
		r.pad->seqData[0][0].x[0] = 0.f; r.pad->seqData[0][0].y[0] = 0.f;
		r.pad->seqData[0][0].x[1] = 1.f; r.pad->seqData[0][0].y[1] = 1.f;

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].channels = 1;

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].setVoltage(0.f);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].setVoltage(10.f);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

		r.pad->inputs[TransitPadModule<>::OUT_SEQ_PH_INPUT].setVoltage(5.f);
		r.run(5);
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
		r.h.dspSteps(5);
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(0.f).margin(0.001f));
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(0.f).margin(0.001f));

		fireTrigger(r.h, r.pad, TransitPadModule<>::OUT_SEQ_INPUT);
		REQUIRE(r.pad->seqSelected[0] == 1);

		r.h.dspSteps(5);
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(1.f).margin(0.001f));
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(0.f).margin(0.001f));
	}
}


TEST_CASE("XY-pad chain: snapshotsUsed bounds which snapshots contribute weight", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	// Slots 0,1 = 0.0 and slots 2,3 = 1.0; snapshots A–D sit at the four
	// corners, all equidistant from the mix point at the centre (0.5, 0.5)
	r.save(0, 0.0f);
	r.save(1, 0.0f);
	r.save(2, 1.0f);
	r.save(3, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Only A/B are active, so only they contribute: both hold 0.0
	r.pad->snapshotsUsed = 2;
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Raising the count lets C/D join the blend
	r.pad->snapshotsUsed = 4;
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	// ...and lowering it again drops them back out. This is the direction that
	// used to be broken: a snapshot that had already earned a weight kept it
	// forever, so C/D went on blending after the user shrank the pad and they
	// were no longer drawn or draggable.
	r.pad->snapshotsUsed = 2;
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));
}


// Regression: lowering "Number of snapshots" must stop the now-inactive pad
// points from contributing. Before the fix, process() only ever wrote weights
// for j < snapshotsUsed, so a snapshot that had earned a weight while the count
// was high kept that weight indefinitely — presetProcessXyPad iterates all of
// getPadFactors(), not just the active prefix, so TRANSIT kept blending a pad
// point that had vanished from the screen.
TEST_CASE("Lowering snapshotsUsed clears the weights of the now-inactive snapshots", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(4, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Pad point A (bound to slot 0, value 0.0) and pad point E (bound to slot 4,
	// value 1.0) both sit exactly on the mix point, so both reach full weight
	// and the blend lands halfway between the two presets.
	r.pad->snapshotsUsed = 8;
	r.pad->snapshots[r.pad->currentSet][4].id = 4;
	r.pad->nodes.setXyImmediate(0, 0.5f, 0.5f);
	r.pad->nodes.setXyImmediate(4, 0.5f, 0.5f);
	r.run(20);
	REQUIRE(r.pad->snapshots[r.pad->currentSet][4].weight == Catch::Approx(1.0f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	// Shrink the pad to A/B only. E is no longer an active pad point, so its
	// weight must be cleared and the blend must fall back to A alone.
	r.pad->snapshotsUsed = 2;
	r.run(20);
	REQUIRE(r.pad->snapshots[r.pad->currentSet][4].weight == 0.f);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Same via the patch-load path: dataFromJson writes snapshotsUsed directly,
	// so it has to be covered by the same clearing and not only by the menu.
	r.pad->snapshotsUsed = 8;
	r.run(20);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	json_t* rootJ = r.pad->dataToJson();
	json_object_set_new(rootJ, "snapshotsUsed", json_integer(2));
	r.pad->dataFromJson(rootJ);
	json_decref(rootJ);
	r.run(20);
	REQUIRE(r.pad->snapshots[r.pad->currentSet][4].weight == 0.f);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));
}


TEST_CASE("bindSnapshot binds and unbinds pad points to Transit slots", "[TransitPad]") {
	Test::Harness h;
	SECTION("Binding semantics") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

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
		Test::Harness h;
		PadRig r = PadRig::make(h);
		r.bind();
		r.save(5, 0.77f);
		connectPad(h, r.transit, r.pad);

		// Park the mix point on snapshot A (weight saturates at 1.0)
		r.pad->snapshotsUsed = 1;
		connectMixInputs(r.pad);
		setMixVoltage(r.pad, -5.f, -5.f);

		r.pad->bindSnapshot(0, 5);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(0.77f).margin(0.001f));

		// Unbinding removes the last contribution → no write happens
		r.target->params[TestParamModule::PARAM_A].setValue(0.42f);
		r.pad->bindSnapshot(0, -1);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(0.42f).margin(0.001f));
	}
}


// TransitPad has exactly one cursor (the Out point), always at id 0; confirm
// an out-of-range id is a silent no-op rather than acting as if it addressed
// Out, since a stray id reaching storage would otherwise corrupt it.

TEST_CASE("setCursorXyImmediate with an out-of-range id is a silent no-op", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->setCursorXyImmediate(0, 0.2f, 0.3f);
	float xBefore = m->params[TransitPadModule<>::OUT_X_POS].getValue();
	float yBefore = m->params[TransitPadModule<>::OUT_Y_POS].getValue();

	REQUIRE_NOTHROW(m->setCursorXyImmediate(1, 0.9f, 0.9f));

	REQUIRE(m->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(xBefore));
	REQUIRE(m->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(yBefore));
}

TEST_CASE("setCursorXyFiltered with an out-of-range id is a silent no-op", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->setCursorXyImmediate(0, 0.2f, 0.3f);
	float xBefore = m->outUiX;
	float yBefore = m->outUiY;

	REQUIRE_NOTHROW(m->setCursorXyFiltered(1, 0.9f, 0.9f));

	REQUIRE(m->outUiX == Catch::Approx(xBefore));
	REQUIRE(m->outUiY == Catch::Approx(yBefore));
}

TEST_CASE("XyScreenNodes setters with an out-of-range id are a silent no-op", "[TransitPad]") {
	Test::Harness h;
	// The node side of the same bound (COUNT, i.e. SNAPSHOTS here) predates
	// this stage — XyScreenNodes has always guarded on its own COUNT — but
	// had no direct test. Cover it alongside the cursor-side fix above.
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

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


// bindAddParameterRequest(presetLoading = true) skips
// the back-fill loop that keeps every slot's preset vector in sync with
// sourceHandles, so an older slot's preset can end up shorter than
// sourceHandles. presetProcessXyPad indexed that short vector by i
// unguarded (heap-buffer-overflow under ASan); presetProcess already had
// the `size() <= i` guard. Drive the real sequence rather than
// hand-shortening the vector, so the test tracks the actual patch-load
// code path. The sibling presetProcessPhase regression lives in
// Transit.test.cpp, since neither site is pad-specific but this one needs
// a TransitPad expander to reach.
// The out-of-bounds read does not reliably abort under ASan in this harness
// (confirmed on the sibling presetProcessPhase case: the redzone byte is
// genuinely poisoned per __asan_address_is_poisoned, but the generated
// check at this call site does not trip, unlike an isolated repro of the
// same pattern). So this asserts the documented contract behaviourally: the
// second (newer) parameter must never be written while it lacks a
// same-sized preset entry, and it is given a sentinel value no crossfade of
// target's values could ever produce.

TEST_CASE("presetProcessXyPad does not write a param whose preset is shorter than sourceHandles", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	TestParamModule* target2 = h.adoptModule(new TestParamModule);

	// Bind one param, save slot 0: sourceHandles.size() == 1, preset[0].size() == 1
	r.bind();
	r.save(0, 0.5f);
	connectPad(h, r.transit, r.pad);

	// Bind a second param with presetLoading = true, as the patch-load path
	// does: sourceHandles.size() == 2, but preset[0].size() is still 1.
	r.transit->bindAddParameterRequest(target2->id, TestParamModule::PARAM_A, true);
	r.transit->taskProcessorDsp.process();
	target2->params[TestParamModule::PARAM_A].setValue(0.f);
	target2->params[TestParamModule::PARAM_A].setValue(1.f);

	// Park the mix point on snapshot A, the one bound to slot 0.
	r.pad->snapshotsUsed = 1;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, -5.f, -5.f);

	// Run the pad over slot 0. Before the fix this reads preset[0][1] out of
	// bounds and writes whatever it finds there into target2's param; after
	// the fix the loop breaks at i == 1 and target2 is left untouched.
	REQUIRE_NOTHROW(r.run(5));
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));
	REQUIRE(target2->params[TestParamModule::PARAM_A].getValue() == 1.f);
}


// TransitPadSnapshotDragWidget::onDragDrop never
// consulted isLocked(), so a locked pad still accepted drag-and-drop
// rebinding from a TRANSIT snapshot button — contradicting the manual's
// documented "dropping is still allowed to highlight a target, but the
// binding is rejected" behaviour.
// This builds the real widget tree (TRANSIT + TransitPad as expanders) and
// dispatches DragEnter/DragDrop directly at the target node, per the
// headless traps documented in FRAMEWORK.md: VCVButton's onDragStart needs
// settings::allowCursorLock = false, and Knob::onDragMove (VCVButton's
// base) calls the un-early-out'd APP->window->getMods(), so a full
// h.events().drag() from the TransitLedButton can't be driven end-to-end.
// Dispatching DragEnter/DragDrop directly at the pad node is the handler
// under test anyway.

// Helper: find a TransitLedButton param widget on a TransitWidget by absolute slot index.
static TransitSnapshotButton* findSnapshotButton(rack::app::ModuleWidget* transitWidget, int slot) {
	for (rack::widget::Widget* w : transitWidget->getParams()) {
		auto* btn = dynamic_cast<TransitLedButton<12>*>(w);
		if (btn && btn->getSlotIndex() == slot) return btn;
	}
	return nullptr;
}

// Helper: find the TransitPad's node drag widget for a given pad point id.
static rack::widget::Widget* findPadNodeWidget(rack::app::ModuleWidget* padWidget, int id) {
	rack::widget::Widget* found = nullptr;
	Test::traversal::walk(padWidget, [&](const Test::traversal::Visit& v) {
		auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(v.widget);
		if (node && node->id == id) {
			found = v.widget;
			return false;
		}
		return true;
	});
	return found;
}

TEST_CASE("Locked pad rejects drag-and-drop rebinding", "[TransitPad][BLOCKER-2]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitWidget<12>* transitWidget = h.addWidget<TransitWidget<12>>(transit);
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	// Drag from slot 3's button, distinct from the slot-0 baseline below, so an
	// erroneously-accepted rebind is observable as a change.
	TransitSnapshotButton* srcButton = findSnapshotButton(transitWidget, 3);
	REQUIRE(srcButton != nullptr);
	rack::widget::Widget* node = findPadNodeWidget(padWidget, 0);
	REQUIRE(node != nullptr);

	// Bind pad point 0 to slot 0 while unlocked, establishing a known baseline.
	pad->bindSnapshot(0, 0);
	REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);

	pad->locked = true;

	event::DragEnter eEnter;
	eEnter.button = GLFW_MOUSE_BUTTON_LEFT;
	eEnter.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragEnter(eEnter);

	// The manual promises dropping "is still allowed to highlight a target" while
	// locked: hovering a snapshot button over the node still arms the highlight.
	auto* dragNode = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(node);
	REQUIRE(dragNode->dropArmed == true);

	event::DragDrop eDrop;
	eDrop.button = GLFW_MOUSE_BUTTON_LEFT;
	eDrop.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragDrop(eDrop);

	// ...but the binding itself is rejected.
	REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);
}

TEST_CASE("Unlocked pad accepts drag-and-drop rebinding", "[TransitPad][BLOCKER-2]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitWidget<12>* transitWidget = h.addWidget<TransitWidget<12>>(transit);
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	TransitSnapshotButton* srcButton = findSnapshotButton(transitWidget, 3);
	REQUIRE(srcButton != nullptr);
	rack::widget::Widget* node = findPadNodeWidget(padWidget, 0);
	REQUIRE(node != nullptr);

	REQUIRE(pad->isLocked() == false);

	event::DragEnter eEnter;
	eEnter.button = GLFW_MOUSE_BUTTON_LEFT;
	eEnter.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragEnter(eEnter);

	event::DragDrop eDrop;
	eDrop.button = GLFW_MOUSE_BUTTON_LEFT;
	eDrop.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragDrop(eDrop);

	REQUIRE(pad->snapshots[pad->currentSet][0].id == 3);
}


// Helper: find the pad's XY screen widget.
static TransitPadXyScreenWidget<TransitPadModule<>>* findPadScreenWidget(rack::app::ModuleWidget* padWidget) {
	TransitPadXyScreenWidget<TransitPadModule<>>* found = nullptr;
	Test::traversal::walk(padWidget, [&](const Test::traversal::Visit& v) {
		auto* screen = dynamic_cast<TransitPadXyScreenWidget<TransitPadModule<>>*>(v.widget);
		if (screen) {
			found = screen;
			return false;
		}
		return true;
	});
	return found;
}

// Helper: count the ui::MenuOverlay children currently on the scene.
// Always compared as a delta — earlier test cases in the same process leave
// overlays behind, so the absolute count is not meaningful.
static int menuOverlayCount() {
	int n = 0;
	for (rack::widget::Widget* c : APP->scene->children) {
		if (dynamic_cast<rack::ui::MenuOverlay*>(c)) n++;
	}
	return n;
}

// Regression for the `e.button == GLFW_PRESS` / `e.action == GLFW_PRESS` mixup:
// because GLFW_PRESS == 1 == GLFW_MOUSE_BUTTON_RIGHT, the condition collapsed to
// `e.button == 1` and fired on press *and* release, so one right-click on the
// empty screen area built the context menu twice. Note this covers XyScreenWidget,
// which ARENA uses as well.
TEST_CASE("One right-click on the pad screen builds exactly one context menu", "[TransitPad]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* screen = findPadScreenWidget(padWidget);
	REQUIRE(screen != nullptr);

	// Top-left corner of the screen: inside the widget but clear of every pad
	// point, so the press reaches the screen's own handler rather than a node's.
	const Vec emptySpot = Vec(5.f, 5.f);
	const int base = menuOverlayCount();

	event::Button ePress;
	rack::widget::EventContext cPress;
	ePress.context = &cPress;
	ePress.button = GLFW_MOUSE_BUTTON_RIGHT;
	ePress.action = GLFW_PRESS;
	ePress.pos = emptySpot;
	screen->onButton(ePress);
	const int afterPress = menuOverlayCount();

	event::Button eRelease;
	rack::widget::EventContext cRelease;
	eRelease.context = &cRelease;
	eRelease.button = GLFW_MOUSE_BUTTON_RIGHT;
	eRelease.action = GLFW_RELEASE;
	eRelease.pos = emptySpot;
	screen->onButton(eRelease);
	const int afterRelease = menuOverlayCount();

	// The press opens the menu...
	REQUIRE(afterPress - base == 1);
	// ...and the release must not open a second one.
	REQUIRE(afterRelease - afterPress == 0);
}


// "Lock pad" is tested for persistence elsewhere; this covers what it is *for*.
TEST_CASE("Locked pad refuses a left press on the screen", "[TransitPad]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* screen = findPadScreenWidget(padWidget);
	REQUIRE(screen != nullptr);

	const Vec spot = Vec(5.f, 5.f);

	SECTION("Unlocked: the press passes through to XyScreenWidget") {
		REQUIRE(pad->isLocked() == false);
		event::Button e;
		rack::widget::EventContext c;
		e.context = &c;
		e.button = GLFW_MOUSE_BUTTON_LEFT;
		e.action = GLFW_PRESS;
		e.pos = spot;
		screen->onButton(e);
		// XyScreenWidget clears the selection on an empty-area left press; the
		// lock short-circuit returns before that ever runs.
		REQUIRE(c.target != screen);
	}

	SECTION("Locked: the screen consumes the press itself, so no node can be dragged") {
		pad->locked = true;
		event::Button e;
		rack::widget::EventContext c;
		e.context = &c;
		e.button = GLFW_MOUSE_BUTTON_LEFT;
		e.action = GLFW_PRESS;
		e.pos = spot;
		screen->onButton(e);
		REQUIRE(c.target == screen);
	}

	SECTION("Locked: right-click still opens the screen context menu") {
		pad->locked = true;
		const int base = menuOverlayCount();
		event::Button e;
		rack::widget::EventContext c;
		e.context = &c;
		e.button = GLFW_MOUSE_BUTTON_RIGHT;
		e.action = GLFW_PRESS;
		e.pos = spot;
		screen->onButton(e);
		REQUIRE(menuOverlayCount() - base == 1);
	}
}


// Space toggles visualize mode. The null-module case is the regression: the
// module browser builds this widget with module == nullptr to render the
// preview, and onHoverKey dereferenced it unconditionally, so a space press
// while the browser preview was hovered segfaulted.
TEST_CASE("Space toggles visualize mode", "[TransitPad]") {
	settings::allowCursorLock = false;

	SECTION("With a module, space flips vizMode and consumes the event") {
		Test::Harness h;
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

		REQUIRE(pad->vizMode == false);

		event::HoverKey e;
		rack::widget::EventContext c;
		e.context = &c;
		e.key = GLFW_KEY_SPACE;
		e.action = GLFW_PRESS;
		e.mods = 0;
		padWidget->onHoverKey(e);
		REQUIRE(pad->vizMode == true);
		REQUIRE(c.target == padWidget);

		// A second press toggles it back off.
		rack::widget::EventContext c2;
		e.context = &c2;
		padWidget->onHoverKey(e);
		REQUIRE(pad->vizMode == false);
	}

	SECTION("A modifier-held space is not the visualize shortcut") {
		Test::Harness h;
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

		event::HoverKey e;
		rack::widget::EventContext c;
		e.context = &c;
		e.key = GLFW_KEY_SPACE;
		e.action = GLFW_PRESS;
		e.mods = RACK_MOD_CTRL;
		padWidget->onHoverKey(e);
		REQUIRE(pad->vizMode == false);
	}

	SECTION("The browser preview (module == nullptr) survives a space press") {
		Test::Harness h;
		TransitPadWidget* padWidget = Test::createWidget<TransitPadWidget>("TransitPad");
		REQUIRE(padWidget->module == nullptr);

		event::HoverKey e;
		rack::widget::EventContext c;
		e.context = &c;
		e.key = GLFW_KEY_SPACE;
		e.action = GLFW_PRESS;
		e.mods = 0;
		REQUIRE_NOTHROW(padWidget->onHoverKey(e));
		Test::destroyWidget(padWidget);
	}
}