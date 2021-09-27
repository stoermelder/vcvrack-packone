#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct LedDisplayCenterChoiceEx : LedDisplayChoice {
	LedDisplayCenterChoiceEx() {
		box.size = mm2px(math::Vec(0, 28.0 / 3));
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		bgColor = nvgRGBAf(0, 0, 0, 0);
		textOffset = math::Vec(0, 18);
	}

	void draw(const DrawArgs& args) override {
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		nvgGlobalTint(args.vg, color::WHITE);

		if (bgColor.a > 0.0) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgFillColor(args.vg, bgColor);
			nvgFill(args.vg);
		}

		std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
		if (font && font->handle >= 0 && !text.empty()) {
			nvgFillColor(args.vg, color);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -0.6f);
			nvgFontSize(args.vg, 12);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
			NVGtextRow textRow;
			nvgTextBreakLines(args.vg, text.c_str(), NULL, box.size.x, &textRow, 1);
			nvgTextBox(args.vg, textOffset.x, textOffset.y, box.size.x, textRow.start, textRow.end);
		}
		nvgResetScissor(args.vg);
	}
};

} // namespace StoermelderPackOne