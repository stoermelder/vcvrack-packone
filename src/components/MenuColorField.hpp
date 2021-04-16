#pragma once
#include "../plugin.hpp"
#include "MenuColorLabel.hpp"

namespace StoermelderPackOne {

struct MenuColorField : ui::TextField {
    bool firstRun = true;
    MenuColorLabel* colorLabel = NULL;
    bool* textSelected = NULL;

    MenuColorField() {
        box.size.x = 80.f;
    }

    void step() override {
        if (firstRun) {
            text = color::toHexString(initColor());
            firstRun = false;
        }
        ui::TextField::step();
    }

    void onSelectKey(const event::SelectKey& e) override {
        if (colorLabel) {
            colorLabel->fillColor = color::fromHexString(rack::string::trim(text));
        }
        if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
            returnColor(color::fromHexString(rack::string::trim(text)));
            ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
            overlay->requestDelete();
            e.consume(this);
        }
        if (!e.getTarget()) {
            ui::TextField::onSelectKey(e);
        }
    }

    void onButton(const event::Button& e) override {
        if (textSelected) *textSelected = false;
        TextField::onButton(e);
    }

    virtual void returnColor(NVGcolor color) { }
    virtual NVGcolor initColor() { return color::BLACK; }
};


} // namespace StoermelderPackOne