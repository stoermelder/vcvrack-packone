#pragma once
#include <orca-c/base.h>   // Usz, Isz
#include <algorithm>
#include <limits>

namespace StoermelderPackOne {
namespace Ahab {

// Cursor + rectangle-selection editing model for the ORCA grid. Pure data and
// clamping math with no Rack/nanovg dependency, so it is unit-testable headless.
// Owned by AhabSimWidget; AhabRenderer reads it via draw()'s parameter.
//
// Invariant: the selection is always non-empty (h,w >= 1) and the cursor is
// always inside it. Both are established by the defaults below and preserved by
// every mutator.
struct AhabEditorState {
    Usz cursor_y = 0;
    Usz cursor_x = 0;

    Usz sel_anchor_y = 0;
    Usz sel_anchor_x = 0;
    // Default 1x1, NOT 0x0 as in the old renderer: a zero-size selection makes
    // clampCursorToSelection() underflow (sel_y + sel_h - 1).
    Usz sel_y = 0, sel_x = 0, sel_h = 1, sel_w = 1;

    bool insertMode = false;

    // Plain move, optionally clamped to field bounds [0..h-1]/[0..w-1].
    // Degenerate field (0) is a no-op; unbounded defaults = plain assignment.
    // Never clamps to the selection — that is setSelection's job.
    void setCursor(Usz y, Usz x,
        Usz field_h = (Usz)std::numeric_limits<Usz>::max(),
        Usz field_w = (Usz)std::numeric_limits<Usz>::max()) {
        if (field_h == 0 || field_w == 0) return;
        if (y >= field_h) y = field_h - 1;
        if (x >= field_w) x = field_w - 1;
        cursor_y = y;
        cursor_x = x;
    }

    void getCursor(Usz& y, Usz& x) const { y = cursor_y; x = cursor_x; }

    // Clamp relative move to [0..h-1]/[0..w-1]; extendSelection updates the
    // selection rect to the new cursor (Shift+arrow).
    // Precondition: field_h/field_w fit in Isz.
    void moveCursorRelative(Isz dy, Isz dx, Usz field_h, Usz field_w, bool extendSelection = false) {
        if (field_h == 0 || field_w == 0) return;
        Isz ny = (Isz)cursor_y + dy;
        Isz nx = (Isz)cursor_x + dx;
        if (ny < 0) ny = 0;
        if (nx < 0) nx = 0;
        if ((Usz)ny >= field_h) ny = (Isz)field_h - 1;
        if ((Usz)nx >= field_w) nx = (Isz)field_w - 1;
        cursor_y = (Usz)ny;
        cursor_x = (Usz)nx;
        if (extendSelection) updateSelectionToCursor();
    }

    // Collapse to a 1x1 selection at the cursor. (The selection is always
    // active in Ahab; it cannot be disabled, only collapsed.)
    void clearSelection() {
        sel_anchor_y = cursor_y; sel_anchor_x = cursor_x;
        sel_y = cursor_y; sel_x = cursor_x; sel_h = 1; sel_w = 1;
    }

    void updateSelectionToCursor() {
        sel_y = std::min(sel_anchor_y, cursor_y);
        sel_x = std::min(sel_anchor_x, cursor_x);
        sel_h = std::max(sel_anchor_y, cursor_y) - sel_y + 1;
        sel_w = std::max(sel_anchor_x, cursor_x) - sel_x + 1;
    }

    void setSelection(Usz y, Usz x, Usz h, Usz w,
        Usz field_h = (Usz)std::numeric_limits<Usz>::max(),
        Usz field_w = (Usz)std::numeric_limits<Usz>::max()) {
        if (h == 0) h = 1;
        if (w == 0) w = 1;
        if (field_h == (Usz)std::numeric_limits<Usz>::max() || field_w == (Usz)std::numeric_limits<Usz>::max()) {
            sel_y = y; sel_x = x; sel_h = h; sel_w = w;
            sel_anchor_y = y; sel_anchor_x = x;
            clampCursorToSelection();
            return;
        }
        if (field_h == 0 || field_w == 0) { clearSelection(); return; }
        if (h > field_h) { y = 0; h = field_h; }
        else if (y > field_h - h) { y = field_h - h; }
        if (w > field_w) { x = 0; w = field_w; }
        else if (x > field_w - w) { x = field_w - w; }
        sel_y = y; sel_x = x; sel_h = h; sel_w = w;
        sel_anchor_y = y; sel_anchor_x = x;
        clampCursorToSelection();
    }

    void getSelectionRect(Usz& y, Usz& x, Usz& h, Usz& w) const {
        y = sel_y; x = sel_x; h = sel_h; w = sel_w;
    }

    void setInsertMode(bool v) { insertMode = v; }
    bool getInsertMode() const { return insertMode; }
    void toggleInsertMode() { insertMode = !insertMode; }

    // Keep the cursor inside the selection. Public (this is an aggregate
    // struct) so it is directly testable. Safe only for a non-empty selection,
    // which the class invariant guarantees.
    void clampCursorToSelection() {
        cursor_y = std::max(sel_y, std::min(cursor_y, sel_y + sel_h - 1));
        cursor_x = std::max(sel_x, std::min(cursor_x, sel_x + sel_w - 1));
    }
};

} // namespace Ahab
} // namespace StoermelderPackOne
