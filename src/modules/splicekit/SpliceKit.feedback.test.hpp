// SpliceKit.feedback.test.hpp — MIDI feedback.
// FeedbackSender: sendFeedback/sendFeedbackOff guards and emitted messages,
// setState LED-state caching, deferred note-off queueing (queueFeedbackOff/
// drainPendingOffs), and the LED color-set helpers.

#include "SpliceKit.test.hpp"


// sendFeedbackOff — guard conditions

TEST_CASE("sendFeedbackOff - no-op for invalid state id (-1)", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.sendFeedbackOff(0, -1);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op when feedback preset is off", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePresetJson("");  // no output
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for MIDI_OUT_NONE spec", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NONE;
	m->feedback.setActivePreset(preset);
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for CC-type spec", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_CC;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note = 20;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for NOTE_OFF-type spec", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NOTE_OFF;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note = 36;
	preset.specs[LED_STATE_COLOR0].value = 0;
	m->feedback.setActivePreset(preset);
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op when FROM_SLOT note mode has no mapping", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	// Cell 0 has no MIDI mapping — FROM_SLOT resolves to NONE → nothing sent
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for FROM_SLOT_TYPE when slot is CC", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	m->trackingProcessor.setMap(MidiTrackingType::CC, 0, 74);
	// CC slot with FROM_SLOT_TYPE resolves to a CC status — must be skipped
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - sends note-off 0x80 for NOTE_ON + FIXED mode", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].channel = 0;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note = 36;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x80);  // note-off, channel 0
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 36);    // fixed note
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 0);     // velocity 0
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - sends note-off with note from slot mapping (FROM_SLOT)", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR1].type = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR1].channel = 2;
	preset.specs[LED_STATE_COLOR1].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR1].value = 100;
	m->feedback.setActivePreset(preset);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);
	m->feedback.sendFeedbackOff(5, LED_STATE_COLOR1);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == (0x80 | 2));  // note-off, channel 2
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 60);           // note from slot
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - sends note-off for FROM_SLOT_TYPE with NOTE slot", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].channel = 1;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 48);
	m->feedback.sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == (0x80 | 1));  // note-off, channel 1
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 48);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 0);
	Test::destroyModule(m);
}


// sendFeedback (on-side) — guard conditions and message construction. Mirrors the
// sendFeedbackOff tests but exercises the path that lights the LED. The on-side also
// handles NOTE_OFF and CC (the off-side only NOTE_ON and FROM_SLOT_TYPE) and resolves
// channel/value/byte2 correctly.

TEST_CASE("sendFeedback - no-op when no preset is active", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePresetJson("");  // no output
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - no-op for NONE-type spec", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NONE;
	m->feedback.setActivePreset(preset);
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - no-op for FROM_SLOT when slot is unmapped", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	// Cell 0 has no mapping — FROM_SLOT resolves to NONE → nothing sent
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

// NOTE: unlike the off-side, the on-side DOES support a CC slot under FROM_SLOT_TYPE
// (it resolves to 0xB0) — that positive case is covered further down.

TEST_CASE("sendFeedback - sends note-on 0x90 for NOTE_ON + FIXED mode", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].channel = 0;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note = 36;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x90);  // note-on, channel 0
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 36);    // fixed note
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 127);   // velocity
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends note-off 0x80 for NOTE_OFF + FIXED mode", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NOTE_OFF;
	preset.specs[LED_STATE_COLOR0].channel = 1;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note = 36;
	preset.specs[LED_STATE_COLOR0].value = 0;
	m->feedback.setActivePreset(preset);
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == (0x80 | 1));  // note-off, channel 1
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 36);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends CC 0xB0 for CC + FROM_SLOT mode with CC slot", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_CC;
	preset.specs[LED_STATE_COLOR0].channel = 2;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value = 100;
	m->feedback.setActivePreset(preset);
	m->trackingProcessor.setMap(MidiTrackingType::CC, 0, 74);
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == (0xB0 | 2));  // CC, channel 2
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 74);           // CC number from slot
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 100);         // value
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends note-on 0x90 for FROM_SLOT_TYPE with NOTE slot", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].channel = 0;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value = 64;
	m->feedback.setActivePreset(preset);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 60);
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x90);  // note-on, slot type is NOTE
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 60);    // note from slot
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 64);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends CC 0xB0 for FROM_SLOT_TYPE with CC slot", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].channel = 3;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value = 127;
	m->feedback.setActivePreset(preset);
	m->trackingProcessor.setMap(MidiTrackingType::CC, 0, 16);
	m->feedback.sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == (0xB0 | 3));  // CC, channel 3
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 16);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 127);
	Test::destroyModule(m);
}


// FeedbackSender::setState

TEST_CASE("setState - no-op when newState equals the cached state", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());
	m->feedback.cellLedState[0] = LED_STATE_COLOR0;

	m->feedback.setState(0, LED_STATE_COLOR0);

	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_COLOR0);
	Test::destroyModule(m);
}

TEST_CASE("setState - on change, sends off then on and updates the cache", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset(36, 127));
	m->feedback.cellLedState[0] = LED_STATE_COLOR0;

	m->feedback.setState(0, LED_STATE_COLOR1);

	// Both a note-off (for the outgoing state) and a note-on (for the incoming one) go out,
	// off first: prevSentMsg is the off, lastSentMsg is the on.
	REQUIRE(m->feedback.midiOutput.sentCount == 2);
	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_COLOR1);
	REQUIRE(m->feedback.midiOutput.prevSentMsg.bytes[0] == 0x80);  // note-off
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x90);  // note-on
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 127);
	Test::destroyModule(m);
}

TEST_CASE("setState - off message addresses the outgoing (old) state, not the incoming one", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	// Distinct notes per state id so the off-message's note number pins down which
	// state it addressed — this is the invariant setState() exists to protect: the
	// cache must still hold the OLD state when sendFeedbackOff() runs, since
	// sendFeedbackOff() resolves the note/CC number from the id passed to it.
	MidiOutPreset preset;
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		preset.specs[s].type = MIDI_OUT_NOTE_ON;
		preset.specs[s].noteMode = MIDI_OUT_FIXED;
		preset.specs[s].note = 20 + s;
	}
	m->feedback.setActivePreset(preset);
	m->feedback.cellLedState[0] = LED_STATE_COLOR0;

	m->feedback.setState(0, LED_STATE_COLOR1);

	REQUIRE(m->feedback.midiOutput.sentCount == 2);
	// prevSentMsg is the off-message, sent while the cache still held the OLD state.
	REQUIRE(m->feedback.midiOutput.prevSentMsg.bytes[0] == 0x80);
	REQUIRE(m->feedback.midiOutput.prevSentMsg.bytes[1] == 20 + LED_STATE_COLOR0);
	// lastSentMsg is the on-message, addressing the NEW state.
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x90);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 20 + LED_STATE_COLOR1);
	Test::destroyModule(m);
}

TEST_CASE("setState - scene mapId (>= MATRIX_COUNT) addresses sceneLedState, not cellLedState", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());
	m->feedback.sceneLedState[0] = LED_STATE_COLOR0;

	m->feedback.setState(MATRIX_COUNT + 0, LED_STATE_COLOR1);

	REQUIRE(m->feedback.sceneLedState[0] == LED_STATE_COLOR1);
	REQUIRE(m->feedback.cellLedState[0] == -1);  // untouched
	Test::destroyModule(m);
}

TEST_CASE("setState - first call after construction sends only the on-message (cache starts at -1)", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());

	m->feedback.setState(0, LED_STATE_COLOR0);

	// oldStateId == -1 makes sendFeedbackOff() a no-op (see its guard), so only the
	// on-message is sent.
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x90);
	Test::destroyModule(m);
}


// FeedbackSender::queueFeedbackOff / drainPendingOffs (GUI -> engine deferral)
// A preset whose note number comes from the cell's own MIDI mapping. This is the mode
// that makes the deferral order observable: the note a note-off addresses depends on
// what trackingProcessor.getMap(cellId) says AT RESOLUTION TIME, so resolving before
// vs. after a remap produces different bytes on the wire.
static void setPortAssignment(SpliceKitModule* m, int cellId, int64_t moduleId, int portId,
		engine::Port::Type type) {
	m->portAssignments[cellId].moduleId = moduleId;
	m->portAssignments[cellId].portId = portId;
	m->portAssignments[cellId].type = type;
}

static MidiOutPreset makeFromSlotPreset() {
	MidiOutPreset preset;
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		preset.specs[s].type = MIDI_OUT_NOTE_ON;
		preset.specs[s].noteMode = MIDI_OUT_FROM_SLOT;
		preset.specs[s].value = 127;
	}
	return preset;
}

TEST_CASE("queueFeedbackOff - does not send on the calling thread", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());

	m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);

	// The whole point of the queue: nothing reaches midi::Output from the GUI thread.
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("drainPendingOffs - sends a queued off and empties the queue", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset(36, 127));

	m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);
	m->feedback.drainPendingOffs();

	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x80);  // note-off
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 36);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == 0);

	// A second drain has nothing left to send.
	m->feedback.drainPendingOffs();
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	Test::destroyModule(m);
}

TEST_CASE("queueFeedbackOff - resolves the note at QUEUE time, not at drain time", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeFromSlotPreset());

	// Cell 0 is mapped to note 60 when the off is queued...
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 60);
	m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);

	// ...and remapped to note 72 before the engine drains it. This mirrors what
	// moveCell/assignPort/clearPort do: queue the off, then rewrite the mapping.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 72);
	m->feedback.drainPendingOffs();

	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x80);
	// 60, not 72 — the off must clear the LED the OLD mapping addressed. Deferring
	// resolution to drain time (queueing the state id instead of the message) would
	// send 72 here and leave the controller's note-60 LED stuck on.
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 60);
	Test::destroyModule(m);
}

TEST_CASE("queueFeedbackOff - applies the same spec filters as sendFeedbackOff", "[SpliceKit]") {
	SpliceKitModule* m = createModule();

	SECTION("no-op for an invalid state id") {
		m->feedback.setActivePreset(makeNoteOnPreset());
		m->feedback.queueFeedbackOff(0, -1);
		m->feedback.drainPendingOffs();
		REQUIRE(m->feedback.midiOutput.sentCount == 0);
	}
	SECTION("no-op when no preset is active") {
		m->feedback.setActivePresetJson("");
		m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);
		m->feedback.drainPendingOffs();
		REQUIRE(m->feedback.midiOutput.sentCount == 0);
	}
	SECTION("no-op for a CC-type spec") {
		MidiOutPreset preset;
		preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_CC;
		preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
		preset.specs[LED_STATE_COLOR0].note = 20;
		m->feedback.setActivePreset(preset);
		m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);
		m->feedback.drainPendingOffs();
		REQUIRE(m->feedback.midiOutput.sentCount == 0);
	}
	SECTION("no-op for an unmapped FROM_SLOT spec") {
		m->feedback.setActivePreset(makeFromSlotPreset());
		// Cell 0 has no mapping, so there is no note number to address.
		m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);
		m->feedback.drainPendingOffs();
		REQUIRE(m->feedback.midiOutput.sentCount == 0);
	}

	Test::destroyModule(m);
}

TEST_CASE("queueFeedbackOff - drops silently when the queue is full", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());

	// The queue holds 16; push well past that. Overflow must not throw, corrupt the
	// buffer, or block — the LED state is invalidated by every caller anyway, so the
	// next engine tick re-sends the correct on-message regardless.
	for (int i = 0; i < 100; i++) m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);
	m->feedback.drainPendingOffs();

	REQUIRE(m->feedback.midiOutput.sentCount == 16);
	Test::destroyModule(m);
}

TEST_CASE("drainPendingOffs - preserves queue order (FIFO)", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		preset.specs[s].type = MIDI_OUT_NOTE_ON;
		preset.specs[s].noteMode = MIDI_OUT_FIXED;
		preset.specs[s].note = 20 + s;
	}
	m->feedback.setActivePreset(preset);

	m->feedback.queueFeedbackOff(0, LED_STATE_COLOR0);
	m->feedback.queueFeedbackOff(1, LED_STATE_COLOR1);
	m->feedback.drainPendingOffs();

	REQUIRE(m->feedback.midiOutput.sentCount == 2);
	REQUIRE(m->feedback.midiOutput.prevSentMsg.bytes[1] == 20 + LED_STATE_COLOR0);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 20 + LED_STATE_COLOR1);
	Test::destroyModule(m);
}

TEST_CASE("process - drains pending offs before emitting new on-messages", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->lightDivider.setDivision(256);
	MidiOutPreset preset;
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		preset.specs[s].type = MIDI_OUT_NOTE_ON;
		preset.specs[s].noteMode = MIDI_OUT_FIXED;
		preset.specs[s].note = 20 + s;
	}
	m->feedback.setActivePreset(preset);
	m->feedback.queueFeedbackOff(0, LED_STATE_COLOR2);

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	// The queued off went out, and it did so before the light loop's on-messages: the
	// very first message of the tick is the off for COLOR2.
	REQUIRE(m->feedback.midiOutput.sentCount > 0);
	REQUIRE(m->feedback.pendingOffs.empty());
	Test::destroyModule(m);
}


// GUI-thread mutators defer their note-off to the engine thread

TEST_CASE("moveCell - queues offs for both cells instead of sending them", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());
	setPortAssignment(m, 0, 1, 0, engine::Port::OUTPUT);
	m->feedback.cellLedState[0] = LED_STATE_COLOR0;
	m->feedback.cellLedState[1] = LED_STATE_COLOR1;

	m->moveCell(0, 1);

	// moveCell runs on the GUI thread — it must not touch midi::Output itself.
	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	m->feedback.drainPendingOffs();
	REQUIRE(m->feedback.midiOutput.sentCount == 2);  // one per cell
	Test::destroyModule(m);
}

TEST_CASE("moveCell - the queued off addresses the note the cell had BEFORE the move", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeFromSlotPreset());
	setPortAssignment(m, 0, 1, 0, engine::Port::OUTPUT);
	// Only cell 0 has a lit LED and a mapping, so exactly one off is expected.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 60);
	m->feedback.cellLedState[0] = LED_STATE_COLOR0;

	m->moveCell(0, 1);
	m->feedback.drainPendingOffs();

	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x80);
	// Note 60 — cell 0's mapping. MIDI mappings stay on their physical button position
	// across a move, so this also documents that the off targets the source button.
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 60);
	Test::destroyModule(m);
}

TEST_CASE("assignPort - rebinding a cell queues the off rather than sending it", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());
	setPortAssignment(m, 3, 1, 0, engine::Port::OUTPUT);
	m->feedback.cellLedState[3] = LED_STATE_COLOR0;

	m->assignPort(3, 2, 1, engine::Port::INPUT);

	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	m->feedback.drainPendingOffs();
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x80);
	Test::destroyModule(m);
}

TEST_CASE("assignPort - no off is queued for a cell that had no prior assignment", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());
	m->feedback.cellLedState[3] = LED_STATE_COLOR0;

	// Cell 3 is unassigned, so assignPort takes the no-cleanup path.
	m->assignPort(3, 2, 1, engine::Port::INPUT);
	m->feedback.drainPendingOffs();

	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("clearPort - queues the off rather than sending it", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeNoteOnPreset());
	setPortAssignment(m, 7, 1, 0, engine::Port::OUTPUT);
	m->feedback.cellLedState[7] = LED_STATE_COLOR0;

	m->clearPort(7);

	REQUIRE(m->feedback.midiOutput.sentCount == 0);
	m->feedback.drainPendingOffs();
	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == 0x80);
	Test::destroyModule(m);
}

TEST_CASE("clearPort - the queued off addresses the note the cell had before clearing", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePreset(makeFromSlotPreset());
	setPortAssignment(m, 7, 1, 0, engine::Port::OUTPUT);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 7, 48);
	m->feedback.cellLedState[7] = LED_STATE_COLOR0;

	m->clearPort(7);
	m->feedback.drainPendingOffs();

	REQUIRE(m->feedback.midiOutput.sentCount == 1);
	REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == 48);
	Test::destroyModule(m);
}


// getActivePreset

TEST_CASE("getActivePreset - returns nullptr when no preset is active", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->feedback.setActivePresetJson("");
	REQUIRE(m->feedback.getActivePreset() == nullptr);
	Test::destroyModule(m);
}

TEST_CASE("getActivePreset - returns &activePreset once a preset is set", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	MidiOutPreset preset;
	preset.name = "Test";
	m->feedback.setActivePreset(preset);
	const MidiOutPreset* p = m->feedback.getActivePreset();
	REQUIRE(p != nullptr);
	REQUIRE(p == &m->feedback.activePreset);
	REQUIRE(p->name == "Test");
	Test::destroyModule(m);
}


// invalidateLedStates

TEST_CASE("invalidateLedStates - resets both LED state arrays to -1", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	for (int i = 0; i < MATRIX_COUNT; i++) m->feedback.cellLedState[i] = i;
	for (int i = 0; i < SCENE_COUNT; i++) m->feedback.sceneLedState[i] = i;
	m->feedback.invalidateLedStates();
	for (int i = 0; i < MATRIX_COUNT; i++) REQUIRE(m->feedback.cellLedState[i] == -1);
	for (int i = 0; i < SCENE_COUNT; i++) REQUIRE(m->feedback.sceneLedState[i] == -1);
	Test::destroyModule(m);
}


// getCellColorSet — auto mode vs explicit override

TEST_CASE("getCellColorSet - auto mode returns 0 (red) for OUTPUT ports", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->cellColorSet[0] = -1;  // auto
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;
	REQUIRE(m->getCellColorSet(0) == 0);
	Test::destroyModule(m);
}

TEST_CASE("getCellColorSet - auto mode returns 1 (blue) for INPUT ports", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->cellColorSet[0] = -1;  // auto
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::INPUT;
	REQUIRE(m->getCellColorSet(0) == 1);
	Test::destroyModule(m);
}

TEST_CASE("getCellColorSet - returns the explicit override when set", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	// Explicitly set to orange (2) on an INPUT port — auto would give 1.
	m->cellColorSet[0] = 2;
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::INPUT;
	REQUIRE(m->getCellColorSet(0) == 2);
	Test::destroyModule(m);
}

TEST_CASE("SpliceKitOutput::setDeviceId invalidates LED states via the FeedbackSender hook", "[SpliceKit]") {
	SpliceKitModule* m = createModule();

	// Pre-populate LED state with non-(-1) values.
	for (int i = 0; i < MATRIX_COUNT; i++) m->feedback.cellLedState[i] = LED_STATE_COLOR0;
	for (int i = 0; i < SCENE_COUNT; i++) m->feedback.sceneLedState[i] = LED_STATE_SCENE_ACTIVE;

	// setDeviceId must trigger the onDeviceChanged hook wired in FeedbackSender's own
	// constructor (midiOutput has no driver attached, so this is a no-op device switch).
	m->feedback.midiOutput.setDeviceId(-1);

	for (int i = 0; i < MATRIX_COUNT; i++) REQUIRE(m->feedback.cellLedState[i] == -1);
	for (int i = 0; i < SCENE_COUNT; i++) REQUIRE(m->feedback.sceneLedState[i] == -1);
	Test::destroyModule(m);
}


// Every shipped controller preset file must load and parse. A malformed or silently
// dropped preset would ship unnoticed, so assert the production loader (getLoadedPresets(),
// which reads presets/SpliceKit/*.ctrl.json sorted by filename) returns exactly the files on
// disk and each parsed into a valid MidiOutPreset.

TEST_CASE("controller presets - every shipped .ctrl.json loads and parses", "[SpliceKit][JSON]") {
	// Every .ctrl.json on disk must be loaded: a malformed file is silently dropped by
	// getLoadedPresets(), so a count mismatch is the only signal.
	std::vector<std::string> files = rack::system::getEntries("presets/SpliceKit");
	std::vector<std::string> ctrlFiles;
	for (const std::string& path : files) {
		if (path.size() >= 10 && path.compare(path.size() - 10, 10, ".ctrl.json") == 0) ctrlFiles.push_back(path);
	}
	REQUIRE(ctrlFiles.size() >= 1);

	const std::vector<LoadedPreset>& presets = getLoadedPresets();
	REQUIRE(presets.size() == ctrlFiles.size());

	// GENERATE runs the body once per shipped preset, so a failure names the exact preset.
	// The generator must not reference locals (GENERATE's lambda has no capture), hence
	// getLoadedPresets() directly.
	const LoadedPreset lp = GENERATE(Catch::Generators::from_range(getLoadedPresets()));
	const MidiOutPreset& p = lp.preset;
	REQUIRE(!p.name.empty());
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		const MidiOutSpec& spec = p.specs[s];
		REQUIRE((spec.type >= MIDI_OUT_NONE && spec.type <= MIDI_OUT_FROM_SLOT_TYPE));
		REQUIRE((spec.channel >= 0 && spec.channel <= 15));
		REQUIRE((spec.noteMode == MIDI_OUT_FROM_SLOT || spec.noteMode == MIDI_OUT_FIXED));
		REQUIRE((spec.note >= 0 && spec.note <= 127));
		REQUIRE((spec.value >= 0 && spec.value <= 127));
	}
}

TEST_CASE("controller presets - getLoadedPresets is ordered by filename", "[SpliceKit][JSON]") {
	// The menu lists presets in filename order (getLoadedPresets() sorts *.ctrl.json). Verify
	// by re-parsing each sorted file independently and checking the display names line up.
	std::vector<std::string> files = rack::system::getEntries("presets/SpliceKit");
	std::vector<std::string> ctrlFiles;
	for (const std::string& path : files) {
		if (path.size() >= 10 && path.compare(path.size() - 10, 10, ".ctrl.json") == 0) ctrlFiles.push_back(path);
	}
	std::sort(ctrlFiles.begin(), ctrlFiles.end());
	REQUIRE(ctrlFiles.size() >= 1);

	const std::vector<LoadedPreset>& presets = getLoadedPresets();
	REQUIRE(presets.size() == ctrlFiles.size());

	// One iteration per file: the cached preset at index i must match an independent
	// re-parse of the i-th filename-sorted file (filename order == menu order). The bounds
	// must not reference locals (GENERATE's lambda has no capture), hence the direct call.
	const size_t i = GENERATE(Catch::Generators::range<size_t>(0, getLoadedPresets().size()));

	std::vector<uint8_t> raw = rack::system::readFile(ctrlFiles[i]);
	REQUIRE(!raw.empty());
	json_error_t err;
	json_t* root = json_loads(std::string(raw.begin(), raw.end()).c_str(), 0, &err);
	REQUIRE(root != nullptr);
	MidiOutPreset mp;
	mp.fromJson(root);
	json_decref(root);
	REQUIRE(presets[i].preset.name == mp.name);
}


// End-to-end feedback: loading a shipped controller preset and driving an LED state through
// the real feedback path (FeedbackSender::setState, called by processLights()) must emit
// exactly the MIDI message the preset's spec encodes — status from type+channel, note from
// the mapping (or the fixed number), value from the spec.

TEST_CASE("controller presets - feedback emits the MIDI message each spec encodes", "[SpliceKit][JSON]") {
	// One iteration per shipped preset; load it from its raw JSON, exactly as the module does.
	const LoadedPreset lp = GENERATE(Catch::Generators::from_range(getLoadedPresets()));

	SpliceKitModule* m = createModule();
	m->feedback.setActivePresetJson(lp.json);
	// Cell 0 → note 60 (NOTE) so FROM_SLOT / FROM_SLOT_TYPE specs resolve to a number.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 60);

	for (int s = 0; s < LED_STATE_COUNT; s++) {
		CATCH_CAPTURE(s);
		const MidiOutSpec& spec = m->feedback.activePreset.specs[s];
		// Cache -1 makes setState send exactly one on-message (the off for -1 is a no-op).
		m->feedback.invalidateLedStates();
		int before = m->feedback.midiOutput.sentCount;

		m->feedback.setState(0, s);

		// Expected bytes, mirroring FeedbackSender::sendFeedback's encoding.
		int noteNum;
		MidiTrackingType slotType;
		bool sends = spec.type != MIDI_OUT_NONE && m->feedback.resolveNote(spec, 0, noteNum, slotType);
		uint8_t status = 0;
		if (sends) {
			switch (spec.type) {
				case MIDI_OUT_NOTE_ON: status = 0x90; break;
				case MIDI_OUT_NOTE_OFF: status = 0x80; break;
				case MIDI_OUT_CC: status = 0xB0; break;
				case MIDI_OUT_FROM_SLOT_TYPE:
					if (slotType == MidiTrackingType::NOTE) status = 0x90;
					else if (slotType == MidiTrackingType::CC) status = 0xB0;
					else sends = false;
					break;
				default: sends = false;
			}
		}

		if (!sends) {
			REQUIRE(m->feedback.midiOutput.sentCount == before);
			continue;
		}

		REQUIRE(m->feedback.midiOutput.sentCount == before + 1);
		REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[0] == (status | (uint8_t)(spec.channel & 0x0F)));
		REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[1] == (uint8_t)(noteNum & 0x7F));
		REQUIRE(m->feedback.midiOutput.lastSentMsg.bytes[2] == (uint8_t)(spec.value & 0x7F));
	}

	Test::destroyModule(m);
}
