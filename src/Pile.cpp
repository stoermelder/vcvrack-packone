#include "plugin.hpp"


namespace Pile {

struct PileModule : Module {
	enum ParamIds {
		PARAM_STEP,
		NUM_PARAMS
	};
	enum InputIds {
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

	float lastResetVoltage;

	dsp::SchmittTrigger incTrigger;
	dsp::SchmittTrigger decTrigger;

	PileModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_STEP, 0.f, 2.f, 0.5f, "Stepsize", "V");
		onReset();
	}

	void onReset() override {
		Module::onReset();
		currentVoltage = 5.f;
		lastResetVoltage = currentVoltage;
	}

	void process(const ProcessArgs& args) override {
		if (inputs[INPUT_RESET].isConnected() && lastResetVoltage != inputs[INPUT_RESET].getVoltage()) {
			currentVoltage = lastResetVoltage = inputs[INPUT_RESET].getVoltage();
		}

		if (incTrigger.process(inputs[INPUT_INC].getVoltage())) {
			currentVoltage = clamp(currentVoltage + params[PARAM_STEP].getValue(), 0.f, 10.f);
		}
		if (decTrigger.process(inputs[INPUT_DEC].getVoltage())) {
			currentVoltage = clamp(currentVoltage - params[PARAM_STEP].getValue(), 0.f, 10.f);
		}
		
		outputs[OUTPUT].setVoltage(currentVoltage);
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "currentVoltage", json_real(currentVoltage));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		currentVoltage = lastResetVoltage = json_real_value(json_object_get(rootJ, "currentVoltage"));
	}
};


struct VoltageLedDisplay : LedDisplayChoice {
	PileModule* module;

	VoltageLedDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		box.size = Vec(31.4f, 16.f);
		textOffset = Vec(2.8f, 11.5f);
	}

	void step() override {
		if (module) {
			text = string::f("%05.2f", module->currentVoltage);
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

		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 134.7f), module, PileModule::PARAM_STEP));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 176.8f), module, PileModule::INPUT_INC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 221.1f), module, PileModule::INPUT_DEC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 265.4f), module, PileModule::INPUT_RESET));

		VoltageLedDisplay* ledDisplay = createWidgetCentered<VoltageLedDisplay>(Vec(22.5f, 292.3f));
		ledDisplay->module = module;
		addChild(ledDisplay);

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.7f), module, PileModule::OUTPUT));
	}
};

} // namespace Pile

Model* modelPile = createModel<Pile::PileModule, Pile::PileWidget>("Pile");