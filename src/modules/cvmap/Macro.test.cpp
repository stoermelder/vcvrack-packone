#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Macro.cpp"

using namespace StoermelderPackOne::Macro;

SYNC_MODEL(modelMacro, "Macro");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Macro]") {
	MacroModule* m = Test::createModule<MacroModule>("Macro");
	MacroWidget* mw = Test::createWidget<MacroWidget>("Macro");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Macro][JSON]") {
	auto module = Test::createModule<MacroModule>("Macro");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}