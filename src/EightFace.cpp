#include "plugin.hpp"
#include <thread>

const int NUM_PRESETS = 8;

const int EIGHTFACE_SEQCVMODE_10V = 0;
const int EIGHTFACE_SEQCVMODE_C4 = 1;
const int EIGHTFACE_SEQCVMODE_TRIG = 2;


struct EightFaceLongPressButton {
	enum Events {
		NO_PRESS,
		SHORT_PRESS,
		LONG_PRESS
	};

	float pressedTime = 0.f;
	dsp::BooleanTrigger trigger;

	Events step(Param &param) {
		Events result = NO_PRESS;
		bool pressed = param.value > 0.f;
		if (pressed && pressedTime >= 0.f) {
			pressedTime += APP->engine->getSampleTime();
			if (pressedTime >= 1.f) {
				pressedTime = -1.f;
				result = LONG_PRESS;
			}
		}

		// Check if released
		if (trigger.process(!pressed)) {
			if (pressedTime >= 0.f) {
				result = SHORT_PRESS;
			}
			pressedTime = 0.f;
		}

		return result;
	}
};


struct EightFace : Module {
	enum ParamIds {
		ENUMS(PRESET_PARAM, NUM_PRESETS),
		NUM_PARAMS
	};
	enum InputIds {
		SEQ_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(MODULE_LIGHT, 2),
		ENUMS(PRESET_LIGHT, NUM_PRESETS),
		NUM_LIGHTS
	};

	std::string pluginSlug;
	std::string modelSlug;

	bool presetSlotUsed[NUM_PRESETS];
	json_t *presetSlot[NUM_PRESETS];

	int preset = 0;

    /** [Stored to JSON] mode for SEQ CV input, 0 = 0-10V, 1 = C4-G4, 2 = Trig */
    int seqCvMode = EIGHTFACE_SEQCVMODE_10V;

	int connected = 0;

	EightFaceLongPressButton typeButtons[NUM_PRESETS];
	dsp::SchmittTrigger seqTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::ClockDivider lightDivider;


	EightFace() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		lightDivider.setDivision(1024);

		for (int i = 0; i < NUM_PRESETS; i++)
			presetSlotUsed[i] = false;

		onReset();
	}

	~EightFace() {
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (presetSlotUsed[i])
				json_decref(presetSlot[i]);
		}
	}

	void onReset() override {
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (presetSlotUsed[i]) {
				json_decref(presetSlot[i]);
				presetSlot[i] = NULL;
			}
			presetSlotUsed[i] = false;
		}

		preset = -1;
		modelSlug = "";
		pluginSlug = "";
		connected = 0;
	}

	void process(const ProcessArgs &args) override {
		if (leftExpander.moduleId >= 0) {
			auto t = APP->engine->getModule(leftExpander.moduleId);
			bool m = modelSlug == "" || (t->model->name == modelSlug && t->model->plugin->name == pluginSlug);
			connected = m ? 2 : 1;

			if (connected == 2) {
				if (inputs[RESET_INPUT].isConnected()) {
					if (resetTrigger.process(inputs[RESET_INPUT].getVoltage()))
						preset = -1;
				}

				if (inputs[SEQ_INPUT].isConnected()) {
					switch (seqCvMode) {
						case EIGHTFACE_SEQCVMODE_10V:
							presetLoad(t, floor(rescale(inputs[SEQ_INPUT].getVoltage(), 0.f, 10.f, 0, NUM_PRESETS)));
							break;
						case EIGHTFACE_SEQCVMODE_C4:
							presetLoad(t, round(clamp(inputs[SEQ_INPUT].getVoltage() * 12.f, 0.f, NUM_PRESETS - 1.f)));
							break;
						case EIGHTFACE_SEQCVMODE_TRIG:
							if (seqTrigger.process(inputs[SEQ_INPUT].getVoltage()))
								presetLoad(t, (preset + 1) % NUM_PRESETS);
							break;
					}
				}

				// Buttons
				for (int i = 0; i < NUM_PRESETS; i++) {
					switch (typeButtons[i].step(params[PRESET_PARAM + i])) {
						default:
						case EightFaceLongPressButton::NO_PRESS: break;
						case EightFaceLongPressButton::SHORT_PRESS: presetLoad(t, i); break;
						case EightFaceLongPressButton::LONG_PRESS: presetSave(t, i); break;
					}
				}
			}
		}
		else {
			connected = 0;
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			lights[MODULE_LIGHT + 0].setBrightness(connected == 2 ? 1.f : 0.f);
			lights[MODULE_LIGHT + 1].setBrightness(connected == 1 ? 1.f : 0.f);

			for (int i = 0; i < NUM_PRESETS; i++) {
				lights[PRESET_LIGHT + i].setBrightness(presetSlotUsed[i] ? (preset == i ? 1.f : 0.4f) : 0.f);
			}
		}
	}

	void presetLoad(Module *m, int i) {
		preset = i;
		if (!presetSlotUsed[i]) return;
		ModuleWidget *mw = APP->scene->rack->getModule(m->id);
		mw->fromJson(presetSlot[i]);
	}

	void presetSave(Module *m, int i) {
		preset = i;
		pluginSlug = m->model->plugin->name;
		modelSlug = m->model->name;
		ModuleWidget *mw = APP->scene->rack->getModule(m->id);
		if (presetSlotUsed[i]) json_decref(presetSlot[i]);
		presetSlotUsed[i] = true;
		presetSlot[i] = mw->toJson();
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "pluginSlug", json_string(pluginSlug.c_str()));
		json_object_set_new(rootJ, "modelSlug", json_string(modelSlug.c_str()));
		json_object_set_new(rootJ, "seqCvMode", json_integer(seqCvMode));

		json_t *presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t *presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(presetSlotUsed[i]));
			if (presetSlotUsed[i]) {
				json_object_set(presetJ, "slot", presetSlot[i]);
			}
			json_array_append_new(presetsJ, presetJ);
		}
		json_object_set_new(rootJ, "presets", presetsJ);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		pluginSlug = json_string_value(json_object_get(rootJ, "pluginSlug"));
		modelSlug = json_string_value(json_object_get(rootJ, "modelSlug"));
		seqCvMode = json_integer_value(json_object_get(rootJ, "seqCvMode"));

		json_t *presetsJ = json_object_get(rootJ, "presets");

		json_t *presetJ;
		size_t presetIndex;
		json_array_foreach(presetsJ, presetIndex, presetJ) {
			presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			presetSlot[presetIndex] = json_deep_copy(json_object_get(presetJ, "slot"));
		}
	}
};


struct EightFaceSeqCvModeMenuItem : MenuItem {
	struct EightFaceSeqCvModeItem : MenuItem {
		EightFace *module;
		int seqCvMode;

		void onAction(const event::Action &e) override {
			module->seqCvMode = seqCvMode;
		}

		void step() override {
			rightText = module->seqCvMode == seqCvMode ? "✔" : "";
			MenuItem::step();
		}
	};

	EightFace *module;
	Menu *createChildMenu() override {
		Menu *menu = new Menu;
		menu->addChild(construct<EightFaceSeqCvModeItem>(&MenuItem::text, "0..10V", &EightFaceSeqCvModeItem::module, module, &EightFaceSeqCvModeItem::seqCvMode, EIGHTFACE_SEQCVMODE_10V));
		menu->addChild(construct<EightFaceSeqCvModeItem>(&MenuItem::text, "C4-G4", &EightFaceSeqCvModeItem::module, module, &EightFaceSeqCvModeItem::seqCvMode, EIGHTFACE_SEQCVMODE_C4));
		menu->addChild(construct<EightFaceSeqCvModeItem>(&MenuItem::text, "Trigger", &EightFaceSeqCvModeItem::module, module, &EightFaceSeqCvModeItem::seqCvMode, EIGHTFACE_SEQCVMODE_TRIG));
		return menu;
	}
};


struct EightFaceWidget : ModuleWidget {
	EightFaceWidget(EightFace *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/EightFace.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 58.9f), module, EightFace::SEQ_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 95.2f), module, EightFace::RESET_INPUT));

		addChild(createLightCentered<MediumLight<GreenRedLight>>(Vec(11.7f, 130.3f), module, EightFace::MODULE_LIGHT));

		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 154.7f), module, EightFace::PRESET_LIGHT + 0));
		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 179.2f), module, EightFace::PRESET_LIGHT + 1));
		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 203.7f), module, EightFace::PRESET_LIGHT + 2));
		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 228.2f), module, EightFace::PRESET_LIGHT + 3));
		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 252.7f), module, EightFace::PRESET_LIGHT + 4));
		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 277.2f), module, EightFace::PRESET_LIGHT + 5));
		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 301.7f), module, EightFace::PRESET_LIGHT + 6));
		addChild(createLightCentered<MediumLight<YellowLight>>(Vec(11.7f, 326.2f), module, EightFace::PRESET_LIGHT + 7));

		addParam(createParamCentered<TL1105>(Vec(30.1f, 154.7f), module, EightFace::PRESET_PARAM + 0));
		addParam(createParamCentered<TL1105>(Vec(30.1f, 179.2f), module, EightFace::PRESET_PARAM + 1));
		addParam(createParamCentered<TL1105>(Vec(30.1f, 203.7f), module, EightFace::PRESET_PARAM + 2));
		addParam(createParamCentered<TL1105>(Vec(30.1f, 228.2f), module, EightFace::PRESET_PARAM + 3));
		addParam(createParamCentered<TL1105>(Vec(30.1f, 252.7f), module, EightFace::PRESET_PARAM + 4));
		addParam(createParamCentered<TL1105>(Vec(30.1f, 277.2f), module, EightFace::PRESET_PARAM + 5));
		addParam(createParamCentered<TL1105>(Vec(30.1f, 301.7f), module, EightFace::PRESET_PARAM + 6));
		addParam(createParamCentered<TL1105>(Vec(30.1f, 326.2f), module, EightFace::PRESET_PARAM + 7));
	}

	
	void appendContextMenu(Menu *menu) override {
		EightFace *module = dynamic_cast<EightFace*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action &e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/EightFace.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());

		EightFaceSeqCvModeMenuItem *seqCvModeMenuItem = construct<EightFaceSeqCvModeMenuItem>(&MenuItem::text, "Port SEQ# mode", &EightFaceSeqCvModeMenuItem::module, module);
		seqCvModeMenuItem->rightText = RIGHT_ARROW;
		menu->addChild(seqCvModeMenuItem);
	}
};


Model *modelEightFace = createModel<EightFace, EightFaceWidget>("EightFace");