#include "../../test/framework.hpp"
#include "CVMapMicro.cpp"

using namespace StoermelderPackOne::CVMapMicro;

SYNC_MODEL(modelCVMapMicro, "CVMapMicro");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMapMicro]") {
	Test::ModuleScaffold<CVMapMicroModule> mods;
	CVMapMicroModule* m = mods.create("CVMapMicro");
	CVMapMicroWidget* mw = Test::createWidget<CVMapMicroWidget>("CVMapMicro");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[CVMapMicro][JSON]") {
	Test::ModuleScaffold<CVMapMicroModule> mods;
	auto module = mods.create("CVMapMicro");

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