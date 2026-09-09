#pragma once

// Shared preamble for the MIDICATFINE test suite.
// Included by MidiCatFine.test.cpp, which pulls the test cases in from MidiCatFine.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiCatFine.cpp"
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

// Helper: set up a full CC-to-param binding.
static void setupBinding(Test::Harness& h, MidiCatModule* midicat, TestParamModule* target, int id, int cc) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(id, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(id, target->id, TestParamModule::PARAM_A);
	h.dspStep();
	midicat->slots[id].cc.ccMode = CCMODE::DIRECT;
}

// SchmittTriggers in the parent MidiCat start UNINITIALIZED, so we prime them first by
// stepping with the input at 0V before driving them high.
static void primeFineTriggers(Test::Harness& h, MidiCatFineModule* fine) {
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(0.f);
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(0.f);
	h.dspStep();
}