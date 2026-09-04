#include "cables.hpp"
#include "modules.hpp"
#include "history.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production cable implementation, backed by the live Rack widget tree. Module lookups
// go through ModuleAccess (vcv_modules.hpp), never directly to the Rack widget tree, so a
// mock `moduleAccess` is usable underneath the *real* RackCableAccess.
// The class is declared in cables.hpp; only the bodies live here.
::rack::app::CableWidget* RackCableAccess::findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) const {
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

void RackCableAccess::removeCable(CableWidget* cw, bool addToHistory) {
	::rack::history::CableRemove* h = new ::rack::history::CableRemove;
	h->setCable(cw);
	if (addToHistory) historyAccessFor().push(h);
	else delete h;
	APP->scene->rack->removeCable(cw);
	delete cw;
}

const std::vector<CableWidget*> RackCableAccess::getCompleteCables() const {
	return APP->scene->rack->getCompleteCables();
}

::rack::history::CableAdd* RackCableAccess::addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory, NVGcolor color) {
	::rack::app::ModuleWidget* outputMw = moduleAccessFor().getModuleWidget(outModuleId);
	::rack::app::ModuleWidget* inputMw = moduleAccessFor().getModuleWidget(inModuleId);
	if (!outputMw || !inputMw) return nullptr;

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
	// Pushed here only when the caller wants one undo entry per cable; otherwise ownership
	// passes to the caller, who folds it into its own ComplexAction. See the declaration.
	if (addToHistory) historyAccessFor().push(h);
	return h;
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the cableAccessFor() macro names directly.
RackCableAccess rackAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
CableAccess* cableAccess = nullptr;
CableAccess& cableAccessFor() {
	return cableAccess ? *cableAccess : rackAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
