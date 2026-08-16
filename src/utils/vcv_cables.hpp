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
	virtual void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory) {}

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

// The production implementation, backed by the live Rack widget tree.
struct RackCableAccess : CableAccess {
	using CableAccess::removeCable;  // keep the port-pair overload visible beside the object one

	CableWidget* findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) const override {
		ModuleWidget* outputMw = APP->scene->rack->getModule(outputModuleId);
		if (!outputMw) return nullptr;
		for (PortWidget* outPort : outputMw->getOutputs()) {
			if (outPort->portId != outputPortId) continue;
			for (CableWidget* cw : APP->scene->rack->getCablesOnPort(outPort)) {
				if (cw->inputPort && cw->inputPort->module &&
					cw->inputPort->module->getId() == inputModuleId &&
					cw->inputPort->portId == inputPortId) {
					return cw;
				}
			}
			break;
		}
		return nullptr;
	}

	void removeCable(CableWidget* cw, bool addToHistory) override {
		history::CableRemove* h = new history::CableRemove;
		h->setCable(cw);
		if (addToHistory) APP->history->push(h);
		else delete h;
		APP->scene->rack->removeCable(cw);
		delete cw;
	}

	void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory) override {
		ModuleWidget* outputMw = APP->scene->rack->getModule(outModuleId);
		ModuleWidget* inputMw  = APP->scene->rack->getModule(inModuleId);
		if (!outputMw || !inputMw) return;

		engine::Cable* c = new engine::Cable;
		c->outputId     = outPortId;
		c->outputModule = outputMw->module;
		c->inputId      = inPortId;
		c->inputModule  = inputMw->module;
		APP->engine->addCable(c);

		CableWidget* cw = new CableWidget;
		cw->color = APP->scene->rack->getNextCableColor();
		cw->setCable(c);
		APP->scene->rack->addCable(cw);
		history::CableAdd* h = new history::CableAdd;
		h->setCable(cw);
		if (addToHistory) APP->history->push(h);
		else delete h;
	}
};

// The active access. Null in production → the shared RackCableAccess is used. Tests point
// this at a mock registry (see SpliceKit.test.hpp).
// This MUST have external linkage with exactly one definition (in vcv.cpp), not the
// `static` per-TU form.
extern CableAccess* cableAccess;

CableAccess& cableAccessFor();


// Thin dispatch wrappers — keep the original free-function API (findCable/removeCable/
// addCableToPort) so existing call sites (Splice-Kit, PanicRoom) are unchanged; all six
// operations now route through the active CableAccess.

P1_UNUSED
static CableWidget* findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) {
	return cableAccessFor().findCable(outputModuleId, outputPortId, inputModuleId, inputPortId);
}

P1_UNUSED
static void removeCable(CableWidget* cw, bool addToHistory = true) {
	cableAccessFor().removeCable(cw, addToHistory);
}

P1_UNUSED
static void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory = true) {
	cableAccessFor().addCableToPort(outModuleId, outPortId, inModuleId, inPortId, addToHistory);
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