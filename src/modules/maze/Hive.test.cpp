#include "../../test/framework.hpp"

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
	HiveMod* m = Test::createModule<HiveMod>("Hive");
	HiveWidget* mw = Test::createWidget<HiveWidget>("Hive");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Hive][JSON]") {
	auto module = Test::createModule<HiveMod>("Hive");

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

TEST_CASE("JSON round-trip preserves state", "[JSON][Hive]") {
	HiveMod* m = Test::createModule<HiveMod>("Hive");
	HiveMod* m2 = Test::createModule<HiveMod>("Hive");

	SECTION("Scalar settings round-trip") {
		// Non-default values (defaults: panelTheme 0, usedRadius 4, sizeFactor computed, normalizePorts true)
		m->panelTheme = 1;
		m->grid.usedRadius = 8;
		m->sizeFactor = 1.5f;
		m->normalizePorts = false;

		json_t* j = m->dataToJson();
		// Start m2 at defaults so dataFromJson() is genuinely exercised
		m2->panelTheme = 0;
		m2->grid.usedRadius = 4;
		m2->sizeFactor = 0.f;
		m2->normalizePorts = true;
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->grid.usedRadius == 8);
		REQUIRE(m2->sizeFactor == Catch::Approx(1.5f));
		REQUIRE(m2->normalizePorts == false);
	}

	SECTION("Grid arrays (grid/gridCv) round-trip") {
		HiveCell c1; c1.pos = RoundAxialVec(2, -1); c1.state = GRIDSTATE::ON; c1.cv = 0.4f;
		HiveCell c2; c2.pos = RoundAxialVec(-3, -1); c2.state = GRIDSTATE::RANDOM; c2.cv = 0.3f;
		HiveCell c3; c3.pos = RoundAxialVec(0, 0); c3.state = GRIDSTATE::ON; c3.cv = 0.9f;
		m->grid.setCell(c1);
		m->grid.setCell(c2);
		m->grid.setCell(c3);

		json_t* j = m->dataToJson();
		// The grid arrays must be serialized as flat arraySize*arraySize elements
		json_t* gridJ = json_object_get(j, "grid");
		json_t* gridCvJ = json_object_get(j, "gridCv");
		REQUIRE(gridJ != nullptr);
		REQUIRE(gridCvJ != nullptr);
		REQUIRE(json_array_size(gridJ) == (size_t)(m->grid.arraySize * m->grid.arraySize));
		REQUIRE(json_array_size(gridCvJ) == (size_t)(m->grid.arraySize * m->grid.arraySize));
		m2->dataFromJson(j);
		json_decref(j);

		HiveCell r1 = m2->grid.getCell(RoundAxialVec(2, -1));
		REQUIRE(r1.state == GRIDSTATE::ON);
		REQUIRE(r1.cv == Catch::Approx(0.4f));
		HiveCell r2 = m2->grid.getCell(RoundAxialVec(-3, -1));
		REQUIRE(r2.state == GRIDSTATE::RANDOM);
		REQUIRE(r2.cv == Catch::Approx(0.3f));
		HiveCell r3 = m2->grid.getCell(RoundAxialVec(0, 0));
		REQUIRE(r3.state == GRIDSTATE::ON);
		REQUIRE(r3.cv == Catch::Approx(0.9f));
	}

	SECTION("Mirror centers (mirrorCenters array) round-trip") {
		m->grid.mirrorCenters[0] = CubeVec(1, 2, 3);
		m->grid.mirrorCenters[1] = CubeVec(4, 5, 6);
		m->grid.mirrorCenters[2] = CubeVec(7, -8, 9);
		m->grid.mirrorCenters[3] = CubeVec(-1, -2, -3);
		m->grid.mirrorCenters[4] = CubeVec(10, 11, 12);
		m->grid.mirrorCenters[5] = CubeVec(-4, -5, -6);

		json_t* j = m->dataToJson();
		// The mirrorCenters array must be serialized with one entry per mirror
		json_t* mirrorsJ = json_object_get(j, "mirrorCenters");
		REQUIRE(mirrorsJ != nullptr);
		REQUIRE(json_array_size(mirrorsJ) == (size_t) 6);
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->grid.mirrorCenters[0].x == 1.f);
		REQUIRE(m2->grid.mirrorCenters[0].y == 2.f);
		REQUIRE(m2->grid.mirrorCenters[0].z == 3.f);
		REQUIRE(m2->grid.mirrorCenters[2].x == 7.f);
		REQUIRE(m2->grid.mirrorCenters[2].y == -8.f);
		REQUIRE(m2->grid.mirrorCenters[2].z == 9.f);
		REQUIRE(m2->grid.mirrorCenters[5].x == -4.f);
		REQUIRE(m2->grid.mirrorCenters[5].y == -5.f);
		REQUIRE(m2->grid.mirrorCenters[5].z == -6.f);
	}

	SECTION("Ports array round-trip") {
		// Distinctive per-port start/direction/position and mode settings
		m->grid.cursor[0].startPos.q = 1; m->grid.cursor[0].startPos.r = 2;
		m->grid.cursor[0].startDir = DIRECTION::E;
		m->grid.cursor[0].pos.q = 3; m->grid.cursor[0].pos.r = 4;
		m->grid.cursor[0].dir = DIRECTION::SE;
		m->grid.cursor[0].turnMode = TURNMODE::ONETWENTY;
		m->grid.cursor[0].ninetyState = TURNMODE::NINETY;
		m->grid.cursor[0].outMode = OUTMODE::BI_5V;
		m->grid.cursor[0].ratchetingEnabled = RATCHETMODE::MULT_TWO;
		m->ratchetingSetProb(0, 0.7f);

		m->grid.cursor[2].startPos.q = 5; m->grid.cursor[2].startPos.r = 6;
		m->grid.cursor[2].startDir = DIRECTION::W;
		m->grid.cursor[2].pos.q = 7; m->grid.cursor[2].pos.r = 8;
		m->grid.cursor[2].dir = DIRECTION::SW;
		m->grid.cursor[2].turnMode = TURNMODE::ONEEIGHTY;
		m->grid.cursor[2].ninetyState = TURNMODE::SIXTY;
		m->grid.cursor[2].outMode = OUTMODE::UNI_1V;
		m->grid.cursor[2].ratchetingEnabled = RATCHETMODE::POWER_TWO;
		m->ratchetingSetProb(2, 0.2f);

		json_t* j = m->dataToJson();
		// The ports array must be serialized with one entry per port
		json_t* portsJ = json_object_get(j, "ports");
		REQUIRE(portsJ != nullptr);
		REQUIRE(json_array_size(portsJ) == (size_t) 4);
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->grid.cursor[0].startPos.q == 1);
		REQUIRE(m2->grid.cursor[0].startPos.r == 2);
		REQUIRE(m2->grid.cursor[0].startDir == DIRECTION::E);
		REQUIRE(m2->grid.cursor[0].pos.q == 3);
		REQUIRE(m2->grid.cursor[0].pos.r == 4);
		REQUIRE(m2->grid.cursor[0].dir == DIRECTION::SE);
		REQUIRE(m2->grid.cursor[0].turnMode == TURNMODE::ONETWENTY);
		REQUIRE(m2->grid.cursor[0].ninetyState == TURNMODE::NINETY);
		REQUIRE(m2->grid.cursor[0].outMode == OUTMODE::BI_5V);
		REQUIRE(m2->grid.cursor[0].ratchetingEnabled == RATCHETMODE::MULT_TWO);
		REQUIRE(m2->grid.cursor[0].ratchetingProb == Catch::Approx(0.7f));

		REQUIRE(m2->grid.cursor[2].startPos.q == 5);
		REQUIRE(m2->grid.cursor[2].startPos.r == 6);
		REQUIRE(m2->grid.cursor[2].startDir == DIRECTION::W);
		REQUIRE(m2->grid.cursor[2].pos.q == 7);
		REQUIRE(m2->grid.cursor[2].pos.r == 8);
		REQUIRE(m2->grid.cursor[2].dir == DIRECTION::SW);
		REQUIRE(m2->grid.cursor[2].turnMode == TURNMODE::ONEEIGHTY);
		REQUIRE(m2->grid.cursor[2].ninetyState == TURNMODE::SIXTY);
		REQUIRE(m2->grid.cursor[2].outMode == OUTMODE::UNI_1V);
		REQUIRE(m2->grid.cursor[2].ratchetingEnabled == RATCHETMODE::POWER_TWO);
		REQUIRE(m2->grid.cursor[2].ratchetingProb == Catch::Approx(0.2f));
	}

	Test::destroyModule(m);
	Test::destroyModule(m2);
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
		REQUIRE(module->outputs[HiveMod::TRIG_OUTPUT].getVoltage() == Catch::Approx(10.f));
	}

	SECTION("CV output reflects cell CV in UNI_3V mode") {
		REQUIRE(module->outputs[HiveMod::CV_OUTPUT].getVoltage() == Catch::Approx(1.5f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Cursor stepping onto OFF cell produces no trigger", "[Hive]") {
	auto module = Test::createModule<HiveMod>("Hive");
	// All cells OFF by default

	warmupTimer(module);
	clockEdge(module);

	SECTION("Trigger output stays at zero for OFF cell") {
		REQUIRE(module->outputs[HiveMod::TRIG_OUTPUT].getVoltage() == Catch::Approx(0.f));
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

// Fire a single rising edge on the given input
static void pulse(HiveMod* module, int input, int frame) {
	module->inputs[input].channels = 1;
	module->inputs[input].setVoltage(0.f);
	module->process(Test::makeProcessArgs(frame));
	module->inputs[input].setVoltage(10.f);
	module->process(Test::makeProcessArgs(frame + 1));
	module->inputs[input].setVoltage(0.f);
}

// Shift a fresh module from the centre cell via the given input, return resulting pos
static RoundAxialVec shiftFromCentre(int input) {
	auto module = Test::createModule<HiveMod>("Hive");
	module->grid.cursor[0].pos = RoundAxialVec(0, 0);
	pulse(module, input, 10);
	RoundAxialVec p = module->grid.cursor[0].pos;
	Test::destroyModule(module);
	return p;
}

TEST_CASE("All four side-shift inputs move the cursor", "[Hive]") {
	// Regression: SHIFT_L2 was wired to the SHIFT_L1 trigger, leaving the
	// "shift left down" jack dead. Each jack must move the cursor, and the two
	// left jacks (and the two right jacks) must reach distinct cells.
	RoundAxialVec centre(0, 0);

	RoundAxialVec r1 = shiftFromCentre(HiveMod::SHIFT_R1_INPUT);
	RoundAxialVec r2 = shiftFromCentre(HiveMod::SHIFT_R2_INPUT);
	RoundAxialVec l1 = shiftFromCentre(HiveMod::SHIFT_L1_INPUT);
	RoundAxialVec l2 = shiftFromCentre(HiveMod::SHIFT_L2_INPUT);

	SECTION("SHIFT_R1 moves the cursor") {
		REQUIRE_FALSE((r1.q == centre.q && r1.r == centre.r));
	}
	SECTION("SHIFT_R2 moves the cursor") {
		REQUIRE_FALSE((r2.q == centre.q && r2.r == centre.r));
	}
	SECTION("SHIFT_L1 moves the cursor") {
		REQUIRE_FALSE((l1.q == centre.q && l1.r == centre.r));
	}
	SECTION("SHIFT_L2 moves the cursor (regression: was dead)") {
		REQUIRE_FALSE((l2.q == centre.q && l2.r == centre.r));
	}
	SECTION("The two left jacks reach distinct cells") {
		REQUIRE_FALSE((l1.q == l2.q && l1.r == l2.r));
	}
	SECTION("The two right jacks reach distinct cells") {
		REQUIRE_FALSE((r1.q == r2.q && r1.r == r2.r));
	}
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