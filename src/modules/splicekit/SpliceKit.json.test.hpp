// SpliceKit.json.test.cpp — preset (JSON) serialization.
// Tests dataToJson/dataFromJson roundtrips plus dataFromJson robustness
// against malformed, missing or out-of-range fields.

#include "SpliceKit.test.hpp"


TEST_CASE("Preset JSON null-guards", "[SpliceKit][JSON]") {
	auto module = createModule();

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


TEST_CASE("JSON roundtrip preserves scene and button mode", "[SpliceKit]") {
	SpliceKitModule* m = createModule();

	m->sceneStore.current = 3;
	m->buttonMode = SpliceKitModule::BUTTON_MOMENTARY;
	m->overlayEnabled = false;
	m->sceneStore.setConnection(3, 1, 5, true);

	json_t* j = m->dataToJson();

	SpliceKitModule* m2 = createModule();
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->sceneStore.current == 3);
	REQUIRE(m2->buttonMode == SpliceKitModule::BUTTON_MOMENTARY);
	REQUIRE(m2->overlayEnabled == false);
	REQUIRE(m2->sceneStore.isConnected(3, 1, 5) == true);
	REQUIRE(m2->sceneStore.isConnected(3, 5, 1) == true);  // symmetric
	REQUIRE(m2->sceneStore.isConnected(0, 1, 5) == false);  // other scene untouched

	Test::destroyModule(m2);
	Test::destroyModule(m);
}


TEST_CASE("JSON roundtrip preserves MIDI maps", "[SpliceKit]") {
	SpliceKitModule* m = createModule();

	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 36);
	m->trackingProcessor.setMap(MidiTrackingType::CC, 1, 74);

	json_t* j = m->dataToJson();

	SpliceKitModule* m2 = createModule();
	m2->dataFromJson(j);
	json_decref(j);

	auto map0 = m2->trackingProcessor.getMap(0);
	REQUIRE(map0.type == MidiTrackingType::NOTE);
	REQUIRE(map0.param == 36);

	auto map1 = m2->trackingProcessor.getMap(1);
	REQUIRE(map1.type == MidiTrackingType::CC);
	REQUIRE(map1.param == 74);

	Test::destroyModule(m2);
	Test::destroyModule(m);
}


// JSON roundtrip — additional stored properties

TEST_CASE("JSON roundtrip preserves crossInstanceEnabled", "[SpliceKit][JSON]") {
	SpliceKitModule* m = createModule();
	m->crossInstanceEnabled = false;
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = createModule();
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->crossInstanceEnabled == false);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip preserves sceneLinkMasterId", "[SpliceKit][JSON]") {
	SpliceKitModule* m = createModule();
	m->sceneLinkMasterId = 17;
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = createModule();
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->sceneLinkMasterId == 17);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip: missing sceneLinkMasterId key defaults to -1", "[SpliceKit][JSON]") {
	SpliceKitModule* m = createModule();
	json_t* j = json_object();  // no "sceneLinkMasterId" key at all
	m->sceneLinkMasterId = 5;   // pre-existing value must be overwritten, not left stale
	REQUIRE_NOTHROW(m->dataFromJson(j));
	json_decref(j);
	REQUIRE(m->sceneLinkMasterId == -1);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip preserves cellColorSet overrides", "[SpliceKit][JSON]") {
	SpliceKitModule* m = createModule();
	m->cellColorSet[3] = 2;  // orange
	m->cellColorSet[10] = 0;  // red (explicit, not auto)
	m->cellColorSet[20] = -1; // auto (must not be written to JSON)
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = createModule();
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->cellColorSet[3] == 2);
	REQUIRE(m2->cellColorSet[10] == 0);
	REQUIRE(m2->cellColorSet[20] == -1);
	// Untouched cells are still auto
	REQUIRE(m2->cellColorSet[0] == -1);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip preserves cellLabels", "[SpliceKit][JSON]") {
	SpliceKitModule* m = createModule();
	m->cellLabels[5] = "VCO Out";
	m->cellLabels[12] = "Filter In";
	// Empty entries are not written, so the destination must remain empty
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = createModule();
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->cellLabels[5] == "VCO Out");
	REQUIRE(m2->cellLabels[12] == "Filter In");
	REQUIRE(m2->cellLabels[0].empty());
	Test::destroyModule(m2);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip - panelTheme and currentScene survive a full save/load", "[SpliceKit][JSON]") {
	SpliceKitModule* m = createModule();
	m->panelTheme = 1;  // dark
	m->sceneStore.current = 5;
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = createModule();
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->panelTheme == 1);
	REQUIRE(m2->sceneStore.current == 5);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}


// dataFromJson — invalid activePreset string falls back to no preset

TEST_CASE("dataFromJson - malformed activePreset JSON reverts to no preset", "[SpliceKit][JSON]") {
	json_t* j = json_object();
	json_object_set_new(j, "activePreset", json_string("this is { not valid json"));

	SpliceKitModule* m = createModule();
	m->dataFromJson(j);
	json_decref(j);
	REQUIRE(m->feedback.getActivePreset() == nullptr);
	Test::destroyModule(m);
}

TEST_CASE("dataFromJson - non-string activePreset value does not crash and reverts to no preset", "[SpliceKit][JSON]") {
	json_t* j = json_object();
	json_object_set_new(j, "activePreset", json_integer(42));  // not a string

	SpliceKitModule* m = createModule();
	REQUIRE_NOTHROW(m->dataFromJson(j));
	json_decref(j);
	REQUIRE(m->feedback.getActivePreset() == nullptr);
	Test::destroyModule(m);
}

TEST_CASE("dataFromJson - non-string cellLabels entry does not crash and is skipped", "[SpliceKit][JSON]") {
	json_t* labelsJ = json_object();
	json_object_set_new(labelsJ, "3", json_integer(7));       // not a string — must be skipped
	json_object_set_new(labelsJ, "5", json_string("kept"));   // valid — must be applied
	json_t* j = json_object();
	json_object_set_new(j, "cellLabels", labelsJ);

	SpliceKitModule* m = createModule();
	m->cellLabels[3] = "stale";
	REQUIRE_NOTHROW(m->dataFromJson(j));
	json_decref(j);
	REQUIRE(m->cellLabels[3] == "");
	REQUIRE(m->cellLabels[5] == "kept");
	Test::destroyModule(m);
}


TEST_CASE("dataFromJson - out-of-range currentScene is clamped into bounds", "[SpliceKit][JSON]") {
	// currentScene indexes sceneConnections[SCENE_COUNT][MATRIX_COUNT] directly in
	// captureScene/switchScene/randomizeCurrentScene, so an unchecked value from a corrupted
	// or hand-edited patch would read and write far outside the array.
	auto load = [](json_int_t v) {
		SpliceKitModule* m = createModule();
		json_t* j = json_object();
		json_object_set_new(j, "currentScene", json_integer(v));
		m->dataFromJson(j);
		json_decref(j);
		int result = m->sceneStore.current;
		Test::destroyModule(m);
		return result;
	};

	REQUIRE(load(99) == SCENE_COUNT - 1);            // far above the top
	REQUIRE(load(SCENE_COUNT) == SCENE_COUNT - 1);   // one past the top
	REQUIRE(load(-1) == 0);                          // negative
	REQUIRE(load(SCENE_COUNT - 1) == SCENE_COUNT - 1);  // top of range survives unchanged
	REQUIRE(load(0) == 0);
	REQUIRE(load(3) == 3);                           // ordinary value is untouched
}

TEST_CASE("dataFromJson - clamped currentScene leaves scene state safely addressable", "[SpliceKit][JSON]") {
	SpliceKitModule* m = createModule();
	json_t* j = json_object();
	json_object_set_new(j, "currentScene", json_integer(99));
	m->dataFromJson(j);
	json_decref(j);

	// The whole point of the clamp: these operations index sceneConnections[currentScene]
	// and must stay in bounds after loading a malformed patch.
	REQUIRE_NOTHROW(m->sceneStore.setConnection(m->sceneStore.current, 0, 1, true));
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1));
	REQUIRE_NOTHROW(m->sceneStore.removeCellConnections(0));
	REQUIRE(m->sceneStore.connections[m->sceneStore.current][0] == 0);

	Test::destroyModule(m);
}


// dataFromJson — index guards on the per-cell/per-scene maps

TEST_CASE("dataFromJson - out-of-range port indices are skipped", "[SpliceKit][JSON]") {
	json_t* portsJ = json_object();
	for (const char* key : {"-1", "64", "999"}) {
		json_t* p = json_object();
		json_object_set_new(p, "moduleId", json_integer(42));
		json_object_set_new(p, "type", json_integer((int)engine::Port::OUTPUT));
		json_object_set_new(p, "portId", json_integer(0));
		json_object_set_new(portsJ, key, p);
	}
	json_t* valid = json_object();
	json_object_set_new(valid, "moduleId", json_integer(77));
	json_object_set_new(valid, "type", json_integer((int)engine::Port::INPUT));
	json_object_set_new(valid, "portId", json_integer(2));
	json_object_set_new(portsJ, "5", valid);

	json_t* j = json_object();
	json_object_set_new(j, "ports", portsJ);

	SpliceKitModule* m = createModule();
	REQUIRE_NOTHROW(m->dataFromJson(j));
	json_decref(j);

	// Only the in-range entry was applied.
	REQUIRE(m->portAssignments[5].moduleId == 77);
	REQUIRE(m->portAssignments[5].portId == 2);
	int assigned = 0;
	for (int i = 0; i < MATRIX_COUNT; i++) if (m->portAssignments[i].isValid()) assigned++;
	REQUIRE(assigned == 1);

	// NOTE: this asserts the *observable* outcome only. Removing the bounds check in
	// dataFromJson would write past portAssignments (index 64 lands on sceneConnections,
	// 1024 bytes past the base) without failing here, because dataFromJson memsets
	// sceneConnections after the ports loop, erasing the clobber before it can be read.
	// Detecting the stray write itself needs ASan, not an in-process assertion.

	Test::destroyModule(m);
}

TEST_CASE("dataFromJson - out-of-range scene and connection indices are skipped", "[SpliceKit][JSON]") {
	auto makePair = [](int a, int b) {
		json_t* pair = json_array();
		json_array_append_new(pair, json_integer(a));
		json_array_append_new(pair, json_integer(b));
		return pair;
	};

	json_t* scenesJ = json_object();
	// Out-of-range scene key — must be skipped wholesale.
	json_t* badScene = json_object();
	json_t* badConns = json_array();
	json_array_append_new(badConns, makePair(0, 1));
	json_object_set_new(badScene, "connections", badConns);
	json_object_set_new(scenesJ, "99", badScene);

	// Valid scene holding one out-of-range pair and one valid pair.
	json_t* okScene = json_object();
	json_t* okConns = json_array();
	json_array_append_new(okConns, makePair(0, MATRIX_COUNT));  // b out of range
	json_array_append_new(okConns, makePair(-1, 3));            // a out of range
	json_array_append_new(okConns, makePair(2, 4));             // valid
	json_object_set_new(okScene, "connections", okConns);
	json_object_set_new(scenesJ, "1", okScene);

	json_t* j = json_object();
	json_object_set_new(j, "scenes", scenesJ);

	SpliceKitModule* m = createModule();
	REQUIRE_NOTHROW(m->dataFromJson(j));
	json_decref(j);

	REQUIRE(m->sceneStore.isConnected(1, 2, 4));
	REQUIRE(m->sceneStore.connections[1][0] == 0);
	REQUIRE(m->sceneStore.connections[1][3] == 0);

	Test::destroyModule(m);
}

TEST_CASE("dataFromJson - out-of-range MIDI map indices are skipped", "[SpliceKit][JSON]") {
	json_t* mapsJ = json_object();
	for (const char* key : {"-1", "72", "999"}) {
		json_t* mapJ = json_object();
		json_object_set_new(mapJ, "type", json_integer((int)MidiTrackingType::NOTE));
		json_object_set_new(mapJ, "param", json_integer(60));
		json_object_set_new(mapsJ, key, mapJ);
	}
	// TOTAL_MAPS - 1 is the last valid slot (a scene button).
	json_t* okMap = json_object();
	json_object_set_new(okMap, "type", json_integer((int)MidiTrackingType::CC));
	json_object_set_new(okMap, "param", json_integer(7));
	json_object_set_new(mapsJ, std::to_string(TOTAL_MAPS - 1).c_str(), okMap);

	json_t* j = json_object();
	json_object_set_new(j, "maps", mapsJ);

	SpliceKitModule* m = createModule();
	REQUIRE_NOTHROW(m->dataFromJson(j));
	json_decref(j);

	auto last = m->trackingProcessor.getMap(TOTAL_MAPS - 1);
	REQUIRE(last.type == MidiTrackingType::CC);
	REQUIRE(last.param == 7);

	Test::destroyModule(m);
}



// MidiOutPreset toJson/fromJson roundtrip. The module-level activePreset *string*
// roundtrip was already covered; these pin the preset file format itself (cells/scenes/specs
// structure, slotsBlockToJson/parseSlotsBlock) and its documented edge cases: empty slot
// blocks omitted, block type from the first mapped slot, and note/CC 0 preserved.

static void requirePresetEqual(const MidiOutPreset& a, const MidiOutPreset& b) {
	REQUIRE(a.name == b.name);
	REQUIRE(a.description == b.description);
	for (int i = 0; i < MATRIX_COUNT; i++) {
		REQUIRE(a.cells[i].type == b.cells[i].type);
		REQUIRE(a.cells[i].number == b.cells[i].number);
	}
	for (int i = 0; i < SCENE_COUNT; i++) {
		REQUIRE(a.scenes[i].type == b.scenes[i].type);
		REQUIRE(a.scenes[i].number == b.scenes[i].number);
	}
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		REQUIRE(a.specs[s].type == b.specs[s].type);
		REQUIRE(a.specs[s].channel == b.specs[s].channel);
		REQUIRE(a.specs[s].noteMode == b.specs[s].noteMode);
		REQUIRE(a.specs[s].note == b.specs[s].note);
		REQUIRE(a.specs[s].value == b.specs[s].value);
	}
}

static MidiOutPreset roundtripPreset(const MidiOutPreset& src) {
	json_t* j = src.toJson();
	MidiOutPreset dst;
	dst.fromJson(j);
	json_decref(j);
	return dst;
}

TEST_CASE("MidiOutPreset roundtrip - preserves a fully populated preset", "[SpliceKit][JSON]") {
	MidiOutPreset src;
	src.name = "Roundtrip";
	src.description = "a description";
	for (int i = 0; i < MATRIX_COUNT; i++) src.cells[i] = {MidiTrackingType::NOTE, i};
	for (int i = 0; i < SCENE_COUNT; i++) src.scenes[i] = {MidiTrackingType::CC, i};
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		src.specs[s].type = (s % 2) ? MIDI_OUT_NOTE_ON : MIDI_OUT_CC;
		src.specs[s].channel = s % 16;
		src.specs[s].noteMode = MIDI_OUT_FIXED;
		src.specs[s].note = s;
		src.specs[s].value = 127 - s;
	}

	requirePresetEqual(src, roundtripPreset(src));
}

TEST_CASE("MidiOutPreset roundtrip - every shipped preset roundtrips unchanged", "[SpliceKit][JSON]") {
	// The shipped files use either no slot block (Generic) or a complete one (all cells
	// listed), so toJson → fromJson must reproduce each one exactly.
	const LoadedPreset lp = GENERATE(Catch::Generators::from_range(getLoadedPresets()));
	requirePresetEqual(lp.preset, roundtripPreset(lp.preset));
}

TEST_CASE("MidiOutPreset roundtrip - empty slot blocks are omitted from JSON", "[SpliceKit][JSON]") {
	MidiOutPreset src;
	src.name = "Specs only";
	// No cells/scenes mapped — only specs.

	json_t* j = src.toJson();
	// slotsBlockToJson returns nullptr for an unmapped block, so the key is omitted.
	REQUIRE(json_object_get(j, "cells") == nullptr);
	REQUIRE(json_object_get(j, "scenes") == nullptr);
	MidiOutPreset dst;
	dst.fromJson(j);
	json_decref(j);

	REQUIRE(dst.hasLayout() == false);
	for (int i = 0; i < MATRIX_COUNT; i++) {
		REQUIRE(dst.cells[i].type == MidiTrackingType::NONE);
	}
	for (int i = 0; i < SCENE_COUNT; i++) {
		REQUIRE(dst.scenes[i].type == MidiTrackingType::NONE);
	}
}

TEST_CASE("MidiOutPreset roundtrip - block type comes from the first mapped slot; note/CC 0 preserved", "[SpliceKit][JSON]") {
	// A slot block carries one type for the whole block. cells[0] is the first mapped slot
	// (NOTE, number 0 — which must survive the roundtrip), cells[3] is CC but is overridden
	// by the block's type on re-parse, and the unmapped cells come back mapped to note 0.
	MidiOutPreset src;
	src.cells[0] = {MidiTrackingType::NOTE, 0};
	src.cells[3] = {MidiTrackingType::CC, 74};

	json_t* j = src.toJson();
	// The first mapped slot (cells[0], NOTE) determines the block's type.
	json_t* cellsJ = json_object_get(j, "cells");
	REQUIRE(cellsJ != nullptr);
	REQUIRE(json_integer_value(json_object_get(cellsJ, "type")) == (int)MidiTrackingType::NOTE);
	MidiOutPreset dst;
	dst.fromJson(j);
	json_decref(j);

	// Number 0 was preserved...
	REQUIRE(dst.cells[0].type == MidiTrackingType::NOTE);
	REQUIRE(dst.cells[0].number == 0);
	// ...cells[3]'s CC was overridden by the block's NOTE type...
	REQUIRE(dst.cells[3].type == MidiTrackingType::NOTE);
	REQUIRE(dst.cells[3].number == 74);
	// ...and the rest of the block came back mapped to note 0.
	REQUIRE(dst.cells[1].type == MidiTrackingType::NOTE);
	REQUIRE(dst.cells[1].number == 0);
	REQUIRE(dst.hasLayout());
}