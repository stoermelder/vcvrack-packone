#include "plugin.hpp"
#include <random>
#include <thread>

namespace FourRounds {

struct FourRoundsModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(ROUND1_INPUT, 16),
		TRIG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(ROUND2_OUTPUT, 8),
		ENUMS(ROUND3_OUTPUT, 4),
		ENUMS(ROUND4_OUTPUT, 2),
		WINNER_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(ROUND_LIGHT, (8 + 4 + 2 + 1) * 2),
		NUM_LIGHTS
	};

	const static int SIZE = 8 + 4 + 2 + 1;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist = std::uniform_int_distribution<int>(0, 1);

	/** [Stored to JSON] */
	int state[SIZE];

	dsp::SchmittTrigger trigTrigger;
	dsp::ClockDivider lightDivider;

	FourRoundsModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		for (int i = 0; i < SIZE; i++)
			state[i] = randDist(randGen);
	}

	void process(const ProcessArgs &args) override {
		if (trigTrigger.process(inputs[TRIG_INPUT].getVoltage())) {
			for (int i = 0; i < SIZE; i++) {
				state[i] = randDist(randGen);
			}
		}

		for (int i = 0; i < 8; i++) {
			float v = inputs[ROUND1_INPUT + i * 2 + state[i]].getVoltage();
			outputs[ROUND2_OUTPUT + i].setVoltage(v);
		}
		for (int i = 0; i < 4; i++) {
			float v = outputs[ROUND2_OUTPUT + i * 2 + state[8 + i]].getVoltage();
			outputs[ROUND3_OUTPUT + i].setVoltage(v);
		}
		for (int i = 0; i < 2; i++) {
			float v = outputs[ROUND3_OUTPUT + i * 2 + state[8 + 4 + i]].getVoltage();
			outputs[ROUND4_OUTPUT + i].setVoltage(v);
		}

		float v = outputs[ROUND3_OUTPUT + state[SIZE - 1]].getVoltage();
		outputs[WINNER_OUTPUT].setVoltage(v);

		if (lightDivider.process()) {
			for (int i = 0; i < SIZE; i++) {
				lights[ROUND_LIGHT + i * 2 + 0].setBrightness(state[i] == 0);
				lights[ROUND_LIGHT + i * 2 + 1].setBrightness(state[i] > 0);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_t* statesJ = json_array();
		for (int i = 0; i < SIZE; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "value", json_integer(state[i]));
			json_array_append_new(statesJ, presetJ);
		}
		json_object_set_new(rootJ, "state", statesJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* statesJ = json_object_get(rootJ, "state");
		json_t* stateJ;
		size_t stateIndex;
		json_array_foreach(statesJ, stateIndex, stateJ) {
			state[stateIndex] = json_integer_value(json_object_get(stateJ, "value"));
		}
	}
};


struct FourRoundsWidget : ModuleWidget {
	FourRoundsWidget(FourRoundsModule* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/FourRounds.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.6f, 324.2f), module,  FourRoundsModule::TRIG_INPUT));

		addInput(createInputCentered<StoermelderPort>(Vec(175.6f, 65.7f), module,  FourRoundsModule::ROUND1_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(222.9f, 85.3f), module,  FourRoundsModule::ROUND1_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(259.0f, 121.5f), module, FourRoundsModule::ROUND1_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(278.6f, 168.7f), module, FourRoundsModule::ROUND1_INPUT + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(278.6f, 219.8f), module, FourRoundsModule::ROUND1_INPUT + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(259.0f, 267.1f), module, FourRoundsModule::ROUND1_INPUT + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(222.9f, 303.2f), module, FourRoundsModule::ROUND1_INPUT + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(175.6f, 322.8f), module, FourRoundsModule::ROUND1_INPUT + 7));
		addInput(createInputCentered<StoermelderPort>(Vec(124.4f, 322.8f), module, FourRoundsModule::ROUND1_INPUT + 8));
		addInput(createInputCentered<StoermelderPort>(Vec(77.2f, 303.2f), module,  FourRoundsModule::ROUND1_INPUT + 9));
		addInput(createInputCentered<StoermelderPort>(Vec(41.0f, 267.1f), module,  FourRoundsModule::ROUND1_INPUT + 10));
		addInput(createInputCentered<StoermelderPort>(Vec(21.4f, 219.8f), module,  FourRoundsModule::ROUND1_INPUT + 11));
		addInput(createInputCentered<StoermelderPort>(Vec(21.4f, 168.7f), module,  FourRoundsModule::ROUND1_INPUT + 12));
		addInput(createInputCentered<StoermelderPort>(Vec(41.0f, 121.5f), module,  FourRoundsModule::ROUND1_INPUT + 13));
		addInput(createInputCentered<StoermelderPort>(Vec(77.2f, 85.3f), module,   FourRoundsModule::ROUND1_INPUT + 14));
		addInput(createInputCentered<StoermelderPort>(Vec(124.4f, 65.7f), module,  FourRoundsModule::ROUND1_INPUT + 15));

		addOutput(createOutputCentered<StoermelderPort>(Vec(187.1f, 104.8f), module, FourRoundsModule::ROUND2_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(239.5f, 157.2f), module, FourRoundsModule::ROUND2_OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(239.5f, 231.2f), module, FourRoundsModule::ROUND2_OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(187.1f, 283.8f), module, FourRoundsModule::ROUND2_OUTPUT + 3));
		addOutput(createOutputCentered<StoermelderPort>(Vec(112.9f, 283.8f), module, FourRoundsModule::ROUND2_OUTPUT + 4));
		addOutput(createOutputCentered<StoermelderPort>(Vec(60.5f, 231.2f), module,  FourRoundsModule::ROUND2_OUTPUT + 5));
		addOutput(createOutputCentered<StoermelderPort>(Vec(60.5f, 157.2f), module,  FourRoundsModule::ROUND2_OUTPUT + 6));
		addOutput(createOutputCentered<StoermelderPort>(Vec(112.9f, 104.8f), module, FourRoundsModule::ROUND2_OUTPUT + 7));

		addOutput(createOutputCentered<StoermelderPort>(Vec(193.4f, 150.9f), module, FourRoundsModule::ROUND3_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(193.4f, 237.7f), module, FourRoundsModule::ROUND3_OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(106.6f, 237.7f), module, FourRoundsModule::ROUND3_OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(106.6f, 150.9f), module, FourRoundsModule::ROUND3_OUTPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(178.2f, 194.3f), module, FourRoundsModule::ROUND4_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(121.8f, 194.3f), module, FourRoundsModule::ROUND4_OUTPUT + 1));

		addOutput(createOutputCentered<StoermelderPort>(Vec(150.0f, 160.5f), module, FourRoundsModule::WINNER_OUTPUT));

		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(178.6f, 83.9f), module,  FourRoundsModule::ROUND_LIGHT + 0));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(207.8f, 96.2f), module,  FourRoundsModule::ROUND_LIGHT + 1));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(248.0f, 136.5f), module, FourRoundsModule::ROUND_LIGHT + 2));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(260.3f, 166.1f), module, FourRoundsModule::ROUND_LIGHT + 3));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(260.3f, 222.2f), module, FourRoundsModule::ROUND_LIGHT + 4));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(248.0f, 251.7f), module, FourRoundsModule::ROUND_LIGHT + 5));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(207.8f, 292.2f), module, FourRoundsModule::ROUND_LIGHT + 6));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(178.6f, 304.5f), module, FourRoundsModule::ROUND_LIGHT + 7));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(122.0f, 304.5f), module, FourRoundsModule::ROUND_LIGHT + 8));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(92.4f, 292.2f), module,  FourRoundsModule::ROUND_LIGHT + 9));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(51.9f, 251.7f), module,  FourRoundsModule::ROUND_LIGHT + 10));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(39.6f, 222.2f), module,  FourRoundsModule::ROUND_LIGHT + 11));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(39.6f, 166.1f), module,  FourRoundsModule::ROUND_LIGHT + 12));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(51.9f, 136.5f), module,  FourRoundsModule::ROUND_LIGHT + 13));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(92.4f, 96.2f), module,   FourRoundsModule::ROUND_LIGHT + 14));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(122.0f, 83.9f), module,  FourRoundsModule::ROUND_LIGHT + 15));

		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(187.1f, 123.4f), module, FourRoundsModule::ROUND_LIGHT + 16 + 0));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(220.8f, 157.1f), module, FourRoundsModule::ROUND_LIGHT + 16 + 1));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(220.8f, 231.1f), module, FourRoundsModule::ROUND_LIGHT + 16 + 2));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(187.1f, 265.1f), module, FourRoundsModule::ROUND_LIGHT + 16 + 3));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(113.1f, 265.1f), module, FourRoundsModule::ROUND_LIGHT + 16 + 4));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(79.0f, 231.1f),  module, FourRoundsModule::ROUND_LIGHT + 16 + 5));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(79.0f, 157.1f),  module, FourRoundsModule::ROUND_LIGHT + 16 + 6));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(113.1f, 123.4f), module, FourRoundsModule::ROUND_LIGHT + 16 + 7));

		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(186.2f, 168.1f), module, FourRoundsModule::ROUND_LIGHT + 16 + 8 + 0));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(186.2f, 220.5f), module, FourRoundsModule::ROUND_LIGHT + 16 + 8 + 1));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(113.8f, 220.5f), module, FourRoundsModule::ROUND_LIGHT + 16 + 8 + 2));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(113.8f, 168.1f), module, FourRoundsModule::ROUND_LIGHT + 16 + 8 + 3));

		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(159.9f, 194.3f), module, FourRoundsModule::ROUND_LIGHT + 16 + 8 + 4 + 0));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(140.1f, 194.3f), module, FourRoundsModule::ROUND_LIGHT + 16 + 8 + 4 + 1));
	}

	/*
	void appendContextMenu(Menu *menu) override {
		FourRoundsModule* module = dynamic_cast<Infix*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/FourRounds.md");
                t.detach();
            }
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
	}
    */
};

} // namespace FourRounds

Model *modelFourRounds = createModel<FourRounds::FourRoundsModule, FourRounds::FourRoundsWidget>("FourRounds");