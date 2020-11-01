#include "plugin.hpp"


Plugin* pluginInstance;

void init(rack::Plugin* p) {
	pluginInstance = p;

	p->addModel(modelCVMap);
	p->addModel(modelCVMapMicro);
	p->addModel(modelCVPam);
	p->addModel(modelRotorA);
	p->addModel(modelReMoveLite);
	p->addModel(modelBolt);
	p->addModel(modelInfix);
	p->addModel(modelInfixMicro);
	p->addModel(modelStrip);
	p->addModel(modelEightFace);
	p->addModel(modelEightFaceX2);
	p->addModel(modelMidiCat);
	p->addModel(modelMidiCatEx);
	p->addModel(modelSipo);
	p->addModel(modelFourRounds);
	p->addModel(modelArena);
	p->addModel(modelMaze);
	p->addModel(modelHive);
	p->addModel(modelIntermix);
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
	p->addModel(modelFlowerSeq);
	p->addModel(modelFlowerSeqEx);
	p->addModel(modelFlowerTrig);
	p->addModel(modelTransit);
	p->addModel(modelTransitEx);
	p->addModel(modelX4);
	p->addModel(modelFacets);
	p->addModel(modelMacro);
	p->addModel(modelRaw);
	p->addModel(modelMidiMon);
	p->addModel(modelAudioInterface64);

	pluginSettings.readFromJson();
}