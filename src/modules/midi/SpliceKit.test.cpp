#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "SpliceKit.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::SpliceKit;

SYNC_MODEL(modelSpliceKit, "SpliceKit");
Test::TestContext<> testContext;


TEST_CASE("Construction and initialization", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitWidget* mw = Test::createWidget<SpliceKitWidget>("SpliceKit");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);
	REQUIRE(m->currentScene == 0);
	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->buttonMode == SpliceKitModule::BUTTON_TOGGLE);
	REQUIRE(m->overlayEnabled == true);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}


TEST_CASE("isConnected and setConnection bitmask", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE(m->isConnected(0, 0, 1) == false);

	m->setConnection(0, 0, 1, true);
	REQUIRE(m->isConnected(0, 0, 1) == true);
	REQUIRE(m->isConnected(0, 1, 0) == true);  // symmetric

	m->setConnection(0, 0, 1, false);
	REQUIRE(m->isConnected(0, 0, 1) == false);
	REQUIRE(m->isConnected(0, 1, 0) == false);

	// Different scene is unaffected
	REQUIRE(m->isConnected(1, 0, 1) == false);

	Test::destroyModule(m);
}


TEST_CASE("clearPending resets pendingCellId and pendingCellIsPhysical", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->pendingCellId = 5;
	m->pendingCellIsPhysical = true;
	m->clearPending();

	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->pendingCellIsPhysical == false);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - first press sets pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Assign cell 3 a port so triggerCell doesn't bail early
	m->portAssignments[3].moduleId = 42;
	m->portAssignments[3].portId   = 0;
	m->portAssignments[3].type     = engine::Port::OUTPUT;

	REQUIRE(m->pendingCellId == -1);
	m->triggerCell(3);
	REQUIRE(m->pendingCellId == 3);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - pressing same cell cancels pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[2].moduleId = 42;
	m->portAssignments[2].portId   = 0;
	m->portAssignments[2].type     = engine::Port::OUTPUT;

	m->triggerCell(2);
	REQUIRE(m->pendingCellId == 2);

	m->triggerCell(2);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - unassigned cell is ignored", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Cell 10 has no port assignment
	m->triggerCell(10);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - MIDI note-on sets pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId   = 0;
	m->portAssignments[5].type     = engine::Port::OUTPUT;

	m->processMapUpdate(MidiTrackingType::NOTE, 5, 100);
	REQUIRE(m->pendingCellId == 5);
	REQUIRE(m->pendingCellIsPhysical == false);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - momentary MIDI note-off clears pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->buttonMode = SpliceKitModule::BUTTON_MOMENTARY;
	m->portAssignments[7].moduleId = 1;
	m->portAssignments[7].portId   = 0;
	m->portAssignments[7].type     = engine::Port::OUTPUT;

	// Note-on: cell 7 becomes pending
	m->processMapUpdate(MidiTrackingType::NOTE, 7, 80);
	REQUIRE(m->pendingCellId == 7);

	// Note-off: momentary mode must clear it
	m->processMapUpdate(MidiTrackingType::NOTE, 7, 0);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - toggle mode ignores note-off", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->buttonMode = SpliceKitModule::BUTTON_TOGGLE;
	m->portAssignments[4].moduleId = 1;
	m->portAssignments[4].portId   = 0;
	m->portAssignments[4].type     = engine::Port::OUTPUT;

	m->processMapUpdate(MidiTrackingType::NOTE, 4, 64);
	REQUIRE(m->pendingCellId == 4);

	// Note-off in toggle mode must NOT clear pending
	m->processMapUpdate(MidiTrackingType::NOTE, 4, 0);
	REQUIRE(m->pendingCellId == 4);

	Test::destroyModule(m);
}


TEST_CASE("JSON roundtrip preserves scene and button mode", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->currentScene    = 3;
	m->buttonMode      = SpliceKitModule::BUTTON_MOMENTARY;
	m->overlayEnabled  = false;
	m->setConnection(3, 1, 5, true);

	json_t* j = m->dataToJson();

	SpliceKitModule* m2 = Test::createModule<SpliceKitModule>("SpliceKit");
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->currentScene   == 3);
	REQUIRE(m2->buttonMode     == SpliceKitModule::BUTTON_MOMENTARY);
	REQUIRE(m2->overlayEnabled == false);
	REQUIRE(m2->isConnected(3, 1, 5) == true);
	REQUIRE(m2->isConnected(3, 5, 1) == true);  // symmetric
	REQUIRE(m2->isConnected(0, 1, 5) == false);  // other scene untouched

	Test::destroyModule(m2);
	Test::destroyModule(m);
}


TEST_CASE("JSON roundtrip preserves MIDI maps", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 36);
	m->trackingProcessor.setMap(MidiTrackingType::CC,   1, 74);

	json_t* j = m->dataToJson();

	SpliceKitModule* m2 = Test::createModule<SpliceKitModule>("SpliceKit");
	m2->dataFromJson(j);
	json_decref(j);

	auto map0 = m2->trackingProcessor.getMap(0);
	REQUIRE(map0.type  == MidiTrackingType::NOTE);
	REQUIRE(map0.param == 36);

	auto map1 = m2->trackingProcessor.getMap(1);
	REQUIRE(map1.type  == MidiTrackingType::CC);
	REQUIRE(map1.param == 74);

	Test::destroyModule(m2);
	Test::destroyModule(m);
}


TEST_CASE("overlayEnabled gates setOverlayMessage", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->overlayEnabled  = false;
	m->overlayMessageId = -1;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == -1);  // not triggered

	m->overlayEnabled = true;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == 0);  // triggered

	Test::destroyModule(m);
}


TEST_CASE("enableLearn and disableLearn", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE(m->learningId == -1);

	m->enableLearn(5);
	REQUIRE(m->learningId == 5);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->disableLearn();
	REQUIRE(m->learningId == -1);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyModule(m);
}


TEST_CASE("startGlobalLearn advances through cells via processMapLearn", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->startGlobalLearn();
	REQUIRE(m->midiLearnMode == true);
	REQUIRE(m->learningId == 0);

	// Simulate learning cell 0
	m->processMapLearn(MidiTrackingType::NOTE, 0);
	REQUIRE(m->learningId == 1);

	// Disable and verify cleanup
	m->disableLearn();
	REQUIRE(m->midiLearnMode == false);
	REQUIRE(m->learningId == -1);

	Test::destroyModule(m);
}


TEST_CASE("portAssignment isValid and clear", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE(m->portAssignments[0].isValid() == false);

	m->portAssignments[0].moduleId = 10;
	m->portAssignments[0].portId   = 2;
	m->portAssignments[0].type     = engine::Port::INPUT;
	REQUIRE(m->portAssignments[0].isValid() == true);

	m->portAssignments[0].clear();
	REQUIRE(m->portAssignments[0].isValid() == false);

	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// sendFeedbackOff — guard conditions
// ---------------------------------------------------------------------------

TEST_CASE("sendFeedbackOff - no-op for invalid state id (-1)", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sendFeedbackOff(0, -1);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op when feedback preset is off", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = 0;  // preset index 0 == no output
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for MIDI_OUT_NONE spec", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NONE;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for CC-type spec", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_CC;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note     = 20;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for NOTE_OFF-type spec", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_NOTE_OFF;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note     = 36;
	preset.specs[LED_STATE_COLOR0].value    = 0;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op when FROM_SLOT note mode has no mapping", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	// Cell 0 has no MIDI mapping — FROM_SLOT resolves to NONE → nothing sent
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - no-op for FROM_SLOT_TYPE when slot is CC", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->trackingProcessor.setMap(MidiTrackingType::CC, 0, 74);
	// CC slot with FROM_SLOT_TYPE resolves to a CC status — must be skipped
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - sends note-off 0x80 for NOTE_ON + FIXED mode", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].channel  = 0;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note     = 36;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == 0x80);  // note-off, channel 0
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 36);    // fixed note
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 0);     // velocity 0
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - sends note-off with note from slot mapping (FROM_SLOT)", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR1].type     = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR1].channel  = 2;
	preset.specs[LED_STATE_COLOR1].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR1].value    = 100;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);
	m->sendFeedbackOff(5, LED_STATE_COLOR1);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == (0x80 | 2));  // note-off, channel 2
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 60);           // note from slot
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedbackOff - sends note-off for FROM_SLOT_TYPE with NOTE slot", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].channel  = 1;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 48);
	m->sendFeedbackOff(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == (0x80 | 1));  // note-off, channel 1
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 48);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 0);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// sendFeedbackOff integration — state transitions in process()
// ---------------------------------------------------------------------------

// Helper: build a preset with NOTE_ON + FIXED mode for all states.
static MidiOutPreset makeNoteOnPreset(int note = 36, int value = 127) {
	MidiOutPreset preset;
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		preset.specs[s].type     = MIDI_OUT_NOTE_ON;
		preset.specs[s].noteMode = MIDI_OUT_FIXED;
		preset.specs[s].note     = note;
		preset.specs[s].value    = value;
	}
	return preset;
}

TEST_CASE("process - unassigned cell transitions cellLedState to OFF", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Pre-set to a non-OFF state to force a transition
	m->cellLedState[0] = LED_STATE_COLOR0;

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->cellLedState[0] == LED_STATE_OFF);
	Test::destroyModule(m);
}

TEST_CASE("process - assigned cell without cable transitions to DIM state", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[0].moduleId = 42;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::OUTPUT;
	m->portHasCable[0]             = false;

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->cellLedState[0] == LED_STATE_COLOR0_DIM);
	Test::destroyModule(m);
}

TEST_CASE("process - cellLedState transitions from old state to OFF when cell unassigned", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->customPreset   = makeNoteOnPreset();
	m->feedbackPreset = PRESET_IDX_CUSTOM;

	// Simulate a previous state that the LED was in
	m->cellLedState[5] = LED_STATE_COLOR1;

	// Cell 5 is unassigned; process() must send note-off for COLOR1 then note-on for OFF
	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->cellLedState[5] == LED_STATE_OFF);
	Test::destroyModule(m);
}

TEST_CASE("process - scene cellLedState transitions to SCENE_ACTIVE for currentScene", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->currentScene     = 2;
	m->sceneLedState[2] = -1;  // force a state send

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->sceneLedState[2] == LED_STATE_SCENE_ACTIVE);
	Test::destroyModule(m);
}

TEST_CASE("process - scene state transitions from active to dim after scene switch", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->customPreset   = makeNoteOnPreset();
	m->feedbackPreset = PRESET_IDX_CUSTOM;

	m->currentScene = 0;
	// Give scene 1 a stored connection so it becomes DIM
	m->setConnection(1, 0, 1, true);

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->sceneLedState[0] == LED_STATE_SCENE_ACTIVE);
	REQUIRE(m->sceneLedState[1] == LED_STATE_SCENE_DIM);
	Test::destroyModule(m);
}
