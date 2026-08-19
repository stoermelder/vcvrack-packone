#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiCatClk.cpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne::MidiCat;

SYNC_MODEL(modelMidiCat, "MidiCat");
SYNC_MODEL(modelMidiCatClk, "MidiCatClk");
Test::TestContext<> testContext;

struct TestParamModule : Module {
	enum ParamIds { PARAM_A, NUM_PARAMS };
	TestParamModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(PARAM_A, 0.f, 1.f, 0.5f, "Parameter A");
	}
};

// Helper: connect MidiCatClk to MidiCat as right expander and let MidiCat discover it.
// After connectClk(), expClk is non-null and all expClkTriggers are in LOW state (primed
// by the process() call inside which reads 0V from all unconnected inputs).
static void connectClk(MidiCatModule* midicat, MidiCatClkModule* clk) {
	midicat->rightExpander.module = clk;
	clk->leftExpander.module = midicat;
	midicat->moduleChangedFlag = true;
	midicat->process(Test::makeProcessArgs(1));
}

// Helper: send a low-then-high-then-low clock pulse on clock input `input`.
// Assumes the SchmittTrigger for that input is already primed to LOW (guaranteed
// after connectClk()). Returns at frame startFrame+2.
static void sendClockPulse(MidiCatModule* midicat, MidiCatClkModule* clk, int input, int64_t startFrame) {
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].channels = 1;
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(startFrame));
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].setVoltage(0.f);
	midicat->process(Test::makeProcessArgs(startFrame + 1));
}

// Helper: set up a full CC-to-param binding.
// Learns CC `cc` on channel `id` and binds to `target->PARAM_A`.
static void setupBinding(MidiCatModule* midicat, TestParamModule* target, int id, int cc) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(id, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(id, target->id, TestParamModule::PARAM_A);
	midicat->process(Test::makeProcessArgs(1));
	midicat->ccs[id].ccMode = CCMODE::DIRECT;
}


// ─── Standalone tests ───────────────────────────────────────────────────────

TEST_CASE("MidiCatClk: construction and initialization", "[MidiCatClk]") {
	MidiCatClkModule* m = Test::createModule<MidiCatClkModule>("MidiCatClk");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 0);
	REQUIRE(m->NUM_OUTPUTS == 0);
	REQUIRE(m->NUM_LIGHTS == 0);
	REQUIRE(m->NUM_INPUTS == 4);

	for (int i = 0; i < 4; i++) {
		REQUIRE(m->inputs[MidiCatClkModule::INPUT_CLOCK + i].channels == 0);
	}

	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[MidiCatClk][JSON]") {
	auto module = Test::createModule<MidiCatClkModule>("MidiCatClk");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("MidiCatClk: JSON round-trip stores and restores panelTheme", "[MidiCatClk]") {
	MidiCatClkModule* m = Test::createModule<MidiCatClkModule>("MidiCatClk");

	m->panelTheme = 3;
	json_t* j = m->dataToJson();
	m->panelTheme = 0;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 3);

	Test::destroyModule(m);
}

TEST_CASE("MidiCatClk: process() does not crash without expander", "[MidiCatClk]") {
	MidiCatClkModule* m = Test::createModule<MidiCatClkModule>("MidiCatClk");
	REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(1)));
	Test::destroyModule(m);
}


// ─── Integration tests ──────────────────────────────────────────────────────

TEST_CASE("MidiCatClk: MidiCat detects expander", "[MidiCatClk][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = Test::createModule<MidiCatClkModule>("MidiCatClk");
	Test::registerModule(midicat);
	Test::registerModule(clk);

	// Flush initial moduleChangedFlag so expClk is properly null before connecting
	midicat->process(Test::makeProcessArgs(0));
	REQUIRE(midicat->expClk.load() == nullptr);

	connectClk(midicat, clk);

	REQUIRE(midicat->expClk.load() != nullptr);
	REQUIRE(midicat->expClk.load() == clk);

	Test::unregisterModule(clk);
	Test::destroyModule(clk);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MidiCatClk: disconnecting expander clears expClk and resets clockModes", "[MidiCatClk][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = Test::createModule<MidiCatClkModule>("MidiCatClk");
	Test::registerModule(midicat);
	Test::registerModule(clk);

	connectClk(midicat, clk);
	REQUIRE(midicat->expClk.load() != nullptr);

	// Set some clock modes to non-OFF
	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
	midicat->setClockMode(1, MidiCatParam::CLOCKMODE::ARM_DEFERRED_FEEDBACK);

	// Disconnect
	midicat->rightExpander.module = nullptr;
	clk->leftExpander.module = nullptr;
	midicat->moduleChangedFlag = true;
	midicat->process(Test::makeProcessArgs(10));

	REQUIRE(midicat->expClk.load() == nullptr);
	REQUIRE(midicat->getClockMode(0) == MidiCatParam::CLOCKMODE::OFF);
	REQUIRE(midicat->getClockMode(1) == MidiCatParam::CLOCKMODE::OFF);

	Test::unregisterModule(clk);
	Test::destroyModule(clk);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MidiCatClk: ARM mode defers param update until clock tick", "[MidiCatClk][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = Test::createModule<MidiCatClkModule>("MidiCatClk");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(clk);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	// Set initial param value via CC 7 = 64 → ~0.504
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	midicat->process(Test::makeProcessArgs(2));
	float initialParamValue = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();

	// Enable ARM mode on channel 0, clock source 0
	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
	midicat->midiParam[0].clockSource = 0;

	connectClk(midicat, clk);

	// Send new CC value 100 — param should NOT change yet (ARM holds it)
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
	midicat->process(Test::makeProcessArgs(3));
	REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(initialParamValue).margin(0.001f));

	// Tick clock 0 — now the deferred value should be applied
	sendClockPulse(midicat, clk, 0, 10);
	float updatedParamValue = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();
	REQUIRE(updatedParamValue == Catch::Approx(100.f / 127.f).margin(0.01f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(clk);
	Test::destroyModule(clk);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MidiCatClk: ARM mode ignores clock on wrong source", "[MidiCatClk][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = Test::createModule<MidiCatClkModule>("MidiCatClk");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(clk);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	// Establish baseline param value via CC 7 = 64
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	midicat->process(Test::makeProcessArgs(2));
	float baseValue = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();

	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
	midicat->midiParam[0].clockSource = 2;  // param listens to clock 2

	connectClk(midicat, clk);

	// Send new MIDI value (will be deferred)
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
	midicat->process(Test::makeProcessArgs(3));

	// Tick clock 0 (wrong source — channel wants clock 2)
	sendClockPulse(midicat, clk, 0, 10);
	// Param should still be at the old value
	REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(baseValue).margin(0.001f));

	// Tick clock 2 (correct source)
	sendClockPulse(midicat, clk, 2, 20);
	REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(100.f / 127.f).margin(0.01f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(clk);
	Test::destroyModule(clk);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MidiCatClk: ARM_DEFERRED_FEEDBACK withholds MIDI feedback until clock tick", "[MidiCatClk][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = Test::createModule<MidiCatClkModule>("MidiCatClk");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(clk);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM_DEFERRED_FEEDBACK);
	midicat->midiParam[0].clockSource = 0;

	connectClk(midicat, clk);

	// Send CC 7 = 40 → stored as deferred (ARM_DEFERRED_FEEDBACK doesn't apply yet)
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 40));
	midicat->process(Test::makeProcessArgs(3));

	// Tick clock 0: applies setValueDeffered=40 and sets getValueLast=40
	sendClockPulse(midicat, clk, 0, 10);
	REQUIRE(midicat->midiParam[0].getValue() == 40);

	// Send CC 7 = 100 — deferred; getValueLast (and getValue()) still equals 40
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
	midicat->process(Test::makeProcessArgs(13));
	REQUIRE(midicat->midiParam[0].getValue() == 40);  // old value until tick

	// Tick clock 0 — applies deferred value, getValueLast advances to 100
	sendClockPulse(midicat, clk, 0, 20);
	REQUIRE(midicat->midiParam[0].getValue() == 100);

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(clk);
	Test::destroyModule(clk);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MidiCatClk: each of the four clock inputs fires its trigger", "[MidiCatClk][MidiCat]") {
	// Each SECTION reuses the same test setup but tests a different clock input.
	// The single channel 0 binding has its clockSource changed per section.
	for (int input = 0; input < 4; input++) {
		MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
		MidiCatClkModule* clk = Test::createModule<MidiCatClkModule>("MidiCatClk");
		TestParamModule* target = new TestParamModule();
		Test::registerModule(midicat);
		Test::registerModule(clk);
		Test::registerModule(target);

		// Bind CC 7 to PARAM_A with ARM quantization on clock input `input`
		setupBinding(midicat, target, 0, 7);
		midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
		midicat->midiParam[0].clockSource = input;

		connectClk(midicat, clk);

		// Establish baseline: send CC 64, tick clock `input` to settle param
		midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
		midicat->process(Test::makeProcessArgs(3));
		sendClockPulse(midicat, clk, input, 10);
		float baseline = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();

		// Send CC 7 = 100 — deferred in ARM mode (param still at baseline)
		midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
		midicat->process(Test::makeProcessArgs(20));
		REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(baseline).margin(0.001f));

		// Tick the correct clock input — param must now update to 100/127
		sendClockPulse(midicat, clk, input, 30);
		REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(100.f / 127.f).margin(0.01f));

		Test::unregisterModule(target);
		delete target;
		Test::unregisterModule(clk);
		Test::destroyModule(clk);
		Test::unregisterModule(midicat);
		Test::destroyModule(midicat);
	}
}
