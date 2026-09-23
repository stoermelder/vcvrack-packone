#pragma once
#include "../plugin.hpp"
#include "TutorialPlacement.hpp"
#include <functional>
#include <string>
#include <vector>


namespace StoermelderPackOne {
namespace Tutorial {

namespace detail {

// Depth-first search for the first descendant matching `pred`, `root` itself included.
template <typename T>
inline T* findFirstIf(widget::Widget* root, const std::function<bool(T*)>& pred) {
	if (!root) return nullptr;
	if (T* hit = dynamic_cast<T*>(root)) {
		if (pred(hit)) return hit;
	}
	for (widget::Widget* child : root->children) {
		if (T* hit = findFirstIf<T>(child, pred)) return hit;
	}
	return nullptr;
}

// Depth-first search for the first descendant of type T, `root` itself included.
template <typename T>
inline T* findFirst(widget::Widget* root) {
	if (!root) return nullptr;
	if (T* hit = dynamic_cast<T*>(root)) return hit;
	for (widget::Widget* child : root->children) {
		if (T* hit = findFirst<T>(child)) return hit;
	}
	return nullptr;
}

// Maps `w`'s box into `mw`'s local coordinates via both corners, so any ZoomWidget in between
// (or a differing zoom level altogether) is taken into account.
inline math::Rect toModuleRect(widget::Widget* w, app::ModuleWidget* mw) {
	math::Vec a = w->getRelativeOffset(math::Vec(), mw);
	math::Vec b = w->getRelativeOffset(w->box.size, mw);
	return math::Rect::fromCorners(a, b);
}

} // namespace detail

/** A resolver for a step's highlighted area, in module-local coordinates. */
struct Target {
	// Returns whether it resolved `moduleRect` (in mw's local coordinates). `mw` is never null.
	std::function<bool(app::ModuleWidget*, math::Rect&)> resolve;

	static Target none() {
		Target t;
		t.resolve = nullptr;
		return t;
	}

	static Target param(int id) {
		Target t;
		t.resolve = [id](app::ModuleWidget* mw, math::Rect& r) {
			app::ParamWidget* w = mw->getParam(id);
			if (!w) return false;
			r = detail::toModuleRect(w, mw);
			return true;
		};
		return t;
	}

	// Union of mw->getParam(firstId ... firstId + count - 1); resolves whichever ids exist,
	// fails only if none of them do.
	static Target params(int firstId, int count) {
		Target t;
		t.resolve = [firstId, count](app::ModuleWidget* mw, math::Rect& r) {
			bool any = false;
			math::Rect u;
			for (int i = 0; i < count; i++) {
				app::ParamWidget* w = mw->getParam(firstId + i);
				if (!w) continue;
				math::Rect wr = detail::toModuleRect(w, mw);
				u = any ? u.expand(wr) : wr;
				any = true;
			}
			if (!any) return false;
			r = u;
			return true;
		};
		return t;
	}

	static Target input(int id) {
		Target t;
		t.resolve = [id](app::ModuleWidget* mw, math::Rect& r) {
			app::PortWidget* w = mw->getInput(id);
			if (!w) return false;
			r = detail::toModuleRect(w, mw);
			return true;
		};
		return t;
	}

	static Target output(int id) {
		Target t;
		t.resolve = [id](app::ModuleWidget* mw, math::Rect& r) {
			app::PortWidget* w = mw->getOutput(id);
			if (!w) return false;
			r = detail::toModuleRect(w, mw);
			return true;
		};
		return t;
	}

	static Target light(int firstLightId) {
		Target t;
		t.resolve = [firstLightId](app::ModuleWidget* mw, math::Rect& r) {
			std::function<bool(app::ModuleLightWidget*)> pred = [firstLightId](app::ModuleLightWidget* l) {
				return l->firstLightId == firstLightId;
			};
			app::ModuleLightWidget* w = detail::findFirstIf<app::ModuleLightWidget>(mw, pred);
			if (!w) return false;
			r = detail::toModuleRect(w, mw);
			return true;
		};
		return t;
	}

	// First descendant of type W, depth-first.
	template <typename W>
	static Target widget() {
		Target t;
		t.resolve = [](app::ModuleWidget* mw, math::Rect& r) {
			W* w = detail::findFirst<W>(mw);
			if (!w) return false;
			r = detail::toModuleRect(w, mw);
			return true;
		};
		return t;
	}

	static Target widget(std::function<widget::Widget*(app::ModuleWidget*)> lookup) {
		Target t;
		t.resolve = [lookup](app::ModuleWidget* mw, math::Rect& r) {
			widget::Widget* w = lookup(mw);
			if (!w) return false;
			r = detail::toModuleRect(w, mw);
			return true;
		};
		return t;
	}

	// A panel rect in millimetres, for printed panel areas without a widget of their own.
	static Target rect(math::Rect mm) {
		Target t;
		t.resolve = [mm](app::ModuleWidget*, math::Rect& r) {
			r = math::Rect(mm2px(mm.pos), mm2px(mm.size));
			return true;
		};
		return t;
	}
};

struct Step {
	std::string title;
	std::string text;
	Target target;                 // Target::none() -> centered on module
	Side side = Side::AUTO;        // placement preference for this step only
	float width = 0.f;             // bubble width override (module px); 0 = Style default
	std::function<void()> onEnter; // optional
	std::function<void()> onLeave;

	Step(std::string title, std::string text, Target target = Target::none())
		: title(std::move(title)), text(std::move(text)), target(std::move(target)) {}

	Step& prefer(Side s) { side = s; return *this; }
	Step& withWidth(float w) { width = w; return *this; }
};

struct Tutorial {
	std::string title;          // caption above each step title ("SPLICE-KIT · 2 / 7")
	std::vector<Step> steps;
};

// Returns the indices of steps whose target (if any) does not resolve on mw.
inline std::vector<int> unresolvedSteps(app::ModuleWidget* mw, const Tutorial& tutorial) {
	std::vector<int> result;
	for (size_t i = 0; i < tutorial.steps.size(); i++) {
		const Step& step = tutorial.steps[i];
		if (!step.target.resolve) continue;
		math::Rect r;
		if (!step.target.resolve(mw, r)) {
			result.push_back((int) i);
		}
	}
	return result;
}

} // namespace Tutorial
} // namespace StoermelderPackOne


// TutorialOverlay's definition needs the complete Target/Step/Tutorial above; included here,
// after them, rather than at the top, so this file is the one safe entry point regardless of
// what a caller includes first (see TutorialOverlay.hpp's own comment).
#include "TutorialOverlay.hpp"


namespace StoermelderPackOne {
namespace Tutorial {

// Appends a TutorialOverlay as the last child of mw; replaces one already open on mw
// synchronously (not via requestDelete(), which only takes effect on the next step() — a
// caller doing start() twice in a row must see exactly one overlay immediately after the
// second call, not after a frame). No-op if steps is empty.
inline void start(app::ModuleWidget* mw, Tutorial tutorial) {
	if (tutorial.steps.empty()) return;

	for (widget::Widget* child : mw->children) {
		if (TutorialOverlay* existing = dynamic_cast<TutorialOverlay*>(child)) {
			mw->removeChild(existing);
			delete existing;
			break;
		}
	}

	TutorialOverlay* overlay = new TutorialOverlay;
	overlay->tutorial = std::move(tutorial);
	mw->addChild(overlay);
}


// Context menu entry "Tutorial…". The factory is called fresh on each click (not once up
// front), so a tutorial referencing live module state — a step's target resolving by id, text
// mentioning current settings — reflects the module as it is when the user actually opens it.
inline ui::MenuItem* createTutorialMenuItem(app::ModuleWidget* mw, std::function<Tutorial()> factory) {
	return createMenuItem("Tutorial…", "", [mw, factory]() {
		start(mw, factory());
	});
}

} // namespace Tutorial
} // namespace StoermelderPackOne
