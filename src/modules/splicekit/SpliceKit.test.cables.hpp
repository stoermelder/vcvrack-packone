// SpliceKit.cables.test.hpp — the patch-cable layer.
// connectLive/disconnectLive/toggleConnection/switchTo/applyToCurrent/capture/
// removeCellConnections and the cross-instance responder were previously tested only on
// their bitmask half; the vcv::addCable/removeCable half was a no-op without a widget tree.
// These tests install a MockCableRegistry (via CableScaffold, see SpliceKit.test.hpp) and
// assert the observable effect: a cable actually appears or disappears in the patch. Also
// covers the create/remove overlay messages and the cross-instance responder's cable.

#include "SpliceKit.test.hpp"


// connectLive / disconnectLive — the stored bitmask and the patch cable must move together.

TEST_CASE("connectLive - creates a real cable in the patch and the bitmask", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1));
}

TEST_CASE("disconnectLive - removes the cable from the patch and the bitmask", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	m->sceneStore.disconnectLive(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
}


// toggleConnection — the module-level wrapper; asserts the patch effect AND the overlay
// message the user reads (2.10).

TEST_CASE("toggleConnection - creates then removes a real cable with overlay feedback", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->toggleConnection(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1));
	REQUIRE(m->overlayMessage.title == "Cable created");

	m->toggleConnection(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	REQUIRE(m->overlayMessage.title == "Cable removed");
}


// capture — reads the patch's cables into a scene's bitmask (the "user pulled a cable by
// hand" path, previously untested).

TEST_CASE("capture - records the patch's current cables into the scene", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	// A cable present in the patch but in no stored scene, as a manual patch would be.
	cables.mock.addCable(42, 0, 43, 0, false);
	m->sceneStore.capture(2);

	REQUIRE(m->sceneStore.isConnected(2, 0, 1));
	// Scenes with no such cable stay empty.
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);
}


// switchTo — the doc's central promise: switching scenes reconciles the patch, removing
// cables of the outgoing scene and creating those stored in the incoming one (2.5).

TEST_CASE("switchTo - reconciles the patch: old cables removed, new cables created", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	m->portAssignments[1] = {43, engine::Port::INPUT, 0};
	m->portAssignments[2] = {44, engine::Port::OUTPUT, 0};
	m->portAssignments[3] = {45, engine::Port::INPUT, 0};

	// Scene 0 (current) has 0↔1 live in the patch; scene 3 stores 2↔3.
	m->sceneStore.current = 0;
	m->sceneStore.connectLive(0, 1);
	m->sceneStore.setConnection(3, 2, 3, true);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	m->sceneStore.switchTo(3);

	// The patch now matches scene 3.
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(cables.mock.hasCable(44, 0, 45, 0));
	// The outgoing scene kept its just-captured topology; the incoming one is untouched.
	REQUIRE(m->sceneStore.isConnected(0, 0, 1));
	REQUIRE(m->sceneStore.isConnected(3, 2, 3));
	REQUIRE(m->sceneStore.current == 3);
}


// applyToCurrent — reconciles the patch AND the current scene's bitmask to newConns.

TEST_CASE("applyToCurrent - reconciles the patch and current scene to the new topology", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	m->portAssignments[1] = {43, engine::Port::INPUT, 0};
	m->portAssignments[2] = {44, engine::Port::OUTPUT, 0};
	m->portAssignments[3] = {45, engine::Port::INPUT, 0};

	// Current scene + patch: 0↔1.
	m->sceneStore.connectLive(0, 1);

	// New topology for the current scene: only 2↔3.
	SceneConns newConns{};
	newConns[2] = (1ULL << 3);
	newConns[3] = (1ULL << 2);
	m->sceneStore.applyToCurrent(newConns);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(cables.mock.hasCable(44, 0, 45, 0));
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 2, 3));
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
}


// reconcile on the current scene — the public path (capture + applyToCurrent + persist)
// that rewrites the patch AND connections[current] to newConns (2.5). Used by the scene-save
// / scene-copy menu actions and randomizeCurrentScene().

TEST_CASE("reconcile - on the current scene captures the patch and applies the new topology", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	m->portAssignments[1] = {43, engine::Port::INPUT, 0};
	m->portAssignments[2] = {44, engine::Port::OUTPUT, 0};
	m->portAssignments[3] = {45, engine::Port::INPUT, 0};

	// A manual cable in the patch (0↔1) that is NOT in the current scene's stored topology —
	// capture() must pick it up before the diff runs, so it isn't silently dropped.
	m->sceneStore.current = 0;
	cables.mock.addCable(42, 0, 43, 0, false);
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);

	// Reconcile the current scene to a topology with only 2↔3.
	SceneConns newConns{};
	newConns[2] = (1ULL << 3);
	newConns[3] = (1ULL << 2);
	m->sceneStore.reconcile(0, newConns);

	// capture() read the manual 0↔1 cable; applyToCurrent removed it and added 2↔3, so the
	// patch now matches newConns and the scene stores exactly newConns.
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(cables.mock.hasCable(44, 0, 45, 0));
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);
	REQUIRE(m->sceneStore.isConnected(0, 2, 3));
	REQUIRE(m->sceneStore.current == 0);
}


// removeCellConnections — tears down every live cable of a cell in the current scene.

TEST_CASE("removeCellConnections - tears down the cell's cables in the patch", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	m->portAssignments[1] = {43, engine::Port::INPUT, 0};
	m->portAssignments[3] = {45, engine::Port::INPUT, 0};

	// Cell 0 cabled to both 1 and 3.
	m->sceneStore.connectLive(0, 1);
	m->sceneStore.connectLive(0, 3);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	REQUIRE(cables.mock.hasCable(42, 0, 45, 0));

	m->sceneStore.removeCellConnections(0);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(cables.mock.hasCable(42, 0, 45, 0) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 3) == false);
}


// Cross-instance responder — the cable-making half of triggerCell's responder path (2.6),
// previously noted as untested because it needs a cable layer.

TEST_CASE("cross-instance responder - creates and removes a real cable", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();   // a arms cell 0 as initiator

	b->triggerCell(5);
	b->taskProcessorUi.step();   // b completes the gesture → cable created

	REQUIRE(cables.mock.hasCable(42, 0, 77, 1));
	REQUIRE(b->overlayMessage.title == "Cable created");

	// Repeating the gesture toggles the cable off.
	a->triggerCell(0);
	a->taskProcessorUi.step();
	b->triggerCell(5);
	b->taskProcessorUi.step();

	REQUIRE(cables.mock.hasCable(42, 0, 77, 1) == false);
	REQUIRE(b->overlayMessage.title == "Cable removed");

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("cross-instance responder - an opted-out responder creates no cable", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();   // a arms cell 0 as initiator

	// b opts out of cross-instance patching. Its own setting decides whether it responds
	// to a's gesture (the responder check), so no cross-instance cable is made.
	b->crossInstanceEnabled = false;
	b->triggerCell(5);
	b->taskProcessorUi.step();

	REQUIRE(cables.mock.hasCable(42, 0, 77, 1) == false);
	// b still armed its own cell (the initiator path, which skips publishing).
	REQUIRE(b->pendingCellId == 5);

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("cross-instance responder - an opted-out initiator never publishes, so no cable is made", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	SpliceKitModule::crossPending()[APP].clear();
	// The doc's other half of the opt-out rule: "disabling it on either instance stops that
	// instance from initiating". a arms locally but publishes nothing to crossPending, so b's
	// press is a plain local arm rather than a completing gesture.
	a->crossInstanceEnabled = false;
	a->triggerCell(0);
	a->taskProcessorUi.step();

	b->triggerCell(5);
	b->taskProcessorUi.step();

	REQUIRE(cables.mock.hasCable(42, 0, 77, 1) == false);
	// Both instances are left merely armed on their own cell.
	REQUIRE(a->pendingCellId == 0);
	REQUIRE(b->pendingCellId == 5);

	SpliceKitModule::crossPending()[APP].clear();
}


// Scene switching must not disturb a cable both scenes agree on. topologyDiff() skips pairs
// whose bit is unchanged, so the cable is never torn down and rebuilt — a rebuild would be an
// audible break in the signal path for a connection the user never asked to change.

TEST_CASE("switchTo - a cable stored in both scenes is left untouched", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.current = 0;
	m->sceneStore.connectLive(0, 1);
	m->sceneStore.setConnection(1, 0, 1, true);   // scene 1 stores the same pair

	m->sceneStore.switchTo(1);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	REQUIRE(m->sceneStore.isConnected(1, 0, 1));
	REQUIRE(m->sceneStore.current == 1);
}


// capture() re-reads the patch rather than trusting the stored bitmask, so a cable the user
// pulled out by hand disappears from the scene on the next capture (the "reads the current
// cable state of assigned ports" promise, in its removal direction).

TEST_CASE("capture - a cable removed by hand is dropped from the scene", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);
	m->sceneStore.capture(0);
	REQUIRE(m->sceneStore.isConnected(0, 0, 1));

	// The user yanks the cable in the patch, bypassing SpliceKit entirely.
	cables.mock.removeCable(42, 0, 43, 0, false);
	m->sceneStore.capture(0);

	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);
}


// moveCell (shift+drag) — the patch-level half of the gesture. The moved cell's own cables
// survive (the port moves with them), while the destination's previous cables are torn out,
// because its old port assignment is discarded by the move.

TEST_CASE("moveCell - source cables survive and the destination's own cables are removed", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	m->assignPort(5, 99, 3, engine::Port::INPUT);   // destination has its own port and cable

	m->sceneStore.connectLive(0, 1);
	m->sceneStore.connectLive(0, 5);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	REQUIRE(cables.mock.hasCable(42, 0, 99, 3));

	m->moveCell(1, 5);

	// Cell 1's port (43:0) moved to cell 5 — the cable on it is still the right cable.
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	// Cell 5's discarded port (99:3) took its cable with it.
	REQUIRE(cables.mock.hasCable(42, 0, 99, 3) == false);
	// The bitmask followed the port: 0↔5 now names the moved connection.
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 5));
	REQUIRE(m->portAssignments[5].moduleId == 43);
	REQUIRE(m->portAssignments[1].isValid() == false);
}


// Aliased cells — two cells assigned to the SAME patch port. Nothing prevents this: port
// learn, drag-to-assign and a hand-edited patch can all produce it (only randomize is
// documented as duplicate-free). One physical cable is then described by two independent
// bitmask bits, which the current implementation does not reconcile.
//
// FAILING — these assert the CORRECT behavior against a known defect, so they are red until
// toggleConnection() is fixed. See var/SpliceKit_test_review.md §2.19 for the analysis and
// the two candidate fixes. Do not "fix" these by relaxing the assertions.
//
// Root cause: toggleConnection() decides create-vs-remove from the bitmask bit of the two
// CELL ids, while it actually creates/removes a cable between two PORTS. When two cells name
// the same port those two views disagree.

TEST_CASE("aliased cells - pressing an alias cell removes the existing cable rather than reporting a create", "[SpliceKit]") {
	CableScaffold cables;
	// ModuleScaffold, not a bare createModule(): these assertions are expected to FAIL until
	// the defect is fixed, and a failing REQUIRE unwinds past any trailing destroyModule().
	// Without RAII the module would leak into the shared instance registry and break later
	// cross-instance tests.
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	m->assignPort(2, 43, 0, engine::Port::INPUT);   // same port as cell 1

	m->toggleConnection(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	// Cell 2 names the same port as cell 1, so the cable 0↔2 would create is already there.
	// The gesture must therefore REMOVE it, and say so — not report a create for a cable that
	// already exists and silently no-op at addCableBetween's duplicate guard.
	m->toggleConnection(0, 2);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(m->overlayMessage.title == "Cable removed");
	// With the cable gone, no cell may still claim a connection to it.
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 2) == false);
}

TEST_CASE("aliased cells - removing a cable leaves no stale bit on the alias cell", "[SpliceKit]") {
	CableScaffold cables;
	ModuleScaffold mods;   // see the note above — this test is expected to fail until fixed
	SpliceKitModule* m = mods.create();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	m->assignPort(2, 43, 0, engine::Port::INPUT);   // same port as cell 1

	// Drive both cells' bits set for the one cable, the state the current implementation
	// reaches after two presses (see the test above).
	m->sceneStore.connectLive(0, 1);
	m->sceneStore.connectLive(0, 2);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	// Removing through cell 1 takes the single real cable away. Cell 2's bit describes that
	// same cable, so it must go too: a bit with no cable behind it recreates the cable on the
	// next switchTo(), resurrecting a connection the user deleted.
	m->toggleConnection(0, 1);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 2) == false);

	// The concrete consequence of a surviving bit: a scene round-trip must not bring the
	// deleted cable back.
	m->sceneStore.switchTo(1);
	m->sceneStore.switchTo(0);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
}
