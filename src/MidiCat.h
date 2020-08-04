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

struct MemParam {
	int paramId = -1;
	int cc = -1;
	CCMODE ccMode;
	int note = -1;
	NOTEMODE noteMode;
	std::string label;
};

struct MemModule {
	std::string pluginName;
	std::string moduleName;
	std::list<MemParam*> paramMap;
	~MemModule() {
		for (auto it : paramMap) delete it;
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne