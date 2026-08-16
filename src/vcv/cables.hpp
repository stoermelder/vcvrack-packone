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

// The production implementation (RackCableAccess) lives in vcv_cables.cpp; this header only
// declares the swappable interface and the one-definition access pointer.

// The active access. Null in production → the shared RackCableAccess is used. Tests point
// this at a mock registry (see SpliceKit.test.hpp).
// This MUST have external linkage with exactly one definition (in vcv_cables.cpp), not the
// `static` per-TU form.
extern CableAccess* cableAccess;

CableAccess& cableAccessFor();


// Thin dispatch wrappers — keep the original free-function API (findCable/removeCable/
// addCableToPort/getCompleteCables) so existing call sites (Splice-Kit, PanicRoom) are
// unchanged; every operation now routes through the active CableAccess.

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