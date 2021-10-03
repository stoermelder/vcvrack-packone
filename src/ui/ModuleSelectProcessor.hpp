#pragma once
#include "../plugin.hpp"
#include <functional>

namespace StoermelderPackOne {

struct ModuleSelectProcessor {
	enum class LEARN_MODE {
		OFF = 0,
		DEFAULT = 1
	};

	Widget* owner;
	std::function<void(ModuleWidget* mw, Vec pos)> callback;
	LEARN_MODE learnMode = LEARN_MODE::OFF;

	void setOwner(Widget* owner) {
		this->owner = owner;
	}

	void startLearn(std::function<void(ModuleWidget* mw, Vec pos)> callback, LEARN_MODE mode = LEARN_MODE::DEFAULT) {
		if (owner == NULL) return;
		this->callback = callback;

		learnMode = learnMode == LEARN_MODE::OFF ? mode : LEARN_MODE::OFF;
		APP->event->setSelectedWidget(owner);
		GLFWcursor* cursor = NULL;
		if (learnMode != LEARN_MODE::OFF) {
			cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		}
		glfwSetCursor(APP->window->win, cursor);
	}

	void disableLearn() {
		owner = NULL;
		callback = { };
		learnMode = LEARN_MODE::OFF;
		glfwSetCursor(APP->window->win, NULL);
	}

	void processDeselect() {
		if (learnMode != LEARN_MODE::OFF) {
			DEFER({
				disableLearn();
			});

			// Learn module
			Widget* w = APP->event->getDraggedWidget();
			if (!w) return;
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
			if (!mw || mw == owner) return;
			Vec pos = w->getRelativeOffset(Vec(1.f, 1.f), mw);
			if (callback) callback(mw, pos);
		}
	}
};

} // namespace StoermelderPackOne