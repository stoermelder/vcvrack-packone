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
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 1.0);
        nvgFillColor(args.vg, fillColor);
        nvgFill(args.vg);
    }
}; // struct MenuColorLabel

} // namespace StoermelderPackOne