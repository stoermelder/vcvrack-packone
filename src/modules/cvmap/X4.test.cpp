#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "X4.cpp"

using namespace StoermelderPackOne::X4;

SYNC_MODEL(modelX4, "X4");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[X4]") {
	X4Module* m = Test::createModule<X4Module>("X4");
	X4Widget* mw = Test::createWidget<X4Widget>("X4");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[X4][JSON]") {
	auto module = Test::createModule<X4Module>("X4");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}