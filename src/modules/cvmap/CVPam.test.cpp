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
		// Harness::addModule registers the target with the engine, which a mapping needs:
		// updateParamHandle() resolves the target by id, so an unregistered module would
		// leave moduleId at -1 and the mapping could not round-trip. See the mapping section
		// in test_harness.hpp.
		Test::Harness h;
		rack::Module* target = h.addModule<rack::Module>("Glue");

		// Map three slots to distinctive paramIds on the target module
		m->learnParam(0, target->id, 0);
		m->learnParam(1, target->id, 2);
		m->learnParam(2, target->id, 4);

		// learnParam must persist the mapping on m before serialization. requireMapped also
		// checks handle->module, which a mapping onto an unregistered target silently leaves
		// null while moduleId still looks right.
		h.requireMapped(&m->paramHandles[0], target, 0);

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

		// Mapped slots must round-trip moduleId and paramId exactly — and resolve to the live
		// target, which is what makes the reloaded mapping actually drive anything.
		h.requireMapped(&m2->paramHandles[0], target, 0);
		h.requireMapped(&m2->paramHandles[1], target, 2);
		h.requireMapped(&m2->paramHandles[2], target, 4);

		// mapLen (derived from the last mapped slot) must round-trip
		REQUIRE(m2->mapLen == mapLen);
		// Unmapped slots stay unmapped
		h.requireUnmapped(&m2->paramHandles[3]);
	}

}