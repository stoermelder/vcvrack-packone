#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "FourRounds.cpp"

using namespace StoermelderPackOne::FourRounds;

SYNC_MODEL(modelFourRounds, "FourRounds");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[FourRounds]") {
	FourRoundsModule* m = Test::createModule<FourRoundsModule>("FourRounds");
	FourRoundsWidget* mw = Test::createWidget<FourRoundsWidget>("FourRounds");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[FourRounds][JSON]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("DIRECT mode routes winner through the bracket", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	module->mode = MODE::DIRECT;

	// state==0 makes even-indexed inputs win
	for (int i = 0; i < FourRoundsModule::SIZE; i++) {
		module->state[i] = 0.f;
	}

	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].channels = 1;
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage((i % 2 == 0) ? 5.f : 1.f);
	}

	module->process(Test::makeProcessArgs(1));

	SECTION("Round-2 outputs carry the winning voltage") {
		for (int i = 0; i < 8; i++) {
			REQUIRE(module->outputs[FourRoundsModule::ROUND2_OUTPUT + i].getVoltage() == Catch::Approx(5.f));
		}
	}

	SECTION("Winner output carries the winning voltage") {
		REQUIRE(module->outputs[FourRoundsModule::WINNER_OUTPUT].getVoltage() == Catch::Approx(5.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("DIRECT mode state=1 selects right input", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	module->mode = MODE::DIRECT;

	// state==1 makes odd-indexed inputs win
	for (int i = 0; i < FourRoundsModule::SIZE; i++) {
		module->state[i] = 1.f;
	}

	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].channels = 1;
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage((i % 2 == 0) ? 1.f : 7.f);
	}

	module->process(Test::makeProcessArgs(1));

	SECTION("Round-2 outputs carry the odd-input voltage") {
		for (int i = 0; i < 8; i++) {
			REQUIRE(module->outputs[FourRoundsModule::ROUND2_OUTPUT + i].getVoltage() == Catch::Approx(7.f));
		}
	}

	SECTION("Winner output carries the odd-input voltage") {
		REQUIRE(module->outputs[FourRoundsModule::WINNER_OUTPUT].getVoltage() == Catch::Approx(7.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Inverted flag swaps winner selection", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	module->mode = MODE::DIRECT;

	// state=0 normally selects even inputs; with inverted it should select odd inputs
	for (int i = 0; i < FourRoundsModule::SIZE; i++) {
		module->state[i] = 0.f;
	}

	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].channels = 1;
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage((i % 2 == 0) ? 2.f : 9.f);
	}

	module->inverted = true;
	module->process(Test::makeProcessArgs(1));

	SECTION("Winner is the odd-side input when inverted") {
		REQUIRE(module->outputs[FourRoundsModule::WINNER_OUTPUT].getVoltage() == Catch::Approx(9.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("INV trigger toggles inverted flag", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	REQUIRE(module->inverted == false);

	module->inputs[FourRoundsModule::INV_INPUT].channels = 1;

	// Rising edge toggles the flag
	module->inputs[FourRoundsModule::INV_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(1));

	module->inputs[FourRoundsModule::INV_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(2));
	REQUIRE(module->inverted == true);

	// Second edge toggles back
	module->inputs[FourRoundsModule::INV_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(3));
	module->inputs[FourRoundsModule::INV_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(4));
	REQUIRE(module->inverted == false);

	Test::destroyModule(module);
}

TEST_CASE("TRIG input captures lastValue in SH mode", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	module->mode = MODE::SH;

	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].channels = 1;
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage(float(i + 1));
	}

	module->inputs[FourRoundsModule::TRIG_INPUT].channels = 1;

	// Low before trigger
	module->inputs[FourRoundsModule::TRIG_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(1));

	// Rising edge captures input voltages
	module->inputs[FourRoundsModule::TRIG_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(2));

	SECTION("lastValue[] stores the sampled input voltages") {
		for (int i = 0; i < 16; i++) {
			REQUIRE(module->lastValue[i] == Catch::Approx(float(i + 1)));
		}
	}

	Test::destroyModule(module);
}

TEST_CASE("SH mode holds sampled voltages after input changes", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	module->mode = MODE::SH;

	// Set initial voltages
	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].channels = 1;
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage((i % 2 == 0) ? 4.f : 0.f);
	}

	// Trigger capture with low-high-low cycle
	module->inputs[FourRoundsModule::TRIG_INPUT].channels = 1;
	module->inputs[FourRoundsModule::TRIG_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(1));
	module->inputs[FourRoundsModule::TRIG_INPUT].setVoltage(10.f);
	module->process(Test::makeProcessArgs(2));
	module->inputs[FourRoundsModule::TRIG_INPUT].setVoltage(0.f);
	module->process(Test::makeProcessArgs(3));

	// Verify trigger captured the values into lastValue
	REQUIRE(module->lastValue[0] == 4.f);
	REQUIRE(module->lastValue[1] == 0.f);
	REQUIRE(module->lastValue[2] == 4.f);

	// Explicitly set state to force left-branch winners (state=0 means s=0)
	for (int i = 0; i < FourRoundsModule::SIZE; i++) {
		module->state[i] = 0.f;
	}

	// Process once more with state locked to left-branch
	module->process(Test::makeProcessArgs(4));

	// Change live inputs — SH output should be unchanged
	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage(0.f);
	}
	module->process(Test::makeProcessArgs(5));

	SECTION("Winner reflects sampled voltage, not current input") {
		REQUIRE(module->outputs[FourRoundsModule::WINNER_OUTPUT].getVoltage() == Catch::Approx(4.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("QUANTUM mode blends inputs by state weight", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	module->mode = MODE::QUANTUM;

	// state=0.5 means equal blend of both inputs
	for (int i = 0; i < FourRoundsModule::SIZE; i++) {
		module->state[i] = 0.5f;
	}

	// even=4V, odd=8V => blend = 6V
	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].channels = 1;
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage((i % 2 == 0) ? 4.f : 8.f);
	}

	module->process(Test::makeProcessArgs(1));

	SECTION("Round-2 outputs are the blended voltage") {
		for (int i = 0; i < 8; i++) {
			REQUIRE(module->outputs[FourRoundsModule::ROUND2_OUTPUT + i].getVoltage() == Catch::Approx(6.f));
		}
	}

	SECTION("Winner output is the blended voltage") {
		REQUIRE(module->outputs[FourRoundsModule::WINNER_OUTPUT].getVoltage() == Catch::Approx(6.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("QUANTUM mode state=0 passes first input unchanged", "[FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");
	module->mode = MODE::QUANTUM;

	for (int i = 0; i < FourRoundsModule::SIZE; i++) {
		module->state[i] = 0.f;
	}

	for (int i = 0; i < 16; i++) {
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].channels = 1;
		module->inputs[FourRoundsModule::ROUND1_INPUT + i].setVoltage((i % 2 == 0) ? 3.f : 7.f);
	}

	module->process(Test::makeProcessArgs(1));

	SECTION("Winner equals first-input voltage") {
		REQUIRE(module->outputs[FourRoundsModule::WINNER_OUTPUT].getVoltage() == Catch::Approx(3.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves state", "[JSON][FourRounds]") {
	auto module = Test::createModule<FourRoundsModule>("FourRounds");

	// Set known state
	module->mode = MODE::SH;
	module->inverted = true;
	for (int i = 0; i < FourRoundsModule::SIZE; i++) {
		module->state[i] = (i % 2 == 0) ? 0.f : 1.f;
	}
	for (int i = 0; i < 16; i++) {
		module->lastValue[i] = float(i) * 0.5f;
	}

	json_t* j = module->dataToJson();

	// Restore into fresh module
	auto module2 = Test::createModule<FourRoundsModule>("FourRounds");
	module2->dataFromJson(j);
	json_decref(j);

	SECTION("Mode is preserved") {
		REQUIRE(module2->mode == MODE::SH);
	}

	SECTION("Inverted flag is preserved") {
		REQUIRE(module2->inverted == true);
	}

	SECTION("State array is preserved") {
		for (int i = 0; i < FourRoundsModule::SIZE; i++) {
			REQUIRE(module2->state[i] == Catch::Approx((i % 2 == 0) ? 0.f : 1.f));
		}
	}

	SECTION("lastValue array is preserved") {
		for (int i = 0; i < 16; i++) {
			REQUIRE(module2->lastValue[i] == Catch::Approx(float(i) * 0.5f));
		}
	}

	Test::destroyModule(module);
	Test::destroyModule(module2);
}