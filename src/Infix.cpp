#include "plugin.hpp"
#include <thread>


namespace Infix {

template < int CHANNELS >
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
		ENUMS(LIGHT_IN, 16),
		NUM_LIGHTS
	};

	InfixModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
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
			int i = inputs[INPUT_POLY].getChannels();
			for (int c = 0; c < 16; c++) {
				lights[LIGHT_IN + c].setBrightness(i > c);
			}
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
	InfixWidget(InfixModule<16>* module)
		: ThemedModuleWidget<InfixModule<16>>(module, "Infix") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 61.3f), module, InfixModule<16>::INPUT_POLY));
		addOutput(createOutputCentered<StoermelderPort>(Vec(55.0f, 61.3f), module, InfixModule<16>::OUTPUT_POLY));

		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 136.3f), module, InfixModule<16>::INPUT_MONO + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 163.7f), module, InfixModule<16>::INPUT_MONO + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 191.1f), module, InfixModule<16>::INPUT_MONO + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 218.5f), module, InfixModule<16>::INPUT_MONO + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 245.8f), module, InfixModule<16>::INPUT_MONO + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 273.2f), module, InfixModule<16>::INPUT_MONO + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 300.6f), module, InfixModule<16>::INPUT_MONO + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(20.0f, 328.0f), module, InfixModule<16>::INPUT_MONO + 7));

		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 136.3f), module, InfixModule<16>::INPUT_MONO + 8));
		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 163.7f), module, InfixModule<16>::INPUT_MONO + 9));
		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 191.1f), module, InfixModule<16>::INPUT_MONO + 10));
		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 218.5f), module, InfixModule<16>::INPUT_MONO + 11));
		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 245.8f), module, InfixModule<16>::INPUT_MONO + 12));
		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 273.2f), module, InfixModule<16>::INPUT_MONO + 13));
		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 300.6f), module, InfixModule<16>::INPUT_MONO + 14));
		addInput(createInputCentered<StoermelderPort>(Vec(55.0f, 328.0f), module, InfixModule<16>::INPUT_MONO + 15));

		PolyLedWidget<WhiteLight>* w1 = createWidgetCentered<PolyLedWidget<WhiteLight>>(Vec(20.0f, 88.1f));
		w1->setModule(module, InfixModule<16>::LIGHT_IN);
		addChild(w1);

		PolyLedWidget<WhiteLight>* w2 = createWidgetCentered<PolyLedWidget<WhiteLight>>(Vec(55.0f, 88.1f));
		w2->setModule(module, InfixModule<16>::LIGHT_OUT);
		addChild(w2);
	}
};

struct InfixMicroWidget : ThemedModuleWidget<InfixModule<8>> {
	InfixMicroWidget(InfixModule<8>* module)
		: ThemedModuleWidget<InfixModule<8>>(module, "InfixMicro") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 60.5f), module, InfixModule<8>::INPUT_POLY));

		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 98.2f), module, InfixModule<8>::LIGHT_OUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 98.2f), module, InfixModule<8>::INPUT_MONO + 0));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 125.6f), module, InfixModule<8>::LIGHT_OUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 125.6f), module, InfixModule<8>::INPUT_MONO + 1));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 153.0f), module, InfixModule<8>::LIGHT_OUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 153.0f), module, InfixModule<8>::INPUT_MONO + 2));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 180.4f), module, InfixModule<8>::LIGHT_OUT + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 180.4f), module, InfixModule<8>::INPUT_MONO + 3));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 207.7f), module, InfixModule<8>::LIGHT_OUT + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 207.7f), module, InfixModule<8>::INPUT_MONO + 4));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 235.1f), module, InfixModule<8>::LIGHT_OUT + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 235.1f), module, InfixModule<8>::INPUT_MONO + 5));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 262.5f), module, InfixModule<8>::LIGHT_OUT + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 262.5f), module, InfixModule<8>::INPUT_MONO + 6));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 289.9f), module, InfixModule<8>::LIGHT_OUT + 7));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 289.9f), module, InfixModule<8>::INPUT_MONO + 7));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.2f), module, InfixModule<8>::OUTPUT_POLY));
	}
};

} // namespace Infix

Model* modelInfix = createModel<Infix::InfixModule<16>, Infix::InfixWidget>("Infix");
Model* modelInfixMicro = createModel<Infix::InfixModule<8>, Infix::InfixMicroWidget>("InfixMicro");