#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct ParamWidgetContextExtender {
	Widget* lastSelectedWidget;

	struct CenterModuleItem : MenuItem {
		ModuleWidget* mw;
		void onAction(const event::Action& e) override {
			StoermelderPackOne::Rack::ViewportCenter{mw};
		}
	};

	void step() {
		Widget* w = APP->event->getDraggedWidget();
		if (!w) return;

		// Only handle right button events
		if (APP->event->dragButton != GLFW_MOUSE_BUTTON_RIGHT) {
			lastSelectedWidget = NULL;
			return;
		}

		if (w != lastSelectedWidget) {
			lastSelectedWidget = w;

			// Was the last touched widget an ParamWidget?
			ParamWidget* pw = dynamic_cast<ParamWidget*>(w);
			if (!pw) return;

			// Retrieve the context menu, if available
			MenuOverlay* overlay = NULL;
			for (Widget* child : APP->scene->children) {
				overlay = dynamic_cast<MenuOverlay*>(child);
				if (overlay) break;
			}
			if (!overlay) return;
			Widget* w = overlay->children.front();
			Menu* menu = dynamic_cast<Menu*>(w);
			if (!menu) return;

			extendParamWidgetContextMenu(pw, menu);
		}
	}

	virtual void extendParamWidgetContextMenu(ParamWidget* pw, Menu* menu) { }

}; // struct ParamWidgetContextExtender

} // namespace StoermelderPackOne