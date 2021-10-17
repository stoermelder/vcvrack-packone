#pragma once
#include "../plugin.hpp"
#include "LedTextDisplay.hpp"

namespace StoermelderPackOne {

struct StoermelderTextField : LedDisplayTextField {
	float textSize = 12.f;
	const static unsigned int defaultMaxTextLength = 4;
	unsigned int maxTextLength;
	NVGcolor bgColor;
	bool isFocused = false;
	bool doubleClick = false;

	StoermelderTextField() {
		maxTextLength = defaultMaxTextLength;
		textOffset = math::Vec(-0.8f, 0.f);
		color = nvgRGB(0xef, 0xef, 0xef);
		bgColor = color::BLACK_TRANSPARENT;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			//nvgScissor(args.vg, RECT_ARGS(args.clipBox));

			if (bgColor.a > 0.0) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, textOffset.x, 0, box.size.x, box.size.y);
				nvgFillColor(args.vg, bgColor);
				nvgFill(args.vg);
			}

			std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
			if (text.length() > 0 && font && font->handle > 0) {
				nvgFillColor(args.vg, color);
				nvgFontFaceId(args.vg, font->handle);
				nvgTextLetterSpacing(args.vg, 0.0);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFontSize(args.vg, textSize);
				nvgTextBox(args.vg, textOffset.x, box.size.y / 2.f, box.size.x, text.c_str(), NULL);
			}

			if (isFocused) {
				NVGcolor highlightColor = color;
				highlightColor.a = 0.5;

				int begin = std::min(cursor, selection);
				int end = std::max(cursor, selection);
				int len = end - begin;

				// hacky way of measuring character width
				NVGglyphPosition glyphs[4];
				nvgTextGlyphPositions(args.vg, 0.f, 0.f, "a", NULL, glyphs, 4);
				float char_width = -2 * glyphs[0].x;

				float ymargin = 2.f;
				nvgBeginPath(args.vg);
				nvgFillColor(args.vg, highlightColor);
				nvgRect(args.vg,
						box.size.x / 2.f + textOffset.x + (begin - 0.5f * TextField::text.size()) * char_width - 1,
						ymargin,
						(len > 0 ? (char_width * len) : 1) + 1,
						box.size.y - 2.f * ymargin);
				nvgFill(args.vg);
			}

			//nvgResetScissor(args.vg);
		}
	}

	void onSelect(const event::Select& e) override {
		isFocused = true;
		e.consume(this);
	}

	void onDeselect(const event::Deselect& e) override {
		isFocused = false;
		LedDisplayTextField::setText(TextField::text);
		e.consume(NULL);
	}

	void onAction(const event::Action& e) override {
		// this gets fired when the user types 'enter'
		event::Deselect eDeselect;
		onDeselect(eDeselect);
		APP->event->selectedWidget = NULL;
		e.consume(NULL);
	}

	void onDoubleClick(const event::DoubleClick& e) override {
		doubleClick = true;
	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_RELEASE) {
			if (doubleClick) {
				doubleClick = false;
				selectAll();
			}
		}
		LedDisplayTextField::onButton(e);
	}

	void onSelectText(const event::SelectText& e) override {
		if (TextField::text.size() < maxTextLength || cursor != selection) {
			LedDisplayTextField::onSelectText(e);
		} else {
			e.consume(NULL);
		}
	}
};

} // namespace StoermelderPackOne