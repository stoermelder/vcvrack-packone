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

TEST_CASE("Preset JSON null-guards", "[Mirror][JSON]") {
	auto module = Test::createModule<MirrorModule>("Mirror");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}