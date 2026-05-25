#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Sail.cpp"

using namespace StoermelderPackOne::Sail;

SYNC_MODEL(modelSail, "Sail");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Sail]") {
	SailModule* m = Test::createModule<SailModule>("Sail");
	SailWidget* mw = Test::createWidget<SailWidget>("Sail");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}