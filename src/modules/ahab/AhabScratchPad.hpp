#pragma once
#include <orca-c/base.h>   // Usz, Glyph
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace Ahab {

// A plain rectangular glyph grid the AhabRandomizer writes into before handing
// the result to the sim. Replaces the old per-glyph trySet(AhabSim*, ...) path:
// generation becomes a pure function into this buffer, and the result can be
// committed to AhabSim as ONE paste command instead of one queued command per
// cell (which could silently overflow the sim's 512-slot command queue).
//
// The buffer is SELECTION-LOCAL: coordinate (0,0) is the top-left corner of the
// region being generated, not of the field. Generators therefore cannot
// scribble outside the selection — writes outside the rect are dropped (like
// the old trySet clamping to field bounds) and reported via set()'s return
// value.
struct ScratchPad {
	Usz h_;
	Usz w_;
	std::vector<Glyph> cells_;
	bool dirty_ = false;

	ScratchPad(Usz h, Usz w) : h_(h), w_(w), cells_((size_t)h * w, '.') {}

	Usz height() const {
		return h_;
	}
	Usz width() const {
		return w_;
	}

	// Returns false if the write was out of bounds (useful for asserts/tests).
	bool set(Usz y, Usz x, Glyph g) {
		if (y >= h_ || x >= w_) return false;
		cells_[(size_t)y * w_ + x] = g;
		dirty_ = true;
		return true;
	}

	Glyph get(Usz y, Usz x) const {
		if (y >= h_ || x >= w_) return '.';
		return cells_[(size_t)y * w_ + x];
	}

	// Did we place anything? An empty buffer must not produce an undo entry or
	// queue traffic when committed.
	bool dirty() const {
		return dirty_;
	}

	Glyph const* data() const {
		return cells_.data();
	}

	// Serialize to ORCA plain text (rows joined by '\n', no trailing newline) —
	// the natural form for test assertions and for handing to
	// AhabSim::loadRectFromOrcaRequest.
	std::string toOrca() const {
		std::string out;
		out.reserve((size_t)h_ * (w_ + 1));
		for (Usz y = 0; y < h_; ++y) {
			out.append(cells_.data() + (size_t)y * w_, w_);
			if (y + 1 < h_) out.push_back('\n');
		}
		return out;
	}
};

} // namespace Ahab
} // namespace StoermelderPackOne
