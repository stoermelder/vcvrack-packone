#include "plugin.hpp"
#include "modules/midiesx/MidiEsx.hpp"
#include "modules/ahab/AhabMidiDriver.hpp"

Plugin* pluginInstance;

void init(rack::Plugin* p) {
	pluginInstance = p;

#ifndef METAMODULE
	p->addModel(modelCVMap);
	p->addModel(modelCVMapCtx);
	p->addModel(modelCVMapMicro);
	p->addModel(modelCVPam);
	p->addModel(modelRotorA);
	p->addModel(modelReMoveLite);
	p->addModel(modelBolt);
	p->addModel(modelInfix);
	p->addModel(modelInfixMicro);
	p->addModel(modelStrip);
	p->addModel(modelStripBay4);
	p->addModel(modelStripPp);
	p->addModel(modelEightFace);
	p->addModel(modelEightFaceX2);
	p->addModel(modelMidiCat);
	p->addModel(modelMidiCatXl);
	p->addModel(modelMidiCatMem);
	p->addModel(modelMidiCatCtx);
	p->addModel(modelMidiCatClk);
	p->addModel(modelMidiCatFine);
	p->addModel(modelSipo);
	p->addModel(modelFourRounds);
	p->addModel(modelArena);
	p->addModel(modelMaze);
	p->addModel(modelHive);
	p->addModel(modelIntermix);
	p->addModel(modelIntermixGate);
	p->addModel(modelIntermixEnv);
	p->addModel(modelIntermixFade);
	p->addModel(modelSail);
	p->addModel(modelPile);
	p->addModel(modelPilePoly);
	p->addModel(modelMidiStep);
	p->addModel(modelMirror);
	p->addModel(modelAffix);
	p->addModel(modelAffixMicro);
	p->addModel(modelGrip);
	p->addModel(modelGlue);
	p->addModel(modelGoto);
	p->addModel(modelStroke);
	p->addModel(modelSpin);
	p->addModel(modelTransit);
	p->addModel(modelTransitEx);
	p->addModel(modelX4);
	p->addModel(modelMacro);
	p->addModel(modelRaw);
	p->addModel(modelMidiMon);
	p->addModel(modelOrbit);
	p->addModel(modelEightFaceMk2);
	p->addModel(modelEightFaceMk2Ex);
	p->addModel(modelMidiPlug);
	p->addModel(modelDirt);
	p->addModel(modelMidiKey);
	p->addModel(modelPanicRoom);
	p->addModel(modelMidiEsx);
	p->addModel(modelAhab);
	p->addModel(modelAudioInterface64);
	p->addModel(modelMb);
	p->addModel(modelMe);
	p->addModel(modelReel);

	StoermelderPackOne::pluginSettings.readFromJson();
	StoermelderPackOne::Ahab::Midi::init();
#else
	p->addModel(modelBolt);
	p->addModel(modelFourRounds);
	p->addModel(modelMaze);
	p->addModel(modelMidiStep);
	p->addModel(modelHive);
	p->addModel(modelOrbit);
	p->addModel(modelPile);
	p->addModel(modelRaw);
#endif

	StoermelderPackOne::pluginSettings.readFromJson();

	if (StoermelderPackOne::pluginSettings.midiEsxDriverEnabled) {
		StoermelderPackOne::MidiEsx::init();
	}
}


namespace StoermelderPackOne {

std::map<std::tuple<std::string, Context*>, Widget*> singletons;

bool registerSingleton(std::string name, Widget* mw) {
	auto it = singletons.find(std::make_tuple(name, APP));
	if (it == singletons.end()) {
		singletons[std::make_tuple(name, APP)] = mw;
		return true;
	}
	return false;
}

bool unregisterSingleton(std::string name, Widget* mw) {
	auto it = singletons.find(std::make_tuple(name, APP));
	if (it != singletons.end() && it->second == mw) {
		singletons.erase(it);
		return true;
	}
	return false;
}

Widget* getSingleton(std::string name) {
	auto it = singletons.find(std::make_tuple(name, APP));
	return it != singletons.end() ? it->second : NULL;
}


std::map<std::tuple<std::string, Context*>, std::set<ExpanderChangeListener*>*> expanderListeners;

void registerExpanderListener(std::string topic, ExpanderChangeListener* l) {
	auto index = std::make_tuple(topic, APP);
	auto it = expanderListeners.find(index);
	if (it == expanderListeners.end()) {
		expanderListeners[index] = new std::set<ExpanderChangeListener *>;
	}
	expanderListeners[index]->insert(l);
}

void unregisterExpanderListener(std::string topic, ExpanderChangeListener* l) {
	auto index = std::make_tuple(topic, APP);
	auto i = expanderListeners[index];
	i->erase(l);
	if (i->size() == 0) {
		delete i;
		expanderListeners.erase(index);
	}
}

void notifyExpanderListeners(std::string topic) {
	auto index = std::make_tuple(topic, APP);
	auto it = expanderListeners.find(index);
	if (it != expanderListeners.end()) {
		for (auto l : *expanderListeners[index]) {
			l->expandersChanged = true;
		}
	}
}

} // namespace StoermelderPackOne