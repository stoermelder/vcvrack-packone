// TransitPad module-only tests: construction, JSON persistence, SET_PARAM/
// SET_CV_INPUT handling, node-position storage, reset/randomize, lock state.
// Split out of a single TransitPad.test.module.hpp; see TransitPad.test.cpp
// for how this file is wired into the test binary.

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


// Regression: process() reads the Mix cursor from the UI-shadow state
// (outUiX/outXfilter) rather than from params[OUT_X_POS] whenever nothing is
// CV/param-map-bound to it, and Rack's own paramsFromJson() only restores the
// param. dataFromJson() must therefore resync the shadow state, or the next
// tick overwrites the restored param and the cursor snaps back to center.
//
// The resync must come from the restored param in every nodePosMode, never
// from mixX[currentSet]: that is only the set's last capture (Store: the last
// "Store positions"; Auto: the moment the set was last left), while the pad
// points themselves restore their live positions from "nodes". Restoring the
// cursor from the capture moved it on every save/load in which it had been
// dragged since, silently changing what TRANSIT blends.
//
// All sections go through Module::toJson()/fromJson(), so the load runs in
// Rack's real order: paramsFromJson() first, then dataFromJson().
TEST_CASE("dataFromJson() applies the restored Mix cursor to the live UI-shadow state", "[TransitPad][JSON]") {
	Test::Harness h;
	typedef TransitPadModule<> M;

	// Save m and load the patch into a fresh module, as Rack does on patch load.
	auto reload = [&](M* m) {
		json_t* j = m->toJson();
		M* m2 = h.addModule<M>("TransitPad");
		m2->fromJson(j);
		json_decref(j);
		return m2;
	};

	for (NODEPOSMODE mode : {NODEPOSMODE::OFF, NODEPOSMODE::STORE, NODEPOSMODE::AUTO}) {
		DYNAMIC_SECTION("Cursor moved since the last capture survives save/load, nodePosMode " << (int)mode) {
			M* m = h.addModule<M>("TransitPad");
			m->nodePosMode = mode;
			// A capture at another position first, so a restore from mixX[0]
			// is distinguishable from the center default too.
			m->setCursorXyImmediate(0, 0.7f, 0.3f);
			m->storeNodePositions(0);
			m->setCursorXyImmediate(0, 0.2f, 0.8f);
			m->nodes.setXyImmediate(0, 0.35f, 0.45f);
			h.dspSteps(200);

			M* m2 = reload(m);
			h.dspSteps(200);

			REQUIRE(m2->getCursorXFinal(0) == Catch::Approx(0.2f).margin(0.01f));
			REQUIRE(m2->getCursorYFinal(0) == Catch::Approx(0.8f).margin(0.01f));
			// ...consistent with the pad points, which restore their live positions.
			REQUIRE(m2->nodes.getXFinal(0) == Catch::Approx(0.35f).margin(0.01f));
			REQUIRE(m2->nodes.getYFinal(0) == Catch::Approx(0.45f).margin(0.01f));
		}
	}

	SECTION("The per-set captures still survive and apply on the next set change") {
		M* m = h.addModule<M>("TransitPad");
		m->nodePosMode = NODEPOSMODE::STORE;
		m->setCursorXyImmediate(0, 0.1f, 0.1f);
		m->storeNodePositions(0);
		m->setCursorXyImmediate(0, 0.9f, 0.6f);
		m->storeNodePositions(3);
		m->currentSet = 3;
		h.dspSteps(200);

		M* m2 = reload(m);
		REQUIRE(m2->currentSet == 3);
		REQUIRE(m2->mixX[0] == 0.1f);
		REQUIRE(m2->mixY[0] == 0.1f);
		h.dspSteps(200);
		REQUIRE(m2->getCursorXFinal(0) == Catch::Approx(0.9f).margin(0.01f));
		REQUIRE(m2->getCursorYFinal(0) == Catch::Approx(0.6f).margin(0.01f));

		m2->changeSet(0);
		h.dspSteps(200);
		REQUIRE(m2->getCursorXFinal(0) == Catch::Approx(0.1f).margin(0.01f));
		REQUIRE(m2->getCursorYFinal(0) == Catch::Approx(0.1f).margin(0.01f));
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


// Regression: the VOLT/C4 paths called changeSet() with the CV's set on every
// sample, so a set-button press was undone on the very next tick and the
// buttons were dead while the CV was patched. The CV must only take over again
// once the set it selects actually changes.
TEST_CASE("Set buttons override a steady VOLT/C4 set CV until the CV changes", "[TransitPad]") {
	Test::Harness h;
	typedef TransitPadModule<> M;

	struct Case { SETCVMODE mode; float setA; float setB; };
	// Voltages selecting set 1 and set 6 in each mode.
	for (Case c : {Case{SETCVMODE::VOLT, 1.5f, 7.6f}, Case{SETCVMODE::C4, 1.f / 12.f, 6.f / 12.f}}) {
		DYNAMIC_SECTION("setCvMode " << (int)c.mode) {
			M* m = h.addModule<M>("TransitPad");
			m->setCvMode = c.mode;
			Input& in = m->inputs[M::SET_CV_INPUT];
			in.channels = 1;
			in.setVoltage(c.setA);
			h.dspSteps(100);
			REQUIRE(m->currentSet == 1);

			auto press = [&](int s) {
				m->params[M::SET_PARAM + s].setValue(1.f);
				h.dspSteps(200);
				m->params[M::SET_PARAM + s].setValue(0.f);
				h.dspSteps(200);
			};

			press(4);
			REQUIRE(m->currentSet == 4);

			// A new CV value takes over again...
			in.setVoltage(c.setB);
			h.dspSteps(100);
			REQUIRE(m->currentSet == 6);

			// ...and so does a CV returning to the set it selected before.
			press(2);
			REQUIRE(m->currentSet == 2);
			in.setVoltage(c.setA);
			h.dspSteps(100);
			REQUIRE(m->currentSet == 1);

			// Re-patching applies the CV even though its set didn't change.
			press(3);
			in.channels = 0;
			h.dspSteps(100);
			REQUIRE(m->currentSet == 3);
			in.channels = 1;
			h.dspSteps(100);
			REQUIRE(m->currentSet == 1);
		}
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


// snapshotsUsed = 0 used to be accepted on load and was unrecoverable: process()
// skips both snapshot loops, so the last weights persist forever, and the
// "Number of snapshots" submenu only offers 1..8, leaving no way back. Reachable
// from a hand-edited or corrupted patch, and from any future build whose
// SNAPSHOTS differs.
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


// Store mode's storeNodePositions() captures a snapshot's x/y/radius/amount
// into snapshots[s][id], alongside the set's mixX/mixY -- the JSON round-trip
// for those fields was only ever checked via mixX/mixY (the Mix cursor
// regression test above), never for the per-snapshot geometry that's saved
// and restored through the same "x"/"y"/"radius"/"amount" JSON keys.
TEST_CASE("JSON round-trip preserves per-set snapshot x/y/radius/amount in Store mode", "[TransitPad][JSON]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->nodePosMode = NODEPOSMODE::STORE;
	m->nodes.setXyImmediate(0, 0.15f, 0.85f);
	m->nodes.setRadiusImmediate(0, 0.4f);
	m->nodes.setAmountImmediate(0, 0.6f);
	m->storeNodePositions(2);

	REQUIRE(m->snapshots[2][0].x == 0.15f);
	REQUIRE(m->snapshots[2][0].y == 0.85f);
	REQUIRE(m->snapshots[2][0].radius == 0.4f);
	REQUIRE(m->snapshots[2][0].amount == 0.6f);

	json_t* j = m->dataToJson();
	m->snapshots[2][0].x = 0.f;
	m->snapshots[2][0].y = 0.f;
	m->snapshots[2][0].radius = 0.f;
	m->snapshots[2][0].amount = 0.f;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->snapshots[2][0].x == 0.15f);
	REQUIRE(m->snapshots[2][0].y == 0.85f);
	REQUIRE(m->snapshots[2][0].radius == 0.4f);
	REQUIRE(m->snapshots[2][0].amount == 0.6f);
}


TEST_CASE("JSON round-trip preserves a custom setColor", "[TransitPad][JSON]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	// Distinct from every default set color (colors[s % colors.size()]) and
	// from color::WHITE, so a restore that silently fell back to either would
	// be observable.
	NVGcolor custom = nvgRGBA(0x11, 0x22, 0x33, 0xff);
	m->setColor[1] = custom;

	json_t* j = m->dataToJson();
	m->setColor[1] = color::WHITE;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->setColor[1].r == Catch::Approx(custom.r));
	REQUIRE(m->setColor[1].g == Catch::Approx(custom.g));
	REQUIRE(m->setColor[1].b == Catch::Approx(custom.b));
	REQUIRE(m->setColor[1].a == Catch::Approx(custom.a));
}


// The motion sequence (Seq::dataToJson/dataFromJson) persists under the
// module's own "output" key -- separate from the per-set "sets" array and
// never covered by any existing round-trip test.
TEST_CASE("JSON round-trip preserves the motion sequence under 'output'", "[TransitPad][JSON]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	// TransitPad's only motion-sequence port is the Out cursor, port index 0
	// (seqPortHidden() hides every other port).
	m->seqSelected[0] = 3;
	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::TRIG_REV;
	m->seqInterpolate[0] = StoermelderPackOne::XYSEQ_INTERPOLATE::CUBIC;
	m->seqData[0][3].length = 2;
	m->seqData[0][3].x[0] = 0.2f;
	m->seqData[0][3].y[0] = 0.3f;
	m->seqData[0][3].x[1] = 0.8f;
	m->seqData[0][3].y[1] = 0.9f;

	json_t* j = m->dataToJson();

	m->seqSelected[0] = 0;
	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::TRIG_FWD;
	m->seqInterpolate[0] = StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR;
	m->seqData[0][3].length = 0;

	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->seqSelected[0] == 3);
	REQUIRE(m->seqMode[0] == StoermelderPackOne::XYSEQ_MODE::TRIG_REV);
	REQUIRE(m->seqInterpolate[0] == StoermelderPackOne::XYSEQ_INTERPOLATE::CUBIC);
	REQUIRE(m->seqData[0][3].length == 2);
	REQUIRE(m->seqData[0][3].x[0] == Catch::Approx(0.2f));
	REQUIRE(m->seqData[0][3].y[0] == Catch::Approx(0.3f));
	REQUIRE(m->seqData[0][3].x[1] == Catch::Approx(0.8f));
	REQUIRE(m->seqData[0][3].y[1] == Catch::Approx(0.9f));
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
		m->setCvMode = SETCVMODE::C4;
		m->nodePosMode = NODEPOSMODE::AUTO;

		Module::ResetEvent e;
		m->onReset(e);

		REQUIRE(m->currentSet == 0);
		REQUIRE(m->snapshotsUsed == 4);
		REQUIRE(m->isLocked() == false);
		REQUIRE(m->setLabel[2] == "");
		REQUIRE(m->setCvMode == SETCVMODE::TRIG_FWD);
		REQUIRE(m->nodePosMode == NODEPOSMODE::OFF);
	}

	SECTION("RandomizeEvent moves the snapshot node positions") {
		// The test binary's RNG is never seeded outside this section, so
		// random::uniform() would otherwise always return 0 -- every node
		// landing on 0 happens to still differ from the 0.5 parked below, so a
		// single unseeded draw can't tell "actually randomized" apart from
		// "always resolves to the same fixed value". Seeding and looping many
		// draws catches that: with a real RNG, some node lands away from 0 too.
		random::init();

		bool anyAwayFromOrigin = false;
		for (int trial = 0; trial < 20 && !anyAwayFromOrigin; trial++) {
			for (uint8_t i = 0; i < 8; i++) m->nodes.setXyImmediate(i, 0.5f, 0.5f);

			Module::RandomizeEvent e;
			m->onRandomize(e);

			for (uint8_t i = 0; i < 8; i++) {
				if (m->getNodeXFinal(i) != 0.f || m->getNodeYFinal(i) != 0.f) anyAwayFromOrigin = true;
			}
		}
		REQUIRE(anyAwayFromOrigin);
	}

	SECTION("Randomize does not latch the momentary set buttons or change the set") {
		// SET_PARAM switches are momentary; randomizing them would silently jump
		// the active set on the next buttonDivider tick. Unseeded, every switch
		// always resolves to 0 regardless of whether randomizeEnabled is honoured
		// at all, so this can't fail even if the guard were removed. Seed the RNG
		// and repeat: with randomizeEnabled == false the switches must stay 0
		// across every draw, not just by chance on one.
		random::init();

		for (int trial = 0; trial < 20; trial++) {
			m->currentSet = 3;
			Module::RandomizeEvent e;
			m->onRandomize(e);
			h.dspSteps(5);

			for (uint8_t s = 0; s < 8; s++) {
				REQUIRE(m->params[TransitPadModule<>::SET_PARAM + s].getValue() == 0.f);
			}
			REQUIRE(m->currentSet == 3);
			REQUIRE(m->params[TransitPadModule<>::ON_PARAM].getValue() == 1.f);
		}
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

	m->inputs[TransitPadModule<>::MIX_X_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::MIX_X_INPUT].setVoltage(3.f); // → x = 3/10 + 0.5 = 0.8
	m->inputs[TransitPadModule<>::MIX_Y_INPUT].channels = 1;
	m->inputs[TransitPadModule<>::MIX_Y_INPUT].setVoltage(-2.f); // → y = -2/10 + 0.5 = 0.3

	float outUiXBefore = m->outUiX;
	float outUiYBefore = m->outUiY;

	h.dspSteps(5);

	REQUIRE(m->getCursorXFinal(0) == Catch::Approx(0.8f).margin(0.01f));
	REQUIRE(m->getCursorYFinal(0) == Catch::Approx(0.3f).margin(0.01f));
	// The UI shadow is untouched by CV — proves it would be the wrong read source.
	REQUIRE(m->outUiX == Catch::Approx(outUiXBefore));
	REQUIRE(m->outUiY == Catch::Approx(outUiYBefore));
}


TEST_CASE("A mapped OUT_X_POS/OUT_Y_POS is not overwritten by the stale UI-drag shadow", "[TransitPad]") {
	// Regression: OUT_X_POS/OUT_Y_POS's widget was swapped from
	// XyScreenMapWidget<StoermelderTrimpot> to a plain StoermelderTrimpot,
	// which never touches XyScreenParamQuantity::hasHandle (only
	// XyScreenMapWidget::draw() does, via APP->engine->getParamHandle()).
	// With hasHandle stuck false, process() always read outUiX/outUiY (the
	// screen-drag shadow, last written by a mouse drag) instead of the
	// mapped/modulated param value, and wrote that stale shadow straight
	// back over the param every frame -- the mapped value and the last
	// screen position fought every frame, visible as jumping.
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	// Drag the screen to a stale position first, exactly what a prior mouse
	// drag would leave behind.
	m->setCursorXyImmediate(0, 0.1f, 0.1f);
	h.dspStep();
	REQUIRE(m->outUiX == Catch::Approx(0.1f));
	REQUIRE(m->outUiY == Catch::Approx(0.1f));

	// Register a ParamHandle on OUT_X_POS/OUT_Y_POS, as a mapping expander
	// (CV-MAP/MIDI-CAT) would, and drive the raw param the way modulation
	// (or the mapping module writing back a learned value) would.
	rack::ParamHandle handleX;
	rack::ParamHandle handleY;
	APP->engine->addParamHandle(&handleX);
	APP->engine->addParamHandle(&handleY);
	h.mapParam(&handleX, m, TransitPadModule<>::OUT_X_POS);
	h.mapParam(&handleY, m, TransitPadModule<>::OUT_Y_POS);
	m->params[TransitPadModule<>::OUT_X_POS].setValue(0.9f);
	m->params[TransitPadModule<>::OUT_Y_POS].setValue(0.8f);

	// XyScreenMapWidget::draw() is what actually flips hasHandle in
	// production (a UI-thread, nanovg-driven side effect this module-only
	// test cannot reach); set it directly here to isolate and verify the
	// process()-side half of the fix, matching how the CV-shadow test above
	// isolates outUiX/outUiY without needing the widget tree.
	reinterpret_cast<StoermelderPackOne::XyScreenParamQuantity*>(m->paramQuantities[TransitPadModule<>::OUT_X_POS])->hasHandle = true;
	reinterpret_cast<StoermelderPackOne::XyScreenParamQuantity*>(m->paramQuantities[TransitPadModule<>::OUT_Y_POS])->hasHandle = true;

	h.dspSteps(5);

	// The mapped value must win over the stale screen-drag shadow, and the
	// param itself must not have been stomped back to that shadow.
	REQUIRE(m->getCursorXFinal(0) == Catch::Approx(0.9f).margin(0.01f));
	REQUIRE(m->getCursorYFinal(0) == Catch::Approx(0.8f).margin(0.01f));
	REQUIRE(m->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(0.9f).margin(0.01f));
	REQUIRE(m->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(0.8f).margin(0.01f));

	APP->engine->removeParamHandle(&handleX);
	APP->engine->removeParamHandle(&handleY);
}


TEST_CASE("Snapshot weights: point inside radius gets nonzero weight", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Default positions: snapshot 0 at (0, 0), mix point at (0.5, 0.5)
	// Distance = sqrt(0.5^2 + 0.5^2) ≈ 0.707, default radius = 1.0 → inside
	h.dspSteps(5);

	REQUIRE(m->snapshots[0][0].weight > 0.f);
}


TEST_CASE("Snapshot weights: point outside radius gets zero weight", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Move mix point to (0.9, 0.9) via the filter state so process() respects it.
	// Snapshot 0 defaults to (0, 0).
	// Distance = sqrt(0.9^2 + 0.9^2) ≈ 1.273, default radius = 1.0 → outside
	m->setCursorXyImmediate(0, 0.9f, 0.9f);
	h.dspSteps(5);

	REQUIRE(m->snapshots[0][0].weight == 0.f);
}


TEST_CASE("Snapshot weights are written to the active set", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	m->snapshotsUsed = 1;

	// Default positions: snapshot 0 at (0, 0), mix at (0.5, 0.5) → nonzero weight
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
