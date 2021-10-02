#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Infix {

template <int CHANNELS>
struct InfixModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_POLY,
		ENUMS(INPUT_MONO, CHANNELS),
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_POLY,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_OUT, CHANNELS),
		NUM_LIGHTS
	};

	InfixModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_POLY, "Polyphonic");
		for (int i = 0; i < CHANNELS; i++) {
			configInput(INPUT_MONO + i, string::f("Channel %i replacement", i + 1));
		}
		configOutput(OUTPUT_POLY, "Polyphonic");
		lightDivider.setDivision(512);
		onReset();
	}

	/** [Stored to JSON] */
	int panelTheme = 0;

	dsp::ClockDivider lightDivider;

	void process(const ProcessArgs& args) override {
		int lastChannel = inputs[INPUT_POLY].getChannels();
		for (int c = 0; c < CHANNELS; c++) {
			float v = inputs[INPUT_POLY].getVoltage(c);
			if (inputs[INPUT_MONO + c].isConnected()) {
				lastChannel = std::max(lastChannel, c + 1);
				v = inputs[INPUT_MONO + c].getVoltage();
			}
			outputs[OUTPUT_POLY].setVoltage(v, c);
		}
		outputs[OUTPUT_POLY].setChannels(lastChannel);

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < CHANNELS; c++) {
				lights[LIGHT_OUT + c].setBrightness(lastChannel > c);
			}
		}
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};


struct InfixWidget : ThemedModuleWidget<InfixModule<16>> {
	typedef InfixModule<16> MODULE;
	InfixWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Infix") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(37.5f, 60.5f), module, MODULE::INPUT_POLY));

		for (int i = 0; i < 8; i++) {
			float o = i * 27.4f;
			addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(23.0f, 98.2f + o), module, MODULE::LIGHT_OUT + i));
			addInput(createInputCentered<StoermelderPort>(Vec(23.0f, 98.2f + o), module, MODULE::INPUT_MONO + i));
			addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(52.0f, 98.2f + o), module, MODULE::LIGHT_OUT + i + 8));
			addInput(createInputCentered<StoermelderPort>(Vec(52.0f, 98.2f + o), module, MODULE::INPUT_MONO + i + 8));
		}

		addOutput(createOutputCentered<StoermelderPort>(Vec(37.5f, 327.2f), module, MODULE::OUTPUT_POLY));
	}
};

struct InfixMicroWidget : ThemedModuleWidget<InfixModule<8>> {
	typedef InfixModule<8> MODULE;
	InfixMicroWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "InfixMicro") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 60.5f), module, MODULE::INPUT_POLY));

		for (int i = 0; i < 8; i++) {
			float o = i * 27.4f;
			addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(23.0f, 98.2f + o), module, MODULE::LIGHT_OUT + i));
			addInput(createInputCentered<StoermelderPort>(Vec(23.0f, 98.2f + o), module, MODULE::INPUT_MONO + i));
		}

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.2f), module, MODULE::OUTPUT_POLY));
	}
};

} // namespace Infix
} // namespace StoermelderPackOne

Model* modelInfix = createModel<StoermelderPackOne::Infix::InfixModule<16>, StoermelderPackOne::Infix::InfixWidget>("Infix");
Model* modelInfixMicro = createModel<StoermelderPackOne::Infix::InfixModule<8>, StoermelderPackOne::Infix::InfixMicroWidget>("InfixMicro");