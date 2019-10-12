#include "plugin.hpp"
#include <thread>

struct Infix : Module {
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

	Infix() {
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


struct InfixWidget : ModuleWidget {
	InfixWidget(Infix *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Infix.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 111.f), module, Infix::MONO_INPUTS + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 143.f), module, Infix::MONO_INPUTS + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 174.9f), module, Infix::MONO_INPUTS + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 206.9f), module, Infix::MONO_INPUTS + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 238.8f), module, Infix::MONO_INPUTS + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 270.8f), module, Infix::MONO_INPUTS + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 302.7f), module, Infix::MONO_INPUTS + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 334.7f), module, Infix::MONO_INPUTS + 7));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 111.f), module, Infix::MONO_INPUTS + 8));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 143.f), module, Infix::MONO_INPUTS + 9));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 174.9f), module, Infix::MONO_INPUTS + 10));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 206.9f), module, Infix::MONO_INPUTS + 11));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 238.8f), module, Infix::MONO_INPUTS + 12));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 270.8f), module, Infix::MONO_INPUTS + 13));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 302.7f), module, Infix::MONO_INPUTS + 14));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 334.7f), module, Infix::MONO_INPUTS + 15));

        addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 61.f), module, Infix::POLY_INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(54.f, 61.f), module, Infix::POLY_OUTPUT));
	}

	
	void appendContextMenu(Menu *menu) override {
		Infix *module = dynamic_cast<Infix*>(this->module);
		assert(module);

        struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Infix.md");
                t.detach();
            }
        };

        menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
  	}
};


Model *modelInfix = createModel<Infix, InfixWidget>("Infix");