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
		MODE_PARAM,
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
		ENUMS(PRESET_LIGHT, NUM_PRESETS * 3),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	std::string pluginSlug;
	/** [Stored to JSON] */
	std::string modelSlug;
	/** [Stored to JSON] */
	std::string moduleName;

	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS];
	/** [Stored to JSON] */
	json_t *presetSlot[NUM_PRESETS];

	/** [Stored to JSON] */
	int preset = 0;
	/** [Stored to JSON] */
	int presetCount = NUM_PRESETS;

	/** [Stored to JSON] mode for SEQ CV input, 0 = 0-10V, 1 = C4-G4, 2 = Trig */
	int seqCvMode = EIGHTFACE_SEQCVMODE_TRIG;

	int connected = 0;
	float modeLight = 0;

	EightFaceLongPressButton typeButtons[NUM_PRESETS];
	dsp::SchmittTrigger seqTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::ClockDivider lightDivider;


	EightFace() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MODE_PARAM, 0, 1, 0, "Read/write switch");
		for (int i = 0; i < NUM_PRESETS; i++) {
			configParam(PRESET_PARAM + i, 0, 1, 0, string::f("Preset slot %d", i + 1));
			presetSlotUsed[i] = false;
		}

		lightDivider.setDivision(512);
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
		presetCount = NUM_PRESETS;
		modelSlug = "";
		pluginSlug = "";
		moduleName = "";
		connected = 0;
	}

	void process(const ProcessArgs &args) override {
		if (leftExpander.moduleId >= 0 && leftExpander.module) {
			Module *t = leftExpander.module;
			bool c = modelSlug == "" || (t->model->name == modelSlug && t->model->plugin->name == pluginSlug);
			connected = c ? 2 : 1;

			if (connected == 2) {
				// RESET input
				if (seqCvMode == EIGHTFACE_SEQCVMODE_TRIG && inputs[RESET_INPUT].isConnected()) {
					if (resetTrigger.process(inputs[RESET_INPUT].getVoltage()))
						presetLoad(t, 0);
				}

				// SEQ input
				if (inputs[SEQ_INPUT].isConnected()) {
					switch (seqCvMode) {
						case EIGHTFACE_SEQCVMODE_10V:
							presetLoad(t, floor(rescale(inputs[SEQ_INPUT].getVoltage(), 0.f, 10.f, 0, presetCount)));
							break;
						case EIGHTFACE_SEQCVMODE_C4:
							presetLoad(t, round(clamp(inputs[SEQ_INPUT].getVoltage() * 12.f, 0.f, NUM_PRESETS - 1.f)));
							break;
						case EIGHTFACE_SEQCVMODE_TRIG:
							if (seqTrigger.process(inputs[SEQ_INPUT].getVoltage()))
								presetLoad(t, (preset + 1) % presetCount);
							break;
					}
				}

				// Buttons
				for (int i = 0; i < NUM_PRESETS; i++) {
					if (params[MODE_PARAM].getValue() == 0.f) {
						switch (typeButtons[i].step(params[PRESET_PARAM + i])) {
							default:
							case EightFaceLongPressButton::NO_PRESS:
								break;
							case EightFaceLongPressButton::SHORT_PRESS:
								presetLoad(t, i); break;
							case EightFaceLongPressButton::LONG_PRESS:
								presetSetCount(i + 1); break;
						}
					}
					else {
						switch (typeButtons[i].step(params[PRESET_PARAM + i])) {
							default:
							case EightFaceLongPressButton::NO_PRESS:
								break;
							case EightFaceLongPressButton::SHORT_PRESS:
								presetSave(t, i); break;
							case EightFaceLongPressButton::LONG_PRESS:
								presetClear(i); break;
						}
					}
				}
			}
		}
		else {
			connected = 0;
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			float s = args.sampleTime * lightDivider.getDivision();
			modeLight += 0.3f * s;
			if (modeLight > 1.f) modeLight = 0.f;

			lights[MODULE_LIGHT + 0].setSmoothBrightness(connected == 2 ? modeLight : 0.f, s);
			lights[MODULE_LIGHT + 1].setBrightness(connected == 1 ? 1.f : 0.f);

			for (int i = 0; i < NUM_PRESETS; i++) {
				if (params[MODE_PARAM].getValue() == 0.f) {
					lights[PRESET_LIGHT + i * 3 + 0].setBrightness(0.f);
					lights[PRESET_LIGHT + i * 3 + 1].setSmoothBrightness(preset != i && presetCount > i ? (presetSlotUsed[i] ? 1.f : 0.2f) : 0.f, s);
					lights[PRESET_LIGHT + i * 3 + 2].setSmoothBrightness(preset == i ? 1.f : 0.f, s);
				}
				else {
					lights[PRESET_LIGHT + i * 3 + 0].setBrightness(presetSlotUsed[i] ? 1.f : 0.f);
					lights[PRESET_LIGHT + i * 3 + 1].setBrightness(0.f);
					lights[PRESET_LIGHT + i * 3 + 2].setBrightness(0.f);
				}
			}
		}
	}

	void presetLoad(Module *m, int p) {
		preset = p;
		if (!presetSlotUsed[p]) return;
		ModuleWidget *mw = APP->scene->rack->getModule(m->id);
		mw->fromJson(presetSlot[p]);
	}

	void presetSave(Module *m, int p) {
		pluginSlug = m->model->plugin->name;
		modelSlug = m->model->name;
		moduleName = m->model->plugin->brand + " " + m->model->name;
		ModuleWidget *mw = APP->scene->rack->getModule(m->id);
		if (presetSlotUsed[p]) json_decref(presetSlot[p]);
		presetSlotUsed[p] = true;
		presetSlot[p] = mw->toJson();
	}

	void presetClear(int p) {
		if (presetSlotUsed[p]) json_decref(presetSlot[p]);
		presetSlotUsed[p] = false;
		bool empty = true;
		for (int i = 0; i < NUM_PRESETS; i++) empty = empty && !presetSlotUsed[i];
		if (empty) {
			pluginSlug = "";
			modelSlug = "";
			moduleName = "";
		}
	}

	void presetSetCount(int p) {
		if (preset >= p) preset = 0;
		presetCount = p;
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "pluginSlug", json_string(pluginSlug.c_str()));
		json_object_set_new(rootJ, "modelSlug", json_string(modelSlug.c_str()));
		json_object_set_new(rootJ, "moduleName", json_string(moduleName.c_str()));
		json_object_set_new(rootJ, "seqCvMode", json_integer(seqCvMode));
		json_object_set_new(rootJ, "preset", json_integer(preset));
		json_object_set_new(rootJ, "presetCount", json_integer(presetCount));

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
		moduleName = json_string_value(json_object_get(rootJ, "moduleName"));
		seqCvMode = json_integer_value(json_object_get(rootJ, "seqCvMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

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
		menu->addChild(construct<EightFaceSeqCvModeItem>(&MenuItem::text, "Trigger", &EightFaceSeqCvModeItem::module, module, &EightFaceSeqCvModeItem::seqCvMode, EIGHTFACE_SEQCVMODE_TRIG));
		menu->addChild(construct<EightFaceSeqCvModeItem>(&MenuItem::text, "0..10V", &EightFaceSeqCvModeItem::module, module, &EightFaceSeqCvModeItem::seqCvMode, EIGHTFACE_SEQCVMODE_10V));
		menu->addChild(construct<EightFaceSeqCvModeItem>(&MenuItem::text, "C4-G4", &EightFaceSeqCvModeItem::module, module, &EightFaceSeqCvModeItem::seqCvMode, EIGHTFACE_SEQCVMODE_C4));
		return menu;
	}
};


struct CKSSH : CKSS {
	CKSSH() {
		shadow->opacity = 0.0f;
		fb->removeChild(sw);

		TransformWidget *tw = new TransformWidget();
		tw->addChild(sw);
		fb->addChild(tw);

		Vec center = sw->box.getCenter();
		tw->translate(center);
		tw->rotate(M_PI/2.0f);
		tw->translate(Vec(center.y, sw->box.size.x).neg());

		tw->box.size = sw->box.size.flip();
		box.size = tw->box.size;
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

		addChild(createLightCentered<SmallLight<GreenRedLight>>(Vec(22.5f, 119.1f), module, EightFace::MODULE_LIGHT));

		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 140.0f), module, EightFace::PRESET_LIGHT + 0 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 163.6f), module, EightFace::PRESET_LIGHT + 1 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 187.1f), module, EightFace::PRESET_LIGHT + 2 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 210.6f), module, EightFace::PRESET_LIGHT + 3 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 234.2f), module, EightFace::PRESET_LIGHT + 4 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 257.7f), module, EightFace::PRESET_LIGHT + 5 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 281.3f), module, EightFace::PRESET_LIGHT + 6 * 3));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(13.2f, 304.8f), module, EightFace::PRESET_LIGHT + 7 * 3));

		addParam(createParamCentered<TL1105>(Vec(27.6f, 135.8f), module, EightFace::PRESET_PARAM + 0));
		addParam(createParamCentered<TL1105>(Vec(27.6f, 159.4f), module, EightFace::PRESET_PARAM + 1));
		addParam(createParamCentered<TL1105>(Vec(27.6f, 182.9f), module, EightFace::PRESET_PARAM + 2));
		addParam(createParamCentered<TL1105>(Vec(27.6f, 206.4f), module, EightFace::PRESET_PARAM + 3));
		addParam(createParamCentered<TL1105>(Vec(27.6f, 230.0f), module, EightFace::PRESET_PARAM + 4));
		addParam(createParamCentered<TL1105>(Vec(27.6f, 253.5f), module, EightFace::PRESET_PARAM + 5));
		addParam(createParamCentered<TL1105>(Vec(27.6f, 277.1f), module, EightFace::PRESET_PARAM + 6));
		addParam(createParamCentered<TL1105>(Vec(27.6f, 300.6f), module, EightFace::PRESET_PARAM + 7));

		addParam(createParamCentered<CKSSH>(Vec(22.5f, 333.0f), module, EightFace::MODE_PARAM));
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

		if (module->moduleName != "") {
			ui::MenuLabel *textLabel = new ui::MenuLabel;
			textLabel->text = "Configured for...";
			menu->addChild(textLabel);

			ui::MenuLabel *modelLabel = new ui::MenuLabel;
			modelLabel->text = module->moduleName;
			menu->addChild(modelLabel);
			menu->addChild(new MenuSeparator());
		}

		EightFaceSeqCvModeMenuItem *seqCvModeMenuItem = construct<EightFaceSeqCvModeMenuItem>(&MenuItem::text, "Port SEQ mode", &EightFaceSeqCvModeMenuItem::module, module);
		seqCvModeMenuItem->rightText = RIGHT_ARROW;
		menu->addChild(seqCvModeMenuItem);
	}
};


Model *modelEightFace = createModel<EightFace, EightFaceWidget>("EightFace");