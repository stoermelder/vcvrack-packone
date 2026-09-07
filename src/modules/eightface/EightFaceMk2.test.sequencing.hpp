// Sequencing (SLOTCVMODE): the manual's table in docs/eightface/EightFaceMk2.md's "Sequencing
// and selecting snapshots" section.
//
// Mirrors EightFace.test.sequencing.hpp's fixture and per-mode tests (mk1's equivalent suite):
// same trigger/step scaffolding, same expected sequences where the two modules agree, differing
// only where mk2's behavior genuinely differs from mk1's (VOLT's clamp, C4's presetLoad(int, ...)
// signature, and mk2 resetting the three random modes where mk1 does not).

// Fixture firing SLOT/RESET inputs like real cables, through Test::Harness::connectInput() rather
// than touching Port internals directly -- setChannels() alone is a no-op while disconnected
// (Port.hpp: "If disconnected, this does nothing"), so a bare setVoltage() never makes
// isConnected() true and the whole CV/RESET branch in process() would silently never run.
struct SequencingFixture {
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m;
	EightFaceMk2Widget<8>* mw;

	explicit SequencingFixture() {
		m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
		mw = h.addWidget<EightFaceMk2Widget<8>>(m);

		// Every slot used, so presetLoad() never bails out on an empty target -- sequencing tests
		// are about which slot gets selected, not whether a particular one is populated (that's
		// the "Empty slots" test below, which deliberately empties one).
		for (int i = 0; i < 8; i++) {
			m->presetSlotUsed[i] = true;
		}
	}

	// Fires a clean 0V -> 10V -> 0V pulse on INPUT_CV, tripping slotTrigger exactly once, then
	// runs process() so the resulting presetLoad() (if any) takes effect on `preset`/`presetPrev`.
	void pulseCv() {
		h.connectInput(m, EightFaceMk2Module<8>::INPUT_CV, 10.f);
		h.dspStep();
		h.connectInput(m, EightFaceMk2Module<8>::INPUT_CV, 0.f);
		h.dspStep();
	}

	void pulseReset() {
		h.connectInput(m, EightFaceMk2Module<8>::INPUT_RESET, 10.f);
		h.dspStep();
		h.connectInput(m, EightFaceMk2Module<8>::INPUT_RESET, 0.f);
		h.dspStep();
	}

	void setCvVoltage(float v) {
		h.connectInput(m, EightFaceMk2Module<8>::INPUT_CV, v);
		h.dspStep();
	}

	// Establishes presetTotal/N[0] (which expSlot()/presetLoad() need) and runs resetTimer past
	// the 1ms guard before the first trigger in a test body.
	//
	// Also primes slotTrigger: dsp::SchmittTrigger<float> starts UNINITIALIZED, and its
	// first-ever transition into HIGH or LOW from that state never reports triggered (digital
	// .hpp: only a genuine LOW->HIGH edge does). process()'s CV branch only runs once
	// INPUT_CV.isConnected() (gated in EightFaceMk2.cpp), so slotTrigger sees no calls at all
	// during the idle dspStep()s above -- its first-ever call is a test's own first pulseCv(),
	// which would otherwise be silently absorbed and throw off every following pulse by one. The
	// throwaway pulse here absorbs that priming transition instead.
	//
	// resetTrigger needs no such priming: process()'s RESET branch runs unconditionally on every
	// process() call (not gated on isConnected()), so it has already primed itself to LOW well
	// before this function returns, from getVoltage() reading the port's untouched 0V default
	// across the idle dspStep()s above.
	void settle() {
		for (int i = 0; i < 100; i++) h.dspStep();
		pulseCv();
	}
};

TEST_CASE("TRIG_FWD advances and wraps at presetCount", "[EightFaceMk2][sequencing]") {
	// The baseline case; every other trigger mode's scaffolding mirrors this one.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	f.m->presetCount = 4;
	f.m->preset = 0;
	f.settle();

	// Wraps at presetCount (4), not NUM_PRESETS (8) -- a shortened sequence is the case most
	// likely to break a naive modulo.
	for (int expected : {1, 2, 3, 0, 1}) {
		f.pulseCv();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_REV advances backward and wraps", "[EightFaceMk2][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_REV;
	f.m->presetCount = 4;
	f.m->preset = 0;
	f.settle();

	for (int expected : {3, 2, 1, 0, 3}) {
		f.pulseCv();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_PINGPONG reverses direction at both ends", "[EightFaceMk2][sequencing]") {
	// Regression for #191/#203 (hanging pingpong on manual slot change).
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_PINGPONG;
	f.m->presetCount = 4;
	f.m->preset = 0;
	f.m->slotCvModeDir = 1;
	f.settle();

	// 0 -> 1 -> 2 -> 3 -> 2 -> 1 -> 0 -> 1 ...
	for (int expected : {1, 2, 3, 2, 1, 0, 1}) {
		f.pulseCv();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_ALT produces the manual's literal 1,2,1,3,1,4... sequence", "[EightFaceMk2][sequencing]") {
	// The manual gives this exact sequence for 6 slots -- assert it literally, rather than
	// re-deriving the recurrence, so a change that still "looks alternating" but disagrees with
	// the documented sequence is caught. 0-indexed here as 0,1,0,2,0,3,0,4,0,5.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_ALT;
	f.m->presetCount = 6;
	f.m->preset = 0;
	f.m->slotCvModeDir = 1;
	f.m->slotCvModeAlt = 0;
	f.settle();

	for (int expected : {1, 0, 2, 0, 3, 0, 4, 0, 5, 0}) {
		f.pulseCv();
		REQUIRE(f.m->preset == expected);
	}
}

TEST_CASE("TRIG_RANDOM_WO_REPEAT never repeats the same slot consecutively", "[EightFaceMk2][sequencing]") {
	// Deterministic property over many triggers, regardless of the RNG's actual sequence.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WO_REPEAT;
	f.m->presetCount = 8;
	f.m->preset = 0;
	f.settle();

	int prev = f.m->preset;
	for (int i = 0; i < 100; i++) {
		f.pulseCv();
		REQUIRE(f.m->preset != prev);
		REQUIRE(f.m->preset >= 0);
		REQUIRE(f.m->preset < 8);
		prev = f.m->preset;
	}
}

TEST_CASE("TRIG_RANDOM_WALK only moves +-1 and clamps at the ends", "[EightFaceMk2][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WALK;
	f.m->presetCount = 8;
	f.m->preset = 4;
	f.settle();

	int prev = f.m->preset;
	for (int i = 0; i < 200; i++) {
		f.pulseCv();
		int cur = f.m->preset;
		REQUIRE(cur >= 0);
		REQUIRE(cur <= 7);
		int step = cur - prev;
		// At the boundary the walk clamps rather than moving -1/+1 illegally, so a repeat (step 0)
		// is possible only there.
		REQUIRE(((step == 1 || step == -1) || ((prev == 0 || prev == 7) && step == 0)));
		prev = cur;
	}
}

TEST_CASE("TRIG_SHUFFLE visits every slot exactly once per permutation", "[EightFaceMk2][sequencing]") {
	// Review #6 was fixed here; no regression test existed before this one.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_SHUFFLE;
	f.m->presetCount = 8;
	f.m->preset = -1;
	f.m->slotCvModeShuffle.clear();
	f.settle();

	std::set<int> visited;
	for (int i = 0; i < 8; i++) {
		f.pulseCv();
		REQUIRE(f.m->preset >= 0);
		REQUIRE(f.m->preset < 8);
		REQUIRE(visited.count(f.m->preset) == 0);
		visited.insert(f.m->preset);
	}
	REQUIRE(visited.size() == 8);

	// The bag refills (slotCvModeShuffle.size() == 0) and a new permutation starts -- still every
	// slot exactly once.
	std::set<int> visited2;
	for (int i = 0; i < 8; i++) {
		f.pulseCv();
		REQUIRE(visited2.count(f.m->preset) == 0);
		visited2.insert(f.m->preset);
	}
	REQUIRE(visited2.size() == 8);
}

TEST_CASE("VOLT maps 0-10V across presetCount, including exactly 10V", "[EightFaceMk2][sequencing]") {
	// Regression for #377 (crash above 10V). EightFaceMk2.cpp:326 clamps the input to
	// [0, 10-1e-6] before rescaling, so mk2 -- unlike mk1 -- never lands on floor(presetCount)
	// (out of range) even at exactly 10V.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::VOLT;
	f.m->presetCount = 4;
	f.settle();

	f.setCvVoltage(0.f);
	REQUIRE(f.m->preset == 0);

	// Each 2.5V step should land one slot further: floor(rescale(v, 0,10, 0,4)).
	f.setCvVoltage(2.6f);
	REQUIRE(f.m->preset == 1);
	f.setCvVoltage(5.1f);
	REQUIRE(f.m->preset == 2);
	f.setCvVoltage(7.6f);
	REQUIRE(f.m->preset == 3);

	// Exactly 10V must not crash and must land on the last slot (clamped just under 10, then
	// floor'd), not presetCount itself.
	REQUIRE_NOTHROW(f.setCvVoltage(10.f));
	REQUIRE(f.m->preset == 3);

	// Above 10V (a misbehaving CV source) must be equally safe.
	REQUIRE_NOTHROW(f.setCvVoltage(15.f));
	REQUIRE(f.m->preset == 3);
}

TEST_CASE("C4 follows V/Oct across presetTotal, and channel 2 retriggers the current slot", "[EightFaceMk2][sequencing]") {
	// Added in v2.0.0 (#330), untested before this. C4 spans presetTotal (all NUM_PRESETS slots
	// on this controller, since no expander is attached here == 8), NOT presetCount, unlike every
	// other mode in this file -- EightFaceMk2.cpp:329's clamp upper bound is presetTotal - 1.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::C4;
	f.m->presetCount = 8;
	f.settle();

	// round(clamp(v*12, 0, presetTotal-1)) -- 1V/oct, C4 (0V) = slot 0.
	f.setCvVoltage(0.f);
	REQUIRE(f.m->preset == 0);

	f.setCvVoltage(3.f / 12.f);
	REQUIRE(f.m->preset == 3);

	// A channel-2 retrigger on the same (unchanged) V/Oct value must re-apply the current slot
	// rather than requiring the voltage to change first.
	f.h.connectInputPoly(f.m, EightFaceMk2Module<8>::INPUT_CV, 2, 3.f / 12.f);
	f.h.dspStep();
	f.m->inputs[EightFaceMk2Module<8>::INPUT_CV].setVoltage(10.f, 1);
	f.h.dspStep();
	f.m->inputs[EightFaceMk2Module<8>::INPUT_CV].setVoltage(0.f, 1);
	f.h.dspStep();
	REQUIRE(f.m->preset == 3);
}

TEST_CASE("ARM arms a slot and applies it on the next trigger", "[EightFaceMk2][sequencing]") {
	// Manual: yellow LED (armed, not yet loaded) then white/blue on trigger (applied).
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::ARM;
	f.m->presetCount = 8;
	f.m->preset = 0;
	f.settle();

	// Arming happens via a short button press with isNext=true (EightFaceMk2.cpp:430:
	// "presetLoad(i, slotCvMode == SLOTCVMODE::ARM, true)"), not through CV -- simulate the press
	// result directly, since driving LongPressButton's own timing is a separate concern.
	f.m->presetLoad(3, /*isNext=*/true);
	REQUIRE(f.m->presetNext == 3);
	REQUIRE(f.m->preset == 0);  // not applied yet

	// ARM's CV branch (EightFaceMk2.cpp:410-414) applies presetNext on the next trigger.
	f.pulseCv();
	REQUIRE(f.m->preset == 3);
}

TEST_CASE("OFF ignores all CV", "[EightFaceMk2][sequencing]") {
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::OFF;
	f.m->presetCount = 8;
	f.m->preset = 2;
	f.settle();

	f.pulseCv();
	f.pulseReset();
	// OFF has no case in the CV switch (EightFaceMk2.cpp:324-417's default: break) -- preset must
	// never move from CV alone under this mode.
	REQUIRE(f.m->preset == 2);
}

TEST_CASE("Empty slots are part of the sequence but are never applied", "[EightFaceMk2][sequencing]") {
	// docs/eightface/EightFaceMk2.md: "Empty slots are part of the sequence but won't have any
	// effect on the bound modules." presetLoad() (EightFaceMk2.cpp:607-627) moves `preset` to the
	// target index unconditionally, then returns without dispatching if presetSlotUsed[p] is
	// false -- so the sequencing cursor still lands on an empty slot, but no task is queued.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	f.m->presetCount = 4;
	f.m->preset = 0;
	// Slot 1 is deliberately emptied.
	for (json_t* vJ : f.m->EightFaceMk2Base<8>::preset[1]) json_decref(vJ);
	f.m->EightFaceMk2Base<8>::preset[1].clear();
	f.m->presetSlotUsed[1] = false;
	f.settle();

	f.pulseCv();
	// The cursor still advances onto the empty slot...
	REQUIRE(f.m->preset == 1);
	// ...and nothing was queued to dispatch for it (dispatch.guiSafeMode defaults to
	// GUI_WITH_LOCK, so a real dispatch would have enqueued into guiTasks).
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 0);

	// Sequencing continues normally past the empty slot.
	f.pulseCv();
	REQUIRE(f.m->preset == 2);
	f.pulseCv();
	REQUIRE(f.m->preset == 3);
}

TEST_CASE("A trigger within 1ms after RESET is ignored", "[EightFaceMk2][sequencing]") {
	// The resetTimer.getTime() >= 1e-3f guard (EightFaceMk2.cpp:336 et al.), added in v1.2.0 for
	// Rack's voltage standards -- a trigger arriving in the same or next sample as RESET must not
	// also advance the sequence.
	SequencingFixture f;
	f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	f.m->presetCount = 4;
	f.m->preset = 0;
	f.settle();

	f.pulseReset();
	REQUIRE(f.m->preset == 0);  // RESET on TRIG_FWD jumps to slot 0

	// Immediately (same millisecond) pulse CV -- must be ignored.
	f.pulseCv();
	REQUIRE(f.m->preset == 0);

	// After the 1ms window has elapsed, the same pulse must be honored.
	int steps = (int)(Test::sampleRate() * 2e-3f) + 1;
	for (int i = 0; i < steps; i++) f.h.dspStep();
	f.pulseCv();
	REQUIRE(f.m->preset == 1);
}

TEST_CASE("RESET behavior matches the manual's Reset column, per mode", "[EightFaceMk2][sequencing]") {
	// mk2 resets FWD/REV/PINGPONG/ALT AND the three random modes -- unlike mk1, which has no reset
	// handling for the random modes at all (see EightFace.test.sequencing.hpp's equivalent, where
	// those SECTIONs are marked EXPECTED TO FAIL). EightFaceMk2.cpp:292-296 groups TRIG_FWD/
	// TRIG_RANDOM/TRIG_RANDOM_WALK/TRIG_RANDOM_WO_REPEAT together, all reset to presetLoad(0), so
	// mk2's random-mode SECTIONs below are ordinary passing tests, not expected failures.
	SequencingFixture f;
	f.m->presetCount = 4;

	SECTION("TRIG_FWD resets to slot 0") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_FWD;
		f.m->preset = 2;
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_REV resets to the last slot") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_REV;
		f.m->preset = 0;
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->preset == f.m->presetCount - 1);
	}

	SECTION("TRIG_PINGPONG resets to slot 0 and forward direction") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_PINGPONG;
		f.m->preset = 2;
		f.m->slotCvModeDir = -1;
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
		REQUIRE(f.m->slotCvModeDir == 1);
	}

	SECTION("TRIG_ALT resets to slot 0") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_ALT;
		f.m->preset = 3;
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_SHUFFLE clears the pending permutation") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_SHUFFLE;
		f.m->slotCvModeShuffle = {1, 2, 3};
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->slotCvModeShuffle.empty());
	}

	SECTION("TRIG_RANDOM resets to slot 0") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM;
		f.m->preset = 3;
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_RANDOM_WO_REPEAT resets to slot 0") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WO_REPEAT;
		f.m->preset = 3;
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}

	SECTION("TRIG_RANDOM_WALK resets to slot 0") {
		f.m->slotCvMode = SLOTCVMODE::TRIG_RANDOM_WALK;
		f.m->preset = 3;
		f.settle();
		f.pulseReset();
		REQUIRE(f.m->preset == 0);
	}
}
