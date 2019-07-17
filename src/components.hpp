#pragma once
#include "plugin.hpp"


struct LongPressButton {
	enum Events {
		NO_PRESS,
		SHORT_PRESS,
		LONG_PRESS
	};

	float pressedTime = 0.f;
	dsp::BooleanTrigger trigger;

	Events step(Param &param) {
		Events result = NO_PRESS;
		bool pressed = param.value > 0.f;
		if (pressed && pressedTime >= 0.f) {
			pressedTime += APP->engine->getSampleTime();
			if (pressedTime >= 1.f) {
				pressedTime = -1.f;
				result = LONG_PRESS;
			}
		}

		// Check if released
		if (trigger.process(!pressed)) {
			if (pressedTime >= 0.f) {
				result = SHORT_PRESS;
			}
			pressedTime = 0.f;
		}

		return result;
	}
};