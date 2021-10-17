#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct ParamHandleIndicator {
	ParamHandle* handle = NULL;
	NVGcolor color;
		
	int indicateCount = 0;
	float sampletime;

	void process(float sampleTime, bool force = false) {
		if (!handle) return;
		if (indicateCount > 0 || force) {
			this->sampletime += sampleTime;
			if (this->sampletime > 0.2f) {
				this->sampletime = 0;
				indicateCount--;
				handle->color = std::abs(indicateCount) % 2 == 1 ? color::BLACK : color;
			}
		}
		else {
			handle->color = color;
		}
	}

	void indicate(ModuleWidget* mw) {
		if (indicateCount > 0) return;
		if (mw) {
			// Move the view to center the mapped module
			StoermelderPackOne::Rack::ViewportCenter{mw};
			APP->scene->rackScroll->setZoom(1.f);
		}
		indicateCount = 20;
	}
}; // struct ParamHandleIndicator

} // namespace StoermelderPackOne