#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {
namespace vcv {

// Searches for an existing cable between the named output and input ports.
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

// Removes a cable from the rack. Pass addToHistory=false to skip
// the undo entry (e.g. when the caller manages its own composite undo action).
static void removeCable(CableWidget* cw, bool addToHistory = true) {
	history::CableRemove* h = new history::CableRemove;
	h->setCable(cw);
	if (addToHistory) APP->history->push(h);
	else delete h;
	APP->scene->rack->removeCable(cw);
	delete cw;
}

// Creates a cable between the given output and input ports and adds
// it to the rack. Pass addToHistory=false to skip the undo entry (e.g. when the
// caller manages its own composite undo action).
static void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory = true) {
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


} // namespace vcv
} // namespace StoermelderPackOne
