#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVPam.cpp"

using namespace StoermelderPackOne::CVPam;

SYNC_MODEL(modelCVPam, "CVPam");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVPam]") {
	CVPamModule* m = Test::createModule<CVPamModule>("CVPam");
	CVPamWidget* mw = Test::createWidget<CVPamWidget>("CVPam");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}