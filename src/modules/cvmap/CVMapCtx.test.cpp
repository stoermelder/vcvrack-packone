#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVMapCtx.cpp"

using namespace StoermelderPackOne::CVMap;

SYNC_MODEL(modelCVMapCtx, "CVMapCtx");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMapCtx]") {
	CVMapCtxModule* m = Test::createModule<CVMapCtxModule>("CVMapCtx");
	CVMapCtxWidget* mw = Test::createWidget<CVMapCtxWidget>("CVMapCtx");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}