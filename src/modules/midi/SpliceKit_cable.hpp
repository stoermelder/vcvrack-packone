#pragma once
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace SpliceKit {

// GUI thread — searches for an existing cable between the named output and input ports.
// Returns nullptr if not found. Must not be called from the engine thread.
static CableWidget* findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) {
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

// GUI thread — removes a cable from the rack and records it in undo history.
static void removeCable(CableWidget* cw) {
	history::CableRemove* h = new history::CableRemove;
	h->setCable(cw);
	APP->history->push(h);
	APP->scene->rack->removeCable(cw);
	delete cw;
}

// GUI thread — creates a cable between the given output and input ports,
// adds it to the rack, and pushes a CableAdd undo action.
static void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId) {
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
	APP->history->push(h);
}

} // namespace SpliceKit
} // namespace StoermelderPackOne
