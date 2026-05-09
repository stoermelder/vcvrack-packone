#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Glue.cpp"

using namespace StoermelderPackOne::Glue;

SYNC_MODEL(modelGlue, "Glue");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Glue]") {
	GlueModule* m = Test::createModule<GlueModule>("Glue");
	GlueWidget* mw = Test::createWidget<GlueWidget>("Glue");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}