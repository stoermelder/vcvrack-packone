#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

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