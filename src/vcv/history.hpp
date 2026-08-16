#pragma once
#include "../plugin.hpp"

// Expands to the compiler's unused-attribute, or nothing where it isn't available.
#if defined(__GNUC__) || defined(__clang__)
#define P1_UNUSED __attribute__((unused))
#else
#define P1_UNUSED
#endif

namespace StoermelderPackOne {
namespace vcv {

// ---- Swappable history layer ----
// Every undo/redo operation routes through HistoryAccess. Production uses RackHistoryAccess
// (defined in history.cpp, backed by APP->history); tests install a recording mock via
// `historyAccess` to assert on pushed actions.
//
// push() takes ownership of the action. The default deletes rather than leaking, so a mock
// that ignores history is still correct (mirrors how RackCableAccess deletes the CableAdd
// it does not push).
struct HistoryAccess {
	virtual ~HistoryAccess() {}

	// Takes ownership. The default deletes rather than leaking, so a mock that ignores
	// history is still correct.
	virtual void push(::rack::history::Action* a) { delete a; }
};

// The active access. Null in production → the shared RackHistoryAccess is used. Tests point
// this at a mock.
// This MUST have external linkage with exactly one definition (in history.cpp), not the
// `static` per-TU form.
extern HistoryAccess* historyAccess;

HistoryAccess& historyAccessFor();


namespace history {

// Thin dispatch wrapper — keep the original free-function spelling (push) so call sites can
// route through the active HistoryAccess without change; every operation now goes through
// the swappable layer.

P1_UNUSED
static void push(::rack::history::Action* a) {
	historyAccessFor().push(a);
}

} // namespace history

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
