#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {

struct FocusMode {
	bool active = false;
	widget::Widget* focusedWidget = nullptr;
	Widget* overlayWidget = nullptr;
	math::Rect focusedWidgetBox; // Absolute position and size in scene coordinates

	struct FocusOverlayWidget : Widget {
		FocusMode* focusMode = nullptr;

		FocusOverlayWidget(FocusMode* fm) {
			focusMode = fm;
		}

		void drawLayer(const DrawArgs& args, int layer) override {
			if (layer != 3) return;
			if (!focusMode->active) return;

			// Draw dimming overlay everywhere except the focused area
			nvgBeginPath(args.vg);
			// Draw full screen
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			// Cut out the focused widget area using hole winding
			nvgRect(args.vg, RECT_ARGS(focusMode->focusedWidgetBox));
			nvgPathWinding(args.vg, NVG_HOLE);

			// Fill with semi-transparent black
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.92f));
			nvgFill(args.vg);;
		}

		void onHover(const HoverEvent& e) override {
			// Consume hover events to prevent interaction with elements below
			e.consume(this);
		}

		void onHoverScroll(const HoverScrollEvent& e) override {
			// Consume scroll events to prevent scrolling
			e.consume(this);
		}

		void onButton(const event::Button& e) override {
			if (!focusMode->focusedWidgetBox.contains(e.pos)) {
				e.consume(this);
			}
		}

		void onSelectKey(const SelectKeyEvent& e) override {
			if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE && (e.mods & RACK_MOD_MASK) == RACK_MOD_SHIFT) {
				focusMode->deactivate();
				e.consume(this);
			}
		}
	};

	~FocusMode() {
		deactivate();
	}

	// Helper to get absolute position of a widget in scene coordinates
	static math::Vec getAbsolutePos(widget::Widget* w) {
		math::Vec pos = Vec(0.f, 0.f);
		widget::Widget* widget = w;
		while (widget && widget != APP->scene) {
			pos = pos.plus(widget->box.pos);
			widget = widget->parent;
		}
		return pos;
	}

	void activate(widget::Widget* widget) {
		if (active) return;
		focusedWidget = widget;
		active = true;

		// Calculate the absolute position of the focused widget
		math::Vec absolutePos = widget->getRelativeOffset(Vec(), APP->scene->rack);
		focusedWidgetBox = math::Rect(absolutePos, widget->box.size);

		// Create and add the overlay widget
		overlayWidget = new FocusOverlayWidget(this);
		overlayWidget->box.size = APP->scene->rack->box.size;
		APP->scene->rack->addChild(overlayWidget);

		// Request keyboard focus for the overlay so it can capture ESC key
		APP->event->setSelectedWidget(overlayWidget);
	}

	void deactivate() {
		if (!active) return;
		active = false;

		// Remove the overlay widget
		if (overlayWidget) {
			APP->scene->rack->removeChild(overlayWidget);
			delete overlayWidget;
			overlayWidget = nullptr;
		}

		focusedWidget = nullptr;
	}
};

} // namespace StoermelderPackOne
