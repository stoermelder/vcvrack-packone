#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

static const int MAX_CHANNELS = 128;

enum CCMODE {
	CCMODE_DIRECT = 0,
	CCMODE_PICKUP1 = 1,
	CCMODE_PICKUP2 = 2
};

enum NOTEMODE {
	NOTEMODE_MOMENTARY = 0,
	NOTEMODE_MOMENTARY_VEL = 1,
	NOTEMODE_TOGGLE = 2
};

struct MidimapParam {
	int paramId = -1;
	int cc = -1;
	CCMODE ccMode;
	int note = -1;
	NOTEMODE noteMode;
	std::string label;
};

struct MidimapModule {
	std::string pluginName;
	std::string moduleName;
	std::list<MidimapParam*> paramMap;
	~MidimapModule() {
		for (auto it : paramMap) delete it;
	}
};

struct MidiCatProcessor {
	virtual void moduleLearn(int moduleId, std::list<MidimapParam*>& params) { }
};

struct MidiCatExpanderMessage {
	int* ccs;
	CCMODE* ccsMode;
	int* notes;
	NOTEMODE* notesMode;
	std::string* textLabel;
	ParamHandle* paramHandles;
};

} // namespace MidiCat
} // namespace StoermelderPackOne