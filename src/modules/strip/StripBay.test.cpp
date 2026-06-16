#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "StripBay.cpp"

using namespace StoermelderPackOne::StripBay;

SYNC_MODEL(modelStripBay4, "StripBay4");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[StripBay]") {
	StripBayModule<4>* m = Test::createModule<StripBayModule<4>>("StripBay4");
	StripBay4Widget* mw = Test::createWidget<StripBay4Widget>("StripBay4");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[StripBay][JSON]") {
	auto module = Test::createModule<StripBayModule<4>>("StripBay4");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}