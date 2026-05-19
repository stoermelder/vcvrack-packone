#pragma once
#include "../../plugin.hpp"
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace MidiBay {

static const int MATRIX_SIZE  = 8;
static const int MATRIX_COUNT = MATRIX_SIZE * MATRIX_SIZE;

// LED state identifiers — order must match the light-loop state assignment in MidiBay.cpp.
enum {
	LED_STATE_OFF = 0,
	LED_STATE_OUT_DIM,
	LED_STATE_OUT,
	LED_STATE_IN_DIM,
	LED_STATE_IN,
	LED_STATE_PENDING,
	LED_STATE_PORT_LEARN,
	LED_STATE_MIDI_LEARN,
	LED_STATE_COUNT
};

// MIDI output message type.
enum MidiOutMsgType { MIDI_OUT_NONE = 0, MIDI_OUT_NOTE_ON, MIDI_OUT_NOTE_OFF, MIDI_OUT_CC };

// How the note / CC number is resolved for each cell.
//   FROM_MAP    — use the button's MIDI input mapping (skipped if unmapped).
//   FIXED       — use spec.note for every cell.
//   FROM_LAYOUT — use the preset's per-cell noteLayout[cellId].
enum MidiOutNoteMode { MIDI_OUT_FROM_MAP = 0, MIDI_OUT_FIXED, MIDI_OUT_FROM_LAYOUT };

// One MIDI message specification per LED state.
struct MidiOutSpec {
	MidiOutMsgType  type     = MIDI_OUT_NONE;
	int             channel  = 0;               // 0–15
	MidiOutNoteMode noteMode = MIDI_OUT_FROM_MAP;
	int             note     = 0;               // used only with MIDI_OUT_FIXED
	int             value    = 0;               // velocity or CC value

	void fromJson(json_t* j) {
		if (!j) return;
		auto gi = [j](const char* k) { return (int)json_integer_value(json_object_get(j, k)); };
		type     = (MidiOutMsgType) gi("type");
		channel  = gi("channel");
		noteMode = (MidiOutNoteMode)gi("noteMode");
		note     = gi("note");
		value    = gi("value");
	}
};

// A named controller preset: a per-cell note layout plus one spec per LED state.
struct MidiOutPreset {
	std::string name;
	int         noteLayout[MATRIX_COUNT] = {};
	MidiOutSpec specs[LED_STATE_COUNT]   = {};

	void fromJson(json_t* rootJ) {
		if (!rootJ) return;
		json_t* nameJ = json_object_get(rootJ, "name");
		if (nameJ) name = json_string_value(nameJ);

		json_t* layoutJ = json_object_get(rootJ, "noteLayout");
		if (layoutJ) {
			int n = std::min((int)json_array_size(layoutJ), MATRIX_COUNT);
			for (int i = 0; i < n; i++)
				noteLayout[i] = (int)json_integer_value(json_array_get(layoutJ, i));
		}

		static const char* const STATE_KEYS[LED_STATE_COUNT] = {
			"off", "outDim", "out", "inDim", "in", "pending", "portLearn", "midiLearn"
		};
		json_t* specsJ = json_object_get(rootJ, "specs");
		if (specsJ) {
			for (int i = 0; i < LED_STATE_COUNT; i++)
				specs[i].fromJson(json_object_get(specsJ, STATE_KEYS[i]));
		}
	}
};

// ---- Controller preset definitions (JSON strings) --------------------------
//
// JSON fields:
//   name        — display name
//   noteLayout  — 64 MIDI note numbers (row-major, row 0 = top);
//                 omit or leave empty when noteMode != 2 (FROM_LAYOUT)
//   specs       — object with one entry per LED state:
//                 "off", "outDim", "out", "inDim", "in",
//                 "pending", "portLearn", "midiLearn"
//
// spec object fields:
//   type     — 0=none  1=note-on  2=note-off  3=cc   (default 0)
//   channel  — MIDI channel 0–15                      (default 0)
//   noteMode — 0=from-input-map  1=fixed  2=from-layout  (default 0)
//   note     — note/CC number; only used with noteMode 1  (default 0)
//   value    — velocity or CC value                    (default 0)
//
// Colour palette references (Novation Launchpad):
//   0=off  5=red(dim)  7=red  21=green  41=blue(dim)  45=blue  63=white
//
// ---------------------------------------------------------------------------

static const char* const CONTROLLER_PRESET_JSON[] = {

// ---- Off -------------------------------------------------------------------
R"json({
    "name": "Off",
    "specs": {
        "off":       {"type":0},
        "outDim":    {"type":0},
        "out":       {"type":0},
        "inDim":     {"type":0},
        "in":        {"type":0},
        "pending":   {"type":0},
        "portLearn": {"type":0},
        "midiLearn": {"type":0}
    }
})json",

// ---- Novation Launchpad X / Mini MK3 — Programmer mode --------------------
// Also compatible with Launchpad MK2 / S in User 1 or User 2 mode.
// Note layout formula: row r (0=top), col c (0=left) → note = (8−r)×10 + c + 1
R"json({
    "name": "Launchpad (Programmer mode)",
    "noteLayout": [
        81,82,83,84,85,86,87,88,
        71,72,73,74,75,76,77,78,
        61,62,63,64,65,66,67,68,
        51,52,53,54,55,56,57,58,
        41,42,43,44,45,46,47,48,
        31,32,33,34,35,36,37,38,
        21,22,23,24,25,26,27,28,
        11,12,13,14,15,16,17,18
    ],
    "specs": {
        "off":       {"type":1,"noteMode":2,"value": 0},
        "outDim":    {"type":1,"noteMode":2,"value": 5},
        "out":       {"type":1,"noteMode":2,"value": 7},
        "inDim":     {"type":1,"noteMode":2,"value":41},
        "in":        {"type":1,"noteMode":2,"value":45},
        "pending":   {"type":1,"noteMode":2,"value":63},
        "portLearn": {"type":1,"noteMode":2,"value":45},
        "midiLearn": {"type":1,"noteMode":2,"value":21}
    }
})json",

// ---- Novation Launchpad MK2 / S — Live (Session) mode ---------------------
// Row 0 (top in MidiBay) = highest clip row in Ableton Live.
// Note layout formula: row r (0=top), col c (0=left) → note = (7−r)×8 + c
R"json({
    "name": "Launchpad MK2 (Live mode)",
    "noteLayout": [
        56,57,58,59,60,61,62,63,
        48,49,50,51,52,53,54,55,
        40,41,42,43,44,45,46,47,
        32,33,34,35,36,37,38,39,
        24,25,26,27,28,29,30,31,
        16,17,18,19,20,21,22,23,
         8, 9,10,11,12,13,14,15,
         0, 1, 2, 3, 4, 5, 6, 7
    ],
    "specs": {
        "off":       {"type":1,"noteMode":2,"value": 0},
        "outDim":    {"type":1,"noteMode":2,"value": 5},
        "out":       {"type":1,"noteMode":2,"value": 7},
        "inDim":     {"type":1,"noteMode":2,"value":41},
        "in":        {"type":1,"noteMode":2,"value":45},
        "pending":   {"type":1,"noteMode":2,"value":63},
        "portLearn": {"type":1,"noteMode":2,"value":45},
        "midiLearn": {"type":1,"noteMode":2,"value":21}
    }
})json",

// ---- Akai APC Mini (original) — Note On ch 0 ------------------------------
// Velocity: 0=off  1=green  3=red  5=yellow.
// No fixed layout — assign note numbers via MIDI Input learn.
R"json({
    "name": "APC Mini",
    "specs": {
        "off":       {"type":1,"noteMode":0,"value":0},
        "outDim":    {"type":1,"noteMode":0,"value":3},
        "out":       {"type":1,"noteMode":0,"value":3},
        "inDim":     {"type":1,"noteMode":0,"value":1},
        "in":        {"type":1,"noteMode":0,"value":1},
        "pending":   {"type":1,"noteMode":0,"value":5},
        "portLearn": {"type":1,"noteMode":0,"value":1},
        "midiLearn": {"type":1,"noteMode":0,"value":1}
    }
})json",

// ---- Generic Note On -------------------------------------------------------
// Works with any controller that lights LEDs via Note On velocity.
// Assign note numbers via MIDI Input learn.
R"json({
    "name": "Generic (Note On)",
    "specs": {
        "off":       {"type":1,"noteMode":0,"value":0},
        "outDim":    {"type":1,"noteMode":0,"value":1},
        "out":       {"type":1,"noteMode":0,"value":2},
        "inDim":     {"type":1,"noteMode":0,"value":3},
        "in":        {"type":1,"noteMode":0,"value":4},
        "pending":   {"type":1,"noteMode":0,"value":5},
        "portLearn": {"type":1,"noteMode":0,"value":3},
        "midiLearn": {"type":1,"noteMode":0,"value":6}
    }
})json",

}; // CONTROLLER_PRESET_JSON

static const int CONTROLLER_PRESET_COUNT =
	(int)(sizeof(CONTROLLER_PRESET_JSON) / sizeof(CONTROLLER_PRESET_JSON[0]));

// Parse and cache all presets on first call (C++11 magic-static, thread-safe).
static std::vector<MidiOutPreset>& getPresets() {
	static std::vector<MidiOutPreset> presets = []() {
		std::vector<MidiOutPreset> v;
		v.reserve(CONTROLLER_PRESET_COUNT);
		for (int i = 0; i < CONTROLLER_PRESET_COUNT; i++) {
			json_error_t err;
			json_t* root = json_loads(CONTROLLER_PRESET_JSON[i], 0, &err);
			MidiOutPreset p;
			if (root) { p.fromJson(root); json_decref(root); }
			v.push_back(std::move(p));
		}
		return v;
	}();
	return presets;
}

} // namespace MidiBay
} // namespace StoermelderPackOne
