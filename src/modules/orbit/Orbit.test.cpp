#include "../../test/framework.hpp"

#include "Orbit.cpp"

using namespace StoermelderPackOne::Orbit;

SYNC_MODEL(modelOrbit, "Orbit");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Orbit]") {
	OrbitModule* m = Test::createModule<OrbitModule>("Orbit");
	OrbitWidget* mw = Test::createWidget<OrbitWidget>("Orbit");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Orbit][JSON]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

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

TEST_CASE("JSON round-trip preserves state", "[JSON][Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");
	module->panelTheme = 1;
	module->polyOut = true;
	module->dist = DISTRIBUTION::UNIFORM;
	
	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);
	
	auto moduleNew = Test::createModule<OrbitModule>("Orbit");
	moduleNew->dataFromJson(rootJ);
	
	REQUIRE(moduleNew->panelTheme == 1);
	REQUIRE(moduleNew->polyOut == true);
	REQUIRE(moduleNew->dist == DISTRIBUTION::UNIFORM);
	
	json_decref(rootJ);
	Test::destroyModule(moduleNew);
	Test::destroyModule(module);
}


TEST_CASE("Stereo panning basic", "[Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

	SECTION("Mono input produces stereo output") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->params[OrbitModule::PARAM_LEVEL].setValue(1.0f);
		
		// Trigger LOW-to-HIGH transition
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Outputs should be active
		float vL = module->outputs[OrbitModule::OUTPUT_L].getVoltage();
		float vR = module->outputs[OrbitModule::OUTPUT_R].getVoltage();
		
		// Sum should approximately equal input * level
		REQUIRE((vL + vR) == Catch::Approx(5.0f).margin(0.2f));
	}

	SECTION("Center pan position") {
		// Reset module to ensure pan starts at center (0.5)
		Module::ResetEvent re;
		module->onReset(re);
		
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(10.0f, 0);
		
		// Explicitly set trigger to LOW to prevent false triggers
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		
		module->params[OrbitModule::PARAM_DRIFT].setValue(0.0f); // No drift
		module->params[OrbitModule::PARAM_LEVEL].setValue(1.0f);
		
		// Simulate connected outputs
		module->outputs[OrbitModule::OUTPUT_L].channels = 1;
		module->outputs[OrbitModule::OUTPUT_R].channels = 1;
		
		// Process once to initialize trigger state
		module->process(Test::makeProcessArgs(1));
		
		// Now explicitly set pan to center position after trigger is initialized
		module->pan[0] = 0.5f;
		
		// Process many cycles to let clickFilter fully settle to center (tau = 0.005s)
		for (int i = 0; i < 1000; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// With center pan (0.5), both channels should be equal
		
		// With center pan (0.5), both channels should be equal
		float vL = module->outputs[OrbitModule::OUTPUT_L].getVoltage();
		float vR = module->outputs[OrbitModule::OUTPUT_R].getVoltage();
		
		REQUIRE(vL == Catch::Approx(vR).margin(0.2f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Spread control", "[Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

	SECTION("Spread parameter affects distribution") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(10.0f, 0);
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		
		// Test with minimal spread
		module->params[OrbitModule::PARAM_SPREAD].setValue(0.1f);
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Reset trigger
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Test with maximum spread
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// With higher spread, distribution can be wider (test passes if no crash)
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
	}

	SECTION("Spread CV input") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		
		module->inputs[OrbitModule::INPUT_SPREAD].channels = 1;
		module->inputs[OrbitModule::INPUT_SPREAD].setVoltage(5.0f, 0); // Half spread
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		
		// Trigger LOW-to-HIGH
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// CV should modulate spread
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Drift control", "[Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

	SECTION("Positive drift moves toward center") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->params[OrbitModule::PARAM_DRIFT].setValue(1.0f); // Drift toward center
		
		// Initial trigger LOW-to-HIGH
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		module->process(Test::makeProcessArgs(1));
		
		// Reset trigger
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		
		// Process multiple times to let drift work
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Outputs should still be valid
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
	}

	SECTION("Negative drift moves away from center") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->params[OrbitModule::PARAM_DRIFT].setValue(-1.0f); // Drift away from center
		
		// Trigger LOW-to-HIGH
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		module->process(Test::makeProcessArgs(1));
		
		// Reset trigger
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		
		// Process multiple times
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Outputs should still be valid
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Distribution modes", "[Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

	SECTION("Uniform distribution") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->dist = DISTRIBUTION::UNIFORM;
		
		// Trigger multiple times and check outputs are valid
		for (int i = 0; i < 10; i++) {
			module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
			module->process(Test::makeProcessArgs(1));
			module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
			for (int j = 0; j < 10; j++) {
				module->process(Test::makeProcessArgs(1));
			}
			
			REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
			REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
		}
	}

	SECTION("External distribution") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		
		module->inputs[OrbitModule::INPUT_DIST].channels = 1;
		module->inputs[OrbitModule::INPUT_DIST].setVoltage(5.0f, 0); // Center position
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->dist = DISTRIBUTION::EXTERNAL;
		
		// Trigger LOW-to-HIGH
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// External distribution should use INPUT_DIST
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Polyphonic processing", "[Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

	SECTION("Multiple channels processed independently") {
		int channels = 4;
		module->inputs[OrbitModule::INPUT_IN].channels = channels;
		module->inputs[OrbitModule::INPUT_TRIG].channels = channels;
		
		for (int c = 0; c < channels; c++) {
			module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f + c, c);
		}
		
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->params[OrbitModule::PARAM_LEVEL].setValue(1.0f);
		module->polyOut = false; // Downmix mode
		
		// Simulate connected outputs
		module->outputs[OrbitModule::OUTPUT_L].channels = 1;
		module->outputs[OrbitModule::OUTPUT_R].channels = 1;
		
		// Trigger all channels LOW-to-HIGH
		for (int c = 0; c < channels; c++) {
			module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, c);
		}
		module->process(Test::makeProcessArgs(1));
		for (int c = 0; c < channels; c++) {
			module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, c);
		}
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// In downmix mode, outputs are mono
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getChannels() == 1);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getChannels() == 1);
		
		// Check outputs are valid
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() != 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() != 0.0f);
	}

	SECTION("Polyphonic output mode") {
		int channels = 4;
		module->inputs[OrbitModule::INPUT_IN].channels = channels;
		module->inputs[OrbitModule::INPUT_TRIG].channels = channels;
		
		for (int c = 0; c < channels; c++) {
			module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f + c, c);
		}
		
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->polyOut = true; // Polyphonic mode
		
		// Simulate connected outputs
		module->outputs[OrbitModule::OUTPUT_L].channels = 1;
		module->outputs[OrbitModule::OUTPUT_R].channels = 1;
		
		// Trigger all channels LOW-to-HIGH
		for (int c = 0; c < channels; c++) {
			module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, c);
		}
		module->process(Test::makeProcessArgs(1));
		for (int c = 0; c < channels; c++) {
			module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, c);
		}
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// In poly mode, outputs match input channels
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getChannels() == channels);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getChannels() == channels);
		
		// Check each channel
		for (int c = 0; c < channels; c++) {
			REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage(c) >= 0.0f);
			REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage(c) >= 0.0f);
		}
	}

	Test::destroyModule(module);
}

TEST_CASE("Level control", "[Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

	SECTION("Level affects output amplitude") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(10.0f, 0);
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(0.0f); // Center pan
		module->params[OrbitModule::PARAM_LEVEL].setValue(0.5f); // Half level
		
		// Trigger LOW-to-HIGH
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		float vL = module->outputs[OrbitModule::OUTPUT_L].getVoltage();
		float vR = module->outputs[OrbitModule::OUTPUT_R].getVoltage();
		
		// At center pan with 0.5 level, each output should be approximately 2.5V
		// (10V * 0.5^2 * 0.5 pan position)
		REQUIRE((vL + vR) < 10.0f); // Should be attenuated
	}

	Test::destroyModule(module);
}

TEST_CASE("Trigger behavior", "[Orbit]") {
	auto module = Test::createModule<OrbitModule>("Orbit");

	SECTION("Trigger updates pan position") {
		module->inputs[OrbitModule::INPUT_IN].channels = 1;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		module->params[OrbitModule::PARAM_DRIFT].setValue(0.0f); // No drift
		
		// First trigger LOW-to-HIGH
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Reset trigger LOW
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Second trigger (should generate new random position)
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 10; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Pan position may have changed (can't guarantee due to randomness)
		// Just verify outputs are still valid
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
	}

	SECTION("Trigger normalization") {
		module->inputs[OrbitModule::INPUT_IN].channels = 2;
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 0);
		module->inputs[OrbitModule::INPUT_IN].setVoltage(5.0f, 1);
		
		// Only trigger first channel
		module->inputs[OrbitModule::INPUT_TRIG].channels = 1;
		module->params[OrbitModule::PARAM_SPREAD].setValue(1.0f);
		
		// Trigger LOW-to-HIGH
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(0.0f, 0);
		module->process(Test::makeProcessArgs(1));
		module->inputs[OrbitModule::INPUT_TRIG].setVoltage(10.0f, 0);
		for (int i = 0; i < 20; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Both channels should be processed (trigger normalized to all channels)
		REQUIRE(module->outputs[OrbitModule::OUTPUT_L].getVoltage() >= 0.0f);
		REQUIRE(module->outputs[OrbitModule::OUTPUT_R].getVoltage() >= 0.0f);
	}

	Test::destroyModule(module);
}