#include "plugin.hpp"


namespace PilePoly {

enum RANGE {
	UNI_5V = 0,
	UNI_10V = 1,
	BI_5V = 2,
	BI_10V = 3,
	UNBOUNDED = 10
};

struct PilePolyModule : Module {
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
		INPUT_RESET_VOLT,
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
	simd::float_4 currentVoltage[4];
	/** [Stored to JSON] */
	RANGE range;

	dsp::TSchmittTrigger<simd::float_4> incTrigger[4];
	dsp::TSchmittTrigger<simd::float_4> decTrigger[4];
	dsp::SchmittTrigger resetTrigger;
	dsp::TExponentialSlewLimiter<simd::float_4> slewLimiter[4];

	PilePolyModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_SLEW, 0.f, 5.f, 0.f, "Slew limiting", "s");
		configParam(PARAM_STEP, 0.f, 5.f, 0.2f, "Stepsize", "V");
		onReset();
	}

	void onReset() override {
		Module::onReset();
		range = RANGE::UNI_10V;
		for (int i = 0; i < 4; i++) {
			currentVoltage[i] = 0.f;
			slewLimiter[i].reset();
		}
	}

	void process(const ProcessArgs& args) override {
		int c = std::min(inputs[INPUT_INC].getChannels(), inputs[INPUT_DEC].getChannels());

		float slew = inputs[INPUT_SLEW].isConnected() ? clamp(inputs[INPUT_SLEW].getVoltage(), 0.f, 5.f) : params[PARAM_SLEW].getValue();
		if (slew > 0.f) slew = (1.f / slew) * 10.f;

		simd::float_4 reset = resetTrigger.process(inputs[INPUT_RESET].getVoltage()) ? simd::float_4::mask() : 0.f;
		simd::float_4 step = params[PARAM_STEP].getValue();

		for (int i = 0; i < c / 4; i++) {
			// RESET-input
			simd::float_4 resetVoltage = inputs[INPUT_RESET_VOLT].getChannels() == 1 ? 
				inputs[INPUT_RESET_VOLT].getVoltage() : inputs[INPUT_RESET_VOLT].getVoltageSimd<simd::float_4>(i * 4);
			currentVoltage[i] = simd::ifelse(reset, resetVoltage, currentVoltage[i]);

			// INC-input
			simd::float_4 incTrig = incTrigger[i].process(inputs[INPUT_INC].getVoltageSimd<simd::float_4>(i * 4));
			currentVoltage[i] = simd::ifelse(incTrig, currentVoltage[i] + step, currentVoltage[i]);

			// DEC-input
			simd::float_4 decTrig = decTrigger[i].process(inputs[INPUT_DEC].getVoltageSimd<simd::float_4>(i * 4));
			currentVoltage[i] = simd::ifelse(decTrig, currentVoltage[i] - step, currentVoltage[i]);

			switch (range) {
				case RANGE::UNI_5V:
					currentVoltage[i] = clamp(currentVoltage[i], 0.f, 5.f);
					break;
				case RANGE::UNI_10V:
					currentVoltage[i] = clamp(currentVoltage[i], 0.f, 10.f);
					break;
				case RANGE::BI_5V:
					currentVoltage[i] = clamp(currentVoltage[i], -5.f, 5.f);
					break;
				case RANGE::BI_10V:
					currentVoltage[i] = clamp(currentVoltage[i], -10.f, 10.f);
					break;
				case RANGE::UNBOUNDED:
					break;
			}

			// SLEW-input
			slewLimiter[i].setRiseFall(slew, slew);

			simd::float_4 v = slewLimiter[i].process(args.sampleTime, currentVoltage[i]);
			outputs[OUTPUT].setVoltageSimd(v, i * 4);
		}

		outputs[OUTPUT].setChannels(c);
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "range", json_integer(range));

		json_t* currentVoltageJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_t* cJ = json_real(currentVoltage[i / 4][i % 4]);
			json_array_append_new(currentVoltageJ, cJ);
		}
		json_object_set_new(rootJ, "currentVoltage", currentVoltageJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		range = (RANGE)json_integer_value(json_object_get(rootJ, "range"));

		json_t* currentVoltageJ = json_object_get(rootJ, "currentVoltage");
		for (int i = 0; i < 16; i++) {
			json_t* cJ = json_array_get(currentVoltageJ, i);
			currentVoltage[i / 4][i % 4] = json_real_value(cJ);
			slewLimiter[i / 4].out[i % 4] = currentVoltage[i / 4][i % 4];
		}
	}
};


struct PilePolyWidget : ThemedModuleWidget<PilePolyModule> {
	PilePolyWidget(PilePolyModule* module)
		: ThemedModuleWidget<PilePolyModule>(module, "PilePoly") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 77.3f), module, PilePolyModule::INPUT_SLEW));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 101.9f), module, PilePolyModule::PARAM_SLEW));

		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 142.6f), module, PilePolyModule::PARAM_STEP));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 167.5f), module, PilePolyModule::INPUT_INC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 203.2f), module, PilePolyModule::INPUT_DEC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 247.6f), module, PilePolyModule::INPUT_RESET));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 283.5f), module, PilePolyModule::INPUT_RESET_VOLT));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.7f), module, PilePolyModule::OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<PilePolyModule>::appendContextMenu(menu);

		struct RangeMenuItem : MenuItem {
			PilePolyModule* module;
			RangeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct RangeItem : MenuItem {
					PilePolyModule* module;
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

} // namespace PilePoly

Model* modelPilePoly = createModel<PilePoly::PilePolyModule, PilePoly::PilePolyWidget>("PilePoly");