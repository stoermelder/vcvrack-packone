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