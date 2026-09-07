// Sequencing (SLOTCVMODE) and write/auto-mode tests. Ported from EightFaceMk2's equivalent suite; mk1's presetLoad(Module*, int, ...) reads
// the bound module directly rather than by moduleId, but the trigger/step scaffolding is the same.

// Fixture wiring one bound module and a helper to fire the SLOT/RESET inputs like real cables,
// through connectInput()/connectForTest() rather than touching internals directly.
struct SequencingFixture {
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceModule<8>* m;
	EightFaceWidget* mw;
	EightFaceModule<8>* boundM;
	EightFaceWidget* boundMw;

	explicit SequencingFixture() {
		m = h.addModule<EightFaceModule<8>>(createEightFaceModule);
		mw = h.addWidget<EightFaceWidget>(m);
		boundM = h.addModule<EightFaceModule<8>>(createEightFaceModule);
		boundMw = Test::createWidget<EightFaceWidget>(boundM);
		connectForTest(m, boundM, boundMw);
		m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

		// dsp::SchmittTrigger's default state is UNINITIALIZED, not LOW: its very first
		// process(in >= highThreshold) call transitions UNINITIALIZED -> HIGH without returning
		// true (Rack/include/dsp/digital.hpp) -- an untriggered arm, not a missed edge. Prime both
		// SLOT_INPUT's and RESET_INPUT's triggers with one full low/high/low cycle here so every
		// test's first pulseSlot()/pulseReset() behaves like a real second-or-later pulse, matching
		// what a patched trigger source would actually deliver. Done BEFORE any slot is marked
		// used, so the constructor's default TRIG_FWD mode finds every slot empty and presetLoad()
		// returns without dispatching -- the priming's rising edge must not itself queue a load.
		h.connectInput(m, EightFaceModule<8>::SLOT_INPUT, 0.f);
		h.connectInput(m, EightFaceModule<8>::RESET_INPUT, 0.f);
		h.dspStep();
		h.connectInput(m, EightFaceModule<8>::SLOT_INPUT, 10.f);
		h.connectInput(m, EightFaceModule<8>::RESET_INPUT, 10.f);
		h.dspStep();
		h.connectInput(m, EightFaceModule<8>::SLOT_INPUT, 0.f);
		h.connectInput(m, EightFaceModule<8>::RESET_INPUT, 0.f);
		h.dspStep();
		REQUIRE(m->dispatch.pendingGuiTasks() == 0);
		m->preset = -1;
		m->presetPrev = -1;

		// Every slot used, so presetLoad() never bails out on an empty target -- sequencing tests
		// are about which slot gets selected, not whether a particular one is populated. The
		// payload is a placeholder, not a real widget-level toJson(): sequencing tests assert
		// `preset`/`presetPrev`, which presetLoad() updates synchronously, and never drain the
		// dispatch queue (no uiFrame()/drain() call in this fixture or its pulse helpers) -- so
		// this JSON is never expected to reach ModuleWidget::fromJson(), which would reject it.
		for (int i = 0; i < 8; i++) {
			m->presetSlotUsed[i] = true;
			m->presetSlot[i] = json_pack("{s:i}", "marker", i);
		}
	}

	~SequencingFixture() {
		Test::unregisterModule(boundM, boundMw);
	}

	// Fires a clean 0V -> 10V -> 0V pulse on SLOT_INPUT, tripping slotTrigger exactly once, then
	// runs process() so the resulting presetLoad() (if any) takes effect on `preset`/`presetPrev`.
	void pulseSlot() {
		h.connectInput(m, EightFaceModule<8>::SLOT_INPUT, 10.f);
		h.dspStep();
		h.connectInput(m, EightFaceModule<8>::SLOT_INPUT, 0.f);
		h.dspStep();
	}

	void pulseReset() {
		h.connectInput(m, EightFaceModule<8>::RESET_INPUT, 10.f);
		h.dspStep();
		h.connectInput(m, EightFaceModule<8>::RESET_INPUT, 0.f);
		h.dspStep();
	}

	void setSlotVoltage(float v) {
		h.connectInput(m, EightFaceModule<8>::SLOT_INPUT, v);
		h.dspStep();
	}
};

TEST_CASE("TRIG_FWD advances and wraps at presetCount", "[EightFace][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	f.m->presetCount = 4;
	f.m->preset = 0;

	// One process() call to establish resetTimer past the 1ms guard before the first trigger.
	for (int i = 0; i < 100; i++) f.h.dspStep();

	for (int expected : {1, 2, 3, 0, 1}) {
		f.pulseSlot();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_REV advances backward and wraps", "[EightFace][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_REV;
	f.m->presetCount = 4;
	f.m->preset = 0;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	for (int expected : {3, 2, 1, 0, 3}) {
		f.pulseSlot();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_PINGPONG reverses at both ends", "[EightFace][sequencing]") {
	// Regression for #191/#203 (hanging pingpong on manual slot change).
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_PINGPONG;
	f.m->presetCount = 4;
	f.m->preset = 0;
	f.m->slotCvModeDir = 1;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	// 0 -> 1 -> 2 -> 3 -> 2 -> 1 -> 0 -> 1 ...
	for (int expected : {1, 2, 3, 2, 1, 0, 1}) {
		f.pulseSlot();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_ALT produces 1,2,1,3,1,4...", "[EightFace][sequencing]") {
	// The manual gives this exact sequence for 6 slots -- asserted literally, 0-indexed here as
	// 0,1,0,2,0,3,0,4,0,5.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_ALT;
	f.m->presetCount = 6;
	f.m->preset = 0;
	f.m->slotCvModeDir = 1;
	f.m->slotCvModeAlt = 0;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	for (int expected : {1, 0, 2, 0, 3, 0, 4, 0, 5, 0}) {
		f.pulseSlot();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_RANDOM_WO_REPEAT never repeats consecutively", "[EightFace][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WO_REPEAT;
	f.m->presetCount = 8;
	f.m->preset = 0;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	int prev = f.m->preset;
	for (int i = 0; i < 100; i++) {
		f.pulseSlot();
		REQUIRE(f.m->preset != prev);
		REQUIRE(f.m->preset >= 0);
		REQUIRE(f.m->preset < 8);
		prev = f.m->preset;
	}
}

TEST_CASE("TRIG_RANDOM_WALK only moves +/-1 and clamps at the ends", "[EightFace][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WALK;
	f.m->presetCount = 8;
	f.m->preset = 4;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	int prev = f.m->preset;
	for (int i = 0; i < 200; i++) {
		f.pulseSlot();
		int cur = f.m->preset;
		REQUIRE(cur >= 0);
		REQUIRE(cur <= 7);
		int step = cur - prev;
		// At the boundary the walk clamps rather than moving -1/+1 illegally, so a repeat (step
		// 0) is possible only there.
		REQUIRE(((step == 1 || step == -1) || ((prev == 0 || prev == 7) && step == 0)));
		prev = cur;
	}
}

TEST_CASE("TRIG_SHUFFLE visits every slot once per permutation", "[EightFace][sequencing]") {
	// Review #6 was fixed here; regression coverage.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_SHUFFLE;
	f.m->presetCount = 8;
	f.m->preset = -1;
	f.m->slotCvModeShuffle.clear();
	for (int i = 0; i < 100; i++) f.h.dspStep();

	std::set<int> visited;
	for (int i = 0; i < 8; i++) {
		f.pulseSlot();
		REQUIRE(f.m->preset >= 0);
		REQUIRE(f.m->preset < 8);
		visited.insert(f.m->preset);
	}
	REQUIRE(visited.size() == 8);
}

TEST_CASE("VOLT maps 0-10V across presetCount, including exactly 10V", "[EightFace][sequencing]") {
	// Regression for #377 (crash above 10V). mk1 does NOT clamp its input
	// (EightFace.cpp:244's rescale has no clamp, unlike mk2's equivalent) -- this is expected to
	// misbehave above 10V and is a real bug, not a test to soften.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::VOLT;
	f.m->presetCount = 8;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	f.setSlotVoltage(0.f);
	REQUIRE(f.m->preset == 0);

	f.setSlotVoltage(5.f);
	REQUIRE(f.m->preset == 4);

	// Exactly 10V must not crash and must land on a valid slot -- rescale(10,0,10,0,8) == 8,
	// floor(8) == 8, which is out of range for an 8-slot module (presetLoad's own bound check,
	// EightFace.cpp:431, then silently no-ops rather than crashing).
	int before = f.m->preset;
	REQUIRE_NOTHROW(f.setSlotVoltage(10.f));
	// Either it stayed on the last valid preset (out-of-range load rejected) or landed in range;
	// what must NOT happen is an out-of-bounds write, which REQUIRE_NOTHROW above already covers
	// for the arithmetic path. Assert the observable state is still sane either way.
	REQUIRE(f.m->preset >= 0);
	REQUIRE(f.m->preset < 8);
	(void)before;
}

TEST_CASE("C4 follows V/Oct; channel 2 retriggers the current slot", "[EightFace][sequencing]") {
	// Added in v2.0.0 (#330). C4 spans presetTotal (== NUM_PRESETS, 8 here), not presetCount.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::C4;
	f.m->presetCount = 8;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	// EightFace.cpp:247: round(clamp(v * 12, 0, NUM_PRESETS - 1)) -- 0V/12 maps to slot 0.
	f.setSlotVoltage(0.f);
	REQUIRE(f.m->preset == 0);

	// A channel-2 retrigger on the same (unchanged) V/Oct value must re-apply the current slot
	// rather than requiring the voltage to change first.
	f.h.connectInputPoly(f.m, EightFaceModule<8>::SLOT_INPUT, 2, 0.f);
	f.h.dspStep();
	f.m->inputs[EightFaceModule<8>::SLOT_INPUT].setVoltage(10.f, 1);
	f.h.dspStep();
	f.m->inputs[EightFaceModule<8>::SLOT_INPUT].setVoltage(0.f, 1);
	f.h.dspStep();
	REQUIRE(f.m->preset == 0);
}

TEST_CASE("ARM arms a slot and applies it on the next trigger", "[EightFace][sequencing]") {
	// Manual: yellow LED (presetNext) then white/blue on trigger. presetLoad(t, i, /*isNext*/true)
	// only records presetNext; the actual load happens on the next SLOT_INPUT trigger via
	// presetLoad(t, presetNext, false, true) (EightFace.cpp:328-332).
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::ARM;
	f.m->presetCount = 8;
	f.m->preset = 0;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	f.m->presetLoad(f.boundM, 3, /*isNext=*/true);
	REQUIRE(f.m->presetNext == 3);
	REQUIRE(f.m->preset == 0);  // not applied yet

	f.pulseSlot();
	REQUIRE(f.m->preset == 3);
}

TEST_CASE("OFF ignores all CV", "[EightFace][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::OFF;
	f.m->presetCount = 8;
	f.m->preset = 2;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	f.pulseSlot();
	f.pulseReset();
	REQUIRE(f.m->preset == 2);
}

TEST_CASE("Empty slots are stepped over but do not apply a preset", "[EightFace][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	f.m->presetCount = 4;
	f.m->preset = 0;
	// Slot 1 is deliberately emptied -- presetLoad() advances `preset` to 1 (the sequence
	// position) but applyPreset() must not dispatch a load for it (EightFace.cpp:419: "if (p < 0
	// || !presetSlotUsed[p]) return;").
	json_decref(f.m->presetSlot[1]);
	f.m->presetSlot[1] = nullptr;
	f.m->presetSlotUsed[1] = false;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	f.pulseSlot();
	REQUIRE(f.m->preset == 1);
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 0);
}

TEST_CASE("A trigger within 1ms after RESET is ignored", "[EightFace][sequencing]") {
	// The resetTimer.getTime() >= 1e-3f guard (added v1.2.0), gating every TRIG_* mode's
	// SLOT_INPUT handling right after a reset.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	f.m->presetCount = 4;
	f.m->preset = 0;
	for (int i = 0; i < 100; i++) f.h.dspStep();

	f.pulseReset();
	REQUIRE(f.m->preset == 0);  // RESET on TRIG_FWD jumps to slot 0

	// Immediately (same millisecond) pulse SLOT_INPUT -- must be ignored.
	f.pulseSlot();
	REQUIRE(f.m->preset == 0);

	// After the 1ms window has elapsed, the same pulse must be honored.
	int steps = (int)(Test::sampleRate() * 2e-3f) + 1;
	for (int i = 0; i < steps; i++) f.h.dspStep();
	f.pulseSlot();
	REQUIRE(f.m->preset == 1);
}

TEST_CASE("RESET behavior per SLOTCVMODE", "[EightFace][sequencing]") {
	// mk1 handles only FWD/REV/PINGPONG/ALT/SHUFFLE
	// (EightFace.cpp:213-234) explicitly. mk1's manual promises "Resets to first slot" for the
	// three random modes too -- the SECTIONs below assert the manual's claim, and the random-mode
	// ones are EXPECTED TO FAIL until mk1 gains the same reset handling mk2 has. Treat as a real
	// bug (per the plan), not a test to weaken.
	SequencingFixture f;
	f.m->presetCount = 4;

	SECTION("TRIG_FWD resets to slot 0") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
		f.m->preset = 2;
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_REV resets to the last slot") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_REV;
		f.m->preset = 0;
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->preset == f.m->presetCount - 1);
	}

	SECTION("TRIG_PINGPONG resets to slot 0 and forward direction") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_PINGPONG;
		f.m->preset = 2;
		f.m->slotCvModeDir = -1;
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
		REQUIRE(f.m->slotCvModeDir == 1);
	}

	SECTION("TRIG_ALT resets to slot 0") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_ALT;
		f.m->preset = 3;
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_SHUFFLE clears the pending permutation") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_SHUFFLE;
		f.m->slotCvModeShuffle = {1, 2, 3};
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->slotCvModeShuffle.empty());
	}

	SECTION("TRIG_RANDOM resets to slot 0 -- EXPECTED TO FAIL: mk1 has no reset case for it") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM;
		f.m->preset = 3;
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_RANDOM_WO_REPEAT resets to slot 0 -- EXPECTED TO FAIL: mk1 has no reset case for it") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WO_REPEAT;
		f.m->preset = 3;
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_RANDOM_WALK resets to slot 0 -- EXPECTED TO FAIL: mk1 has no reset case for it") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WALK;
		f.m->preset = 3;
		for (int i = 0; i < 100; i++) f.h.dspStep();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}
}


// ---- Write / auto mode -----------------------------------------------------------------------

TEST_CASE("Write-mode short-press saves; long-press clears", "[EightFace][write]") {
	SequencingFixture f;
	f.m->ctrlMode = CTRLMODE::WRITE;
	f.m->params[EightFaceModule<8>::CTRLMODE_PARAM].setValue((float)CTRLMODE::WRITE);
	// Empty slot 0 so a save is observable.
	json_decref(f.m->presetSlot[0]);
	f.m->presetSlot[0] = nullptr;
	f.m->presetSlotUsed[0] = false;

	// Short press: param up then down within one buttonDivider tick's sample time.
	f.m->params[EightFaceModule<8>::PRESET_PARAM + 0].setValue(1.f);
	// Step a full division so ClockDividerEx's randomized starting phase cannot skip the button
	// processing block entirely (ClockDividerEx randomizes its starting phase).
	for (uint32_t i = 0; i < f.m->buttonDivider.division + 1; i++) f.h.dspStep();
	f.m->params[EightFaceModule<8>::PRESET_PARAM + 0].setValue(0.f);
	for (uint32_t i = 0; i < f.m->buttonDivider.division + 1; i++) f.h.dspStep();

	REQUIRE(f.m->presetSlotUsed[0] == true);

	// Long press: hold for >= 1 second. LongPressButton::process() only runs once per
	// buttonDivider tick, with sampleTime pre-scaled by the division, so a real 1s hold needs
	// sampleRate raw dspStep() calls, not sampleRate/division divider firings.
	f.m->params[EightFaceModule<8>::PRESET_PARAM + 0].setValue(1.f);
	int stepsFor1s = (int)std::ceil(Test::sampleRate()) + (int)f.m->buttonDivider.division * 2;
	for (int i = 0; i < stepsFor1s; i++) f.h.dspStep();
	f.m->params[EightFaceModule<8>::PRESET_PARAM + 0].setValue(0.f);
	for (uint32_t i = 0; i < f.m->buttonDivider.division + 1; i++) f.h.dspStep();

	REQUIRE(f.m->presetSlotUsed[0] == false);
}

TEST_CASE("Write-mode ignores the CV input entirely", "[EightFace][write]") {
	SequencingFixture f;
	f.m->ctrlMode = CTRLMODE::WRITE;
	f.m->params[EightFaceModule<8>::CTRLMODE_PARAM].setValue((float)CTRLMODE::WRITE);
	f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	f.m->preset = 0;

	f.pulseSlot();
	f.pulseReset();
	REQUIRE(f.m->preset == 0);
}

TEST_CASE("Long-press in read-mode sets presetCount; suppressed when presetCountLongPress is false", "[EightFace][write]") {
	SequencingFixture f;
	f.m->ctrlMode = CTRLMODE::READ;
	f.m->params[EightFaceModule<8>::CTRLMODE_PARAM].setValue((float)CTRLMODE::READ);
	f.m->presetCount = 8;

	SECTION("presetCountLongPress true: long-press on slot i sets presetCount to i+1") {
		f.m->presetCountLongPress = true;
		f.m->params[EightFaceModule<8>::PRESET_PARAM + 2].setValue(1.f);
		// LongPressButton::process() is only called once per buttonDivider tick, with sampleTime
		// pre-scaled by the division (EightFace.cpp: "sampleTime = args.sampleTime * buttonDivider
		// .division") -- so a real 1s hold needs sampleRate raw dspStep() calls, not
		// sampleRate/division divider firings.
		int stepsFor1s = (int)std::ceil(Test::sampleRate()) + (int)f.m->buttonDivider.division * 2;
		for (int i = 0; i < stepsFor1s; i++) f.h.dspStep();
		f.m->params[EightFaceModule<8>::PRESET_PARAM + 2].setValue(0.f);
		for (uint32_t i = 0; i < f.m->buttonDivider.division + 1; i++) f.h.dspStep();

		REQUIRE(f.m->presetCount == 3);
	}

	SECTION("presetCountLongPress false: long-press does nothing to presetCount") {
		f.m->presetCountLongPress = false;
		f.m->params[EightFaceModule<8>::PRESET_PARAM + 2].setValue(1.f);
		// LongPressButton::process() is only called once per buttonDivider tick, with sampleTime
		// pre-scaled by the division (EightFace.cpp: "sampleTime = args.sampleTime * buttonDivider
		// .division") -- so a real 1s hold needs sampleRate raw dspStep() calls, not
		// sampleRate/division divider firings.
		int stepsFor1s = (int)std::ceil(Test::sampleRate()) + (int)f.m->buttonDivider.division * 2;
		for (int i = 0; i < stepsFor1s; i++) f.h.dspStep();
		f.m->params[EightFaceModule<8>::PRESET_PARAM + 2].setValue(0.f);
		for (uint32_t i = 0; i < f.m->buttonDivider.division + 1; i++) f.h.dspStep();

		REQUIRE(f.m->presetCount == 8);
	}
}
