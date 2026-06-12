#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Stroke.cpp"

using namespace StoermelderPackOne::Stroke;

SYNC_MODEL(modelStroke, "Stroke");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Stroke]") {
	StrokeModule<10>* m = Test::createModule<StrokeModule<10>>("Stroke");
	StrokeWidget* mw = Test::createWidget<StrokeWidget>("Stroke");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Stroke][JSON]") {
	auto module = Test::createModule<StrokeModule<10>>("Stroke");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}