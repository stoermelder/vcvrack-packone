#include "plugin.hpp"


namespace Sail {

enum MODE {
	DIFF = 0,
	ABSOLUTE = 1
};

struct SailModule : Module {
	enum ParamIds {
		PARAM_SLEW,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_VALUE,
		INPUT_FINE,
		INPUT_SLEW,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_ACTIVE,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	MODE mode;
	/** [Stored to JSON] */
	bool valueFiltering;

	bool mod;
	float valueBase;
	float value[2];

	ParamQuantity* paramQuantity;
	ParamQuantity* paramQuantityPriv;

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;
	dsp::ExponentialFilter valueFilter;
	dsp::ExponentialSlewLimiter slewLimiter;

	SailModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_SLEW, 0.f, 5.f, 0.f, "Slew limiting", "s");
		processDivider.setDivision(64);
		lightDivider.setDivision(512);
		valueFilter.setTau(1 / 30.f);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		paramQuantity = NULL;
		mode = MODE::DIFF;
		valueFiltering = true;
		slewLimiter.reset();
	}

	void process(const ProcessArgs& args) override {
		if (processDivider.process()) {
			// Copy to second variable as paramQuantity might become NULL through the app thread
			if (paramQuantity != paramQuantityPriv) {
				valueBase = std::numeric_limits<float>::min();
				paramQuantityPriv = paramQuantity;
			}

			if (paramQuantityPriv && paramQuantityPriv->isBounded()) {
				value[1] = value[0]; // Previous value for delta-calculation
				value[0] = inputs[INPUT_VALUE].getVoltage();

				if (valueBase == std::numeric_limits<float>::min())
					valueBase = slewLimiter.out = valueFilter.out = value[0];
				if (valueFiltering)
					value[0] = valueFilter.process(args.sampleTime * processDivider.getDivision(), value[0]);

				float s = inputs[INPUT_SLEW].isConnected() ? clamp(inputs[INPUT_SLEW].getVoltage(), 0.f, 5.f) : params[PARAM_SLEW].getValue();
				if (s > 0.f) {
					s = (1.f / s) * 10.f;
					slewLimiter.setRiseFall(s, s);
					value[0] = slewLimiter.process(args.sampleTime * processDivider.getDivision(), value[0]);
				}

				switch (mode) {
					case MODE::DIFF: {
						float d = value[0] - value[1];
						if (valueBase != value[0] && d != 0.f) {
							bool m = mod || inputs[INPUT_FINE].getVoltage() >= 1.f;
							if (m) d /= 10.f;
							paramQuantityPriv->moveScaledValue(d / 10.f);
						}
						break;
					}
					case MODE::ABSOLUTE: {
						float d = value[0] - value[1];
						if (valueBase != value[0] && d != 0.f) {
							paramQuantityPriv->setScaledValue(value[0] / 10.f);
						}
						break;
					}
				}

				if (outputs[OUTPUT].isConnected()) {
					outputs[OUTPUT].setVoltage(paramQuantityPriv->getScaledValue() * 10.f);
				}
			}
		}

		if (lightDivider.process()) {
			lights[LIGHT_ACTIVE].setSmoothBrightness(paramQuantity ? 1.f : 0.f, args.sampleTime * lightDivider.getDivision());
		}
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "valueFiltering", json_boolean(valueFiltering));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mode = (MODE)json_integer_value(json_object_get(rootJ, "mode"));
		valueFiltering = json_boolean_value(json_object_get(rootJ, "valueFiltering"));
	}
};


struct SailWidget : ThemedModuleWidget<SailModule> {
	SailWidget(SailModule* module)
		: ThemedModuleWidget<SailModule>(module, "Sail") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(22.5f, 135.4f), module, SailModule::LIGHT_ACTIVE));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 172.2f), module, SailModule::INPUT_SLEW));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 196.8f), module, SailModule::PARAM_SLEW));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 239.2f), module, SailModule::INPUT_FINE));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 283.5f), module, SailModule::INPUT_VALUE));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.7f), module, SailModule::OUTPUT));
	}

	void step() override {
		ThemedModuleWidget<SailModule>::step();
		if (!module) return;

		Widget* w = APP->event->getHoveredWidget();
		if (!w) { module->paramQuantity = NULL; return; }
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) { module->paramQuantity = NULL; return; }
		ParamQuantity* q = p->paramQuantity;
		if (!q) { module->paramQuantity = NULL; return; }

		module->paramQuantity = q;
		module->mod = APP->window->getMods() & GLFW_MOD_SHIFT;
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<SailModule>::appendContextMenu(menu);

		struct ModeMenuItem : MenuItem {
			SailModule* module;
			ModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct ModeItem : MenuItem {
					SailModule* module;
					MODE mode;
					void onAction(const event::Action& e) override {
						module->mode = mode;
					}
					void step() override {
						rightText = module->mode == mode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<ModeItem>(&MenuItem::text, "Differential", &ModeItem::module, module, &ModeItem::mode, MODE::DIFF));
				menu->addChild(construct<ModeItem>(&MenuItem::text, "Absolute", &ModeItem::module, module, &ModeItem::mode, MODE::ABSOLUTE));
				return menu;
			}
		};

		struct ValueFilteringItem : MenuItem {
			SailModule* module;
			void onAction(const event::Action& e) override {
				module->valueFiltering ^= true;
			}
			void step() override {
				rightText = module->valueFiltering ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Mode", &ModeMenuItem::module, module));
		menu->addChild(construct<ValueFilteringItem>(&MenuItem::text, "Smoothing", &ValueFilteringItem::module, module));
	}
};

} // namespace Sail

Model* modelSail = createModel<Sail::SailModule, Sail::SailWidget>("Sail");