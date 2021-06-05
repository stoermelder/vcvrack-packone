#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {


struct StoermelderLedDisplay : LightWidget {
	std::shared_ptr<Font> font;
	NVGcolor color = nvgRGB(0xef, 0xef, 0xef);
	std::string text;
	Vec textOffset;

	StoermelderLedDisplay() {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		box.size = Vec(39.1f, 13.2f);
	}

	void draw(const DrawArgs& args) override {
		if (text.length() > 0) {
			nvgFillColor(args.vg, color);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, 0.0);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFontSize(args.vg, 12);
			nvgTextBox(args.vg, 0.f, box.size.y / 2.f, box.size.x, text.c_str(), NULL);
		}
	}
};


struct LedTextDisplay : OpaqueWidget {
	std::string text;
	std::shared_ptr<Font> font;
	float fontSize;
	math::Vec textOffset;
	NVGcolor color;
	NVGcolor bgColor;

	LedTextDisplay() {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		fontSize = 12.f;
		color = nvgRGB(0xff, 0xd7, 0x14);
		bgColor = color::BLACK;
		textOffset = math::Vec(5, 2);
	}

	void draw(const DrawArgs& args) override {
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		if (bgColor.a > 0.0) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.0);
			nvgFillColor(args.vg, bgColor);
			nvgFill(args.vg);
		}

		if (font->handle >= 0) {
			nvgFillColor(args.vg, color);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, 0.0);

			nvgFontSize(args.vg, 12);
			nvgTextBox(args.vg, textOffset.x, textOffset.y + fontSize, box.size.x - 2 * textOffset.x, text.c_str(), NULL);
		}
		nvgResetScissor(args.vg);
	}
};

} // namespace StoermelderPackOne