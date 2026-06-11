#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Dirt.cpp"

using namespace StoermelderPackOne::Dirt;

SYNC_MODEL(modelDirt, "Dirt");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Dirt]") {
	DirtModule* m = Test::createModule<DirtModule>("Dirt");
	DirtWidget* mw = Test::createWidget<DirtWidget>("Dirt");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Dirt][JSON]") {
	auto module = Test::createModule<DirtModule>("Dirt");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}