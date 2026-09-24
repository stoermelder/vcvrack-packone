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

// ---- Swappable engine-query layer ----
// APP->engine->getFrame() is read-only production API with no public setter, so a test harness
// that drives its own frame counter (Test::Harness) cannot make the engine's answer agree with
// it by assignment. Five plugin call sites read getFrame() directly rather than taking
// args.frame — SpliceKit.cpp:542,563,601 stamp outgoing MIDI (msg.frame = getFrame() + 1/2), and
// Ahab.cpp:410 / AhabMidiDriver.cpp:95 do the same — so under an un-seamed harness those stamps
// were always 1 or 2 instead of a real frame.
//
// Same shape as UiAccess::hasWindow(): a single read-only query, mockable so production code
// asking through the seam gets the harness's counter under test without knowing it is being
// tested, while APP->engine keeps its own (irrelevant, under a Harness) frame counter at 0. Not
// a general Engine facade — only the one call plugin code actually makes was added; a richer
// facade built ahead of a real consumer would only invite drift from what production needs.
struct EngineAccess {
	virtual ~EngineAccess() {}

	// The engine's current frame counter. Production is APP->engine->getFrame(). Mock answers
	// whatever a test harness's own DSP clock is currently at.
	virtual int64_t getFrame() const { return 0; }
};


// The production implementation; body in the .cpp. Declared here — and `final` — so a release
// build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RealEngineAccess final : EngineAccess {
	int64_t getFrame() const override;
};
// The shared production instance, defined in the .cpp.
extern RealEngineAccess realEngineAccess;


// Debug builds keep the mockable seam; release resolves the access statically. See cables.hpp
// for why, and for the DEBUGPLUGIN contract.
#ifdef DEBUGPLUGIN
// Null by default -> the shared instance above is used. Tests point this at a mock.
extern EngineAccess* engineAccess;
EngineAccess& engineAccessFor();
#else
#define engineAccessFor() ::StoermelderPackOne::vcv::realEngineAccess
#endif


namespace engine {

P1_UNUSED
static int64_t getFrame() {
	return engineAccessFor().getFrame();
}

} // namespace engine

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
