#include "cables.hpp"
#include "modules.hpp"
#include "history.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production cable implementation, backed by the live Rack widget tree. Module lookups
// go through ModuleAccess (vcv_modules.hpp), never directly to the Rack widget tree, so a
// mock `moduleAccess` is usable underneath the *real* RackCableAccess.
struct RackCableAccess : CableAccess {
	using CableAccess::removeCable;  // keep the port-pair overload visible beside the object one

	::rack::app::CableWidget* findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) const override {
		::rack::app::ModuleWidget* outputMw = moduleAccessFor().getModuleWidget(outputModuleId);
		if (!outputMw) return nullptr;
		for (::rack::app::PortWidget* outPort : outputMw->getOutputs()) {
			if (outPort->portId != outputPortId) continue;
			for (::rack::app::CableWidget* cw : APP->scene->rack->getCablesOnPort(outPort)) {
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
		::rack::history::CableRemove* h = new ::rack::history::CableRemove;
		h->setCable(cw);
		if (addToHistory) historyAccessFor().push(h);
		else delete h;
		APP->scene->rack->removeCable(cw);
		delete cw;
	}

	const std::vector<CableWidget*> getCompleteCables() const override {
		return APP->scene->rack->getCompleteCables();
	}

	void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory, NVGcolor color = color::BLACK_TRANSPARENT) override {
		::rack::app::ModuleWidget* outputMw = moduleAccessFor().getModuleWidget(outModuleId);
		::rack::app::ModuleWidget* inputMw = moduleAccessFor().getModuleWidget(inModuleId);
		if (!outputMw || !inputMw) return;

		::rack::engine::Cable* c = new ::rack::engine::Cable;
		c->outputId = outPortId;
		c->outputModule = outputMw->module;
		c->inputId = inPortId;
		c->inputModule = inputMw->module;
		APP->engine->addCable(c);

		::rack::app::CableWidget* cw = new ::rack::app::CableWidget;
		cw->color = (color.a != 0.f) ? color : APP->scene->rack->getNextCableColor();
		cw->setCable(c);
		APP->scene->rack->addCable(cw);
		::rack::history::CableAdd* h = new ::rack::history::CableAdd;
		h->setCable(cw);
		if (addToHistory) historyAccessFor().push(h);
		else delete h;
	}
};

// The single definition of the swappable cable layer's active access. See the comment on
// the `extern` declaration in vcv_cables.hpp for why this must live in exactly one TU
// rather than being `static` in the header.
CableAccess* cableAccess = nullptr;

CableAccess& cableAccessFor() {
	static RackCableAccess rackAccess;
	return cableAccess ? *cableAccess : rackAccess;
}

} // namespace vcv
} // namespace StoermelderPackOne
