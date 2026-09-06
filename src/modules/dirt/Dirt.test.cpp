#include "../../test/framework.hpp"
#include "Dirt.cpp"

using namespace StoermelderPackOne::Dirt;

SYNC_MODEL(modelDirt, "Dirt");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Dirt]") {
	Test::ModuleScaffold<DirtModule> mods;
	DirtModule* m = mods.create("Dirt");
	DirtWidget* mw = Test::createWidget<DirtWidget>("Dirt");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Dirt][JSON]") {
	Test::ModuleScaffold<DirtModule> mods;
	auto module = mods.create("Dirt");

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

TEST_CASE("JSON round-trip preserves state", "[Dirt][JSON]") {
	Test::ModuleScaffold<DirtModule> mods;
	DirtModule* m = mods.create("Dirt");
	DirtModule* m2 = mods.create("Dirt");

	SECTION("channels array round-trips (noiseRatio, crosstalkRatio, crackleRatio)") {
		// Distinctive, non-uniform values per channel so an indexing or
		// key-mismatch bug would be caught.
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) {
			m->noise[i].ratio = 0.01f * (i + 1);
			m->crosstalk.ratio[i] = 0.02f * (i + 1);
			m->crackle.ratio[i] = 0.03f * (i + 1);
		}

		json_t* j = m->dataToJson();
		// The channels array must be serialized with one entry per channel
		json_t* channelsJ = json_object_get(j, "channels");
		REQUIRE(channelsJ != nullptr);
		REQUIRE(json_array_size(channelsJ) == (size_t) PORT_MAX_CHANNELS);

		m2->dataFromJson(j);
		json_decref(j);

		for (int i = 0; i < PORT_MAX_CHANNELS; i++) {
			REQUIRE(m2->noise[i].ratio == Catch::Approx(0.01f * (i + 1)));
			REQUIRE(m2->crosstalk.ratio[i] == Catch::Approx(0.02f * (i + 1)));
			REQUIRE(m2->crackle.ratio[i] == Catch::Approx(0.03f * (i + 1)));
		}
	}

	SECTION("defect processors round-trip") {
		m->pitchDefect.baseProb = 0.11f;
		m->pitchDefect.speedMin = 0.12f;
		m->pitchDefect.speedMax = 0.13f;
		m->crushDefect.baseProb = 0.21f;
		m->crushDefect.speedMin = 0.22f;
		m->crushDefect.speedMax = 0.23f;
		m->dropoutDefect.baseProb = 0.31f;
		m->dropoutDefect.dropoutMax = 0.32f;

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->pitchDefect.baseProb == Catch::Approx(0.11f));
		REQUIRE(m2->pitchDefect.speedMin == Catch::Approx(0.12f));
		REQUIRE(m2->pitchDefect.speedMax == Catch::Approx(0.13f));
		REQUIRE(m2->crushDefect.baseProb == Catch::Approx(0.21f));
		REQUIRE(m2->crushDefect.speedMin == Catch::Approx(0.22f));
		REQUIRE(m2->crushDefect.speedMax == Catch::Approx(0.23f));
		REQUIRE(m2->dropoutDefect.baseProb == Catch::Approx(0.31f));
		REQUIRE(m2->dropoutDefect.dropoutMax == Catch::Approx(0.32f));
	}

}