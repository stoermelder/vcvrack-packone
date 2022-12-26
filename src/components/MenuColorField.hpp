#pragma once
#include "../plugin.hpp"
#include "MenuColorLabel.hpp"

namespace StoermelderPackOne {

struct MenuColorField : ui::TextField {
	NVGcolor* color;
	NVGcolor textColor;
	bool* textSelected = NULL;

	MenuColorField() {
		box.size.x = 80.f;
	}

	void step() override {
		if (!color::isEqual(*color, textColor)) {
			// color has been modified outside of this widget
			text = color::toHexString(*color);
			textColor = *color;
		}
		ui::TextField::step();
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
			ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
			overlay->requestDelete();
			e.consume(this);
		}
		if (e.action == GLFW_RELEASE) {
			*color = color::fromHexString(rack::string::trim(text));
			textColor = *color;
		}
		if (!e.getTarget()) {
			ui::TextField::onSelectKey(e);
		}
	}

	void onButton(const event::Button& e) override {
		if (textSelected) *textSelected = false;
		TextField::onButton(e);
	}
};


} // namespace StoermelderPackOne