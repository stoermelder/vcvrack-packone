#pragma once
#include "../plugin.hpp"
#include <functional>

namespace StoermelderPackOne {

struct ModuleSelectProcessor {
	enum class LEARN_MODE {
		OFF = 0,
		SINGLE = 1,
		MULTI = 2
	};

	Widget* owner;
	std::function<void(ModuleWidget* mw, Vec pos)> callback;
	LEARN_MODE learnMode = LEARN_MODE::OFF;

	void setOwner(Widget* owner) {
		this->owner = owner;
	}

	void startLearn(std::function<void(ModuleWidget* mw, Vec pos)> callback, LEARN_MODE mode = LEARN_MODE::SINGLE) {
		if (owner == NULL) {
			return;
		}
		if (learnMode != LEARN_MODE::OFF) {
			disableLearn();
			return;
		}

		this->callback = callback;
		learnMode = mode;
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
		if (APP && APP->window && APP->window->win) {
			glfwSetCursor(APP->window->win, NULL);
		}
	}

	bool isLearning() {
		return learnMode != LEARN_MODE::OFF;
	}

	void commitLearn(bool forceDisable) {
		if (learnMode == LEARN_MODE::SINGLE || forceDisable) {
			disableLearn();
		}
	}

	void processDeselect() {
		if (isLearning()) {
			bool success = false;
			DEFER({
				commitLearn(!success);
			});

			// Learn module
			Widget* w = APP->event->getDraggedWidget();
			if (!w) return;
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
			if (!mw || mw == owner) return;
			Vec pos = w->getRelativeOffset(Vec(1.f, 1.f), mw);
			success = true;
			if (callback) callback(mw, pos);
		}
	}

	// Only needed with LEARN_MODE::MULTI
	void step() {
		if (learnMode == LEARN_MODE::MULTI && APP->event->getSelectedWidget() != owner) {
			APP->event->setSelectedWidget(owner);
		}
	}
};

} // namespace StoermelderPackOne