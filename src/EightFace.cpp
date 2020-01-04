#include "plugin.hpp"
#include <functional>
#include <mutex>
#include <condition_variable>
#include <random>


namespace EightFace {

enum SLOTCVMODE {
	SLOTCVMODE_TRIG_FWD = 2,
	SLOTCVMODE_TRIG_REV = 4,
	SLOTCVMODE_TRIG_PINGPONG = 5,
	SLOTCVMODE_TRIG_RANDOM = 6,
	SLOTCVMODE_10V = 0,
	SLOTCVMODE_C4 = 1,
	SLOTCVMODE_ARM = 3
};

enum MODE {
	MODE_LEFT = 0,
	MODE_RIGHT = 1
};


template < int NUM_PRESETS >
struct EightFaceModule : Module {
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
		ENUMS(LEFT_LIGHT, 2),
		ENUMS(RIGHT_LIGHT, 2),
		ENUMS(PRESET_LIGHT, NUM_PRESETS * 3),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] left? right? */
	MODE mode = MODE_LEFT;

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
	/** [Stored to JSON] */
	bool autoload = false;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE_TRIG_FWD;
	int slotCvModeDir = 1;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int>* randDist = NULL;

	int connected = 0;
	int presetNext = -1;
	float modeLight = 0;


	std::mutex workerMutex;
	std::condition_variable workerCondVar;
	std::thread* worker;
	bool workerIsRunning = true;
	bool workerDoProcess = false;
	int workerPreset = -1;
	ModuleWidget* workerModuleWidget;

	LongPressButton typeButtons[NUM_PRESETS];
	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;

	dsp::ClockDivider lightDivider;
	dsp::ClockDivider buttonDivider;

	EightFaceModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MODE_PARAM, 0, 1, 0, "Switch Read/write mode");
		for (int i = 0; i < NUM_PRESETS; i++) {
			configParam(PRESET_PARAM + i, 0, 1, 0, string::f("Preset slot %d", i + 1));
			typeButtons[i].param = &params[PRESET_PARAM + i];
			presetSlotUsed[i] = false;
		}

		lightDivider.setDivision(512);
		buttonDivider.setDivision(4);
		onReset();
		worker = new std::thread(&EightFaceModule::workerProcess, this);
	}

	~EightFaceModule() {
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (presetSlotUsed[i])
				json_decref(presetSlot[i]);
		}
		delete randDist;

		workerIsRunning = false;
		workerDoProcess = true;
		workerCondVar.notify_one();
		worker->join();
		delete worker;
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
		if (randDist) delete randDist;
		randDist = new std::uniform_int_distribution<int>(0, presetCount - 1);
		autoload = false;
	}

	void process(const ProcessArgs& args) override {
		Expander* exp = mode == MODE_LEFT ? &leftExpander : &rightExpander;
		if (exp->moduleId >= 0 && exp->module) {
			Module* t = exp->module;
			bool c = modelSlug == "" || (t->model->name == modelSlug && t->model->plugin->name == pluginSlug);
			connected = c ? 2 : 1;

			if (connected == 2) {
				// Read mode
				if (params[MODE_PARAM].getValue() == 0.f) {
					// RESET input
					if (slotCvMode == SLOTCVMODE_TRIG_FWD || slotCvMode == SLOTCVMODE_TRIG_REV || slotCvMode == SLOTCVMODE_TRIG_PINGPONG) {
						if (inputs[RESET_INPUT].isConnected() && resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
							resetTimer.reset();
							presetLoad(t, 0);
						}
					}

					// SLOT input
					if (resetTimer.process(args.sampleTime) >= 1e-3f && inputs[SLOT_INPUT].isConnected()) {
						switch (slotCvMode) {
							case SLOTCVMODE_10V:
								presetLoad(t, std::floor(rescale(inputs[SLOT_INPUT].getVoltage(), 0.f, 10.f, 0, presetCount)));
								break;
							case SLOTCVMODE_C4:
								presetLoad(t, std::round(clamp(inputs[SLOT_INPUT].getVoltage() * 12.f, 0.f, NUM_PRESETS - 1.f)));
								break;
							case SLOTCVMODE_TRIG_FWD:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, (preset + 1) % presetCount);
								break;
							case SLOTCVMODE_TRIG_REV:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, (preset - 1 + presetCount) % presetCount);
								break;
							case SLOTCVMODE_TRIG_PINGPONG:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									int n = preset + slotCvModeDir;
									if (n == presetCount - 1) 
										slotCvModeDir = -1;
									if (n == 0) 
										slotCvModeDir = 1;
									presetLoad(t, n);
								}
								break;
							case SLOTCVMODE_TRIG_RANDOM:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, (*randDist)(randGen));
								break;
							case SLOTCVMODE_ARM:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, presetNext);
								break;
						}
					}

					// Buttons
					if (buttonDivider.process()) {
						float sampleTime = args.sampleTime * buttonDivider.division;
						for (int i = 0; i < NUM_PRESETS; i++) {
							switch (typeButtons[i].process(sampleTime)) {
								default:
								case LongPressButton::NO_PRESS:
									break;
								case LongPressButton::SHORT_PRESS:
									presetLoad(t, i, slotCvMode == SLOTCVMODE_ARM, true); break;
								case LongPressButton::LONG_PRESS:
									presetSetCount(i + 1); break;
							}
						}
					}
				}
				// Write mode
				else {
					if (buttonDivider.process()) {
						float sampleTime = args.sampleTime * buttonDivider.division;
						for (int i = 0; i < NUM_PRESETS; i++) {
							switch (typeButtons[i].process(sampleTime)) {
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
		}
		else {
			connected = 0;
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			float s = args.sampleTime * lightDivider.getDivision();
			modeLight += 0.7f * s;
			if (modeLight > 1.5f) modeLight = 0.f;

			if (mode == MODE_LEFT) {
				lights[LEFT_LIGHT + 0].setBrightness(connected == 2 ? std::min(modeLight, 1.f) : 0.f);
				lights[LEFT_LIGHT + 1].setBrightness(connected == 1 ? 1.f : 0.f);
				lights[RIGHT_LIGHT + 0].setBrightness(0.f);
				lights[RIGHT_LIGHT + 1].setBrightness(0.f);
			}
			else {
				lights[LEFT_LIGHT + 0].setBrightness(0.f);
				lights[LEFT_LIGHT + 1].setBrightness(0.f);
				lights[RIGHT_LIGHT + 0].setBrightness(connected == 2 ? std::min(modeLight, 1.f) : 0.f);
				lights[RIGHT_LIGHT + 1].setBrightness(connected == 1 ? 1.f : 0.f);
			}

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


	void workerProcess() {
		while (true) {
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondVar.wait(lock, std::bind(&EightFaceModule::workerDoProcess, this));
			if (!workerIsRunning || workerPreset < 0) return;
			workerModuleWidget->fromJson(presetSlot[workerPreset]);
			workerDoProcess = false;
		}
	}

	void presetLoad(Module* m, int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		if (!isNext) {
			if (p != preset || force) {
				preset = p;
				presetNext = -1;
				if (!presetSlotUsed[p]) return;
				ModuleWidget* mw = APP->scene->rack->getModule(m->id);
				//mw->fromJson(presetSlot[p]);
				workerModuleWidget = mw;
				workerPreset = p;
				workerDoProcess = true;
				workerCondVar.notify_one();
			}
		}
		else {
			if (!presetSlotUsed[p]) return;
			presetNext = p;
		}
	}

	void presetSave(Module* m, int p) {
		pluginSlug = m->model->plugin->name;
		modelSlug = m->model->name;
		moduleName = m->model->plugin->brand + " " + m->model->name;

		// Do not handle some specific modules known to use mapping of parameters:
		// Potential thread locking when multi-threading is enabled and parameter mappings
		// are restored from preset.
		/*
		if (!( (pluginSlug == "Stoermelder-P1" && (modelSlug == "CVMap" || modelSlug == "CVMapMicro" || modelSlug == "CVPam" || modelSlug == "ReMoveLite" || modelSlug == "MidiCat"))
			|| (pluginSlug == "Core" && modelSlug == "MIDI-Map")))
			return;
		*/

		ModuleWidget* mw = APP->scene->rack->getModule(m->id);
		if (presetSlotUsed[p]) json_decref(presetSlot[p]);
		presetSlotUsed[p] = true;
		presetSlot[p] = mw->toJson();
	}

	void presetClear(int p) {
		if (presetSlotUsed[p])
			json_decref(presetSlot[p]);
		presetSlot[p] = NULL;
		presetSlotUsed[p] = false;
		if (preset == p) preset = -1;
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
		delete randDist;
		randDist = new std::uniform_int_distribution<int>(0, presetCount - 1);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "pluginSlug", json_string(pluginSlug.c_str()));
		json_object_set_new(rootJ, "modelSlug", json_string(modelSlug.c_str()));
		json_object_set_new(rootJ, "moduleName", json_string(moduleName.c_str()));
		json_object_set_new(rootJ, "slotCvMode", json_integer(slotCvMode));
		json_object_set_new(rootJ, "preset", json_integer(preset));
		json_object_set_new(rootJ, "presetCount", json_integer(presetCount));

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(presetSlotUsed[i]));
			if (presetSlotUsed[i]) {
				json_object_set(presetJ, "slot", presetSlot[i]);
			}
			json_array_append_new(presetsJ, presetJ);
		}
		json_object_set_new(rootJ, "presets", presetsJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* modeJ = json_object_get(rootJ, "mode");
		if (modeJ) mode = (MODE)json_integer_value(modeJ);
		pluginSlug = json_string_value(json_object_get(rootJ, "pluginSlug"));
		modelSlug = json_string_value(json_object_get(rootJ, "modelSlug"));
		json_t* moduleNameJ = json_object_get(rootJ, "moduleName");
		if (moduleNameJ) moduleName = json_string_value(json_object_get(rootJ, "moduleName"));
		slotCvMode = (SLOTCVMODE)json_integer_value(json_object_get(rootJ, "slotCvMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		json_t* presetsJ = json_object_get(rootJ, "presets");
		json_t* presetJ;
		size_t presetIndex;
		json_array_foreach(presetsJ, presetIndex, presetJ) {
			presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			presetSlot[presetIndex] = json_deep_copy(json_object_get(presetJ, "slot"));
		}

		if (preset >= presetCount) 
			preset = 0;

		if (autoload) {
			Expander* exp = mode == MODE_LEFT ? &leftExpander : &rightExpander;
			if (exp->moduleId >= 0 && exp->module) {
				Module* t = exp->module;
				presetLoad(t, 0, false);
			}
		}
	}
};


template < typename MODULE >
struct SlovCvModeMenuItem : MenuItem {
	struct SlotCvModeItem : MenuItem {
		MODULE* module;
		SLOTCVMODE slotCvMode;

		void onAction(const event::Action& e) override {
			module->slotCvMode = slotCvMode;
		}

		void step() override {
			rightText = module->slotCvMode == slotCvMode ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	SlovCvModeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger forward", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_FWD));
		menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger reverse", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_REV));
		menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pingpong", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_PINGPONG));
		menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_RANDOM));
		menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "0..10V", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_10V));
		menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "C4", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_C4));
		menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Arm", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_ARM));
		return menu;
	}
};

template < typename MODULE >
struct AutoloadItem : MenuItem {
	MODULE* module;

	void onAction(const event::Action& e) override {
		module->autoload ^= true;
	}

	void step() override {
		rightText = module->autoload ? "✔" : "";
		MenuItem::step();
	}
};

template < typename MODULE >
struct ModeItem : MenuItem {
	MODULE* module;

	void onAction(const event::Action& e) override {
		module->mode = module->mode == MODE_LEFT ? MODE_RIGHT : MODE_LEFT;
	}

	void step() override {
		rightText = module->mode == MODE_LEFT ? "Left" : "Right";
		MenuItem::step();
	}
};


struct CKSSH : CKSS {
	CKSSH() {
		shadow->opacity = 0.0f;
		fb->removeChild(sw);

		TransformWidget* tw = new TransformWidget();
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

struct WhiteRedLight : GrayModuleLightWidget {
	WhiteRedLight() {
		this->addBaseColor(SCHEME_WHITE);
		this->addBaseColor(SCHEME_RED);
	}
};


template < typename MODULE >
struct EightFaceWidgetTemplate : ModuleWidget {
	void appendContextMenu(Menu* menu) override {
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		if (module->moduleName != "") {
			menu->addChild(new MenuSeparator());

			ui::MenuLabel* textLabel = new ui::MenuLabel;
			textLabel->text = "Configured for...";
			menu->addChild(textLabel);

			ui::MenuLabel* modelLabel = new ui::MenuLabel;
			modelLabel->text = module->moduleName;
			menu->addChild(modelLabel);
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SlovCvModeMenuItem<MODULE>>(&MenuItem::text, "Port SLOT mode", &SlovCvModeMenuItem<MODULE>::module, module));
		menu->addChild(construct<ModeItem<MODULE>>(&MenuItem::text, "Module", &ModeItem<MODULE>::module, module));
		menu->addChild(construct<AutoloadItem<MODULE>>(&MenuItem::text, "Autoload first preset", &AutoloadItem<MODULE>::module, module));
	}
};

struct EightFaceWidget : ThemedModuleWidget<EightFaceModule<8>, EightFaceWidgetTemplate<EightFaceModule<8>>> {
	typedef EightFaceModule<8> MODULE;

	EightFaceWidget(MODULE* module)
		: ThemedModuleWidget<MODULE, EightFaceWidgetTemplate<MODULE>>(module, "EightFace") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 58.9f), module, MODULE::SLOT_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 95.2f), module, MODULE::RESET_INPUT));

		addChild(createLightCentered<TriangleLeftLight<SmallLight<WhiteRedLight>>>(Vec(13.8f, 119.1f), module, MODULE::LEFT_LIGHT));
		addChild(createLightCentered<TriangleRightLight<SmallLight<WhiteRedLight>>>(Vec(31.2f, 119.1f), module, MODULE::RIGHT_LIGHT));

		addParam(createParamCentered<LEDButton>(Vec(22.5f, 140.6f), module, MODULE::PRESET_PARAM + 0));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 164.1f), module, MODULE::PRESET_PARAM + 1));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 187.7f), module, MODULE::PRESET_PARAM + 2));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 211.2f), module, MODULE::PRESET_PARAM + 3));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 234.8f), module, MODULE::PRESET_PARAM + 4));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 258.3f), module, MODULE::PRESET_PARAM + 5));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 281.9f), module, MODULE::PRESET_PARAM + 6));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 305.4f), module, MODULE::PRESET_PARAM + 7));

		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 140.6f), module, MODULE::PRESET_LIGHT + 0 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 164.1f), module, MODULE::PRESET_LIGHT + 1 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 187.7f), module, MODULE::PRESET_LIGHT + 2 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 211.2f), module, MODULE::PRESET_LIGHT + 3 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 234.8f), module, MODULE::PRESET_LIGHT + 4 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 258.3f), module, MODULE::PRESET_LIGHT + 5 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 281.9f), module, MODULE::PRESET_LIGHT + 6 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(22.5f, 305.4f), module, MODULE::PRESET_LIGHT + 7 * 3));

		addParam(createParamCentered<CKSSH>(Vec(22.5f, 336.2f), module, MODULE::MODE_PARAM));
	}
};

struct EightFaceX2Widget : ThemedModuleWidget<EightFaceModule<16>, EightFaceWidgetTemplate<EightFaceModule<16>>> {
	typedef EightFaceModule<16> MODULE;

	EightFaceX2Widget(MODULE* module)
		: ThemedModuleWidget<MODULE, EightFaceWidgetTemplate<MODULE>>(module, "EightFaceX2") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(30.0f, 58.9f), module, MODULE::SLOT_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(30.0f, 95.2f), module, MODULE::RESET_INPUT));

		addChild(createLightCentered<TriangleLeftLight<SmallLight<WhiteRedLight>>>(Vec(21.3f, 119.1f), module, MODULE::LEFT_LIGHT));
		addChild(createLightCentered<TriangleRightLight<SmallLight<WhiteRedLight>>>(Vec(38.7f, 119.1f), module, MODULE::RIGHT_LIGHT));

		addParam(createParamCentered<LEDButton>(Vec(17.7f, 140.6f), module, MODULE::PRESET_PARAM + 0));
		addParam(createParamCentered<LEDButton>(Vec(17.7f, 164.1f), module, MODULE::PRESET_PARAM + 1));
		addParam(createParamCentered<LEDButton>(Vec(17.7f, 187.7f), module, MODULE::PRESET_PARAM + 2));
		addParam(createParamCentered<LEDButton>(Vec(17.7f, 211.2f), module, MODULE::PRESET_PARAM + 3));
		addParam(createParamCentered<LEDButton>(Vec(17.7f, 234.8f), module, MODULE::PRESET_PARAM + 4));
		addParam(createParamCentered<LEDButton>(Vec(17.7f, 258.3f), module, MODULE::PRESET_PARAM + 5));
		addParam(createParamCentered<LEDButton>(Vec(17.7f, 281.9f), module, MODULE::PRESET_PARAM + 6));
		addParam(createParamCentered<LEDButton>(Vec(17.7f, 305.4f), module, MODULE::PRESET_PARAM + 7));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 140.6f), module, MODULE::PRESET_PARAM + 8));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 164.1f), module, MODULE::PRESET_PARAM + 9));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 187.7f), module, MODULE::PRESET_PARAM + 10));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 211.2f), module, MODULE::PRESET_PARAM + 11));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 234.8f), module, MODULE::PRESET_PARAM + 12));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 258.3f), module, MODULE::PRESET_PARAM + 13));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 281.9f), module, MODULE::PRESET_PARAM + 14));
		addParam(createParamCentered<LEDButton>(Vec(42.3f, 305.4f), module, MODULE::PRESET_PARAM + 15));

		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 140.6f), module, MODULE::PRESET_LIGHT + 0 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 164.1f), module, MODULE::PRESET_LIGHT + 1 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 187.7f), module, MODULE::PRESET_LIGHT + 2 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 211.2f), module, MODULE::PRESET_LIGHT + 3 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 234.8f), module, MODULE::PRESET_LIGHT + 4 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 258.3f), module, MODULE::PRESET_LIGHT + 5 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 281.9f), module, MODULE::PRESET_LIGHT + 6 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(17.7f, 305.4f), module, MODULE::PRESET_LIGHT + 7 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 140.6f), module, MODULE::PRESET_LIGHT + 8 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 164.1f), module, MODULE::PRESET_LIGHT + 9 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 187.7f), module, MODULE::PRESET_LIGHT + 10 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 211.2f), module, MODULE::PRESET_LIGHT + 11 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 234.8f), module, MODULE::PRESET_LIGHT + 12 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 258.3f), module, MODULE::PRESET_LIGHT + 13 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 281.9f), module, MODULE::PRESET_LIGHT + 14 * 3));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(42.3f, 305.4f), module, MODULE::PRESET_LIGHT + 15 * 3));

		addParam(createParamCentered<CKSSH>(Vec(30.0f, 336.2f), module, MODULE::MODE_PARAM));
	}
};

} // namespace EightFace

Model* modelEightFace = createModel<EightFace::EightFaceModule<8>, EightFace::EightFaceWidget>("EightFace");
Model* modelEightFaceX2 = createModel<EightFace::EightFaceModule<16>, EightFace::EightFaceX2Widget>("EightFaceX2");