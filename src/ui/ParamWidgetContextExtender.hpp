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

			// Was the last touched widget a ParamWidget?
			ParamWidget* pw = dynamic_cast<ParamWidget*>(w);
			if (!pw) return;

			// Retrieve the context menu, if available
			MenuOverlay* overlay = NULL;
			for (auto rit = APP->scene->children.rbegin(); rit != APP->scene->children.rend(); rit++) {
				overlay = dynamic_cast<MenuOverlay*>(*rit);
				if (overlay) break;
			}
			if (!overlay) return;

			Menu* menu = overlay->getFirstDescendantOfType<Menu>();
			if (!menu) return;

			extendParamWidgetContextMenu(pw, menu);
		}
	}

	virtual void extendParamWidgetContextMenu(ParamWidget* pw, Menu* menu) { }

}; // struct ParamWidgetContextExtender

} // namespace StoermelderPackOne