#pragma once
#include "../../utils/ScaledMapParam.hpp"
#include <algorithm>
#include <cmath>

namespace StoermelderPackOne {
namespace MidiCat {

/** Relative ("fine") adjustment of a mapped parameter, used while the FINE-expander
 *  holds one of its range gates high: incoming MIDI values are interpreted as an offset
 *  from a reference point instead of an absolute value. Until the controller passes
 *  through the reference point the parameter is held ("pickup"), so enabling fine mode
 *  never makes the parameter jump.
 *  Owned by the DSP thread.
 */
struct PrecisionProcessor {
	bool pickedUp;
	int midiRefPoint;
	float paramValue;
	float precision;

	void init(int midiMin, int midiMax) {
		pickedUp = false;
		this->midiRefPoint = (midiMax - midiMin) / 2;
	}

	void setPrecision(float precision, float paramValue, int midiRefPoint = -1) {
		this->precision = precision;
		this->paramValue = paramValue;
		if (midiRefPoint != -1) this->midiRefPoint = midiRefPoint;
	}

	float process(int midiValue) {
		if (!pickedUp) {
			if (midiValue == midiRefPoint) {
				pickedUp = true;
			}
			return paramValue;
		}
		else {
			float diff = float(midiValue - midiRefPoint) * precision;
			return paramValue + diff;
		}
	}
}; // struct PrecisionProcessor

/** The value pipeline of one mapping slot: scaling, slew-limiting and curve (inherited
 *  from ScaledMapParam), plus the two MIDI-CAT specific behaviours -- clock-quantized
 *  writes driven by the CLK-expander, and feedback read from a bound LED instead of the
 *  parameter itself.
 *  Owned by the DSP thread.
 */
struct MidiCatParam : ScaledMapParam<int> {
	enum class CLOCKMODE {
		OFF = 0,
		ARM = 1,
		ARM_DEFERRED_FEEDBACK = 2
	};

	CLOCKMODE clockMode = CLOCKMODE::OFF;
	int clockSource = 0;

	int setValueDeffered;
	int getValueLast;

	int lightFirstId = -1;
	int lightNumColors = 0;

	PrecisionProcessor precProcessor;

	void reset(bool resetSettings = true) override {
		if (resetSettings) {
			clockMode = CLOCKMODE::OFF;
			clockSource = 0;
		}
		lightFirstId = -1;
		lightNumColors = 0;
		ScaledMapParam<int>::reset(resetSettings);
	}

	void setValue(float i) override {
		switch (clockMode) {
			case CLOCKMODE::OFF:
				ScaledMapParam<int>::setValue(i);
				break;
			case CLOCKMODE::ARM:
			case CLOCKMODE::ARM_DEFERRED_FEEDBACK:
				setValueDeffered = i;
				break;
		}
	}

	int getValue() override {
		if (!hasLight()) {
			switch (clockMode) {
				case CLOCKMODE::OFF:
					return ScaledMapParam<int>::getValue();
				case CLOCKMODE::ARM:
					return setValueDeffered;
				case CLOCKMODE::ARM_DEFERRED_FEEDBACK:
					return getValueLast;
			}
		}
		else {
			if (paramQuantity->module->lights.size() >= size_t(lightFirstId + lightNumColors)) {
				int f = 0;
				for (int i = 0; i < lightNumColors; i++) {
					int b = int(std::ceil(paramQuantity->module->lights[lightFirstId + i].getBrightness() * 4.f));
					f += b << (i * 2);
				}
				return std::min(f << ((3 - lightNumColors) * 2 + 1), 127);
			}
		}
		return 0;
	}

	void tick(int clock) {
		if (clockMode != CLOCKMODE::OFF && clockSource == clock) {
			ScaledMapParam<int>::setValue(setValueDeffered);
		}
		if (clockMode == CLOCKMODE::ARM_DEFERRED_FEEDBACK) {
			getValueLast = ScaledMapParam<int>::getValue();
		}
	}

	bool isNear(int value, int jump = -1) {
		if (value == -1) return false;
		int p = getValue();
		int delta3p = (limitMaxT - limitMinT + 1) * 3 / 100;
		bool r = p - delta3p <= value && value <= p + delta3p;

		if (jump >= 0) {
			int delta7p = (limitMaxT - limitMinT + 1) * 7 / 100;
			r = r && p - delta7p <= jump && jump <= p + delta7p;
		}

		return r;
	}

	void setLight(int lightFirstId = -1, int lightNumColors = 0) {
		this->lightFirstId = lightFirstId;
		this->lightNumColors = lightNumColors;
		if (lightFirstId >= 0 && clockMode == CLOCKMODE::ARM_DEFERRED_FEEDBACK) {
			clockMode = CLOCKMODE::ARM;
		}
	}

	inline bool hasLight() {
		return lightFirstId >= 0;
	}
}; // struct MidiCatParam

} // namespace MidiCat
} // namespace StoermelderPackOne
