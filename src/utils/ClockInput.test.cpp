#include "../test/test_plugin.hpp"
#include "ClockInput.hpp"

using namespace StoermelderPackOne;

namespace {
const float kSample = 1.f / 48000.f;

// Runs `samples` samples of the external clock and returns the ticks produced. The input is 10 V
// for `highSamples` samples every `period` samples (0 = silent).
int runExternal(ClockInput& c, int samples, bool running, int multiply, int divide, int period, int highSamples = 24) {
	int ticks = 0;
	for (int i = 0; i < samples; i++) {
		float v = (period > 0 && i % period < highSamples) ? 10.f : 0.f;
		ticks += c.processExternal(running, v, multiply, divide);
	}
	return ticks;
}

int runInternal(ClockInput& c, int samples, bool active, float bpm, float ticksPerQuarter) {
	int ticks = 0;
	for (int i = 0; i < samples; i++) ticks += c.processInternal(kSample, active, bpm, ticksPerQuarter);
	return ticks;
}

// A Schmitt trigger starts high, so give the input a low sample before the first edge.
void primeExternal(ClockInput& c, int multiply, int divide) {
	runExternal(c, 10, true, multiply, divide, 0);
}
}

TEST_CASE("Not running produces no ticks from either source", "[ClockInput]") {
	ClockInput c;
	REQUIRE(runInternal(c, 48000, false, 120.f, 24.f) == 0);
	REQUIRE(runExternal(c, 48000, false, 1, 1, 4800) == 0);
}

TEST_CASE("Internal clock ticks at ticksPerQuarter per quarter note", "[ClockInput]") {
	// 120 BPM = 2 quarter notes per second.
	ClockInput a;
	int t24 = runInternal(a, 48000, true, 120.f, 24.f);
	REQUIRE(t24 >= 47);
	REQUIRE(t24 <= 49);
	ClockInput b;
	int t4 = runInternal(b, 48000, true, 120.f, 4.f);
	REQUIRE(t4 >= 7);
	REQUIRE(t4 <= 9);
}

TEST_CASE("External x1 passes every edge", "[ClockInput]") {
	ClockInput c;
	primeExternal(c, 1, 1);
	REQUIRE(runExternal(c, 48000, true, 1, 1, 4800) == 10);
}

TEST_CASE("External x6 spreads six ticks over each period", "[ClockInput]") {
	ClockInput c;
	primeExternal(c, 6, 1);
	runExternal(c, 4800 * 3, true, 6, 1, 4800); // lets the multiplier measure the period
	int ticks = runExternal(c, 4800 * 10, true, 6, 1, 4800);
	REQUIRE(ticks >= 58);
	REQUIRE(ticks <= 61);
}

TEST_CASE("External /4 ticks on every fourth edge", "[ClockInput]") {
	ClockInput c;
	primeExternal(c, 1, 4);
	int ticks = runExternal(c, 4800 * 40, true, 1, 4, 4800);
	REQUIRE(ticks == 10);
}

TEST_CASE("An external tick restarts the internal phase", "[ClockInput]") {
	ClockInput c;
	primeExternal(c, 1, 1);
	runInternal(c, 500, true, 120.f, 24.f);
	REQUIRE(c.phase() > 0.f);
	runExternal(c, 100, true, 1, 1, 100);
	REQUIRE(c.phase() == 0.f);
}

TEST_CASE("resetPhase delays the internal clock's next tick by a full period", "[ClockInput]") {
	ClockInput c;
	// 120 BPM at 24/quarter is one tick per 1000 samples: just short of one, reset, just short again.
	REQUIRE(runInternal(c, 900, true, 120.f, 24.f) == 0);
	c.resetPhase();
	REQUIRE(runInternal(c, 900, true, 120.f, 24.f) == 0);
}

TEST_CASE("reset forgets the phase and the ratio state", "[ClockInput]") {
	ClockInput c;
	runInternal(c, 500, true, 120.f, 24.f);
	c.reset();
	REQUIRE(c.phase() == 0.f);
}
