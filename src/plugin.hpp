#include "rack.hpp"
#include "components.hpp"
#include "helpers.hpp"
#include "pluginsettings.hpp"

using namespace rack;


extern Plugin* pluginInstance;

extern StoermelderSettings pluginSettings;

extern Model* modelCVMap;
extern Model* modelCVMapCtx;
extern Model* modelCVMapMicro;
extern Model* modelCVPam;
extern Model* modelRotorA;
extern Model* modelReMoveLite;
extern Model* modelBolt;
extern Model* modelInfix;
extern Model* modelInfixMicro;
extern Model* modelStrip;
extern Model* modelStripBay4;
extern Model* modelEightFace;
extern Model* modelEightFaceX2;
extern Model* modelMidiCat;
extern Model* modelMidiCatMem;
extern Model* modelMidiCatCtx;
extern Model* modelSipo;
extern Model* modelFourRounds;
extern Model* modelArena;
extern Model* modelMaze;
extern Model* modelHive;
extern Model* modelIntermix;
extern Model* modelIntermixGate;
extern Model* modelIntermixEnv;
extern Model* modelIntermixFade;
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
extern Model* modelTransit;
extern Model* modelTransitEx;
extern Model* modelX4;
extern Model* modelMacro;
extern Model* modelRaw;
extern Model* modelMidiMon;
extern Model* modelOrbit;
extern Model* modelEightFaceMk2;
extern Model* modelEightFaceMk2Ex;
extern Model* modelMidiPlug;
extern Model* modelAudioInterface64;
extern Model* modelMb;
extern Model* modelMe;


namespace StoermelderPackOne {

bool registerSingleton(std::string name, Widget* mw);
bool unregisterSingleton(std::string name, Widget* mw);
Widget* getSingleton(std::string name);

} // namespace StoermelderPackOne