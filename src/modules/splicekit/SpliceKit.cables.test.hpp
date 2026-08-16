// SpliceKit.cables.test.hpp — the patch-cable layer.
// connectLive/disconnectLive/toggleConnection/switchTo/applyToCurrent/capture/
// removeCellConnections and the cross-instance responder were previously tested only on
// their bitmask half; the vcv::addCableToPort/removeCable/findCable half was a no-op
// without a widget tree. These tests install a MockCableRegistry (via CableScaffold, see
// SpliceKit.test.hpp) and assert the observable effect: a cable actually appears or
// disappears in the patch. Also covers the create/remove overlay messages and the
// cross-instance responder's cable creation.

#include "SpliceKit.test.hpp"


// connectLive / disconnectLive — the stored bitmask and the patch cable must move together.

TEST_CASE("connectLive - creates a real cable in the patch and the bitmask", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);

	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1));

	Test::destroyModule(m);
}

TEST_CASE("disconnectLive - removes the cable from the patch and the bitmask", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	m->sceneStore.connectLive(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	m->sceneStore.disconnectLive(0, 1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == false);

	Test::destroyModule(m);
}


// toggleConnection — the module-level wrapper; asserts the patch effect AND the overlay
// message the user reads (2.10).

TEST_CASE("toggleConnection - creates then removes a real cable with overlay feedback", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
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

	Test::destroyModule(m);
}


// capture — reads the patch's cables into a scene's bitmask (the "user pulled a cable by
// hand" path, previously untested).

TEST_CASE("capture - records the patch's current cables into the scene", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);

	// A cable present in the patch but in no stored scene, as a manual patch would be.
	cables.mock.addCable(42, 0, 43, 0, false);
	m->sceneStore.capture(2);

	REQUIRE(m->sceneStore.isConnected(2, 0, 1));
	// Scenes with no such cable stay empty.
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);

	Test::destroyModule(m);
}


// switchTo — the doc's central promise: switching scenes reconciles the patch, removing
// cables that belong to the outgoing scene and creating cables stored in the incoming one
// (2.5).

TEST_CASE("switchTo - reconciles the patch: old cables removed, new cables created", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
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

	Test::destroyModule(m);
}


// applyToCurrent — reconciles the patch AND the current scene's bitmask to newConns.

TEST_CASE("applyToCurrent - reconciles the patch and current scene to the new topology", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
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

	Test::destroyModule(m);
}


// reconcile on the current scene — the public path (capture + applyToCurrent + persist)
// that rewrites the patch AND connections[current] to newConns (2.5). This is what the
// scene-save / scene-copy menu actions and randomizeCurrentScene() use.

TEST_CASE("reconcile - on the current scene captures the patch and applies the new topology", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
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

	Test::destroyModule(m);
}


// removeCellConnections — tears down every live cable of a cell in the current scene.

TEST_CASE("removeCellConnections - tears down the cell's cables in the patch", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
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

	Test::destroyModule(m);
}


// Cross-instance responder — the cable-making half of triggerCell's responder path (2.6),
// previously noted as untested because it needs a cable layer.

TEST_CASE("cross-instance responder - creates and removes a real cable", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* a = createModule();
	SpliceKitModule* b = createModule();
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
	Test::destroyModule(b);
	Test::destroyModule(a);
}

TEST_CASE("cross-instance responder - an opted-out responder creates no cable", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* a = createModule();
	SpliceKitModule* b = createModule();
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
	Test::destroyModule(b);
	Test::destroyModule(a);
}
