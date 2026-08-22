#pragma once
// Test cases for AhabSim state management: undo/redo history, reset, stepping,
// and the RNG seed. Included by AhabSim.test.cpp.

#include "Ahab.test.hpp"


TEST_CASE("Undo and redo functionality", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Make a change with undo enabled
	sim.fillRectRequest(1, 1, 2, 2, 'X');
	sim.process();
	
	REQUIRE(sim.canUndo() == true);
	REQUIRE(sim.getUndoCount() == 1);
	
	// Undo
	sim.undoRequest();
	sim.process();
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Should be back to '.'
	REQUIRE(buffer[1 * w + 1] == '.');
	
	// Redo
	REQUIRE(sim.canRedo() == true);
	sim.redoRequest();
	sim.process();
	
	sim.getDisplayBuffer(h, w);
	buffer = sim.getFieldBuffer();
	
	// Should have 'X' again
	REQUIRE(buffer[1 * w + 1] == 'X');
}

TEST_CASE("Undo limit enforcement", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Set a small undo limit
	sim.setUndoLimit(2);
	
	// Make 3 changes
	sim.fillRectRequest(0, 0, 1, 1, 'A');
	sim.process();
	sim.fillRectRequest(1, 1, 1, 1, 'B');
	sim.process();
	sim.fillRectRequest(2, 2, 1, 1, 'C');
	sim.process();
	
	// Should only have 2 undo entries
	REQUIRE(sim.getUndoCount() <= 2);
}

TEST_CASE("Undo after resize in same queue drain", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(3, 3, false);
	sim.process();
	
	// Make a change with undo
	sim.fillRectRequest(0, 0, 1, 1, 'X');
	sim.process();
	REQUIRE(sim.canUndo() == true);
	
	// Enqueue a resize AND an undo; both drain in the same process() call, so the
	// undo snapshot must be captured against the already-resized field (previously
	// this wrote past the too-small UI-allocated buffer, caught by ASan).
	sim.setFieldSizeRequest(5, 5, false);
	sim.undoRequest();
	sim.process();
	
	// Undo restores the pre-fill 3x3 state; the redo snapshot holds the 5x5 state
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(h == 3);
	REQUIRE(w == 3);
	REQUIRE(buffer[0] == '.');
	REQUIRE(sim.canRedo() == true);
}

TEST_CASE("Reset clears field and state", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Fill field
	sim.fillRectRequest(0, 0, 5, 5, 'X');
	sim.process();
	
	// Step a few times
	sim.stepRequest();
	sim.process();
	sim.stepRequest();
	sim.process();
	
	Usz tick_before = sim.getTickNumber();
	REQUIRE(tick_before > 0);
	
	// Reset
	sim.resetRequest();
	sim.process();
	
	REQUIRE(sim.getTickNumber() == 0);
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Field should be cleared
	for (Usz i = 0; i < h * w; ++i) {
		REQUIRE(buffer[i] == '.');
	}
}

TEST_CASE("Step increments tick counter", "[AhabSim]") {
	AhabSim sim;
	
	Usz tick_before = sim.getTickNumber();
	
	sim.stepRequest();
	sim.process();
	
	REQUIRE(sim.getTickNumber() == tick_before + 1);
	
	sim.stepRequest();
	sim.process();
	
	REQUIRE(sim.getTickNumber() == tick_before + 2);
}

TEST_CASE("Random seed setting", "[AhabSim]") {
	AhabSim sim;
	
	sim.setRandomSeed(12345);
	REQUIRE(sim.getRandomSeed() == 12345);
	
	sim.setRandomSeed(67890);
	REQUIRE(sim.getRandomSeed() == 67890);
}
