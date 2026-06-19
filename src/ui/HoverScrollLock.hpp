#pragma once
#include "../plugin.hpp"
#include <chrono>

namespace StoermelderPackOne {

// Injected base that suppresses onHoverScroll for 500 ms after the cursor enters
// the widget, preventing accidental zoom/scroll when the user glides over the
// widget while already scrolling the rack viewport.
//
// Subclasses put their scroll logic in doHoverScroll() instead of onHoverScroll().
// The default doHoverScroll() forwards to Base::onHoverScroll(), so plain scroll
// widgets (e.g. list scrollers) get the right behaviour for free.
template <typename Base>
struct WithHoverScrollLock : Base {
	std::chrono::time_point<std::chrono::system_clock> lastHoverEntry{std::chrono::system_clock::now()};

	void onHover(const widget::Widget::HoverEvent& e) override {
		Base::onHover(e);
		e.consume(this);
	}

	void onEnter(const widget::Widget::EnterEvent& e) override {
		lastHoverEntry = std::chrono::system_clock::now();
		Base::onEnter(e);
	}

	void onHoverScroll(const widget::Widget::HoverScrollEvent& e) override {
		auto now = std::chrono::system_clock::now();
		if (now - lastHoverEntry > std::chrono::milliseconds{350}) {
			doHoverScroll(e);
		}
		else {
			lastHoverEntry = now;
		}
	}

	virtual void doHoverScroll(const widget::Widget::HoverScrollEvent& e) {
		Base::onHoverScroll(e);
	}
};

} // namespace StoermelderPackOne
