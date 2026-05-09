#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "CVMapMicro.cpp"

using namespace StoermelderPackOne::CVMapMicro;

SYNC_MODEL(modelCVMapMicro, "CVMapMicro");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVMapMicro]") {
	CVMapMicroModule* m = Test::createModule<CVMapMicroModule>("CVMapMicro");
	CVMapMicroWidget* mw = Test::createWidget<CVMapMicroWidget>("CVMapMicro");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}