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
	p->addModel(modelInfix16);
	p->addModel(modelInfix8);
	p->addModel(modelStrip);
	p->addModel(modelEightFace);
	p->addModel(modelEightFaceX2);
	p->addModel(modelMidiCat);
	p->addModel(modelSipo);
	p->addModel(modelFourRounds);
	p->addModel(modelArena);
	p->addModel(modelMaze);
	p->addModel(modelAudioInterface64);
}