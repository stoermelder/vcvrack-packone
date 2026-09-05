#include "../../test/framework.hpp"
#include "EightFace.cpp"

using namespace StoermelderPackOne::EightFace;

SYNC_MODEL(modelEightFace, "EightFace");
SYNC_MODEL(modelEightFaceX2, "EightFaceX2");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[EightFace]") {
	Test::ModuleScaffold<EightFaceModule<8>> mods;
	EightFaceModule<8>* m = mods.create("EightFace");
	EightFaceWidget* mw = Test::createWidget<EightFaceWidget>("EightFace");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[EightFace][JSON]") {
	Test::ModuleScaffold<EightFaceModule<8>> mods;
	auto module = mods.create("EightFace");

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

TEST_CASE("EightFaceX2 Construction and initialization", "[EightFace]") {
	Test::ModuleScaffold<EightFaceModule<16>> mods;
	EightFaceModule<16>* m = mods.create("EightFaceX2");
	EightFaceX2Widget* mw = Test::createWidget<EightFaceX2Widget>("EightFaceX2");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("JSON round-trip preserves state", "[EightFace][JSON]") {
	Test::ModuleScaffold<EightFaceModule<8>> mods;
	EightFaceModule<8>* m = mods.create("EightFace");

	SECTION("Scalars") {
		// Distinct, non-default values for every scalar stored to JSON
		m->panelTheme = 1;
		m->guiSafeMode = GUISAFEMODE::GUI;
		m->side = SIDE::RIGHT;
		m->pluginSlug = "Stoermelder-P1";
		m->modelSlug = "Glue";
		m->realPluginSlug = "Stoermelder-P1";
		m->realModelSlug = "Glue";
		m->moduleName = "Stoermelder Glue";
		m->slotCvMode = SLOTCVMODE::TRIG_PINGPONG;
		m->preset = 3;
		m->presetCount = 6;
		m->presetCountLongPress = false;

		json_t* rootJ = m->dataToJson();
		REQUIRE(rootJ != nullptr);

		auto restored = mods.create("EightFace");
		restored->dataFromJson(rootJ);

		REQUIRE(restored->panelTheme == 1);
		REQUIRE(restored->guiSafeMode == GUISAFEMODE::GUI);
		REQUIRE(restored->side == SIDE::RIGHT);
		REQUIRE(restored->pluginSlug == "Stoermelder-P1");
		REQUIRE(restored->modelSlug == "Glue");
		REQUIRE(restored->realPluginSlug == "Stoermelder-P1");
		REQUIRE(restored->realModelSlug == "Glue");
		REQUIRE(restored->moduleName == "Stoermelder Glue");
		REQUIRE(restored->slotCvMode == SLOTCVMODE::TRIG_PINGPONG);
		REQUIRE(restored->preset == 3);
		REQUIRE(restored->presetCount == 6);
		REQUIRE(restored->presetCountLongPress == false);

		json_decref(rootJ);
	}

	SECTION("Presets array with nested slot objects") {
		// Populate two slots with representative payloads, leave the rest unused.
		auto makeSlotJson = [](int id) {
			json_t* slotJ = json_object();
			json_object_set_new(slotJ, "id", json_integer(id));
			json_object_set_new(slotJ, "leftModuleId", json_integer(-1));
			json_object_set_new(slotJ, "rightModuleId", json_integer(-1));
			return slotJ;
		};
		m->presetSlotUsed[0] = true;
		m->presetSlot[0] = makeSlotJson(101);
		m->presetSlotUsed[5] = true;
		m->presetSlot[5] = makeSlotJson(105);

		json_t* rootJ = m->dataToJson();
		REQUIRE(rootJ != nullptr);

		// Serialization: one entry per slot, payload only for used slots
		json_t* presetsJ = json_object_get(rootJ, "presets");
		REQUIRE(json_is_array(presetsJ));
		REQUIRE(json_array_size(presetsJ) == 8);
		REQUIRE(json_is_true(json_object_get(json_array_get(presetsJ, 0), "slotUsed")));
		REQUIRE(json_object_get(json_array_get(presetsJ, 0), "slot") != nullptr);
		REQUIRE(json_is_false(json_object_get(json_array_get(presetsJ, 1), "slotUsed")));
		REQUIRE(json_object_get(json_array_get(presetsJ, 1), "slot") == nullptr);

		auto restored = mods.create("EightFace");
		restored->dataFromJson(rootJ);

		// Every slot restores its used-flag; payloads come back as deep copies
		for (int i = 0; i < restored->presetMax; i++) {
			CATCH_INFO("Slot " << i);
			bool expectedUsed = (i == 0 || i == 5);
			REQUIRE(restored->presetSlotUsed[i] == expectedUsed);
			if (expectedUsed) {
				REQUIRE(restored->presetSlot[i] != m->presetSlot[i]);
				REQUIRE(json_equal(restored->presetSlot[i], m->presetSlot[i]) == 1);
			}
			else {
				REQUIRE(restored->presetSlot[i] == nullptr);
			}
		}

		json_decref(rootJ);
	}

}


TEST_CASE("dataFromJson tolerates an oversized presets array", "[EightFace][JSON]") {
	Test::ModuleScaffold<EightFaceModule<8>> mods;
	// A preset written by a build with more slots (or a hand-edited patch) must
	// not write past the fixed-size presetSlotUsed[]/presetSlot[] arrays.
	EightFaceModule<8>* m = mods.create("EightFace");

	json_t* rootJ = json_object();
	json_t* presetsJ = json_array();
	for (int i = 0; i < 20; i++) {
		json_t* itemJ = json_object();
		// The first (in-bounds) and all out-of-bounds entries claim slots
		bool used = (i == 0) || (i >= 8);
		json_object_set_new(itemJ, "slotUsed", json_boolean(used));
		if (used) {
			json_t* slotJ = json_object();
			json_object_set_new(slotJ, "marker", json_integer(i));
			json_object_set_new(itemJ, "slot", slotJ);
		}
		json_array_append_new(presetsJ, itemJ);
	}
	json_object_set_new(rootJ, "presets", presetsJ);

	REQUIRE_NOTHROW(m->dataFromJson(rootJ));

	// The in-bounds entry applied normally and kept its exact payload; the
	// out-of-bounds entries were dropped rather than smashing slot 0's storage
	// (before the bound check, presetSlotUsed[8] aliased the first byte of
	// presetSlot[0], corrupting the pointer).
	REQUIRE(m->presetSlotUsed[0] == true);
	REQUIRE(json_integer_value(json_object_get(m->presetSlot[0], "marker")) == 0);
	for (int i = 1; i < m->presetMax; i++) {
		REQUIRE(m->presetSlotUsed[i] == false);
	}

	json_decref(rootJ);
}