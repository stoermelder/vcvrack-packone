#include "../../test/framework.hpp"
#include "Strip.cpp"

using namespace StoermelderPackOne::Strip;

SYNC_MODEL(modelStrip, "Strip");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Strip]") {
	StripModule* m = Test::createModule<StripModule>("Strip");
	StripWidget* mw = Test::createWidget<StripWidget>("Strip");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Strip][JSON]") {
	auto module = Test::createModule<StripModule>("Strip");

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