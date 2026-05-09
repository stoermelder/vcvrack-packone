#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "Pile.cpp"

using namespace StoermelderPackOne::Pile;

SYNC_MODEL(modelPile, "Pile");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Pile]") {
	PileModule* m = Test::createModule<PileModule>("Pile");
	PileWidget* mw = Test::createWidget<PileWidget>("Pile");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Increment and decrement", "[Pile]") {
	auto module = Test::createModule<PileModule>("Pile");

	SECTION("Increment increases voltage") {
		module->params[PileModule::PARAM_STEP].setValue(1.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f); // No slew
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger LOW-to-HIGH
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		for (int i = 0; i < 40; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->getCurrentVoltage() == 1.0f);
		
		// Reset trigger LOW
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Trigger again
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		for (int i = 0; i < 40; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->getCurrentVoltage() == 2.0f);
	}

	SECTION("Decrement decreases voltage") {
		module->currentVoltage = 5.0f;
		module->slewLimiter.out = 5.0f; // Initialize slew limiter output
		module->params[PileModule::PARAM_STEP].setValue(1.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f); // No slew
		
		module->inputs[PileModule::INPUT_DEC].channels = 1;
		
		// Trigger LOW-to-HIGH
		module->inputs[PileModule::INPUT_DEC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		module->inputs[PileModule::INPUT_DEC].setVoltage(10.0f);
		for (int i = 0; i < 40; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->getCurrentVoltage() == 4.0f);
	}

	SECTION("Multiple increments accumulate") {
		module->params[PileModule::PARAM_STEP].setValue(0.5f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger three times
		for (int t = 0; t < 3; t++) {
			module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
			for (int i = 0; i < 10; i++) {
				module->process(Test::makeProcessArgs(1));
			}
			module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
			for (int i = 0; i < 40; i++) {
				module->process(Test::makeProcessArgs(1));
			}
		}
		
		REQUIRE(module->getCurrentVoltage() == Catch::Approx(1.5f).margin(0.01f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Voltage range clamping", "[Pile]") {
	auto module = Test::createModule<PileModule>("Pile");

	SECTION("UNI_10V range clamps to 0..10V") {
		module->range = RANGE::UNI_10V;
		module->params[PileModule::PARAM_STEP].setValue(1.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger many times to exceed 10V
		for (int t = 0; t < 15; t++) {
			module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
			for (int i = 0; i < 5; i++) {
				module->process(Test::makeProcessArgs(1));
			}
			module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
			for (int i = 0; i < 40; i++) {
				module->process(Test::makeProcessArgs(1));
			}
		}
		
		REQUIRE(module->getCurrentVoltage() <= 10.0f);
		REQUIRE(module->getCurrentVoltage() == 10.0f);
	}

	SECTION("UNI_5V range clamps to 0..5V") {
		module->range = RANGE::UNI_5V;
		module->params[PileModule::PARAM_STEP].setValue(1.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger many times
		for (int t = 0; t < 10; t++) {
			module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
			for (int i = 0; i < 5; i++) {
				module->process(Test::makeProcessArgs(1));
			}
			module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
			for (int i = 0; i < 40; i++) {
				module->process(Test::makeProcessArgs(1));
			}
		}
		
		REQUIRE(module->getCurrentVoltage() <= 5.0f);
		REQUIRE(module->getCurrentVoltage() == 5.0f);
	}

	SECTION("BI_10V range clamps to -10..10V") {
		module->range = RANGE::BI_10V;
		module->params[PileModule::PARAM_STEP].setValue(1.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_DEC].channels = 1;
		
		// Trigger many times to go negative
		for (int t = 0; t < 15; t++) {
			module->inputs[PileModule::INPUT_DEC].setVoltage(0.0f);
			for (int i = 0; i < 5; i++) {
				module->process(Test::makeProcessArgs(1));
			}
			module->inputs[PileModule::INPUT_DEC].setVoltage(10.0f);
			for (int i = 0; i < 40; i++) {
				module->process(Test::makeProcessArgs(1));
			}
		}
		
		REQUIRE(module->getCurrentVoltage() >= -10.0f);
		REQUIRE(module->getCurrentVoltage() == -10.0f);
	}

	SECTION("BI_5V range clamps to -5..5V") {
		module->range = RANGE::BI_5V;
		module->params[PileModule::PARAM_STEP].setValue(1.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_DEC].channels = 1;
		
		// Trigger many times
		for (int t = 0; t < 10; t++) {
			module->inputs[PileModule::INPUT_DEC].setVoltage(0.0f);
			for (int i = 0; i < 5; i++) {
				module->process(Test::makeProcessArgs(1));
			}
			module->inputs[PileModule::INPUT_DEC].setVoltage(10.0f);
			for (int i = 0; i < 40; i++) {
				module->process(Test::makeProcessArgs(1));
			}
		}
		
		REQUIRE(module->getCurrentVoltage() >= -5.0f);
		REQUIRE(module->getCurrentVoltage() == -5.0f);
	}

	SECTION("UNBOUNDED range allows any value") {
		module->range = RANGE::UNBOUNDED;
		module->params[PileModule::PARAM_STEP].setValue(5.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger many times
		for (int t = 0; t < 10; t++) {
			module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
			for (int i = 0; i < 5; i++) {
				module->process(Test::makeProcessArgs(1));
			}
			module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
			for (int i = 0; i < 40; i++) {
				module->process(Test::makeProcessArgs(1));
			}
		}
		
		// Should exceed normal ranges
		REQUIRE(module->getCurrentVoltage() >= 10.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Reset input", "[Pile]") {
	auto module = Test::createModule<PileModule>("Pile");

	SECTION("Reset input sets voltage") {
		module->currentVoltage = 5.0f;
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_RESET].channels = 1;
		module->inputs[PileModule::INPUT_RESET].setVoltage(3.0f);
		
		// Process to apply reset
		for (int i = 0; i < 40; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->getCurrentVoltage() == 3.0f);
	}

	SECTION("Reset voltage changes update current voltage") {
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_RESET].channels = 1;
		module->inputs[PileModule::INPUT_RESET].setVoltage(2.0f);
		
		for (int i = 0; i < 40; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		REQUIRE(module->getCurrentVoltage() == 2.0f);
		
		// Change reset voltage
		module->inputs[PileModule::INPUT_RESET].setVoltage(7.0f);
		
		for (int i = 0; i < 40; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		REQUIRE(module->getCurrentVoltage() == 7.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Slew limiting", "[Pile]") {
	auto module = Test::createModule<PileModule>("Pile");

	SECTION("No slew produces instant changes") {
		module->params[PileModule::PARAM_STEP].setValue(5.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger LOW-to-HIGH
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		for (int i = 0; i < 50; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Output should match internal voltage closely
		float output = module->outputs[PileModule::OUTPUT].getVoltage();
		REQUIRE(output == Catch::Approx(5.0f).margin(0.1f));
	}

	SECTION("High slew produces gradual changes") {
		module->params[PileModule::PARAM_STEP].setValue(5.0f);
		module->params[PileModule::PARAM_SLEW].setValue(5.0f); // Maximum slew (slowest rate)
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// First, process enough samples to trigger processDivider (32) to set slew rate
		for (int i = 0; i < 40; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		float outputInitial = module->outputs[PileModule::OUTPUT].getVoltage();
		
		// Trigger LOW-to-HIGH
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 5; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		
		// Process one cycle and check that it's slewing
		module->process(Test::makeProcessArgs(1));
		float outputAfterOne = module->outputs[PileModule::OUTPUT].getVoltage();
		
		// Process more cycles to see progression
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		float outputAfterTen = module->outputs[PileModule::OUTPUT].getVoltage();
		
		// Should be slewing gradually - each sample should be closer to target
		REQUIRE(outputAfterOne > outputInitial); // Started moving
		REQUIRE(outputAfterTen > outputAfterOne); // Continuing to move
		REQUIRE(outputAfterTen < outputInitial + 5.0f); // But not reached target yet
	}

	SECTION("Slew CV input") {
		module->params[PileModule::PARAM_STEP].setValue(5.0f);
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_SLEW].channels = 1;
		module->inputs[PileModule::INPUT_SLEW].setVoltage(3.0f); // Medium slew via CV
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger LOW-to-HIGH
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		for (int i = 0; i < 50; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// CV should affect slew rate
		float output = module->outputs[PileModule::OUTPUT].getVoltage();
		REQUIRE(output >= 0.0f);
		REQUIRE(output <= 5.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Step size parameter", "[Pile]") {
	auto module = Test::createModule<PileModule>("Pile");

	SECTION("Different step sizes") {
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Test small step
		module->params[PileModule::PARAM_STEP].setValue(0.1f);
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		for (int i = 0; i < 50; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->getCurrentVoltage() == Catch::Approx(0.1f).margin(0.01f));
		
		// Reset trigger LOW
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Test large step
		module->params[PileModule::PARAM_STEP].setValue(5.0f);
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		for (int i = 0; i < 50; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->getCurrentVoltage() == Catch::Approx(5.1f).margin(0.01f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Output voltage", "[Pile]") {
	auto module = Test::createModule<PileModule>("Pile");

	SECTION("Output matches current voltage without slew") {
		module->params[PileModule::PARAM_SLEW].setValue(0.0f);
		module->params[PileModule::PARAM_STEP].setValue(3.0f);
		
		module->inputs[PileModule::INPUT_INC].channels = 1;
		
		// Trigger LOW-to-HIGH
		module->inputs[PileModule::INPUT_INC].setVoltage(0.0f);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		module->inputs[PileModule::INPUT_INC].setVoltage(10.0f);
		for (int i = 0; i < 50; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		float internal = module->getCurrentVoltage();
		float output = module->outputs[PileModule::OUTPUT].getVoltage();
		
		REQUIRE(output == Catch::Approx(internal).margin(0.1f));
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON serialization", "[JSON][Pile]") {
	auto module = Test::createModule<PileModule>("Pile");

	SECTION("Module state is serialized and deserialized") {
		module->panelTheme = 1;
		module->currentVoltage = 7.5f;
		module->range = RANGE::BI_5V;
		
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		
		auto moduleNew = Test::createModule<PileModule>("Pile");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->panelTheme == 1);
		REQUIRE(moduleNew->currentVoltage == Catch::Approx(7.5f).margin(0.01f));
		REQUIRE(moduleNew->range == RANGE::BI_5V);
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}

// Regression test:
// "Removed slew-limiting after preset-load"
//
// Before the fix, dataFromJson() restored currentVoltage but left
// slewLimiter.out at its default value (0 V).  On the very first
// process() call after a patch load, the slew limiter would therefore
// start fading from 0 V toward the restored voltage instead of
// immediately outputting the correct voltage.  The fix adds:
//   slewLimiter.out = currentVoltage;
// inside dataFromJson() so that the slew limiter starts from the
// correct value.
TEST_CASE("No slew applied to output immediately after loading from preset", "[JSON][Pile]") {
	const float TARGET_VOLTAGE = 7.5f;

	auto module = Test::createModule<PileModule>("Pile");

	SECTION("Output equals restored voltage on first process after load, even with max slew") {
		// Save state with a known voltage.
		module->currentVoltage = TARGET_VOLTAGE;
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);

		// Create a new module and set the slew knob to maximum so that
		// any slew limiter offset would be very visible.
		auto moduleNew = Test::createModule<PileModule>("Pile");
		moduleNew->params[PileModule::PARAM_SLEW].setValue(5.0f); // maximum slew

		// Load the preset.  Before the fix, slewLimiter.out would stay at 0
		// so the first output sample would be near 0, not TARGET_VOLTAGE.
		moduleNew->dataFromJson(rootJ);
		json_decref(rootJ);

		// Process a single sample with slew divider reset so the slew rate
		// is applied from the very first step.
		moduleNew->process(Test::makeProcessArgs(1));

		// The output must immediately equal the restored voltage because
		// slewLimiter.out was initialised to currentVoltage in dataFromJson.
		float output = moduleNew->outputs[PileModule::OUTPUT].getVoltage();
		REQUIRE(output == Catch::Approx(TARGET_VOLTAGE).margin(0.01f));

		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}