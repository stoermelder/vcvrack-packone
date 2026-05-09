#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "EightFace.cpp"

using namespace StoermelderPackOne::EightFace;

SYNC_MODEL(modelEightFace, "EightFace");
SYNC_MODEL(modelEightFaceX2, "EightFaceX2");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[EightFace]") {
	EightFaceModule<8>* m = Test::createModule<EightFaceModule<8>>("EightFace");
	EightFaceWidget* mw = Test::createWidget<EightFaceWidget>("EightFace");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("EightFaceX2 Construction and initialization", "[EightFace]") {
	EightFaceModule<16>* m = Test::createModule<EightFaceModule<16>>("EightFaceX2");
	EightFaceX2Widget* mw = Test::createWidget<EightFaceX2Widget>("EightFaceX2");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}