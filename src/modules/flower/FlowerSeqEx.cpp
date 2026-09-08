#include "FlowerSeq.hpp"

namespace StoermelderPackOne {
namespace Flower {

template < int STEPS, int PATTERNS, int PHRASES >
struct FlowerSeqExModule : FlowerChainModule {
	enum ParamIds {
		PARAM_RAND,
		PARAM_STEPMODE,
		ENUMS(PARAM_STEP, STEPS),
		ENUMS(PARAM_STEP_BUTTON, STEPS),
		PARAM_STEP_CENTER,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_RAND,
		ENUMS(INPUT_STEP, STEPS),
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_CV,
		OUTPUT_AUX,
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_RAND,
		ENUMS(LIGHT_STEP, STEPS * 3),
		ENUMS(LIGHT_EDIT, STEPS * 3),
		NUM_LIGHTS
	};

	typedef FlowerSeqExModule<STEPS, PATTERNS, PHRASES> MODULE;

	/** [Stored to JSON] */
	int panelTheme = 0;

	typedef FlowerSeq<MODULE, STEPS> SEQ;
	SEQ seq{this};

	/** [Stored to JSON] */
	bool randomizeInherit;
	/** [Stored to JSON] flags which targets should be randomized */
	FlowerProcessArgs::RandomizeFlags randomizeFlags;


	dsp::SchmittTrigger seqRandTrigger;
	dsp::ClockDivider lightDivider;

	FlowerProcessArgs argsProducer;
	FlowerProcessArgs argsConsumer;

	FlowerSeqExModule() {
		rightExpander.consumerMessage = &argsConsumer;
		rightExpander.producerMessage = &argsProducer;

		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_RAND, 0.f, 1.f, 0.f, "Randomize sequence");

		configParam<SeqStepModeParamQuantity<MODULE>>(PARAM_STEPMODE, 0.f, 1.f, 0.f, "Mode");
		auto pq1 = dynamic_cast<SeqStepModeParamQuantity<MODULE>*>(paramQuantities[PARAM_STEPMODE]);
		pq1->module = this;

		configParam<SeqFlowerKnobParamQuantity<MODULE>>(PARAM_STEP_CENTER, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), 0.f);
		auto pq2 = dynamic_cast<SeqFlowerKnobParamQuantity<MODULE>*>(paramQuantities[PARAM_STEP_CENTER]);
		pq2->module = this;

		for (int i = 0; i < STEPS; i++) {
			configParam<SeqStepParamQuantity<MODULE>>(PARAM_STEP + i, 0.f, 1.f, 0.5f, string::f("Step %i", i + 1), "V");
			auto pq1 = dynamic_cast<SeqStepParamQuantity<MODULE>*>(paramQuantities[PARAM_STEP + i]);
			pq1->module = this;
			pq1->i = i;

			configParam<SeqStepButtonParamQuantity<MODULE, STEPS>>(PARAM_STEP_BUTTON + i, 0.f, 1.f, 0.f);
			auto pq2 = dynamic_cast<SeqStepButtonParamQuantity<MODULE, STEPS>*>(paramQuantities[PARAM_STEP_BUTTON + i]);
			pq2->module = this;
			pq2->i = i;
		}

		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		randomizeInherit = true;
		randomizeFlags.reset();
		randomizeFlags.set(0);
		seq.reset();
	}

	void resetOutputs() override {
		outputs[OUTPUT_CV].setVoltage(0.f);
		outputs[OUTPUT_AUX].setVoltage(0.f);
	}

	void process(const ProcessArgs& args) override {
		// A sibling removal elsewhere in the chain means our own left neighbor
		// may have changed too; resetOutputs() here, onExpanderChange() also
		// covers a change to our own immediate neighbor.
		if (consumeSiblingRemoved()) {
			resetOutputs();
		}

		Module* mr = leftExpander.module;
		if (!mr || !isFlowerSeqModel(mr->model)) {
			resetOutputs();
			return;
		}

		auto seqArgs = reinterpret_cast<FlowerProcessArgs*>(mr->rightExpander.consumerMessage);

		if (rightExpander.module) {
			auto seqArgs1 = reinterpret_cast<FlowerProcessArgs*>(rightExpander.producerMessage);
			*seqArgs1 = *seqArgs;
			rightExpander.messageFlipRequested = true;
		}

		seqArgs->randomizeFlagsSlave = randomizeFlags;
		if (!randomizeInherit) seqArgs->randTick = false;
		seq.process(*seqArgs);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		seq.dataToJson(rootJ);
		json_object_set_new(rootJ, "randomizeFlags", json_string(randomizeFlags.to_string().c_str()));
		json_object_set_new(rootJ, "randomizeInherit", json_boolean(randomizeInherit));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		seq.dataFromJson(rootJ);
		const char* randomizeFlagsJ = json_string_value(json_object_get(rootJ, "randomizeFlags"));
		if (randomizeFlagsJ) randomizeFlags = FlowerProcessArgs::RandomizeFlags(randomizeFlagsJ);
		randomizeInherit = json_is_true(json_object_get(rootJ, "randomizeInherit"));
	}
};



struct FlowerSeqExWidget : ThemedModuleWidget<FlowerSeqExModule<16, 8, 8>> {
	typedef FlowerSeqExModule<16, 8, 8> MODULE;

	FlowerSeqExWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "FlowerSeqEx") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = -37.5f;

		addParam(createParamCentered<FlowerKnob>(Vec(o + 195.0f, 190.0f), module, MODULE::PARAM_STEP_CENTER));

		addParam(createParamCentered<TL1105>(Vec(o + 116.2f, 326.7f), module, MODULE::PARAM_RAND));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 93.2f, 313.7f), module, MODULE::INPUT_RAND));

		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 318.9f, 291.9f), module, MODULE::OUTPUT_CV));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 297.1f, 313.6f), module, MODULE::OUTPUT_AUX));
		addParam(createParamCentered<TL1105>(Vec(o + 274.f, 326.7f), module, MODULE::PARAM_STEPMODE));

		// Edit leds
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 219.2f, 68.5f), module, MODULE::LIGHT_EDIT + 0 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 262.9f, 86.4f), module, MODULE::LIGHT_EDIT + 1 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 298.1f, 121.1f), module, MODULE::LIGHT_EDIT + 2 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 316.6f, 165.8f), module, MODULE::LIGHT_EDIT + 3 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 316.6f, 214.1f), module, MODULE::LIGHT_EDIT + 4 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 298.1f, 257.9f), module, MODULE::LIGHT_EDIT + 5 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 262.9f, 293.0f), module, MODULE::LIGHT_EDIT + 6 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 219.2f, 311.5f), module, MODULE::LIGHT_EDIT + 7 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 171.0f, 311.5f), module, MODULE::LIGHT_EDIT + 8 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 126.4f, 293.0f), module, MODULE::LIGHT_EDIT + 9 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 92.2f, 257.9f), module, MODULE::LIGHT_EDIT + 10 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 73.6f, 214.1f), module, MODULE::LIGHT_EDIT + 11 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 73.6f, 165.8f), module, MODULE::LIGHT_EDIT + 12 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 92.2f, 121.1f), module, MODULE::LIGHT_EDIT + 13 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 126.4f, 86.4f), module, MODULE::LIGHT_EDIT + 14 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(o + 171.0f, 68.5f), module, MODULE::LIGHT_EDIT + 15 * 3));

		// Steps
		addParam(createParamCentered<LEDButton>(Vec(o + 195.0f, 137.9f), module, MODULE::PARAM_STEP_BUTTON + 0));
		addParam(createParamCentered<LEDButton>(Vec(o + 214.9f, 141.9f), module, MODULE::PARAM_STEP_BUTTON + 1));
		addParam(createParamCentered<LEDButton>(Vec(o + 231.8f, 153.2f), module, MODULE::PARAM_STEP_BUTTON + 2));
		addParam(createParamCentered<LEDButton>(Vec(o + 243.0f, 169.7f), module, MODULE::PARAM_STEP_BUTTON + 3));
		addParam(createParamCentered<LEDButton>(Vec(o + 247.1f, 190.0f), module, MODULE::PARAM_STEP_BUTTON + 4));
		addParam(createParamCentered<LEDButton>(Vec(o + 243.1f, 209.9f), module, MODULE::PARAM_STEP_BUTTON + 5));
		addParam(createParamCentered<LEDButton>(Vec(o + 231.8f, 226.8f), module, MODULE::PARAM_STEP_BUTTON + 6));
		addParam(createParamCentered<LEDButton>(Vec(o + 214.9f, 238.1f), module, MODULE::PARAM_STEP_BUTTON + 7));
		addParam(createParamCentered<LEDButton>(Vec(o + 195.0f, 242.1f), module, MODULE::PARAM_STEP_BUTTON + 8));
		addParam(createParamCentered<LEDButton>(Vec(o + 175.1f, 238.1f), module, MODULE::PARAM_STEP_BUTTON + 9));
		addParam(createParamCentered<LEDButton>(Vec(o + 158.2f, 226.8f), module, MODULE::PARAM_STEP_BUTTON + 10));
		addParam(createParamCentered<LEDButton>(Vec(o + 146.9f, 209.9f), module, MODULE::PARAM_STEP_BUTTON + 11));
		addParam(createParamCentered<LEDButton>(Vec(o + 142.9f, 190.0f), module, MODULE::PARAM_STEP_BUTTON + 12));
		addParam(createParamCentered<LEDButton>(Vec(o + 146.9f, 169.7f), module, MODULE::PARAM_STEP_BUTTON + 13));
		addParam(createParamCentered<LEDButton>(Vec(o + 158.2f, 153.2f), module, MODULE::PARAM_STEP_BUTTON + 14));
		addParam(createParamCentered<LEDButton>(Vec(o + 175.1f, 141.9f), module, MODULE::PARAM_STEP_BUTTON + 15));

		addChild(createLightCentered<FlowerLight>(Vec(o + 195.0f, 137.9f), module, MODULE::LIGHT_STEP + 0 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 214.9f, 141.9f), module, MODULE::LIGHT_STEP + 1 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 231.8f, 153.2f), module, MODULE::LIGHT_STEP + 2 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 243.0f, 169.7f), module, MODULE::LIGHT_STEP + 3 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 247.1f, 190.0f), module, MODULE::LIGHT_STEP + 4 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 243.1f, 209.9f), module, MODULE::LIGHT_STEP + 5 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 231.8f, 226.8f), module, MODULE::LIGHT_STEP + 6 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 214.9f, 238.1f), module, MODULE::LIGHT_STEP + 7 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 195.0f, 242.1f), module, MODULE::LIGHT_STEP + 8 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 175.1f, 238.1f), module, MODULE::LIGHT_STEP + 9 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 158.2f, 226.8f), module, MODULE::LIGHT_STEP + 10 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 146.9f, 209.9f), module, MODULE::LIGHT_STEP + 11 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 142.9f, 190.0f), module, MODULE::LIGHT_STEP + 12 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 146.9f, 169.7f), module, MODULE::LIGHT_STEP + 13 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 158.2f, 153.2f), module, MODULE::LIGHT_STEP + 14 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(o + 175.1f, 141.9f), module, MODULE::LIGHT_STEP + 15 * 3));

		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 195.0f, 107.2f), module, MODULE::PARAM_STEP + 0));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 230.2f, 104.9f), module, MODULE::PARAM_STEP + 1));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 253.6f, 131.4f), module, MODULE::PARAM_STEP + 2));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 280.1f, 154.9f), module, MODULE::PARAM_STEP + 3));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 277.8f, 190.0f), module, MODULE::PARAM_STEP + 4));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 280.1f, 225.2f), module, MODULE::PARAM_STEP + 5));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 253.6f, 248.6f), module, MODULE::PARAM_STEP + 6));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 230.2f, 275.1f), module, MODULE::PARAM_STEP + 7));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 195.0f, 272.8f), module, MODULE::PARAM_STEP + 8));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 159.8f, 275.1f), module, MODULE::PARAM_STEP + 9));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 136.4f, 248.6f), module, MODULE::PARAM_STEP + 10));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 109.9f, 225.2f), module, MODULE::PARAM_STEP + 11));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 112.2f, 190.0f), module, MODULE::PARAM_STEP + 12));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 109.9f, 154.9f), module, MODULE::PARAM_STEP + 13));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 136.4f, 131.4f), module, MODULE::PARAM_STEP + 14));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(o + 159.8f, 104.9f), module, MODULE::PARAM_STEP + 15));

		addInput(createInputCentered<StoermelderPort>(Vec(o + 195.0f, 66.2f), module, MODULE::INPUT_STEP + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 242.2f, 75.6f), module, MODULE::INPUT_STEP + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 282.6f, 102.4f), module, MODULE::INPUT_STEP + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 309.4f, 142.6f), module, MODULE::INPUT_STEP + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 318.8f, 190.0f), module, MODULE::INPUT_STEP + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 309.4f, 237.4f), module, MODULE::INPUT_STEP + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 282.6f, 277.6f), module, MODULE::INPUT_STEP + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 242.2f, 304.4f), module, MODULE::INPUT_STEP + 7));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 195.0f, 313.8f), module, MODULE::INPUT_STEP + 8));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 147.6f, 304.4f), module, MODULE::INPUT_STEP + 9));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 107.4f, 277.6f), module, MODULE::INPUT_STEP + 10));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 80.6f, 237.4f), module, MODULE::INPUT_STEP + 11));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 71.2f, 190.0f), module, MODULE::INPUT_STEP + 12));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 80.6f, 142.6f), module, MODULE::INPUT_STEP + 13));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 107.4f, 102.4f), module, MODULE::INPUT_STEP + 14));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 147.6f, 75.6f), module, MODULE::INPUT_STEP + 15));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("RAND-port targets", "", [=](Menu* menu) {
			menu->addChild(createBoolPtrMenuItem("Trigger from master", "", &module->randomizeInherit));
			menu->addChild(new MenuSeparator());
			menu->addChild(createMenuLabel("Steps"));
			menu->addChild(createRandomizeFlagMenuItem("Value", &module->randomizeFlags, FlowerProcessArgs::STEP_VALUE));
			menu->addChild(createRandomizeFlagMenuItem("Disabled", &module->randomizeFlags, FlowerProcessArgs::STEP_DISABLED));
			menu->addChild(createRandomizeFlagMenuItem("Auxiliary value", &module->randomizeFlags, FlowerProcessArgs::STEP_AUX));
			menu->addChild(createRandomizeFlagMenuItem("Probability", &module->randomizeFlags, FlowerProcessArgs::STEP_PROB));
			menu->addChild(createRandomizeFlagMenuItem("Ratchets", &module->randomizeFlags, FlowerProcessArgs::STEP_RATCHETS));
			menu->addChild(createRandomizeFlagMenuItem("Slew", &module->randomizeFlags, FlowerProcessArgs::STEP_SLEW));
		}));

		appendFlowerSeqMenu(menu, module);
	}
};

} // namespace Flower
} // namespace StoermelderPackOne

Model* modelFlowerSeqEx = createModel<StoermelderPackOne::Flower::FlowerSeqExModule<16, 8, 8>, StoermelderPackOne::Flower::FlowerSeqExWidget>("FlowerSeqEx");