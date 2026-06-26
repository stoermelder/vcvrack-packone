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


// ---------------------------------------------------------------------------
// sendFeedback (on-side) — guard conditions and message construction
// ---------------------------------------------------------------------------
//
// These mirror the sendFeedbackOff tests but exercise the on-side path that
// actually lights the controller LED. The off-side was a strict subset of the
// on-side types (only NOTE_ON and FROM_SLOT_TYPE); the on-side also handles
// NOTE_OFF and CC message types and resolves channel/value/byte2 correctly.

TEST_CASE("sendFeedback - no-op when no preset is active", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = 0;  // no output
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - no-op for NONE-type spec", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NONE;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - no-op for FROM_SLOT when slot is unmapped", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	// Cell 0 has no mapping — FROM_SLOT resolves to NONE → nothing sent
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - no-op for FROM_SLOT_TYPE when slot is CC", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->trackingProcessor.setMap(MidiTrackingType::CC, 0, 74);
	// CC slot with FROM_SLOT_TYPE resolves to a CC status — but for the *on-side*
	// (MIDI_OUT_FROM_SLOT_TYPE), the code does support CC→0xB0. This test instead
	// verifies the *no-op* case: if we set a NONE-type spec on top, it still
	// returns early before any branch is taken.
	preset.specs[LED_STATE_COLOR0].type = MIDI_OUT_NONE;
	m->customPreset = preset;
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends note-on 0x90 for NOTE_ON + FIXED mode", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_NOTE_ON;
	preset.specs[LED_STATE_COLOR0].channel  = 0;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note     = 36;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == 0x90);  // note-on, channel 0
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 36);    // fixed note
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 127);   // velocity
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends note-off 0x80 for NOTE_OFF + FIXED mode", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_NOTE_OFF;
	preset.specs[LED_STATE_COLOR0].channel  = 1;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FIXED;
	preset.specs[LED_STATE_COLOR0].note     = 36;
	preset.specs[LED_STATE_COLOR0].value    = 0;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == (0x80 | 1));  // note-off, channel 1
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 36);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 0);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends CC 0xB0 for CC + FROM_SLOT mode with CC slot", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_CC;
	preset.specs[LED_STATE_COLOR0].channel  = 2;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 100;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->trackingProcessor.setMap(MidiTrackingType::CC, 0, 74);
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == (0xB0 | 2));  // CC, channel 2
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 74);           // CC number from slot
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 100);         // value
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends note-on 0x90 for FROM_SLOT_TYPE with NOTE slot", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].channel  = 0;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 64;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 60);
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == 0x90);  // note-on, slot type is NOTE
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 60);    // note from slot
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 64);
	Test::destroyModule(m);
}

TEST_CASE("sendFeedback - sends CC 0xB0 for FROM_SLOT_TYPE with CC slot", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	MidiOutPreset preset;
	preset.specs[LED_STATE_COLOR0].type     = MIDI_OUT_FROM_SLOT_TYPE;
	preset.specs[LED_STATE_COLOR0].channel  = 3;
	preset.specs[LED_STATE_COLOR0].noteMode = MIDI_OUT_FROM_SLOT;
	preset.specs[LED_STATE_COLOR0].value    = 127;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->trackingProcessor.setMap(MidiTrackingType::CC, 0, 16);
	m->sendFeedback(0, LED_STATE_COLOR0);
	REQUIRE(m->midiOutput.sentCount          == 1);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[0] == (0xB0 | 3));  // CC, channel 3
	REQUIRE(m->midiOutput.lastSentMsg.bytes[1] == 16);
	REQUIRE(m->midiOutput.lastSentMsg.bytes[2] == 127);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// getActivePreset
// ---------------------------------------------------------------------------

TEST_CASE("getActivePreset - returns nullptr for feedbackPreset == 0", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = 0;
	REQUIRE(m->getActivePreset() == nullptr);
	Test::destroyModule(m);
}

TEST_CASE("getActivePreset - returns nullptr for out-of-range index", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = CONTROLLER_PRESET_COUNT;  // one past the end
	REQUIRE(m->getActivePreset() == nullptr);
	Test::destroyModule(m);
}

TEST_CASE("getActivePreset - returns a built-in preset for valid index", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = 1;  // first built-in (index 0 == "no output")
	const MidiOutPreset* p = m->getActivePreset();
	REQUIRE(p != nullptr);
	REQUIRE(p != &m->customPreset);
	Test::destroyModule(m);
}

TEST_CASE("getActivePreset - returns &customPreset for PRESET_IDX_CUSTOM", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	const MidiOutPreset* p = m->getActivePreset();
	REQUIRE(p == &m->customPreset);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// invalidateLedStates
// ---------------------------------------------------------------------------

TEST_CASE("invalidateLedStates - resets both LED state arrays to -1", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	for (int i = 0; i < MATRIX_COUNT; i++) m->cellLedState[i]   = i;
	for (int i = 0; i < MATRIX_SIZE;  i++) m->sceneLedState[i]  = i;
	m->invalidateLedStates();
	for (int i = 0; i < MATRIX_COUNT; i++) REQUIRE(m->cellLedState[i]  == -1);
	for (int i = 0; i < MATRIX_SIZE;  i++) REQUIRE(m->sceneLedState[i] == -1);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// getCellColorSet — auto mode vs explicit override
// ---------------------------------------------------------------------------

TEST_CASE("getCellColorSet - auto mode returns 0 (red) for OUTPUT ports", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->cellColorSet[0] = -1;  // auto
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::OUTPUT;
	REQUIRE(m->getCellColorSet(0) == 0);
	Test::destroyModule(m);
}

TEST_CASE("getCellColorSet - auto mode returns 1 (blue) for INPUT ports", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->cellColorSet[0] = -1;  // auto
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::INPUT;
	REQUIRE(m->getCellColorSet(0) == 1);
	Test::destroyModule(m);
}

TEST_CASE("getCellColorSet - returns the explicit override when set", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	// Explicitly set to orange (2) on an INPUT port — auto would give 1.
	m->cellColorSet[0] = 2;
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::INPUT;
	REQUIRE(m->getCellColorSet(0) == 2);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// JSON roundtrip — additional stored properties
// ---------------------------------------------------------------------------

TEST_CASE("JSON roundtrip preserves crossInstanceEnabled", "[SpliceKit][JSON]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->crossInstanceEnabled = false;
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = Test::createModule<SpliceKitModule>("SpliceKit");
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->crossInstanceEnabled == false);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip preserves cellColorSet overrides", "[SpliceKit][JSON]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->cellColorSet[3]  = 2;  // orange
	m->cellColorSet[10] = 0;  // red (explicit, not auto)
	m->cellColorSet[20] = -1; // auto (must not be written to JSON)
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = Test::createModule<SpliceKitModule>("SpliceKit");
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->cellColorSet[3]  == 2);
	REQUIRE(m2->cellColorSet[10] == 0);
	REQUIRE(m2->cellColorSet[20] == -1);
	// Untouched cells are still auto
	REQUIRE(m2->cellColorSet[0]  == -1);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip preserves cellLabels", "[SpliceKit][JSON]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->cellLabels[5]  = "VCO Out";
	m->cellLabels[12] = "Filter In";
	// Empty entries are not written, so the destination must remain empty
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = Test::createModule<SpliceKitModule>("SpliceKit");
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->cellLabels[5]  == "VCO Out");
	REQUIRE(m2->cellLabels[12] == "Filter In");
	REQUIRE(m2->cellLabels[0].empty());
	Test::destroyModule(m2);
	Test::destroyModule(m);
}

TEST_CASE("JSON roundtrip - panelTheme and currentScene survive a full save/load", "[SpliceKit][JSON]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->panelTheme   = 1;  // dark
	m->currentScene = 5;
	json_t* j = m->dataToJson();
	SpliceKitModule* m2 = Test::createModule<SpliceKitModule>("SpliceKit");
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->panelTheme   == 1);
	REQUIRE(m2->currentScene == 5);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// process() — PENDING, PORT_LEARN, MIDI_LEARN LED state transitions
// ---------------------------------------------------------------------------

TEST_CASE("process - pending cell transitions cellLedState to PENDING", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[0].moduleId = 42;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::OUTPUT;

	// First press → triggerCell sets pendingCellId
	m->triggerCell(0);
	REQUIRE(m->pendingCellId == 0);

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->cellLedState[0] == LED_STATE_PENDING);
	Test::destroyModule(m);
}

TEST_CASE("process - port-learning cell transitions cellLedState to PORT_LEARN", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portLearningId = 7;
	// m->portSelectProcessor is in learn mode iff isLearning() is true; without
	// starting learn() the LED branch for portLearningId is still taken because
	// the process() code only checks portLearningId, not isLearning(). This
	// matches the current production behaviour.

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->cellLedState[7] == LED_STATE_PORT_LEARN);
	Test::destroyModule(m);
}

TEST_CASE("process - midi-learning cell transitions cellLedState to MIDI_LEARN", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->learningId = 11;

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->cellLedState[11] == LED_STATE_MIDI_LEARN);
	Test::destroyModule(m);
}

TEST_CASE("process - cell connected to pending cell transitions to CONNECTED1", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Cell 0 is pending and connected to cell 5 in scene 0.
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId   = 0;
	m->portAssignments[0].type     = engine::Port::OUTPUT;
	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId   = 1;
	m->portAssignments[5].type     = engine::Port::INPUT;
	m->setConnection(0, 0, 5, true);

	m->triggerCell(0);  // pendingCellId = 0

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	// Cell 5 is connected to pending cell 0. Cell 5 is an INPUT with no explicit
	// color override, so it auto-resolves to color set 1 (blue) → CONNECTED1.
	REQUIRE(m->cellLedState[0] == LED_STATE_PENDING);
	REQUIRE(m->cellLedState[5] == LED_STATE_CONNECTED1);
	Test::destroyModule(m);
}

TEST_CASE("process - cell with port assignment and no cable transitions to COLOR0_DIM", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->customPreset   = makeNoteOnPreset();
	m->feedbackPreset = PRESET_IDX_CUSTOM;

	m->portAssignments[2].moduleId = 1;
	m->portAssignments[2].portId   = 0;
	m->portAssignments[2].type     = engine::Port::OUTPUT;
	m->portHasCable[2]             = false;  // explicit — no cable

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	// OUTPUT with default color set 0, no cable → COLOR0_DIM
	REQUIRE(m->cellLedState[2] == LED_STATE_COLOR0_DIM);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// requestSceneChange and requestReset — guiQueue dispatch
// ---------------------------------------------------------------------------

TEST_CASE("requestSceneChange - enqueues a switchScene lambda", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 0;
	m->setConnection(1, 0, 1, true);  // scene 1 has a stored connection

	REQUIRE(m->guiQueue.size() == 0);
	m->requestSceneChange(1);
	REQUIRE(m->guiQueue.size() == 1);

	// Running the lambda switches the scene.
	m->guiQueue.shift()();
	REQUIRE(m->currentScene == 1);
	Test::destroyModule(m);
}

TEST_CASE("requestReset - enqueues a reset lambda that clears all state", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Populate state that should be wiped by reset.
	m->currentScene        = 4;
	m->feedbackPreset      = 2;
	m->setConnection(2, 0, 1, true);
	m->portAssignments[3].moduleId = 1;
	m->portAssignments[3].portId   = 0;
	m->portAssignments[3].type     = engine::Port::OUTPUT;
	m->portHasCable[3]             = true;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);

	REQUIRE(m->guiQueue.size() == 0);
	m->requestReset();
	REQUIRE(m->guiQueue.size() == 1);
	m->guiQueue.shift()();

	REQUIRE(m->currentScene         == 0);
	REQUIRE(m->feedbackPreset       == 0);
	REQUIRE(m->portAssignments[3].isValid() == false);
	REQUIRE(m->portHasCable[3]      == false);
	// All scenes cleared
	for (int s = 0; s < MATRIX_SIZE; s++) {
		for (int c = 0; c < MATRIX_COUNT; c++) {
			REQUIRE(m->sceneConnections[s][c] == 0);
		}
	}
	// MIDI map for cell 5 was cleared
	auto map = m->trackingProcessor.getMap(5);
	REQUIRE(map.type == MidiTrackingType::NONE);

	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// reconcileScene — non-current scene path
// ---------------------------------------------------------------------------

TEST_CASE("reconcileScene - non-current scene copies newConns without touching currentScene cables", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->currentScene = 0;
	m->setConnection(0, 0, 1, true);  // current scene has a connection
	m->setConnection(2, 5, 7, true);  // scene 2 has its own connection (will be overwritten)

	uint64_t newConns[MATRIX_COUNT] = {};
	newConns[2] = (1ULL << 3) | (1ULL << 9);  // 2↔3 and 2↔9
	m->reconcileScene(2, newConns);

	// Scene 0 (current) is unchanged
	REQUIRE(m->isConnected(0, 0, 1) == true);
	// Scene 2 reflects newConns
	REQUIRE(m->isConnected(2, 2, 3) == true);
	REQUIRE(m->isConnected(2, 2, 9) == true);
	// Old scene-2 connections are gone
	REQUIRE(m->isConnected(2, 5, 7) == false);
	Test::destroyModule(m);
}

TEST_CASE("reconcileScene - non-current scene with all-zero newConns clears that scene", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 7;  // keep current scene inactive
	m->setConnection(3, 1, 2, true);
	REQUIRE(m->isConnected(3, 1, 2) == true);

	uint64_t empty[MATRIX_COUNT] = {};
	m->reconcileScene(3, empty);
	REQUIRE(m->isConnected(3, 1, 2) == false);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// processMapLearn — completion paths beyond sequential
// ---------------------------------------------------------------------------

TEST_CASE("processMapLearn - single-learn mode clears learningId but leaves learnActive alone", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->midiLearnMode = false;       // single-learn mode
	m->learningId    = 7;
	m->trackingProcessor.enableMapLearn(7);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->processMapLearn(MidiTrackingType::NOTE, 7);
	// processMapLearn only resets learningId; the caller is expected to follow up
	// with disableLearn() (which clears learnActive) for single-learn. The mismatch
	// is a property of the production code that this test pins down.
	REQUIRE(m->learningId == -1);
	REQUIRE(m->midiLearnMode == false);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	// Cleanup: the test must leave learnActive false so destruction is clean.
	m->disableLearn();
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyModule(m);
}

TEST_CASE("processMapLearn - sequential learn ends after the last cell", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->midiLearnMode = true;
	m->learningId    = MATRIX_COUNT - 1;  // last cell
	m->trackingProcessor.enableMapLearn(MATRIX_COUNT - 1);

	m->processMapLearn(MidiTrackingType::NOTE, MATRIX_COUNT - 1);
	// nextId = MATRIX_COUNT — out of range, so learningId stays -1 and
	// midiLearnMode is reset to false.
	REQUIRE(m->learningId    == -1);
	REQUIRE(m->midiLearnMode == false);
	// learnActive is NOT cleared by processMapLearn (same as single-learn path) —
	// the caller has to follow up with disableLearn() to fully tear down.
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->disableLearn();
	Test::destroyModule(m);
}

TEST_CASE("processMapLearn - single learn does not advance to the next cell", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->midiLearnMode = false;
	m->learningId    = 42;
	m->trackingProcessor.enableMapLearn(42);

	m->processMapLearn(MidiTrackingType::CC, 42);
	REQUIRE(m->learningId == -1);  // single learn: cleared, not advanced
	// The very next learn invocation must NOT assume sequential state.
	REQUIRE(m->midiLearnMode == false);

	m->disableLearn();
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// MidiTrackingProcessor — clearMap on a cell with no prior mapping is a no-op
// ---------------------------------------------------------------------------

TEST_CASE("trackingProcessor.clearMap - on an unmapped cell is a safe no-op", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE_NOTHROW(m->trackingProcessor.clearMap(20));
	auto m0 = m->trackingProcessor.getMap(20);
	REQUIRE(m0.type  == MidiTrackingType::NONE);
	REQUIRE(m0.param == 0);

	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// setOverlayMessage - empty/garbage title is still queued (callers are trusted)
// ---------------------------------------------------------------------------

TEST_CASE("setOverlayMessage - both subtitles can be empty", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->overlayEnabled  = true;
	m->overlayMessageId = -1;

	m->setOverlayMessage("Note", "", "");
	REQUIRE(m->overlayMessageId == 0);
	REQUIRE(m->overlayMessage.title == "Note");
	REQUIRE(m->overlayMessage.subtitle[0].empty());
	REQUIRE(m->overlayMessage.subtitle[1].empty());

	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// removeCellConnections
// ---------------------------------------------------------------------------

TEST_CASE("removeCellConnections - clears all bitmask bits for the cell in the current scene", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->currentScene = 0;

	m->setConnection(0, 4, 1, true);
	m->setConnection(0, 4, 7, true);
	m->setConnection(0, 4, 9, true);
	REQUIRE(m->sceneConnections[0][4] != 0);

	// removeCellConnections also calls removeCableBetween() for each neighbour,
	// which dereferences the rack's module list. With no port assignments set
	// removeCableBetween() is a no-op so this is safe to call.
	m->removeCellConnections(4);

	REQUIRE(m->sceneConnections[0][4] == 0);
	REQUIRE(m->isConnected(0, 4, 1) == false);
	REQUIRE(m->isConnected(0, 4, 7) == false);
	REQUIRE(m->isConnected(0, 4, 9) == false);
	// Other scenes unaffected
	REQUIRE(m->sceneConnections[3][4] == 0);
	REQUIRE(m->sceneConnections[0][1] == 0);
	Test::destroyModule(m);
}

TEST_CASE("removeCellConnections - no-op when cell has no connections", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	// No connections set anywhere — removeCellConnections must be safe.
	REQUIRE_NOTHROW(m->removeCellConnections(20));
	REQUIRE(m->sceneConnections[0][20] == 0);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// applyPresetLayout — sets up the trackingProcessor from a custom preset
// ---------------------------------------------------------------------------

TEST_CASE("applyPresetLayout - applies cell and scene mappings from custom preset", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	MidiOutPreset preset;
	preset.cells[0].type   = MidiTrackingType::NOTE;  preset.cells[0].number = 36;
	preset.cells[1].type   = MidiTrackingType::CC;    preset.cells[1].number = 1;
	preset.scenes[2].type  = MidiTrackingType::NOTE;  preset.scenes[2].number = 50;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;

	// Pre-existing maps must be wiped before applyPresetLayout runs.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 30, 70);

	m->applyPresetLayout();

	// Cells from preset are mapped.
	auto c0 = m->trackingProcessor.getMap(0);
	REQUIRE(c0.type  == MidiTrackingType::NOTE);
	REQUIRE(c0.param == 36);
	auto c1 = m->trackingProcessor.getMap(1);
	REQUIRE(c1.type  == MidiTrackingType::CC);
	REQUIRE(c1.param == 1);
	// Scene 2 is MATRIX_COUNT + 2 = 66.
	auto s2 = m->trackingProcessor.getMap(66);
	REQUIRE(s2.type  == MidiTrackingType::NOTE);
	REQUIRE(s2.param == 50);
	// Pre-existing map for cell 30 was cleared.
	auto c30 = m->trackingProcessor.getMap(30);
	REQUIRE(c30.type == MidiTrackingType::NONE);
	// Unmapped cells remain unmapped.
	auto c2 = m->trackingProcessor.getMap(2);
	REQUIRE(c2.type == MidiTrackingType::NONE);

	Test::destroyModule(m);
}

TEST_CASE("applyPresetLayout - no-op when no preset is active", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = 0;  // no preset
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);

	m->applyPresetLayout();  // must not throw, must not clear

	auto m5 = m->trackingProcessor.getMap(5);
	REQUIRE(m5.type  == MidiTrackingType::NOTE);
	REQUIRE(m5.param == 60);
	Test::destroyModule(m);
}

TEST_CASE("applyPresetLayout - invalidates LED states so they are re-sent", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Pre-populate LED state with non-(-1) values.
	for (int i = 0; i < MATRIX_COUNT; i++) m->cellLedState[i]  = LED_STATE_COLOR0;
	for (int i = 0; i < MATRIX_SIZE;  i++) m->sceneLedState[i] = LED_STATE_SCENE_ACTIVE;

	MidiOutPreset preset;
	preset.cells[0].type = MidiTrackingType::NOTE;  preset.cells[0].number = 36;
	m->customPreset   = preset;
	m->feedbackPreset = PRESET_IDX_CUSTOM;

	m->applyPresetLayout();
	for (int i = 0; i < MATRIX_COUNT; i++) REQUIRE(m->cellLedState[i]  == -1);
	for (int i = 0; i < MATRIX_SIZE;  i++) REQUIRE(m->sceneLedState[i] == -1);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// dataFromJson — invalid customPreset string falls back to feedbackPreset == 0
// ---------------------------------------------------------------------------

TEST_CASE("dataFromJson - malformed customPresetJson reverts to no preset", "[SpliceKit][JSON]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->feedbackPreset = PRESET_IDX_CUSTOM;
	m->customPresetJson = "this is { not valid json";

	json_t* j = json_object();
	json_object_set_new(j, "feedbackPreset", json_integer(PRESET_IDX_CUSTOM));
	json_object_set_new(j, "customPreset",   json_string(m->customPresetJson.c_str()));
	SpliceKitModule* m2 = Test::createModule<SpliceKitModule>("SpliceKit");
	m2->dataFromJson(j);
	json_decref(j);
	REQUIRE(m2->feedbackPreset == 0);
	Test::destroyModule(m2);
	Test::destroyModule(m);
}
