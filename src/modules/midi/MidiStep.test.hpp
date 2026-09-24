#pragma once

// Shared preamble for the MIDISTEP test suite.
// Included by MidiStep.test.cpp, which pulls the test cases in from MidiStep.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiStep.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiStep;

static Test::TestContext<> testContext;

// Convenience: build a CC message on channel 0.
static rack::midi::Message cc(uint8_t ccNum, uint8_t value) {
	return Test::makeMidiMessage(0xb, 0, ccNum, value);
}

// Drive process() until OUTPUT_INC/OUTPUT_DEC voltage on the given output/channel
// reads high, or give up. Returns true if a high (10V) sample was observed.
static bool pollHigh(Test::Harness& h, MidiStepModule* module, int out, int channel = 0, int frames = 4096) {
	bool high = false;
	for (int i = 0; i < frames; i++) {
		h.dspStep();
		if (module->outputs[out].getVoltage(channel) > 5.f) high = true;
	}
	return high;
}