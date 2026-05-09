#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Mirror.cpp"

using namespace StoermelderPackOne::Mirror;

SYNC_MODEL(modelMirror, "Mirror");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Mirror]") {
	MirrorModule* m = Test::createModule<MirrorModule>("Mirror");
	MirrorWidget* mw = Test::createWidget<MirrorWidget>("Mirror");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}