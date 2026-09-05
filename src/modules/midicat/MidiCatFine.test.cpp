#include "../../test/framework.hpp"
#include "MidiCatFine.cpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne::MidiCat;

SYNC_MODEL(modelMidiCat, "MidiCat");
SYNC_MODEL(modelMidiCatFine, "MidiCatFine");
Test::TestContext<> testContext;

struct TestParamModule : Module {
	enum ParamIds { PARAM_A, NUM_PARAMS };
	TestParamModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(PARAM_A, 0.f, 1.f, 0.5f, "Parameter A");
	}
};

// Helper: connect MidiCatFine to MidiCat as right expander and let MidiCat discover it.
// After connectFine(), expFine is non-null. The Fine expander does not itself need
// any priming before its voltage getters are queried.
static void connectFine(MidiCatModule* midicat, MidiCatFineModule* fine) {
	midicat->rightExpander.module = fine;
	fine->leftExpander.module = midicat;
	midicat->moduleChangedFlag = true;
	midicat->process(Test::makeProcessArgs(1));
}

// Helper: set up a full CC-to-param binding.
static void setupBinding(MidiCatModule* midicat, TestParamModule* target, int id, int cc) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(id, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(id, target->id, TestParamModule::PARAM_A);
	midicat->process(Test::makeProcessArgs(1));
	midicat->slots[id].cc.ccMode = CCMODE::DIRECT;
}

// Send a low→high→low transition on a fine expander input and return the startFrame+2
// that the second process() call uses (so the test framework can continue from there).
// SchmittTriggers in the parent MidiCat start UNINITIALIZED, so we prime them first by
// calling process() with the input at 0V before driving them high.
static void primeFineTriggers(MidiCatModule* midicat, MidiCatFineModule* fine) {
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(0.f);
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(0.f);
	midicat->process(Test::makeProcessArgs(1));
}

static void sendFineGate(MidiCatModule* midicat, int input, float voltage, int64_t startFrame) {
	midicat->process(Test::makeProcessArgs(startFrame));
	midicat->process(Test::makeProcessArgs(startFrame + 1));
}


// ─── Standalone tests ───────────────────────────────────────────────────────

TEST_CASE("Construction and initialization", "[MidiCatFine]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	MidiCatFineModule* m = mods.create("MidiCatFine");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 0);
	REQUIRE(m->NUM_OUTPUTS == 0);
	REQUIRE(m->NUM_LIGHTS == 0);
	REQUIRE(m->NUM_INPUTS == 2);

	REQUIRE(m->inputs[MidiCatFineModule::INPUT_LOWRANGE].channels == 0);
	REQUIRE(m->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels == 0);

	// onReset() defaults
	REQUIRE(m->highRange == Catch::Approx(0.01f));
	REQUIRE(m->lowRange == Catch::Approx(0.1f));
}

TEST_CASE("Preset JSON null-guards", "[MidiCatFine][JSON]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	auto module = mods.create("MidiCatFine");

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

TEST_CASE("JSON round-trip preserves state", "[MidiCatFine][JSON]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	MidiCatFineModule* m = mods.create("MidiCatFine");

	m->panelTheme = 3;
	m->highRange = 0.05f;
	json_t* j = m->dataToJson();
	m->panelTheme = 0;
	m->highRange = 0.01f;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 3);
	REQUIRE(m->highRange == Catch::Approx(0.05f));
}

TEST_CASE("dataFromJson ignores missing keys", "[MidiCatFine][JSON]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	MidiCatFineModule* m = mods.create("MidiCatFine");

	json_error_t err;
	json_t* emptyJ = json_loads("{}", 0, &err);
	REQUIRE(emptyJ != nullptr);
	m->dataFromJson(emptyJ);
	json_decref(emptyJ);

	// Defaults must be retained
	REQUIRE(m->highRange == Catch::Approx(0.01f));
	REQUIRE(m->panelTheme == StoermelderPackOne::pluginSettings.panelThemeDefault);
}

TEST_CASE("dataFromJson handles null values without crashing", "[MidiCatFine][JSON]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	MidiCatFineModule* m = mods.create("MidiCatFine");

	json_error_t err;
	json_t* nullJ = json_loads("{\"panelTheme\": null, \"highRange\": null}", 0, &err);
	REQUIRE(nullJ != nullptr);
	// json_object_get returns a non-null pointer for json_null values,
	// so the `if (highRangeJ)` guard passes; json_real_value of a null
	// returns 0, so highRange is overwritten with 0. The function must
	// not crash in either case.
	REQUIRE_NOTHROW(m->dataFromJson(nullJ));
	json_decref(nullJ);
}

TEST_CASE("process() does not crash without expander or parent", "[MidiCatFine]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	MidiCatFineModule* m = mods.create("MidiCatFine");
	REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(1)));
}

TEST_CASE("Voltage getters return input voltages", "[MidiCatFine]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	MidiCatFineModule* m = mods.create("MidiCatFine");

	REQUIRE(m->getLowRangeVoltage() == Catch::Approx(0.f));
	REQUIRE(m->getHighRangeVoltage() == Catch::Approx(0.f));

	m->inputs[MidiCatFineModule::INPUT_LOWRANGE].channels = 1;
	m->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(3.5f);
	REQUIRE(m->getLowRangeVoltage() == Catch::Approx(3.5f));

	m->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels = 1;
	m->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(7.25f);
	REQUIRE(m->getHighRangeVoltage() == Catch::Approx(7.25f));

	// Range getters are constant
	REQUIRE(m->getLowRange() == Catch::Approx(0.1f));
	REQUIRE(m->getHighRange() == Catch::Approx(0.01f));
}


// ─── Integration tests with MidiCat parent ──────────────────────────────────

TEST_CASE("MidiCat detects expander", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	// MidiCat.expanders.hpp detects fine via `exp->model == modelMidiCatFine` — a mismatch here
	// (missing/wrong SYNC_MODEL) would make the REQUIRE below fail with no useful diagnosis.
	Test::requireModelSync(modelMidiCatFine, "MidiCatFine");
	Test::registerModule(midicat);
	Test::registerModule(fine);

	// Flush initial expandersChanged so expFine is properly null before connecting
	midicat->process(Test::makeProcessArgs(0));
	REQUIRE(midicat->expanders.fine() == nullptr);

	connectFine(midicat, fine);

	REQUIRE(midicat->expanders.fine() != nullptr);
	REQUIRE(midicat->expanders.fine() == fine);

	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("Disconnecting expander clears expFine and ccFineMode", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	Test::registerModule(midicat);
	Test::registerModule(fine);

	connectFine(midicat, fine);
	REQUIRE(midicat->expanders.fine() != nullptr);

	// Force ccFineMode true (it should be cleared on disconnect)
	midicat->ccFineMode = true;

	// Disconnect
	midicat->rightExpander.module = nullptr;
	fine->leftExpander.module = nullptr;
	midicat->moduleChangedFlag = true;
	midicat->process(Test::makeProcessArgs(10));

	REQUIRE(midicat->expanders.fine() == nullptr);
	REQUIRE(midicat->ccFineMode == false);

	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("process() does not crash without parent connection", "[MidiCatFine]") {
	Test::ModuleScaffold<MidiCatFineModule> mods;
	MidiCatFineModule* m = mods.create("MidiCatFine");
	m->leftExpander.module = nullptr;
	m->rightExpander.module = nullptr;
	REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(1)));
}


// ─── FineExpanderDriver interaction with parent ───────────────────────────────

TEST_CASE("rising edge on LOWRANGE enables fine mode at low precision", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	REQUIRE(midicat->ccFineMode == false);

	// Drive LOWRANGE high — should enable fine mode at low precision (0.1)
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(10));

	REQUIRE(midicat->ccFineMode == true);
	// After enabling, all channels' precProcessor is initialized; verify
	// the precision on channel 0 is the low range.
	REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.1f));	// init(0, 127) sets the ref point to (127-0)/2 = 63.
	REQUIRE(midicat->slots[0].param.precProcessor.midiRefPoint == 63);
	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("Falling edge on LOWRANGE disables fine mode when HIGHRANGE is low", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	// Enable fine mode by raising LOWRANGE.
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(10));
	REQUIRE(midicat->ccFineMode == true);

	// Lower LOWRANGE — fine mode should disable.
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(0.f);
	midicat->process(Test::makeProcessArgs(11));

	REQUIRE(midicat->ccFineMode == false);

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("Rising edge on HIGHRANGE enables fine mode at high precision", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	// Drive HIGHRANGE high — fine mode should enable at high precision (0.01).
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(10));

	REQUIRE(midicat->ccFineMode == true);
	REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.01f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("High precision follows user setting (2% / 5%)", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels = 1;

	SECTION("2% precision") {
		fine->highRange = 0.02f;
		fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(10.f);
		midicat->process(Test::makeProcessArgs(10));
		REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.02f));
	}
	SECTION("5% precision") {
		fine->highRange = 0.05f;
		fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(10.f);
		midicat->process(Test::makeProcessArgs(10));
		REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.05f));
	}

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("HIGHRANGE rising while LOWRANGE is high updates the ref-point from current CC", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	// First, drive LOWRANGE high. setFineMode() initialises the ref
	// point via init(getLimitMin(), getLimitMax()) = init(0, 127), so
	// the initial ref is (127-0)/2 = 63.
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(10));
	REQUIRE(midicat->ccFineMode == true);
	REQUIRE(midicat->slots[0].param.precProcessor.midiRefPoint == 63);

	// Now also drive HIGHRANGE high. With LOWRANGE still high, the
	// production code passes updateRefPoint=true, which causes
	// setPrecision() to overwrite midiRefPoint with slots[0].cc.getValue().
	// The CC value last received in setupBinding is 64.
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(11));

	REQUIRE(midicat->ccFineMode == true);
	REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.01f));
	REQUIRE(midicat->slots[0].param.precProcessor.midiRefPoint == 64);

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("HIGHRANGE falling while LOWRANGE is high restores low precision", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels = 1;

	// Both high
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(10.f);
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(10));
	REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.01f));

	// Drop HIGHRANGE while keeping LOWRANGE — should fall back to low precision.
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(0.f);
	midicat->process(Test::makeProcessArgs(11));

	REQUIRE(midicat->ccFineMode == true);
	REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.1f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("HIGHRANGE falling when both are low disables fine mode", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	// Enable via HIGHRANGE
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(10));
	REQUIRE(midicat->ccFineMode == true);

	// Drop HIGHRANGE — fine mode should disable (LOWRANGE is not high).
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(0.f);
	midicat->process(Test::makeProcessArgs(11));

	REQUIRE(midicat->ccFineMode == false);

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}

TEST_CASE("LOWRANGE ignored while HIGHRANGE is high", "[MidiCatFine][MidiCat]") {
	Test::ModuleScaffold<MidiCatModule> midicatMods;
	MidiCatModule* midicat = midicatMods.create("MidiCat");
	Test::ModuleScaffold<MidiCatFineModule> fineMods;
	MidiCatFineModule* fine = fineMods.create("MidiCatFine");
	TestParamModule* target = new TestParamModule();
	Test::registerModule(midicat);
	Test::registerModule(fine);
	Test::registerModule(target);

	setupBinding(midicat, target, 0, 7);
	connectFine(midicat, fine);
	primeFineTriggers(midicat, fine);

	// Drive HIGHRANGE high first.
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_HIGHRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(10));
	REQUIRE(midicat->ccFineMode == true);
	REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.01f));

	// Now drive LOWRANGE high. Because the LOWRANGE rising-edge branch
	// is guarded by `!expFineHighTrigger.isHigh()`, it must NOT switch
	// precision back to 0.1.
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].channels = 1;
	fine->inputs[MidiCatFineModule::INPUT_LOWRANGE].setVoltage(10.f);
	midicat->process(Test::makeProcessArgs(11));

	REQUIRE(midicat->slots[0].param.precProcessor.precision == Catch::Approx(0.01f));

	Test::unregisterModule(target);
	delete target;
	Test::unregisterModule(fine);
	Test::unregisterModule(midicat);
}
