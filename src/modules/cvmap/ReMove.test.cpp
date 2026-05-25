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