#pragma once
#include "plugin.hpp"


struct LongPressButton {
	enum Events {
		NO_PRESS,
		SHORT_PRESS,
		LONG_PRESS
	};

	float pressedTime = 0.f;
	dsp::BooleanTrigger trigger;

	Events step(Param &param) {
		Events result = NO_PRESS;
		bool pressed = param.value > 0.f;
		if (pressed && pressedTime >= 0.f) {
			pressedTime += APP->engine->getSampleTime();
			if (pressedTime >= 1.f) {
				pressedTime = -1.f;
				result = LONG_PRESS;
			}
		}

		// Check if released
		if (trigger.process(!pressed)) {
			if (pressedTime >= 0.f) {
				result = SHORT_PRESS;
			}
			pressedTime = 0.f;
		}

		return result;
	}
};


template <typename TBase>
struct TriangleLeftLight : TBase {
	void drawLight(const widget::Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, this->box.size.x, 0);
		nvgLineTo(args.vg, this->box.size.x, this->box.size.y);
		nvgLineTo(args.vg, 0, this->box.size.y / 2.f);
		nvgClosePath(args.vg);

		// Background
		if (this->bgColor.a > 0.0) {
			nvgFillColor(args.vg, this->bgColor);
			nvgFill(args.vg);
		}

		// Foreground
		if (this->color.a > 0.0) {
			nvgFillColor(args.vg, this->color);
			nvgFill(args.vg);
		}

		// Border
		if (this->borderColor.a > 0.0) {
			nvgStrokeWidth(args.vg, 0.5);
			nvgStrokeColor(args.vg, this->borderColor);
			nvgStroke(args.vg);
		}
	}
};

template <typename TBase>
struct TriangleRightLight : TBase {
	void drawLight(const widget::Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, 0);
		nvgLineTo(args.vg, 0, this->box.size.y);
		nvgLineTo(args.vg, this->box.size.x, this->box.size.y / 2.f);
		nvgClosePath(args.vg);

		// Background
		if (this->bgColor.a > 0.0) {
			nvgFillColor(args.vg, this->bgColor);
			nvgFill(args.vg);
		}

		// Foreground
		if (this->color.a > 0.0) {
			nvgFillColor(args.vg, this->color);
			nvgFill(args.vg);
		}

		// Border
		if (this->borderColor.a > 0.0) {
			nvgStrokeWidth(args.vg, 0.5);
			nvgStrokeColor(args.vg, this->borderColor);
			nvgStroke(args.vg);
		}
	}
};


struct MyBlackScrew : app::SvgScrew {
	widget::TransformWidget* tw;

	MyBlackScrew() {
		fb->removeChild(sw);

		tw = new TransformWidget();
		tw->addChild(sw);
		fb->addChild(tw);

		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/Screw.svg")));

		tw->box.size = sw->box.size;
		box.size = tw->box.size;

		float angle = random::uniform() * M_PI;
		tw->identity();
		// Rotate SVG
		math::Vec center = sw->box.getCenter();
		tw->translate(center);
		tw->rotate(angle);
		tw->translate(center.neg());
	}
};

struct MyTrimpot : app::SvgKnob {
	MyTrimpot() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/Trimpot.svg")));
	}
};