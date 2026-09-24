// REMOVE test cases: construction, JSON persistence, onReset, and sequence management
// (seqResize/seqUpdate/seqNext/seqPrev/seqSet/seqRand). Included by ReMove.test.cpp
// inside namespace __json. Not standalone: ReMove.test.hpp supplies everything used here.

TEST_CASE("Construction and initialization", "[ReMove]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	ReMoveModule* m = mods.create("ReMoveLite");
	ReMoveWidget* mw = Test::createWidget<ReMoveWidget>("ReMoveLite");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[ReMove][JSON]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");

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

TEST_CASE("Preset JSON clamps out-of-range scalars", "[ReMove][JSON]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");

	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);
	Test::testPresetOutOfRangeScalars(h, module, rootJ);
	json_decref(rootJ);
}

TEST_CASE("dataToJson writes the recorder array and config fields", "[ReMove][JSON]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");

	module->panelTheme = 2;
	module->audioRate = true;
	module->seqCount = 6;
	module->seq = 3;
	module->seqCvMode = SEQCVMODE_C4;
	module->seqChangeMode = SEQCHANGEMODE_OFFSET;
	module->runCvMode = RUNCVMODE_TRIG;
	module->recOutCvMode = RECOUTCVMODE_TRIG;
	module->inCvMode = INCVMODE_BI;
	module->outCvMode = OUTCVMODE_CV_BI;
	module->recMode = RECMODE_MOVE;
	module->recAutoplay = true;
	module->playMode = PLAYMODE_PINGPONG;
	module->sampleRate = 1.f / 120.f;
	module->isPlaying = true;

	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);

	REQUIRE(json_integer_value(json_object_get(rootJ, "panelTheme")) == 2);
	REQUIRE(json_boolean_value(json_object_get(rootJ, "audioRate")) == true);

	json_t* recJ = json_object_get(rootJ, "recorder");
	REQUIRE(recJ != nullptr);
	REQUIRE(json_array_size(recJ) == 1);
	json_t* rec0J = json_array_get(recJ, 0);
	REQUIRE(rec0J != nullptr);

	REQUIRE(json_integer_value(json_object_get(rec0J, "seqCount")) == 6);
	REQUIRE(json_integer_value(json_object_get(rec0J, "seq")) == 3);
	REQUIRE(json_integer_value(json_object_get(rec0J, "seqCvMode")) == (int)SEQCVMODE_C4);
	REQUIRE(json_integer_value(json_object_get(rec0J, "seqChangeMode")) == (int)SEQCHANGEMODE_OFFSET);
	REQUIRE(json_integer_value(json_object_get(rec0J, "runCvMode")) == (int)RUNCVMODE_TRIG);
	REQUIRE(json_integer_value(json_object_get(rec0J, "recOutCvMode")) == (int)RECOUTCVMODE_TRIG);
	REQUIRE(json_integer_value(json_object_get(rec0J, "inCvMode")) == (int)INCVMODE_BI);
	REQUIRE(json_integer_value(json_object_get(rec0J, "outCvMode")) == (int)OUTCVMODE_CV_BI);
	REQUIRE(json_integer_value(json_object_get(rec0J, "recMode")) == (int)RECMODE_MOVE);
	REQUIRE(json_boolean_value(json_object_get(rec0J, "recAutoplay")) == true);
	REQUIRE(json_integer_value(json_object_get(rec0J, "playMode")) == (int)PLAYMODE_PINGPONG);
	REQUIRE(json_real_value(json_object_get(rec0J, "sampleRate")) == Catch::Approx(1.f / 120.f));
	REQUIRE(json_boolean_value(json_object_get(rec0J, "isPlaying")) == true);

	json_decref(rootJ);
}

TEST_CASE("dataFromJson round-trip preserves all config fields", "[ReMove][JSON]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto src = mods.create("ReMoveLite");
	auto dst = mods.create("ReMoveLite");

	src->panelTheme = 9;
	src->audioRate = true;
	src->parameterChangesDirect = true;
	src->seqCount = 6;
	src->seq = 2;
	src->seqCvMode = SEQCVMODE_C4;
	src->seqChangeMode = SEQCHANGEMODE_OFFSET;
	src->runCvMode = RUNCVMODE_TRIG;
	src->recOutCvMode = RECOUTCVMODE_TRIG;
	src->inCvMode = INCVMODE_BI;
	src->outCvMode = OUTCVMODE_CV_BI;
	src->recMode = RECMODE_MOVE;
	src->recAutoplay = true;
	src->playMode = PLAYMODE_PINGPONG;
	src->sampleRate = 1.f / 100.f;
	src->isPlaying = true;

	json_t* rootJ = src->dataToJson();
	REQUIRE_NOTHROW(dst->dataFromJson(rootJ));

	REQUIRE(dst->panelTheme == 9);
	REQUIRE(dst->audioRate == true);
	REQUIRE(dst->parameterChangesDirect == true);
	REQUIRE(dst->seqCount == 6);
	REQUIRE(dst->seq == 2);
	REQUIRE(dst->seqCvMode == SEQCVMODE_C4);
	REQUIRE(dst->seqChangeMode == SEQCHANGEMODE_OFFSET);
	REQUIRE(dst->runCvMode == RUNCVMODE_TRIG);
	REQUIRE(dst->recOutCvMode == RECOUTCVMODE_TRIG);
	REQUIRE(dst->inCvMode == INCVMODE_BI);
	REQUIRE(dst->outCvMode == OUTCVMODE_CV_BI);
	REQUIRE(dst->recMode == RECMODE_MOVE);
	REQUIRE(dst->recAutoplay == true);
	REQUIRE(dst->playMode == PLAYMODE_PINGPONG);
	REQUIRE(dst->sampleRate == Catch::Approx(1.f / 100.f));
	REQUIRE(dst->isPlaying == true);
	// dataFromJson forces isRecording = false.
	REQUIRE(dst->isRecording == false);

	json_decref(rootJ);
}

TEST_CASE("dataFromJson resets REC_PARAM and triggers seqUpdate", "[ReMove][JSON]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");

	module->params[ReMoveModule::REC_PARAM].setValue(1.f);
	module->dataPtr = 999;

	json_t* rootJ = module->dataToJson();
	REQUIRE_NOTHROW(module->dataFromJson(rootJ));

	REQUIRE(module->params[ReMoveModule::REC_PARAM].getValue() == 0.f);
	// dataPtr is reset by seqUpdate() (RESTART mode) to seqLow.
	REQUIRE(module->dataPtr == module->seqLow);

	json_decref(rootJ);
}

TEST_CASE("dataFromJson handles empty JSON object", "[ReMove][JSON]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");

	// Dirty state so we can detect accidental clobbering on missing keys.
	module->panelTheme = 7;
	module->seqCount = 6;
	module->seq = 2;
	module->recAutoplay = true;
	module->isPlaying = true;

	json_t* emptyJ = json_object();
	REQUIRE_NOTHROW(module->dataFromJson(emptyJ));

	// Nothing should be overwritten (every property is null-guarded).
	REQUIRE(module->panelTheme == 7);
	REQUIRE(module->seqCount == 6);
	REQUIRE(module->seq == 2);
	REQUIRE(module->recAutoplay == true);
	REQUIRE(module->isPlaying == true);

	json_decref(emptyJ);
}

TEST_CASE("dataFromJson decompresses run-length-encoded seqData", "[ReMove][JSON]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto src = mods.create("ReMoveLite");
	auto dst = mods.create("ReMoveLite");

	// Write a sequence with a run-length pattern: three 0.5s then two 0.7s.
	// Source's dataToJson compresses consecutive same values to a count integer.
	src->seqResize(4);
	src->seq = 0;
	src->seqLength[0] = 5;
	for (int i = 0; i < 5; i++) src->seqData[i] = (i < 3) ? 0.5f : 0.7f;

	json_t* rootJ = src->dataToJson();
	REQUIRE_NOTHROW(dst->dataFromJson(rootJ));

	REQUIRE(dst->seqCount == 4);
	REQUIRE(dst->seq == 0);
	REQUIRE(dst->seqLength[0] == 5);

	// Verify the values decompressed correctly.
	for (int i = 0; i < 3; i++) {
		REQUIRE(dst->seqData[i] == Catch::Approx(0.5f));
	}
	REQUIRE(dst->seqData[3] == Catch::Approx(0.7f));
	REQUIRE(dst->seqData[4] == Catch::Approx(0.7f));

	json_decref(rootJ);
}

TEST_CASE("dataToJson compresses a run that extends to the last sample of a sequence", "[ReMove][JSON]") {
	// The compressor's inner run-detection loop advances j past seqLength[i] once a run
	// reaches the sequence's last sample, then used to unconditionally read
	// seqData[i * s + j] to seed the next comparison — one slot past the sequence's valid
	// data. That's only an actual out-of-bounds access at the very edge of the allocation:
	// with seqCount=1, s == REMOVE_MAX_DATA, so the sequence must be filled to the full
	// REMOVE_MAX_DATA to make the read land on seqData[REMOVE_MAX_DATA], one past the
	// allocated buffer — a shorter sequence just reads unused-but-allocated space next door.
	Test::ModuleScaffold<ReMoveModule> mods;
	auto src = mods.create("ReMoveLite");
	auto dst = mods.create("ReMoveLite");

	src->seqResize(1);
	src->seq = 0;
	src->seqLength[0] = REMOVE_MAX_DATA;
	src->seqData[0] = 0.1f;
	src->seqData[1] = 0.2f;
	// A run of identical values filling the rest of the sequence, reaching its last sample.
	for (int i = 2; i < REMOVE_MAX_DATA; i++) src->seqData[i] = 0.3f;

	json_t* rootJ;
	REQUIRE_NOTHROW(rootJ = src->dataToJson());
	REQUIRE_NOTHROW(dst->dataFromJson(rootJ));

	REQUIRE(dst->seqLength[0] == REMOVE_MAX_DATA);
	REQUIRE(dst->seqData[0] == Catch::Approx(0.1f));
	REQUIRE(dst->seqData[1] == Catch::Approx(0.2f));
	REQUIRE(dst->seqData[2] == Catch::Approx(0.3f));
	REQUIRE(dst->seqData[REMOVE_MAX_DATA - 1] == Catch::Approx(0.3f));

	json_decref(rootJ);
}

TEST_CASE("dataToJson compresses a sequence of entirely identical values", "[ReMove][JSON]") {
	// The whole sequence is one run from the first sample, exercising the same
	// end-of-sequence path as above from the very start of the data. Filled to
	// REMOVE_MAX_DATA for the same reason: only a fully-filled seqCount=1 sequence makes
	// the compressor's boundary read land past the actual seqData allocation.
	Test::ModuleScaffold<ReMoveModule> mods;
	auto src = mods.create("ReMoveLite");
	auto dst = mods.create("ReMoveLite");

	src->seqResize(1);
	src->seq = 0;
	src->seqLength[0] = REMOVE_MAX_DATA;
	for (int i = 0; i < REMOVE_MAX_DATA; i++) src->seqData[i] = 0.6f;

	json_t* rootJ;
	REQUIRE_NOTHROW(rootJ = src->dataToJson());
	REQUIRE_NOTHROW(dst->dataFromJson(rootJ));

	REQUIRE(dst->seqLength[0] == REMOVE_MAX_DATA);
	REQUIRE(dst->seqData[0] == Catch::Approx(0.6f));
	REQUIRE(dst->seqData[REMOVE_MAX_DATA - 1] == Catch::Approx(0.6f));

	json_decref(rootJ);
}

TEST_CASE("dataFromJson clips seqData writes to seqLength", "[ReMove][JSON]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	// Build a JSON object with a seqData array longer than the declared seqLength.
	// dataFromJson should refuse to write past seqLength[i].
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);
	module->seq = 0;

	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "panelTheme", json_integer(0));
	json_object_set_new(rootJ, "audioRate", json_boolean(false));
	json_object_set_new(rootJ, "parameterChangesDirect", json_boolean(false));

	json_t* recJ = json_array();
	json_t* rec0J = json_object();

	json_object_set_new(rec0J, "seqCount", json_integer(4));
	json_object_set_new(rec0J, "seq", json_integer(0));

	// seqLength[0] = 2, but seqData[0] has 10 elements.
	json_t* seqLengthJ = json_array();
	json_array_append_new(seqLengthJ, json_integer(2));
	for (int i = 1; i < 4; i++) json_array_append_new(seqLengthJ, json_integer(0));
	json_object_set_new(rec0J, "seqLength", seqLengthJ);

	json_t* seqDataJ = json_array();
	json_t* seqData0J = json_array();
	for (int i = 0; i < 10; i++) {
		json_array_append_new(seqData0J, json_real(0.5));
	}
	json_array_append_new(seqDataJ, seqData0J);
	for (int i = 1; i < 4; i++) json_array_append_new(seqDataJ, json_array());
	json_object_set_new(rec0J, "seqData", seqDataJ);

	json_array_append_new(recJ, rec0J);
	json_object_set_new(rootJ, "recorder", recJ);

	REQUIRE_NOTHROW(module->dataFromJson(rootJ));

	REQUIRE(module->seqLength[0] == 2);
	REQUIRE(module->seqData[0] == Catch::Approx(0.5f));
	REQUIRE(module->seqData[1] == Catch::Approx(0.5f));
	// Should NOT have written seqData[2..9] because seqLength=2.

	json_decref(rootJ);
}


TEST_CASE("dataFromJson clamps out-of-range seqCount and seq", "[ReMove][JSON]") {
	// seqCount and seq are used as array indices/divisors (seqLength[REMOVE_MAX_SEQ],
	// REMOVE_MAX_DATA / seqCount) without further validation past dataFromJson, so a
	// hand-edited or malformed preset must not be able to push them out of range.
	auto makeRootJ = [](int seqCount, int seq) {
		json_t* rootJ = json_object();
		json_t* recJ = json_array();
		json_t* rec0J = json_object();
		json_object_set_new(rec0J, "seqCount", json_integer(seqCount));
		json_object_set_new(rec0J, "seq", json_integer(seq));
		json_array_append_new(recJ, rec0J);
		json_object_set_new(rootJ, "recorder", recJ);
		return rootJ;
	};

	SECTION("seqCount values are clamped into [1, REMOVE_MAX_SEQ]") {
		for (int seqCount : {0, -1, 9, 64, 9999}) {
			Test::ModuleScaffold<ReMoveModule> mods;
			auto module = mods.create("ReMoveLite");

			json_t* rootJ = makeRootJ(seqCount, 0);
			REQUIRE_NOTHROW(module->dataFromJson(rootJ));

			REQUIRE(module->seqCount >= 1);
			REQUIRE(module->seqCount <= REMOVE_MAX_SEQ);
			REQUIRE(module->seq < module->seqCount);

			json_decref(rootJ);
		}
	}

	SECTION("seq >= seqCount is clamped into range") {
		Test::ModuleScaffold<ReMoveModule> mods;
		auto module = mods.create("ReMoveLite");

		json_t* rootJ = makeRootJ(4, 9998);
		REQUIRE_NOTHROW(module->dataFromJson(rootJ));

		REQUIRE(module->seq >= 0);
		REQUIRE(module->seq < module->seqCount);

		json_decref(rootJ);
	}
}


TEST_CASE("onReset clears playback state and sequence data", "[ReMove][init]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");

	// Dirty state first.
	module->audioRate = !module->audioRate;
	module->isPlaying = true;
	module->playDir = REMOVE_PLAYDIR_REV;
	module->isRecording = true;
	module->recTouched = true;
	module->recAutoplay = true;
	module->dataPtr = 12345;
	module->parameterChangesDirect = true;
	module->seq = 5;

	// Pre-set some seqLength data so we can verify reset.
	for (int i = 0; i < REMOVE_MAX_SEQ; i++) {
		module->seqLength[i] = i + 100;
	}

	Module::ResetEvent e;
	module->onReset(e);

	REQUIRE(module->isPlaying == false);
	REQUIRE(module->playDir == REMOVE_PLAYDIR_FWD);
	REQUIRE(module->isRecording == false);
	REQUIRE(module->recTouched == false);
	REQUIRE(module->recAutoplay == false);
	REQUIRE(module->dataPtr == 0);
	REQUIRE(module->parameterChangesDirect == false);
	REQUIRE(module->seq == 0);

	// seqResize(4) is called from onReset; seqCount becomes 4.
	REQUIRE(module->seqCount == 4);

	// All seqLength entries must be reset to 0.
	for (int i = 0; i < REMOVE_MAX_SEQ; i++) {
		REQUIRE(module->seqLength[i] == 0);
	}
}


// Sequence management (seqResize, seqUpdate, seqNext, seqPrev, seqSet, seqRand)

TEST_CASE("seqResize sets count, resets playback, zeros lengths", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");

	// Initial state: seqCount=4, seq=0
	REQUIRE(module->seqCount == 4);

	// Dirty state.
	module->seq = 3;
	module->isPlaying = true;
	for (int i = 0; i < REMOVE_MAX_SEQ; i++) module->seqLength[i] = i + 1;
	module->dataPtr = 999;

	module->seqResize(6);

	REQUIRE(module->seqCount == 6);
	REQUIRE(module->seq == 0); // seqResize resets seq to 0
	REQUIRE(module->isPlaying == false); // seqResize forces isPlaying=false
	REQUIRE(module->dataPtr == 0);
	for (int i = 0; i < REMOVE_MAX_SEQ; i++) {
		REQUIRE(module->seqLength[i] == 0);
	}
}

TEST_CASE("seqResize is a no-op while recording", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqCount = 4;
	module->isRecording = true;
	module->seq = 2;

	module->seqResize(6);

	// isRecording guard short-circuits; seqCount unchanged.
	REQUIRE(module->seqCount == 4);
}

TEST_CASE("seqUpdate sets seqLow/seqHigh based on seq and seqCount", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(8);

	// Each seq gets REMOVE_MAX_DATA / seqCount samples.
	int s = REMOVE_MAX_DATA / 8;

	module->seq = 0;
	module->seqUpdate();
	REQUIRE(module->seqLow == 0);
	REQUIRE(module->seqHigh == s);

	module->seq = 1;
	module->seqUpdate();
	REQUIRE(module->seqLow == s);
	REQUIRE(module->seqHigh == 2 * s);

	module->seq = 7;
	module->seqUpdate();
	REQUIRE(module->seqLow == 7 * s);
	REQUIRE(module->seqHigh == 8 * s);
}

TEST_CASE("seqNext cycles and wraps around", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);

	module->seq = 0;
	module->seqNext();
	REQUIRE(module->seq == 1);
	module->seqNext();
	REQUIRE(module->seq == 2);
	module->seqNext();
	REQUIRE(module->seq == 3);
	module->seqNext(); // wrap
	REQUIRE(module->seq == 0);
}

TEST_CASE("seqPrev cycles backwards and wraps", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);

	module->seq = 0;
	module->seqPrev(); // wrap
	REQUIRE(module->seq == 3);

	module->seqPrev();
	REQUIRE(module->seq == 2);
	module->seqPrev();
	REQUIRE(module->seq == 1);
	module->seqPrev();
	REQUIRE(module->seq == 0);
}

TEST_CASE("seqNext with skipEmpty advances past empty sequences", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);

	// Make seq 0 empty, seq 1 has data, others empty.
	module->seqLength[0] = 0;
	module->seqLength[1] = 10;
	module->seqLength[2] = 0;
	module->seqLength[3] = 0;
	module->seq = 0;

	module->seqNext(true); // should skip seq 0 (empty) and land on seq 1
	REQUIRE(module->seq == 1);
}

TEST_CASE("seqSet ignores same-value calls", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);
	module->seq = 2;

	module->seqSet(2);
	REQUIRE(module->seq == 2); // unchanged
}

TEST_CASE("seqSet clamps to valid range", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);

	module->seqSet(99); // out of range
	REQUIRE(module->seq == 3); // clamped to seqCount-1

	module->seqSet(-5); // negative
	REQUIRE(module->seq == 0); // clamped to 0
}

TEST_CASE("seqRand always lands within seqCount", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);

	for (int i = 0; i < 50; i++) {
		module->seqRand();
		REQUIRE(module->seq >= 0);
		REQUIRE(module->seq < 4);
	}
}

TEST_CASE("SEQCHANGEMODE_RESTART resets dataPtr and playDir on seqUpdate", "[ReMove][seq]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(4);
	module->seqChangeMode = SEQCHANGEMODE_RESTART;
	module->seq = 1;
	module->dataPtr = 5000;
	module->playDir = REMOVE_PLAYDIR_REV;

	module->seqUpdate();

	REQUIRE(module->dataPtr == module->seqLow);
	REQUIRE(module->playDir == REMOVE_PLAYDIR_FWD);
}

TEST_CASE("SEQCHANGEMODE_OFFSET preserves relative position when switching sequences", "[ReMove][seq]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);
	module->seqChangeMode = SEQCHANGEMODE_OFFSET;
	module->seq = 0;
	module->seqLength[0] = 100;
	module->dataPtr = 25; // 25 into seq 0

	module->seq = 1;
	module->seqLength[1] = 50;
	module->seqUpdate();

	// OFFSET mode maps the prior index modulo the new seq length, offset by seqLow.
	int s = REMOVE_MAX_DATA / 4;
	REQUIRE(module->dataPtr == s + (25 % s) % 50);
}


