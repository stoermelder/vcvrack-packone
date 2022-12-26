#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {

struct MenuColorPicker : MenuEntry {
	NVGcolor* color;
	NVGcolor hslcolor;
	float h = 0.5f;
	float s = 1.f;
	float l = 0.5f;

	struct hGradient : OpaqueWidget {
		MenuColorPicker* picker;

		hGradient(MenuColorPicker* picker) {
			this->picker = picker;
		}

		void draw(const DrawArgs& args) override {
			nvgBeginPath(args.vg);
			float x = box.size.x - 4.f;
			float y = box.size.y - 4.f;
			nvgRoundedRect(args.vg, 2.f, 2.f, x, y, 3.f);

			for (int i = 0; i < 6; i++) {
				float x1 = float(i) * 1.f / 6.f;
				float x2 = (float(i) + 1.f) * 1.f / 6.f;

				nvgScissor(args.vg, x1 * box.size.x, 0.f, x2 * box.size.x + 0.3f, box.size.y);
				NVGpaint paint = nvgLinearGradient(args.vg, x1 * box.size.x, 0.f, x2 * box.size.x, 0.f, nvgHSL(x1, picker->s, picker->l), nvgHSL(x2, picker->s, picker->l));
				nvgFillPaint(args.vg, paint);
				nvgFill(args.vg);
				nvgResetScissor(args.vg);
			}
		}
	};

	struct hSlider : ui::Slider {
		struct hQuantity : Quantity {
			MenuColorPicker* picker;

			hQuantity(MenuColorPicker* picker) {
				this->picker = picker;
			}
			void setValue(float value) override {
				picker->h = clamp(value, 0.f, 1.f);
				picker->updateColor();
			}
			float getValue() override {
				return picker->h;
			}
			float getDefaultValue() override {
				return 0.f;
			}
			float getMinValue() override {
				return 0.f;
			}
			float getMaxValue() override {
				return 1.f;
			}
			float getDisplayValue() override {
				return getValue();
			}
			std::string getDisplayValueString() override {
				return string::f("%.2f", picker->h * 360.f);
			}
			void setDisplayValue(float displayValue) override {
				setValue(displayValue);
			}
			std::string getLabel() override {
				return "Hue";
			}
			std::string getUnit() override {
				return "°";
			}
		};

		hSlider(MenuColorPicker* picker) {
			quantity = new hQuantity(picker);
		}
		~hSlider() {
			delete quantity;
		}
	};

	struct sGradient : OpaqueWidget {
		MenuColorPicker* picker;

		sGradient(MenuColorPicker* picker) {
			this->picker = picker;
		}

		void draw(const DrawArgs& args) override {
			nvgBeginPath(args.vg);
			float x = box.size.x - 4.f;
			float y = box.size.y - 4.f;
			nvgRoundedRect(args.vg, 2.f, 2.f, x, y, 3.f);
			NVGpaint paint = nvgLinearGradient(args.vg, 0.f, 0.f, box.size.x, 0.f, nvgHSL(picker->h, 0.f, picker->l), nvgHSL(picker->h, 1.f, picker->l));
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
		}
	};

	struct sSlider : ui::Slider {
		struct sQuantity : Quantity {
			MenuColorPicker* picker;

			sQuantity(MenuColorPicker* picker) {
				this->picker = picker;
			}
			void setValue(float value) override {
				picker->s = clamp(value, 0.f, 1.f);
				picker->updateColor();
			}
			float getValue() override {
				return picker->s;
			}
			float getDefaultValue() override {
				return 1.f;
			}
			float getMinValue() override {
				return 0.f;
			}
			float getMaxValue() override {
				return 1.f;
			}
			float getDisplayValue() override {
				return getValue();
			}
			std::string getDisplayValueString() override {
				return string::f("%.2f", picker->s * 100.f);
			}
			void setDisplayValue(float displayValue) override {
				setValue(displayValue);
			}
			std::string getLabel() override {
				return "Saturation";
			}
			std::string getUnit() override {
				return "%";
			}
		};

		sSlider(MenuColorPicker* picker) {
			quantity = new sQuantity(picker);
		}
		~sSlider() {
			delete quantity;
		}
	};

	struct lGradient : OpaqueWidget {
		MenuColorPicker* picker;

		lGradient(MenuColorPicker* picker) {
			this->picker = picker;
		}

		void draw(const DrawArgs& args) override {
			nvgBeginPath(args.vg);
			float x = box.size.x - 4.f;
			float y = box.size.y - 4.f;
			nvgRoundedRect(args.vg, 2.f, 2.f, x, y, 3.f);
			NVGpaint paint = nvgLinearGradient(args.vg, 0.f, 0.f, box.size.x, 0.f, nvgHSL(picker->h, picker->s, 0.f), nvgHSL(picker->h, picker->s, 1.f));
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
		}
	};

	struct lSlider : ui::Slider {
		struct lQuantity : Quantity {
			MenuColorPicker* picker;

			lQuantity(MenuColorPicker* picker) {
				this->picker = picker;
			}
			void setValue(float value) override {
				picker->l = clamp(value, 0.f, 1.f);
				picker->updateColor();
			}
			float getValue() override {
				return picker->l;
			}
			float getDefaultValue() override {
				return 0.5f;
			}
			float getMinValue() override {
				return 0.f;
			}
			float getMaxValue() override {
				return 1.f;
			}
			float getDisplayValue() override {
				return getValue();
			}
			std::string getDisplayValueString() override {
				return string::f("%.2f", picker->l * 100.f);
			}
			void setDisplayValue(float displayValue) override {
				setValue(displayValue);
			}
			std::string getLabel() override {
				return "Lightness";
			}
			std::string getUnit() override {
				return "%";
			}
		};

		lSlider(MenuColorPicker* picker) {
			quantity = new lQuantity(picker);
		}
		~lSlider() {
			delete quantity;
		}
	};

	MenuColorPicker() {
		const float width = 280.f;
		const float pad = 4.0f;

		hGradient* hgradient = new hGradient(this);
		hgradient->box.size = Vec(width, 50.f);
		addChild(hgradient);

		hSlider* hslider = new hSlider(this);
		hslider->box.pos = hgradient->box.getBottomLeft() + Vec(pad, -BND_WIDGET_HEIGHT - pad);
		hslider->box.size = Vec(width - 2.f * pad, BND_WIDGET_HEIGHT);
		addChild(hslider);

		sGradient* sgradient = new sGradient(this);
		sgradient->box.pos = Vec(0.f, hgradient->box.getBottomLeft().y + 2.f);
		sgradient->box.size = Vec(width, 50.f);
		addChild(sgradient);

		sSlider* sslider = new sSlider(this);
		sslider->box.pos = sgradient->box.getBottomLeft() + Vec(pad, -BND_WIDGET_HEIGHT - pad);
		sslider->box.size = Vec(width - 2.f * pad, BND_WIDGET_HEIGHT);
		addChild(sslider);

		lGradient* lgradient = new lGradient(this);
		lgradient->box.pos = Vec(0.f, sgradient->box.getBottomLeft().y + 2.f);
		lgradient->box.size = Vec(width, 50.f);
		addChild(lgradient);

		lSlider* lslider = new lSlider(this);
		lslider->box.pos = lgradient->box.getBottomLeft() + Vec(pad, -BND_WIDGET_HEIGHT - pad);
		lslider->box.size = Vec(width - 2.f * pad, BND_WIDGET_HEIGHT);
		addChild(lslider);

		box.size = Vec(width, lgradient->box.getBottomLeft().y);
	}

	void draw(const DrawArgs& args) override {
		bndMenuLabel(args.vg, 0.0, 0.0, box.size.x, box.size.y, -1, "");
		OpaqueWidget::draw(args);
	}

	void step() override {
		if (!color::isEqual(*color, hslcolor)) {
			// color has been modified outside of this widget

			// Convert rgb to hsl
			// Find greatest and smallest channel values
			float cmin = std::min(color->r, std::min(color->g, color->b));
			float cmax = std::max(color->r, std::max(color->g, color->b));
			float delta = cmax - cmin;

			// Calculate hue
			// No difference
			if (delta == 0.f)
				h = 0.f;
			// Red is max
			else if (cmax == color->r)
				h = int((color->g - color->b) / delta) % 6;
			// Green is max
			else if (cmax == color->g)
				h = (color->b - color->r) / delta + 2.f;
			// Blue is max
			else
				h = (color->r - color->g) / delta + 4.f;

			h = round(h * 60.f);
			// Make negative hues positive behind 360°
			if (h < 0.f)
				h += 360.f;

			h /= 360.f;
			// Calculate lightness
			l = (cmax + cmin) / 2;
			// Calculate saturation
			s = delta == 0 ? 0 : delta / (1 - abs(2 * l - 1));

			hslcolor = *color;
		}
		OpaqueWidget::step();
	}

	void onAction(const ActionEvent& e) override {
		e.consume(this);
	}

	void updateColor() {
		*color = hslcolor = nvgHSL(h, s, l);
	}
}; // struct MenuColorPicker

} // namespace StoermelderPackOne