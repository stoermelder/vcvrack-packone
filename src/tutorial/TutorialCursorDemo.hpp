#pragma once
// Requires Tutorial.hpp's HighlightStyle to already be defined — always #include "Tutorial.hpp"
// (never this file directly); it includes this one at the point where that's guaranteed.
#include "../plugin.hpp"
#include <cmath>
#include <functional>
#include <string>
#include <vector>


namespace StoermelderPackOne {
namespace Tutorial {

// ---- Cursor demos -----------------------------------------------------------------------------
//
// An illustrative fake cursor that walks between a fixed sequence of points on a module panel,
// pausing on each with a click-ripple animation — for a step that wants to show "click A, then
// click B" as an animated gesture. Module-agnostic: every point and click fires a callback, so
// the demo doesn't need to know what a "cell" or "port" is.

// One stop in a CursorDemoWidget's sequence: `target` resolves this stop's point (module-local
// coordinates) fresh every frame; `onClick` (optional) fires once when the cursor "clicks" here —
// the place to drive whatever real module function the click illustrates.
//
// `keyLabel`, if non-empty, illustrates a keyboard shortcut instead of a click: the widget draws
// a key cap ("SPACE", "ESC", …) and `onClick` fires when the key is "pressed" instead.
struct CursorDemoStop {
	std::function<math::Vec(app::ModuleWidget*)> target;
	std::function<void()> onClick;
	std::string keyLabel;

	CursorDemoStop(std::function<math::Vec(app::ModuleWidget*)> target, std::function<void()> onClick = nullptr)
		: target(std::move(target)), onClick(std::move(onClick)) {}
};

// Convenience CursorDemoStop targeting a ParamWidget's own center. Falls back to (0,0) if the
// id doesn't resolve.
inline CursorDemoStop cursorDemoParamStop(int paramId, std::function<void()> onClick = nullptr) {
	return CursorDemoStop(
		[paramId](app::ModuleWidget* mw) -> math::Vec {
			app::ParamWidget* w = mw->getParam(paramId);
			if (!w) return math::Vec();
			math::Vec a = w->getRelativeOffset(math::Vec(), mw);
			math::Vec b = w->getRelativeOffset(w->box.size, mw);
			return math::Rect::fromCorners(a, b).getCenter();
		},
		std::move(onClick)
	);
}

// Convenience CursorDemoStop illustrating a keyboard shortcut, anchored to the panel's center —
// for a global/hover shortcut (SPACE, ESC, …) with no single control it belongs to.
inline CursorDemoStop cursorDemoKeyStop(std::string keyLabel, std::function<void()> onClick = nullptr) {
	CursorDemoStop stop(
		[](app::ModuleWidget* mw) -> math::Vec { return mw->box.zeroPos().getCenter(); },
		std::move(onClick)
	);
	stop.keyLabel = std::move(keyLabel);
	return stop;
}

// Draws the fake cursor (or, for a keyLabel stop, a key cap), cycling through `stops` in order:
// MOVE (ease to the next stop; instant for a keyLabel stop) -> CLICK (ripple/pop-in + onClick) ->
// HOLD -> next stop's MOVE. Non-interactive (TransparentWidget), purely illustrative.
//
// Usage (see SpliceKit.tutorial.hpp's "Patch" and "Port map view" steps for full examples):
//   auto* cursor = addStepWidget<CursorDemoWidget>(mw);
//   cursor->mw = mw;
//   cursor->stops = { cursorDemoParamStop(PARAM_A), cursorDemoParamStop(PARAM_B, [module]() {
//       module->toggleConnection(a, b);
//   }) };
//   // or, for a keyboard shortcut instead of a click:
//   cursor->stops = { cursorDemoKeyStop("SPACE") };
struct CursorDemoWidget : widget::TransparentWidget {
	enum Phase { MOVE, CLICK, HOLD };
	static constexpr float MOVE_DURATION = 0.5f;
	static constexpr float CLICK_DURATION = 0.35f;
	static constexpr float HOLD_DURATION = 0.7f;

	app::ModuleWidget* mw = nullptr;
	std::vector<CursorDemoStop> stops;

	size_t index = 0; // stops[index] is the stop just reached (CLICK/HOLD) or being moved to (MOVE)
	Phase phase = MOVE;
	float phaseElapsed = 0.f;
	double lastTime = -1.0;
	math::Vec cursorPos;

	// A keyLabel stop has no "walking there", so its MOVE phase collapses to instant.
	float phaseDuration(Phase p) const {
		switch (p) {
			case MOVE: return stops[index].keyLabel.empty() ? MOVE_DURATION : 0.f;
			case CLICK: return CLICK_DURATION;
			case HOLD: return HOLD_DURATION;
		}
		return 0.f;
	}

	size_t prevIndex() const {
		return index == 0 ? stops.size() - 1 : index - 1;
	}

	void step() override {
		box.size = mw ? mw->box.size : math::Vec(); // covers the panel so drawLayer(1) can paint over it

		double now = vcv::fs::getTime();
		float dt = lastTime < 0.0 ? 0.f : (float) (now - lastTime);
		lastTime = now;

		if (mw && !stops.empty()) {
			phaseElapsed += dt;
			float duration = phaseDuration(phase);
			while (phaseElapsed >= duration) {
				phaseElapsed -= duration;
				if (phase == MOVE) {
					phase = CLICK;
				}
				else if (phase == CLICK) {
					if (stops[index].onClick) stops[index].onClick();
					phase = HOLD;
				}
				else {
					index = (index + 1) % stops.size();
					phase = MOVE;
				}
				duration = phaseDuration(phase);
			}

			math::Vec from = stops[prevIndex()].target(mw);
			math::Vec to = stops[index].target(mw);
			if (phase == MOVE) {
				float t = phaseElapsed / MOVE_DURATION;
				cursorPos = from.crossfade(to, t);
			}
			else {
				cursorPos = to;
			}
		}
		TransparentWidget::step();
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		if (!mw || stops.empty()) return;
		if (phase == MOVE && !stops[index].keyLabel.empty()) return; // instant transition, nothing to draw

		if (!stops[index].keyLabel.empty()) drawKeyCap(args, stops[index].keyLabel);
		else drawCursor(args);

		TransparentWidget::drawLayer(args, layer);
	}

private:
	void drawCursor(const DrawArgs& args) {
		bool clicking = (phase == CLICK);
		float clickT = clicking ? (phaseElapsed / CLICK_DURATION) : 0.f; // 0..1 over the click

		nvgSave(args.vg);

		// Ripple: an expanding, fading ring centered on the cursor while a click is in progress.
		if (clicking) {
			float radius = 3.f + 6.f * clickT;
			float alpha = 1.f - clickT;
			NVGcolor c = HighlightStyle::accentColor();
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, cursorPos.x, cursorPos.y, radius);
			nvgStrokeColor(args.vg, nvgRGBAf(c.r, c.g, c.b, alpha));
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);
		}

		// The fake cursor itself: a simple filled dot, shrinking slightly on click for a tactile
		// "press" cue.
		float r = clicking ? 3.5f - 1.f * std::sin(clickT * (float) M_PI) : 3.5f;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cursorPos.x, cursorPos.y, r);
		nvgFillColor(args.vg, color::WHITE);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGBf(0.f, 0.f, 0.f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		nvgRestore(args.vg);
	}

	// Illustrative key cap ("SPACE", "ESC", …), popping in (scale + fade) during CLICK and
	// staying at full size/opacity through HOLD.
	void drawKeyCap(const DrawArgs& args, const std::string& label) {
		bool popping = (phase == CLICK);
		float t = popping ? (phaseElapsed / CLICK_DURATION) : 1.f;
		float scale = 0.7f + 0.3f * t;
		float alpha = t;

		nvgSave(args.vg);

		float fontSize = 11.f;
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgFontSize(args.vg, fontSize);
		float bounds[4];
		nvgTextBounds(args.vg, 0.f, 0.f, label.c_str(), nullptr, bounds);
		float textW = bounds[2] - bounds[0];
		float padX = 6.f, padY = 4.f;
		float capW = (textW + 2.f * padX) * scale;
		float capH = (fontSize + 2.f * padY) * scale;

		nvgTranslate(args.vg, cursorPos.x, cursorPos.y);

		NVGcolor accent = HighlightStyle::accentColor();
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, -capW * 0.5f, -capH * 0.5f, capW, capH, 3.f * scale);
		nvgFillColor(args.vg, nvgRGBAf(0.1f, 0.1f, 0.1f, 0.85f * alpha));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGBAf(accent.r, accent.g, accent.b, alpha));
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);

		nvgFontSize(args.vg, fontSize * scale);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, alpha));
		nvgText(args.vg, 0.f, 0.5f, label.c_str(), nullptr);

		nvgRestore(args.vg);
	}
};

} // namespace Tutorial
} // namespace StoermelderPackOne
