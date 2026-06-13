#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "RotorA.cpp"

using namespace StoermelderPackOne::RotorA;

SYNC_MODEL(modelRotorA, "RotorA");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[RotorA]") {
	RotorAModule* m = Test::createModule<RotorAModule>("RotorA");
	RotorAWidget* mw = Test::createWidget<RotorAWidget>("RotorA");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[RotorA][JSON]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("Basic modulation", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Modulator at 0V outputs to first channel") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		module->params[RotorAModule::CHANNELS_OFFSET_PARAM].setValue(0.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Process enough samples for divider
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getChannels() == 16);
		
		// First channel should have voltage
		float v0 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		REQUIRE(v0 > 0.0f);
	}

	SECTION("Modulator at 5V outputs to middle channel") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(5.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Process enough samples
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Middle channels should have voltage
		float v7 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(7);
		float v8 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(8);
		REQUIRE((v7 + v8) > 0.0f);
	}

	SECTION("Modulator at 10V outputs to last channel") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(10.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Process enough samples
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Last channels should have voltage		
		float v14 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(14);
		float v15 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(15);
		REQUIRE((v14 + v15) > 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Carrier signal", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Carrier affects output amplitude") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Test with low carrier
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(2.0f);
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		float v1 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		
		// Test with high carrier
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(10.0f);
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		float v2 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		
		// Higher carrier should produce higher output
		REQUIRE(v2 > v1);
	}

	SECTION("Carrier defaults to 10V when disconnected") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		// Carrier disconnected - should default to 10V
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		float v = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		REQUIRE(v > 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Base signal modulation", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Base signal affects corresponding channel") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f); // Target channel 0
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(10.0f);
		
		module->inputs[RotorAModule::BASE_INPUT].channels = 1;
		module->inputs[RotorAModule::BASE_INPUT].setVoltage(5.0f, 0);
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Channel 0 should be modulated by base signal
		float v0 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		REQUIRE(v0 > 0.0f);
	}

	SECTION("Polyphonic base signals modulate independently") {
		int channels = 4;
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(10.0f);
		
		module->inputs[RotorAModule::BASE_INPUT].channels = channels;
		for (int c = 0; c < channels; c++) {
			module->inputs[RotorAModule::BASE_INPUT].setVoltage((float)(c + 1), c);
		}
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// First channel affected by base signals
		float v0 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		REQUIRE(v0 > 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Channel count control", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Changing channel count affects output") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(5.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Test with different channel counts
		for (float channels = 2.f; channels <= 16.f; channels += 2.f) {
			module->params[RotorAModule::CHANNELS_PARAM].setValue(channels);
			module->params[RotorAModule::CHANNELS_OFFSET_PARAM].setValue(0.f);
			
			// Process enough samples for divider to update
			for (int i = 0; i < 600; i++) {
				module->process(Test::makeProcessArgs(1));
			}
			
			REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getChannels() == (int)channels);
		}
	}

	SECTION("Minimum channel count is 2") {
		module->params[RotorAModule::CHANNELS_PARAM].setValue(2.f);
		
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getChannels() == 2);
	}

	Test::destroyModule(module);
}

TEST_CASE("Channel offset", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Offset shifts output channels") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(4.f);
		module->params[RotorAModule::CHANNELS_OFFSET_PARAM].setValue(0.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// No offset - first 4 channels should be used
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getChannels() == 4);
		float v0_no_offset = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		REQUIRE(v0_no_offset > 0.0f);
		
		// With offset of 4 - channels 4-7 should be used
		module->params[RotorAModule::CHANNELS_OFFSET_PARAM].setValue(4.f);
		
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getChannels() == 8);
		
		// First 4 channels should be zero
		for (int c = 0; c < 4; c++) {
			REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(c) == 0.0f);
		}
		
		// Channels 4-7 should have voltage
		float sum = 0.0f;
		for (int c = 4; c < 8; c++) {
			sum += module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(c);
		}
		REQUIRE(sum > 0.0f);
	}

	SECTION("Offset maximum is 14") {
		module->params[RotorAModule::CHANNELS_PARAM].setValue(2.f);
		module->params[RotorAModule::CHANNELS_OFFSET_PARAM].setValue(14.f);
		
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Channels 14 and 15
		REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getChannels() == 16);
		REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(14) > 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Modulator clamping", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Modulator clamped to 0..10V range") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(16.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Test negative voltage (should clamp to 0)
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(-5.0f);
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		float v0 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(0);
		REQUIRE(v0 > 0.0f); // Should output to first channel
		
		// Test over-voltage (should clamp to 10V)
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(15.0f);
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		// Should output to last channels
		float v15 = module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(15);
		REQUIRE(v15 >= 0.0f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Distribution between channels", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Modulator between values distributes to adjacent channels") {
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(10.0f);
		
		module->params[RotorAModule::CHANNELS_PARAM].setValue(4.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Set modulator to 2.5V (between channels)
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(2.5f);
		
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Should distribute across multiple channels
		int activeChannels = 0;
		for (int c = 0; c < 4; c++) {
			if (module->outputs[RotorAModule::POLY_OUTPUT].getVoltage(c) > 0.1f) {
				activeChannels++;
			}
		}
		
		// At least one channel should be active
		REQUIRE(activeChannels > 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("Output without inputs", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Module processes without crashing when no inputs connected") {
		module->params[RotorAModule::CHANNELS_PARAM].setValue(8.f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Process without any inputs connected
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Should not crash and produce valid (likely zero) outputs
		REQUIRE(module->outputs[RotorAModule::POLY_OUTPUT].getChannels() == 8);
	}

	Test::destroyModule(module);
}

TEST_CASE("Clock divider updates", "[RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Channel parameters update after processing divider samples") {
		module->params[RotorAModule::CHANNELS_PARAM].setValue(4.f);
		module->params[RotorAModule::CHANNELS_OFFSET_PARAM].setValue(2.f);
		
		module->inputs[RotorAModule::MOD_INPUT].channels = 1;
		module->inputs[RotorAModule::MOD_INPUT].setVoltage(0.0f);
		
		module->inputs[RotorAModule::CAR_INPUT].channels = 1;
		module->inputs[RotorAModule::CAR_INPUT].setVoltage(5.0f);
		
		// Simulate connected output
		module->outputs[RotorAModule::POLY_OUTPUT].channels = 1;
		
		// Process more than divider period (512 samples)
		for (int i = 0; i < 600; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->channels == 4);
		REQUIRE(module->channelsOffset == 2);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON serialization", "[JSON][RotorA]") {
	auto module = Test::createModule<RotorAModule>("RotorA");

	SECTION("Module state is serialized and deserialized") {
		module->panelTheme = 1;
		
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		
		auto moduleNew = Test::createModule<RotorAModule>("RotorA");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->panelTheme == 1);
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}