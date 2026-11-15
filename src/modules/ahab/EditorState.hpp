#pragma once
#include <orca-c/base.h>   // Usz, Isz
#include "Ahab.hpp"        // MAX_FIELD_HEIGHT/WIDTH
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace StoermelderPackOne {
namespace Ahab {

// Cursor + rectangle-selection editing model for the ORCA grid. Pure data and
// clamping math with no Rack/nanovg dependency, so it is unit-testable headless.
// AhabModule owns it as an AhabSharedEditorState (below); AhabRenderer reads a
// snapshot via draw()'s parameter.
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

// The editor state (cursor, selection, insert mode) as one lock-free atomic word, so
// the UI thread (keys, mouse) and the DSP thread (remote commands) can both change it
// without a lock and without routing one through the other. AhabEditorState stays the
// value type that does the math; this class only moves it across threads.
//
// Every transition is computed from one complete snapshot and published in a single
// CAS, so the class invariant (non-empty selection, cursor inside it) holds for every
// value any thread can observe. Concurrent writers are applied one after the other,
// never mixed.
struct AhabSharedEditorState {
	// Every coordinate and size has to fit in 7 bits; Ahab.hpp asserts that
	// MAX_FIELD_HEIGHT/WIDTH do.
	std::atomic<uint64_t> word;

	AhabSharedEditorState() : word(pack(AhabEditorState())) {}

	AhabEditorState load() const {
		return unpack(word.load());
	}

	void store(const AhabEditorState& s) {
		word.store(pack(s));
	}

	// load -> fn(copy) -> compare_exchange. `fn` may run more than once if another
	// thread changed the state in between, so it must not have side effects outside
	// the state it is given.
	template <typename F>
	void update(F fn) {
		uint64_t cur = word.load();
		AhabEditorState s;
		do {
			s = unpack(cur);
			fn(s);
		}
		while (!word.compare_exchange_weak(cur, pack(s)));
	}

	static uint64_t pack(const AhabEditorState& s) {
		uint64_t w = 0;
		int shift = 0;
		auto put = [&](Usz v) {
			w |= (uint64_t)(v > 127 ? 127 : v) << shift;
			shift += 7;
		};
		put(s.cursor_y); put(s.cursor_x);
		put(s.sel_anchor_y); put(s.sel_anchor_x);
		put(s.sel_y); put(s.sel_x); put(s.sel_h); put(s.sel_w);
		w |= (uint64_t)(s.insertMode ? 1 : 0) << shift;
		return w;
	}

	static AhabEditorState unpack(uint64_t w) {
		AhabEditorState s;
		int shift = 0;
		auto get = [&]() {
			Usz v = (Usz)((w >> shift) & 127);
			shift += 7;
			return v;
		};
		s.cursor_y = get(); s.cursor_x = get();
		s.sel_anchor_y = get(); s.sel_anchor_x = get();
		s.sel_y = get(); s.sel_x = get(); s.sel_h = get(); s.sel_w = get();
		s.insertMode = ((w >> shift) & 1) != 0;
		return s;
	}
};

// A block of grid cells copied out of a field. The module owns one for the remote
// `copy`/`paste` commands (DSP thread only, separate from the OS clipboard so a
// remote command never overwrites what the user copied); the widget owns another
// to serialize its selection for the OS clipboard and for files (UI thread only).
// No synchronisation: each instance belongs to one thread. It knows nothing about
// the sim: the caller hands in the field to copy from.
struct AhabClipboard {
	Glyph cells[MAX_FIELD_HEIGHT * MAX_FIELD_WIDTH];
	Usz height = 0, width = 0;

	void clear() {
		height = width = 0;
	}

	// Copies the rect from a field of fh × fw cells, clipped to it. Returns false and
	// keeps the old content if the rect starts outside the field.
	bool copyFrom(const Glyph* field, Usz fh, Usz fw, Usz y, Usz x, Usz h, Usz w) {
		if (y >= fh || x >= fw) return false;
		h = std::min(h, fh - y);
		w = std::min(w, fw - x);
		for (Usz row = 0; row < h; row++) {
			std::memcpy(cells + row * w, field + (y + row) * fw + x, w * sizeof(Glyph));
		}
		height = h;
		width = w;
		return true;
	}

	// ORCA plain text: one line per row, joined by '\n' (no trailing one). Empty if
	// the clipboard is.
	std::string toOrca() const {
		std::string out;
		out.reserve((size_t)height * ((size_t)width + 1));
		for (Usz row = 0; row < height; row++) {
			out.append(cells + row * width, width);
			if (row + 1 < height) out.push_back('\n');
		}
		return out;
	}
};

} // namespace Ahab
} // namespace StoermelderPackOne
