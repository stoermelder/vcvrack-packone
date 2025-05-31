#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

using namespace rack;

struct StoermelderTrimpot : app::SvgKnob {
	widget::SvgWidget* fg;
	StoermelderTrimpot() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Trimpot.svg")));

#ifndef METAMODULE
		fg = new widget::SvgWidget;
		fg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Trimpot-fg.svg")));
		fb->addChildAbove(fg, tw);
#endif
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
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/SmallKnob.svg")));

#ifndef METAMODULE
		fg = new widget::SvgWidget;
		fg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/SmallKnob-fg.svg")));
		fb->addChildAbove(fg, tw);
#endif
	}
};

struct StoermelderLargeKnob : app::SvgKnob {
	widget::SvgWidget* fg;
	StoermelderLargeKnob() {
		minAngle = -0.83 * M_PI;
		maxAngle = 0.83 * M_PI;
		shadow->blurRadius = 1.5f;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/LargeKnob.svg")));

#ifndef METAMODULE
		fg = new widget::SvgWidget;
		fg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/LargeKnob-fg.svg")));
		fb->addChildAbove(fg, tw);
#endif
	}
};

} // namespace StoermelderPackOne