#pragma once
#include "../../plugin.hpp"
#include "MidiTrackingProcessor.hpp"
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace SpliceKit {

static const int MATRIX_SIZE  = 8;
static const int MATRIX_COUNT = MATRIX_SIZE * MATRIX_SIZE;
// Number of onboard scene slots. Currently equal to MATRIX_SIZE (the matrix's row/column
// count) but conceptually independent — kept as its own constant so scene-related code
// doesn't implicitly depend on the matrix's dimensions, which matter for an unrelated reason
// (SpliceKitVizOverlay::cellCenter() and the grid layout in SpliceKitWidget's constructor).
static const int SCENE_COUNT  = 8;

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
enum MidiOutMsgType { MIDI_OUT_NONE = 0, MIDI_OUT_NOTE_ON, MIDI_OUT_NOTE_OFF, MIDI_OUT_CC, MIDI_OUT_FROM_SLOT_TYPE };

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

		parseSlotsBlock(json_object_get(rootJ, "cells"),  cells,  MATRIX_COUNT);
		parseSlotsBlock(json_object_get(rootJ, "scenes"), scenes, SCENE_COUNT);

		static const char* const STATE_KEYS[LED_STATE_COUNT] = {
			"off",
			"color0dim", "color0", "color1dim", "color1",
			"color2dim", "color2", "color3dim", "color3",
			"pending", "portLearn", "midiLearn",
			"sceneActive", "sceneDim",
			"connected0", "connected1", "connected2", "connected3"
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

static const char* const CONTROLLER_PRESET_JSON[] = {

// ---- Off -------------------------------------------------------------------
R"json({
    "name": "Off",
    "specs": {
        "off":          {"type":0},
        "color0dim":    {"type":0},
        "color0":       {"type":0},
        "color1dim":    {"type":0},
        "color1":       {"type":0},
        "color2dim":    {"type":0},
        "color2":       {"type":0},
        "color3dim":    {"type":0},
        "color3":       {"type":0},
        "pending":      {"type":0},
        "portLearn":    {"type":0},
        "midiLearn":    {"type":0},
        "sceneActive":  {"type":0},
        "sceneDim":     {"type":0},
        "connected0":   {"type":0},
        "connected1":   {"type":0},
        "connected2":   {"type":0},
        "connected3":   {"type":0}
    }
})json",

// ---- Novation Launchpad / S — X-Y mode ------------------------------------
// Grid cells: Note On, row r (0=top) col c (0=left) → note = 16×r + c
//   top row = 0–7, bottom row = 112–119.
// Scene launch buttons (right-side column): Note On, note = 16×r + 8
//   → notes 8, 24, 40, 56, 72, 88, 104, 120 (top to bottom).
// Bi-colour LEDs: velocity = 16×Green + Red + 12  (no RGB; no hardware init needed).
// https://userguides.novationmusic.com/hc/en-gb/articles/23731330800146-Launchpad-Mini-s-default-MIDI-mappings
// https://downloads.novationmusic.com/novation/launchpad-mk1/launchpad
R"json({
    "name": "Launchpad / S (X-Y mode)",
    "cells": {
        "type": 1,
        "numbers": [
              0,  1,  2,  3,  4,  5,  6,  7,
             16, 17, 18, 19, 20, 21, 22, 23,
             32, 33, 34, 35, 36, 37, 38, 39,
             48, 49, 50, 51, 52, 53, 54, 55,
             64, 65, 66, 67, 68, 69, 70, 71,
             80, 81, 82, 83, 84, 85, 86, 87,
             96, 97, 98, 99,100,101,102,103,
            112,113,114,115,116,117,118,119
        ]
    },
    "scenes": {
        "type": 1,
        "numbers": [8,24,40,56,72,88,104,120]
    },
    "specs": {
        "off":          {"type":1,"channel":0,"noteMode":0,"value":12},
        "color0dim":    {"type":1,"channel":0,"noteMode":0,"value":13},
        "color0":       {"type":1,"channel":0,"noteMode":0,"value":15},
        "color1dim":    {"type":1,"channel":0,"noteMode":0,"value":28},
        "color1":       {"type":1,"channel":0,"noteMode":0,"value":60},
        "color2dim":    {"type":1,"channel":0,"noteMode":0,"value":28},
        "color2":       {"type":1,"channel":0,"noteMode":0,"value":60},
        "color3dim":    {"type":1,"channel":0,"noteMode":0,"value":29},
        "color3":       {"type":1,"channel":0,"noteMode":0,"value":62},
        "pending":      {"type":1,"channel":0,"noteMode":0,"value":63},
        "portLearn":    {"type":1,"channel":0,"noteMode":0,"value":62},
        "midiLearn":    {"type":1,"channel":0,"noteMode":0,"value":29},
        "sceneActive":  {"type":1,"channel":0,"noteMode":0,"value":60},
        "sceneDim":     {"type":1,"channel":0,"noteMode":0,"value":28},
        "connected0":   {"type":1,"channel":0,"noteMode":0,"value":15},
        "connected1":   {"type":1,"channel":0,"noteMode":0,"value":60},
        "connected2":   {"type":1,"channel":0,"noteMode":0,"value":60},
        "connected3":   {"type":1,"channel":0,"noteMode":0,"value":62}
    }
})json",

// ---- Novation Launchpad MK2 / S — Session mode ----------------------------
// Grid cells: Note On, row r (0=top) col c (0=left) → note = (7−r)×10 + c + 11
// Top-row round buttons (scene): CC 104–111 (fixed regardless of layout, p.7/8).
// channel 0 — static colour (MIDI ch 1)
// channel 1 — flash: alternates between this colour and current static colour, one beat (MIDI ch 2)
// channel 2 — pulse: breathes dark→full, two beats (MIDI ch 3)
// https://downloads.novationmusic.com/novation/launchpad-mk2/launchpad-mini-mk2
R"json({
    "name": "Launchpad MK2 (Session mode)",
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
        "numbers": [104,105,106,107,108,109,110,111]
    },
    "specs": {
        "off":          {"type":1,"channel":0,"noteMode":0,"value": 0},
        "color0dim":    {"type":1,"channel":0,"noteMode":0,"value": 7},
        "color0":       {"type":1,"channel":0,"noteMode":0,"value": 5},
        "color1dim":    {"type":1,"channel":0,"noteMode":0,"value":47},
        "color1":       {"type":1,"channel":0,"noteMode":0,"value":44},
        "color2dim":    {"type":1,"channel":0,"noteMode":0,"value":10},
        "color2":       {"type":1,"channel":0,"noteMode":0,"value":96},
        "color3dim":    {"type":1,"channel":0,"noteMode":0,"value":27},
        "color3":       {"type":1,"channel":0,"noteMode":0,"value":25},
        "pending":      {"type":1,"channel":1,"noteMode":0,"value": 3},
        "portLearn":    {"type":1,"channel":2,"noteMode":0,"value":45},
        "midiLearn":    {"type":1,"channel":2,"noteMode":0,"value": 1},
        "sceneActive":  {"type":1,"channel":0,"noteMode":0,"value":63},
        "sceneDim":     {"type":1,"channel":0,"noteMode":0,"value": 2},
        "connected0":   {"type":1,"channel":2,"noteMode":0,"value": 5},
        "connected1":   {"type":1,"channel":2,"noteMode":0,"value":44},
        "connected2":   {"type":1,"channel":2,"noteMode":0,"value":96},
        "connected3":   {"type":1,"channel":2,"noteMode":0,"value":25}
    }
})json",


// ---- Novation Launchpad X / Mini MK3 — Programmer mode --------------------
// Grid cells: Note On, row r (0=top) col c (0=left) → note = (8−r)×10 + c + 1
// Scene buttons (top row of round buttons): CC 91–98
// channel 0 — static colour (MIDI ch 1)
// channel 1 — flash: alternates between this colour and current static colour, one beat (MIDI ch 2)
// channel 2 — pulse: breathes dark→full, two beats (MIDI ch 3)
// https://downloads.novationmusic.com/novation/launchpad-mk3/launchpad-mini-mk3-0
R"json({
    "name": "Launchpad X / MK3 (Programmer mode)",
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
        "off":          {"type":1,"channel":0,"noteMode":0,"value": 0},
        "color0dim":    {"type":1,"channel":0,"noteMode":0,"value": 7},
        "color0":       {"type":1,"channel":0,"noteMode":0,"value": 5},
        "color1dim":    {"type":1,"channel":0,"noteMode":0,"value":47},
        "color1":       {"type":1,"channel":0,"noteMode":0,"value":44},
        "color2dim":    {"type":1,"channel":0,"noteMode":0,"value":10},
        "color2":       {"type":1,"channel":0,"noteMode":0,"value":96},
        "color3dim":    {"type":1,"channel":0,"noteMode":0,"value":27},
        "color3":       {"type":1,"channel":0,"noteMode":0,"value":25},
        "pending":      {"type":1,"channel":1,"noteMode":0,"value": 3},
        "portLearn":    {"type":1,"channel":2,"noteMode":0,"value":45},
        "midiLearn":    {"type":1,"channel":2,"noteMode":0,"value": 1},
        "sceneActive":  {"type":1,"channel":0,"noteMode":0,"value":63},
        "sceneDim":     {"type":1,"channel":0,"noteMode":0,"value": 2},
        "connected0":   {"type":1,"channel":2,"noteMode":0,"value": 5},
        "connected1":   {"type":1,"channel":2,"noteMode":0,"value":44},
        "connected2":   {"type":1,"channel":2,"noteMode":0,"value":96},
        "connected3":   {"type":1,"channel":2,"noteMode":0,"value":25}
    }
})json",

// ---- Akai APC Mini (original) — Note On ch 0 ------------------------------
// Grid cells: Note On, row r (0=top) col c (0=left) → note = (7−r)×8 + c
//   top row = 56–63, bottom row = 0–7.
// Scene Launch 1–8 (right-side buttons): notes 82–89, top to bottom.
// Velocity: 0=off  1=green  2=green blink  3=red  4=red blink  5=yellow  6=yellow blink
// Only 3 colors available; color2 maps to green, color3 to yellow.
R"json({
    "name": "APC Mini",
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
    "scenes": {
        "type": 1,
        "numbers": [82,83,84,85,86,87,88,89]
    },
    "specs": {
        "off":          {"type":1,"noteMode":0,"value":0},
        "color0dim":    {"type":1,"noteMode":0,"value":5},
        "color0":       {"type":1,"noteMode":0,"value":3},
        "color1dim":    {"type":1,"noteMode":0,"value":5},
        "color1":       {"type":1,"noteMode":0,"value":1},
        "color2dim":    {"type":1,"noteMode":0,"value":5},
        "color2":       {"type":1,"noteMode":0,"value":1},
        "color3dim":    {"type":1,"noteMode":0,"value":5},
        "color3":       {"type":1,"noteMode":0,"value":5},
        "pending":      {"type":1,"noteMode":0,"value":6},
        "portLearn":    {"type":1,"noteMode":0,"value":2},
        "midiLearn":    {"type":1,"noteMode":0,"value":4},
        "sceneActive":  {"type":1,"noteMode":0,"value":1},
        "sceneDim":     {"type":1,"noteMode":0,"value":5},
        "connected0":   {"type":1,"noteMode":0,"value":4},
        "connected1":   {"type":1,"noteMode":0,"value":2},
        "connected2":   {"type":1,"noteMode":0,"value":2},
        "connected3":   {"type":1,"noteMode":0,"value":6}
    }
})json",

// ---- Akai APC Mini MK2 — RGB LEDs, MIDI channel encodes behavior ----------
// Grid cells: Note On, row r (0=top) col c (0=left) → note = (7−r)×8 + c
//   top row = 56–63, bottom row = 0–7 (same convention as original APC Mini).
// Scene Launch 1–8 (right-side buttons): notes 112–119, single-color green,
//   ch 0 only (blink/pulse not supported on single-color LEDs).
// MIDI channel determines LED behaviour (p.3):
//   ch 0–6  = solid at 10–100% brightness;  ch 6 used here for 100%
//   ch 7–10 = pulsing at 1/16–1/2 note;     ch 7 (1/16) used for learn
//   ch 11–15= blinking at 1/24–1/2 note;    ch 11 (1/24) used for pending/connected
// Colour palette (p.4–5): 0=off  3=white  5=red  9=amber  21=green  45=blue
// https://cdn.inmusicbrands.com/akai/attachments/APC%20mini%20mk2%20-%20Communication%20Protocol%20-%20v1.0.pdf
R"json({
    "name": "APC Mini MK2",
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
    "scenes": {
        "type": 1,
        "numbers": [112,113,114,115,116,117,118,119]
    },
    "specs": {
        "off":          {"type":1,"channel": 6,"noteMode":0,"value": 0},
        "color0dim":    {"type":1,"channel": 1,"noteMode":0,"value": 5},
        "color0":       {"type":1,"channel": 6,"noteMode":0,"value": 5},
        "color1dim":    {"type":1,"channel": 1,"noteMode":0,"value":45},
        "color1":       {"type":1,"channel": 6,"noteMode":0,"value":45},
        "color2dim":    {"type":1,"channel": 1,"noteMode":0,"value":21},
        "color2":       {"type":1,"channel": 6,"noteMode":0,"value":21},
        "color3dim":    {"type":1,"channel": 1,"noteMode":0,"value": 9},
        "color3":       {"type":1,"channel": 6,"noteMode":0,"value": 9},
        "pending":      {"type":1,"channel":11,"noteMode":0,"value": 3},
        "portLearn":    {"type":1,"channel": 7,"noteMode":0,"value":45},
        "midiLearn":    {"type":1,"channel": 7,"noteMode":0,"value":21},
        "sceneActive":  {"type":1,"channel": 6,"noteMode":0,"value":21},
        "sceneDim":     {"type":1,"channel": 1,"noteMode":0,"value":21},
        "connected0":   {"type":1,"channel":11,"noteMode":0,"value": 5},
        "connected1":   {"type":1,"channel":11,"noteMode":0,"value":45},
        "connected2":   {"type":1,"channel":11,"noteMode":0,"value":21},
        "connected3":   {"type":1,"channel":11,"noteMode":0,"value": 9}
    }
})json",

// ---- Ableton Push 2 --------------------------------------------------------
// Grid cells: Note On, row r (0=top) col c (0=left) → note = (9−r)×8 + c + 28
//   top row = 92–99, bottom row = 36–43.
// Scene buttons (below the display, left to right): CC 20–27.
//   These use CC for both button press input and LED feedback.
// LED colour palette (index 0–127, documented subset):
//   0=off  122=white(204,204,204)  123=light-gray(64,64,64)
//   124=dark-gray(20,20,20)  125=blue  126=green  127=red
// Animation channels: 0=static
//   6=pulse(1/24)…10=pulse(1/2)   11=blink(1/24)…15=blink(1/2)
//   ch10 (slowest pulse, ~0.5 s at 120 BPM) used for idle/dim states.
//   ch6  (fastest pulse) used for connected highlights.
//   ch11 (fastest blink) used for pending/learn states.
// spec type 4 (from-slot-type): sends Note On for pad cells, CC for scene buttons.
// Push 2 must be in User mode (not Live mode) for direct MIDI control.
// https://github.com/Ableton/push-interface/blob/main/doc/AbletonPush2MIDIDisplayInterface.asc
R"json({
    "name": "Ableton Push 2",
    "cells": {
        "type": 1,
        "numbers": [
            92,93,94,95,96,97,98,99,
            84,85,86,87,88,89,90,91,
            76,77,78,79,80,81,82,83,
            68,69,70,71,72,73,74,75,
            60,61,62,63,64,65,66,67,
            52,53,54,55,56,57,58,59,
            44,45,46,47,48,49,50,51,
            36,37,38,39,40,41,42,43
        ]
    },
    "scenes": {
        "type": 2,
        "numbers": [20,21,22,23,24,25,26,27]
    },
    "specs": {
        "off":          {"type":4,"channel": 0,"noteMode":0,"value":  0},
        "color0dim":    {"type":1,"channel": 0,"noteMode":0,"value":127},
        "color0":       {"type":1,"channel": 0,"noteMode":0,"value": 68},
        "color1dim":    {"type":1,"channel": 0,"noteMode":0,"value":125},
        "color1":       {"type":1,"channel": 0,"noteMode":0,"value": 99},
        "color2dim":    {"type":1,"channel": 0,"noteMode":0,"value": 69},
        "color2":       {"type":1,"channel": 0,"noteMode":0,"value": 39},
        "color3dim":    {"type":1,"channel": 0,"noteMode":0,"value":126},
        "color3":       {"type":1,"channel": 0,"noteMode":0,"value": 89},
        "pending":      {"type":1,"channel":10,"noteMode":0,"value":122},
        "portLearn":    {"type":1,"channel":11,"noteMode":0,"value": 24},
        "midiLearn":    {"type":1,"channel":11,"noteMode":0,"value": 22},
        "sceneActive":  {"type":4,"channel": 0,"noteMode":0,"value":122},
        "sceneDim":     {"type":4,"channel": 0,"noteMode":0,"value":123},
        "connected0":   {"type":1,"channel":11,"noteMode":0,"value":127},
        "connected1":   {"type":1,"channel":11,"noteMode":0,"value":120},
        "connected2":   {"type":1,"channel":11,"noteMode":0,"value": 69},
        "connected3":   {"type":1,"channel":11,"noteMode":0,"value":126}
    }
})json",

// ---- Generic Note On -------------------------------------------------------
// Works with any controller that lights LEDs via Note On velocity.
// Assign note numbers via MIDI learn.
R"json({
    "name": "Generic (Note On)",
    "specs": {
        "off":          {"type":1,"noteMode":0,"value": 0},
        "color0dim":    {"type":1,"noteMode":0,"value": 1},
        "color0":       {"type":1,"noteMode":0,"value": 2},
        "color1dim":    {"type":1,"noteMode":0,"value": 3},
        "color1":       {"type":1,"noteMode":0,"value": 4},
        "color2dim":    {"type":1,"noteMode":0,"value": 5},
        "color2":       {"type":1,"noteMode":0,"value": 6},
        "color3dim":    {"type":1,"noteMode":0,"value": 7},
        "color3":       {"type":1,"noteMode":0,"value": 8},
        "pending":      {"type":1,"noteMode":0,"value": 9},
        "portLearn":    {"type":1,"noteMode":0,"value":10},
        "midiLearn":    {"type":1,"noteMode":0,"value":11},
        "sceneActive":  {"type":1,"noteMode":0,"value":12},
        "sceneDim":     {"type":1,"noteMode":0,"value":13},
        "connected0":   {"type":1,"noteMode":0,"value":14},
        "connected1":   {"type":1,"noteMode":0,"value":15},
        "connected2":   {"type":1,"noteMode":0,"value":16},
        "connected3":   {"type":1,"noteMode":0,"value":17}
    }
})json",

}; // CONTROLLER_PRESET_JSON

static const int CONTROLLER_PRESET_COUNT =
	(int)(sizeof(CONTROLLER_PRESET_JSON) / sizeof(CONTROLLER_PRESET_JSON[0]));

// Sentinel stored in the module's feedbackPreset field when a user-loaded custom
// preset is active. The value is persisted in patch JSON under "feedbackPreset",
// so it must remain 255 forever for backward compatibility. It must never equal
// any built-in preset index — use CONTROLLER_PRESET_COUNT to guard that range.
static const int PRESET_IDX_CUSTOM = 255;

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

} // namespace SpliceKit
} // namespace StoermelderPackOne
