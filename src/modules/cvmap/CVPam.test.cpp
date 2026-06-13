#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVPam.cpp"

using namespace StoermelderPackOne::CVPam;

SYNC_MODEL(modelCVPam, "CVPam");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVPam]") {
	CVPamModule* m = Test::createModule<CVPamModule>("CVPam");
	CVPamWidget* mw = Test::createWidget<CVPamWidget>("CVPam");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[CVPam][JSON]") {
	auto module = Test::createModule<CVPamModule>("CVPam");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}