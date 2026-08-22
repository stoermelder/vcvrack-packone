#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Macro.cpp"

using namespace StoermelderPackOne::Macro;

SYNC_MODEL(modelMacro, "Macro");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Macro]") {
	MacroModule* m = Test::createModule<MacroModule>("Macro");
	MacroWidget* mw = Test::createWidget<MacroWidget>("Macro");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Macro][JSON]") {
	auto module = Test::createModule<MacroModule>("Macro");

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

TEST_CASE("JSON round-trip preserves state", "[Macro][JSON]") {
	MacroModule* m = Test::createModule<MacroModule>("Macro");
	MacroModule* m2 = Test::createModule<MacroModule>("Macro");

	SECTION("Scalar settings round-trip") {
		m->panelTheme = 1;
		m->processDivision = 32;
		m->setParameterChangesDirect(true);
		m->bipolarInput = true;
		m->lockParameterChanges = true;

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->processDivision == 32);
		REQUIRE(m2->parameterChangesDirect == true);
		REQUIRE(m2->bipolarInput == true);
		REQUIRE(m2->lockParameterChanges == true);
	}

	SECTION("CV outputs (cvs array) round-trip") {
		// Distinctive per-port slew/min/max and bipolar flag (encoded as minValue/maxValue)
		m->scaleCvs[0].setSlew(1.5f);
		m->scaleCvs[0].setMin(0.1f);
		m->scaleCvs[0].setMax(0.9f);
		m->scaleCvs[0].paramQuantity->minValue = -5.f;  // bipolar
		m->scaleCvs[0].paramQuantity->maxValue = 5.f;

		m->scaleCvs[1].setSlew(2.5f);
		m->scaleCvs[1].setMin(0.2f);
		m->scaleCvs[1].setMax(0.8f);
		m->scaleCvs[1].paramQuantity->minValue = 0.f;   // unipolar
		m->scaleCvs[1].paramQuantity->maxValue = 10.f;

		json_t* j = m->dataToJson();
		// The cvs array must be serialized with one entry per CV port
		json_t* cvsJ = json_object_get(j, "cvs");
		REQUIRE(cvsJ != nullptr);
		REQUIRE(json_array_size(cvsJ) == (size_t) CVPORTS);
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->scaleCvs[0].getSlew() == Catch::Approx(1.5f));
		REQUIRE(m2->scaleCvs[0].getMin() == Catch::Approx(0.1f));
		REQUIRE(m2->scaleCvs[0].getMax() == Catch::Approx(0.9f));
		REQUIRE(m2->scaleCvs[0].paramQuantity->minValue == Catch::Approx(-5.f));
		REQUIRE(m2->scaleCvs[0].paramQuantity->maxValue == Catch::Approx(5.f));

		REQUIRE(m2->scaleCvs[1].getSlew() == Catch::Approx(2.5f));
		REQUIRE(m2->scaleCvs[1].getMin() == Catch::Approx(0.2f));
		REQUIRE(m2->scaleCvs[1].getMax() == Catch::Approx(0.8f));
		REQUIRE(m2->scaleCvs[1].paramQuantity->minValue == Catch::Approx(0.f));
		REQUIRE(m2->scaleCvs[1].paramQuantity->maxValue == Catch::Approx(10.f));
	}

	SECTION("Mapping slots (maps array) round-trip") {
		// A registered target is required so the maps array carries moduleId/paramId
		// entries. The per-slot slew/min/max (stored in scaleParam via dataToJsonMap/
		// dataFromJsonMap) round-trip independently of the engine lock that
		// moduleId/paramId resolution needs — those don't round-trip in a unit test.
		rack::Module* target = Test::createModule<rack::Module>("Glue");
		Test::registerModule(target);

		m->learnParam(0, target->id, 0);
		m->learnParam(1, target->id, 2);

		// Distinctive per-slot scaling values
		m->scaleParam[0].setSlew(0.5f);
		m->scaleParam[0].setMin(0.3f);
		m->scaleParam[0].setMax(0.7f);
		m->scaleParam[1].setSlew(0.6f);
		m->scaleParam[1].setMin(0.4f);
		m->scaleParam[1].setMax(0.8f);

		json_t* j = m->dataToJson();
		// The maps array must be serialized with one entry per map slot
		json_t* mapsJ = json_object_get(j, "maps");
		REQUIRE(mapsJ != nullptr);
		REQUIRE(json_array_size(mapsJ) == (size_t) m->mapLen);
		m2->dataFromJson(j);
		json_decref(j);

		// Per-slot slew/min/max round-trip (stored in scaleParam, no engine lock needed)
		REQUIRE(m2->scaleParam[0].getSlew() == Catch::Approx(0.5f));
		REQUIRE(m2->scaleParam[0].getMin() == Catch::Approx(0.3f));
		REQUIRE(m2->scaleParam[0].getMax() == Catch::Approx(0.7f));
		REQUIRE(m2->scaleParam[1].getSlew() == Catch::Approx(0.6f));
		REQUIRE(m2->scaleParam[1].getMin() == Catch::Approx(0.4f));
		REQUIRE(m2->scaleParam[1].getMax() == Catch::Approx(0.8f));

		Test::unregisterModule(target);
		Test::destroyModule(target);
	}

	Test::destroyModule(m);
	Test::destroyModule(m2);
}