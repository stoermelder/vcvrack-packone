#include "plugin.hpp"
#include "FlowerTrig.hpp"

namespace Flower {

template < int STEPS, int PATTERNS, int PHRASES >
struct FlowerTrigModule : Module {
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
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_GATE,
		OUTPUT_TRIG,
		ENUMS(OUTPUT_STEP, STEPS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_STEP, STEPS * 3),
		ENUMS(LIGHT_EDIT, STEPS * 3),
		NUM_LIGHTS
	};

	typedef FlowerTrigModule<STEPS, PATTERNS, PHRASES> MODULE;

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	typedef FlowerTrig<MODULE, STEPS> SEQ;
	SEQ seq{this};

	/** [Stored to JSON] */
	bool randomizeInherit;
	/** [Stored to JSON] flags which targets should be randomized */
	FlowerProcessArgs::RandomizeFlags randomizeFlags;


	dsp::SchmittTrigger seqRandTrigger;
	dsp::ClockDivider lightDivider;

	FlowerProcessArgs argsProducer;
	FlowerProcessArgs argsConsumer;

	FlowerTrigModule() {
		leftExpander.consumerMessage = &argsConsumer;
		leftExpander.producerMessage = &argsProducer;

		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_RAND, 0.f, 1.f, 0.f, "Randomize sequence");

		configParam<TrigStepModeParamQuantity<MODULE>>(PARAM_STEPMODE, 0.f, 1.f, 0.f, "Mode");
		auto pq1 = dynamic_cast<TrigStepModeParamQuantity<MODULE>*>(paramQuantities[PARAM_STEPMODE]);
		pq1->module = this;

		configParam<TrigFlowerKnobParamQuantity<MODULE>>(PARAM_STEP_CENTER, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), 0.f);
		auto pq2 = dynamic_cast<TrigFlowerKnobParamQuantity<MODULE>*>(paramQuantities[PARAM_STEP_CENTER]);
		pq2->module = this;

		for (int i = 0; i < STEPS; i++) {
			configParam(PARAM_STEP + i, 0.f, 1.f, 0.1f, string::f("Step %i length", i + 1), "", 0.f);

			configParam<TrigStepButtonParamQuantity<MODULE, STEPS>>(PARAM_STEP_BUTTON + i, 0.f, 1.f, 0.f);
			auto pq2 = dynamic_cast<TrigStepButtonParamQuantity<MODULE, STEPS>*>(paramQuantities[PARAM_STEP_BUTTON + i]);
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

	void process(const ProcessArgs& args) override {
		Module* mr = rightExpander.module;
		if (!mr || !(mr->model->plugin->slug == "Stoermelder-P1") || !(mr->model->slug == "FlowerSeq" || mr->model->slug == "FlowerTrig")) return;

		auto seqArgs = reinterpret_cast<FlowerProcessArgs*>(mr->leftExpander.consumerMessage);

		if (leftExpander.module) {
			auto seqArgs1 = reinterpret_cast<FlowerProcessArgs*>(leftExpander.producerMessage);
			*seqArgs1 = *seqArgs;
			leftExpander.messageFlipRequested = true;
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
		randomizeFlags = FlowerProcessArgs::RandomizeFlags(json_string_value(json_object_get(rootJ, "randomizeFlags")));
		randomizeInherit = json_is_true(json_object_get(rootJ, "randomizeInherit"));
	}
};


struct FlowerTrigWidget : ThemedModuleWidget<FlowerTrigModule<16, 8, 8>> {
	typedef FlowerTrigModule<16, 8, 8> MODULE;

	FlowerTrigWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "FlowerTrig") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = -37.5f;

		addParam(createParamCentered<FlowerKnob>(Vec(o + 195.0f, 190.0f), module, MODULE::PARAM_STEP_CENTER));

		addParam(createParamCentered<TL1105>(Vec(o + 116.2f, 326.7f), module, MODULE::PARAM_RAND));
		addInput(createInputCentered<StoermelderPort>(Vec(o + 93.2f, 313.7f), module, MODULE::INPUT_RAND));

		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 318.9f, 291.9f), module, MODULE::OUTPUT_GATE));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 297.1f, 313.6f), module, MODULE::OUTPUT_TRIG));
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

		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 195.0f, 66.2f), module, MODULE::OUTPUT_STEP + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 242.2f, 75.6f), module, MODULE::OUTPUT_STEP + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 282.6f, 102.4f), module, MODULE::OUTPUT_STEP + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 309.4f, 142.6f), module, MODULE::OUTPUT_STEP + 3));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 318.8f, 190.0f), module, MODULE::OUTPUT_STEP + 4));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 309.4f, 237.4f), module, MODULE::OUTPUT_STEP + 5));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 282.6f, 277.6f), module, MODULE::OUTPUT_STEP + 6));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 242.2f, 304.4f), module, MODULE::OUTPUT_STEP + 7));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 195.0f, 313.8f), module, MODULE::OUTPUT_STEP + 8));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 147.6f, 304.4f), module, MODULE::OUTPUT_STEP + 9));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 107.4f, 277.6f), module, MODULE::OUTPUT_STEP + 10));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 80.6f, 237.4f), module, MODULE::OUTPUT_STEP + 11));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 71.2f, 190.0f), module, MODULE::OUTPUT_STEP + 12));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 80.6f, 142.6f), module, MODULE::OUTPUT_STEP + 13));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 107.4f, 102.4f), module, MODULE::OUTPUT_STEP + 14));
		addOutput(createOutputCentered<StoermelderPort>(Vec(o + 147.6f, 75.6f), module, MODULE::OUTPUT_STEP + 15));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct StepRandomizeMenuItem : MenuItem {
			MODULE* module;
			StepRandomizeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct InheritRandomizeItem : MenuItem {
					MODULE* module;
					void onAction(const event::Action& e) override {
						module->randomizeInherit ^= true;
					}
					void step() override {
						rightText = module->randomizeInherit ? "✔" : "";
						MenuItem::step();
					}
				};

				struct StepRandomizeItem : MenuItem {
					MODULE* module;
					int idx;
					void onAction(const event::Action& e) override {
						module->randomizeFlags.flip(idx);
					}
					void step() override {
						rightText = module->randomizeFlags.test(idx) ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<InheritRandomizeItem>(&MenuItem::text, "Trigger from master", &InheritRandomizeItem::module, module));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Steps"));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Value", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_VALUE));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Disabled", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_DISABLED));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Probability", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_PROB));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Ratchets", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_RATCHETS));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Attack", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_ATTACK));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Decay", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_DECAY));
				return menu;
			}
		}; // StepRandomizeMenuItem

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<StepRandomizeMenuItem>(&MenuItem::text, "RAND-port targets", &StepRandomizeMenuItem::module, module));
	}
};

} // namespace Flower

Model* modelFlowerTrig = createModel<Flower::FlowerTrigModule<16, 8, 8>, Flower::FlowerTrigWidget>("FlowerTrig");