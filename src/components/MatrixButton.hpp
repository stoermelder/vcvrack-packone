#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

// Replacement for TGrayModuleLightWidget whose drawLayer applies NVG additive blending
// (NVG_ONE_MINUS_DST_COLOR, NVG_ONE) to the entire layer, causing colors to wash out
// towards white. This class draws the fill with NVG_SOURCE_OVER so each color renders
// at face value, then switches back to additive only for the halo so the glow effect
// is preserved without contributing to the fill wash-out.
template <typename TBase = app::ModuleLightWidget>
struct TSaturatedModuleLightWidget : TBase {
	void drawLayer(const widget::Widget::DrawArgs& args, int layer) override {
		if (layer == 1) {
			// Fill with normal alpha blend so the color is not additively boosted to white.
			nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);
			this->drawLight(args);
			// Halo uses additive blend so it glows softly without washing out the fill.
			nvgGlobalCompositeBlendFunc(args.vg, NVG_ONE_MINUS_DST_COLOR, NVG_ONE);
			this->drawHalo(args);
		}
		widget::Widget::drawLayer(args, layer);
	}
};
using SaturatedModuleLightWidget = TSaturatedModuleLightWidget<>;

template <typename TBase = SaturatedModuleLightWidget>
struct TSaturatedRedGreenBlueLight : TBase {
	TSaturatedRedGreenBlueLight() {
		this->addBaseColor(SCHEME_RED);
		this->addBaseColor(SCHEME_GREEN);
		this->addBaseColor(SCHEME_BLUE);
	}
};
using SaturatedRedGreenBlueLight = TSaturatedRedGreenBlueLight<>;

template < typename BASE, typename MODULE >
struct MatrixButtonLight : BASE {
	MatrixButtonLight() {
		this->box.size = math::Vec(26.5f, 26.5f);
	}

	void drawBackground(const widget::Widget::DrawArgs& args) override {
	}

	void drawLight(const Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.8f, 0.8f, this->box.size.x - 2 * 0.8f, this->box.size.y - 2 * 0.8f, 3.4f);

		//nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFillColor(args.vg, this->color);
		nvgFill(args.vg);
	}
};

// Saturated variant of MatrixButtonLight for modules that use SCHEME colors decomposed
// into R/G/B light channels. Standard Rack rendering screen-blends the SCHEME base colors
// (which are not pure primaries) causing wash-out for mixed colors like orange and yellow.
// This widget bypasses that by reading the raw per-channel brightness values directly and
// rendering with normal alpha blending. An additional low-opacity additive pass recreates
// the lit LED feel, and the outer halo is preserved by restoring additive blend for drawHalo.
template <typename MODULE>
struct SaturatedMatrixButtonLight : MatrixButtonLight<SaturatedRedGreenBlueLight, MODULE> {
	void drawLight(const Widget::DrawArgs& args) override {
		NVGcolor col;
		if (this->module) {
			float r = math::clamp(this->module->lights[this->firstLightId + 0].getBrightness(), 0.f, 1.f);
			float g = math::clamp(this->module->lights[this->firstLightId + 1].getBrightness(), 0.f, 1.f);
			float b = math::clamp(this->module->lights[this->firstLightId + 2].getBrightness(), 0.f, 1.f);
			float a = std::max({r, g, b});
			if (a <= 0.f) return;
			col = nvgRGBAf(r, g, b, a);
		}
		else {
			if (this->color.a <= 0.f) return;
			col = this->color;
		}

		// Pass 1: solid fill at true color (NVG_SOURCE_OVER set by TSaturatedModuleLightWidget).
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.8f, 0.8f, this->box.size.x - 2 * 0.8f, this->box.size.y - 2 * 0.8f, 3.4f);
		nvgFillColor(args.vg, col);
		nvgFill(args.vg);

		// Pass 2: additive inner glow at reduced opacity for the lit LED feel.
		nvgGlobalCompositeBlendFunc(args.vg, NVG_ONE_MINUS_DST_COLOR, NVG_ONE);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.8f, 0.8f, this->box.size.x - 2 * 0.8f, this->box.size.y - 2 * 0.8f, 3.4f);
		nvgFillColor(args.vg, nvgRGBAf(col.r, col.g, col.b, col.a * 0.35f));
		nvgFill(args.vg);
		nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);
	}
};

struct MatrixButton : app::SvgSwitch {
	MatrixButton() {
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton1.svg")));
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