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

	Test::destroyModule(module);
}