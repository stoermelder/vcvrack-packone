#include "plugin.hpp"
#include "components.hpp"
#include <thread>

const int NUM_PRESETS = 8;

const int EIGHTFACE_SLOTCVMODE_TRIG = 2;
const int EIGHTFACE_SLOTCVMODE_10V = 0;
const int EIGHTFACE_SLOTCVMODE_C4 = 1;
const int EIGHTFACE_SLOTCVMODE_CLOCK = 3;


struct EightFace : Module {
	enum ParamIds {
		MODE_PARAM,
		ENUMS(PRESET_PARAM, NUM_PRESETS),
		NUM_PARAMS
	};
	enum InputIds {
		SLOT_INPUT,
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
	int slotCvMode = EIGHTFACE_SLOTCVMODE_TRIG;

	int connected = 0;
	int presetNext = -1;
	float modeLight = 0;

	LongPressButton typeButtons[NUM_PRESETS];
	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::ClockDivider lightDivider;


	EightFace() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MODE_PARAM, 0, 1, 0, "Switch Read/write mode");
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
		presetNext = -1;
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
				// Read mode
				if (params[MODE_PARAM].getValue() == 0.f) {
					// SEQ input
					if (inputs[SLOT_INPUT].isConnected()) {
						switch (slotCvMode) {
							case EIGHTFACE_SLOTCVMODE_10V:
								presetLoad(t, std::floor(rescale(inputs[SLOT_INPUT].getVoltage(), 0.f, 10.f, 0, presetCount)));
								break;
							case EIGHTFACE_SLOTCVMODE_C4:
								presetLoad(t, std::round(clamp(inputs[SLOT_INPUT].getVoltage() * 12.f, 0.f, NUM_PRESETS - 1.f)));
								break;
							case EIGHTFACE_SLOTCVMODE_TRIG:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, (preset + 1) % presetCount);
								break;
							case EIGHTFACE_SLOTCVMODE_CLOCK:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, presetNext);
								break;
						}
					}

					// RESET input
					if (slotCvMode == EIGHTFACE_SLOTCVMODE_TRIG && inputs[RESET_INPUT].isConnected()) {
						if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
							presetLoad(t, 0);
						}
					}

					// Buttons
					for (int i = 0; i < NUM_PRESETS; i++) {
						switch (typeButtons[i].step(params[PRESET_PARAM + i])) {
							default:
							case LongPressButton::NO_PRESS:
								break;
							case LongPressButton::SHORT_PRESS:
								presetLoad(t, i, slotCvMode == EIGHTFACE_SLOTCVMODE_CLOCK); break;
							case LongPressButton::LONG_PRESS:
								presetSetCount(i + 1); break;
						}
					}
				}
				// Write mode
				else {
					for (int i = 0; i < NUM_PRESETS; i++) {
						switch (typeButtons[i].step(params[PRESET_PARAM + i])) {
							default:
							case LongPressButton::NO_PRESS:
								break;
							case LongPressButton::SHORT_PRESS:
								presetSave(t, i); break;
							case LongPressButton::LONG_PRESS:
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
					lights[PRESET_LIGHT + i * 3 + 0].setBrightness(presetNext == i ? 1.f : 0.f);
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

	void presetLoad(Module *m, int p, bool isNext = false) {
		if (p < 0 || p >= presetCount)
			return;

		if (!isNext) {
			preset = p;
			presetNext = -1;
			if (!presetSlotUsed[p]) return;
			ModuleWidget *mw = APP->scene->rack->getModule(m->id);
			mw->fromJson(presetSlot[p]);
		}
		else {
			if (!presetSlotUsed[p]) return;
			presetNext = p;
		}
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
		if (presetSlotUsed[p]) 
			json_decref(presetSlot[p]);
		presetSlot[p] = NULL;
		presetSlotUsed[p] = false;
		if (preset == p) 
			preset = -1;
		bool empty = true;
		for (int i = 0; i < NUM_PRESETS; i++) 
			empty = empty && !presetSlotUsed[i];
		if (empty) {
			pluginSlug = "";
			modelSlug = "";
			moduleName = "";
		}
	}

	void presetSetCount(int p) {
		if (preset >= p) preset = 0;
		presetCount = p;
		presetNext = -1;
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "pluginSlug", json_string(pluginSlug.c_str()));
		json_object_set_new(rootJ, "modelSlug", json_string(modelSlug.c_str()));
		json_object_set_new(rootJ, "moduleName", json_string(moduleName.c_str()));
		json_object_set_new(rootJ, "slotCvMode", json_integer(slotCvMode));
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
		json_t *moduleNameJ = json_object_get(rootJ, "moduleName");
		if (moduleNameJ) moduleName = json_string_value(json_object_get(rootJ, "moduleName"));
		slotCvMode = json_integer_value(json_object_get(rootJ, "slotCvMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		json_t *presetsJ = json_object_get(rootJ, "presets");
		json_t *presetJ;
		size_t presetIndex;
		json_array_foreach(presetsJ, presetIndex, presetJ) {
			presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			presetSlot[presetIndex] = json_deep_copy(json_object_get(presetJ, "slot"));
		}

		if (preset >= presetCount) preset = 0;
	}
};


struct EightFaceSlotCvModeMenuItem : MenuItem {
	struct EightFaceSlotCvMenuItem : MenuItem {
		EightFace *module;
		int slotCvMode;

		void onAction(const event::Action &e) override {
			module->slotCvMode = slotCvMode;
		}

		void step() override {
			rightText = module->slotCvMode == slotCvMode ? "✔" : "";
			MenuItem::step();
		}
	};

	EightFace *module;
	Menu *createChildMenu() override {
		Menu *menu = new Menu;
		menu->addChild(construct<EightFaceSlotCvMenuItem>(&MenuItem::text, "Seq Trigger", &EightFaceSlotCvMenuItem::module, module, &EightFaceSlotCvMenuItem::slotCvMode, EIGHTFACE_SLOTCVMODE_TRIG));
		menu->addChild(construct<EightFaceSlotCvMenuItem>(&MenuItem::text, "Seq 0..10V", &EightFaceSlotCvMenuItem::module, module, &EightFaceSlotCvMenuItem::slotCvMode, EIGHTFACE_SLOTCVMODE_10V));
		menu->addChild(construct<EightFaceSlotCvMenuItem>(&MenuItem::text, "Seq C4-G4", &EightFaceSlotCvMenuItem::module, module, &EightFaceSlotCvMenuItem::slotCvMode, EIGHTFACE_SLOTCVMODE_C4));
		menu->addChild(construct<EightFaceSlotCvMenuItem>(&MenuItem::text, "Clock", &EightFaceSlotCvMenuItem::module, module, &EightFaceSlotCvMenuItem::slotCvMode, EIGHTFACE_SLOTCVMODE_CLOCK));
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

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 58.9f), module, EightFace::SLOT_INPUT));
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

		EightFaceSlotCvModeMenuItem *slotCvModeMenuItem = construct<EightFaceSlotCvModeMenuItem>(&MenuItem::text, "Port SLOT mode", &EightFaceSlotCvModeMenuItem::module, module);
		slotCvModeMenuItem->rightText = RIGHT_ARROW;
		menu->addChild(slotCvModeMenuItem);
	}
};


Model *modelEightFace = createModel<EightFace, EightFaceWidget>("EightFace");