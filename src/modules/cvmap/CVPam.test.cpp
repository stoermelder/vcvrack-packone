#include "../../test/framework.hpp"
#include "CVPam.cpp"

using namespace StoermelderPackOne::CVPam;

SYNC_MODEL(modelCVPam, "CVPam");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[CVPam]") {
	Test::ModuleScaffold<CVPamModule> mods;
	CVPamModule* m = mods.create("CVPam");
	CVPamWidget* mw = Test::createWidget<CVPamWidget>("CVPam");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[CVPam][JSON]") {
	Test::ModuleScaffold<CVPamModule> mods;
	auto module = mods.create("CVPam");

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

}

TEST_CASE("JSON round-trip preserves state", "[CVPam][JSON]") {
	Test::ModuleScaffold<CVPamModule> mods;
	Test::ModuleScaffold<rack::Module> targetMods;
	CVPamModule* m = mods.create("CVPam");
	CVPamModule* m2 = mods.create("CVPam");

	SECTION("Scalar settings round-trip") {
		// Distinct, non-default values for every CVPam-specific scalar stored to JSON
		m->panelTheme = 1;
		m->bipolarOutput = true;  // default false
		m->audioRate = false;     // default true
		m->locked = true;         // default false

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->bipolarOutput == true);
		REQUIRE(m2->audioRate == false);
		REQUIRE(m2->locked == true);
	}

	SECTION("Mapping slots (maps array) round-trip") {
		// A registered target module is required for moduleId/paramId to persist through
		// updateParamHandle(): the engine resolves the module by id, so an unregistered
		// module would leave moduleId at -1 and the mapping could not round-trip.
		rack::Module* target = targetMods.create("Glue");
		Test::registerModule(target);

		// The target must be registered so the engine can resolve it by id
		REQUIRE(APP->engine->getModule(target->id) == target);

		// Map three slots to distinctive paramIds on the target module
		m->learnParam(0, target->id, 0);
		m->learnParam(1, target->id, 2);
		m->learnParam(2, target->id, 4);

		// learnParam must persist the mapping on m before serialization
		REQUIRE(m->paramHandles[0].moduleId == target->id);
		REQUIRE(m->paramHandles[0].paramId == 0);

		json_t* j = m->dataToJson();
		// The maps array must be serialized with one entry per map slot
		json_t* mapsJ = json_object_get(j, "maps");
		REQUIRE(mapsJ != nullptr);
		REQUIRE(json_array_size(mapsJ) == (size_t) m->mapLen);

		// Rack allows only ONE ParamHandle per (moduleId, paramId): with
		// overwrite=false, updateParamHandle_NoLock() resets the *new* handle
		// when another handle still claims the param. Release m's claims so m2
		// can take them — mirrors a real preset load, where the old module
		// instance (and its handles) is destroyed before dataFromJson() runs.
		int mapLen = m->mapLen;
		m->clearMaps_WithLock();

		m2->dataFromJson(j);
		json_decref(j);

		// Mapped slots must round-trip moduleId and paramId exactly
		REQUIRE(m2->paramHandles[0].moduleId == target->id);
		REQUIRE(m2->paramHandles[0].paramId == 0);
		REQUIRE(m2->paramHandles[1].moduleId == target->id);
		REQUIRE(m2->paramHandles[1].paramId == 2);
		REQUIRE(m2->paramHandles[2].moduleId == target->id);
		REQUIRE(m2->paramHandles[2].paramId == 4);

		// mapLen (derived from the last mapped slot) must round-trip
		REQUIRE(m2->mapLen == mapLen);
		// Unmapped slots stay unmapped
		REQUIRE(m2->paramHandles[3].moduleId == -1);

		Test::unregisterModule(target);
	}

}