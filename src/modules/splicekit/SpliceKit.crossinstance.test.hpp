// SpliceKit.crossinstance.test.hpp — cross-instance patching.
// collectAssignedPorts()/collectCableEndCandidates(), which gather cable-end
// candidates across every participating SpliceKit instance so the cross-instance
// gesture lands on ports owned by other instances.

#include "SpliceKit.test.hpp"


// cross-instance cable-end candidates
// A cable created by the cross-instance gesture lands on a port owned by another SpliceKit
// instance. collectCableEndCandidates() must therefore span every participating instance,
// otherwise SpliceKitWidget::step() reports "no cable" and the cell renders as unconnected.
TEST_CASE("collectAssignedPorts - collects only valid assignments, keyed by direction", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[0] = {42, engine::Port::OUTPUT, 3};
	m->portAssignments[1] = {42, engine::Port::INPUT, 3};   // same port id, other direction
	// cell 2 deliberately left unassigned

	std::set<std::pair<int64_t, int>> ports;
	m->collectAssignedPorts(ports);

	REQUIRE(ports.size() == 2);
	REQUIRE(ports.count({42, 3 * 2 + (int)engine::Port::OUTPUT}) == 1);
	REQUIRE(ports.count({42, 3 * 2 + (int)engine::Port::INPUT}) == 1);
}

TEST_CASE("collectCableEndCandidates - includes another instance's assignments", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();

	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	// The far end of a cross-instance cable is owned by b, but must be a candidate for a.
	auto ports = a->collectCableEndCandidates();
	REQUIRE(ports.count({42, 0 * 2 + (int)engine::Port::OUTPUT}) == 1);
	REQUIRE(ports.count({77, 1 * 2 + (int)engine::Port::INPUT}) == 1);

	// Symmetric: b sees a's assignment too, so both cells light.
	auto portsB = b->collectCableEndCandidates();
	REQUIRE(portsB.count({42, 0 * 2 + (int)engine::Port::OUTPUT}) == 1);
}

TEST_CASE("collectCableEndCandidates - excludes instances that opted out of cross-instance patching", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();

	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	// b opts out: it can never be the far end of a cross-instance cable, so its cells
	// are not candidates for a.
	b->crossInstanceEnabled = false;
	auto ports = a->collectCableEndCandidates();
	REQUIRE(ports.count({42, 0 * 2 + (int)engine::Port::OUTPUT}) == 1);
	REQUIRE(ports.count({77, 1 * 2 + (int)engine::Port::INPUT}) == 0);

	// And a itself opting out reduces the set to its own assignments.
	b->crossInstanceEnabled = true;
	a->crossInstanceEnabled = false;
	auto portsOptedOut = a->collectCableEndCandidates();
	REQUIRE(portsOptedOut.size() == 1);
	REQUIRE(portsOptedOut.count({42, 0 * 2 + (int)engine::Port::OUTPUT}) == 1);
}

TEST_CASE("collectCableEndCandidates - a lone instance yields exactly its own assignments", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	m->portAssignments[1] = {43, engine::Port::INPUT, 2};

	auto ports = m->collectCableEndCandidates();
	REQUIRE(ports.size() == 2);
}

// cross-instance connected highlight
// When one instance arms a cell, the cells on OTHER instances that already share a
// cross-instance connected highlight
// When one instance arms a cell, cells on OTHER instances that already share a cross-instance
// cable with that armed port blink — the cross-instance counterpart of resolveCellVisual()'s
// local connectedToPending. Such a cable is never stored in a scene, so the initiator resolves
// its port's cables once when it arms and publishes them on CrossPendingState::partners; peers
// match their cells against that list in refreshPeerConnected() and leave the result in
// peerConnected[] for the engine-thread loop.
//
// Coverage boundary: collectCablePartners() needs real CableWidgets, which the harness does
// not provide, so the initiator's resolution step is NOT covered here — with no cables in the
// patch it returns an empty set either way. Everything downstream IS covered below by
// publishing `partners` directly the way the initiator would.

TEST_CASE("peer connected - a flagged cell blinks in the connected state", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->portAssignments[5] = {77, engine::Port::INPUT, 1};

	// Not flagged: ordinary assigned appearance.
	REQUIRE(m->resolveCellVisual(5, true, true).stateId == LED_STATE_COLOR_DIM_BY_SET[m->getCellColorSet(5)]);

	// Flagged: same state id the local connected-to-pending case uses, so every controller
	// preset lights it without needing a new LED_STATE_*.
	m->peerConnected[5] = true;
	REQUIRE(m->resolveCellVisual(5, true, true).stateId == LED_STATE_CONNECTED_BY_SET[m->getCellColorSet(5)]);
}

TEST_CASE("peer connected - a local selection still wins", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->portAssignments[5] = {77, engine::Port::INPUT, 1};
	m->peerConnected[5] = true;

	// The cell the user armed here renders as PENDING, not as a peer highlight.
	m->pendingCellId = 5;
	REQUIRE(m->resolveCellVisual(5, true, true).stateId == LED_STATE_PENDING);
}

TEST_CASE("peer connected - no armed peer clears every flag", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};
	b->peerConnected[5] = true;   // left over from an earlier gesture

	// crossPending is empty, so nothing should stay highlighted.
	b->refreshPeerConnected();
	REQUIRE(b->peerConnected[5] == false);

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("peer connected - the initiator does not highlight its own cells", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	a->peerConnected[0] = true;

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();   // a becomes the initiator in crossPending

	// a's own LEDs are driven by its pendingCellId; the peer highlight is for everyone else.
	a->refreshPeerConnected();
	REQUIRE(a->peerConnected[0] == false);

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("peer connected - an opted-out instance shows no highlight", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();

	b->crossInstanceEnabled = false;
	b->peerConnected[5] = true;
	b->refreshPeerConnected();
	// b cannot participate in the gesture, so it must not advertise a connection to it.
	REQUIRE(b->peerConnected[5] == false);

	SpliceKitModule::crossPending()[APP].clear();
}

// The published-partners matching itself. These set CrossPendingState::partners the way the
// initiator's collectCablePartners() call would, so the peer-side logic is covered without any
// CableWidget scaffolding.

TEST_CASE("peer connected - a cell in the published partner list is flagged", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};   // cabled to a's armed port
	b->portAssignments[6] = {88, engine::Port::INPUT, 2};   // an input, but not cabled to it

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();

	// What the initiator would have published for a cable 42:0 -> 77:1.
	SpliceKitModule::crossPending()[APP].partners = {{77, 1}};

	b->refreshPeerConnected();
	REQUIRE(b->peerConnected[5] == true);
	// Not cabled to the armed port, so it stays dark even though the direction is legal —
	// this is what distinguishes "already connected" from "could be connected".
	REQUIRE(b->peerConnected[6] == false);

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("peer connected - a same-direction cell is never flagged", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::OUTPUT, 1};  // same direction as the armed port

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();

	// Even if the id somehow appeared in the list, resolveDirection() rejects the pair: two
	// outputs can never share a cable.
	SpliceKitModule::crossPending()[APP].partners = {{77, 1}};

	b->refreshPeerConnected();
	REQUIRE(b->peerConnected[5] == false);

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("peer connected - clearing the pending entry drops the published partners", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();
	SpliceKitModule::crossPending()[APP].partners = {{77, 1}};
	b->refreshPeerConnected();
	REQUIRE(b->peerConnected[5] == true);

	// The published list is part of the entry, so cancelling the gesture must drop it — a
	// leftover list would keep peers highlighting a selection that no longer exists.
	a->clearPendingGui();
	REQUIRE(SpliceKitModule::crossPending()[APP].partners.empty());

	b->refreshPeerConnected();
	REQUIRE(b->peerConnected[5] == false);

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("peer connected - unassigned cells are never flagged", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	// b has no assignments at all.

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();

	b->refreshPeerConnected();
	for (int i = 0; i < MATRIX_COUNT; i++) {
		REQUIRE(b->peerConnected[i] == false);
	}

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("collectCableEndCandidates - a destroyed instance leaves no dangling entry", "[SpliceKit]") {
	// b is deliberately destroyed mid-test (inner scope) to exercise registry cleanup while a
	// is still alive — the exact scenario under test — so b is left on the bare
	// createModule()/destroyModule() pattern rather than folded into a's ModuleScaffold.
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};

	{
		SpliceKitModule* b = createModule();
		b->portAssignments[5] = {77, engine::Port::INPUT, 1};
		REQUIRE(a->collectCableEndCandidates().count({77, 1 * 2 + (int)engine::Port::INPUT}) == 1);
		Test::destroyModule(b);
	}

	// The destructor must have removed b from the registry — otherwise this dereferences
	// a dangling pointer.
	auto ports = a->collectCableEndCandidates();
	REQUIRE(ports.size() == 1);
	REQUIRE(ports.count({77, 1 * 2 + (int)engine::Port::INPUT}) == 0);
}

// Cross-instance responder direction clash
// The responder resolves the initiator's armed port against its own; a same-direction pair
// (two outputs, or two inputs) can never share a cable, so no cable is made and the user is
// told why — the cross-instance counterpart of toggleConnection()'s single-instance rejection.
// (The cable-making half of the responder needs real CableWidgets, which the harness does not
// provide — same boundary as the collectCablePartners() note above.)
TEST_CASE("cross-instance responder - same-direction pair reports the clash", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::OUTPUT, 1};  // same direction as a's armed port

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();   // a becomes the initiator in crossPending

	// b completes a's gesture, but the pair is same-direction: no cable, and the user sees
	// the same rejection toggleConnection() would have shown on a single instance.
	b->triggerCell(5);
	b->taskProcessorUi.step();

	REQUIRE(b->overlayMessage.title == "Both ports are outputs");

	SpliceKitModule::crossPending()[APP].clear();
}

TEST_CASE("cross-instance responder - two inputs report the clash", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* a = mods.create();
	SpliceKitModule* b = mods.create();
	a->portAssignments[0] = {42, engine::Port::INPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};  // same direction as a's armed port

	SpliceKitModule::crossPending()[APP].clear();
	a->triggerCell(0);
	a->taskProcessorUi.step();   // a becomes the initiator in crossPending

	b->triggerCell(5);
	b->taskProcessorUi.step();

	REQUIRE(b->overlayMessage.title == "Both ports are inputs");

	SpliceKitModule::crossPending()[APP].clear();
}
