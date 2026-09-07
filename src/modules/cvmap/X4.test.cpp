#include "../../test/framework.hpp"
#include "X4.cpp"

using namespace StoermelderPackOne::X4;

SYNC_MODEL(modelX4, "X4");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[X4]") {
	Test::ModuleScaffold<X4Module> mods;
	X4Module* m = mods.create("X4");
	X4Widget* mw = Test::createWidget<X4Widget>("X4");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[X4][JSON]") {
	Test::ModuleScaffold<X4Module> mods;
	auto module = mods.create("X4");

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

TEST_CASE("JSON round-trip preserves state", "[X4][JSON]") {
	Test::ModuleScaffold<X4Module> mods;
	X4Module* m = mods.create("X4");
	X4Module* m2 = mods.create("X4");

	SECTION("X4 scalar settings round-trip") {
		// Distinct, non-default values for every X4-specific scalar stored to JSON
		m->panelTheme = 1;
		m->audioRate = true;
		m->parameterChangesDirect = true;

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->audioRate == true);
		REQUIRE(m2->parameterChangesDirect == true);
	}

	SECTION("readParam nested array round-trips (2x5 booleans)") {
		// Distinctive, non-uniform patterns so every entry is individually checked
		for (size_t i = 0; i < 5; i++) {
			m->readParamA[i] = (i % 2 == 0); // true, false, true, false, true
			m->readParamB[i] = (i % 3 == 0); // true, false, false, true, false
		}

		json_t* j = m->dataToJson();
		// The readParam JSON is a nested array: outer length 2, each inner length 5
		json_t* readParamJ = json_object_get(j, "readParam");
		REQUIRE(readParamJ != nullptr);
		REQUIRE(json_array_size(readParamJ) == 2);
		REQUIRE(json_array_size(json_array_get(readParamJ, 0)) == 5);
		REQUIRE(json_array_size(json_array_get(readParamJ, 1)) == 5);

		m2->dataFromJson(j);
		json_decref(j);

		for (size_t i = 0; i < 5; i++) {
			REQUIRE(m2->readParamA[i] == (i % 2 == 0));
			REQUIRE(m2->readParamB[i] == (i % 3 == 0));
		}
	}

}