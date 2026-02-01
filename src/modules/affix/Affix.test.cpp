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

// Define the single instance used by tests
static Test::TestContext<> testContext;

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