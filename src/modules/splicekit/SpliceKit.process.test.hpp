// SpliceKit.process.test.cpp — process() behavior and visual resolution.
// Tests per-cell/per-scene LED state transitions in process(), the
// resolveCellVisual/resolveSceneVisual precedence rules, requestSceneChange
// and resetModuleState.

#include "SpliceKit.test.hpp"


// sendFeedbackOff integration — state transitions in process()

TEST_CASE("process - unassigned cell transitions cellLedState to OFF", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);  // lightDivider defaults sample-rate-relative; pin for the loop below

	// Pre-set to a non-OFF state to force a transition
	m->feedback.cellLedState[0] = LED_STATE_COLOR0;

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_OFF);
	Test::destroyModule(m);
}

TEST_CASE("process - assigned cell without cable transitions to DIM state", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);

	m->portAssignments[0].moduleId = 42;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;
	m->portHasCable[0] = false;

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_COLOR0_DIM);
	Test::destroyModule(m);
}

TEST_CASE("process - cellLedState transitions from old state to OFF when cell unassigned", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);
	m->feedback.setActivePreset(makeNoteOnPreset());

	// Simulate a previous state that the LED was in
	m->feedback.cellLedState[5] = LED_STATE_COLOR1;

	// Cell 5 is unassigned; process() must send note-off for COLOR1 then note-on for OFF
	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[5] == LED_STATE_OFF);
	Test::destroyModule(m);
}

TEST_CASE("process - scene cellLedState transitions to SCENE_ACTIVE for currentScene", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);

	m->sceneStore.current = 2;
	m->feedback.sceneLedState[2] = -1;  // force a state send

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.sceneLedState[2] == LED_STATE_SCENE_ACTIVE);
	Test::destroyModule(m);
}

TEST_CASE("process - physical scene button press works normally when not linked", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 0;  // sceneLinkMasterId stays -1 (default)

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // establish trigger baseline at low

	m->params[SpliceKitModule::PARAM_SCENE + 1].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();  // rising edge on the next divided tick

	// requestSceneChange() enqueues switchScene(1) onto taskProcessorUi rather than
	// applying it inline. With no window present (as in this test binary), the divider
	// ticks above already started a real background worker for m, so poking
	// internalQueue's ring buffer directly here would race that worker's own drain.
	// step() goes through the same CAS-guarded drain() the worker uses, so it is safe to
	// call concurrently — whichever of the two gets there first runs the task, the other
	// is a no-op — and the effect is observable either way via sceneStore.current.
	m->taskProcessorUi.step();
	REQUIRE(m->sceneStore.current == 1);

	Test::destroyModule(m);
}

TEST_CASE("process - physical scene button press is ignored while following a scene link master", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneLinkMasterId = 999;  // any id — process() only checks it's >= 0
	m->sceneStore.current = 0;

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();  // establish trigger baseline at low

	m->params[SpliceKitModule::PARAM_SCENE + 1].setValue(1.f);
	for (int i = 0; i < 256; i++) engine.step();  // rising edge on the next divided tick

	REQUIRE(m->sceneStore.current == 0);   // unaffected — scene button press ignored while linked
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);

	Test::destroyModule(m);
}


TEST_CASE("process - scene state transitions from active to dim after scene switch", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);
	m->feedback.setActivePreset(makeNoteOnPreset());

	m->sceneStore.current = 0;
	// Give scene 1 a stored connection so it becomes DIM
	m->sceneStore.setConnection(1, 0, 1, true);

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.sceneLedState[0] == LED_STATE_SCENE_ACTIVE);
	REQUIRE(m->feedback.sceneLedState[1] == LED_STATE_SCENE_DIM);
	Test::destroyModule(m);
}


// process() — PENDING, PORT_LEARN, MIDI_LEARN LED state transitions

TEST_CASE("process - pending cell transitions cellLedState to PENDING", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);

	m->portAssignments[0].moduleId = 42;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;

	// First press → triggerCell sets pendingCellId
	m->triggerCell(0);
	REQUIRE(m->pendingCellId == 0);
	// triggerCell() enqueues the cross-instance half onto taskProcessorUi; drain it
	// synchronously (as every other triggerCell() test does) before the loop below gets
	// a chance to spin up a background worker for it — see the loop's own comment.
	m->taskProcessorUi.step();
	SpliceKitModule::crossPending[APP].clear();

	Test::SimpleEngine engine;
	engine.registerModule(m);
	// 256 steps trips processDivider, which calls taskProcessorUi.process(). With no
	// window present (as in this test binary) that starts a real background worker
	// thread for m — harmless on its own, but combined with a still-queued task it
	// would race destroyModule() below. The drain above prevents that by ensuring the
	// queue is already empty before the worker could ever start.
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_PENDING);
	Test::destroyModule(m);
}

TEST_CASE("process - port-learning cell transitions cellLedState to PORT_LEARN", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);

	m->portLearningId = 7;
	// m->portSelectProcessor is in learn mode iff isLearning() is true; without
	// starting learn() the LED branch for portLearningId is still taken because
	// the process() code only checks portLearningId, not isLearning(). This
	// matches the current production behaviour.

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[7] == LED_STATE_PORT_LEARN);
	Test::destroyModule(m);
}

TEST_CASE("process - midi-learning cell transitions cellLedState to MIDI_LEARN", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);

	m->learningId = 11;

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->feedback.cellLedState[11] == LED_STATE_MIDI_LEARN);
	Test::destroyModule(m);
}

TEST_CASE("process - cell connected to pending cell transitions to CONNECTED1", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
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
	// Drain the cross-instance half synchronously before the loop below — see the
	// identical drain in the PENDING test above for why an undrained queue plus this
	// loop races a background worker against destroyModule().
	m->taskProcessorUi.step();
	SpliceKitModule::crossPending[APP].clear();

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	// Cell 5 is connected to pending cell 0. Cell 5 is an INPUT with no explicit
	// color override, so it auto-resolves to color set 1 (blue) → CONNECTED1.
	REQUIRE(m->feedback.cellLedState[0] == LED_STATE_PENDING);
	REQUIRE(m->feedback.cellLedState[5] == LED_STATE_CONNECTED1);
	Test::destroyModule(m);
}

TEST_CASE("resolveCellVisual - precedence order pending > connected > port-learn > midi-learn > assigned > off", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

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

	Test::destroyModule(m);
}

TEST_CASE("resolveSceneVisual - precedence order midi-learn > active > has-connections > off", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

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

	Test::destroyModule(m);
}

TEST_CASE("process - cell with port assignment and no cable transitions to COLOR0_DIM", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->lightDivider.setDivision(256);
	m->feedback.setActivePreset(makeNoteOnPreset());

	m->portAssignments[2].moduleId = 1;
	m->portAssignments[2].portId = 0;
	m->portAssignments[2].type = engine::Port::OUTPUT;
	m->portHasCable[2] = false;  // explicit — no cable

	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	// OUTPUT with default color set 0, no cable → COLOR0_DIM
	REQUIRE(m->feedback.cellLedState[2] == LED_STATE_COLOR0_DIM);
	Test::destroyModule(m);
}


// requestSceneChange — taskProcessorUi dispatch — and resetModuleState, which runs
// directly on the GUI thread rather than being queued.

TEST_CASE("requestSceneChange - enqueues a switchScene lambda", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 0;
	m->sceneStore.setConnection(1, 0, 1, true);  // scene 1 has a stored connection

	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);
	m->requestSceneChange(1);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 1);

	// Running the lambda switches the scene.
	m->taskProcessorUi.internalQueue.queue.shift()();
	REQUIRE(m->sceneStore.current == 1);
	Test::destroyModule(m);
}

TEST_CASE("resetModuleState - clears all state", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

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

	// Runs synchronously — nothing is queued.
	m->resetModuleState();
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);

	REQUIRE(m->sceneStore.current == 0);
	REQUIRE(m->feedback.getActivePreset() == nullptr);
	REQUIRE(m->portAssignments[3].isValid() == false);
	REQUIRE(m->portHasCable[3] == false);
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

	Test::destroyModule(m);
}
