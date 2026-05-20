#pragma once
#include "../../plugin.hpp"
#include "MidiTrackingProcessor.hpp"
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

// How the note/CC number is resolved when sending MIDI output for an LED state.
//   FROM_SLOT — use the button's current MIDI input mapping number (the note/CC
//               that was learned or applied from the preset layout).
//   FIXED     — use spec.note for every button, regardless of its mapping.
enum MidiOutNoteMode { MIDI_OUT_FROM_SLOT = 0, MIDI_OUT_FIXED };

// One MIDI output message specification per LED state.
struct MidiOutSpec {
	MidiOutMsgType  type     = MIDI_OUT_NONE;
	int             channel  = 0;               // 0–15
	MidiOutNoteMode noteMode = MIDI_OUT_FROM_SLOT;
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

// A single input slot: which MIDI message triggers this button.
// type==NONE means no auto-mapping; the user must learn manually.
struct MidiSlot {
	MidiTrackingType type   = MidiTrackingType::NONE;
	int              number = 0;    // note or CC number
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
		slots[i].type   = type;
		slots[i].number = (int)json_integer_value(json_array_get(numsJ, i));
	}
}

// A named controller preset: per-button input slots + one output spec per LED state.
struct MidiOutPreset {
	std::string name;
	MidiSlot    cells[MATRIX_COUNT] = {};   // 64 matrix cell input slots
	MidiSlot    scenes[MATRIX_SIZE] = {};   // 8 scene button input slots
	MidiOutSpec specs[LED_STATE_COUNT]  = {};

	bool hasLayout() const {
		for (int i = 0; i < MATRIX_COUNT; i++)
			if (cells[i].type != MidiTrackingType::NONE) return true;
		for (int i = 0; i < MATRIX_SIZE; i++)
			if (scenes[i].type != MidiTrackingType::NONE) return true;
		return false;
	}

	void fromJson(json_t* rootJ) {
		if (!rootJ) return;
		json_t* nameJ = json_object_get(rootJ, "name");
		if (nameJ) name = json_string_value(nameJ);

		parseSlotsBlock(json_object_get(rootJ, "cells"),  cells,  MATRIX_COUNT);
		parseSlotsBlock(json_object_get(rootJ, "scenes"), scenes, MATRIX_SIZE);

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
//   name    — display name
//
//   cells   — input slot block for the 64 matrix buttons (optional):
//               { "type": <1|2>, "numbers": [ 64 values ] }
//               type: 1=note  2=cc
//
//   scenes  — input slot block for the 8 scene buttons (optional):
//               { "type": <1|2>, "numbers": [ 8 values ] }
//
//   specs   — object with one LED-output spec per state:
//               "off", "outDim", "out", "inDim", "in",
//               "pending", "portLearn", "midiLearn"
//
// spec object fields:
//   type     — 0=none  1=note-on  2=note-off  3=cc      (default 0)
//   channel  — MIDI channel 0–15                         (default 0)
//   noteMode — 0=from-slot  1=fixed                      (default 0)
//              from-slot: uses the button's current MIDI mapping number
//              fixed:     uses the "note" field below
//   note     — note/CC number; only used with noteMode 1  (default 0)
//   value    — velocity or CC value                       (default 0)
//
// Colour palette references (Novation Launchpad):
//   0=off  5=red(dim)  7=red  21=green  41=blue(dim)  45=blue  63=white
//
// Novation Launchpad X / Mini MK3 — Programmer mode channel semantics:
//   channel 0 — static colour
//   channel 1 — flashing: alternates between this colour and static colour,
//               synced to MIDI beat clock (one period = one beat)
//   channel 2 — pulsing: breathes dark→full, synced to MIDI clock
//               (one period = two beats)
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
// Grid cells: Note On, row r (0=top) col c (0=left) → note = (8−r)×10 + c + 1
// Scene buttons (top row of round buttons): CC 91–98
// Static colour on channel 0; pending = channel 1 (hardware flash, one beat);
// learn states = channel 2 (hardware pulse, two beats).
R"json({
    "name": "Launchpad MK3 (Programmer mode)",
    "cells": {
        "type": 1,
        "numbers": [
            81,82,83,84,85,86,87,88,
            71,72,73,74,75,76,77,78,
            61,62,63,64,65,66,67,68,
            51,52,53,54,55,56,57,58,
            41,42,43,44,45,46,47,48,
            31,32,33,34,35,36,37,38,
            21,22,23,24,25,26,27,28,
            11,12,13,14,15,16,17,18
        ]
    },
    "scenes": {
        "type": 2,
        "numbers": [91,92,93,94,95,96,97,98]
    },
    "specs": {
        "off":       {"type":1,"channel":0,"noteMode":0,"value": 0},
        "outDim":    {"type":1,"channel":0,"noteMode":0,"value": 5},
        "out":       {"type":1,"channel":0,"noteMode":0,"value": 7},
        "inDim":     {"type":1,"channel":0,"noteMode":0,"value":41},
        "in":        {"type":1,"channel":0,"noteMode":0,"value":45},
        "pending":   {"type":1,"channel":1,"noteMode":0,"value":63},
        "portLearn": {"type":1,"channel":2,"noteMode":0,"value":45},
        "midiLearn": {"type":1,"channel":2,"noteMode":0,"value":21}
    }
})json",

// ---- Novation Launchpad MK2 / S — Live (Session) mode ---------------------
// Grid cells: Note On, row r (0=top) col c (0=left) → note = (7−r)×8 + c
// No scene layout — MK2 top-row buttons send CC 104–111, which share the same
// numbers as MK3 Programmer mode; add manually via right-click if needed.
// Static colours only — MK2 uses a different channel protocol for flash/pulse.
R"json({
    "name": "Launchpad MK2 (Live mode)",
    "cells": {
        "type": 1,
        "numbers": [
            56,57,58,59,60,61,62,63,
            48,49,50,51,52,53,54,55,
            40,41,42,43,44,45,46,47,
            32,33,34,35,36,37,38,39,
            24,25,26,27,28,29,30,31,
            16,17,18,19,20,21,22,23,
             8, 9,10,11,12,13,14,15,
             0, 1, 2, 3, 4, 5, 6, 7
        ]
    },
    "specs": {
        "off":       {"type":1,"noteMode":0,"value": 0},
        "outDim":    {"type":1,"noteMode":0,"value": 5},
        "out":       {"type":1,"noteMode":0,"value": 7},
        "inDim":     {"type":1,"noteMode":0,"value":41},
        "in":        {"type":1,"noteMode":0,"value":45},
        "pending":   {"type":1,"noteMode":0,"value":63},
        "portLearn": {"type":1,"noteMode":0,"value":45},
        "midiLearn": {"type":1,"noteMode":0,"value":21}
    }
})json",

// ---- Akai APC Mini (original) — Note On ch 0 ------------------------------
// No layout — assign note numbers via MIDI learn.
// Velocity: 0=off  1=green  3=red  5=yellow.
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
// Assign note numbers via MIDI learn.
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
