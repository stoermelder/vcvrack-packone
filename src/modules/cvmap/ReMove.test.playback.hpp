// REMOVE test cases: lights, onRandomize, playback (mapped-parameter fixture, per
// PLAYMODE dataPtr/CV_OUTPUT behavior), and slew characterization. Included by
// ReMove.test.cpp inside namespace __playback. Not standalone: ReMove.test.hpp
// supplies everything used here.

TEST_CASE("SEQ lights reflect active sequence and total count", "[ReMove][lights]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(4);
	module->seq = 2;

	// Drive enough samples for lightDivider to fire (sampleRate / 100).
	h.dspSteps(1000);

	// Active seq light should be brighter than inactive ones.
	float activeBrightness = module->lights[ReMoveModule::SEQ_LIGHT + 2].getBrightness();
	float inactiveBrightness = module->lights[ReMoveModule::SEQ_LIGHT + 0].getBrightness();
	REQUIRE(activeBrightness > inactiveBrightness);
	REQUIRE(activeBrightness >= 0.7f);

	// Sequences beyond seqCount should have zero brightness.
	float beyondSeqCount = module->lights[ReMoveModule::SEQ_LIGHT + 7].getBrightness();
	REQUIRE(beyondSeqCount <= 0.3f); // could be 0.3 from `seqCount >= i + 1` but seqCount=4 → 0
}

TEST_CASE("REC light reflects isRecording", "[ReMove][lights]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->isRecording = false;

	h.dspSteps(500);
	REQUIRE(module->lights[ReMoveModule::REC_LIGHT].getBrightness() < 0.1f);

	module->isRecording = true;
	h.dspSteps(1000);
	REQUIRE(module->lights[ReMoveModule::REC_LIGHT].getBrightness() > 0.5f);
}

TEST_CASE("RUN lights reflect isPlaying when PHASE is disconnected", "[ReMove][lights]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->isPlaying = true;
	module->seqResize(4);
	module->seq = 0;
	module->seqLength[0] = 0; // no data; playback will not advance
	// sampleRate is in seconds per sample. Default is 1/60s ≈ 0.0167.
	// The playback loop checks `sampleTimer > sampleRate`. With a large
	// sampleRate value, the timer never crosses the threshold and the
	// `paramQuantity==NULL → isPlaying=false` branch is never executed.
	module->sampleRate = 1e6f;

	// Force the lightDivider to fire on every call.
	module->lightDivider.setDivision(1.f);

	// Run a first step to initialize internal trigger state.
	h.dspStep();
	// Re-assert isPlaying (in case process() touched it for an unrelated
	// reason; in the empty-sequence / disconnected-PHASE path it should
	// remain true).
	module->isPlaying = true;
	module->sampleTimer.reset();
	h.dspSteps(4);

	REQUIRE(module->isPlaying == true);
	REQUIRE(module->lights[ReMoveModule::RUN_LIGHT + 0].getBrightness() > 0.5f);
	REQUIRE(module->lights[ReMoveModule::RUN_LIGHT + 1].getBrightness() < 0.1f);
}

TEST_CASE("RUN lights reflect PHASE connection when PHASE is connected", "[ReMove][lights]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->isPlaying = false;
	module->inputs[ReMoveModule::PHASE_INPUT].channels = 1;

	h.dspSteps(1000);

	// PHASE connected → RUN light 1 (alt indicator) is on, light 0 off.
	REQUIRE(module->lights[ReMoveModule::RUN_LIGHT + 1].getBrightness() > 0.5f);
}


// Randomize

TEST_CASE("onRandomize generates non-empty seqLength for all sequences", "[ReMove][random]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);
	module->seq = 0;

	Module::RandomizeEvent e;
	module->onRandomize(e);

	for (int i = 0; i < module->seqCount; i++) {
		REQUIRE(module->seqLength[i] > 0);
	}

	// All values must be clamped to [0, 1].
	int s = REMOVE_MAX_DATA / 4;
	for (int i = 0; i < module->seqCount; i++) {
		for (int j = 0; j < module->seqLength[i]; j++) {
			REQUIRE(module->seqData[i * s + j] >= 0.f);
			REQUIRE(module->seqData[i * s + j] <= 1.f);
		}
	}
}

TEST_CASE("onRandomize works at every menu sample rate", "[ReMove][random]") {
	// onRandomize() derives `l` (samples generated per sequence) from sampleRate and then
	// computes `c % (l / 8)`, which divides by zero if l < 8. None of the eight menu sample
	// rates reach that — the smallest, 15Hz, gives l = round(15 * 8) = 120 — but nothing
	// previously exercised all eight to confirm none of them regress into it.
	const float menuSampleRates[] = {
		1.f/15.f, 1.f/30.f, 1.f/60.f, 1.f/100.f,
		1.f/200.f, 1.f/500.f, 1.f/1000.f, 1.f/2000.f
	};

	for (float sr : menuSampleRates) {
		Test::ModuleScaffold<ReMoveModule> mods;
		auto module = mods.create("ReMoveLite");
		module->seqResize(4);
		module->sampleRate = sr;

		Module::RandomizeEvent e;
		REQUIRE_NOTHROW(module->onRandomize(e));

		for (int i = 0; i < module->seqCount; i++) {
			REQUIRE(module->seqLength[i] > 0);
		}
	}
}

TEST_CASE("onRandomize does not divide by zero with an extreme sampleRate", "[ReMove][random]") {
	// sampleRate is loaded from JSON with no validation (ReMove.cpp:773), so a hand-edited or
	// malformed preset can set it to a value none of the menu's eight options would ever
	// produce. A large seconds-per-sample value rounds `l` down toward 0, which used to divide
	// by zero in `c % (l / 8)`; l is now clamped to a minimum of 8.
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(1);
	module->sampleRate = 100.f; // 100 seconds per sample: l would round to 0 without the clamp

	Module::RandomizeEvent e;
	REQUIRE_NOTHROW(module->onRandomize(e));

	REQUIRE(module->seqLength[0] > 0);
}


// Playback (mapped parameter)
//
// getParamQuantity(0) returns NULL until paramHandles[0] resolves to a mapped target, and
// process() bails out of both the recording and playback blocks on that NULL. The fixture
// below maps ReMOVE's own PHASE param onto a second ReMOVE instance's SLEW_PARAM (any bounded
// param on a registered module works; a second ReMOVE avoids depending on another module's
// slug), which is what unlocks the playback advance block for these tests.

TEST_CASE("Playback drives CV_OUTPUT and the mapped parameter per PLAYMODE", "[ReMove][playback]") {
	// The end-to-end observable of playback is what a patch actually receives: CV_OUTPUT's
	// voltage and the mapped target's parameter value (read via h.mappedValue(), the same
	// way a user's patch would via the target's knob/display). dataPtr/seq/playDir are
	// internal bookkeeping; they're asserted alongside as explanation for *why*, but the
	// pass/fail signal is the output a real patch would see.
	Test::Harness h;
	ReMoveModule* m = h.addModule<ReMoveModule>("ReMoveLite");
	rack::Module* target = h.addModule<ReMoveModule>("ReMoveLite");
	h.mapParam(&m->paramHandles[0], target, ReMoveModule::SLEW_PARAM);
	m->updateMapLen();
	h.connectOutput(m, ReMoveModule::CV_OUTPUT);
	// Default OUTCVMODE_CV_UNI rescales the internal 0..1 value to 0..10V, giving clean,
	// easy-to-read expected voltages for the 0.1/0.5/0.9 sequence below.
	REQUIRE(m->outCvMode == OUTCVMODE_CV_UNI);

	// One sequence spanning the whole buffer, three samples.
	m->seqResize(1);
	m->seqLength[0] = 3;
	m->seqData[0] = 0.1f;
	m->seqData[1] = 0.5f;
	m->seqData[2] = 0.9f;
	m->dataPtr = m->seqLow;
	m->playDir = REMOVE_PLAYDIR_FWD;
	m->isPlaying = true;
	// sampleRate is seconds per sample; a tiny value makes the playback timer cross its
	// threshold on every dspStep() instead of waiting for real elapsed time.
	m->sampleRate = 1e-9f;

	SECTION("PLAYMODE_LOOP wraps back to the start of the sequence") {
		m->playMode = PLAYMODE_LOOP;

		h.dspStep(); // outputs seqData[0]=0.1, dataPtr 0 -> 1
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(1.f));
		h.dspStep(); // outputs seqData[1]=0.5, dataPtr 1 -> 2
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(5.f));
		h.dspStep(); // outputs seqData[2]=0.9, dataPtr wraps 3 -> seqLow
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(9.f));
		REQUIRE(m->dataPtr == m->seqLow);
		REQUIRE(m->playDir == REMOVE_PLAYDIR_FWD);

		h.dspStep(); // loop: back to seqData[0]=0.1
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(1.f));
		REQUIRE(h.mappedValue(&m->paramHandles[0]) == Catch::Approx(0.1f * 0.975f));
	}

	SECTION("PLAYMODE_ONESHOT stops on the last sample") {
		m->playMode = PLAYMODE_ONESHOT;

		h.dspStep();
		h.dspStep();
		h.dspStep(); // reaches the end, ONESHOT steps back and stops on the last sample
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(9.f));
		REQUIRE(m->dataPtr == m->seqLow + 2);
		REQUIRE(m->playDir == REMOVE_PLAYDIR_NONE);

		// Playback no longer advances once stopped: the output holds at the last value.
		h.dspStep();
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(9.f));
		REQUIRE(m->dataPtr == m->seqLow + 2);
	}

	SECTION("PLAYMODE_PINGPONG reverses direction at the end and walks back") {
		// Each step outputs seqData[dataPtr] *before* dataPtr advances, so the reversing
		// step both outputs the peak (0.9) and decrements dataPtr onto that same peak
		// index — the next step reads it again before moving on. Net output sequence:
		// 0.1, 0.5, 0.9, 0.9, 0.5, 0.1, ...
		m->playMode = PLAYMODE_PINGPONG;

		h.dspStep(); // 0.1
		h.dspStep(); // 0.5
		h.dspStep(); // 0.9, end reached -> reverses (dataPtr decremented onto the peak)
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(9.f));
		REQUIRE(m->playDir == REMOVE_PLAYDIR_REV);

		h.dspStep(); // re-reads the peak once before moving on
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(9.f));
		h.dspStep(); // walking back: 0.5
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(5.f));
		h.dspStep(); // outputs 0.1, then the same step's boundary check snaps back to start
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(1.f));
		REQUIRE(m->dataPtr == m->seqLow);
		REQUIRE(m->playDir == REMOVE_PLAYDIR_FWD);
	}

	SECTION("PLAYMODE_SEQLOOP advances to the next sequence and plays its data") {
		// Two non-empty, distinguishable sequences, so this checks that playback actually
		// rotates to seq 1 and starts outputting *its* data — not just that dataPtr/seq
		// change to some in-range value (seqCount=1 can't tell rotation from standing still).
		m->seqResize(2); // resets isPlaying to false; re-armed below
		m->seqLength[0] = 3;
		m->seqLength[1] = 2;
		m->seq = 0;
		m->seqUpdate();
		m->seqData[m->seqLow] = 0.1f;
		m->seqData[m->seqLow + 1] = 0.5f;
		m->seqData[m->seqLow + 2] = 0.9f;
		m->playMode = PLAYMODE_SEQLOOP;
		m->playDir = REMOVE_PLAYDIR_FWD;
		m->isPlaying = true;
		m->sampleRate = 1e-9f;
		m->dataPtr = m->seqLow;

		// Fill seq 1 with its own distinct data, addressed via its own seqLow/seqHigh so
		// this doesn't depend on REMOVE_MAX_DATA's layout directly.
		{
			int savedSeq = m->seq;
			m->seq = 1;
			m->seqUpdate();
			m->seqData[m->seqLow] = 0.2f;
			m->seqData[m->seqLow + 1] = 0.6f;
			m->seq = savedSeq;
			m->seqUpdate();
			m->dataPtr = m->seqLow;
		}

		h.dspStep(); // 0.1
		h.dspStep(); // 0.5
		h.dspStep(); // 0.9, end of seq 0 -> seqNext(true) -> seq 1
		REQUIRE(m->seq == 1);
		REQUIRE(m->dataPtr == m->seqLow); // SEQCHANGEMODE_RESTART resets to the new seqLow

		h.dspStep(); // now playing seq 1's data: 0.2
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(2.f));
		h.dspStep(); // 0.6
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(6.f));
	}

	SECTION("PLAYMODE_SEQLOOP with a single sequence loops back to itself") {
		// The seqCount=1 case: not a rotation, but still a real outcome worth covering
		// (seqNext(true) with nowhere else to go must not get stuck or go out of range) —
		// kept alongside the two-sequence case above rather than in its place.
		m->playMode = PLAYMODE_SEQLOOP;

		h.dspStep();
		h.dspStep();
		h.dspStep();
		REQUIRE(m->seq == 0);
		REQUIRE(m->dataPtr == m->seqLow);
		REQUIRE(m->playDir == REMOVE_PLAYDIR_FWD);

		// Still playing its own data from the top.
		h.dspStep();
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(1.f));
	}

	SECTION("PLAYMODE_SEQRANDOM can land on a different sequence and play its data") {
		// seqRand() draws from a clock-seeded RNG, so a single run can't assert which
		// sequence it picks without flaking. Instead: run it repeatedly across many
		// independent modules and require that at least one trial actually left seq 0 and
		// then output seq 1's (distinct) data — proving seqRand() runs and its result is
		// actually played, not just that seq stays in range (a stuck implementation would
		// also satisfy that trivially).
		bool observedOtherSeq = false;
		for (int trial = 0; trial < 200 && !observedOtherSeq; trial++) {
			Test::Harness h2;
			ReMoveModule* m2 = h2.addModule<ReMoveModule>("ReMoveLite");
			rack::Module* target2 = h2.addModule<ReMoveModule>("ReMoveLite");
			h2.mapParam(&m2->paramHandles[0], target2, ReMoveModule::SLEW_PARAM);
			m2->updateMapLen();
			h2.connectOutput(m2, ReMoveModule::CV_OUTPUT);

			m2->seqResize(2);
			m2->seqLength[0] = 3;
			m2->seqLength[1] = 2;
			m2->seq = 0;
			m2->seqUpdate();
			m2->seqData[m2->seqLow] = 0.1f;
			m2->seqData[m2->seqLow + 1] = 0.5f;
			m2->seqData[m2->seqLow + 2] = 0.9f;
			{
				int savedSeq = m2->seq;
				m2->seq = 1;
				m2->seqUpdate();
				m2->seqData[m2->seqLow] = 0.2f;
				m2->seqData[m2->seqLow + 1] = 0.6f;
				m2->seq = savedSeq;
				m2->seqUpdate();
			}
			m2->dataPtr = m2->seqLow;
			m2->playDir = REMOVE_PLAYDIR_FWD;
			m2->isPlaying = true;
			m2->sampleRate = 1e-9f;
			m2->playMode = PLAYMODE_SEQRANDOM;

			h2.dspStep();
			h2.dspStep();
			h2.dspStep(); // reaches end of seq 0 -> seqRand()

			REQUIRE(m2->seq >= 0);
			REQUIRE(m2->seq < m2->seqCount);
			if (m2->seq == 1) {
				observedOtherSeq = true;
				h2.dspStep(); // now playing seq 1's data
				REQUIRE(m2->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(2.f));
			}
		}
		REQUIRE(observedOtherSeq);
	}
}

TEST_CASE("SLEW_PARAM limits how fast CV_OUTPUT can change during playback", "[ReMove][slew]") {
	// A characterization test, not a behavior guarantee: setValue() (ReMove.cpp:517-523)
	// passes `sampleRate` — this module's own name for its recording interval in seconds per
	// sample, not the engine's sample time — as the slew limiter's deltaTime. That means the
	// slew rate scales with the recording sample rate setting rather than the engine's actual
	// rate: whatever `sampleRate` is set to is also how much simulated time the limiter
	// thinks has passed on every played sample, real elapsed engine time notwithstanding.
	// The docs describe this as smoothing "between recorded steps", so it may well be
	// intentional, but nothing previously pinned the resulting shape down; this exists so a
	// future change to that coupling shows up here rather than silently.
	//
	// A real `sampleRate` (not the 1e-9f trick the other playback tests use) is needed here:
	// that value is both the playback-advance interval (real elapsed engine time the
	// harness must actually drive through with dspSteps()) and the slew's deltaTime, and
	// this test wants both real.
	Test::Harness h;
	ReMoveModule* m = h.addModule<ReMoveModule>("ReMoveLite");
	rack::Module* target = h.addModule<ReMoveModule>("ReMoveLite");
	h.mapParam(&m->paramHandles[0], target, ReMoveModule::SLEW_PARAM);
	m->updateMapLen();
	h.connectOutput(m, ReMoveModule::CV_OUTPUT);

	// Silence, then full scale held for many samples — long enough for even the slowest
	// slew setting to converge, unlike a 2-sample loop where the target would alternate
	// back to silence before a slow ramp ever reached it.
	const int holdSamples = 60;
	m->seqResize(1);
	m->seqLength[0] = 1 + holdSamples;
	m->seqData[0] = 0.f;
	for (int i = 1; i <= holdSamples; i++) m->seqData[i] = 1.f;
	m->dataPtr = m->seqLow;
	m->playDir = REMOVE_PLAYDIR_FWD;
	m->isPlaying = true;
	m->playMode = PLAYMODE_LOOP;
	m->sampleRate = 1.f / 60.f; // the module's default recording rate

	// Number of dspStep()s to cross one sampleRate interval of real engine time.
	int stepsPerSample = (int)std::ceil(m->sampleRate / (1.f / Test::sampleRate())) + 1;

	SECTION("No slew: output reaches the target on the sample that plays it") {
		m->params[ReMoveModule::SLEW_PARAM].setValue(0.f);

		h.dspSteps(stepsPerSample); // plays seqData[0] = 0.f
		h.dspSteps(stepsPerSample); // plays seqData[1] = 1.f

		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(10.f));
	}

	SECTION("Mid-range slew: output has not yet reached the target on the sample that plays it") {
		m->params[ReMoveModule::SLEW_PARAM].setValue(0.5f);

		h.dspSteps(stepsPerSample); // plays seqData[0] = 0.f
		h.dspSteps(stepsPerSample); // plays seqData[1] = 1.f, slewed toward it

		float voltage = m->outputs[ReMoveModule::CV_OUTPUT].getVoltage();
		REQUIRE(voltage > 0.f);
		REQUIRE(voltage < 10.f);
	}

	SECTION("Maximum slew setting still reaches the target given a long enough hold") {
		m->params[ReMoveModule::SLEW_PARAM].setValue(0.975f); // configParam's max

		bool reachedTarget = false;
		for (int played = 0; played < holdSamples && !reachedTarget; played++) {
			h.dspSteps(stepsPerSample);
			if (m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(10.f))
				reachedTarget = true;
		}
		REQUIRE(reachedTarget);
	}
}
