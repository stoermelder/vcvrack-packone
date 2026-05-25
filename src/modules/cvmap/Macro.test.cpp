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