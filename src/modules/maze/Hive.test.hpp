#pragma once

// Shared preamble for the HIVE test suite.
// Included by Hive.test.cpp, which pulls the test cases in from Hive.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "Hive.cpp"

using namespace StoermelderPackOne::Hive;

using HiveMod = HiveModule<MAX_RADIUS, 4>;

Test::TestContext<> testContext;

// Warm up the reset timer so the clock trigger guard (>= 1ms) is satisfied
static void warmupTimer(Test::Harness& h, HiveMod* module, int samples = 100) {
	module->inputs[HiveMod::CLK_INPUT].channels = 1;
	module->inputs[HiveMod::CLK_INPUT].setVoltage(0.f);
	h.dspSteps(samples);
}

// Fire a single clock rising edge on port 0
static void clockEdge(Test::Harness& h, HiveMod* module) {
	module->inputs[HiveMod::CLK_INPUT].setVoltage(10.f);
	h.dspStep();
	module->inputs[HiveMod::CLK_INPUT].setVoltage(0.f);
}