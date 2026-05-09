#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "Hive.cpp"

using namespace StoermelderPackOne::Hive;

using HiveMod = HiveModule<MAX_RADIUS, 4>;

SYNC_MODEL(modelHive, "Hive");
Test::TestContext<> testContext;

// Warm up the reset timer so the clock trigger guard (>= 1ms) is satisfied
static void warmupTimer(HiveMod* module, int samples = 100) {
	module->inputs[HiveMod::CLK_INPUT].channels = 1;
	module->inputs[HiveMod::CLK_INPUT].setVoltage(0.f);
	for (int i = 0; i < samples; i++) {
		module->process(Test::makeProcessArgs(i));
	}
}

// Fire a single clock rising edge on port 0
static void clockEdge(HiveMod* module, int frame = 200) {
	module->inputs[HiveMod::CLK_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(frame));
	module->inputs[HiveMod::CLK_INPUT].setVoltage(0.f);
}

TEST_CASE("Construction and initialization", "[Hive]") {
	HiveMod* module = Test::createModule<HiveMod>("Hive");
	HiveWidget* mw = Test::createWidget<HiveWidget>(module);

	Test::destroyWidget(mw);
	Test::destroyModule(module);
}

TEST_CASE("Reset clears grid and restores cursor defaults", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");

	// Dirty up state
	module->grid.cursor[0].pos.q = 2;
	module->grid.cursor[0].pos.r = 3;
	module->grid.cursor[0].dir = DIRECTION::SW;
	module->grid.cursor[0].turnMode = TURNMODE::ONEEIGHTY;

	// Set a cell to ON then reset
	HiveCell cell;
	cell.pos = RoundAxialVec(0, 0);
	cell.state = GRIDSTATE::ON;
	cell.cv = 0.5f;
	module->grid.setCell(cell);

	Module::ResetEvent re;
	module->onReset(re);

	SECTION("Cursor 0 position reset to SW edge") {
		REQUIRE(module->grid.cursor[0].pos.q == -module->grid.usedRadius);
		REQUIRE(module->grid.cursor[0].pos.r == 0);
	}

	SECTION("Cursor 0 direction reset to NE") {
		REQUIRE(module->grid.cursor[0].dir == DIRECTION::NE);
	}

	SECTION("Turn mode reset to SIXTY") {
		REQUIRE(module->grid.cursor[0].turnMode == TURNMODE::SIXTY);
	}

	SECTION("Cell at (0,0) cleared to OFF") {
		REQUIRE(module->grid.getCell(RoundAxialVec(0, 0)).state == GRIDSTATE::OFF);
	}

	Test::destroyModule(module);
}

TEST_CASE("gridClear sets all visible cells to OFF with zero CV", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");

	// Set a visible cell to ON
	HiveCell cell;
	cell.pos = RoundAxialVec(1, 2);
	cell.state = GRIDSTATE::ON;
	cell.cv = 0.7f;
	module->grid.setCell(cell);

	REQUIRE(module->grid.getCell(RoundAxialVec(1, 2)).state == GRIDSTATE::ON);

	module->gridClear();

	SECTION("Previously ON cell is now OFF") {
		REQUIRE(module->grid.getCell(RoundAxialVec(1, 2)).state == GRIDSTATE::OFF);
	}

	SECTION("Center cell (0,0) is OFF after clear") {
		REQUIRE(module->grid.getCell(RoundAxialVec(0, 0)).state == GRIDSTATE::OFF);
	}

	Test::destroyModule(module);
}

TEST_CASE("Clock input advances cursor position (NE direction)", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	// Initial: cursor[0] at (-4, 0), dir=NE
	// After one NE move (POINTY): q += 1, r -= 1 → (-3, -1)

	REQUIRE(module->grid.cursor[0].pos.q == -4);
	REQUIRE(module->grid.cursor[0].pos.r == 0);

	warmupTimer(module);
	clockEdge(module);

	SECTION("Cursor moves NE: q+1, r-1") {
		REQUIRE(module->grid.cursor[0].pos.q == -3);
		REQUIRE(module->grid.cursor[0].pos.r == -1);
	}

	Test::destroyModule(module);
}

TEST_CASE("Cursor stepping onto ON cell fires trigger and CV outputs", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	// Cursor starts at (-4, 0), NE → will step to (-3, -1)
	HiveCell cell;
	cell.pos = RoundAxialVec(-3, -1);
	cell.state = GRIDSTATE::ON;
	cell.cv = 0.5f;  // UNI_3V: rescale(0.5, 0,1, 0,3) = 1.5V
	module->grid.setCell(cell);

	warmupTimer(module);
	clockEdge(module);

	SECTION("Trigger output fires (10V) on ON cell") {
		REQUIRE(module->outputs[HiveMod::TRIG_OUTPUT].getVoltage() == Approx(10.f));
	}

	SECTION("CV output reflects cell CV in UNI_3V mode") {
		REQUIRE(module->outputs[HiveMod::CV_OUTPUT].getVoltage() == Approx(1.5f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Cursor stepping onto OFF cell produces no trigger", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	// All cells OFF by default

	warmupTimer(module);
	clockEdge(module);

	SECTION("Trigger output stays at zero for OFF cell") {
		REQUIRE(module->outputs[HiveMod::TRIG_OUTPUT].getVoltage() == Approx(0.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Turn trigger rotates cursor direction in SIXTY mode", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	// Initial: dir = NE = 1, turnMode = SIXTY
	// SIXTY turn: dir = (1 + 2) % 12 = 3 = E

	REQUIRE(module->grid.cursor[0].dir == DIRECTION::NE);
	REQUIRE(module->grid.cursor[0].turnMode == TURNMODE::SIXTY);

	module->inputs[HiveMod::TURN_INPUT].channels = 1;
	module->inputs[HiveMod::TURN_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(1));
	module->inputs[HiveMod::TURN_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(2));

	SECTION("Direction rotated: NE(1) becomes E(3)") {
		REQUIRE(module->grid.cursor[0].dir == DIRECTION::E);
	}

	Test::destroyModule(module);
}

TEST_CASE("Turn trigger rotates direction in ONETWENTY mode", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	module->grid.cursor[0].turnMode = TURNMODE::ONETWENTY;
	// ONETWENTY: dir = (NE=1 + 4) % 12 = 5 = SE

	module->inputs[HiveMod::TURN_INPUT].channels = 1;
	module->inputs[HiveMod::TURN_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(1));
	module->inputs[HiveMod::TURN_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(2));

	SECTION("Direction rotated: NE(1) becomes SE(5)") {
		REQUIRE(module->grid.cursor[0].dir == DIRECTION::SE);
	}

	Test::destroyModule(module);
}

TEST_CASE("Reset input returns cursor to start position", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	module->inputs[HiveMod::RESET_INPUT].channels = 1;
	module->inputs[HiveMod::RESET_INPUT].setVoltage(0.f);

	warmupTimer(module);
	clockEdge(module);
	REQUIRE(module->grid.cursor[0].pos.q == -3);

	// Rising edge on reset
	module->inputs[HiveMod::RESET_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(201));
	module->inputs[HiveMod::RESET_INPUT].setVoltage(0.f);

	SECTION("Cursor position returns to startPos") {
		REQUIRE(module->grid.cursor[0].pos.q == module->grid.cursor[0].startPos.q);
		REQUIRE(module->grid.cursor[0].pos.r == module->grid.cursor[0].startPos.r);
	}

	Test::destroyModule(module);
}

TEST_CASE("normalizePorts propagates clock from port 0 to port 1", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	REQUIRE(module->normalizePorts == true);
	// CLK port 1 not connected (channels=0)

	int qBefore1 = module->grid.cursor[1].pos.q;

	warmupTimer(module);
	clockEdge(module);

	SECTION("Port 0 cursor advances") {
		REQUIRE(module->grid.cursor[0].pos.q == -3);
	}

	SECTION("Port 1 cursor also advances via clock normalization") {
		REQUIRE(module->grid.cursor[1].pos.q != qBefore1);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves grid state and cursor configuration", "[JSON][Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");

	// Set cell and cursor state
	HiveCell cell;
	cell.pos = RoundAxialVec(2, -1);
	cell.state = GRIDSTATE::ON;
	cell.cv = 0.4f;
	module->grid.setCell(cell);

	module->grid.cursor[0].dir = DIRECTION::SE;
	module->grid.cursor[0].turnMode = TURNMODE::ONETWENTY;
	module->grid.cursor[0].outMode = OUTMODE::BI_5V;
	module->grid.cursor[1].pos.q = -2;
	module->grid.cursor[1].pos.r = 1;
	module->normalizePorts = false;

	json_t* j = module->dataToJson();

	auto module2 = Test::createModule<HiveMod>("Hive");
	module2->dataFromJson(j);
	json_decref(j);

	SECTION("Grid cell state and CV preserved") {
		HiveCell c = module2->grid.getCell(RoundAxialVec(2, -1));
		REQUIRE(c.state == GRIDSTATE::ON);
		REQUIRE(c.cv == Approx(0.4f));
	}

	SECTION("Cursor 0 direction preserved") {
		REQUIRE(module2->grid.cursor[0].dir == DIRECTION::SE);
	}

	SECTION("Cursor 0 turnMode preserved") {
		REQUIRE(module2->grid.cursor[0].turnMode == TURNMODE::ONETWENTY);
	}

	SECTION("Cursor 0 outMode preserved") {
		REQUIRE(module2->grid.cursor[0].outMode == OUTMODE::BI_5V);
	}

	SECTION("Cursor 1 position preserved") {
		REQUIRE(module2->grid.cursor[1].pos.q == -2);
		REQUIRE(module2->grid.cursor[1].pos.r == 1);
	}

	SECTION("normalizePorts preserved") {
		REQUIRE(module2->normalizePorts == false);
	}

	Test::destroyModule(module);
	Test::destroyModule(module2);
}
