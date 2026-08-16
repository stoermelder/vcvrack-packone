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

// ---- Swappable cable layer ----
// Every cable operation in this namespace routes through CableAccess, so the whole layer
// is replaceable: production uses RackCableAccess (the live Rack widget tree); the
// unit-test harness installs a mock registry via `cableAccess` to assert on cable effects
// without a RackWidget/CableWidget tree.
//
// The interface exposes two views over the same operation:
//   - the object view (findCable / removeCable(CableWidget*) / addCableToPort) is the
//     Rack-primitive form, the only one that touches real Rack objects;
//   - the port-pair view (hasCable / addCable / removeCable(ids)) is what Splice-Kit
//     calls; its defaults are expressed in terms of the object view.
// Each implementation overrides the view that suits it: RackCableAccess implements the
// object view, a mock registry implements the port-pair view. The other view's methods
// are left at their safe defaults (nullptr / no-op / delegation), so a mock never has to
// fabricate CableWidget objects and the Rack implementation gets the port-pair API for
// free. Must not be called from the engine thread (the cable API is GUI-thread only).
struct CableAccess {
	virtual ~CableAccess() {}

	// Object view — the Rack-primitive operations. Defaults are null/no-op: only
	// RackCableAccess (or a mock that specifically wants the object view) overrides these.
	virtual CableWidget* findCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId) const { return nullptr; }
	virtual void removeCable(CableWidget* cw, bool addToHistory) {}
	// color: cable color; a fully transparent color (the default color::BLACK_TRANSPARENT)
	// means "use Rack's default next cable color".
	virtual void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory, NVGcolor color = color::BLACK_TRANSPARENT) {}
	// Enumerate all complete (both ends patched) cables in the rack.
	virtual const std::vector<CableWidget*> getCompleteCables() const { return {}; }

	// Port-pair view — expressed in terms of the object view by default.
	virtual bool hasCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId) const {
		return findCable(outModuleId, outPortId, inModuleId, inPortId) != nullptr;
	}
	virtual void addCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory) {
		addCableToPort(outModuleId, outPortId, inModuleId, inPortId, addToHistory);
	}
	virtual void removeCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory) {
		CableWidget* cw = findCable(outModuleId, outPortId, inModuleId, inPortId);
		if (cw) removeCable(cw, addToHistory);
	}
};

// The production implementation. Only the declaration lives here — bodies stay in cables.cpp
// so this header need not pull in modules.hpp/history.hpp. It is declared here, rather than
// being private to the .cpp, so a release build's call sites see the concrete type and
// devirtualize; `final` is what lets the compiler prove no further override exists.
struct RackCableAccess final : CableAccess {
	using CableAccess::removeCable;  // keep the port-pair overload visible beside the object one
	CableWidget* findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) const override;
	void removeCable(CableWidget* cw, bool addToHistory) override;
	const std::vector<CableWidget*> getCompleteCables() const override;
	void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory, NVGcolor color = color::BLACK_TRANSPARENT) override;
};
// The shared production instance, defined in cables.cpp.
extern RackCableAccess rackAccess;


// The mockable seam is keyed on DEBUGPLUGIN rather than a flag of its own: `make
// DEBUGPLUGIN=1` and the test binaries (plugin-test.mk) both define it, so one locally-built
// dylib serves debugging and the test suite alike. This is the contract the other five vcv
// layers follow; vcv/build.cpp carries a sentinel that catches a mismatched dylib.
#ifdef DEBUGPLUGIN
// Null by default → `rackAccess` is used. Tests point this at a mock registry per test (see
// SpliceKit.test.hpp / CableScaffold). MUST have external linkage with exactly one
// definition (in cables.cpp), not the `static` per-TU form.
extern CableAccess* cableAccess;
CableAccess& cableAccessFor();
#else
// Naming the concrete `rackAccess` object gives every wrapper below a known dynamic type, so
// the calls devirtualize and the cross-TU call to cableAccessFor() disappears entirely.
#define cableAccessFor() ::StoermelderPackOne::vcv::rackAccess
#endif


P1_UNUSED
static CableWidget* findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) {
	return cableAccessFor().findCable(outputModuleId, outputPortId, inputModuleId, inputPortId);
}

P1_UNUSED
static void removeCable(CableWidget* cw, bool addToHistory = true) {
	cableAccessFor().removeCable(cw, addToHistory);
}

P1_UNUSED
static std::vector<CableWidget*> getCompleteCables() {
	return cableAccessFor().getCompleteCables();
}

P1_UNUSED
static void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory = true, NVGcolor color = color::BLACK_TRANSPARENT) {
	cableAccessFor().addCableToPort(outModuleId, outPortId, inModuleId, inPortId, addToHistory, color);
}

P1_UNUSED
static bool hasCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId) {
	return cableAccessFor().hasCable(outModuleId, outPortId, inModuleId, inPortId);
}

P1_UNUSED
static void addCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory) {
	cableAccessFor().addCable(outModuleId, outPortId, inModuleId, inPortId, addToHistory);
}

P1_UNUSED
static void removeCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory) {
	cableAccessFor().removeCable(outModuleId, outPortId, inModuleId, inPortId, addToHistory);
}


} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED