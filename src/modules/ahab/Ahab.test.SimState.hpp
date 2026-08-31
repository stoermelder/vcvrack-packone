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

TEST_CASE("Undo and redo restore the tick counter", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(5, 5, false);
	sim.process();

	REQUIRE(sim.getTickNumber() == 0);
	sim.fillRectRequest(1, 1, 2, 2, 'X'); // snapshot captures tick 0
	sim.process();

	// Step the VM so the live tick diverges from the snapshot's tick
	sim.stepRequest();
	sim.process();
	REQUIRE(sim.getTickNumber() == 1);

	// Undo rolls the tick back to the snapshot's tick
	sim.undoRequest();
	sim.process();
	REQUIRE(sim.getTickNumber() == 0);

	// Redo re-restores the stepped tick
	REQUIRE(sim.canRedo() == true);
	sim.redoRequest();
	sim.process();
	REQUIRE(sim.getTickNumber() == 1);
}

TEST_CASE("New edit clears redo history", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(5, 5, false);
	sim.process();

	sim.fillRectRequest(0, 0, 1, 1, 'A');
	sim.process();
	sim.undoRequest();
	sim.process();
	REQUIRE(sim.canRedo() == true);

	// A new edit invalidates redo...
	sim.fillRectRequest(1, 1, 1, 1, 'B');
	sim.process();
	REQUIRE(sim.canRedo() == false);

	// ...and redo is then a safe no-op
	sim.redoRequest();
	sim.process();
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == '.');          // 'A' still undone
	REQUIRE(buffer[1 * w + 1] == 'B');  // new edit still applied
}

TEST_CASE("Multiple undos and redos walk the history", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(3, 3, false);
	sim.process();

	sim.fillRectRequest(0, 0, 1, 3, 'A'); // top row
	sim.process();
	sim.fillRectRequest(2, 0, 1, 3, 'C'); // bottom row
	sim.process();

	sim.undoRequest(); // drop 'C'
	sim.process();
	sim.undoRequest(); // drop 'A'
	sim.process();

	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	for (Usz i = 0; i < h * w; ++i) {
		REQUIRE(buffer[i] == '.');
	}

	// Redo replays the edits in order
	sim.redoRequest();
	sim.process();
	buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == 'A');
	REQUIRE(buffer[2 * w] == '.');

	sim.redoRequest();
	sim.process();
	buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == 'A');
	REQUIRE(buffer[2 * w] == 'C');
	REQUIRE(sim.canRedo() == false);
}

TEST_CASE("Undo and redo on empty history are no-ops", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(2, 2, false);
	sim.process();

	sim.undoRequest(); // nothing to undo -> request is dropped
	sim.process();
	sim.redoRequest(); // nothing to redo
	sim.process();

	REQUIRE(sim.getTickNumber() == 0);
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	REQUIRE(h == 2);
	REQUIRE(w == 2);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == '.');
}

TEST_CASE("Undo limit zero disables undo", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(3, 3, false);
	sim.process();
	sim.setUndoLimit(0);

	sim.fillRectRequest(0, 0, 1, 1, 'X');
	sim.process();

	REQUIRE(sim.canUndo() == false);
}

TEST_CASE("Undo limit trims the redo history too", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(3, 3, false);
	sim.process();
	sim.setUndoLimit(3);

	sim.fillRectRequest(0, 0, 1, 1, 'A');
	sim.process();
	sim.fillRectRequest(1, 1, 1, 1, 'B');
	sim.process();
	sim.undoRequest();
	sim.process();
	sim.undoRequest();
	sim.process();
	REQUIRE(sim.getRedoCount() == 2);

	// Shrinking the limit drops the OLDEST redo entries
	sim.setUndoLimit(1);
	REQUIRE(sim.getRedoCount() == 1);

	sim.redoRequest();
	sim.process();
	REQUIRE(sim.canRedo() == false);
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == 'A');         // the kept (most recent) redo state
	REQUIRE(buffer[1 * w + 1] == '.');
}

TEST_CASE("resetUndo clears undo and redo history", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(3, 3, false);
	sim.process();

	sim.fillRectRequest(0, 0, 1, 1, 'X');
	sim.process();
	sim.undoRequest();
	sim.process();
	REQUIRE(sim.canRedo() == true);

	sim.resetUndo();
	REQUIRE(sim.canUndo() == false);
	REQUIRE(sim.canRedo() == false);
}

TEST_CASE("Reset is undoable", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(3, 3, false);
	sim.process();
	sim.fillRectRequest(0, 0, 3, 3, 'X');
	sim.process();
	sim.stepRequest();
	sim.process();
	REQUIRE(sim.getTickNumber() == 1);

	sim.resetRequest();
	sim.process();
	REQUIRE(sim.getTickNumber() == 0);
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == '.');

	// Undoing the reset restores the filled field AND its tick
	sim.undoRequest();
	sim.process();
	buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == 'X');
	REQUIRE(buffer[h * w - 1] == 'X');
	REQUIRE(sim.getTickNumber() == 1);
}

TEST_CASE("Move rectangle is a single undo snapshot", "[AhabSim]") {
	AhabSim sim;

	sim.setFieldSizeRequest(5, 5, false);
	sim.process();

	sim.fillRectRequest(0, 0, 2, 2, 'M');
	sim.process();
	REQUIRE(sim.getUndoCount() == 1);

	// The whole move lands as ONE undo entry, not one per affected cell
	sim.moveRectRequest(0, 0, 2, 2, 3, 3);
	sim.process();
	REQUIRE(sim.getUndoCount() == 2);

	sim.undoRequest();
	sim.process();
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == 'M');          // back at the source
	REQUIRE(buffer[3 * w + 3] == '.');  // destination cleared again
}
