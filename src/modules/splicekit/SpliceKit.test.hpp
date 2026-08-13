#pragma once

// Shared preamble and helpers for the SpliceKit test suite.
// Included by every SpliceKit*.test.cpp file; each test file builds its
// own binary (see plugin-test.mk), so the test context is per-binary.

#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "SpliceKit.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::SpliceKit;

SYNC_MODEL(modelSpliceKit, "SpliceKit");
Test::TestContext<> testContext;

// Helper: build a preset with NOTE_ON + FIXED mode for all states.
static MidiOutPreset makeNoteOnPreset(int note = 36, int value = 127) {
	MidiOutPreset preset;
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		preset.specs[s].type = MIDI_OUT_NOTE_ON;
		preset.specs[s].noteMode = MIDI_OUT_FIXED;
		preset.specs[s].note = note;
		preset.specs[s].value = value;
	}
	return preset;
}