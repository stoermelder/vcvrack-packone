#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVMap.cpp"

using namespace StoermelderPackOne::CVMap;

SYNC_MODEL(modelCVMap, "CVMap");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMap]") {
	CVMapModule* m = Test::createModule<CVMapModule>("CVMap");
	CVMapWidget* mw = Test::createWidget<CVMapWidget>("CVMap");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[CvMap][JSON]") {
	auto module = Test::createModule<CVMapModule>("CVMap");

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

TEST_CASE("JSON round-trip preserves state", "[CvMap][JSON]") {
	CVMapModule* m = Test::createModule<CVMapModule>("CVMap");
	CVMapModule* m2 = Test::createModule<CVMapModule>("CVMap");

	SECTION("Scalar settings round-trip") {
		// Distinct, non-default values for every scalar stored to JSON
		m->panelTheme = 1;
		m->audioRate = true;
		m->locked = true;
		m->parameterChangesDirect = true;
		m->bipolarInput = true;
		m->lockParameterChanges = false;
		m->textScrolling = false;
		m->mappingIndicatorHidden = true;
		NVGcolor c = color::fromHexString("#12345678");
		m->mappingIndicatorColor = c;

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->audioRate == true);
		REQUIRE(m2->locked == true);
		REQUIRE(m2->parameterChangesDirect == true);
		REQUIRE(m2->bipolarInput == true);
		REQUIRE(m2->lockParameterChanges == false);
		REQUIRE(m2->textScrolling == false);
		REQUIRE(m2->mappingIndicatorHidden == true);
		REQUIRE(color::toHexString(m2->mappingIndicatorColor) == color::toHexString(c));
	}

	SECTION("inputConfig array round-trips (both configs, all 16 labels)") {
		// Distinctive label on EVERY channel of BOTH input configs, plus a
		// distinct hideUnused flag per config
		for (int i = 0; i < 2; i++) {
			m->inputConfig[i].hideUnused = (i == 0);
			for (int j = 0; j < 16; j++) {
				m->inputConfig[i].label[j] = std::to_string(i) + "-" + std::to_string(j);
			}
		}

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		for (int i = 0; i < 2; i++) {
			REQUIRE(m2->inputConfig[i].hideUnused == (i == 0));
			for (int j = 0; j < 16; j++) {
				REQUIRE(m2->inputConfig[i].label[j] == std::to_string(i) + "-" + std::to_string(j));
			}
		}
	}

	SECTION("maps array round-trips (input, slew, min, max)") {
		// Populate three distinct maps. moduleId/paramId are engine-resolved
		// references to external modules and cannot be round-tripped in a unit
		// test without a registered target, so we verify the per-map data that
		// CVMap stores directly (input, slew, min, max) plus the array length.
		const int numMaps = 3;
		for (int i = 0; i < numMaps; i++) {
			m->paramHandles[i].moduleId = 100 + i;
			m->paramHandles[i].paramId = 10 + i;
			m->mapInput[i] = (i * 7) % 32;
			m->mapParam[i].setSlew(0.1f * (i + 1));
			m->mapParam[i].setMin(0.1f * i);
			m->mapParam[i].setMax(0.9f - 0.1f * i);
		}
		m->updateMapLen();

		json_t* j = m->dataToJson();
		// The maps array must be serialized with one entry per map slot
		json_t* mapsJ = json_object_get(j, "maps");
		REQUIRE(mapsJ != nullptr);
		REQUIRE(json_array_size(mapsJ) == (size_t) m->mapLen);

		m2->dataFromJson(j);
		json_decref(j);

		for (int i = 0; i < numMaps; i++) {
			REQUIRE(m2->mapInput[i] == (i * 7) % 32);
			REQUIRE(m2->mapParam[i].getSlew() == Catch::Approx(0.1f * (i + 1)));
			REQUIRE(m2->mapParam[i].getMin() == Catch::Approx(0.1f * i));
			REQUIRE(m2->mapParam[i].getMax() == Catch::Approx(0.9f - 0.1f * i));
		}
	}

	Test::destroyModule(m);
	Test::destroyModule(m2);
}