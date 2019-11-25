#include "plugin.hpp"
#include <thread>


namespace Infix {

template < int CHANNELS >
struct InfixModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		POLY_INPUT,
		ENUMS(MONO_INPUT, CHANNELS),
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(MONO_LIGHT, CHANNELS),
		NUM_LIGHTS
	};

	InfixModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		lightDivider.setDivision(512);
		onReset();
	}

	dsp::ClockDivider lightDivider;

	void process(const ProcessArgs& args) override {
		int lastChannel = inputs[POLY_INPUT].getChannels();
		for (int c = 0; c < CHANNELS; c++) {
			float v = inputs[POLY_INPUT].getVoltage(c);
			if (inputs[MONO_INPUT + c].isConnected()) {
				lastChannel = std::max(lastChannel, c + 1);
				v = inputs[MONO_INPUT + c].getVoltage();
			}
			outputs[POLY_OUTPUT].setVoltage(v, c);
		}
		outputs[POLY_OUTPUT].setChannels(lastChannel);

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < CHANNELS; c++) {
				lights[MONO_LIGHT + c].setBrightness(lastChannel > c);
			}
		}
	}
};


struct Infix16Widget : ModuleWidget {
	Infix16Widget(InfixModule<16>* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Infix.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 74.4f), module, InfixModule<16>::POLY_INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(54.f, 74.4f), module, InfixModule<16>::POLY_OUTPUT));

		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 136.3f), module, InfixModule<16>::MONO_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 163.7f), module, InfixModule<16>::MONO_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 191.1f), module, InfixModule<16>::MONO_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 218.5f), module, InfixModule<16>::MONO_INPUT + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 245.8f), module, InfixModule<16>::MONO_INPUT + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 273.2f), module, InfixModule<16>::MONO_INPUT + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 300.6f), module, InfixModule<16>::MONO_INPUT + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(20.6f, 328.0f), module, InfixModule<16>::MONO_INPUT + 7));

		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 136.3f), module, InfixModule<16>::MONO_INPUT + 8));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 163.7f), module, InfixModule<16>::MONO_INPUT + 9));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 191.1f), module, InfixModule<16>::MONO_INPUT + 10));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 218.5f), module, InfixModule<16>::MONO_INPUT + 11));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 245.8f), module, InfixModule<16>::MONO_INPUT + 12));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 273.2f), module, InfixModule<16>::MONO_INPUT + 13));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 300.6f), module, InfixModule<16>::MONO_INPUT + 14));
		addInput(createInputCentered<StoermelderPort>(Vec(54.f, 328.0f), module, InfixModule<16>::MONO_INPUT + 15));
	}
	
	void appendContextMenu(Menu* menu) override {
		InfixModule<16>* module = dynamic_cast<InfixModule<16>*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Infix.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
	}
};

struct Infix8Widget : ModuleWidget {
	Infix8Widget(InfixModule<8>* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Infix8.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 60.5f), module, InfixModule<8>::POLY_INPUT));

		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 98.2f), module, InfixModule<8>::MONO_LIGHT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 98.2f), module, InfixModule<8>::MONO_INPUT + 0));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 125.6f), module, InfixModule<8>::MONO_LIGHT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 125.6f), module, InfixModule<8>::MONO_INPUT + 1));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 153.0f), module, InfixModule<8>::MONO_LIGHT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 153.0f), module, InfixModule<8>::MONO_INPUT + 2));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 180.4f), module, InfixModule<8>::MONO_LIGHT + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 180.4f), module, InfixModule<8>::MONO_INPUT + 3));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 207.7f), module, InfixModule<8>::MONO_LIGHT + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 207.7f), module, InfixModule<8>::MONO_INPUT + 4));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 235.1f), module, InfixModule<8>::MONO_LIGHT + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 235.1f), module, InfixModule<8>::MONO_INPUT + 5));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 262.5f), module, InfixModule<8>::MONO_LIGHT + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 262.5f), module, InfixModule<8>::MONO_INPUT + 6));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(22.5f, 289.9f), module, InfixModule<8>::MONO_LIGHT + 7));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 289.9f), module, InfixModule<8>::MONO_INPUT + 7));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.2f), module, InfixModule<8>::POLY_OUTPUT));
	}
	
	void appendContextMenu(Menu* menu) override {
		InfixModule<8>* module = dynamic_cast<InfixModule<8>*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Infix.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
	}
};

} // namespace Infix

Model* modelInfix16 = createModel<Infix::InfixModule<16>, Infix::Infix16Widget>("Infix");
Model* modelInfix8 = createModel<Infix::InfixModule<8>, Infix::Infix8Widget>("Infix8");