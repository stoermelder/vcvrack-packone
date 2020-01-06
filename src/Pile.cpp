#include "plugin.hpp"


namespace Pile {

enum RANGE {
	UNI_5V = 0,
	UNI_10V = 1,
	BI_5V = 2,
	BI_10V = 3,
	UNBOUNDED = 10
};

struct PileModule : Module {
	enum ParamIds {
		PARAM_SLEW,
		PARAM_STEP,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_SLEW,
		INPUT_INC,
		INPUT_DEC,
		INPUT_RESET,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	float currentVoltage;
	/** [Stored to JSON] */
	RANGE range;

	float lastResetVoltage;

	dsp::SchmittTrigger incTrigger;
	dsp::SchmittTrigger decTrigger;
	dsp::ExponentialSlewLimiter slewLimiter;

	dsp::ClockDivider processDivider;

	PileModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_SLEW, 0.f, 5.f, 0.f, "Slew limiting", "s");
		configParam(PARAM_STEP, 0.f, 5.f, 0.5f, "Stepsize", "V");
		processDivider.setDivision(32);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		currentVoltage = 0.f;
		range = RANGE::UNI_10V;
		lastResetVoltage = currentVoltage;
	}

	void process(const ProcessArgs& args) override {
		if (inputs[INPUT_RESET].isConnected() && lastResetVoltage != inputs[INPUT_RESET].getVoltage()) {
			currentVoltage = lastResetVoltage = inputs[INPUT_RESET].getVoltage();
		}

		if (incTrigger.process(inputs[INPUT_INC].getVoltage())) {
			currentVoltage += params[PARAM_STEP].getValue();
		}
		if (decTrigger.process(inputs[INPUT_DEC].getVoltage())) {
			currentVoltage -= params[PARAM_STEP].getValue();
		}
		
		switch (range) {
			case RANGE::UNI_5V:
				currentVoltage = clamp(currentVoltage, 0.f, 5.f);
				break;
			case RANGE::UNI_10V:
				currentVoltage = clamp(currentVoltage, 0.f, 10.f);
				break;
			case RANGE::BI_5V:
				currentVoltage = clamp(currentVoltage, -5.f, 5.f);
				break;
			case RANGE::BI_10V:
				currentVoltage = clamp(currentVoltage, -10.f, 10.f);
				break;
			case RANGE::UNBOUNDED:
				break;
		}

		if (processDivider.process()) {
			float s = inputs[INPUT_SLEW].isConnected() ? clamp(inputs[INPUT_SLEW].getVoltage(), 0.f, 5.f) : params[PARAM_SLEW].getValue();
			if (s > 0.f) s = (1.f / s) * 10.f;
			slewLimiter.setRiseFall(s, s);
		}

		float v = slewLimiter.process(args.sampleTime, currentVoltage);
		outputs[OUTPUT].setVoltage(v);
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "currentVoltage", json_real(currentVoltage));
		json_object_set_new(rootJ, "range", json_integer(range));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		currentVoltage = lastResetVoltage = json_real_value(json_object_get(rootJ, "currentVoltage"));
		range = (RANGE)json_integer_value(json_object_get(rootJ, "range"));
	}
};


struct VoltageLedDisplay : LedDisplayChoice {
	PileModule* module;

	VoltageLedDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		box.size = Vec(37.9f, 16.f);
		textOffset = Vec(1.8f, 11.5f);
	}

	void step() override {
		if (module) {
			text = string::f("%+06.2f", std::max(std::min(module->currentVoltage, 99.99f), -99.99f));
		} 
		LedDisplayChoice::step();
	}
};


struct PileWidget : ThemedModuleWidget<PileModule> {
	PileWidget(PileModule* module)
		: ThemedModuleWidget<PileModule>(module, "Pile") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		VoltageLedDisplay* ledDisplay = createWidgetCentered<VoltageLedDisplay>(Vec(22.5f, 43.0f));
		ledDisplay->module = module;
		addChild(ledDisplay);

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 112.9f), module, PileModule::INPUT_SLEW));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 137.5f), module, PileModule::PARAM_SLEW));

		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 178.2f), module, PileModule::PARAM_STEP));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 203.1f), module, PileModule::INPUT_INC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 238.8f), module, PileModule::INPUT_DEC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 283.5f), module, PileModule::INPUT_RESET));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.7f), module, PileModule::OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<PileModule>::appendContextMenu(menu);

		struct RangeMenuItem : MenuItem {
			PileModule* module;
			RangeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct RangeItem : MenuItem {
					PileModule* module;
					RANGE range;
					void onAction(const event::Action& e) override {
						module->range = range;
					}
					void step() override {
						rightText = module->range == range ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<RangeItem>(&MenuItem::text, "0..5V", &RangeItem::module, module, &RangeItem::range, RANGE::UNI_5V));
				menu->addChild(construct<RangeItem>(&MenuItem::text, "0..10V", &RangeItem::module, module, &RangeItem::range, RANGE::UNI_10V));
				menu->addChild(construct<RangeItem>(&MenuItem::text, "-5..5V", &RangeItem::module, module, &RangeItem::range, RANGE::BI_5V));
				menu->addChild(construct<RangeItem>(&MenuItem::text, "-10..10V", &RangeItem::module, module, &RangeItem::range, RANGE::BI_10V));
				menu->addChild(construct<RangeItem>(&MenuItem::text, "Unbounded", &RangeItem::module, module, &RangeItem::range, RANGE::UNBOUNDED));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<RangeMenuItem>(&MenuItem::text, "Voltage range", &RangeMenuItem::module, module));
	}
};

} // namespace Pile

Model* modelPile = createModel<Pile::PileModule, Pile::PileWidget>("Pile");