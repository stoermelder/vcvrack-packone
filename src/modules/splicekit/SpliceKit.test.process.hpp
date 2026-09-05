// SpliceKit.process.test.hpp — process() behavior and visual resolution.
// LED state transitions, resolveCellVisual/resolveSceneVisual precedence,
// requestSceneChange and resetModuleState.

#include "SpliceKit.test.hpp"


// sendFeedbackOff integration — state transitions in process()

TEST_CASE("process - unassigned cell transitions cellLedState to OFF", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);  // lightDivider defaults sample-rate-relative; pin for the loop below

	// Pre-set to a non-OFF state to force a transition
	m->feedback.cellLedState[0] = LED_STATE_COLOR0;

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_OFF);
}

TEST_CASE("process - assigned cell without cable transitions to DIM state", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);

	m->portAssignments[0].moduleId = 42;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;
	m->portHasCable[0] = false;

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_COLOR0_DIM);
}

TEST_CASE("process - cellLedState transitions from old state to OFF when cell unassigned", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);
	m->feedback.setActivePreset(makeNoteOnPreset());

	// Simulate a previous state that the LED was in
	m->feedback.cellLedState[5] = LED_STATE_COLOR1;

	// Cell 5 is unassigned; process() must send note-off for COLOR1 then note-on for OFF
	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[5] == LED_STATE_OFF);
}

TEST_CASE("process - scene cellLedState transitions to SCENE_ACTIVE for currentScene", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);

	m->sceneStore.current = 2;
	m->feedback.sceneLedState[2] = -1;  // force a state send

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.sceneLedState[2] == LED_STATE_SCENE_ACTIVE);
}

TEST_CASE("process - physical scene button press works normally when not linked", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->sceneStore.current = 0;  // sceneLinkMasterId stays -1 (default)

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // establish trigger baseline at low

	m->params[SpliceKitModule::PARAM_SCENE + 1].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();  // rising edge on the next divided tick

	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 1);  // switchScene(1) queued
	m->taskProcessorUi.internalQueue.queue.shift()();
	REQUIRE(m->sceneStore.current == 1);
}

TEST_CASE("process - physical scene button press is ignored while following a scene link master", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->sceneLinkMasterId = 999;  // any id — process() only checks it's >= 0
	m->sceneStore.current = 0;

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // establish trigger baseline at low

	m->params[SpliceKitModule::PARAM_SCENE + 1].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();  // rising edge on the next divided tick

	REQUIRE(m->sceneStore.current == 0);   // unaffected — scene button press ignored while linked
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);
}


// physical matrix button release: in momentary mode a release clears the pending selection
// (processCellButtons()); in toggle mode it does not. The MIDI note-off analogue is in
// SpliceKit.midi.test.hpp; this is the physical-button path.

TEST_CASE("process - momentary mode: releasing a pressed cell clears the pending selection", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->buttonMode = SpliceKitModule::BUTTON_MOMENTARY;
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // initialize trigger at low

	// Press cell 0 → rising edge arms it.
	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == 0);
	REQUIRE(m->pendingCellIsPhysical);

	// Release cell 0 → momentary mode clears the pending selection.
	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(0.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == -1);
}

TEST_CASE("process - toggle mode: releasing a pressed cell keeps the pending selection", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	// buttonMode defaults to BUTTON_TOGGLE
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // initialize trigger at low

	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == 0);

	// Release — toggle mode does not clear.
	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(0.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == 0);

}

// two-press matrix-cell flow through process(): the physical-button path via
// processCellButtons(), where the first press arms the cell and a second press on another
// cell toggles the connection. The MIDI analogue is in SpliceKit.midi.test.hpp; this drives
// params[PARAM_MATRIX] as a real button press would.

TEST_CASE("process - first press arms the cell, second press creates the cable", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // initialize triggers at low

	// First press: cell 0 → rising edge arms it.
	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == 0);

	// Second press on a different cell → toggleConnection(0, 1) is queued and pending cleared.
	m->params[SpliceKitModule::PARAM_MATRIX + 1].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == -1);

	// Draining the GUI queue runs the queued toggleConnection → a real cable appears.
	m->taskProcessorUi.step();
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
}

// physical matrix button edge case: pressing the same cell again cancels the armed selection
// (triggerCell's pendingCellId == id branch) — the physical counterpart of the MIDI test.

TEST_CASE("process - pressing the same cell again cancels the selection", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // initialize trigger at low

	// Press cell 0 → arms it.
	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == 0);

	// Release and press the same cell again → cancels the selection (toggle mode keeps the
	// release from clearing, so the second rising edge sees pendingCellId == 0).
	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(0.f);
	for (int i = 0; i < 256; i++) engine.step();
	m->params[SpliceKitModule::PARAM_MATRIX + 0].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();
	REQUIRE(m->pendingCellId == -1);
}


TEST_CASE("process - scene state transitions from active to dim after scene switch", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);
	m->feedback.setActivePreset(makeNoteOnPreset());

	m->sceneStore.current = 0;
	// Give scene 1 a stored connection so it becomes DIM
	m->sceneStore.setConnection(1, 0, 1, true);

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.sceneLedState[0] == LED_STATE_SCENE_ACTIVE);
	REQUIRE(m->feedback.sceneLedState[1] == LED_STATE_SCENE_DIM);
}


// process() — PENDING LED state transition. The PORT_LEARN/MIDI_LEARN transitions are
// covered by resolveCellVisual's precedence test below, which also pins their resolution order.

TEST_CASE("process - pending cell transitions cellLedState to PENDING", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);

	m->portAssignments[0].moduleId = 42;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;

	// First press → triggerCell sets pendingCellId
	m->triggerCell(0);
	REQUIRE(m->pendingCellId == 0);

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_PENDING);
}

TEST_CASE("process - cell connected to pending cell transitions to CONNECTED1", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);

	// Cell 0 is pending and connected to cell 5 in scene 0.
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;
	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId = 1;
	m->portAssignments[5].type = engine::Port::INPUT;
	m->sceneStore.setConnection(0, 0, 5, true);

	m->triggerCell(0);  // pendingCellId = 0

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	// Cell 5 is connected to pending cell 0. Cell 5 is an INPUT with no explicit
	// color override, so it auto-resolves to color set 1 (blue) → CONNECTED1.
	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_PENDING);
	REQUIRE(m->feedback.cellLedState[5] == LED_STATE_CONNECTED1);
}

TEST_CASE("resolveCellVisual - precedence order pending > connected > port-learn > midi-learn > assigned > off", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	// Cell 5 is connected to cell 0 in the current scene, and every lower-precedence
	// flag is also set on cell 5, so this only passes if resolveCellVisual checks
	// precedence in the documented order rather than any other.
	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;
	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId = 1;
	m->portAssignments[5].type = engine::Port::INPUT;
	m->portHasCable[5] = true;
	m->sceneStore.setConnection(0, 0, 5, true);

	m->pendingCellId = 0;
	m->portLearningId = 5;
	m->learningId = 5;

	// pending: cell 0 itself.
	CellVisual pending = m->resolveCellVisual(0, true, true);
	REQUIRE(pending.stateId == LED_STATE_PENDING);

	// connected-to-pending beats port-learn and midi-learn, both also set on cell 5.
	CellVisual connected = m->resolveCellVisual(5, true, true);
	REQUIRE(connected.stateId == LED_STATE_CONNECTED1);

	// port-learn beats midi-learn and assigned, once connected-to-pending is removed.
	m->pendingCellId = -1;
	CellVisual portLearn = m->resolveCellVisual(5, true, true);
	REQUIRE(portLearn.stateId == LED_STATE_PORT_LEARN);

	// midi-learn beats assigned, once port-learn is removed.
	m->portLearningId = -1;
	CellVisual midiLearn = m->resolveCellVisual(5, true, true);
	REQUIRE(midiLearn.stateId == LED_STATE_MIDI_LEARN);

	// assigned beats off, once midi-learn is removed.
	m->learningId = -1;
	CellVisual assigned = m->resolveCellVisual(5, true, true);
	REQUIRE(assigned.stateId == LED_STATE_COLOR1);

	// off: cell 6 has no assignment and none of the above flags.
	CellVisual off = m->resolveCellVisual(6, true, true);
	REQUIRE(off.stateId == LED_STATE_OFF);
}

TEST_CASE("resolveSceneVisual - precedence order midi-learn > active > has-connections > off", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;
	m->portAssignments[1].moduleId = 1;
	m->portAssignments[1].portId = 1;
	m->portAssignments[1].type = engine::Port::INPUT;
	m->sceneStore.setConnection(2, 0, 1, true);

	m->sceneStore.current = 2;
	m->learningId = MATRIX_COUNT + 2;

	// midi-learn beats active, both set on scene 2.
	SceneVisual midiLearn = m->resolveSceneVisual(2, true);
	REQUIRE(midiLearn.stateId == LED_STATE_MIDI_LEARN);

	// active beats has-connections, once midi-learn is removed.
	m->learningId = -1;
	SceneVisual active = m->resolveSceneVisual(2, true);
	REQUIRE(active.stateId == LED_STATE_SCENE_ACTIVE);

	// has-connections beats off, once active is removed (scene 2 no longer current).
	m->sceneStore.current = 0;
	SceneVisual hasConn = m->resolveSceneVisual(2, true);
	REQUIRE(hasConn.stateId == LED_STATE_SCENE_DIM);

	// off: scene 3 has no connections and is not current or midi-learning.
	SceneVisual off = m->resolveSceneVisual(3, true);
	REQUIRE(off.stateId == LED_STATE_OFF);

}

TEST_CASE("process - cell with port assignment and no cable transitions to COLOR0_DIM", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->lightDivider.setDivision(256);
	m->feedback.setActivePreset(makeNoteOnPreset());

	m->portAssignments[2].moduleId = 1;
	m->portAssignments[2].portId = 0;
	m->portAssignments[2].type = engine::Port::OUTPUT;
	m->portHasCable[2] = false;  // explicit — no cable

	Test::SimpleEngine engine;
	engine.addModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	// OUTPUT with default color set 0, no cable → COLOR0_DIM
	REQUIRE(m->feedback.cellLedState[2] == LED_STATE_COLOR0_DIM);
}


// requestSceneChange — taskProcessorUi dispatch — and resetModuleState, which runs directly
// on the GUI thread rather than being queued.

TEST_CASE("requestSceneChange - enqueues a switchScene lambda", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->sceneStore.current = 0;
	m->sceneStore.setConnection(1, 0, 1, true);  // scene 1 has a stored connection

	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);
	m->requestSceneChange(1);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 1);

	// Running the lambda switches the scene.
	m->taskProcessorUi.internalQueue.queue.shift()();
	REQUIRE(m->sceneStore.current == 1);
}

TEST_CASE("resetModuleState - clears all state", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	// Populate state that should be wiped by reset.
	m->sceneStore.current = 4;
	MidiOutPreset preset;
	preset.name = "Test";
	m->feedback.setActivePreset(preset);
	m->sceneStore.setConnection(2, 0, 1, true);
	m->portAssignments[3].moduleId = 1;
	m->portAssignments[3].portId = 0;
	m->portAssignments[3].type = engine::Port::OUTPUT;
	m->portHasCable[3] = true;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);
	// A scene-button map (index MATRIX_COUNT..TOTAL_MAPS-1) must be cleared too.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, MATRIX_COUNT + 1, 70);
	// A scene link is patch wiring, not a user setting — it must not survive Initialize.
	m->sceneLinkMasterId = 999;

	// Runs synchronously — nothing is queued.
	m->resetModuleState();
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);

	REQUIRE(m->sceneStore.current == 0);
	REQUIRE(m->feedback.getActivePreset() == nullptr);
	REQUIRE(m->portAssignments[3].isValid() == false);
	REQUIRE(m->portHasCable[3] == false);
	REQUIRE(m->sceneLinkMasterId == -1);
	// All scenes cleared
	for (int s = 0; s < SCENE_COUNT; s++) {
		for (int c = 0; c < MATRIX_COUNT; c++) {
			REQUIRE(m->sceneStore.connections[s][c] == 0);
		}
	}
	// MIDI map for cell 5 was cleared
	auto map = m->trackingProcessor.getMap(5);
	REQUIRE(map.type == MidiTrackingType::NONE);
	// MIDI map for scene button 1 was cleared
	auto sceneMap = m->trackingProcessor.getMap(MATRIX_COUNT + 1);
	REQUIRE(sceneMap.type == MidiTrackingType::NONE);
}

TEST_CASE("resetModuleState - removes the current scene's live cables from the patch", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	// capture()+applyToCurrent(empty) is what actually reconciles the patch; the bitmask
	// clears that follow are metadata-only and would leave a stale cable behind on their own.
	m->resetModuleState();

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
}
