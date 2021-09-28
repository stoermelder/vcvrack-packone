#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct StoermelderTrimpot : app::SvgKnob {
	widget::SvgWidget* fg;
	StoermelderTrimpot() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;

		fg = new widget::SvgWidget;
		fb->addChildAbove(fg, tw);

		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Trimpot.svg")));
		fg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Trimpot-fg.svg")));
		sw->box.size = shadow->box.size = Vec(16.6f, 16.6f);
	}
};

struct StoermelderTrimpotSnap : StoermelderTrimpot {
	StoermelderTrimpotSnap() {
		snap = true;
	}
};

struct StoermelderSmallKnob : app::SvgKnob {
	widget::SvgWidget* fg;
	StoermelderSmallKnob() {
		minAngle = -0.83 * M_PI;
		maxAngle = 0.83 * M_PI;

		fg = new widget::SvgWidget;
		fb->addChildAbove(fg, tw);

		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/SmallKnob.svg")));
		fg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/SmallKnob-fg.svg")));
		sw->box.size = shadow->box.size = Vec(22.7f, 22.7f);
	}
};

struct StoermelderLargeKnob : app::SvgKnob {
	widget::SvgWidget* fg;
	StoermelderLargeKnob() {
		minAngle = -0.83 * M_PI;
		maxAngle = 0.83 * M_PI;
		shadow->blurRadius = 1.5f;

		fg = new widget::SvgWidget;
		fb->addChildAbove(fg, tw);

		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/LargeKnob.svg")));
		fg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/LargeKnob-fg.svg")));
		sw->box.size = shadow->box.size = Vec(36.0f, 36.0f);
	}
};

} // namespace StoermelderPackOne