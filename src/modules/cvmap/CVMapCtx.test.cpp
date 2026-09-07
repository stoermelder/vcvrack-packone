#include "../../test/framework.hpp"
#include "CVMapCtx.cpp"

using namespace StoermelderPackOne::CVMap;

SYNC_MODEL(modelCVMapCtx, "CVMapCtx");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMapCtx]") {
	Test::ModuleScaffold<CVMapCtxModule> mods;
	CVMapCtxModule* m = mods.create("CVMapCtx");
	CVMapCtxWidget* mw = Test::createWidget<CVMapCtxWidget>("CVMapCtx");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[CVMapCtx][JSON]") {
	Test::ModuleScaffold<CVMapCtxModule> mods;
	auto module = mods.create("CVMapCtx");

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

TEST_CASE("JSON round-trip preserves state", "[CVMapCtx][JSON]") {
	Test::ModuleScaffold<CVMapCtxModule> mods;
	CVMapCtxModule* m = mods.create("CVMapCtx");
	CVMapCtxModule* m2 = mods.create("CVMapCtx");

	m->panelTheme = 1;
	m->cvMapId = "ABC123xy";
	json_t* j = m->dataToJson();
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->panelTheme == 1);
	REQUIRE(m2->cvMapId == "ABC123xy");

}