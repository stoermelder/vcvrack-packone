#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct ParamWidgetContextExtender {
	Widget* lastSelectedWidget;

	void step() {
		Widget* w = APP->event->getDraggedWidget();
		if (!w) return;
		if (w != lastSelectedWidget) {
			lastSelectedWidget = w;
			ParamWidget* pw = dynamic_cast<ParamWidget*>(w);
			if (!pw) return;
			extendParamWidgetContextMenu(pw);
		}
	}

    Menu* getContextMenu() {
        MenuOverlay* overlay = NULL;
        for (Widget* child : APP->scene->children) {
            overlay = dynamic_cast<MenuOverlay*>(child);
            if (overlay) break;
        }
        if (!overlay) return NULL;
        Widget* w = overlay->children.front();
        Menu* menu = dynamic_cast<Menu*>(w);
        return menu;
    }

    virtual void extendParamWidgetContextMenu(ParamWidget* pw) { }

}; // struct ParamWidgetContextExtender

} // namespace StoermelderPackOne