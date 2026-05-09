#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "EightFaceMk2.cpp"
#include "EightFaceMk2Ex.cpp"

using namespace StoermelderPackOne::EightFaceMk2;

SYNC_MODEL(modelEightFaceMk2, "EightFaceMk2");
SYNC_MODEL(modelEightFaceMk2Ex, "EightFaceMk2Ex");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[EightFaceMk2]") {
	EightFaceMk2Module<8>* m = Test::createModule<EightFaceMk2Module<8>>("EightFaceMk2");
	EightFaceMk2Widget<8>* mw = Test::createWidget<EightFaceMk2Widget<8>>("EightFaceMk2");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("EightFaceMk2Ex Construction and initialization", "[EightFaceMk2]") {
	EightFaceMk2ExModule<8>* m = Test::createModule<EightFaceMk2ExModule<8>>("EightFaceMk2Ex");
	EightFaceMk2ExWidget<8>* mw = Test::createWidget<EightFaceMk2ExWidget<8>>("EightFaceMk2Ex");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}