#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Spin.cpp"

using namespace StoermelderPackOne::Spin;

SYNC_MODEL(modelSpin, "Spin");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Spin]") {
	SpinModule* m = Test::createModule<SpinModule>("Spin");
	SpinWidget* mw = Test::createWidget<SpinWidget>("Spin");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}