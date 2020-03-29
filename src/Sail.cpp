#include "plugin.hpp"


namespace Sail {

enum class IN_MODE {
	DIFF = 0,
	ABSOLUTE = 1
};

enum class OUT_MODE {
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

	const float FINE = 0.1f;

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	IN_MODE inMode;
	/** [Stored to JSON] */
	OUT_MODE outMode;

	bool fineMod;

	float inVoltBase;
	float inVoltTarget;
	float incdecTarget;

	float valueBaseOut;
	float valuePrevious;

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
		configParam(PARAM_STEP, 0.f, 2.f, 0.2f, "Stepsize", "%", 0.f, 10.f);
		processDivider.setDivision(32);
		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		paramQuantity = NULL;
		inMode = IN_MODE::DIFF;
		outMode = OUT_MODE::REDUCED;
		slewLimiter.reset();
	}

	void process(const ProcessArgs& args) override {
		if (incTrigger.process(inputs[INPUT_INC].getVoltage())) {
			float step = params[PARAM_STEP].getValue() / 10.f;
			if (fineMod || inputs[INPUT_FINE].getVoltage() >= 1.f) step *= FINE;
			incdecTarget += step;
		}
		if (decTrigger.process(inputs[INPUT_DEC].getVoltage())) {
			float step = params[PARAM_STEP].getValue() / 10.f;
			if (fineMod || inputs[INPUT_FINE].getVoltage() >= 1.f) step *= FINE;
			incdecTarget -= step;
		}

		if (processDivider.process()) {
			// Copy to second variable as paramQuantity might become NULL through the app thread
			if (paramQuantity != paramQuantityPriv) {
				paramQuantityPriv = paramQuantity;
				// Current parameter value
				valuePrevious = paramQuantityPriv ? paramQuantityPriv->getScaledValue() : 0.f;
				inVoltTarget = incdecTarget = slewLimiter.out = valuePrevious;
				inVoltBase = clamp(inputs[INPUT_VALUE].getVoltage() / 10.f, 0.f, 1.f);
			}

			if (paramQuantityPriv && paramQuantityPriv->isBounded() && paramQuantityPriv->module != this) {
				float valueNext = valuePrevious;

				if (inputs[INPUT_VALUE].isConnected()) {
					// IN-port
					float inVolt = clamp(inputs[INPUT_VALUE].getVoltage() / 10.f, 0.f, 1.f);
					switch (inMode) {
						case IN_MODE::DIFF: {
							// Change since last time
							float d1 = inVolt - inVoltBase;
							inVoltBase = inVolt;
							if (fineMod || inputs[INPUT_FINE].getVoltage() >= 1.f) d1 *= FINE;
							// Actual change of parameter after slew limiting
							float d2 = inVoltTarget - valuePrevious;
							// Reapply the sum of both
							valueNext = clamp(valuePrevious + d1 + d2, 0.f, 1.f);
							inVoltTarget = valueNext;
							break;
						}
						case IN_MODE::ABSOLUTE: {
							// Only move on input voltage change
							if (inVolt != inVoltBase) {
								valueNext = inVolt;
								// Detach when target value has been reached
								if (valuePrevious == inVolt) inVoltBase = inVolt;
							}
							break;
						}
					}
				}
				else {
					// INC/DEC-ports
					incdecTarget = clamp(incdecTarget, 0.f, 1.f);
					valueNext = incdecTarget;
				}

				// Apply slew limiting
				float slew = inputs[INPUT_SLEW].isConnected() ? clamp(inputs[INPUT_SLEW].getVoltage(), 0.f, 5.f) : params[PARAM_SLEW].getValue();
				if (slew > 0.f) {
					slew = (1.f / slew) * 10.f;
					slewLimiter.setRiseFall(slew, slew);
					valueNext = slewLimiter.process(args.sampleTime * processDivider.getDivision(), valueNext);
				}

				// Determine the relative change
				float delta = valueNext - valuePrevious;
				if (delta != 0.f) {
					paramQuantityPriv->moveScaledValue(delta);
					valueBaseOut = paramQuantityPriv->getScaledValue();
				}

				valuePrevious = valueNext;

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
		json_object_set_new(rootJ, "inMode", json_integer((int)inMode));
		json_object_set_new(rootJ, "outMode", json_integer((int)outMode));
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

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(22.5f, 38.0f), module, SailModule::LIGHT_ACTIVE));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 69.0f), module, SailModule::INPUT_FINE));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 113.3f), module, SailModule::INPUT_SLEW));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 137.9f), module, SailModule::PARAM_SLEW));

		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 178.6f), module, SailModule::PARAM_STEP));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 203.5f), module, SailModule::INPUT_INC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 239.2f), module, SailModule::INPUT_DEC));

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
		module->fineMod = APP->window->getMods() & GLFW_MOD_SHIFT;
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