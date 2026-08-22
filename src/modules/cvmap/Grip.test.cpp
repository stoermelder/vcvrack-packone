#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Grip.cpp"

using namespace StoermelderPackOne::Grip;

SYNC_MODEL(modelGrip, "Grip");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Grip]") {
	GripModule* m = Test::createModule<GripModule>("Grip");
	GripWidget* mw = Test::createWidget<GripWidget>("Grip");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Grip][JSON]") {
	auto module = Test::createModule<GripModule>("Grip");

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

	Test::destroyModule(module);
}