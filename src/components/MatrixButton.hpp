#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {

template < typename BASE, typename MODULE >
struct MatrixButtonLight : BASE {
	MatrixButtonLight() {
		this->box.size = math::Vec(26.5f, 26.5f);
	}

	void drawLight(const Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.8f, 0.8f, this->box.size.x - 2 * 0.8f, this->box.size.y - 2 * 0.8f, 3.4f);

		//nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFillColor(args.vg, this->color);
		nvgFill(args.vg);
	}
};

struct MatrixButton : app::SvgSwitch {
	MatrixButton() {
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/MatrixButton.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/MatrixButton1.svg")));
		fb->removeChild(shadow);
		delete shadow;
	}
};

struct MatrixButtonParamQuantity : ParamQuantity {
	void setValue(float value) override {
		ParamQuantity::setValue(std::round(value));
	}
};

} // namespace StoermelderPackOne