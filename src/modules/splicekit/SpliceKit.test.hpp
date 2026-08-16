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

// Shadows Test::createModule<SpliceKitModule> for this suite: puts taskProcessorUi into
// sync mode (see GuiTaskProcessor::syncMode) right after construction, before any test
// code can call triggerCell()/process() and give it a chance to enqueue something. With
// sync mode off, taskProcessorUi.process() (called every processDivider tick, from
// SpliceKitModule::process()) starts a REAL background worker thread the instant
// APP->window is null — which it always is in this headless test binary. That worker
// then races the test's own thread over crossPending/the instance registry (both
// documented GUI-thread-only, an invariant the production code upholds by construction
// but this harness cannot), and over any module the test destroys while the worker is
// still mid-callback. Sync mode collapses enqueue() to an inline call and makes
// process() a no-op, so no such thread ever exists — the same fix MidiKit's
// SyncTaskWorker (TaskWorker.hpp) applies for its own worker abstraction.
static SpliceKitModule* createModule() {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->taskProcessorUi.syncMode = true;
	return m;
}

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