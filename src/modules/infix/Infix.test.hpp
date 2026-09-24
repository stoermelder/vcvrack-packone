#pragma once

// Shared preamble for the INFIX test suite.
// Included by Infix.test.cpp, which pulls the test cases in from Infix.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Infix.cpp"

using namespace StoermelderPackOne::Infix;

Test::TestContext<> testContext;

// Connect a polyphonic input with the given voltages (one per channel).
static void setPolyInput(InfixModule<16>* m, std::initializer_list<float> voltages) {
	int ch = 0;
	for (float v : voltages) {
		m->inputs[InfixModule<16>::INPUT_POLY].setVoltage(v, ch++);
	}
	m->inputs[InfixModule<16>::INPUT_POLY].channels = (int)voltages.size();
}

// Pre-seed the output so Output::setChannels() isn't blocked by channels==0.
static void seedOutput(InfixModule<16>* m) {
	m->outputs[InfixModule<16>::OUTPUT_POLY].channels = 16;
}

// Connect a single mono replacement input on channel c.
static void setMonoInput(InfixModule<16>* m, int c, float voltage) {
	m->inputs[InfixModule<16>::INPUT_MONO + c].channels = 1;
	m->inputs[InfixModule<16>::INPUT_MONO + c].setVoltage(voltage);
}

// Disconnect a mono replacement input on channel c.
static void disconnectMonoInput(InfixModule<16>* m, int c) {
	m->inputs[InfixModule<16>::INPUT_MONO + c].channels = 0;
}