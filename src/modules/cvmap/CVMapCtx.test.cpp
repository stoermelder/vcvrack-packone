#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVMapCtx.cpp"

using namespace StoermelderPackOne::CVMap;

SYNC_MODEL(modelCVMapCtx, "CVMapCtx");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMapCtx]") {
	CVMapCtxModule* m = Test::createModule<CVMapCtxModule>("CVMapCtx");
	CVMapCtxWidget* mw = Test::createWidget<CVMapCtxWidget>("CVMapCtx");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[CVMapCtx][JSON]") {
	auto module = Test::createModule<CVMapCtxModule>("CVMapCtx");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}