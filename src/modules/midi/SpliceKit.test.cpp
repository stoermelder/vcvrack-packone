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


TEST_CASE("Preset JSON null-guards", "[SpliceKit][JSON]") {
	auto module = Test::createModule<SpliceKitModule>("SpliceKit");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
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

// ---------------------------------------------------------------------------
// moveCell
// ---------------------------------------------------------------------------

TEST_CASE("moveCell - no-op when source equals target", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[4].moduleId = 42;
	m->portAssignments[4].portId   = 0;
	m->portAssignments[4].type     = engine::Port::OUTPUT;
	m->cellLabels[4]               = "keep";
	m->cellColorSet[4]             = 1;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 4, 36);

	m->moveCell(4, 4);

	REQUIRE(m->portAssignments[4].moduleId == 42);
	REQUIRE(m->cellLabels[4]              == "keep");
	REQUIRE(m->cellColorSet[4]            == 1);
	auto map = m->trackingProcessor.getMap(4);
	REQUIRE(map.type  == MidiTrackingType::NOTE);
	REQUIRE(map.param == 36);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - port assignment is transferred and source is cleared", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[3].moduleId = 42;
	m->portAssignments[3].portId   = 1;
	m->portAssignments[3].type     = engine::Port::OUTPUT;
	// toId has an existing (discarded) assignment
	m->portAssignments[7].moduleId = 99;
	m->portAssignments[7].portId   = 0;
	m->portAssignments[7].type     = engine::Port::INPUT;

	m->moveCell(3, 7);

	REQUIRE(m->portAssignments[7].moduleId == 42);
	REQUIRE(m->portAssignments[7].portId   == 1);
	REQUIRE(m->portAssignments[7].type     == engine::Port::OUTPUT);
	REQUIRE(m->portAssignments[3].isValid() == false);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - MIDI mappings stay on their original cells", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId   = 0;
	m->portAssignments[5].type     = engine::Port::OUTPUT;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);
	m->trackingProcessor.setMap(MidiTrackingType::CC,   9, 74);

	m->moveCell(5, 9);

	// fromId keeps its mapping (physical button position unchanged).
	auto src = m->trackingProcessor.getMap(5);
	REQUIRE(src.type  == MidiTrackingType::NOTE);
	REQUIRE(src.param == 60);

	// toId keeps its own mapping (not overwritten by fromId's).
	auto dst = m->trackingProcessor.getMap(9);
	REQUIRE(dst.type  == MidiTrackingType::CC);
	REQUIRE(dst.param == 74);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - label and color are transferred and source is reset", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[2].moduleId = 1;
	m->portAssignments[2].portId   = 0;
	m->portAssignments[2].type     = engine::Port::OUTPUT;
	m->cellLabels[2]               = "VCO Out";
	m->cellColorSet[2]             = 2;  // orange

	m->moveCell(2, 9);

	REQUIRE(m->cellLabels[9]   == "VCO Out");
	REQUIRE(m->cellColorSet[9] == 2);
	REQUIRE(m->cellLabels[2].empty());
	REQUIRE(m->cellColorSet[2] == -1);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - scene connections are redirected in current scene", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->setConnection(0, 0, 2, true);
	m->setConnection(0, 0, 4, true);

	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::OUTPUT;

	m->moveCell(0, 5);

	// toId inherits fromId's connections.
	REQUIRE(m->isConnected(0, 5, 2) == true);
	REQUIRE(m->isConnected(0, 5, 4) == true);
	// fromId is cleared.
	REQUIRE(m->isConnected(0, 0, 2) == false);
	REQUIRE(m->isConnected(0, 0, 4) == false);
	// Neighbours now point at toId, not fromId.
	REQUIRE(m->isConnected(0, 2, 5) == true);
	REQUIRE(m->isConnected(0, 2, 0) == false);
	REQUIRE(m->isConnected(0, 4, 5) == true);
	REQUIRE(m->isConnected(0, 4, 0) == false);

	// fromId's bitmask was never zeroed — its connections survived as toId's.
	// In production this means the physical cables remain in the patch untouched.
	REQUIRE(m->isConnected(0, 5, 2) == true);
	REQUIRE(m->isConnected(0, 5, 4) == true);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - toId's existing connections are discarded", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->setConnection(0, 3, 7, true);  // fromId=3 → cell 7
	m->setConnection(0, 5, 8, true);  // toId=5 existing connections — discarded
	m->setConnection(0, 5, 9, true);

	m->portAssignments[3].moduleId = 1;
	m->portAssignments[3].portId   = 0;
	m->portAssignments[3].type     = engine::Port::OUTPUT;

	m->moveCell(3, 5);

	// toId has fromId's connection only.
	REQUIRE(m->isConnected(0, 5, 7) == true);
	REQUIRE(m->isConnected(0, 5, 8) == false);
	REQUIRE(m->isConnected(0, 5, 9) == false);
	// Discarded neighbours no longer reference toId.
	REQUIRE(m->isConnected(0, 8, 5) == false);
	REQUIRE(m->isConnected(0, 9, 5) == false);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - fromId-toId connection is dropped and not a self-connection", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->setConnection(0, 0, 3, true);  // fromId↔toId: will not become self-connection
	m->setConnection(0, 0, 7, true);  // other connection — should transfer

	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::OUTPUT;

	m->moveCell(0, 3);

	REQUIRE(m->isConnected(0, 3, 3) == false);  // no self-connection
	REQUIRE(m->isConnected(0, 3, 7) == true);   // other connection transferred
	REQUIRE(m->isConnected(0, 0, 7) == false);  // fromId cleared
	REQUIRE(m->isConnected(0, 0, 3) == false);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - connections transferred across all scenes independently", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Different connection topology in scenes 1, 2, 3.
	m->setConnection(1, 10, 20, true);
	m->setConnection(2, 10, 30, true);
	m->setConnection(3, 10, 40, true);

	m->portAssignments[10].moduleId = 1;
	m->portAssignments[10].portId   = 0;
	m->portAssignments[10].type     = engine::Port::OUTPUT;

	m->moveCell(10, 50);

	REQUIRE(m->isConnected(1, 50, 20) == true);
	REQUIRE(m->isConnected(2, 50, 30) == true);
	REQUIRE(m->isConnected(3, 50, 40) == true);
	REQUIRE(m->isConnected(1, 10, 20) == false);
	REQUIRE(m->isConnected(2, 10, 30) == false);
	REQUIRE(m->isConnected(3, 10, 40) == false);

	Test::destroyModule(m);
}


TEST_CASE("moveCell - overlay message is posted", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[1].moduleId = 1;
	m->portAssignments[1].portId   = 0;
	m->portAssignments[1].type     = engine::Port::OUTPUT;
	m->overlayEnabled   = true;
	m->overlayMessageId = -1;

	m->moveCell(1, 6);

	REQUIRE(m->overlayMessageId == 0);

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


// ---------------------------------------------------------------------------
// copyScene
// ---------------------------------------------------------------------------

TEST_CASE("copyScene - no-op when src equals dst", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->setConnection(2, 0, 1, true);
	m->copyScene(2, 2);
	REQUIRE(m->isConnected(2, 0, 1) == true);  // unchanged
	Test::destroyModule(m);
}

TEST_CASE("copyScene - copies connections from src to dst", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 7;  // keep src and dst both inactive
	m->setConnection(1, 0, 5, true);
	m->setConnection(1, 3, 7, true);
	m->copyScene(1, 4);
	REQUIRE(m->isConnected(4, 0, 5) == true);
	REQUIRE(m->isConnected(4, 3, 7) == true);
	REQUIRE(m->isConnected(4, 5, 0) == true);  // symmetric
	Test::destroyModule(m);
}

TEST_CASE("copyScene - overwrites existing connections in dst", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 7;
	m->setConnection(0, 1, 2, true);  // src has 1↔2
	m->setConnection(3, 4, 5, true);  // dst has 4↔5 (will be overwritten)
	m->copyScene(0, 3);
	REQUIRE(m->isConnected(3, 1, 2) == true);
	REQUIRE(m->isConnected(3, 4, 5) == false);
	Test::destroyModule(m);
}

TEST_CASE("copyScene - src scene remains unchanged", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 7;
	m->setConnection(2, 0, 3, true);
	m->copyScene(2, 5);
	REQUIRE(m->isConnected(2, 0, 3) == true);
	Test::destroyModule(m);
}

TEST_CASE("copyScene - unrelated scenes are not affected", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 7;
	m->setConnection(1, 0, 5, true);
	m->setConnection(6, 10, 20, true);
	m->copyScene(1, 4);
	REQUIRE(m->isConnected(6, 10, 20) == true);
	REQUIRE(m->isConnected(4, 10, 20) == false);
	Test::destroyModule(m);
}

TEST_CASE("copyScene - can copy to current scene (bitmask transfer, no cables in test)", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	// src=1 is inactive, dst=4 is current. reconcileScene always memcpy's newConns.
	m->currentScene = 4;
	m->setConnection(1, 0, 3, true);
	m->copyScene(1, 4);
	REQUIRE(m->isConnected(4, 0, 3) == true);
	Test::destroyModule(m);
}

TEST_CASE("copyScene - posts overlay message", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->overlayEnabled   = true;
	m->overlayMessageId = -1;
	m->copyScene(1, 3);
	REQUIRE(m->overlayMessageId == 0);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// requestCopyScene
// ---------------------------------------------------------------------------

TEST_CASE("requestCopyScene - enqueues a guiQueue item that performs the copy", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 7;
	m->setConnection(2, 0, 1, true);
	REQUIRE(m->guiQueue.size() == 0);
	m->requestCopyScene(2, 5);
	REQUIRE(m->guiQueue.size() == 1);
	m->guiQueue.shift()();
	REQUIRE(m->isConnected(5, 0, 1) == true);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// MIDI scene copy detection (processMapUpdate)
// ---------------------------------------------------------------------------

TEST_CASE("processMapUpdate - single scene activation sets pendingMidiSceneId and queues scene change", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	REQUIRE(m->pendingMidiSceneId == -1);
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 2, 100);
	REQUIRE(m->pendingMidiSceneId == 2);
	REQUIRE(m->guiQueue.size() == 1);  // switchScene enqueued
	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - note-off clears pendingMidiSceneId", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 3, 100);
	REQUIRE(m->pendingMidiSceneId == 3);
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 3, 0);
	REQUIRE(m->pendingMidiSceneId == -1);
	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - note-off for a different scene is a no-op", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->pendingMidiSceneId = 4;
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 7, 0);  // different scene
	REQUIRE(m->pendingMidiSceneId == 4);  // unchanged
	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - two activations without release queues scene copy", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// First activation: normal scene change, pending set.
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 1, 100);
	REQUIRE(m->pendingMidiSceneId == 1);
	REQUIRE(m->guiQueue.size() == 1);

	// Second activation (different scene, no release): copy queued, pending cleared.
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 5, 100);
	REQUIRE(m->pendingMidiSceneId == -1);
	REQUIRE(m->guiQueue.size() == 2);  // switchScene(1) + copyScene(1, 5)

	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - same scene activated twice without release is not a copy", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 4, 100);
	REQUIRE(m->pendingMidiSceneId == 4);
	size_t queueSize = m->guiQueue.size();

	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 4, 100);  // same scene again
	REQUIRE(m->pendingMidiSceneId == 4);         // pending still set to same scene
	REQUIRE(m->guiQueue.size() == queueSize + 1);  // another switchScene, not copyScene

	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - after a copy, the next activation is treated normally", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Trigger a copy: press scene 1, then scene 2 (no release).
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 1, 100);
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 2, 100);
	REQUIRE(m->pendingMidiSceneId == -1);  // consumed by copy

	// Next activation should behave as a normal scene change.
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 6, 100);
	REQUIRE(m->pendingMidiSceneId == 6);

	Test::destroyModule(m);
}
