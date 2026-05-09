#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Stroke.cpp"

using namespace StoermelderPackOne::Stroke;

SYNC_MODEL(modelStroke, "Stroke");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Stroke]") {
	StrokeModule<10>* m = Test::createModule<StrokeModule<10>>("Stroke");
	StrokeWidget* mw = Test::createWidget<StrokeWidget>("Stroke");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}