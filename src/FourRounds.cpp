#include "plugin.hpp"
#include <random>

namespace FourRounds {

enum MODE {
	DIRECT = 0,
	SH = 1,
	QUANTUM = 2
};

struct FourRoundsModule : Module {
	enum ParamIds {
		TRIG_PARAM,
		INV_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(ROUND1_INPUT, 16),
		TRIG_INPUT,
		INV_INPUT,
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
		ENUMS(ROUND_LIGHT, (8 + 4 + 2 + 1) * 6),
		NUM_LIGHTS
	};

	const static int SIZE = 8 + 4 + 2 + 1;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist = std::uniform_int_distribution<int>(0, 1);
	std::uniform_real_distribution<float> randFloatDist = std::uniform_real_distribution<float>(0, 1);

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	float state[SIZE];
	/** [Stored to JSON] */
	float lastValue[16];
	/** [Stored to JSON] */
	MODE mode = MODE::DIRECT;
	/** [Stored to JSON] */
	bool inverted = false;

	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger invTrigger;
	dsp::ClockDivider lightDivider;

	FourRoundsModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(TRIG_PARAM, 0.0f, 1.0f, 0.0f, "Trigger next contest");
		configParam<TriggerParamQuantity>(INV_PARAM, 0.0f, 1.0f, 0.0f, "Invert current state");
		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		for (int i = 0; i < SIZE; i++)
			state[i] = randDist(randGen);
		for (int i = 0; i < 16; i++)
			lastValue[i] = 0.f;
		mode = MODE::DIRECT;
		inverted = false;
	}

	void process(const ProcessArgs &args) override {
		if (trigTrigger.process(inputs[TRIG_INPUT].getVoltage() + params[TRIG_PARAM].getValue())) {
			switch (mode) {
				case MODE::DIRECT:
				case MODE::SH:
					for (int i = 0; i < SIZE; i++) {
						state[i] = randDist(randGen);
					}
					break;
				case MODE::QUANTUM:
					for (int i = 0; i < SIZE; i++) {
						state[i] = randFloatDist(randGen);
					}
					break;
			}
			for (int i = 0; i < 16; i++) {
				lastValue[i] = inputs[ROUND1_INPUT + i].getVoltage();
			}
		}

		if (invTrigger.process(inputs[INV_INPUT].getVoltage() + params[INV_PARAM].getValue())) {
			inverted ^= true;
		}

		for (int i = 0; i < 8; i++) {
			float v = getInputVoltage(i);
			outputs[ROUND2_OUTPUT + i].setVoltage(v);
		}
		for (int i = 0; i < 4; i++) {
			float v = getOutputVoltage(8, ROUND2_OUTPUT, i);
			outputs[ROUND3_OUTPUT + i].setVoltage(v);
		}
		for (int i = 0; i < 2; i++) {
			float v = getOutputVoltage(8 + 4, ROUND3_OUTPUT, i);
			outputs[ROUND4_OUTPUT + i].setVoltage(v);
		}

		float v = getOutputVoltage(SIZE - 1, ROUND4_OUTPUT, 0);
		outputs[WINNER_OUTPUT].setVoltage(v);

		if (lightDivider.process()) {
			switch (mode) {
				case MODE::DIRECT:
				case MODE::SH: {
					for (int i = 0; i < SIZE; i++) {
						lights[ROUND_LIGHT + i * 6 + 0].setBrightness(inverted ? state[i] == 1.f : 0.f);
						lights[ROUND_LIGHT + i * 6 + 1].setBrightness(inverted ? 0.f : state[i] == 0.f);
						lights[ROUND_LIGHT + i * 6 + 2].setBrightness(0.f);
						lights[ROUND_LIGHT + i * 6 + 3].setBrightness(inverted ? state[i] == 0.f : 0.f);
						lights[ROUND_LIGHT + i * 6 + 4].setBrightness(inverted ? 0.f : state[i] == 1.f);
						lights[ROUND_LIGHT + i * 6 + 5].setBrightness(0.f);
					}
					break;
				}
				case MODE::QUANTUM: {
					for (int i = 0; i < SIZE; i++) {
						float l1 = inverted ? state[i] : (1.f - state[i]);
						lights[ROUND_LIGHT + i * 6 + 0].setBrightness(l1);
						lights[ROUND_LIGHT + i * 6 + 1].setBrightness(l1);
						lights[ROUND_LIGHT + i * 6 + 2].setBrightness(l1);
						float l2 = inverted ? (1.f - state[i]) : state[i];
						lights[ROUND_LIGHT + i * 6 + 3].setBrightness(l2);
						lights[ROUND_LIGHT + i * 6 + 4].setBrightness(l2);
						lights[ROUND_LIGHT + i * 6 + 5].setBrightness(l2);
					}
					break;
				}
			}
		}
	}

	inline float getInputVoltage(int index) {
		switch (mode) {
			case MODE::DIRECT: {
				int s = (int(state[index]) + inverted) % 2;
				return inputs[ROUND1_INPUT + index * 2 + s].getVoltage();
			}
			case MODE::SH: {
				int s = (int(state[index]) + inverted) % 2;
				return lastValue[index * 2 + s];
			}
			case MODE::QUANTUM: {
				float v1 = inputs[ROUND1_INPUT + index * 2 + 0].getVoltage();
				float v2 = inputs[ROUND1_INPUT + index * 2 + 1].getVoltage();
				return !inverted ? (v1 * (1.f - state[index]) + v2 * state[index]) : (v1 * state[index] + v2 * (1.f - state[index]));
			}
			default: {
				return 0.f;
			}
		}
	}

	inline float getOutputVoltage(int state_offset, int output_offset, int index) {
		switch (mode) {
			case MODE::DIRECT:
			case MODE::SH: {
				int s = (int(state[state_offset + index]) + inverted) % 2;
				return outputs[output_offset + index * 2 + s].getVoltage();
			}
			case MODE::QUANTUM: {
				float v1 = outputs[output_offset + index * 2 + 0].getVoltage();
				float v2 = outputs[output_offset + index * 2 + 1].getVoltage();
				int i = state_offset + index;
				return !inverted ? (v1 * (1.f - state[i]) + v2 * state[i]) : (v1 * state[i] + v2 * (1.f - state[i]));
			}
			default: {
				return 0.f;
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* statesJ = json_array();
		for (int i = 0; i < SIZE; i++) {
			json_t* stateJ = json_object();
			json_object_set_new(stateJ, "value", json_real(state[i]));
			json_array_append_new(statesJ, stateJ);
		}
		json_object_set_new(rootJ, "state", statesJ);

		json_t* lastValuesJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_t* lastValueJ = json_object();
			json_object_set_new(lastValueJ, "value", json_real(lastValue[i]));
			json_array_append_new(lastValuesJ, lastValueJ);
		}
		json_object_set_new(rootJ, "lastValue", lastValuesJ);

		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "inverted", json_boolean(inverted));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* statesJ = json_object_get(rootJ, "state");
		json_t* stateJ;
		size_t stateIndex;
		json_array_foreach(statesJ, stateIndex, stateJ) {
			state[stateIndex] = json_real_value(json_object_get(stateJ, "value"));
		}

		json_t* lastValuesJ = json_object_get(rootJ, "lastValue");
		json_t* lastValueJ;
		size_t lastValueIndex;
		json_array_foreach(lastValuesJ, lastValueIndex, lastValueJ) {
			lastValue[lastValueIndex] = json_real_value(json_object_get(lastValueJ, "value"));
		}

		json_t* modeJ = json_object_get(rootJ, "mode");
		mode = (MODE)json_integer_value(modeJ);
		json_t* invertedJ = json_object_get(rootJ, "inverted");
		inverted = json_boolean_value(invertedJ);
	}
};


struct FourRoundsWidget : ThemedModuleWidget<FourRoundsModule> {
	FourRoundsWidget(FourRoundsModule* module)
		: ThemedModuleWidget<FourRoundsModule>(module, "FourRounds") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(23.1f, 327.8f), module, FourRoundsModule::TRIG_INPUT));
		addParam(createParamCentered<TL1105>(Vec(23.1f, 305.0f), module, FourRoundsModule::TRIG_PARAM));
		addInput(createInputCentered<StoermelderPort>(Vec(277.1f, 327.8f), module, FourRoundsModule::INV_INPUT));
		addParam(createParamCentered<TL1105>(Vec(277.1f, 305.0f), module, FourRoundsModule::INV_PARAM));

		addInput(createInputCentered<StoermelderPort>(Vec(175.6f, 52.1f), module,  FourRoundsModule::ROUND1_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(222.9f, 71.7f), module,  FourRoundsModule::ROUND1_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(259.0f, 107.8f), module, FourRoundsModule::ROUND1_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(278.6f, 155.1f), module, FourRoundsModule::ROUND1_INPUT + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(278.6f, 206.2f), module, FourRoundsModule::ROUND1_INPUT + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(259.0f, 253.5f), module, FourRoundsModule::ROUND1_INPUT + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(222.9f, 289.7f), module, FourRoundsModule::ROUND1_INPUT + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(175.6f, 309.2f), module, FourRoundsModule::ROUND1_INPUT + 7));
		addInput(createInputCentered<StoermelderPort>(Vec(124.4f, 309.2f), module, FourRoundsModule::ROUND1_INPUT + 8));
		addInput(createInputCentered<StoermelderPort>(Vec(77.2f, 289.7f), module,  FourRoundsModule::ROUND1_INPUT + 9));
		addInput(createInputCentered<StoermelderPort>(Vec(41.0f, 253.5f), module,  FourRoundsModule::ROUND1_INPUT + 10));
		addInput(createInputCentered<StoermelderPort>(Vec(21.4f, 206.2f), module,  FourRoundsModule::ROUND1_INPUT + 11));
		addInput(createInputCentered<StoermelderPort>(Vec(21.4f, 155.1f), module,  FourRoundsModule::ROUND1_INPUT + 12));
		addInput(createInputCentered<StoermelderPort>(Vec(41.0f, 107.8f), module,  FourRoundsModule::ROUND1_INPUT + 13));
		addInput(createInputCentered<StoermelderPort>(Vec(77.2f, 71.7f), module,   FourRoundsModule::ROUND1_INPUT + 14));
		addInput(createInputCentered<StoermelderPort>(Vec(124.4f, 52.1f), module,  FourRoundsModule::ROUND1_INPUT + 15));

		addOutput(createOutputCentered<StoermelderPort>(Vec(187.1f, 91.2f), module, FourRoundsModule::ROUND2_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(239.5f, 143.6f), module, FourRoundsModule::ROUND2_OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(239.5f, 217.7f), module, FourRoundsModule::ROUND2_OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(187.1f, 270.2f), module, FourRoundsModule::ROUND2_OUTPUT + 3));
		addOutput(createOutputCentered<StoermelderPort>(Vec(112.9f, 270.2f), module, FourRoundsModule::ROUND2_OUTPUT + 4));
		addOutput(createOutputCentered<StoermelderPort>(Vec(60.5f, 217.7f), module,  FourRoundsModule::ROUND2_OUTPUT + 5));
		addOutput(createOutputCentered<StoermelderPort>(Vec(60.5f, 143.6f), module,  FourRoundsModule::ROUND2_OUTPUT + 6));
		addOutput(createOutputCentered<StoermelderPort>(Vec(112.9f, 91.2f), module, FourRoundsModule::ROUND2_OUTPUT + 7));

		addOutput(createOutputCentered<StoermelderPort>(Vec(193.4f, 137.3f), module, FourRoundsModule::ROUND3_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(193.4f, 224.1f), module, FourRoundsModule::ROUND3_OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(106.6f, 224.1f), module, FourRoundsModule::ROUND3_OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(106.6f, 137.3f), module, FourRoundsModule::ROUND3_OUTPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(178.2f, 180.7f), module, FourRoundsModule::ROUND4_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(121.8f, 180.7f), module, FourRoundsModule::ROUND4_OUTPUT + 1));

		addOutput(createOutputCentered<StoermelderPort>(Vec(150.0f, 146.9f), module, FourRoundsModule::WINNER_OUTPUT));

		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(178.6f, 70.3f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 0));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(207.8f, 82.6f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 1));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(248.0f, 122.9f), module, FourRoundsModule::ROUND_LIGHT + 3 * 2));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(260.3f, 152.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(260.3f, 208.6f), module, FourRoundsModule::ROUND_LIGHT + 3 * 4));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(248.0f, 238.1f), module, FourRoundsModule::ROUND_LIGHT + 3 * 5));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(207.8f, 278.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * 6));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(178.6f, 290.8f), module, FourRoundsModule::ROUND_LIGHT + 3 * 7));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(122.0f, 290.8f), module, FourRoundsModule::ROUND_LIGHT + 3 * 8));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(92.4f, 278.5f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 9));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(51.9f, 238.1f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 10));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(39.6f, 208.6f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 11));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(39.6f, 152.5f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 12));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(51.9f, 122.9f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 13));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(92.4f, 82.6f), module,   FourRoundsModule::ROUND_LIGHT + 3 * 14));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(122.0f, 70.3f), module,  FourRoundsModule::ROUND_LIGHT + 3 * 15));

		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(187.1f, 109.8f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 0)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(220.8f, 143.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 1)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(220.8f, 217.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 2)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(187.1f, 251.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 3)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(113.1f, 251.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 4)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(79.0f, 217.5f),  module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 5)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(79.0f, 143.5f),  module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 6)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(113.1f, 109.8f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 7)));

		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(186.2f, 154.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 8 + 0)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(186.2f, 206.9f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 8 + 1)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(113.8f, 206.9f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 8 + 2)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(113.8f, 154.5f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 8 + 3)));

		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(159.9f, 180.6f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 8 + 4 + 0)));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(140.1f, 180.6f), module, FourRoundsModule::ROUND_LIGHT + 3 * (16 + 8 + 4 + 1)));
	}

	void appendContextMenu(Menu *menu) override {
		ThemedModuleWidget<FourRoundsModule>::appendContextMenu(menu);
		FourRoundsModule* module = dynamic_cast<FourRoundsModule*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Mode"));

		struct ModeItem : MenuItem {
			FourRoundsModule* module;
			MODE mode;
			
			void onAction(const event::Action &e) override {
				module->mode = mode;
			}

			void step() override {
				rightText = module->mode == mode ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<ModeItem>(&MenuItem::text, "CV / audio", &ModeItem::module, module, &ModeItem::mode, MODE::DIRECT));
		menu->addChild(construct<ModeItem>(&MenuItem::text, "Sample & hold", &ModeItem::module, module, &ModeItem::mode, MODE::SH));
		menu->addChild(construct<ModeItem>(&MenuItem::text, "Quantum", &ModeItem::module, module, &ModeItem::mode, MODE::QUANTUM));
	}
};

} // namespace FourRounds

Model *modelFourRounds = createModel<FourRounds::FourRoundsModule, FourRounds::FourRoundsWidget>("FourRounds");