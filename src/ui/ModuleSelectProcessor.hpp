#pragma once
#include "../plugin.hpp"
#include <functional>

namespace StoermelderPackOne {

template<typename TWidget>
struct SelectProcessor {
	enum class LEARN_MODE {
		OFF = 0,
		SINGLE = 1,
		MULTI = 2
	};

	Widget* owner = nullptr;
	std::function<void(TWidget* w, Vec pos)> learnCallback;
	std::function<void()> abortCallback;
	LEARN_MODE learnMode = LEARN_MODE::OFF;

	void setOwner(Widget* owner) {
		this->owner = owner;
	}

	void startLearn(std::function<void(TWidget* w, Vec pos)> learnCallback, LEARN_MODE mode = LEARN_MODE::SINGLE,
			std::function<void()> abortCallback = {}) {
		if (owner == NULL) {
			return;
		}
		if (learnMode != LEARN_MODE::OFF) {
			disableLearn();
			return;
		}

		this->learnCallback = learnCallback;
		this->abortCallback = abortCallback;
		learnMode = mode;
		APP->event->setSelectedWidget(owner);
		GLFWcursor* cursor = NULL;
		if (learnMode != LEARN_MODE::OFF) {
			cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		}
		if (APP->window) glfwSetCursor(APP->window->win, cursor);
	}

	void disableLearn() {
		owner = NULL;
		learnCallback = {};
		if (abortCallback) abortCallback();
		abortCallback = {};
		learnMode = LEARN_MODE::OFF;
		if (APP->window) glfwSetCursor(APP->window->win, NULL);
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

			Widget* w = APP->event->getDraggedWidget();
			if (!w) return;
			TWidget* tw = dynamic_cast<TWidget*>(w);
			if (!tw) tw = w->getAncestorOfType<TWidget>();
			if (!tw || tw == dynamic_cast<TWidget*>(owner)) return;
			Vec pos = w->getRelativeOffset(Vec(1.f, 1.f), tw);
			success = true;
			if (learnCallback) learnCallback(tw, pos);
		}
	}

	// Only needed with LEARN_MODE::MULTI
	void step() {
		if (learnMode == LEARN_MODE::MULTI && APP->event->getSelectedWidget() != owner) {
			APP->event->setSelectedWidget(owner);
		}
	}
};

using ModuleSelectProcessor = SelectProcessor<ModuleWidget>;
using PortSelectProcessor = SelectProcessor<PortWidget>;

} // namespace StoermelderPackOne
