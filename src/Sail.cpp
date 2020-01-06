#include "plugin.hpp"


namespace Sail {

enum IN_MODE {
	DIFF = 0,
	ABSOLUTE = 1
};

enum OUT_MODE {
	REDUCED = 0,
	FULL = 1
};

struct SailModule : Module {
	enum ParamIds {
		PARAM_SLEW,
		PARAM_STEP,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_VALUE,
		INPUT_FINE,
		INPUT_SLEW,
		INPUT_INC,
		INPUT_DEC,
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
	IN_MODE inMode;
	/** [Stored to JSON] */
	OUT_MODE outMode;

	bool mod;
	float valueBase;
	float valueBaseOut;
	float value[2];

	float trig;

	ParamQuantity* paramQuantity;
	ParamQuantity* paramQuantityPriv;

	dsp::SchmittTrigger incTrigger;
	dsp::SchmittTrigger decTrigger;

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;
	dsp::ExponentialSlewLimiter slewLimiter;

	SailModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_SLEW, 0.f, 5.f, 0.f, "Slew limiting", "s");
		configParam(PARAM_STEP, 0.f, 2.f, 5.f, "Stepsize", "V");
		processDivider.setDivision(32);
		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		paramQuantity = NULL;
		inMode = IN_MODE::DIFF;
		outMode = OUT_MODE::REDUCED;
		trig = 0.f;
		slewLimiter.reset();
	}

	void process(const ProcessArgs& args) override {
		if (incTrigger.process(inputs[INPUT_INC].getVoltage())) {
			trig += params[PARAM_STEP].getValue();
		}
		if (decTrigger.process(inputs[INPUT_DEC].getVoltage())) {
			trig -= params[PARAM_STEP].getValue();
		}

		if (processDivider.process()) {
			// Copy to second variable as paramQuantity might become NULL through the app thread
			if (paramQuantity != paramQuantityPriv) {
				valueBase = valueBaseOut = std::numeric_limits<float>::min();
				paramQuantityPriv = paramQuantity;
				trig = (!inputs[INPUT_VALUE].isConnected() && paramQuantityPriv) ? (paramQuantityPriv->getScaledValue() * 10.f) : 0.f;
			}

			if (paramQuantityPriv && paramQuantityPriv->isBounded() && paramQuantityPriv->module != this) {
				value[1] = value[0]; // Previous value for delta-calculation
				value[0] = inputs[INPUT_VALUE].isConnected() ? inputs[INPUT_VALUE].getVoltage() : 0.f;
				value[0] = clamp(value[0] + trig, 0.f, 10.f);

				if (valueBase == std::numeric_limits<float>::min()) {
					valueBase = slewLimiter.out = value[0];
				}

				float s = inputs[INPUT_SLEW].isConnected() ? clamp(inputs[INPUT_SLEW].getVoltage(), 0.f, 5.f) : params[PARAM_SLEW].getValue();
				if (s > 0.f) {
					s = (1.f / s) * 10.f;
					slewLimiter.setRiseFall(s, s);
					value[0] = slewLimiter.process(args.sampleTime * processDivider.getDivision(), value[0]);
				}

				switch (inMode) {
					case IN_MODE::DIFF: {
						float d = value[0] - value[1];
						if (valueBase != value[0] && d != 0.f) {
							bool m = mod || inputs[INPUT_FINE].getVoltage() >= 1.f;
							if (m) d /= 10.f;
							paramQuantityPriv->moveScaledValue(d / 10.f);
							valueBaseOut = paramQuantityPriv->getScaledValue();
						}
						break;
					}
					case IN_MODE::ABSOLUTE: {
						float d = value[0] - value[1];
						if (valueBase != value[0] && d != 0.f) {
							paramQuantityPriv->setScaledValue(value[0] / 10.f);
							valueBaseOut = paramQuantityPriv->getScaledValue();
						}
						break;
					}
				}

				if (outputs[OUTPUT].isConnected()) {
					switch (outMode) {
						case OUT_MODE::REDUCED: {
							float v = paramQuantityPriv->getScaledValue();
							if (v != valueBaseOut) {
 								outputs[OUTPUT].setVoltage(v * 10.f);
							}
							break;
						}
						case OUT_MODE::FULL: {
							outputs[OUTPUT].setVoltage(paramQuantityPriv->getScaledValue() * 10.f);
							break;
						}
					}
				}
			}
		}

		if (lightDivider.process()) {
			bool active = paramQuantityPriv && paramQuantityPriv->isBounded() && paramQuantityPriv->module != this;
			lights[LIGHT_ACTIVE].setSmoothBrightness(active ? 1.f : 0.f, args.sampleTime * lightDivider.getDivision());
		}
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "inMode", json_integer(inMode));
		json_object_set_new(rootJ, "outMode", json_integer(outMode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		inMode = (IN_MODE)json_integer_value(json_object_get(rootJ, "inMode"));
		outMode = (OUT_MODE)json_integer_value(json_object_get(rootJ, "outMode"));
	}
};


struct SailWidget : ThemedModuleWidget<SailModule> {
	SailWidget(SailModule* module)
		: ThemedModuleWidget<SailModule>(module, "Sail") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 60.4f), module, SailModule::INPUT_SLEW));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 85.0f), module, SailModule::PARAM_SLEW));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 127.4f), module, SailModule::INPUT_FINE));

		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 170.f), module, SailModule::PARAM_STEP));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 194.9f), module, SailModule::INPUT_INC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 230.5f), module, SailModule::INPUT_DEC));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 274.8f), module, SailModule::INPUT_VALUE));
		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(22.5f, 297.f), module, SailModule::LIGHT_ACTIVE));
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

		struct InModeMenuItem : MenuItem {
			SailModule* module;
			InModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct InModeItem : MenuItem {
					SailModule* module;
					IN_MODE inMode;
					void onAction(const event::Action& e) override {
						module->inMode = inMode;
					}
					void step() override {
						rightText = module->inMode == inMode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<InModeItem>(&MenuItem::text, "Differential", &InModeItem::module, module, &InModeItem::inMode, IN_MODE::DIFF));
				menu->addChild(construct<InModeItem>(&MenuItem::text, "Absolute", &InModeItem::module, module, &InModeItem::inMode, IN_MODE::ABSOLUTE));
				return menu;
			}
		};

		struct OutModeMenuItem : MenuItem {
			SailModule* module;
			OutModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct OutModeItem : MenuItem {
					SailModule* module;
					OUT_MODE outMode;
					void onAction(const event::Action& e) override {
						module->outMode = outMode;
					}
					void step() override {
						rightText = module->outMode == outMode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Reduced", &OutModeItem::module, module, &OutModeItem::outMode, OUT_MODE::REDUCED));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Continuous", &OutModeItem::module, module, &OutModeItem::outMode, OUT_MODE::FULL));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<InModeMenuItem>(&MenuItem::text, "IN-mode", &InModeMenuItem::module, module));
		menu->addChild(construct<OutModeMenuItem>(&MenuItem::text, "OUT-mode", &OutModeMenuItem::module, module));
	}
};

} // namespace Sail

Model* modelSail = createModel<Sail::SailModule, Sail::SailWidget>("Sail");