#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Infix.cpp"

using namespace StoermelderPackOne::Infix;

SYNC_MODEL(modelInfix, "Infix");
SYNC_MODEL(modelInfixMicro, "InfixMicro");
Test::TestContext<> testContext;


// Connect a polyphonic input with the given voltages (one per channel).
static void setPolyInput(InfixModule<16>* m, std::initializer_list<float> voltages) {
	int ch = 0;
	for (float v : voltages) {
		m->inputs[InfixModule<16>::INPUT_POLY].setVoltage(v, ch++);
	}
	m->inputs[InfixModule<16>::INPUT_POLY].channels = (int)voltages.size();
}

// Pre-seed the output so Output::setChannels() isn't blocked by channels==0.
static void seedOutput(InfixModule<16>* m) {
	m->outputs[InfixModule<16>::OUTPUT_POLY].channels = 16;
}

// Connect a single mono replacement input on channel c.
static void setMonoInput(InfixModule<16>* m, int c, float voltage) {
	m->inputs[InfixModule<16>::INPUT_MONO + c].channels = 1;
	m->inputs[InfixModule<16>::INPUT_MONO + c].setVoltage(voltage);
}

// Disconnect a mono replacement input on channel c.
static void disconnectMonoInput(InfixModule<16>* m, int c) {
	m->inputs[InfixModule<16>::INPUT_MONO + c].channels = 0;
}


TEST_CASE("Construction and initialization", "[Infix]") {
	InfixModule<16>* m = Test::createModule<InfixModule<16>>("Infix");
	InfixWidget* mw = Test::createWidget<InfixWidget>("Infix");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}


// Pass-through: poly input passes verbatim when no mono inputs are connected
TEST_CASE("Poly pass-through: all channels forwarded when no mono inputs connected", "[Infix]") {
	auto* m = Test::createModule<InfixModule<16>>("Infix");

	setPolyInput(m, {1.f, 2.f, 3.f, 4.f});
	seedOutput(m);
	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getChannels() == 4);
	REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(0) == Catch::Approx(1.f));
	REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(1) == Catch::Approx(2.f));
	REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(2) == Catch::Approx(3.f));
	REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(3) == Catch::Approx(4.f));

	Test::destroyModule(m);
}


// Single-channel replacement
TEST_CASE("Mono input replaces its corresponding poly channel", "[Infix]") {
	auto* m = Test::createModule<InfixModule<16>>("Infix");

	setPolyInput(m, {1.f, 2.f, 3.f, 4.f});
	setMonoInput(m, 1, 9.f); // replace channel 1
	seedOutput(m);
	m->process(Test::makeProcessArgs(1));

	SECTION("Replaced channel carries mono voltage") {
		REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(1) == Catch::Approx(9.f));
	}

	SECTION("Other channels retain poly voltage") {
		REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(0) == Catch::Approx(1.f));
		REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(2) == Catch::Approx(3.f));
		REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getVoltage(3) == Catch::Approx(4.f));
	}

	SECTION("Output channel count unchanged") {
		REQUIRE(m->outputs[InfixModule<16>::OUTPUT_POLY].getChannels() == 4);
	}

	Test::destroyModule(m);
}