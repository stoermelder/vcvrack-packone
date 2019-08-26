#include "plugin.hpp"


Plugin *pluginInstance;

void init(rack::Plugin *p) {
	pluginInstance = p;

	p->addModel(modelCVMap);
	p->addModel(modelCVMapMicro);
	p->addModel(modelCVPam);
	p->addModel(modelRotorA);
	p->addModel(modelReMoveLite);
	p->addModel(modelBolt);
	p->addModel(modelInfix);
	p->addModel(modelStrip);
	p->addModel(modelEightFace);
	p->addModel(modelMidiCat);
	p->addModel(modelAudioInterface64);
}