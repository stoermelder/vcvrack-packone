#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Affix.cpp"

using namespace StoermelderPackOne::Affix;

// Helper to create module with proper SampleRateChangeEvent
template <int CHANNELS>
static AffixModule<CHANNELS>* createAffixModule(std::string modelSlug) {
	auto module = Test::createModule<AffixModule<CHANNELS>>(modelSlug);
	return module;
}

SYNC_MODEL(modelAffix, "Affix");
SYNC_MODEL(modelAffixMicro, "AffixMicro");
Test::TestContext<> testContext;


TEST_CASE("Construction and initialization", "[Affix]") {
	AffixModule<16>* m = Test::createModule<AffixModule<16>>("Affix");
	AffixWidget* mw = Test::createWidget<AffixWidget>("Affix");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Affix][JSON]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("Voltage mode", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");
	
	SECTION("Parameter values can be set and retrieved") {
		module->paramMode = PARAM_MODE::VOLTAGE;
		
		// Set parameter values
		module->params[AffixModule<16>::PARAM_MONO + 0].setValue(1.5f);
		module->params[AffixModule<16>::PARAM_MONO + 1].setValue(-0.5f);
		
		// Verify values are set
		REQUIRE(module->params[AffixModule<16>::PARAM_MONO + 0].getValue() == Catch::Approx(1.5f).margin(0.01f));
		REQUIRE(module->params[AffixModule<16>::PARAM_MONO + 1].getValue() == Catch::Approx(-0.5f).margin(0.01f));
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON serialization", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");
	
	SECTION("Module state is serialized and deserialized") {
		module->panelTheme = 1;
		module->paramMode = PARAM_MODE::SEMITONE;
		module->numberOfChannels = 8;
		
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		
		// Create a new module and load state
		auto moduleNew = Test::createModule<AffixModule<16>>("Affix");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->panelTheme == 1);
		REQUIRE(moduleNew->paramMode == PARAM_MODE::SEMITONE);
		REQUIRE(moduleNew->numberOfChannels == 8);
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}

TEST_CASE("Parameter reset", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");
	
	SECTION("onReset restores default state") {
		module->paramMode = PARAM_MODE::OCTAVE;
		module->numberOfChannels = 4;
		
        rack::engine::Module::ResetEvent re;
        module->onReset(re);
		
		REQUIRE(module->paramMode == PARAM_MODE::VOLTAGE);
		REQUIRE(module->numberOfChannels == 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("Parameter quantity display", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");
	
	SECTION("ParamQuantity displays correct format in voltage mode") {
		module->paramMode = PARAM_MODE::VOLTAGE;
		module->params[AffixModule<16>::PARAM_MONO + 0].setValue(2.5f);
		
		auto pq = dynamic_cast<AffixModule<16>::AffixParamQuantity*>(
			module->paramQuantities[AffixModule<16>::PARAM_MONO + 0]
		);
		
		REQUIRE(pq != nullptr);
		std::string display = pq->getDisplayValueString();
		REQUIRE(!display.empty());
	}

	Test::destroyModule(module);
}

TEST_CASE("Semitone mode", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");
	
	SECTION("Semitone mode snaps to 12ths") {
		module->setParamMode(PARAM_MODE::SEMITONE);
		
		// Set non-quantized value
		auto pq = module->paramQuantities[AffixModule<16>::PARAM_MONO + 0];
		pq->setValue(1.234f);
		
		// The underlying param value should be snapped to nearest semitone
		float val = module->params[AffixModule<16>::PARAM_MONO + 0].getValue();
		float snapped = std::round(1.234f * 12.f) / 12.f;
		REQUIRE(val == Catch::Approx(snapped).margin(0.001f));
	}
	
	SECTION("Semitone display format") {
		module->setParamMode(PARAM_MODE::SEMITONE);
		module->params[AffixModule<16>::PARAM_MONO + 0].setValue(1.5f); // 1 octave, 6 semitones
		
		auto pq = dynamic_cast<AffixModule<16>::AffixParamQuantity*>(
			module->paramQuantities[AffixModule<16>::PARAM_MONO + 0]
		);
		
		std::string display = pq->getDisplayValueString();
		REQUIRE(!display.empty());
		// Format should be "octaves, semitones"
	}

	Test::destroyModule(module);
}

TEST_CASE("Octave mode", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");
	
	SECTION("Octave mode snaps to integers") {
		module->setParamMode(PARAM_MODE::OCTAVE);
		
		// Set non-integer value using ParamQuantity
		auto pq = module->paramQuantities[AffixModule<16>::PARAM_MONO + 0];
		pq->setValue(2.7f);
		
		// Should snap to nearest octave (integer)
		float val = module->params[AffixModule<16>::PARAM_MONO + 0].getValue();
		REQUIRE(val == Catch::Approx(std::round(2.7f)).margin(0.001f));
	}
	
	SECTION("Octave display format") {
		module->setParamMode(PARAM_MODE::OCTAVE);
		module->params[AffixModule<16>::PARAM_MONO + 0].setValue(3.0f);
		
		auto pq = dynamic_cast<AffixModule<16>::AffixParamQuantity*>(
			module->paramQuantities[AffixModule<16>::PARAM_MONO + 0]
		);
		
		std::string display = pq->getDisplayValueString();
		REQUIRE(!display.empty());
	}

	Test::destroyModule(module);
}


// Regression test for bug #403:
// "Fixed wrong output voltage in Semitone/Octave-mode after loading"
// dataFromJson must call setParamMode() so that non-snapped param values
// (e.g. read back from a patch file that was saved with a different mode)
// are snapped to the nearest semitone/octave before processing begins.
TEST_CASE("Output voltage snapped after loading unsnapped param in Semitone mode", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");

	SECTION("Unsnapped param value is snapped to nearest semitone on load") {
		// Directly set raw param to an unsnapped value (1.1 V ≠ any exact semitone).
		// In semitone units: 1.1 * 12 = 13.2 → nearest semitone = 13 → 13/12 ≈ 1.08333 V
		module->params[AffixModule<16>::PARAM_MONO + 0].setValue(1.1f);
		module->numberOfChannels = 1;

		// Build the plugin-specific JSON as it would appear when loading a patch
		// that was originally saved in SEMITONE mode.
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(0));
		json_object_set_new(rootJ, "paramMode", json_integer((int)PARAM_MODE::SEMITONE));
		json_object_set_new(rootJ, "numberOfChannels", json_integer(1));

		// dataFromJson must call setParamMode() which snaps all param values.
		module->dataFromJson(rootJ);
		json_decref(rootJ);

		// Process one sample with no input voltage.
		module->inputs[AffixModule<16>::INPUT_POLY].setVoltage(0.f, 0);
		module->inputs[AffixModule<16>::INPUT_POLY].channels = 1;
		module->process(Test::makeProcessArgs(1));

		// Output must equal the snapped value, not the raw 1.1 V.
		float output = module->outputs[AffixModule<16>::OUTPUT_POLY].getVoltage(0);
		float expected = std::round(1.1f * 12.f) / 12.f; // 13/12 ≈ 1.08333
		REQUIRE(output == Catch::Approx(expected).margin(0.001f));
		REQUIRE(output != Catch::Approx(1.1f).margin(0.001f));
	}

	SECTION("Unsnapped param value is snapped to nearest octave on load") {
		// 2.7 V → nearest octave = 3 V
		module->params[AffixModule<16>::PARAM_MONO + 0].setValue(2.7f);
		module->numberOfChannels = 1;

		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(0));
		json_object_set_new(rootJ, "paramMode", json_integer((int)PARAM_MODE::OCTAVE));
		json_object_set_new(rootJ, "numberOfChannels", json_integer(1));

		module->dataFromJson(rootJ);
		json_decref(rootJ);

		module->inputs[AffixModule<16>::INPUT_POLY].setVoltage(0.f, 0);
		module->inputs[AffixModule<16>::INPUT_POLY].channels = 1;
		module->process(Test::makeProcessArgs(1));

		float output = module->outputs[AffixModule<16>::OUTPUT_POLY].getVoltage(0);
		float expected = std::round(2.7f); // 3.0
		REQUIRE(output == Catch::Approx(expected).margin(0.001f));
		REQUIRE(output != Catch::Approx(2.7f).margin(0.001f));
	}

	Test::destroyModule(module);
}

// Regression test for bug #387:
// "Fixed knob reset on double-click in Semitone/Octave-mode"
// Without the fix, AffixParamQuantity had no reset() override, so the
// cached 'v' field was not cleared when the knob was double-clicked to
// reset.  After reset, getValue() would still return the pre-reset 'v'
// value instead of the default (0 V), causing the knob to visually stay
// at the wrong position and subsequent drags to start from the stale value.
TEST_CASE("Param cached value reset in Semitone and Octave mode", "[Affix]") {
	auto module = Test::createModule<AffixModule<16>>("Affix");

	SECTION("After reset() in Semitone mode, getValue() returns default value") {
		module->setParamMode(PARAM_MODE::SEMITONE);

		// Move the param to a non-default value so 'v' is populated.
		auto pq = dynamic_cast<AffixModule<16>::AffixParamQuantity*>(
			module->paramQuantities[AffixModule<16>::PARAM_MONO + 0]);
		REQUIRE(pq != nullptr);
		pq->setValue(2.5f); // 'v' is now 2.5

		// Simulate a double-click reset.
		pq->reset();

		// After reset, getValue() must return the default (0 V), not the
		// stale 2.5 V that was in 'v' before the fix.
		REQUIRE(pq->getValue() == Catch::Approx(0.0f).margin(0.001f));
	}

	SECTION("After reset() in Octave mode, getValue() returns default value") {
		module->setParamMode(PARAM_MODE::OCTAVE);

		auto pq = dynamic_cast<AffixModule<16>::AffixParamQuantity*>(
			module->paramQuantities[AffixModule<16>::PARAM_MONO + 0]);
		REQUIRE(pq != nullptr);
		pq->setValue(3.0f); // 'v' is now 3.0

		pq->reset();

		REQUIRE(pq->getValue() == Catch::Approx(0.0f).margin(0.001f));
	}

	SECTION("Raw param storage is also zero after reset") {
		module->setParamMode(PARAM_MODE::SEMITONE);

		auto pq = dynamic_cast<AffixModule<16>::AffixParamQuantity*>(
			module->paramQuantities[AffixModule<16>::PARAM_MONO + 0]);
		pq->setValue(1.0f);
		pq->reset();

		// The underlying stored value must also be 0 so process() outputs 0 V.
		REQUIRE(module->params[AffixModule<16>::PARAM_MONO + 0].getValue()
			== Catch::Approx(0.0f).margin(0.001f));
	}

	Test::destroyModule(module);
}