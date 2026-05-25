#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "StripPp.cpp"

using namespace StoermelderPackOne::Strip;

SYNC_MODEL(modelStripPp, "StripPp");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[StripPp]") {
	StripPpModule* m = Test::createModule<StripPpModule>("StripPp");
	StripPpWidget* mw = Test::createWidget<StripPpWidget>("StripPp");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}