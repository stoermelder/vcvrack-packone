// Arena.test.seqinput.hpp — SEQ_INPUT trigger-mode coverage (review §3.4).
// Covers seqProcess()'s seven XYSEQ_MODE branches (XySeqWidget.hpp:96-144),
// called from Arena.cpp:293 whenever SEQ_INPUT is patched. Nothing in the
// suite drove this input before; every mode advances/selects seqSelected[port]
// with a different edge case, none of them previously pinned.

#include "Arena.test.hpp"


// Fires a clean 0V -> 10V -> 0V pulse on SEQ_INPUT + port, mimicking a real
// cable. See primeSeqTrigger() below for why the first call on a fresh
// trigger needs special handling.
static void pulseSeq(Test::Harness& h, MODULE* m, int port) {
	h.connectInput(m, MODULE::SEQ_INPUT + port, 10.f);
	h.dspStep();
	h.connectInput(m, MODULE::SEQ_INPUT + port, 0.f);
	h.dspStep();
}

// Arms seqTrigger[port]: its default state is UNINITIALIZED, and the first
// process(in >= highThreshold) call only transitions UNINITIALIZED -> HIGH
// without returning true (Rack/include/dsp/digital.hpp) -- an untriggered
// arm, not a missed edge. Call this once per port before treating pulseSeq()
// as a real, counted trigger.
static void primeSeqTrigger(Test::Harness& h, MODULE* m, int port) {
	pulseSeq(h, m, port);
}


TEST_CASE("SEQ_INPUT TRIG_FWD advances only through non-empty slots", "[Arena][Seq]") {
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::TRIG_FWD;
	primeSeqTrigger(h, m, 0);
	// Only slots 0 and 3 are populated; 1 and 2 must be skipped entirely.
	markSlotUsed(m, 0, 0);
	markSlotUsed(m, 0, 3);
	m->seqSelected[0] = 0;

	pulseSeq(h, m, 0);
	REQUIRE(m->seqSelected[0] == 3);
	pulseSeq(h, m, 0);
	REQUIRE(m->seqSelected[0] == 0);
	pulseSeq(h, m, 0);
	REQUIRE(m->seqSelected[0] == 3);
	pulseSeq(h, m, 0);
	REQUIRE(m->seqSelected[0] == 0);
}

TEST_CASE("SEQ_INPUT TRIG_REV advances only through non-empty slots, in reverse", "[Arena][Seq]") {
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::TRIG_REV;
	primeSeqTrigger(h, m, 0);
	markSlotUsed(m, 0, 0);
	markSlotUsed(m, 0, 3);
	m->seqSelected[0] = 0;

	// Reverse from slot 0 must land on slot 3 first (wrapping past 1 and 2),
	// not on slot 15 or slot 1 — the mirror image of the FWD test above.
	pulseSeq(h, m, 0);
	REQUIRE(m->seqSelected[0] == 3);
	pulseSeq(h, m, 0);
	REQUIRE(m->seqSelected[0] == 0);
	pulseSeq(h, m, 0);
	REQUIRE(m->seqSelected[0] == 3);
}

TEST_CASE("SEQ_INPUT TRIG_FWD with every slot empty terminates and leaves the selection unchanged", "[Arena][Seq]") {
	// The do/while at XySeqWidget.hpp:101-104 exits on `seqSelected[port] != t`
	// failing, i.e. wrapping all the way back to the start — the one loop in
	// the module that would hang if that exit condition were ever weakened.
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::TRIG_FWD;
	primeSeqTrigger(h, m, 0);
	m->seqSelected[0] = 5;
	// No slot marked used anywhere — seqInit() already leaves every length at 0.

	pulseSeq(h, m, 0);

	REQUIRE(m->seqSelected[0] == 5);
}

TEST_CASE("SEQ_INPUT TRIG_REV with every slot empty terminates and leaves the selection unchanged", "[Arena][Seq]") {
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::TRIG_REV;
	primeSeqTrigger(h, m, 0);
	m->seqSelected[0] = 5;

	pulseSeq(h, m, 0);

	REQUIRE(m->seqSelected[0] == 5);
}

TEST_CASE("SEQ_INPUT VOLT mode maps 0-10V onto slots 0-15 and clamps out-of-range voltage", "[Arena][Seq]") {
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::VOLT;

	auto setVoltAndStep = [&](float v) {
		h.connectInput(m, MODULE::SEQ_INPUT + 0, v);
		h.dspStep();
	};

	setVoltAndStep(0.f);
	REQUIRE(m->seqSelected[0] == 0);

	setVoltAndStep(10.f);
	REQUIRE(m->seqSelected[0] == 15);

	// Out-of-range voltage clamps to the same slot as the nearest in-range
	// boundary, rather than indexing seqData[port][] out of bounds (first
	// review's §1.5 fix — previously verified only by inspection).
	setVoltAndStep(-5.f);
	REQUIRE(m->seqSelected[0] == 0);

	setVoltAndStep(15.f);
	REQUIRE(m->seqSelected[0] == 15);
}

TEST_CASE("SEQ_INPUT C4 mode maps 1V/oct onto slots 0-15 and clamps above the top slot", "[Arena][Seq]") {
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	m->seqMode[0] = StoermelderPackOne::XYSEQ_MODE::C4;

	auto setVoltAndStep = [&](float v) {
		h.connectInput(m, MODULE::SEQ_INPUT + 0, v);
		h.dspStep();
	};

	setVoltAndStep(0.f);
	REQUIRE(m->seqSelected[0] == 0);

	setVoltAndStep(1.f);
	REQUIRE(m->seqSelected[0] == 12);

	// 15/12 V is the exact point clamp(v*12, 0, 15) starts limiting rather
	// than passing v*12 through to round() unchanged.
	setVoltAndStep(1.25f);
	REQUIRE(m->seqSelected[0] == 15);

	setVoltAndStep(2.f);
	REQUIRE(m->seqSelected[0] == 15);
}

TEST_CASE("SEQ_INPUT random trigger modes stay within their declared slot range", "[Arena][Seq]") {
	// Cheap statistical guard for the first review's §1.4: TRIG_RANDOM_16 uses
	// the literal 16.f (== XYSEQ_COUNT) rather than XYSEQ_COUNT - 1, relying on
	// random::uniform()'s open [0,1) contract to stay in bounds. Firing many
	// triggers across all three random modes is the cheapest way to notice a
	// regression without asserting on the RNG itself.
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	auto checkRange = [&](StoermelderPackOne::XYSEQ_MODE mode, int upperExclusive) {
		m->seqMode[0] = mode;
		primeSeqTrigger(h, m, 0);
		m->seqSelected[0] = 0;
		for (int i = 0; i < 500; i++) {
			pulseSeq(h, m, 0);
			REQUIRE(m->seqSelected[0] >= 0);
			REQUIRE(m->seqSelected[0] < upperExclusive);
		}
	};

	checkRange(StoermelderPackOne::XYSEQ_MODE::TRIG_RANDOM_16, 16);
	checkRange(StoermelderPackOne::XYSEQ_MODE::TRIG_RANDOM_8, 8);
	checkRange(StoermelderPackOne::XYSEQ_MODE::TRIG_RANDOM_4, 4);
}
