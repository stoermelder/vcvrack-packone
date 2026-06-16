#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Spin.cpp"

using namespace StoermelderPackOne::Spin;

SYNC_MODEL(modelSpin, "Spin");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Spin]") {
	SpinModule* m = Test::createModule<SpinModule>("Spin");
	SpinWidget* mw = Test::createWidget<SpinWidget>("Spin");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Spin][JSON]") {
	auto module = Test::createModule<SpinModule>("Spin");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}