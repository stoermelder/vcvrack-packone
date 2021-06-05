#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct MenuColorLabel : MenuLabel {
	NVGcolor fillColor;

	MenuColorLabel() {
		box.size.y *= 1.4f;
	}
	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 2.f, 2.f, box.size.x - 4.f, box.size.y - 4.f, 2.f);
		nvgFillColor(args.vg, fillColor);
		nvgFill(args.vg);
	}
}; // struct MenuColorLabel

} // namespace StoermelderPackOne