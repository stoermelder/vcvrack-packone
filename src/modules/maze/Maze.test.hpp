#pragma once

// Shared preamble for the MAZE test suite.
// Included by Maze.test.cpp, which pulls the test cases in from Maze.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "Maze.cpp"

using namespace StoermelderPackOne::Maze;

using MazeMod = MazeModule<32, 4>;

Test::TestContext<> testContext;

// Warm up the reset timer so the clock trigger guard (>= 1ms) is satisfied
static void warmupTimer(Test::Harness& h, MazeMod* module, int samples = 100) {
	module->inputs[MazeMod::CLK_INPUT].channels = 1;
	module->inputs[MazeMod::CLK_INPUT].setVoltage(0.f);
	h.dspSteps(samples);
}

// Fire a single clock rising edge on port 0
static void clockEdge(Test::Harness& h, MazeMod* module) {
	module->inputs[MazeMod::CLK_INPUT].setVoltage(10.f);
	h.dspStep();
	module->inputs[MazeMod::CLK_INPUT].setVoltage(0.f);
}