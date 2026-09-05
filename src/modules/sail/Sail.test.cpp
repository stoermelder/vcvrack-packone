#include "../../test/framework.hpp"
#include "Sail.cpp"

using namespace StoermelderPackOne::Sail;

SYNC_MODEL(modelSail, "Sail");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Sail]") {
	Test::ModuleScaffold<SailModule> mods;
	SailModule* m = mods.create("Sail");
	SailWidget* mw = Test::createWidget<SailWidget>("Sail");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Sail][JSON]") {
	Test::ModuleScaffold<SailModule> mods;
	auto module = mods.create("Sail");

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