#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Dirt.cpp"

using namespace StoermelderPackOne::Dirt;

SYNC_MODEL(modelDirt, "Dirt");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Dirt]") {
	DirtModule* m = Test::createModule<DirtModule>("Dirt");
	DirtWidget* mw = Test::createWidget<DirtWidget>("Dirt");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}