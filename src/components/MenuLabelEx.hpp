#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct MenuLabelEx : MenuLabel {
	std::string rightText;
	void draw(const DrawArgs& args) override {
		MenuLabel::draw(args);
		// Right text
		float x = box.size.x - bndLabelWidth(args.vg, -1, rightText.c_str());
		bndMenuLabel(args.vg, x, 0.0, box.size.x, box.size.y, -1, rightText.c_str());
	}
}; // struct MenuLabelEx

} // namespace StoermelderPackOne