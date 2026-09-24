#pragma once
// Headless unit tests for AhabEditorState: the cursor + rectangle-selection
// clamping model extracted from AhabRenderer. Pure data math, no widget — the
// whole point of the extraction is that this is testable without AhabSimWidget.
// Included by Ahab.test.cpp.

#include "../../test/test_plugin.hpp"
#include "AhabEditorState.hpp"

using StoermelderPackOne::Ahab::AhabEditorState;


TEST_CASE("fresh-state invariants", "[AhabEditorState]") {
	AhabEditorState s;

	// Default cursor at origin; selection is 1x1 at the origin (NOT 0x0); insert
	// mode off.
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);

	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 0);
	CHECK(sx == 0);
	CHECK(sh == 1);
	CHECK(sw == 1);

	CHECK(s.getInsertMode() == false);

	// Regression pin for the old 0x0 default: clamping on a fresh state must
	// not underflow (sel_y + sel_h - 1).
	s.clampCursorToSelection();
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);
}


TEST_CASE("setCursor is a plain move, not selection-clamped", "[AhabEditorState]") {
	AhabEditorState s;

	// Park a selection elsewhere so the old selection-clamping bug would bite.
	s.setSelection(5, 5, 3, 3, 10, 10);
	s.setCursor(0, 0); // unbounded: plain assignment
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);

	// Field-bounds clamp
	s.setCursor(99, 99, 10, 10);
	s.getCursor(y, x);
	CHECK(y == 9);
	CHECK(x == 9);

	// Degenerate (zero-size) field is a no-op
	s.setCursor(2, 2, 0, 0);
	s.getCursor(y, x);
	CHECK(y == 9);
	CHECK(x == 9);
}


TEST_CASE("setSelection clamps to field and re-clamps cursor", "[AhabEditorState]") {
	AhabEditorState s;

	// Normalize zero-size selection to 1x1 and re-clamp the cursor into it.
	s.setCursor(3, 3, 10, 10);
	s.setSelection(1, 1, 0, 0, 10, 10);
	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 1);
	CHECK(sx == 1);
	CHECK(sh == 1);
	CHECK(sw == 1);
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 1); // cursor snapped into the new selection
	CHECK(x == 1);

	// Clamp height/vertical start: y is pulled up so y+h fits in the field.
	s.setSelection(8, 8, 5, 5, 10, 10);
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 5);
	CHECK(sh == 5);
	CHECK(sx == 5);
	CHECK(sw == 5);

	// h == field_h clamps y to 0.
	s.setSelection(3, 0, 10, 10, 10, 10);
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 0);
	CHECK(sh == 10);

	// y + h exactly equals field_h: no clamp.
	s.setSelection(5, 0, 5, 10, 10, 10);
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 5);
	CHECK(sh == 5);
}


TEST_CASE("Unbounded setSelection also clamps the cursor", "[AhabEditorState]") {
	AhabEditorState s;

	// Regression pin for the simLoad()/simClear() bug: with the cursor parked
	// inside a stale selection, an unbounded setSelection(0,0,1,1) must pull
	// the cursor to (0,0), not leave it stranded.
	s.setSelection(5, 5, 3, 3, 10, 10);
	s.setCursor(0, 0); // plain move — the cursor leaves the old selection
	s.setSelection(0, 0, 1, 1); // unbounded
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);
	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 0);
	CHECK(sx == 0);
	CHECK(sh == 1);
	CHECK(sw == 1);
}


TEST_CASE("Escape path leaves the cursor put", "[AhabEditorState]") {
	AhabEditorState s;

	// Escape collapses the selection to the cursor via an unbounded
	// setSelection(cy, cx, 1, 1); the new clamp must be a no-op there.
	s.setCursor(4, 6, 10, 10);
	s.setSelection(2, 3, 5, 5, 10, 10);
	s.setCursor(4, 6, 10, 10);
	s.setSelection(4, 6, 1, 1); // Escape: collapse to cursor
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 4);
	CHECK(x == 6);
	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 4);
	CHECK(sx == 6);
	CHECK(sh == 1);
	CHECK(sw == 1);
}


TEST_CASE("moveCursorRelative clamps to field bounds", "[AhabEditorState]") {
	AhabEditorState s;

	// Clamp at the low end.
	s.setCursor(0, 0, 10, 10);
	s.moveCursorRelative(-1, -1, 10, 10);
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);

	// Clamp at the high end.
	s.moveCursorRelative(100, 100, 10, 10);
	s.getCursor(y, x);
	CHECK(y == 9);
	CHECK(x == 9);

	// Delta larger than the field in both directions, negative then positive.
	s.moveCursorRelative(-100, -100, 10, 10);
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);
	s.moveCursorRelative(100, 100, 10, 10);
	s.getCursor(y, x);
	CHECK(y == 9);
	CHECK(x == 9);
}


TEST_CASE("moveCursorRelative extendSelection", "[AhabEditorState]") {
	AhabEditorState s;

	// Anchor at (9,9), then move the cursor away with extendSelection; the rect
	// must span anchor and cursor.
	s.setCursor(9, 9, 10, 10);
	s.clearSelection(); // anchor = cursor = (9,9)
	s.setCursor(2, 4, 10, 10);
	s.moveCursorRelative(-1, -2, 10, 10, true); // cursor -> (1,2)
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 1);
	CHECK(x == 2);
	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 1);
	CHECK(sx == 2);
	CHECK(sh == 9);
	CHECK(sw == 8);

	// extendSelection=false leaves the rect untouched.
	s.setCursor(5, 5, 10, 10);
	s.moveCursorRelative(0, 0, 10, 10, false);
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 1);
	CHECK(sx == 2);
	CHECK(sh == 9);
	CHECK(sw == 8);
}


TEST_CASE("moveCursorRelative re-clamps after field shrink", "[AhabEditorState]") {
	AhabEditorState s;

	// The context-menu slider case: the field shrinks under a cursor near the
	// far edge; moveCursorRelative(0,0,h,w) exists purely for its clamp.
	s.setCursor(8, 8, 10, 10);
	s.moveCursorRelative(0, 0, 4, 4);
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 3);
	CHECK(x == 3);
}


TEST_CASE("updateSelectionToCursor rect math", "[AhabEditorState]") {
	// Anchor top-left, cursor bottom-right.
	{
		AhabEditorState s;
		s.setCursor(0, 0, 10, 10);
		s.clearSelection(); // anchor = (0,0)
		s.setCursor(3, 4, 10, 10);
		s.updateSelectionToCursor();
		Usz sy, sx, sh, sw;
		s.getSelectionRect(sy, sx, sh, sw);
		CHECK(sy == 0); CHECK(sx == 0); CHECK(sh == 4); CHECK(sw == 5);
	}
	// Anchor bottom-right, cursor top-left.
	{
		AhabEditorState s;
		s.setCursor(3, 4, 10, 10);
		s.clearSelection(); // anchor = (3,4)
		s.setCursor(0, 0, 10, 10);
		s.updateSelectionToCursor();
		Usz sy, sx, sh, sw;
		s.getSelectionRect(sy, sx, sh, sw);
		CHECK(sy == 0); CHECK(sx == 0); CHECK(sh == 4); CHECK(sw == 5);
	}
	// Anchor bottom-left, cursor top-right.
	{
		AhabEditorState s;
		s.setCursor(3, 0, 10, 10);
		s.clearSelection(); // anchor = (3,0)
		s.setCursor(0, 4, 10, 10);
		s.updateSelectionToCursor();
		Usz sy, sx, sh, sw;
		s.getSelectionRect(sy, sx, sh, sw);
		CHECK(sy == 0); CHECK(sx == 0); CHECK(sh == 4); CHECK(sw == 5);
	}
	// Anchor top-right, cursor bottom-left.
	{
		AhabEditorState s;
		s.setCursor(0, 4, 10, 10);
		s.clearSelection(); // anchor = (0,4)
		s.setCursor(3, 0, 10, 10);
		s.updateSelectionToCursor();
		Usz sy, sx, sh, sw;
		s.getSelectionRect(sy, sx, sh, sw);
		CHECK(sy == 0); CHECK(sx == 0); CHECK(sh == 4); CHECK(sw == 5);
	}
}


TEST_CASE("clearSelection collapses to 1x1 at the cursor", "[AhabEditorState]") {
	AhabEditorState s;

	s.setCursor(2, 3, 10, 10);
	s.setSelection(0, 0, 5, 5, 10, 10);
	s.clearSelection();
	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 2);
	CHECK(sx == 3);
	CHECK(sh == 1);
	CHECK(sw == 1);
}


TEST_CASE("insertMode toggle/read", "[AhabEditorState]") {
	AhabEditorState s;

	CHECK(s.getInsertMode() == false);
	s.setInsertMode(true);
	CHECK(s.getInsertMode() == true);
	s.toggleInsertMode();
	CHECK(s.getInsertMode() == false);
	s.toggleInsertMode();
	CHECK(s.getInsertMode() == true);
}


TEST_CASE("reset() call sequence", "[AhabEditorState]") {
	AhabEditorState s;

	// reset(): setCursor(0,0,fh,fw) then setSelection(0,0,1,1,fh,fw) ends with
	// the cursor at the origin — unchanged from before the extraction.
	s.setCursor(0, 0, 10, 20);
	s.setSelection(0, 0, 1, 1, 10, 20);
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);
	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 0); CHECK(sx == 0); CHECK(sh == 1); CHECK(sw == 1);
}


TEST_CASE("simLoad()/simClear() call sequence", "[AhabEditorState]") {
	AhabEditorState s;

	// Park the selection/cursor away from the origin first (the pre-bug state).
	s.setCursor(5, 5, 10, 20);
	s.setSelection(5, 5, 3, 3, 10, 20);
	// The file-load/clear path: setCursor(0,0) then an UNBOUNDED
	// setSelection(0,0,1,1) must end with a consistent cursor (0,0) + selection
	// (0,0,1,1), not a cursor stranded in the stale selection.
	s.setCursor(0, 0);
	s.setSelection(0, 0, 1, 1);
	Usz y, x;
	s.getCursor(y, x);
	CHECK(y == 0);
	CHECK(x == 0);
	Usz sy, sx, sh, sw;
	s.getSelectionRect(sy, sx, sh, sw);
	CHECK(sy == 0); CHECK(sx == 0); CHECK(sh == 1); CHECK(sw == 1);
}
