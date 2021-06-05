#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

static const int MAX_CHANNELS = 128;

#define MIDIOPTION_VELZERO_BIT 0

enum class CCMODE {
	DIRECT = 0,
	PICKUP1 = 1,
	PICKUP2 = 2,
	TOGGLE = 3,
	TOGGLE_VALUE = 4
};

enum class NOTEMODE {
	MOMENTARY = 0,
	MOMENTARY_VEL = 1,
	TOGGLE = 2,
	TOGGLE_VEL = 3
};


struct MidiCatCtxBase : Module {
	virtual std::string getMidiCatId() { return ""; }
};


struct MemParam {
	int paramId = -1;
	int cc = -1;
	CCMODE ccMode;
	bool cc14bit = false;
	int note = -1;
	NOTEMODE noteMode;
	std::string label;
	int midiOptions = 0;
	float slew = 0.f;
	float min = 0.f;
	float max = 1.f;
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