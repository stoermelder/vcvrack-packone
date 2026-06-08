#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "ReMove.cpp"

using namespace StoermelderPackOne::ReMove;

SYNC_MODEL(modelReMoveLite, "ReMoveLite");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[ReMove]") {
	ReMoveModule* m = Test::createModule<ReMoveModule>("ReMoveLite");
	ReMoveWidget* mw = Test::createWidget<ReMoveWidget>("ReMoveLite");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[ReMove][JSON]") {
	auto module = Test::createModule<ReMoveModule>("ReMoveLite");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}