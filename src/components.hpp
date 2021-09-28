#pragma once
#include "plugin.hpp"
#include "ui/ThemedModuleWidget.hpp"
#include "components/LedTextDisplay.hpp"


struct LongPressButton {
	enum Event {
		NO_PRESS,
		SHORT_PRESS,
		LONG_PRESS
	};

	Param* param;
	float pressedTime = 0.f;
	dsp::BooleanTrigger trigger;

	inline Event process(float sampleTime, float longPressThreshold = 1.f) {
		Event result = NO_PRESS;
		bool pressed = param->value > 0.f;
		if (pressed && pressedTime >= 0.f) {
			pressedTime += sampleTime;
			if (pressedTime >= longPressThreshold) {
				pressedTime = -longPressThreshold;
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


struct StoermelderBlackScrew : app::SvgScrew {
	widget::TransformWidget* tw;

	StoermelderBlackScrew() {
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


struct StoermelderPort : app::SvgPort {
	StoermelderPort() {
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Port.svg")));
		box.size = shadow->box.size = Vec(22.2f, 22.2f);
	}
};

template <typename TBase>
struct StoermelderPortLight : TBase {
	float size = 24.8f;

	StoermelderPortLight() {
		this->box.size = math::Vec(size, size);
	}

	void drawLight(const widget::Widget::DrawArgs& args) override {
		float radius = size / 2.0f;
		float radius2 = 22.2f / 2.0f;

		// Background
		if (TBase::bgColor.a > 0.0) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, radius, radius, radius);
			nvgCircle(args.vg, radius, radius, radius2);
			nvgPathWinding(args.vg, NVG_HOLE);	// Mark second circle as a hole.
			nvgFillColor(args.vg, TBase::bgColor);
			nvgFill(args.vg);
		}

		// Foreground
		if (TBase::color.a > 0.0) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, radius, radius, radius);
			nvgCircle(args.vg, radius, radius, radius2);
			nvgPathWinding(args.vg, NVG_HOLE);	// Mark second circle as a hole.
			nvgFillColor(args.vg, TBase::color);
			nvgFill(args.vg);
		}

		// Border
		if (TBase::borderColor.a > 0.0) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, radius, radius, radius);
			nvgStrokeWidth(args.vg, 0.5);
			nvgStrokeColor(args.vg, TBase::borderColor);
			nvgStroke(args.vg);
		}
	}

	void drawHalo(const widget::Widget::DrawArgs& args) override {
		float radius = size / 2.0f;
		float oradius = 2.5f * radius;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, radius - oradius, radius - oradius, 2 * oradius, 2 * oradius);

		NVGpaint paint;
		NVGcolor icol = color::mult(TBase::color, 0.07);
		NVGcolor ocol = nvgRGB(0, 0, 0);
		paint = nvgRadialGradient(args.vg, radius, radius, radius, oradius, icol, ocol);
		nvgFillPaint(args.vg, paint);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFill(args.vg);
	}
};


template < typename LIGHT = WhiteLight, int COLORS = 1>
struct PolyLedWidget : Widget {
	PolyLedWidget() {
		box.size = mm2px(Vec(6.f, 6.f));
	}

	void setModule(Module* module, int firstlightId) {
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 0)), module, firstlightId + COLORS * 0));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 0)), module, firstlightId + COLORS * 1));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 0)), module, firstlightId + COLORS * 2));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 0)), module, firstlightId + COLORS * 3));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 2)), module, firstlightId + COLORS * 4));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 2)), module, firstlightId + COLORS * 5));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 2)), module, firstlightId + COLORS * 6));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 2)), module, firstlightId + COLORS * 7));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 4)), module, firstlightId + COLORS * 8));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 4)), module, firstlightId + COLORS * 9));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 4)), module, firstlightId + COLORS * 10));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 4)), module, firstlightId + COLORS * 11));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 6)), module, firstlightId + COLORS * 12));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 6)), module, firstlightId + COLORS * 13));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 6)), module, firstlightId + COLORS * 14));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 6)), module, firstlightId + COLORS * 15));
	}
};


template < typename MODULE, int SCENE_MAX >
struct SceneLedDisplay : StoermelderPackOne::StoermelderLedDisplay {
	MODULE* module;

	void step() override {
		if (module) {
			text = string::f("%02d", module->sceneSelected + 1);
		} 
		else {
			text = "00";
		}
		StoermelderLedDisplay::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		StoermelderLedDisplay::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();

		struct SceneItem : MenuItem {
			MODULE* module;
			int scene;
			
			void onAction(const event::Action& e) override {
				module->sceneSet(scene);
			}

			void step() override {
				rightText = module->sceneSelected == scene ? "✔" : "";
				MenuItem::step();
			}
		};

		struct CopyMenuItem : MenuItem {
			MODULE* module;
			CopyMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct CopyItem : MenuItem {
					MODULE* module;
					int scene;
					
					void onAction(const event::Action& e) override {
						module->sceneCopy(scene);
					}
				};

				for (int i = 0; i < SCENE_MAX; i++) {
					menu->addChild(construct<CopyItem>(&MenuItem::text, string::f("%02u", i + 1), &CopyItem::module, module, &CopyItem::scene, i));
				}

				return menu;
			}
		};

		struct CountMenuItem : MenuItem {
			MODULE* module;
			CountMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct CountItem : MenuItem {
					MODULE* module;
					int count;
					
					void onAction(const event::Action& e) override {
						module->sceneSetCount(count);
					}

					void step() override {
						rightText = module->sceneCount == count ? "✔" : "";
						MenuItem::step();
					}
				};

				for (int i = 0; i < SCENE_MAX; i++) {
					menu->addChild(construct<CountItem>(&MenuItem::text, string::f("%02u", i + 1), &CountItem::module, module, &CountItem::count, i + 1));
				}

				return menu;
			}
		};

		struct ResetItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->sceneReset();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scene"));
		for (int i = 0; i < SCENE_MAX; i++) {
			menu->addChild(construct<SceneItem>(&MenuItem::text, string::f("%02u", i + 1), &SceneItem::module, module, &SceneItem::scene, i));
		}
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<CountMenuItem>(&MenuItem::text, "Count", &CountMenuItem::module, module));
		menu->addChild(construct<CopyMenuItem>(&MenuItem::text, "Copy to", &CopyMenuItem::module, module));
		menu->addChild(construct<ResetItem>(&MenuItem::text, "Reset", &ResetItem::module, module));
	}
};


struct TriggerParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		return ParamQuantity::getLabel();
	}
	std::string getLabel() override {
		return "";
	}
};

struct BufferedTriggerParamQuantity : TriggerParamQuantity {
	float buffer = false;
	void setValue(float value) override {
		if (value >= 1.f) buffer = true;
		TriggerParamQuantity::setValue(value);
	}
	void resetBuffer() {
		buffer = false;
	}
};


struct CKSSH : CKSS {
	CKSSH() {
		shadow->opacity = 0.0f;
		fb->removeChild(sw);

		TransformWidget* tw = new TransformWidget();
		tw->addChild(sw);
		fb->addChild(tw);

		Vec center = sw->box.getCenter();
		tw->translate(center);
		tw->rotate(M_PI/2.0f);
		tw->translate(Vec(center.y, sw->box.size.x).neg());

		tw->box.size = sw->box.size.flip();
		box.size = tw->box.size;
	}
};

struct CKSSThreeH : CKSSThree {
	CKSSThreeH() {
		shadow->opacity = 0.0f;
		fb->removeChild(sw);

		TransformWidget* tw = new TransformWidget();
		tw->addChild(sw);
		fb->addChild(tw);

		Vec center = sw->box.getCenter();
		tw->translate(center);
		tw->rotate(M_PI / 2.0f);
		// Why does this not work as expected?!
		tw->translate(Vec(center.y, sw->box.size.x + center.x + 1.3f).neg());

		tw->box.size = sw->box.size.flip();
		fb->box.size = tw->box.size;
		box.size = tw->box.size;
	}
};