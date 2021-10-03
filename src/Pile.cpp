#include "plugin.hpp"
#include "components/Knobs.hpp"
#include "components/VoltageLedDisplay.hpp"

namespace StoermelderPackOne {
namespace Pile {

enum class RANGE {
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
		configInput(INPUT_SLEW, "Slew CV");
		configInput(INPUT_INC, "Increment"),
		configInput(INPUT_DEC, "Decrement");
		configInput(INPUT_RESET, "Reset");
		configOutput(OUTPUT, "Voltage");
		configParam(PARAM_SLEW, 0.f, 5.f, 0.f, "Slew limiting", "s");
		configParam(PARAM_STEP, 0.f, 5.f, 0.2f, "Stepsize", "V");
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

	float getCurrentVoltage() {
		return currentVoltage;
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "range", json_integer((int)range));
		json_object_set_new(rootJ, "currentVoltage", json_real(currentVoltage));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		range = (RANGE)json_integer_value(json_object_get(rootJ, "range"));
		currentVoltage = lastResetVoltage = json_real_value(json_object_get(rootJ, "currentVoltage"));
		slewLimiter.out = currentVoltage;
	}
};


struct PileWidget : ThemedModuleWidget<PileModule> {
	PileWidget(PileModule* module)
		: ThemedModuleWidget<PileModule>(module, "Pile") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		VoltageLedDisplay<PileModule>* ledDisplay = createWidgetCentered<VoltageLedDisplay<PileModule>>(Vec(22.5f, 43.0f));
		ledDisplay->box.size = Vec(39.1f, 13.2f);
		ledDisplay->module = module;
		addChild(ledDisplay);

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 113.3f), module, PileModule::INPUT_SLEW));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 137.9f), module, PileModule::PARAM_SLEW));

		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 178.6f), module, PileModule::PARAM_STEP));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 203.5f), module, PileModule::INPUT_INC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 239.2f), module, PileModule::INPUT_DEC));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 283.5f), module, PileModule::INPUT_RESET));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.7f), module, PileModule::OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<PileModule>::appendContextMenu(menu);

		menu->addChild(new MenuSeparator());
		menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem("Voltage range",
			{
				{ RANGE::UNI_5V, "0..5V" },
				{ RANGE::UNI_10V, "0..10V" },
				{ RANGE::BI_5V, "-5..5V" },
				{ RANGE::BI_10V, "-10..10V" },
				{ RANGE::UNBOUNDED, "Unbounded" }
			},
			&module->range
		));
	}
};

} // namespace Pile
} // namespace StoermelderPackOne

Model* modelPile = createModel<StoermelderPackOne::Pile::PileModule, StoermelderPackOne::Pile::PileWidget>("Pile");