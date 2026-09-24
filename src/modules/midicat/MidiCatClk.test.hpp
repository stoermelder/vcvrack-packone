#pragma once

// Shared preamble for the MIDICATCLK test suite.
// Included by MidiCatClk.test.cpp, which pulls the test cases in from MidiCatClk.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiCatClk.cpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne::MidiCat;

Test::TestContext<> testContext;

struct TestParamModule : Module {
	enum ParamIds { PARAM_A, NUM_PARAMS };
	TestParamModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(PARAM_A, 0.f, 1.f, 0.5f, "Parameter A");
	}
};

// Helper: send a low-then-high-then-low clock pulse on clock input `input`.
// Assumes the SchmittTrigger for that input is already primed to LOW (guaranteed
// after connectClk()).
static void sendClockPulse(Test::Harness& h, MidiCatModule* midicat, MidiCatClkModule* clk, int input) {
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].channels = 1;
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].setVoltage(10.f);
	h.dspStep();
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].setVoltage(0.f);
	h.dspStep();
}

// Helper: set up a full CC-to-param binding.
// Learns CC `cc` on channel `id` and binds to `target->PARAM_A`.
static void setupBinding(Test::Harness& h, MidiCatModule* midicat, TestParamModule* target, int id, int cc) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(id, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(id, target->id, TestParamModule::PARAM_A);
	h.dspStep();
	midicat->slots[id].cc.ccMode = CCMODE::DIRECT;
}