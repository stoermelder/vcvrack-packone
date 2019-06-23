#include "plugin.hpp"


struct Insert : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
        POLY_INPUT,
		ENUMS(MONO_INPUTS, 16),
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHANNEL_LIGHTS, 16),
		NUM_LIGHTS
	};

	int channels;

	Insert() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void process(const ProcessArgs &args) override {
		int lastChannel = inputs[POLY_INPUT].getChannels();
		for (int c = 0; c < 16; c++) {
			float v = inputs[POLY_INPUT].getVoltage(c);
			if (inputs[MONO_INPUTS + c].isConnected()) {
				lastChannel = std::max(lastChannel, c + 1);
				v = inputs[MONO_INPUTS + c].getVoltage();
			}
			outputs[POLY_OUTPUT].setVoltage(v, c);
		}
		outputs[POLY_OUTPUT].setChannels(lastChannel);
	}
};


struct InsertWidget : ModuleWidget {
	InsertWidget(Insert *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Insert.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 111.f), module, Insert::MONO_INPUTS + 0));
		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 143.f), module, Insert::MONO_INPUTS + 1));
		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 174.9f), module, Insert::MONO_INPUTS + 2));
		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 206.9f), module, Insert::MONO_INPUTS + 3));
		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 238.8f), module, Insert::MONO_INPUTS + 4));
		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 270.8f), module, Insert::MONO_INPUTS + 5));
		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 302.7f), module, Insert::MONO_INPUTS + 6));
		addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 334.7f), module, Insert::MONO_INPUTS + 7));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 111.f), module, Insert::MONO_INPUTS + 8));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 143.f), module, Insert::MONO_INPUTS + 9));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 174.9f), module, Insert::MONO_INPUTS + 10));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 206.9f), module, Insert::MONO_INPUTS + 11));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 238.8f), module, Insert::MONO_INPUTS + 12));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 270.8f), module, Insert::MONO_INPUTS + 13));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 302.7f), module, Insert::MONO_INPUTS + 14));
		addInput(createInputCentered<PJ301MPort>(Vec(54.f, 334.7f), module, Insert::MONO_INPUTS + 15));

        addInput(createInputCentered<PJ301MPort>(Vec(20.6f, 61.f), module, Insert::POLY_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(54.f, 61.f), module, Insert::POLY_OUTPUT));
	}
};


Model *modelInsert = createModel<Insert, InsertWidget>("Insert");