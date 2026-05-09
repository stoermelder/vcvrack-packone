#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Strip.cpp"

using namespace StoermelderPackOne::Strip;

SYNC_MODEL(modelStrip, "Strip");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Strip]") {
	StripModule* m = Test::createModule<StripModule>("Strip");
	StripWidget* mw = Test::createWidget<StripWidget>("Strip");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}