#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVMapMicro.cpp"

using namespace StoermelderPackOne::CVMapMicro;

SYNC_MODEL(modelCVMapMicro, "CVMapMicro");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMapMicro]") {
	CVMapMicroModule* m = Test::createModule<CVMapMicroModule>("CVMapMicro");
	CVMapMicroWidget* mw = Test::createWidget<CVMapMicroWidget>("CVMapMicro");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[CVMapMicro][JSON]") {
	auto module = Test::createModule<CVMapMicroModule>("CVMapMicro");

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