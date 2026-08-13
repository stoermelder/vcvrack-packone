// SpliceKit.crossinstance.test.cpp — cross-instance patching.
// Tests collectAssignedPorts()/collectCableEndCandidates(), which gather
// cable-end candidates across every participating SpliceKit instance so the
// cross-instance gesture lands on ports owned by other instances.

#include "SpliceKit.test.hpp"


// cross-instance cable-end candidates
// A cable created by the cross-instance gesture lands on a port owned by another SpliceKit
// instance. collectCableEndCandidates() must therefore span every participating instance,
// otherwise SpliceKitWidget::step() reports "no cable" and the cell renders as unconnected.
TEST_CASE("collectAssignedPorts - collects only valid assignments, keyed by direction", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[0] = {42, engine::Port::OUTPUT, 3};
	m->portAssignments[1] = {42, engine::Port::INPUT, 3};   // same port id, other direction
	// cell 2 deliberately left unassigned

	std::set<std::pair<int64_t, int>> ports;
	m->collectAssignedPorts(ports);

	REQUIRE(ports.size() == 2);
	REQUIRE(ports.count({42, 3 * 2 + (int)engine::Port::OUTPUT}) == 1);
	REQUIRE(ports.count({42, 3 * 2 + (int)engine::Port::INPUT}) == 1);

	Test::destroyModule(m);
}

TEST_CASE("collectCableEndCandidates - includes another instance's assignments", "[SpliceKit]") {
	SpliceKitModule* a = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* b = Test::createModule<SpliceKitModule>("SpliceKit");

	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	b->portAssignments[5] = {77, engine::Port::INPUT, 1};

	// The far end of a cross-instance cable is owned by b, but must be a candidate for a.
	auto ports = a->collectCableEndCandidates();
	REQUIRE(ports.count({42, 0 * 2 + (int)engine::Port::OUTPUT}) == 1);
	REQUIRE(ports.count({77, 1 * 2 + (int)engine::Port::INPUT}) == 1);

	// Symmetric: b sees a's assignment too, so both cells light.
	auto portsB = b->collectCableEndCandidates();
	REQUIRE(portsB.count({42, 0 * 2 + (int)engine::Port::OUTPUT}) == 1);

	Test::destroyModule(b);
	Test::destroyModule(a);
}

TEST_CASE("collectCableEndCandidates - excludes instances that opted out of cross-instance patching", "[SpliceKit]") {
	SpliceKitModule* a = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* b = Test::createModule<SpliceKitModule>("SpliceKit");

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

	Test::destroyModule(b);
	Test::destroyModule(a);
}

TEST_CASE("collectCableEndCandidates - a lone instance yields exactly its own assignments", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[0] = {42, engine::Port::OUTPUT, 0};
	m->portAssignments[1] = {43, engine::Port::INPUT, 2};

	auto ports = m->collectCableEndCandidates();
	REQUIRE(ports.size() == 2);

	Test::destroyModule(m);
}

TEST_CASE("collectCableEndCandidates - a destroyed instance leaves no dangling entry", "[SpliceKit]") {
	SpliceKitModule* a = Test::createModule<SpliceKitModule>("SpliceKit");
	a->portAssignments[0] = {42, engine::Port::OUTPUT, 0};

	{
		SpliceKitModule* b = Test::createModule<SpliceKitModule>("SpliceKit");
		b->portAssignments[5] = {77, engine::Port::INPUT, 1};
		REQUIRE(a->collectCableEndCandidates().count({77, 1 * 2 + (int)engine::Port::INPUT}) == 1);
		Test::destroyModule(b);
	}

	// The destructor must have removed b from the registry — otherwise this dereferences
	// a dangling pointer.
	auto ports = a->collectCableEndCandidates();
	REQUIRE(ports.size() == 1);
	REQUIRE(ports.count({77, 1 * 2 + (int)engine::Port::INPUT}) == 0);

	Test::destroyModule(a);
}
