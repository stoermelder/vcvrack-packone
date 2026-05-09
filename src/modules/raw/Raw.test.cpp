#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "Raw.cpp"

using namespace StoermelderPackOne::Raw;

SYNC_MODEL(modelRaw, "Raw");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Raw]") {
	RawModule* module = Test::createModule<RawModule>("Raw");
	RawWidget* mw = Test::createWidget<RawWidget>(module);

	Test::destroyWidget(mw);
	Test::destroyModule(module);
}

TEST_CASE("Reset clears internal delay buffers", "[Raw]") {
	auto module = Test::createModule<RawModule>("Raw");

	// Drive the module briefly
	module->inputs[RawModule::INPUT].channels = 1;
	module->inputs[RawModule::INPUT].setVoltage(5.f);

	for (int i = 0; i < 200; i++) {
		module->process(Test::makeProcessArgs(i));
	}

	Module::ResetEvent re;
	module->onReset(re);

	SECTION("x delay buffers zeroed") {
		for (int c = 0; c < 4; c++) {
			for (int d = 0; d < 3; d++) {
				simd::float_4 v = module->x[c][d];
				REQUIRE(v[0] == Approx(0.f));
				REQUIRE(v[1] == Approx(0.f));
				REQUIRE(v[2] == Approx(0.f));
				REQUIRE(v[3] == Approx(0.f));
			}
		}
	}

	SECTION("y delay buffers zeroed") {
		for (int c = 0; c < 4; c++) {
			for (int d = 0; d < 2; d++) {
				simd::float_4 v = module->y[c][d];
				REQUIRE(v[0] == Approx(0.f));
				REQUIRE(v[1] == Approx(0.f));
				REQUIRE(v[2] == Approx(0.f));
				REQUIRE(v[3] == Approx(0.f));
			}
		}
	}

	Test::destroyModule(module);
}

TEST_CASE("Zero input produces zero output", "[Raw]") {
	auto module = Test::createModule<RawModule>("Raw");

	module->inputs[RawModule::INPUT].channels = 1;
	module->inputs[RawModule::INPUT].setVoltage(0.f);

	for (int i = 0; i < 500; i++) {
		module->process(Test::makeProcessArgs(i));
	}

	SECTION("Output stays near zero for zero input") {
		REQUIRE(std::abs(module->outputs[RawModule::OUTPUT].getVoltage()) < 0.01f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Non-zero input produces non-zero output after settling", "[Raw]") {
	auto module = Test::createModule<RawModule>("Raw");

	module->inputs[RawModule::INPUT].channels = 1;
	module->inputs[RawModule::INPUT].setVoltage(1.f);

	float maxAbs = 0.f;
	for (int i = 0; i < 2000; i++) {
		module->process(Test::makeProcessArgs(i));
		float v = module->outputs[RawModule::OUTPUT].getVoltage();
		if (std::abs(v) > maxAbs) maxAbs = std::abs(v);
	}

	SECTION("At least some non-zero output is produced") {
		REQUIRE(maxAbs > 0.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Output channel count tracks input channel count", "[Raw]") {
	// Output::setChannels() early-returns if output.channels == 0
	// Pre-seeding output.channels > 0 simulates connected cable
	auto module = Test::createModule<RawModule>("Raw");

	SECTION("Single channel") {
		module->inputs[RawModule::INPUT].channels = 1;
		module->outputs[RawModule::OUTPUT].channels = 1;
		module->inputs[RawModule::INPUT].setVoltage(0.f);
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->outputs[RawModule::OUTPUT].getChannels() == 1);
	}

	SECTION("Four channels") {
		module->inputs[RawModule::INPUT].channels = 4;
		module->outputs[RawModule::OUTPUT].channels = 4;
		for (int c = 0; c < 4; c++) {
			module->inputs[RawModule::INPUT].setVoltage(0.f, c);
		}
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->outputs[RawModule::OUTPUT].getChannels() == 4);
	}

	SECTION("Zero channels (disconnected) produces zero output channels") {
		module->inputs[RawModule::INPUT].channels = 0;
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->outputs[RawModule::OUTPUT].getChannels() == 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("Output gain parameter is computed correctly by prepareParameters", "[Raw]") {
	// out_gain = pow(10, dB/20) * 5  (normalises ±1 back to ±5V)
	auto module = Test::createModule<RawModule>("Raw");

	SECTION("-20 dB yields out_gain 0.5") {
		module->params[RawModule::PARAM_GAIN_OUT].setValue(-20.f);
		module->prepareParameters();
		REQUIRE(module->out_gain == Approx(0.5f));
	}

	SECTION("0 dB yields out_gain 5.0") {
		module->params[RawModule::PARAM_GAIN_OUT].setValue(0.f);
		module->prepareParameters();
		REQUIRE(module->out_gain == Approx(5.f));
	}

	SECTION("+20 dB yields out_gain 50.0") {
		module->params[RawModule::PARAM_GAIN_OUT].setValue(20.f);
		module->prepareParameters();
		REQUIRE(module->out_gain == Approx(50.f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Output voltage scales linearly with out_gain", "[Raw]") {
	// Set up two modules with different out_gain values but identical resonator
	// state, then verify the output voltages differ by the expected ratio.

	auto modLow  = Test::createModule<RawModule>("Raw");
	auto modHigh = Test::createModule<RawModule>("Raw");

	modLow->params[RawModule::PARAM_GAIN_OUT].setValue(-20.f);
	modHigh->params[RawModule::PARAM_GAIN_OUT].setValue(20.f);
	modLow->prepareParameters();
	modHigh->prepareParameters();

	REQUIRE(modHigh->out_gain / modLow->out_gain == Approx(100.f));

	// Seed identical resonator state for identical velocity
	modLow->x[0][1]  = simd::float_4(0.1f, 0.f, 0.f, 0.f);
	modHigh->x[0][1] = simd::float_4(0.1f, 0.f, 0.f, 0.f);
	modLow->x[0][2]  = simd::float_4(0.f, 0.f, 0.f, 0.f);
	modHigh->x[0][2] = simd::float_4(0.f, 0.f, 0.f, 0.f);
	modLow->y[0][1]  = simd::float_4(0.f, 0.f, 0.f, 0.f);
	modHigh->y[0][1] = simd::float_4(0.f, 0.f, 0.f, 0.f);

	modLow->inputs[RawModule::INPUT].channels = 1;
	modHigh->inputs[RawModule::INPUT].channels = 1;
	modLow->outputs[RawModule::OUTPUT].channels = 1;
	modHigh->outputs[RawModule::OUTPUT].channels = 1;
	modLow->inputs[RawModule::INPUT].setVoltage(0.f);
	modHigh->inputs[RawModule::INPUT].setVoltage(0.f);

	// Process one sample with identical state but different out_gain
	modLow->process(Test::makeProcessArgs(1));
	modHigh->process(Test::makeProcessArgs(1));

	float vLow  = modLow->outputs[RawModule::OUTPUT].getVoltage();
	float vHigh = modHigh->outputs[RawModule::OUTPUT].getVoltage();

	SECTION("High-gain output is larger than low-gain output") {
		REQUIRE(std::abs(vHigh) > std::abs(vLow));
	}

	SECTION("Output ratio matches gain ratio") {
		if (std::abs(vLow) > 0.f) {
			REQUIRE(std::abs(vHigh) / std::abs(vLow) == Approx(100.f).epsilon(0.01f));
		}
	}

	Test::destroyModule(modLow);
	Test::destroyModule(modHigh);
}

TEST_CASE("JSON round-trip preserves panelTheme", "[JSON][Raw]") {
	auto module = Test::createModule<RawModule>("Raw");
	module->panelTheme = 1;

	json_t* j = module->dataToJson();

	auto module2 = Test::createModule<RawModule>("Raw");
	module2->dataFromJson(j);
	json_decref(j);

	SECTION("panelTheme restored") {
		REQUIRE(module2->panelTheme == 1);
	}

	Test::destroyModule(module);
	Test::destroyModule(module2);
}