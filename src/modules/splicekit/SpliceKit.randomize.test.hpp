// SpliceKit.randomize.test.cpp — randomization.
// Tests randomizeCurrentScene/randomizePortAssignmentsFrom (random valid
// topologies) and onRandomize, including the randomization opt-out for the
// matrix and scene param quantities.

#include "SpliceKit.test.hpp"


// onRandomize / randomizeCurrentScene — random valid topology for the current scene

TEST_CASE("Construction - matrix and scene param quantities exclude default randomization", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	REQUIRE(m->paramQuantities[SpliceKitModule::PARAM_MATRIX]->randomizeEnabled == false);
	REQUIRE(m->paramQuantities[SpliceKitModule::PARAM_SCENE]->randomizeEnabled == false);
	Test::destroyModule(m);
}

TEST_CASE("randomizeCurrentScene - no connections when no ports are assigned", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->randomizeCurrentScene();
	for (int i = 0; i < MATRIX_COUNT; i++) REQUIRE(m->sceneStore.connections[m->sceneStore.current][i] == 0);
	Test::destroyModule(m);
}

TEST_CASE("randomizeCurrentScene - only pairs assigned outputs with assigned inputs", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	int outs[] = {0, 5};
	int ins[] = {1, 2, 10};
	for (int i : outs) {
		m->portAssignments[i].moduleId = 42;
		m->portAssignments[i].portId = i;
		m->portAssignments[i].type = engine::Port::OUTPUT;
	}
	for (int i : ins) {
		m->portAssignments[i].moduleId = 42;
		m->portAssignments[i].portId = i;
		m->portAssignments[i].type = engine::Port::INPUT;
	}

	m->randomizeCurrentScene();

	// Exactly min(#outs, #ins) = 2 pairs were formed, each connecting one output to one input.
	int connectionCount = 0;
	for (int i = 0; i < MATRIX_COUNT; i++) {
		for (int j = i + 1; j < MATRIX_COUNT; j++) {
			if (!m->sceneStore.isConnected(m->sceneStore.current, i, j)) continue;
			connectionCount++;
			bool iOut = m->portAssignments[i].isValid() && m->portAssignments[i].type == engine::Port::OUTPUT;
			bool jOut = m->portAssignments[j].isValid() && m->portAssignments[j].type == engine::Port::OUTPUT;
			bool iIn = m->portAssignments[i].isValid() && m->portAssignments[i].type == engine::Port::INPUT;
			bool jIn = m->portAssignments[j].isValid() && m->portAssignments[j].type == engine::Port::INPUT;
			REQUIRE(((iOut && jIn) || (iIn && jOut)));
		}
	}
	REQUIRE(connectionCount == 2);

	Test::destroyModule(m);
}

TEST_CASE("randomizeCurrentScene - replaces the scene's previous topology", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->portAssignments[0].moduleId = 42; m->portAssignments[0].portId = 0; m->portAssignments[0].type = engine::Port::OUTPUT;
	m->portAssignments[1].moduleId = 42; m->portAssignments[1].portId = 1; m->portAssignments[1].type = engine::Port::INPUT;
	// Stale connection between cells with no valid port assignment — must disappear.
	m->sceneStore.connections[m->sceneStore.current][5] |= (1ULL << 6);
	m->sceneStore.connections[m->sceneStore.current][6] |= (1ULL << 5);

	m->randomizeCurrentScene();

	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 5, 6) == false);
	REQUIRE(m->sceneStore.isConnected(m->sceneStore.current, 0, 1) == true);  // the only valid pair — deterministic

	Test::destroyModule(m);
}

TEST_CASE("randomizePortAssignmentsFrom - clears every cell even when candidates is empty", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->portAssignments[0].moduleId = 42; m->portAssignments[0].portId = 0; m->portAssignments[0].type = engine::Port::OUTPUT;
	m->cellLabels[0] = "stale label";

	m->randomizePortAssignmentsFrom({});

	REQUIRE(m->portAssignments[0].isValid() == false);
	REQUIRE(m->cellLabels[0].empty());

	Test::destroyModule(m);
}

TEST_CASE("randomizePortAssignmentsFrom - a single candidate is assigned to exactly one cell", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	for (int i = 0; i < MATRIX_COUNT; i++) m->cellLabels[i] = "stale label";

	std::vector<PortAssignment> candidates(1);
	candidates[0].moduleId = 99;
	candidates[0].portId = 3;
	candidates[0].type = engine::Port::OUTPUT;

	m->randomizePortAssignmentsFrom(candidates);

	// No duplicates: with only one candidate, exactly one cell gets it and every other cell
	// is cleared rather than repeating the same port.
	int matches = 0;
	for (int i = 0; i < MATRIX_COUNT; i++) {
		REQUIRE(m->cellLabels[i].empty());
		if (!m->portAssignments[i].isValid()) continue;
		REQUIRE(m->portAssignments[i].moduleId == 99);
		REQUIRE(m->portAssignments[i].portId == 3);
		REQUIRE(m->portAssignments[i].type == engine::Port::OUTPUT);
		matches++;
	}
	REQUIRE(matches == 1);

	Test::destroyModule(m);
}

TEST_CASE("randomizePortAssignmentsFrom - each candidate is used at most once, surplus cells cleared", "[SpliceKit]") {
	SpliceKitModule* m = createModule();

	std::vector<PortAssignment> candidates(3);
	candidates[0] = {10, engine::Port::OUTPUT, 0};
	candidates[1] = {20, engine::Port::INPUT,  1};
	candidates[2] = {30, engine::Port::OUTPUT, 2};

	m->randomizePortAssignmentsFrom(candidates);

	std::vector<bool> candidateUsed(candidates.size(), false);
	int assignedCount = 0;
	for (int i = 0; i < MATRIX_COUNT; i++) {
		const auto& pa = m->portAssignments[i];
		if (!pa.isValid()) continue;
		assignedCount++;
		bool matched = false;
		for (size_t c = 0; c < candidates.size(); c++) {
			if (pa.moduleId != candidates[c].moduleId || pa.portId != candidates[c].portId
			    || pa.type != candidates[c].type) continue;
			REQUIRE(candidateUsed[c] == false);  // no duplicates
			candidateUsed[c] = true;
			matched = true;
			break;
		}
		REQUIRE(matched);
	}
	// All 3 candidates were placed (fewer candidates than cells), and the other 61 cells cleared.
	REQUIRE(assignedCount == 3);

	Test::destroyModule(m);
}

TEST_CASE("randomizePortAssignmentsFrom - fills every cell without duplicates when there are more candidates than cells", "[SpliceKit]") {
	SpliceKitModule* m = createModule();

	std::vector<PortAssignment> candidates(MATRIX_COUNT + 20);
	for (size_t i = 0; i < candidates.size(); i++) {
		candidates[i] = {(int64_t)i, (i % 2 == 0) ? engine::Port::OUTPUT : engine::Port::INPUT, (int)i};
	}

	m->randomizePortAssignmentsFrom(candidates);

	std::set<int64_t> seenModuleIds;
	for (int i = 0; i < MATRIX_COUNT; i++) {
		REQUIRE(m->portAssignments[i].isValid() == true);  // every cell filled — enough candidates for all
		auto inserted = seenModuleIds.insert(m->portAssignments[i].moduleId);
		REQUIRE(inserted.second == true);  // moduleId (unique per candidate here) never repeats
	}

	Test::destroyModule(m);
}

TEST_CASE("onRandomize - runs randomizePortAssignments directly without queueing", "[SpliceKit]") {
	SpliceKitModule* m = createModule();

	// Rack calls onRandomize() on the GUI thread, so it enumerates ports inline rather than
	// enqueueing — which would make taskProcessorUi multi-producer against the engine thread.
	// No modules in the rack in this test, so this is a safe no-op; just confirm it runs
	// without throwing (real port enumeration is exercised via randomizePortAssignmentsFrom).
	REQUIRE_NOTHROW(m->onRandomize());
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);

	Test::destroyModule(m);
}
