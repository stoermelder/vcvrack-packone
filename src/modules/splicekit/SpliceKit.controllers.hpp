#pragma once
#include "../../plugin.hpp"
#include "../midi/MidiTrackingProcessor.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace SpliceKit {

static const int MATRIX_SIZE = 8;
static const int MATRIX_COUNT = MATRIX_SIZE * MATRIX_SIZE;

// Number of onboard scene slots. Currently equal to MATRIX_SIZE (the matrix's row/column
// count) but conceptually independent — kept as its own constant so scene-related code
// doesn't implicitly depend on the matrix's dimensions, which matter for an unrelated reason
// (SpliceKitVizOverlay::cellCenter() and the grid layout in SpliceKitWidget's constructor).
static const int SCENE_COUNT = 8;

// LED state identifiers — order must match the light-loop state assignment in SpliceKit.cpp.
// Color sets 0–3: dim then bright for each set, then connected per set.
//   default: set 0 = red (output), set 1 = blue (input), set 2 = orange, set 3 = green
enum {
	LED_STATE_OFF = 0,
	LED_STATE_COLOR0_DIM,    // color set 0, no cable
	LED_STATE_COLOR0,        // color set 0, cable present
	LED_STATE_COLOR1_DIM,    // color set 1, no cable
	LED_STATE_COLOR1,        // color set 1, cable present
	LED_STATE_COLOR2_DIM,    // color set 2, no cable
	LED_STATE_COLOR2,        // color set 2, cable present
	LED_STATE_COLOR3_DIM,    // color set 3, no cable
	LED_STATE_COLOR3,        // color set 3, cable present
	LED_STATE_PENDING,
	LED_STATE_PORT_LEARN,
	LED_STATE_MIDI_LEARN,
	LED_STATE_SCENE_ACTIVE,  // scene button: currently selected scene
	LED_STATE_SCENE_DIM,     // scene button: inactive but has stored connections
	LED_STATE_CONNECTED0,    // cell connected to the pending cell, color set 0
	LED_STATE_CONNECTED1,    // cell connected to the pending cell, color set 1
	LED_STATE_CONNECTED2,    // cell connected to the pending cell, color set 2
	LED_STATE_CONNECTED3,    // cell connected to the pending cell, color set 3
	LED_STATE_COUNT
};

// MIDI output message type.
// MIDI_OUT_FROM_SLOT_TYPE: derive status byte from the slot's own type (Note→NoteOn, CC→CC).
// Requires noteMode=from-slot; the slot type must be NOTE or CC.
enum MidiOutMsgType {
	MIDI_OUT_NONE = 0,
	MIDI_OUT_NOTE_ON,
	MIDI_OUT_NOTE_OFF,
	MIDI_OUT_CC,
	MIDI_OUT_FROM_SLOT_TYPE
};

// How the note/CC number is resolved when sending MIDI output for an LED state.
//   FROM_SLOT — use the button's current MIDI input mapping number (the note/CC
//               that was learned or applied from the preset layout).
//   FIXED     — use spec.note for every button, regardless of its mapping.
enum MidiOutNoteMode {
	MIDI_OUT_FROM_SLOT = 0,
	MIDI_OUT_FIXED
};

// One MIDI output message specification per LED state.
struct MidiOutSpec {
	MidiOutMsgType type = MIDI_OUT_NONE;
	int channel = 0;            // 0–15
	MidiOutNoteMode noteMode = MIDI_OUT_FROM_SLOT;
	int note = 0;               // used only with MIDI_OUT_FIXED
	int value = 0;              // velocity or CC value

	void fromJson(json_t* j) {
		if (!j) return;
		auto gi = [j](const char* k) { return (int)json_integer_value(json_object_get(j, k)); };
		type = (MidiOutMsgType) gi("type");
		channel = gi("channel");
		noteMode = (MidiOutNoteMode)gi("noteMode");
		note = gi("note");
		value = gi("value");
	}

	json_t* toJson() const {
		json_t* j = json_object();
		json_object_set_new(j, "type", json_integer(type));
		json_object_set_new(j, "channel", json_integer(channel));
		json_object_set_new(j, "noteMode", json_integer(noteMode));
		json_object_set_new(j, "note", json_integer(note));
		json_object_set_new(j, "value", json_integer(value));
		return j;
	}
};

// A single input slot: which MIDI message triggers this button.
// type==NONE means no auto-mapping; the user must learn manually.
struct MidiSlot {
	MidiTrackingType type = MidiTrackingType::NONE;
	int number = 0;  // note or CC number
};

// Parse a slot block from JSON:
//   { "type": <0|1|2>, "numbers": [ n0, n1, ... ] }
// type: 0=none, 1=note, 2=cc  (matches MidiTrackingType enum values)
static void parseSlotsBlock(json_t* j, MidiSlot* slots, int count) {
	if (!j) return;
	auto type = (MidiTrackingType)json_integer_value(json_object_get(j, "type"));
	json_t* numsJ = json_object_get(j, "numbers");
	if (!numsJ) return;
	int n = std::min((int)json_array_size(numsJ), count);
	for (int i = 0; i < n; i++) {
		slots[i].type = type;
		slots[i].number = (int)json_integer_value(json_array_get(numsJ, i));
	}
}

// Serialize a slot block to JSON, mirroring parseSlotsBlock(). All slots in a block share
// one MidiTrackingType (the layout format has no per-slot type), so the first mapped slot's
// type is used for the whole block; returns nullptr if no slot in the block is mapped.
static json_t* slotsBlockToJson(const MidiSlot* slots, int count) {
	MidiTrackingType type = MidiTrackingType::NONE;
	for (int i = 0; i < count; i++) {
		if (slots[i].type != MidiTrackingType::NONE) {
			type = slots[i].type;
			break;
		}
	}
	if (type == MidiTrackingType::NONE) return nullptr;

	json_t* j = json_object();
	json_object_set_new(j, "type", json_integer((int)type));
	json_t* numsJ = json_array();
	for (int i = 0; i < count; i++) {
		json_array_append_new(numsJ, json_integer(slots[i].number));
	}
	json_object_set_new(j, "numbers", numsJ);
	return j;
}

// LED-state keys, in LED_STATE_* order — shared by MidiOutPreset::fromJson/toJson.
static const char* const LED_STATE_KEYS[LED_STATE_COUNT] = {
	"off",
	"color0dim", "color0", "color1dim", "color1",
	"color2dim", "color2", "color3dim", "color3",
	"pending", "portLearn", "midiLearn",
	"sceneActive", "sceneDim",
	"connected0", "connected1", "connected2", "connected3"
};

// A named controller preset: per-button input slots + one output spec per LED state.
struct MidiOutPreset {
	std::string name;
	std::string description;             // optional free-text notes (layout/hardware references)
	MidiSlot cells[MATRIX_COUNT] = {};   // 64 matrix cell input slots
	MidiSlot scenes[SCENE_COUNT] = {};   // scene button input slots
	MidiOutSpec specs[LED_STATE_COUNT] = {};

	bool hasLayout() const {
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (cells[i].type != MidiTrackingType::NONE) return true;
		}
		for (int i = 0; i < SCENE_COUNT; i++) {
			if (scenes[i].type != MidiTrackingType::NONE) return true;
		}
		return false;
	}

	void fromJson(json_t* rootJ) {
		if (!rootJ) return;
		json_t* nameJ = json_object_get(rootJ, "name");
		if (nameJ) name = json_string_value(nameJ);
		json_t* descJ = json_object_get(rootJ, "description");
		if (descJ) description = json_string_value(descJ);

		parseSlotsBlock(json_object_get(rootJ, "cells"), cells, MATRIX_COUNT);
		parseSlotsBlock(json_object_get(rootJ, "scenes"), scenes, SCENE_COUNT);

		json_t* specsJ = json_object_get(rootJ, "specs");
		if (specsJ) {
			for (int i = 0; i < LED_STATE_COUNT; i++) {
				specs[i].fromJson(json_object_get(specsJ, LED_STATE_KEYS[i]));
			}
		}
	}

	json_t* toJson() const {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "name", json_string(name.c_str()));
		if (!description.empty()) {
			json_object_set_new(rootJ, "description", json_string(description.c_str()));
		}

		json_t* cellsJ = slotsBlockToJson(cells, MATRIX_COUNT);
		if (cellsJ) json_object_set_new(rootJ, "cells", cellsJ);
		json_t* scenesJ = slotsBlockToJson(scenes, SCENE_COUNT);
		if (scenesJ) json_object_set_new(rootJ, "scenes", scenesJ);

		json_t* specsJ = json_object();
		for (int i = 0; i < LED_STATE_COUNT; i++) {
			json_object_set_new(specsJ, LED_STATE_KEYS[i], specs[i].toJson());
		}
		json_object_set_new(rootJ, "specs", specsJ);
		return rootJ;
	}
};

// ---- Controller preset definitions -----------------------------------------
//
// Built-in presets are loaded from *.ctrl.json files in presets/SpliceKit/,
// sorted by filename (hence the "01-", "02-", ... numeric prefixes, which
// only control menu order). "No output" is not one of these files — it is
// the module's default state when no preset is selected; see
// SpliceKitModule::activePresetJson. Edit these files, or drop in new ones,
// to add/change built-in presets without recompiling.
//
// JSON fields:
//   name        — display name
//   description — optional free-text notes (layout/hardware references)
//
//   cells   — input slot block for the 64 matrix buttons (optional):
//               { "type": <1|2>, "numbers": [ 64 values ] }
//               type: 1=note  2=cc
//
//   scenes  — input slot block for the 8 scene buttons (optional):
//               { "type": <1|2>, "numbers": [ 8 values ] }
//
//   specs   — object with one LED-output spec per state:
//               "off",
//               "color0dim", "color0", "color1dim", "color1",
//               "color2dim", "color2", "color3dim", "color3",
//               "pending", "portLearn", "midiLearn",
//               "sceneActive", "sceneDim",
//               "connected0", "connected1", "connected2", "connected3"
//
// spec object fields:
//   type     — 0=none  1=note-on  2=note-off  3=cc  4=from-slot-type  (default 0)
//              from-slot-type: derive status byte from the slot's type (Note→NoteOn, CC→CC);
//              noteMode must be from-slot; output only occurs when the slot is mapped.
//   channel  — MIDI channel 0–15                         (default 0)
//   noteMode — 0=from-slot  1=fixed                      (default 0)
//              from-slot: uses the button's current MIDI mapping number
//              fixed:     uses the "note" field below
//   note     — note/CC number; only used with noteMode 1  (default 0)
//   value    — velocity or CC value                       (default 0)
//
// Colour set defaults: 0=red  1=blue  2=orange  3=green
//
// Colour palette references (Novation Launchpad X / MK3 / MK2):
//   0=off  5=red(dim)  7=red  17=green(dim)  21=green  9=yellow(dim)  13=yellow
//   41=blue(dim)  45=blue  63=white
//
// Novation Launchpad (original) bi-colour velocity formula (channel 0 only):
//   velocity = 16×Green + Red + 12   (Green/Red each 0–3)
//   12=off  13=red(dim)  15=red  28=green(dim)  60=green  29=amber(dim)
//   63=amber  62=yellow  56=green-flash*  11=red-flash*  (* requires CC 0/40 init)
//
// Novation Launchpad X / Mini MK3 — Programmer mode channel semantics:
//   channel 0 — static colour
//   channel 1 — flashing: alternates between this colour and static colour,
//               synced to MIDI beat clock (one period = one beat)
//   channel 2 — pulsing: breathes dark→full, synced to MIDI clock
//               (one period = two beats)
//
// ---------------------------------------------------------------------------

// Directory containing the built-in *.ctrl.json preset files, resolved relative
// to the plugin install directory. Under the test harness (TESTING=1) the
// plugin path is empty and tests run with the repo root as the working
// directory, so a plain relative path resolves correctly there too.
static std::string controllerPresetsDir() {
	if (isTesting() || !pluginInstance || pluginInstance->path.empty()) {
		return "presets/SpliceKit";
	}
	return pluginInstance->path + "/presets/SpliceKit";
}

// One loaded preset plus the raw JSON text it was parsed from, so
// "Save preset to file..." can write back the exact source file contents.
struct LoadedPreset {
	MidiOutPreset preset;
	std::string json;
};

// Parse and cache all presets on first call (C++11 magic-static, thread-safe).
// Reads every *.ctrl.json file in controllerPresetsDir(), sorted by filename.
static std::vector<LoadedPreset>& getLoadedPresets() {
	static std::vector<LoadedPreset> presets = []() {
		std::vector<LoadedPreset> v;
		std::string dir = controllerPresetsDir();
		std::vector<std::string> files = rack::system::getEntries(dir);
		std::sort(files.begin(), files.end());
		for (const std::string& path : files) {
			if (path.size() < 10 || path.compare(path.size() - 10, 10, ".ctrl.json") != 0) continue;
			std::vector<uint8_t> raw = rack::system::readFile(path);
			if (raw.empty()) continue;

			LoadedPreset lp;
			lp.json.assign(raw.begin(), raw.end());

			json_error_t err;
			json_t* root = json_loads(lp.json.c_str(), 0, &err);
			if (!root) continue;
			lp.preset.fromJson(root);
			json_decref(root);

			v.push_back(std::move(lp));
		}
		return v;
	}();
	return presets;
}

} // namespace SpliceKit
} // namespace StoermelderPackOne
