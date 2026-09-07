#include "../../test/framework.hpp"
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

// Helper: send a low-then-high-then-low clock pulse on clock input `input`.
// Assumes the SchmittTrigger for that input is already primed to LOW (guaranteed
// after connectClk()).
static void sendClockPulse(Test::Harness& h, MidiCatModule* midicat, MidiCatClkModule* clk, int input) {
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].channels = 1;
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].setVoltage(10.f);
	h.dspStep();
	clk->inputs[MidiCatClkModule::INPUT_CLOCK + input].setVoltage(0.f);
	h.dspStep();
}

// Helper: set up a full CC-to-param binding.
// Learns CC `cc` on channel `id` and binds to `target->PARAM_A`.
static void setupBinding(Test::Harness& h, MidiCatModule* midicat, TestParamModule* target, int id, int cc) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(id, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(id, target->id, TestParamModule::PARAM_A);
	h.dspStep();
	midicat->slots[id].cc.ccMode = CCMODE::DIRECT;
}


// ─── Standalone tests ───────────────────────────────────────────────────────

TEST_CASE("Construction and initialization", "[MidiCatClk]") {
	Test::Harness h;
	MidiCatClkModule* m = h.addModule<MidiCatClkModule>("MidiCatClk");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 0);
	REQUIRE(m->NUM_OUTPUTS == 0);
	REQUIRE(m->NUM_LIGHTS == 0);
	REQUIRE(m->NUM_INPUTS == 4);

	for (int i = 0; i < 4; i++) {
		REQUIRE(m->inputs[MidiCatClkModule::INPUT_CLOCK + i].channels == 0);
	}
}

TEST_CASE("Preset JSON null-guards", "[MidiCatClk][JSON]") {
	Test::Harness h;
	auto module = h.addModule<MidiCatClkModule>("MidiCatClk");

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

}

TEST_CASE("JSON round-trip preserves state", "[MidiCatClk]") {
	Test::Harness h;
	MidiCatClkModule* m = h.addModule<MidiCatClkModule>("MidiCatClk");

	m->panelTheme = 3;
	json_t* j = m->dataToJson();
	m->panelTheme = 0;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 3);
}

// ─── Integration tests ──────────────────────────────────────────────────────

TEST_CASE("MidiCat detects expander", "[MidiCatClk][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = h.addModule<MidiCatClkModule>("MidiCatClk");
	// MidiCat.expanders.hpp detects clk via `exp->model == modelMidiCatClk` — a mismatch here
	// (missing/wrong SYNC_MODEL) would make the REQUIRE below fail with no useful diagnosis.
	Test::requireModelSync(modelMidiCatClk, "MidiCatClk");

	// Flush initial moduleChangedFlag so expClk is properly null before connecting
	h.dspStep();
	REQUIRE(midicat->expanders.clk() == nullptr);

	h.connectExpander(midicat, clk);
	h.dspStep();

	REQUIRE(midicat->expanders.clk() != nullptr);
	REQUIRE(midicat->expanders.clk() == clk);
}

TEST_CASE("Disconnecting expander clears expClk and resets clockModes", "[MidiCatClk][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = h.addModule<MidiCatClkModule>("MidiCatClk");

	h.connectExpander(midicat, clk);
	h.dspStep();
	REQUIRE(midicat->expanders.clk() != nullptr);

	// Set some clock modes to non-OFF
	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
	midicat->setClockMode(1, MidiCatParam::CLOCKMODE::ARM_DEFERRED_FEEDBACK);

	// Disconnect
	h.disconnectExpander(midicat, Test::Harness::SIDE_RIGHT);
	h.dspStep();

	REQUIRE(midicat->expanders.clk() == nullptr);
	REQUIRE(midicat->getClockMode(0) == MidiCatParam::CLOCKMODE::OFF);
	REQUIRE(midicat->getClockMode(1) == MidiCatParam::CLOCKMODE::OFF);
}

TEST_CASE("ARM mode defers param update until clock tick", "[MidiCatClk][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = h.addModule<MidiCatClkModule>("MidiCatClk");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	setupBinding(h, midicat, target, 0, 7);
	// Set initial param value via CC 7 = 64 → ~0.504
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	h.dspStep();
	float initialParamValue = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();

	// Enable ARM mode on channel 0, clock source 0
	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
	midicat->slots[0].param.clockSource = 0;

	h.connectExpander(midicat, clk);
	h.dspStep();

	// Send new CC value 100 — param should NOT change yet (ARM holds it)
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
	h.dspStep();
	REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(initialParamValue).margin(0.001f));

	// Tick clock 0 — now the deferred value should be applied
	sendClockPulse(h, midicat, clk, 0);
	float updatedParamValue = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();
	REQUIRE(updatedParamValue == Catch::Approx(100.f / 127.f).margin(0.01f));
}

TEST_CASE("ARM mode ignores clock on wrong source", "[MidiCatClk][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = h.addModule<MidiCatClkModule>("MidiCatClk");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	setupBinding(h, midicat, target, 0, 7);
	// Establish baseline param value via CC 7 = 64
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	h.dspStep();
	float baseValue = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();

	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
	midicat->slots[0].param.clockSource = 2;  // param listens to clock 2

	h.connectExpander(midicat, clk);
	h.dspStep();

	// Send new MIDI value (will be deferred)
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
	h.dspStep();

	// Tick clock 0 (wrong source — channel wants clock 2)
	sendClockPulse(h, midicat, clk, 0);
	// Param should still be at the old value
	REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(baseValue).margin(0.001f));

	// Tick clock 2 (correct source)
	sendClockPulse(h, midicat, clk, 2);
	REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(100.f / 127.f).margin(0.01f));
}

TEST_CASE("ARM_DEFERRED_FEEDBACK withholds MIDI feedback until clock tick", "[MidiCatClk][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatClkModule* clk = h.addModule<MidiCatClkModule>("MidiCatClk");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	setupBinding(h, midicat, target, 0, 7);
	midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM_DEFERRED_FEEDBACK);
	midicat->slots[0].param.clockSource = 0;

	h.connectExpander(midicat, clk);
	h.dspStep();

	// Send CC 7 = 40 → stored as deferred (ARM_DEFERRED_FEEDBACK doesn't apply yet)
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 40));
	h.dspStep();

	// Tick clock 0: applies setValueDeffered=40 and sets getValueLast=40
	sendClockPulse(h, midicat, clk, 0);
	REQUIRE(midicat->slots[0].param.getValue() == 40);

	// Send CC 7 = 100 — deferred; getValueLast (and getValue()) still equals 40
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
	h.dspStep();
	REQUIRE(midicat->slots[0].param.getValue() == 40);  // old value until tick

	// Tick clock 0 — applies deferred value, getValueLast advances to 100
	sendClockPulse(h, midicat, clk, 0);
	REQUIRE(midicat->slots[0].param.getValue() == 100);
}

TEST_CASE("Each of the four clock inputs fires its trigger", "[MidiCatClk][MidiCat]") {
	// Each SECTION reuses the same test setup but tests a different clock input.
	// The single channel 0 binding has its clockSource changed per section.
	for (int input = 0; input < 4; input++) {
		Test::Harness h;
		MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
		MidiCatClkModule* clk = h.addModule<MidiCatClkModule>("MidiCatClk");
		TestParamModule* target = h.adoptModule(new TestParamModule);

		// Bind CC 7 to PARAM_A with ARM quantization on clock input `input`
		setupBinding(h, midicat, target, 0, 7);
		midicat->setClockMode(0, MidiCatParam::CLOCKMODE::ARM);
		midicat->slots[0].param.clockSource = input;

		h.connectExpander(midicat, clk);
		h.dspStep();

		// Establish baseline: send CC 64, tick clock `input` to settle param
		midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
		h.dspStep();
		sendClockPulse(h, midicat, clk, input);
		float baseline = target->getParamQuantity(TestParamModule::PARAM_A)->getValue();

		// Send CC 7 = 100 — deferred in ARM mode (param still at baseline)
		midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, 7, 100));
		h.dspStep();
		REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(baseline).margin(0.001f));

		// Tick the correct clock input — param must now update to 100/127
		sendClockPulse(h, midicat, clk, input);
		REQUIRE(target->getParamQuantity(TestParamModule::PARAM_A)->getValue() == Catch::Approx(100.f / 127.f).margin(0.01f));
	}
}
