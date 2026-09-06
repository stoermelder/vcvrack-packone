// SpliceKit.mapping.test.cpp — cell mapping operations.
// Tests moveCell, removeCellConnections, applyPresetLayout, assignPort,
// clearPort, toggleConnection and onReset.

#include "SpliceKit.test.hpp"


// moveCell

TEST_CASE("moveCell - no-op when source equals target", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[4].moduleId = 42;
	m->portAssignments[4].portId = 0;
	m->portAssignments[4].type = engine::Port::OUTPUT;
	m->cellLabels[4] = "keep";
	m->cellColorSet[4] = 1;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 4, 36);

	m->moveCell(4, 4);

	REQUIRE(m->portAssignments[4].moduleId == 42);
	REQUIRE(m->cellLabels[4] == "keep");
	REQUIRE(m->cellColorSet[4] == 1);
	auto map = m->trackingProcessor.getMap(4);
	REQUIRE(map.type == MidiTrackingType::NOTE);
	REQUIRE(map.param == 36);
}


TEST_CASE("moveCell - port assignment is transferred and source is cleared", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[3].moduleId = 42;
	m->portAssignments[3].portId = 1;
	m->portAssignments[3].type = engine::Port::OUTPUT;
	// toId has an existing (discarded) assignment
	m->portAssignments[7].moduleId = 99;
	m->portAssignments[7].portId = 0;
	m->portAssignments[7].type = engine::Port::INPUT;

	m->moveCell(3, 7);

	REQUIRE(m->portAssignments[7].moduleId == 42);
	REQUIRE(m->portAssignments[7].portId == 1);
	REQUIRE(m->portAssignments[7].type == engine::Port::OUTPUT);
	REQUIRE(m->portAssignments[3].isValid() == false);
}


TEST_CASE("moveCell - MIDI mappings stay on their original cells", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId = 0;
	m->portAssignments[5].type = engine::Port::OUTPUT;
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);
	m->trackingProcessor.setMap(MidiTrackingType::CC, 9, 74);

	m->moveCell(5, 9);

	// fromId keeps its mapping (physical button position unchanged).
	auto src = m->trackingProcessor.getMap(5);
	REQUIRE(src.type == MidiTrackingType::NOTE);
	REQUIRE(src.param == 60);

	// toId keeps its own mapping (not overwritten by fromId's).
	auto dst = m->trackingProcessor.getMap(9);
	REQUIRE(dst.type == MidiTrackingType::CC);
	REQUIRE(dst.param == 74);
}


TEST_CASE("moveCell - label and color are transferred and source is reset", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[2].moduleId = 1;
	m->portAssignments[2].portId = 0;
	m->portAssignments[2].type = engine::Port::OUTPUT;
	m->cellLabels[2] = "VCO Out";
	m->cellColorSet[2] = 2;  // orange

	m->moveCell(2, 9);

	REQUIRE(m->cellLabels[9] == "VCO Out");
	REQUIRE(m->cellColorSet[9] == 2);
	REQUIRE(m->cellLabels[2].empty());
	REQUIRE(m->cellColorSet[2] == -1);
}


TEST_CASE("moveCell - scene connections are redirected in current scene", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->sceneStore.setConnection(0, 0, 2, true);
	m->sceneStore.setConnection(0, 0, 4, true);

	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;

	m->moveCell(0, 5);

	// toId inherits fromId's connections.
	REQUIRE(m->sceneStore.isConnected(0, 5, 2) == true);
	REQUIRE(m->sceneStore.isConnected(0, 5, 4) == true);
	// fromId is cleared.
	REQUIRE(m->sceneStore.isConnected(0, 0, 2) == false);
	REQUIRE(m->sceneStore.isConnected(0, 0, 4) == false);
	// Neighbours now point at toId, not fromId.
	REQUIRE(m->sceneStore.isConnected(0, 2, 5) == true);
	REQUIRE(m->sceneStore.isConnected(0, 2, 0) == false);
	REQUIRE(m->sceneStore.isConnected(0, 4, 5) == true);
	REQUIRE(m->sceneStore.isConnected(0, 4, 0) == false);

	// fromId's bitmask was never zeroed — its connections survived as toId's.
	// In production this means the physical cables remain in the patch untouched.
	REQUIRE(m->sceneStore.isConnected(0, 5, 2) == true);
	REQUIRE(m->sceneStore.isConnected(0, 5, 4) == true);
}


TEST_CASE("moveCell - toId's existing connections are discarded", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->sceneStore.setConnection(0, 3, 7, true);  // fromId=3 → cell 7
	m->sceneStore.setConnection(0, 5, 8, true);  // toId=5 existing connections — discarded
	m->sceneStore.setConnection(0, 5, 9, true);

	m->portAssignments[3].moduleId = 1;
	m->portAssignments[3].portId = 0;
	m->portAssignments[3].type = engine::Port::OUTPUT;

	m->moveCell(3, 5);

	// toId has fromId's connection only.
	REQUIRE(m->sceneStore.isConnected(0, 5, 7) == true);
	REQUIRE(m->sceneStore.isConnected(0, 5, 8) == false);
	REQUIRE(m->sceneStore.isConnected(0, 5, 9) == false);
	// Discarded neighbours no longer reference toId.
	REQUIRE(m->sceneStore.isConnected(0, 8, 5) == false);
	REQUIRE(m->sceneStore.isConnected(0, 9, 5) == false);
}


TEST_CASE("moveCell - fromId-toId connection is dropped and not a self-connection", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->sceneStore.setConnection(0, 0, 3, true);  // fromId↔toId: will not become self-connection
	m->sceneStore.setConnection(0, 0, 7, true);  // other connection — should transfer

	m->portAssignments[0].moduleId = 1;
	m->portAssignments[0].portId = 0;
	m->portAssignments[0].type = engine::Port::OUTPUT;

	m->moveCell(0, 3);

	REQUIRE(m->sceneStore.isConnected(0, 3, 3) == false);  // no self-connection
	REQUIRE(m->sceneStore.isConnected(0, 3, 7) == true);   // other connection transferred
	REQUIRE(m->sceneStore.isConnected(0, 0, 7) == false);  // fromId cleared
	REQUIRE(m->sceneStore.isConnected(0, 0, 3) == false);
}


TEST_CASE("moveCell - connections transferred across all scenes independently", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	// Different connection topology in scenes 1, 2, 3.
	m->sceneStore.setConnection(1, 10, 20, true);
	m->sceneStore.setConnection(2, 10, 30, true);
	m->sceneStore.setConnection(3, 10, 40, true);

	m->portAssignments[10].moduleId = 1;
	m->portAssignments[10].portId = 0;
	m->portAssignments[10].type = engine::Port::OUTPUT;

	m->moveCell(10, 50);

	REQUIRE(m->sceneStore.isConnected(1, 50, 20) == true);
	REQUIRE(m->sceneStore.isConnected(2, 50, 30) == true);
	REQUIRE(m->sceneStore.isConnected(3, 50, 40) == true);
	REQUIRE(m->sceneStore.isConnected(1, 10, 20) == false);
	REQUIRE(m->sceneStore.isConnected(2, 10, 30) == false);
	REQUIRE(m->sceneStore.isConnected(3, 10, 40) == false);
}


TEST_CASE("moveCell - overlay message is posted", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[1].moduleId = 1;
	m->portAssignments[1].portId = 0;
	m->portAssignments[1].type = engine::Port::OUTPUT;
	m->overlayEnabled = true;
	m->overlayMessageId = -1;

	m->moveCell(1, 6);

	REQUIRE(m->overlayMessageId == 0);
}

TEST_CASE("moveCell - fromId's cable survives the move and toId's own cable is removed from the patch", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(2, 43, 0, engine::Port::INPUT);
	m->assignPort(5, 99, 3, engine::Port::INPUT);  // toId's own port, with its own cable

	m->sceneStore.connectLive(0, 2);
	m->sceneStore.connectLive(0, 5);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	REQUIRE(cables.mock.hasCable(42, 0, 99, 3));

	m->moveCell(0, 5);

	// fromId's port (42:0) moved to toId — the same physical cable is still there.
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	// toId's discarded port (99:3) took its cable out of the patch with it.
	REQUIRE(cables.mock.hasCable(42, 0, 99, 3) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 5, 2));
}


// removeCellConnections

TEST_CASE("removeCellConnections - clears all bitmask bits for the cell in the current scene", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->sceneStore.current = 0;

	m->sceneStore.setConnection(0, 4, 1, true);
	m->sceneStore.setConnection(0, 4, 7, true);
	m->sceneStore.setConnection(0, 4, 9, true);
	REQUIRE(m->sceneStore.connections[0][4] != 0);

	// removeCellConnections also tears down each neighbour's cable (dereferencing the rack
	// module list); with no port assignments the cable half of disconnectLive() is a no-op, so safe.
	m->sceneStore.removeCellConnections(4);

	REQUIRE(m->sceneStore.connections[0][4] == 0);
	REQUIRE(m->sceneStore.isConnected(0, 4, 1) == false);
	REQUIRE(m->sceneStore.isConnected(0, 4, 7) == false);
	REQUIRE(m->sceneStore.isConnected(0, 4, 9) == false);
	// Other scenes unaffected
	REQUIRE(m->sceneStore.connections[3][4] == 0);
	REQUIRE(m->sceneStore.connections[0][1] == 0);
}

TEST_CASE("removeCellConnections - no-op when cell has no connections", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	// No connections set anywhere — removeCellConnections must be safe.
	REQUIRE_NOTHROW(m->sceneStore.removeCellConnections(20));
	REQUIRE(m->sceneStore.connections[0][20] == 0);
}


// applyPresetLayout — sets up the trackingProcessor from a custom preset

TEST_CASE("applyPresetLayout - applies cell and scene mappings from custom preset", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	MidiOutPreset preset;
	preset.cells[0].type = MidiTrackingType::NOTE;
	preset.cells[0].number = 36;
	preset.cells[1].type = MidiTrackingType::CC;
	preset.cells[1].number = 1;
	preset.scenes[2].type = MidiTrackingType::NOTE;
	preset.scenes[2].number = 50;
	m->feedback.setActivePreset(preset);

	// Pre-existing maps must be wiped before applyPresetLayout runs.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 30, 70);

	m->applyPresetLayout();

	// Cells from preset are mapped.
	auto c0 = m->trackingProcessor.getMap(0);
	REQUIRE(c0.type == MidiTrackingType::NOTE);
	REQUIRE(c0.param == 36);
	auto c1 = m->trackingProcessor.getMap(1);
	REQUIRE(c1.type == MidiTrackingType::CC);
	REQUIRE(c1.param == 1);
	// Scene 2 is MATRIX_COUNT + 2 = 66.
	auto s2 = m->trackingProcessor.getMap(66);
	REQUIRE(s2.type == MidiTrackingType::NOTE);
	REQUIRE(s2.param == 50);
	// Pre-existing map for cell 30 was cleared.
	auto c30 = m->trackingProcessor.getMap(30);
	REQUIRE(c30.type == MidiTrackingType::NONE);
	// Unmapped cells remain unmapped.
	auto c2 = m->trackingProcessor.getMap(2);
	REQUIRE(c2.type == MidiTrackingType::NONE);
}

TEST_CASE("applyPresetLayout - maps every valid MIDI note/CC number 0..127, including 0", "[SpliceKit]") {
	// Note 0 and CC 0 are legal MIDI values and must not be skipped; sweep the full range for
	// both cell and scene slots to guard against any other off-by-one at the boundaries.
	// One scaffold accumulates all 128 modules across iterations (rather than one per
	// iteration) and destroys them all at TEST_CASE exit, so a REQUIRE failing partway through
	// the sweep still leaks nothing.
	ModuleScaffold mods;
	for (int number = 0; number <= 127; number++) {
		SpliceKitModule* m = mods.create();

		MidiOutPreset preset;
		preset.cells[0].type = MidiTrackingType::NOTE;
		preset.cells[0].number = number;
		preset.cells[1].type = MidiTrackingType::CC;
		preset.cells[1].number = number;
		preset.scenes[0].type = MidiTrackingType::NOTE;
		preset.scenes[0].number = number;
		m->feedback.setActivePreset(preset);

		m->applyPresetLayout();

		auto c0 = m->trackingProcessor.getMap(0);
		REQUIRE(c0.type == MidiTrackingType::NOTE);
		REQUIRE(c0.param == number);
		auto c1 = m->trackingProcessor.getMap(1);
		REQUIRE(c1.type == MidiTrackingType::CC);
		REQUIRE(c1.param == number);
		// Scene 0 is MATRIX_COUNT + 0.
		auto s0 = m->trackingProcessor.getMap(MATRIX_COUNT);
		REQUIRE(s0.type == MidiTrackingType::NOTE);
		REQUIRE(s0.param == number);
	}
}

TEST_CASE("applyPresetLayout - no-op when no preset is active", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->feedback.setActivePresetJson("");  // no preset
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 5, 60);

	m->applyPresetLayout();  // must not throw, must not clear

	auto m5 = m->trackingProcessor.getMap(5);
	REQUIRE(m5.type == MidiTrackingType::NOTE);
	REQUIRE(m5.param == 60);
}

TEST_CASE("applyPresetLayout - invalidates LED states so they are re-sent", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	// Pre-populate LED state with non-(-1) values.
	for (int i = 0; i < MATRIX_COUNT; i++) m->feedback.cellLedState[i] = LED_STATE_COLOR0;
	for (int i = 0; i < SCENE_COUNT; i++) m->feedback.sceneLedState[i] = LED_STATE_SCENE_ACTIVE;

	MidiOutPreset preset;
	preset.cells[0].type = MidiTrackingType::NOTE;
	preset.cells[0].number = 36;
	m->feedback.setActivePreset(preset);

	m->applyPresetLayout();
	for (int i = 0; i < MATRIX_COUNT; i++) REQUIRE(m->feedback.cellLedState[i] == -1);
	for (int i = 0; i < SCENE_COUNT; i++) REQUIRE(m->feedback.sceneLedState[i] == -1);
}


// assignPort — rebinding a cell must discard everything derived from the old port

TEST_CASE("assignPort - assigns port to an empty cell", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->assignPort(4, 42, 3, engine::Port::OUTPUT);

	REQUIRE(m->portAssignments[4].isValid());
	REQUIRE(m->portAssignments[4].moduleId == 42);
	REQUIRE(m->portAssignments[4].portId == 3);
	REQUIRE(m->portAssignments[4].type == engine::Port::OUTPUT);
}

TEST_CASE("assignPort - rebinding clears connections in every scene, not just the current one", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	m->assignPort(2, 44, 0, engine::Port::INPUT);

	// Cell 0 is wired in the active scene (0) and in an inactive one (3).
	m->sceneStore.setConnection(0, 0, 1, true);
	m->sceneStore.setConnection(3, 0, 2, true);
	REQUIRE(m->sceneStore.isConnected(0, 0, 1));
	REQUIRE(m->sceneStore.isConnected(3, 0, 2));

	// Rebind cell 0 to a different port — its old connections describe the discarded port.
	m->assignPort(0, 99, 7, engine::Port::OUTPUT);

	REQUIRE(m->portAssignments[0].moduleId == 99);
	REQUIRE(m->portAssignments[0].portId == 7);

	// A stale bit in an inactive scene would recreate a cable to the wrong port on switchScene().
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);
	REQUIRE(m->sceneStore.isConnected(3, 0, 2) == false);
	// Symmetric halves must be cleared too, or the neighbour still claims the connection.
	REQUIRE(m->sceneStore.connections[0][1] == 0);
	REQUIRE(m->sceneStore.connections[3][2] == 0);
	REQUIRE(m->sceneStore.connections[0][0] == 0);
	REQUIRE(m->sceneStore.connections[3][0] == 0);
}

TEST_CASE("assignPort - rebinding removes the current scene's live cable from the patch", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	// Rebind cell 0 to a different port — the old port's cable must be torn down, not just
	// the bitmask bit, or the patch keeps a cable to a port cell 0 no longer describes.
	m->assignPort(0, 99, 7, engine::Port::OUTPUT);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
}

TEST_CASE("assignPort - rebinding drops the label describing the old port", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(5, 42, 0, engine::Port::OUTPUT);
	m->cellLabels[5] = "Filter cutoff";

	m->assignPort(5, 77, 1, engine::Port::INPUT);

	REQUIRE(m->cellLabels[5].empty());
}

TEST_CASE("assignPort - assigning to an empty cell preserves a pre-set label", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	// No previous port, so nothing stale to discard — a label typed on an unassigned cell must survive.
	m->cellLabels[6] = "Reverb send";

	m->assignPort(6, 42, 0, engine::Port::OUTPUT);

	REQUIRE(m->cellLabels[6] == "Reverb send");
}

TEST_CASE("assignPort - invalidates LED states so a changed color set is re-sent", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	std::fill(m->feedback.cellLedState, m->feedback.cellLedState + MATRIX_COUNT, LED_STATE_OFF);

	// OUTPUT → INPUT flips the auto color set (0/red → 1/blue); invalidate the cached LED state
	// or the controller keeps showing the previous set's color.
	m->assignPort(0, 42, 0, engine::Port::INPUT);

	REQUIRE(m->getCellColorSet(0) == 1);
	REQUIRE(m->feedback.cellLedState[0] == -1);
}

TEST_CASE("assignPort - out-of-range cell ids are ignored", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	REQUIRE_NOTHROW(m->assignPort(-1, 42, 0, engine::Port::OUTPUT));
	REQUIRE_NOTHROW(m->assignPort(MATRIX_COUNT, 42, 0, engine::Port::OUTPUT));
}

TEST_CASE("assignPort - explicit color set override survives a rebind", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(2, 42, 0, engine::Port::OUTPUT);
	m->cellColorSet[2] = 3;  // explicit green, chosen by the user for this button position

	m->assignPort(2, 88, 4, engine::Port::INPUT);

	// The color set is a property of the physical button, not the port — like the MIDI mapping,
	// it stays put across a rebind.
	REQUIRE(m->cellColorSet[2] == 3);
	REQUIRE(m->getCellColorSet(2) == 3);
}


// clearPort

TEST_CASE("clearPort - discards the port assignment", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(4, 42, 3, engine::Port::OUTPUT);

	m->clearPort(4);

	REQUIRE(m->portAssignments[4].isValid() == false);
}

TEST_CASE("clearPort - clears connections in every scene, not just the current one", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	m->assignPort(2, 44, 0, engine::Port::INPUT);

	// Cell 0 is wired in the active scene (0) and in an inactive one (3).
	m->sceneStore.setConnection(0, 0, 1, true);
	m->sceneStore.setConnection(3, 0, 2, true);
	REQUIRE(m->sceneStore.isConnected(0, 0, 1));
	REQUIRE(m->sceneStore.isConnected(3, 0, 2));

	m->clearPort(0);

	// A stale bit in an inactive scene would recreate a cable to the wrong port if cell 0 were
	// later reassigned and that scene activated (see the regression test below).
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);
	REQUIRE(m->sceneStore.isConnected(3, 0, 2) == false);
	// Symmetric halves must be cleared too, or the neighbour still claims the connection.
	REQUIRE(m->sceneStore.connections[0][1] == 0);
	REQUIRE(m->sceneStore.connections[3][2] == 0);
	REQUIRE(m->sceneStore.connections[0][0] == 0);
	REQUIRE(m->sceneStore.connections[3][0] == 0);
}

TEST_CASE("clearPort - removes the current scene's live cable from the patch", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	m->clearPort(0);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
}

TEST_CASE("clearPort - drops the label describing the old port", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(5, 42, 0, engine::Port::OUTPUT);
	m->cellLabels[5] = "Filter cutoff";

	m->clearPort(5);

	REQUIRE(m->cellLabels[5].empty());
}

TEST_CASE("clearPort - invalidates LED state so the cleared cell is re-sent as off", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	std::fill(m->feedback.cellLedState, m->feedback.cellLedState + MATRIX_COUNT, LED_STATE_OFF);

	m->clearPort(0);

	REQUIRE(m->feedback.cellLedState[0] == -1);
}

TEST_CASE("clearPort - no-op on an already-empty cell", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	REQUIRE_NOTHROW(m->clearPort(4));
	REQUIRE(m->portAssignments[4].isValid() == false);
}

TEST_CASE("clearPort - out-of-range cell ids are ignored", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	REQUIRE_NOTHROW(m->clearPort(-1));
	REQUIRE_NOTHROW(m->clearPort(MATRIX_COUNT));
}

TEST_CASE("clearPort - regression: bypassing this cleanup let a stale bit resurrect a "
          "connection to the wrong port on a later rebind", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(5, 42, 0, engine::Port::OUTPUT);
	m->assignPort(9, 43, 0, engine::Port::INPUT);

	// Scene 3 (inactive) holds a connection 5<->9.
	m->sceneStore.setConnection(3, 5, 9, true);

	// Clear cell 5's port via the same path the cell menu's "Clear" item now uses.
	m->clearPort(5);

	// Reassign cell 5 to a different port entirely.
	m->assignPort(5, 77, 1, engine::Port::OUTPUT);

	// Without clearPort's cleanup, the stale bit in scene 3 would still connect 5<->9,
	// recreating a cable to a port the user never chose once scene 3 becomes active.
	REQUIRE(m->sceneStore.isConnected(3, 5, 9) == false);
}


// onDragDrop needs real widget/event plumbing, so these cover the module-level invariant the
// widget fix relies on: moveCell() rewrites both cells, so a pending selection on either is stale.

TEST_CASE("moveCell - leaves a stale pending selection on the source cell", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->triggerCell(0);
	REQUIRE(m->pendingCellId == 0);

	m->moveCell(0, 5);

	// moveCell deliberately does not touch pendingCellId — the drag-drop caller clears it
	// (SpliceKitCellButton::onDragDrop, shiftDrag branch).
	REQUIRE(m->portAssignments[0].isValid() == false);
	REQUIRE(m->pendingCellId == 0);
}

TEST_CASE("moveCell - a pending selection on the moved-away cell cannot be cancelled by pressing it", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);

	m->triggerCell(0);
	REQUIRE(m->pendingCellId == 0);

	m->moveCell(0, 5);

	// This is what makes issue 17 worse than a cosmetic stale blink: triggerCell() returns on the
	// !isValid() guard before reaching the "pressing the pending cell cancels it" branch.
	m->triggerCell(0);
	REQUIRE(m->pendingCellId == 0);

	// clearPendingLocal() — what the fixed drag-drop path calls — does clear it.
	m->clearPendingLocal();
	REQUIRE(m->pendingCellId == -1);
}

TEST_CASE("moveCell - a pending selection on the destination cell silently changes meaning", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(5, 77, 3, engine::Port::INPUT);

	// Cell 5 is pending, selected while it still referred to port 77:3.
	m->triggerCell(5);
	REQUIRE(m->pendingCellId == 5);

	m->moveCell(0, 5);

	// Still pending but now pointing at a different port than the user selected — a second press
	// would connect the wrong one. Hence the unconditional clear in the drag-drop path.
	REQUIRE(m->pendingCellId == 5);
	REQUIRE(m->portAssignments[5].moduleId == 42);
	REQUIRE(m->portAssignments[5].portId == 0);
}


// clearPort/assignPort + pending selection
// Unlike moveCell (widget caller clears, since it rewrites two cells at once), these clear their
// own cell's pending selection — they're reached from several call sites (cell menu, drag-drop,
// both port-learn paths) that would otherwise each have to remember to do it.

TEST_CASE("clearPort - drops a pending selection on the cleared cell", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(3, 42, 0, engine::Port::OUTPUT);

	m->triggerCell(3);
	REQUIRE(m->pendingCellId == 3);

	m->clearPort(3);

	// Regression: the cell used to stay pending here. It kept blinking (resolveCellVisual tests
	// pendingCellId before `assigned`) and could not be cancelled by pressing it, because
	// triggerCell() returns on the !isValid() guard first.
	REQUIRE(m->portAssignments[3].isValid() == false);
	REQUIRE(m->pendingCellId == -1);
}

TEST_CASE("clearPort - leaves a pending selection on an unrelated cell alone", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(3, 42, 0, engine::Port::OUTPUT);
	m->assignPort(7, 43, 0, engine::Port::INPUT);

	m->triggerCell(7);
	REQUIRE(m->pendingCellId == 7);

	// Clearing a different cell must not cancel the user's in-progress selection.
	m->clearPort(3);
	REQUIRE(m->pendingCellId == 7);
}

TEST_CASE("assignPort - rebinding drops a pending selection on the rebound cell", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(4, 42, 0, engine::Port::OUTPUT);

	// Cell 4 is pending, selected while it still referred to port 42:0.
	m->triggerCell(4);
	REQUIRE(m->pendingCellId == 4);

	// Rebinding to a different port makes that selection stale — a second press would otherwise
	// connect a port the user never selected.
	m->assignPort(4, 77, 1, engine::Port::OUTPUT);

	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->portAssignments[4].moduleId == 77);
}

TEST_CASE("assignPort - assigning an empty cell drops a pending selection on it", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(2, 42, 0, engine::Port::OUTPUT);

	m->triggerCell(2);
	REQUIRE(m->pendingCellId == 2);
	m->clearPort(2);

	// The cell is empty now but assignPort must still clear pending, since its early-out for empty
	// cells skips the rest of the cleanup contract.
	m->pendingCellId = 2;
	m->assignPort(2, 99, 4, engine::Port::INPUT);
	REQUIRE(m->pendingCellId == -1);
}

TEST_CASE("assignPort - leaves a pending selection on an unrelated cell alone", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(3, 42, 0, engine::Port::OUTPUT);
	m->assignPort(7, 43, 0, engine::Port::INPUT);

	m->triggerCell(7);
	REQUIRE(m->pendingCellId == 7);

	m->assignPort(3, 77, 2, engine::Port::OUTPUT);
	REQUIRE(m->pendingCellId == 7);
}


// toggleConnection — direction validation
// toggleConnection() decides create-vs-remove from the patch (vcv::hasCable()), not the cell-pair
// bitmask bit (see the aliased-cells tests in SpliceKit.cables.test.hpp), so exercising both
// directions needs a CableScaffold to report cable state accurately. The reject-and-report cases
// below don't toggle, so they're unaffected.

TEST_CASE("toggleConnection - output to input creates the connection", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->toggleConnection(0, 1);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1));
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	// Toggling again removes it.
	m->toggleConnection(0, 1);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
}

TEST_CASE("toggleConnection - argument order does not matter", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	// input-first must resolve the same pair as output-first.
	m->toggleConnection(1, 0);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1));
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
}

TEST_CASE("toggleConnection - two outputs are rejected and reported", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::OUTPUT);

	m->toggleConnection(0, 1);

	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	REQUIRE(m->overlayMessage.title == "Both ports are outputs");
}

TEST_CASE("toggleConnection - two inputs are rejected and reported", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::INPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->toggleConnection(0, 1);

	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	REQUIRE(m->overlayMessage.title == "Both ports are inputs");
}

TEST_CASE("toggleConnection - unassigned cell is ignored without an overlay message", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	// Cell 1 is left unassigned.
	m->setOverlayMessage("sentinel", "");

	m->toggleConnection(0, 1);

	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	// The !isValid() branch returns before any setOverlayMessage call.
	REQUIRE(m->overlayMessage.title == "sentinel");
}

TEST_CASE("toggleConnection - only the current scene is affected", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	m->sceneStore.current = 3;

	m->toggleConnection(0, 1);

	REQUIRE(m->sceneStore.isConnected(3, 0, 1));
	for (int s = 0; s < SCENE_COUNT; s++) {
		if (s != 3) REQUIRE(m->sceneStore.isConnected(s, 0, 1) == false);
	}
}


// onReset — what it adds on top of resetModuleState()

TEST_CASE("onReset - clears labels, color overrides and cancels learn", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->cellLabels[2] = "Filter cutoff";
	m->cellColorSet[2] = 3;
	m->pendingCellId = 4;
	m->enableLearn(5);
	REQUIRE(m->learningId == 5);

	m->onReset();

	REQUIRE(m->cellLabels[2].empty());
	REQUIRE(m->cellColorSet[2] == -1);
	REQUIRE(m->learningId == -1);
	REQUIRE(m->midiLearnMode == false);
	REQUIRE(m->portLearningId == -1);
	REQUIRE(m->portLearnMode == false);
	REQUIRE(m->pendingCellId == -1);
}

TEST_CASE("onReset - clears state directly without queueing", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->sceneStore.current = 5;
	m->sceneStore.setConnection(5, 0, 1, true);

	m->onReset();
	// Rack calls onReset() on the GUI thread, so it works inline — enqueueing here would make
	// taskProcessorUi multi-producer against the engine thread.
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);

	REQUIRE(m->sceneStore.current == 0);
	REQUIRE(m->sceneStore.connections[5][0] == 0);
}