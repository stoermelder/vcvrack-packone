#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct ParamHandleIndicator : ParamHandle {
	NVGcolor color;
		
	int indicateCount = 0;
	float sampletime;

	void process(float sampleTime, bool force = false) {
		if (indicateCount > 0 || force) {
			this->sampletime += sampleTime;
			if (this->sampletime > 0.2f) {
				this->sampletime = 0;
				indicateCount--;
				ParamHandle::color = std::abs(indicateCount) % 2 == 1 ? color::BLACK : color;
			}
		}
		else {
			ParamHandle::color = color;
		}
	}

	void indicate(ModuleWidget* mw) {
		if (indicateCount > 0) return;
		if (mw) {
			// Move the view to center the mapped module
			StoermelderPackOne::Rack::ViewportCenter{mw};
		}
		indicateCount = 20;
	}
}; // struct ParamHandleIndicator

} // namespace StoermelderPackOne