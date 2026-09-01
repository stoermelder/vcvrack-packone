#include "../../test/framework.hpp"

#include "Maze.cpp"

using namespace StoermelderPackOne::Maze;

using MazeMod = MazeModule<32, 4>;

SYNC_MODEL(modelMaze, "Maze");
Test::TestContext<> testContext;

// Warm up the reset timer so the clock trigger guard (>= 1ms) is satisfied
static void warmupTimer(MazeMod* module, int samples = 100) {
	module->inputs[MazeMod::CLK_INPUT].channels = 1;
	module->inputs[MazeMod::CLK_INPUT].setVoltage(0.f);
	for (int i = 0; i < samples; i++) {
		module->process(Test::makeProcessArgs(i));
	}
}

// Fire a single clock rising edge on port 0
static void clockEdge(MazeMod* module, int frame = 200) {
	module->inputs[MazeMod::CLK_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(frame));
	module->inputs[MazeMod::CLK_INPUT].setVoltage(0.f);
}

TEST_CASE("Construction and initialization", "[Maze]") {
	MazeMod* m = Test::createModule<MazeMod>("Maze");
	MazeWidget32* mw = Test::createWidget<MazeWidget32>("Maze");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Maze][JSON]") {
	auto module = Test::createModule<MazeMod>("Maze");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves state", "[JSON][Maze]") {
	MazeMod* m = Test::createModule<MazeMod>("Maze");
	MazeMod* m2 = Test::createModule<MazeMod>("Maze");

	SECTION("Scalar settings round-trip") {
		// Non-default values (defaults: panelTheme 0, usedSize 8, normalizePorts true)
		m->panelTheme = 1;
		m->gridResize(12);
		m->normalizePorts = false;

		json_t* j = m->dataToJson();
		// Start m2 at defaults so dataFromJson() is genuinely exercised
		m2->panelTheme = 0;
		m2->usedSize = 8;
		m2->normalizePorts = true;
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->usedSize == 12);
		REQUIRE(m2->normalizePorts == false);
	}

	SECTION("Grid arrays (grid/gridCv) round-trip") {
		m->gridResize(12);
		m->gridSetState(2, 3, GRIDSTATE::ON, 0.6f);
		m->gridSetState(5, 1, GRIDSTATE::RANDOM, 0.3f);
		m->gridSetState(10, 7, GRIDSTATE::ON, 0.9f);

		json_t* j = m->dataToJson();
		// The grid arrays must be serialized as flat SIZE*SIZE elements
		json_t* gridJ = json_object_get(j, "grid");
		json_t* gridCvJ = json_object_get(j, "gridCv");
		REQUIRE(gridJ != nullptr);
		REQUIRE(gridCvJ != nullptr);
		REQUIRE(json_array_size(gridJ) == (size_t)(32 * 32));
		REQUIRE(json_array_size(gridCvJ) == (size_t)(32 * 32));
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->grid[2][3] == GRIDSTATE::ON);
		REQUIRE(m2->gridCv[2][3] == Catch::Approx(0.6f));
		REQUIRE(m2->grid[5][1] == GRIDSTATE::RANDOM);
		REQUIRE(m2->gridCv[5][1] == Catch::Approx(0.3f));
		REQUIRE(m2->grid[10][7] == GRIDSTATE::ON);
		REQUIRE(m2->gridCv[10][7] == Catch::Approx(0.9f));
		// Untouched cells stay OFF with zero CV
		REQUIRE(m2->grid[0][0] == GRIDSTATE::OFF);
		REQUIRE(m2->gridCv[0][0] == Catch::Approx(0.f));
	}

	SECTION("Ports array round-trip") {
		// Distinctive per-port start/direction/position and mode settings
		m->xStartPos[0] = 1; m->yStartPos[0] = 2;
		m->xStartDir[0] = -1; m->yStartDir[0] = 0;
		m->xPos[0] = 3; m->yPos[0] = 4;
		m->xDir[0] = 0; m->yDir[0] = 1;
		m->turnMode[0] = TURNMODE::ONEEIGHTY;
		m->outMode[0] = OUTMODE::BI_5V;
		m->ratchetingEnabled[0] = RATCHETMODE::MULT_TWO;
		m->ratchetingSetProb(0, 0.7f);

		m->xStartPos[2] = 5; m->yStartPos[2] = 6;
		m->xStartDir[2] = 1; m->yStartDir[2] = 0;
		m->xPos[2] = 7; m->yPos[2] = 8;
		m->xDir[2] = -1; m->yDir[2] = 0;
		m->turnMode[2] = TURNMODE::NINETY;
		m->outMode[2] = OUTMODE::UNI_1V;
		m->ratchetingEnabled[2] = RATCHETMODE::POWER_TWO;
		m->ratchetingSetProb(2, 0.2f);

		json_t* j = m->dataToJson();
		// The ports array must be serialized with one entry per port
		json_t* portsJ = json_object_get(j, "ports");
		REQUIRE(portsJ != nullptr);
		REQUIRE(json_array_size(portsJ) == (size_t) 4);
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->xStartPos[0] == 1);
		REQUIRE(m2->yStartPos[0] == 2);
		REQUIRE(m2->xStartDir[0] == -1);
		REQUIRE(m2->yStartDir[0] == 0);
		REQUIRE(m2->xPos[0] == 3);
		REQUIRE(m2->yPos[0] == 4);
		REQUIRE(m2->xDir[0] == 0);
		REQUIRE(m2->yDir[0] == 1);
		REQUIRE(m2->turnMode[0] == TURNMODE::ONEEIGHTY);
		REQUIRE(m2->outMode[0] == OUTMODE::BI_5V);
		REQUIRE(m2->ratchetingEnabled[0] == RATCHETMODE::MULT_TWO);
		REQUIRE(m2->ratchetingProb[0] == Catch::Approx(0.7f));

		REQUIRE(m2->xStartPos[2] == 5);
		REQUIRE(m2->yStartPos[2] == 6);
		REQUIRE(m2->xStartDir[2] == 1);
		REQUIRE(m2->yStartDir[2] == 0);
		REQUIRE(m2->xPos[2] == 7);
		REQUIRE(m2->yPos[2] == 8);
		REQUIRE(m2->xDir[2] == -1);
		REQUIRE(m2->yDir[2] == 0);
		REQUIRE(m2->turnMode[2] == TURNMODE::NINETY);
		REQUIRE(m2->outMode[2] == OUTMODE::UNI_1V);
		REQUIRE(m2->ratchetingEnabled[2] == RATCHETMODE::POWER_TWO);
		REQUIRE(m2->ratchetingProb[2] == Catch::Approx(0.2f));
	}

	Test::destroyModule(m);
	Test::destroyModule(m2);
}


TEST_CASE("Reset clears grid and restores cursor defaults", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");

	module->grid[2][3] = GRIDSTATE::ON;
	module->xPos[0] = 5;
	module->yPos[0] = 7;
	module->xDir[0] = -1;
	module->turnMode[0] = TURNMODE::ONEEIGHTY;

	Module::ResetEvent re;
	module->onReset(re);

	SECTION("Grid cleared to OFF") {
		bool allOff = true;
		for (int i = 0; i < 32; i++)
			for (int j = 0; j < 32; j++)
				if (module->grid[i][j] != GRIDSTATE::OFF) allOff = false;
		REQUIRE(allOff);
	}

	SECTION("Cursor 0 position reset to start") {
		REQUIRE(module->xPos[0] == module->xStartPos[0]);
		REQUIRE(module->yPos[0] == module->yStartPos[0]);
	}

	SECTION("Cursor 0 direction reset to right") {
		REQUIRE(module->xDir[0] == 1);
		REQUIRE(module->yDir[0] == 0);
	}

	SECTION("turnMode reset to NINETY") {
		REQUIRE(module->turnMode[0] == TURNMODE::NINETY);
	}

	Test::destroyModule(module);
}

TEST_CASE("gridClear sets all cells to OFF with zero CV", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");

	module->gridSetState(0, 0, GRIDSTATE::ON, 0.5f);
	module->gridSetState(3, 4, GRIDSTATE::RANDOM, 0.8f);
	module->gridSetState(7, 7, GRIDSTATE::ON, 0.2f);

	module->gridClear();

	SECTION("All cells OFF") {
		bool allOff = true;
		for (int i = 0; i < 32; i++)
			for (int j = 0; j < 32; j++)
				if (module->grid[i][j] != GRIDSTATE::OFF) allOff = false;
		REQUIRE(allOff);
	}

	SECTION("All CV values zero") {
		bool allZero = true;
		for (int i = 0; i < 32; i++)
			for (int j = 0; j < 32; j++)
				if (module->gridCv[i][j] != 0.f) allZero = false;
		REQUIRE(allZero);
	}

	Test::destroyModule(module);
}

TEST_CASE("gridSetState writes cell state and CV", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");

	module->gridSetState(5, 3, GRIDSTATE::ON, 0.75f);
	module->gridSetState(1, 6, GRIDSTATE::RANDOM, 0.25f);

	SECTION("Cell (5,3) is ON with correct CV") {
		REQUIRE(module->grid[5][3] == GRIDSTATE::ON);
		REQUIRE(module->gridCv[5][3] == Catch::Approx(0.75f));
	}

	SECTION("Cell (1,6) is RANDOM with correct CV") {
		REQUIRE(module->grid[1][6] == GRIDSTATE::RANDOM);
		REQUIRE(module->gridCv[1][6] == Catch::Approx(0.25f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Clock input advances cursor position", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	// Initial: xPos[0]=0, yPos[0]=0, xDir[0]=1 → advances to (1, 0)

	warmupTimer(module);
	clockEdge(module);

	SECTION("xPos advanced by one step rightward") {
		REQUIRE(module->xPos[0] == 1);
		REQUIRE(module->yPos[0] == 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("Cursor wraps at grid boundary", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	module->xPos[0] = module->usedSize - 1;  // rightmost column

	warmupTimer(module);
	clockEdge(module);

	SECTION("xPos wraps to 0") {
		REQUIRE(module->xPos[0] == 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("Cursor stepping onto ON cell fires trigger and CV outputs", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	// Cursor at (0,0) moving right → next cell is (1,0)
	module->gridSetState(1, 0, GRIDSTATE::ON, 0.5f);  // UNI_3V: rescale(0.5, 0,1, 0,3) = 1.5V

	warmupTimer(module);
	clockEdge(module);

	SECTION("Trigger output fires (10V) when stepping onto ON cell") {
		REQUIRE(module->outputs[MazeMod::TRIG_OUTPUT].getVoltage() == Catch::Approx(10.f));
	}

	SECTION("CV output reflects cell CV in UNI_3V mode") {
		REQUIRE(module->outputs[MazeMod::CV_OUTPUT].getVoltage() == Catch::Approx(1.5f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Cursor stepping onto OFF cell produces no trigger", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	// All cells OFF by default

	warmupTimer(module);
	clockEdge(module);

	SECTION("Trigger output stays at zero for OFF cell") {
		REQUIRE(module->outputs[MazeMod::TRIG_OUTPUT].getVoltage() == Catch::Approx(0.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Turn trigger rotates cursor direction 90 degrees (NINETY mode)", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	// Initial: xDir=1, yDir=0 (right), turnMode=NINETY

	REQUIRE(module->turnMode[0] == TURNMODE::NINETY);

	module->inputs[MazeMod::TURN_INPUT].channels = 1;
	module->inputs[MazeMod::TURN_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(1));
	module->inputs[MazeMod::TURN_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(2));

	SECTION("Direction changes from right (1,0) to down (0,1)") {
		REQUIRE(module->xDir[0] == 0);
		REQUIRE(module->yDir[0] == 1);
	}

	Test::destroyModule(module);
}

TEST_CASE("Turn trigger reverses direction in ONEEIGHTY mode", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	module->turnMode[0] = TURNMODE::ONEEIGHTY;

	module->inputs[MazeMod::TURN_INPUT].channels = 1;
	module->inputs[MazeMod::TURN_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(1));
	module->inputs[MazeMod::TURN_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(2));

	SECTION("Direction reversed: right (1,0) becomes left (-1,0)") {
		REQUIRE(module->xDir[0] == -1);
		REQUIRE(module->yDir[0] == 0);
	}

	Test::destroyModule(module);
}

// Fire a single rising edge on the given input
static void pulse(MazeMod* module, int input, int frame) {
	module->inputs[input].channels = 1;
	module->inputs[input].setVoltage(0.f);
	module->process(Test::makeProcessArgs(frame));
	module->inputs[input].setVoltage(10.f);
	module->process(Test::makeProcessArgs(frame + 1));
	module->inputs[input].setVoltage(0.f);
}

TEST_CASE("Side-shift inputs nudge cursors perpendicular to travel", "[Maze]") {
	// Heading East (xDir=1, yDir=0): SHIFT_R moves +y (down/right of travel),
	// SHIFT_L moves -y (up/left of travel).
	SECTION("SHIFT_R moves the cursor down (right of eastward travel)") {
		auto module = Test::createModule<MazeMod>("Maze");
		module->xPos[0] = 4; module->yPos[0] = 4;
		module->xDir[0] = 1; module->yDir[0] = 0;

		pulse(module, MazeMod::SHIFT_R_INPUT, 10);

		REQUIRE(module->xPos[0] == 4);
		REQUIRE(module->yPos[0] == 5);
		Test::destroyModule(module);
	}

	SECTION("SHIFT_L moves the cursor up (left of eastward travel)") {
		auto module = Test::createModule<MazeMod>("Maze");
		module->xPos[0] = 4; module->yPos[0] = 4;
		module->xDir[0] = 1; module->yDir[0] = 0;

		pulse(module, MazeMod::SHIFT_L_INPUT, 10);

		REQUIRE(module->xPos[0] == 4);
		REQUIRE(module->yPos[0] == 3);
		Test::destroyModule(module);
	}
}

TEST_CASE("Reset input returns cursor to start position", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	module->inputs[MazeMod::RESET_INPUT].channels = 1;
	module->inputs[MazeMod::RESET_INPUT].setVoltage(0.f);

	warmupTimer(module);
	clockEdge(module);
	REQUIRE(module->xPos[0] == 1);

	// Rising edge on reset
	module->inputs[MazeMod::RESET_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(201));
	module->inputs[MazeMod::RESET_INPUT].setVoltage(0.f);

	SECTION("Cursor position returns to xStartPos / yStartPos") {
		REQUIRE(module->xPos[0] == module->xStartPos[0]);
		REQUIRE(module->yPos[0] == module->yStartPos[0]);
	}

	Test::destroyModule(module);
}

TEST_CASE("normalizePorts propagates clock from port 0 to port 1", "[Maze]") {
	auto module = Test::createModule<MazeMod>("Maze");
	REQUIRE(module->normalizePorts == true);
	// CLK port 1 is not connected (channels=0)

	int xBefore1 = module->xPos[1];

	warmupTimer(module);
	clockEdge(module);

	SECTION("Port 0 cursor advances") {
		REQUIRE(module->xPos[0] == 1);
	}

	SECTION("Port 1 cursor advances via clock normalization") {
		REQUIRE(module->xPos[1] == xBefore1 + 1);
	}

	Test::destroyModule(module);
}