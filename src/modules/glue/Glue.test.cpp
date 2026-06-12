#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Glue.cpp"

using namespace StoermelderPackOne::Glue;

SYNC_MODEL(modelGlue, "Glue");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Glue]") {
	GlueModule* m = Test::createModule<GlueModule>("Glue");
	GlueWidget* mw = Test::createWidget<GlueWidget>("Glue");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Glue][JSON]") {
	auto module = Test::createModule<GlueModule>("Glue");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}