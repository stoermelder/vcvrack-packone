#include "../../test/framework.hpp"
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

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves state", "[Mirror][JSON]") {
	MirrorModule* m = Test::createModule<MirrorModule>("Mirror");

	m->targetModuleIds = {11, 22, 33};
	// Distinctive paramId on EVERY CV input
	for (int i = 0; i < 8; i++) {
		m->cvParamId[i] = 10 + i;
	}

	json_t* j = m->dataToJson();

	MirrorModule* m2 = Test::createModule<MirrorModule>("Mirror");
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->targetModuleIds.size() == 3);
	REQUIRE(m2->targetModuleIds[0] == 11);
	REQUIRE(m2->targetModuleIds[1] == 22);
	REQUIRE(m2->targetModuleIds[2] == 33);

	for (int i = 0; i < 8; i++) {
		REQUIRE(m2->cvParamId[i] == 10 + i);
	}

	Test::destroyModule(m);
	Test::destroyModule(m2);
}