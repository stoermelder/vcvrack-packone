#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVMap.cpp"

using namespace StoermelderPackOne::CVMap;

SYNC_MODEL(modelCVMap, "CVMap");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMap]") {
	CVMapModule* m = Test::createModule<CVMapModule>("CVMap");
	CVMapWidget* mw = Test::createWidget<CVMapWidget>("CVMap");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}