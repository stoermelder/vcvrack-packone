#pragma once
#include "../../plugin.hpp"
#include "../../components/LedTextDisplay.hpp"

namespace StoermelderPackOne {
namespace Intermix {

enum FADE_LENGTH {
	FADE_LENGTH_4S = 0,
	FADE_LENGTH_15S = 1,
	FADE_LENGTH_60S = 2
};

template<int PORTS>
struct IntermixBase {
	typedef float (*IntermixMatrix)[PORTS];
	virtual IntermixMatrix expGetCurrentMatrix() { return NULL; }
	virtual int expGetChannelCount() { return 0; }
	virtual void expSetFade(int i, float* fadeIn, float* fadeOut) { }
};


template<typename MODULE>
struct FadeLengthParamQuantity : ParamQuantity {
	MODULE* module;

	float getMaxValue() override {
		switch (module->fadeLengthMode) {
			case FADE_LENGTH_4S: return 4.f;
			case FADE_LENGTH_15S: return 15.f;
			case FADE_LENGTH_60S: return 60.f;
		}
	}

	std::string getUnit() override {
		switch (module->fadeLengthMode) {
			case FADE_LENGTH_4S: return "s (4s max)";
			case FADE_LENGTH_15S: return "s (15s max)";
			case FADE_LENGTH_60S: return "s (60s max)";
		}
	}
};

} // namespace Intermix
} // namespace StoermelderPackOne