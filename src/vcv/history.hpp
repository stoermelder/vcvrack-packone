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


// The production implementation; bodies in the .cpp. Declared here — and `final` — so a
// release build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RackHistoryAccess final : HistoryAccess {
	void push(::rack::history::Action* a) override;
};
// The shared production instance, defined in the .cpp.
extern RackHistoryAccess rackHistoryAccess;


// Debug builds keep the mockable seam; release resolves the access statically. See
// cables.hpp for why, and for the DEBUGPLUGIN contract.
#ifdef DEBUGPLUGIN
// Null by default -> the shared instance above is used. Tests point this at a mock.
extern HistoryAccess* historyAccess;
HistoryAccess& historyAccessFor();
#else
#define historyAccessFor() ::StoermelderPackOne::vcv::rackHistoryAccess
#endif


namespace history {

P1_UNUSED
static void push(::rack::history::Action* a) {
	historyAccessFor().push(a);
}

} // namespace history

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
