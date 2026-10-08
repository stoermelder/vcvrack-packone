// Arena.test.seq.hpp — SEQ_PH_INPUT motion-sequence playback (review §3.3).
// Covers process()'s "phase in, position out" path (Arena.cpp:298-305) and the
// seqValue() interpolation it calls into (XySeqWidget.hpp:146-188). Nothing
// in the suite exercised this before: every other Arena test either leaves
// SEQ_PH_INPUT unpatched or drives positions directly.

#include "Arena.test.hpp"


TEST_CASE("SEQ_PH_INPUT - linear interpolation lands on the first point, midpoint and last point", "[Arena][Seq]") {
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	// 3-point sequence on MIX-0's currently-selected slot.
	setSeqData(m, 0, {0.1f, 0.5f, 0.9f}, {0.2f, 0.6f, 1.0f});
	m->seqInterpolate[0] = StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR;

	auto setPhase = [&](float volts) {
		m->inputs[MODULE::SEQ_PH_INPUT + 0].channels = 1;
		m->inputs[MODULE::SEQ_PH_INPUT + 0].setVoltage(volts);
		h.dspStep();
	};

	setPhase(0.f);
	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.1f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(0.2f));

	setPhase(5.f);
	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.5f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(0.6f));

	setPhase(10.f);
	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.9f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(1.0f));
}

TEST_CASE("SEQ_PH_INPUT - out-of-range phase voltage clamps to the sequence's ends", "[Arena][Seq]") {
	// The first review's §4.5 corrected the manual to describe -5V as 0V and
	// 15V as 10V; nothing pinned that behaviour in code until now.
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	setSeqData(m, 0, {0.25f, 0.75f}, {0.1f, 0.9f});
	m->seqInterpolate[0] = StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR;

	m->inputs[MODULE::SEQ_PH_INPUT + 0].channels = 1;
	m->inputs[MODULE::SEQ_PH_INPUT + 0].setVoltage(-5.f);
	h.dspStep();
	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.25f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(0.1f));

	m->inputs[MODULE::SEQ_PH_INPUT + 0].setVoltage(15.f);
	h.dspStep();
	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.75f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(0.9f));
}

TEST_CASE("SEQ_PH_INPUT - an empty sequence returns dead centre", "[Arena][Seq]") {
	// seqValue() special-cases length == 0 (XySeqWidget.hpp:148) rather than
	// indexing an empty XySeqItem — surprising enough next to the populated
	// case to pin explicitly.
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	REQUIRE(m->seqLength(0) == 0);

	m->inputs[MODULE::SEQ_PH_INPUT + 0].channels = 1;
	m->inputs[MODULE::SEQ_PH_INPUT + 0].setVoltage(5.f);
	h.dspStep();

	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.5f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(0.5f));
}

TEST_CASE("SEQ_PH_INPUT - CUBIC and LINEAR interpolation agree at the knots but differ mid-segment", "[Arena][Seq]") {
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	// A non-collinear 4-point sequence so cubic and linear actually disagree
	// away from the knots (a straight line would make them coincide everywhere).
	setSeqData(m, 0, {0.f, 0.2f, 0.9f, 1.f}, {0.f, 0.8f, 0.1f, 1.f});

	auto positionAt = [&](StoermelderPackOne::XYSEQ_INTERPOLATE interp, float volts) {
		m->seqInterpolate[0] = interp;
		m->inputs[MODULE::SEQ_PH_INPUT + 0].channels = 1;
		m->inputs[MODULE::SEQ_PH_INPUT + 0].setVoltage(volts);
		h.dspStep();
		return Vec(m->params[MODULE::MIX_X_POS + 0].getValue(), m->params[MODULE::MIX_Y_POS + 0].getValue());
	};

	// At the knots (pos == 0 and pos == 1, i.e. phase 0V/10V) both modes land
	// exactly on the first/last stored point.
	Vec linStart = positionAt(StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR, 0.f);
	Vec cubStart = positionAt(StoermelderPackOne::XYSEQ_INTERPOLATE::CUBIC, 0.f);
	REQUIRE(linStart.x == Catch::Approx(0.f));
	REQUIRE(linStart.y == Catch::Approx(0.f));
	REQUIRE(cubStart.x == Catch::Approx(linStart.x));
	REQUIRE(cubStart.y == Catch::Approx(linStart.y));

	Vec linEnd = positionAt(StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR, 10.f);
	Vec cubEnd = positionAt(StoermelderPackOne::XYSEQ_INTERPOLATE::CUBIC, 10.f);
	REQUIRE(linEnd.x == Catch::Approx(1.f));
	REQUIRE(linEnd.y == Catch::Approx(1.f));
	REQUIRE(cubEnd.x == Catch::Approx(linEnd.x));
	REQUIRE(cubEnd.y == Catch::Approx(linEnd.y));

	// Mid-segment (between point 1 and point 2) the two interpolations diverge.
	Vec linMid = positionAt(StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR, 5.f);
	Vec cubMid = positionAt(StoermelderPackOne::XYSEQ_INTERPOLATE::CUBIC, 5.f);
	REQUIRE((std::abs(linMid.x - cubMid.x) > 0.01f || std::abs(linMid.y - cubMid.y) > 0.01f));
}

TEST_CASE("SEQ_PH_INPUT takes precedence over MIX_X_INPUT/MIX_Y_INPUT when both are patched", "[Arena][Seq]") {
	// process()'s setX/setY short-circuit (Arena.cpp:296-323): a patched phase
	// input silences the plain CV inputs entirely rather than combining with
	// them. That is user-visible — a cable into MIX_X_INPUT stops doing
	// anything the moment SEQ_PH_INPUT is also patched — and was previously
	// encoded only in the flags, with no test.
	Test::Harness h;
	auto* m = h.addModule<MODULE>("Arena");

	setSeqData(m, 0, {0.2f, 0.8f}, {0.3f, 0.7f});
	m->seqInterpolate[0] = StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR;
	// Attenuverters default to 0 (Arena.cpp:155-157), which would silently zero
	// the CV path regardless of precedence; set them to unity so a passing
	// assertion actually demonstrates the short-circuit, not a zeroed input.
	m->params[MODULE::MIX_X_PARAM + 0].setValue(1.f);
	m->params[MODULE::MIX_Y_PARAM + 0].setValue(1.f);

	// Both inputs patched: SEQ_PH_INPUT must win.
	m->inputs[MODULE::SEQ_PH_INPUT + 0].channels = 1;
	m->inputs[MODULE::SEQ_PH_INPUT + 0].setVoltage(0.f);
	m->inputs[MODULE::MIX_X_INPUT + 0].channels = 1;
	m->inputs[MODULE::MIX_X_INPUT + 0].setVoltage(10.f);
	m->inputs[MODULE::MIX_Y_INPUT + 0].channels = 1;
	m->inputs[MODULE::MIX_Y_INPUT + 0].setVoltage(10.f);
	h.dspStep();

	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.2f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(0.3f));

	// Disconnect SEQ_PH_INPUT: the plain CV inputs take over on the next tick.
	h.disconnectPort(m->inputs[MODULE::SEQ_PH_INPUT + 0]);
	h.dspStep();

	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(1.f));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(1.f));
}
