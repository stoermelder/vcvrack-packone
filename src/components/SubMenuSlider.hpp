#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct SubMenuSlider : MenuItem {
	static constexpr float SENSITIVITY = 0.001f;

	/** Not owned. */
	Quantity* quantity = NULL;

	SubMenuSlider() {
		box.size.y = BND_WIDGET_HEIGHT;
	}

	void draw(const DrawArgs& args) override {
		BNDwidgetState state = BND_DEFAULT;
		if (APP->event->hoveredWidget == this)
			state = BND_HOVER;
		if (APP->event->draggedWidget == this)
			state = BND_ACTIVE;

		float progress = quantity ? quantity->getScaledValue() : 0.f;
		std::string text = quantity ? quantity->getString() : "";
		bndSlider(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE, state, progress, text.c_str(), NULL);
	}

	void onDragStart(const event::DragStart& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		APP->window->cursorLock();
	}

	void onDragMove(const event::DragMove& e) override {
		if (quantity) {
			quantity->moveScaledValue(SENSITIVITY * e.mouseDelta.x);
		}
	}

	void onDragEnd(const event::DragEnd& e) override {
		APP->window->cursorUnlock();
	}

	void onDoubleClick(const event::DoubleClick& e) override {
		if (quantity)
			quantity->reset();
	}
};

} // namespace StoermelderPackOne