#include "rack.hpp"
#include "components.hpp"
#include "helpers.hpp"
#include "pluginsettings.hpp"

using namespace rack;


extern Plugin* pluginInstance;

extern StoermelderSettings pluginSettings;

extern Model* modelCVMap;
extern Model* modelCVMapMicro;
extern Model* modelCVPam;
extern Model* modelRotorA;
extern Model* modelReMoveLite;
extern Model* modelBolt;
extern Model* modelInfix;
extern Model* modelInfixMicro;
extern Model* modelStrip;
extern Model* modelStripCon4;
extern Model* modelStripBlock;
extern Model* modelEightFace;
extern Model* modelEightFaceX2;
extern Model* modelMidiCat;
extern Model* modelMidiCatMem;
extern Model* modelMidiCatMap;
extern Model* modelSipo;
extern Model* modelFourRounds;
extern Model* modelArena;
extern Model* modelMaze;
extern Model* modelHive;
extern Model* modelIntermix;
extern Model* modelSail;
extern Model* modelPile;
extern Model* modelPilePoly;
extern Model* modelMidiStep;
extern Model* modelMirror;
extern Model* modelAffix;
extern Model* modelAffixMicro;
extern Model* modelGrip;
extern Model* modelGlue;
extern Model* modelGoto;
extern Model* modelStroke;
extern Model* modelSpin;
extern Model* modelFlowerSeq;
extern Model* modelFlowerSeqEx;
extern Model* modelFlowerTrig;
extern Model* modelTransit;
extern Model* modelTransitEx;
extern Model* modelX4;
extern Model* modelPrisma;
extern Model* modelMacro;
extern Model* modelRaw;
extern Model* modelMidiMon;
extern Model* modelOrbit;
extern Model* modelEightFaceMk2;
extern Model* modelEightFaceMk2Ex;
extern Model* modelMidiPlug;
extern Model* modelAudioInterface64;
extern Model* modelMb;


namespace StoermelderPackOne {

bool registerSingleton(std::string name, ModuleWidget* mw);
bool unregisterSingleton(std::string name, ModuleWidget* mw);

} // namespace StoermelderPackOne