#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct StoermelderLargeKnob : app::SvgKnob {
	StoermelderLargeKnob() {
		minAngle = -0.83 * M_PI;
		maxAngle = 0.83 * M_PI;
		shadow->blurRadius = 1.5f;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/LargeKnob.svg")));
		sw->box.size = shadow->box.size = Vec(36.0f, 36.0f);
	}
};

} // namespace StoermelderPackOne