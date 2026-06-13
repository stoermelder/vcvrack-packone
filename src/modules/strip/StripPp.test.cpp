#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "StripPp.cpp"

using namespace StoermelderPackOne::Strip;

SYNC_MODEL(modelStripPp, "StripPp");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[StripPp]") {
	StripPpModule* m = Test::createModule<StripPpModule>("StripPp");
	StripPpWidget* mw = Test::createWidget<StripPpWidget>("StripPp");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[StripPp][JSON]") {
	auto module = Test::createModule<StripPpModule>("StripPp");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}