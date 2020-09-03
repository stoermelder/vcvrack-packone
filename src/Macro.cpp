#include "plugin.hpp"
#include "MapModuleBase.hpp"
#include "components/MapButton.hpp"
#include "components/VoltageLedDisplay.hpp"
#include "digital/ScaledMapParam.hpp"

namespace StoermelderPackOne {
namespace Macro {

static const int MAPS = 6;

struct MacroModule : CVMapModuleBase<MAPS> {
	enum ParamIds {
		ENUMS(PARAM_MAP, MAPS),
		PARAM_KNOB,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_MAP, 2 * MAPS),
		ENUMS(LIGHT_MAP_CV, 2 * MAPS),
		NUM_LIGHTS
	};

	/** [Stored to Json] */
	ScaledMapParam<float> scaleParam[MAPS];

	/** [Stored to JSON] */
	int panelTheme = 0;

	dsp::ClockDivider lightDivider;

	MacroModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_KNOB, 0.f, 1.f, 0.f, "Macro knob", "%", 0.f, 100.f);

		for (size_t i = 0; i < MAPS; i++) {
			MacroModule::configParam<MapParamQuantity<MacroModule>>(PARAM_MAP + i, 0.f, 1.f, 0.f, string::f("Map %i", i + 1));
			MapParamQuantity<MacroModule>* pq = dynamic_cast<MapParamQuantity<MacroModule>*>(paramQuantities[PARAM_MAP + i]);
			pq->module = this;
			pq->id = i;
			paramHandles[i].text = "MACRO";
			scaleParam[i].setAbsolutes(0.f, 1.f, std::numeric_limits<float>::infinity());
		}

		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		CVMapModuleBase<MAPS>::onReset();
		for (size_t i = 0; i < MAPS; i++) {
			scaleParam[i].reset();
		}
	}

	void process(const Module::ProcessArgs& args) override {
		float v = params[PARAM_KNOB].getValue();

		if (inputs[INPUT].isConnected()) {
			float v1 = inputs[INPUT].getVoltage();
			if (bipolarInput) v1 += 5.f;
			v = v * v1;
			v = rescale(v, 0.f, 10.f, 0.f, 1.f);
		}

		for (size_t i = 0; i < MAPS; i++) {
			ParamQuantity* paramQuantity = getParamQuantity(i);
			if (paramQuantity) {
				scaleParam[i].paramQuantity = paramQuantity;

				if (lockParameterChanges || lastValue[i] != v) {
					scaleParam[i].setValue(v);
					lastValue[i] = v;
				}

				scaleParam[i].process(args.sampleTime);
				scaleParam[i].getValue();
			}
		}

		if (lightDivider.process()) {
			for (int i = 0; i < MAPS; i++) {
				lights[LIGHT_MAP + i * 2].setBrightness(paramHandles[i].moduleId >= 0 && learningId != i ? 1.f : 0.f);
				lights[LIGHT_MAP + i * 2 + 1].setBrightness(learningId == i ? 1.f : 0.f);
			}
		}

		CVMapModuleBase<MAPS>::process(args);
	}

	void commitLearn() override {
		CVMapModuleBase<MAPS>::commitLearn();
		disableLearn(learningId);
	}

	float getCurrentVoltage() {
		return inputs[INPUT].getVoltage();
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<MAPS>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataToJsonMap(json_t* mapJ, int i) override {
		json_object_set_new(mapJ, "slew", json_real(scaleParam[i].getSlew()));
		json_object_set_new(mapJ, "min", json_real(scaleParam[i].getMin()));
		json_object_set_new(mapJ, "max", json_real(scaleParam[i].getMax()));
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<MAPS>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}

	void dataFromJsonMap(json_t* mapJ, int i) override {
		json_t* slewJ = json_object_get(mapJ, "slew");
		json_t* minJ = json_object_get(mapJ, "min");
		json_t* maxJ = json_object_get(mapJ, "max");
		if (slewJ) scaleParam[i].setSlew(json_real_value(slewJ));
		if (minJ) scaleParam[i].setMin(json_real_value(minJ));
		if (maxJ) scaleParam[i].setMax(json_real_value(maxJ));
	}
};


struct MacroButton : MapButton<MacroModule> {
	void appendContextMenu(Menu* menu) override {
		struct SlewSlider : ui::Slider {
			struct SlewQuantity : Quantity {
				const float SLEW_MIN = 0.f;
				const float SLEW_MAX = 5.f;
				ScaledMapParam<float>* p;
				void setValue(float value) override {
					value = clamp(value, SLEW_MIN, SLEW_MAX);
					p->setSlew(value);
				}
				float getValue() override {
					return p->getSlew();
				}
				float getDefaultValue() override {
					return 0.f;
				}
				std::string getLabel() override {
					return "Slew-limiting";
				}
				int getDisplayPrecision() override {
					return 2;
				}
				float getMaxValue() override {
					return SLEW_MAX;
				}
				float getMinValue() override {
					return SLEW_MIN;
				}
			}; // struct SlewQuantity

			SlewSlider(ScaledMapParam<float>* p) {
				box.size.x = 220.0f;
				quantity = construct<SlewQuantity>(&SlewQuantity::p, p);
			}
			~SlewSlider() {
				delete quantity;
			}
		}; // struct SlewSlider

		struct ScalingLabel : MenuLabel {
			ScaledMapParam<float>* p;
			void step() override {
				float min = p->getMin();
				float max = p->getMax();

				float f1 = rescale(p->absoluteMin, p->absoluteMin, p->absoluteMax, min, max);
				f1 = clamp(f1, 0.f, 1.f) * 100.f;
				float f2 = rescale(p->absoluteMax, p->absoluteMin, p->absoluteMax, min, max);
				f2 = clamp(f2, 0.f, 1.f) * 100.f;

				float g1 = rescale(0.f, min, max, p->absoluteMin, p->absoluteMax);
				g1 = clamp(g1, p->absoluteMin, p->absoluteMax);
				float g1a = g1 * 100.f;
				float g2 = rescale(1.f, min, max, p->absoluteMin, p->absoluteMax);
				g2 = clamp(g2, p->absoluteMin, p->absoluteMax);
				float g2a = g2 * 100.f;

				text = string::f("[%.1f%, %.1f%] " RIGHT_ARROW " [%.1f%, %.1f%]", g1a, g2a, f1, f2);
			}
		}; // struct ScalingLabel

		struct MinSlider : ui::Slider {
			struct MinQuantity : Quantity {
				ScaledMapParam<float>* p;
				void setValue(float value) override {
					value = clamp(value, -1.f, 2.f);
					p->setMin(value);
				}
				float getValue() override {
					return p->getMin();
				}
				float getDefaultValue() override {
					return 0.f;
				}
				float getMinValue() override {
					return -1.f;
				}
				float getMaxValue() override {
					return 2.f;
				}
				float getDisplayValue() override {
					return getValue() * 100;
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue / 100);
				}
				std::string getLabel() override {
					return "Low";
				}
				std::string getUnit() override {
					return "%";
				}
				int getDisplayPrecision() override {
					return 3;
				}
			}; // struct MinQuantity

			MinSlider(ScaledMapParam<float>* p) {
				box.size.x = 220.0f;
				quantity = construct<MinQuantity>(&MinQuantity::p, p);
			}
			~MinSlider() {
				delete quantity;
			}
		}; // struct MinSlider

		struct MaxSlider : ui::Slider {
			struct MaxQuantity : Quantity {
				ScaledMapParam<float>* p;
				void setValue(float value) override {
					value = clamp(value, -1.f, 2.f);
					p->setMax(value);
				}
				float getValue() override {
					return p->getMax();
				}
				float getDefaultValue() override {
					return 1.f;
				}
				float getMinValue() override {
					return -1.f;
				}
				float getMaxValue() override {
					return 2.f;
				}
				float getDisplayValue() override {
					return getValue() * 100;
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue / 100);
				}
				std::string getLabel() override {
					return "High";
				}
				std::string getUnit() override {
					return "%";
				}
				int getDisplayPrecision() override {
					return 3;
				}
			}; // struct MaxQuantity

			MaxSlider(ScaledMapParam<float>* p) {
				box.size.x = 220.0f;
				quantity = construct<MaxQuantity>(&MaxQuantity::p, p);
			}
			~MaxSlider() {
				delete quantity;
			}
		}; // struct MaxSlider

		menu->addChild(new MenuSeparator());
		menu->addChild(new SlewSlider(&module->scaleParam[id]));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
		menu->addChild(construct<ScalingLabel>(&ScalingLabel::p, &module->scaleParam[id]));
		menu->addChild(new MinSlider(&module->scaleParam[id]));
		menu->addChild(new MaxSlider(&module->scaleParam[id]));
	}
};

struct Macro4Widget : ThemedModuleWidget<MacroModule> {
	typedef MacroModule MODULE;
	Macro4Widget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Macro") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		VoltageLedDisplay<MODULE>* ledDisplay = createWidgetCentered<VoltageLedDisplay<MODULE>>(Vec(22.5f, 290.7f));
		ledDisplay->box.size = Vec(39.1f, 13.2f);
		ledDisplay->module = module;
		addChild(ledDisplay);

		float o = 28.8f;
		for (size_t i = 0; i < MAPS; i++) {
			MacroButton* button = createParamCentered<MacroButton>(Vec(22.5f, 60.3f + o * i), module, MODULE::PARAM_MAP + i);
			button->setModule(module);
			button->id = i;
			addParam(button);
			addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(22.5f, 60.3f + o * i), module, MODULE::LIGHT_MAP + i * 2));
		}

		addChild(createParamCentered<StoermelderSmallKnob>(Vec(22.5f, 242.5f), module, MODULE::PARAM_KNOB));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 327.9f), module, MODULE::INPUT));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct LockItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->lockParameterChanges ^= true;
			}
			void step() override {
				rightText = module->lockParameterChanges ? "Locked" : "Unlocked";
				MenuItem::step();
			}
		};

		struct UniBiItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->bipolarInput ^= true;
			}
			void step() override {
				rightText = module->bipolarInput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LockItem>(&MenuItem::text, "Parameter changes", &LockItem::module, module));
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Voltage range", &UniBiItem::module, module));
	}
};

} // namespace Macro
} // namespace StoermelderPackOne

Model* modelMacro = createModel<StoermelderPackOne::Macro::MacroModule, StoermelderPackOne::Macro::Macro4Widget>("Macro");