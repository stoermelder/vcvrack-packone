#include "plugin.hpp"
#include "EightFace.hpp"
#include <functional>
#include <mutex>
#include <condition_variable>
#include <random>

namespace StoermelderPackOne {
namespace EightFace {

enum class SLOTCVMODE {
	OFF = -1,
	TRIG_FWD = 2,
	TRIG_REV = 4,
	TRIG_PINGPONG = 5,
	TRIG_ALT = 9,
	TRIG_RANDOM = 6,
	TRIG_RANDOM_WO_REPEATS = 7,
	TRIG_RANDOM_WALK = 8,
	TRIG_SHUFFLE = 10,
	VOLT = 0,
	C4 = 1,
	ARM = 3
};

enum class SIDE {
	LEFT = 0,
	RIGHT = 1
};

enum class CTRLMODE {
	READ,
	AUTO,
	WRITE
};

template < int NUM_PRESETS >
struct EightFaceModule : Module {
	enum ParamIds {
		CTRLMODE_PARAM,
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
	/** Current operating mode */
	CTRLMODE ctrlMode = CTRLMODE::READ;

	/** [Stored to JSON] left? right? */
	SIDE side = SIDE::LEFT;

	/** [Stored to JSON] */
	std::string pluginSlug;
	/** [Stored to JSON] */
	std::string modelSlug;
	/** [Stored to JSON] */
	std::string realPluginSlug;
	/** [Stored to JSON] */
	std::string realModelSlug;
	/** [Stored to JSON] */
	std::string moduleName;

	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS];
	/** [Stored to JSON] */
	json_t* presetSlot[NUM_PRESETS];

	/** [Stored to JSON] */
	int preset = 0;
	/** [Stored to JSON] */
	int presetCount = NUM_PRESETS;
	/** [Stored to JSON] */
	AUTOLOAD autoload = AUTOLOAD::OFF;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE::TRIG_FWD;
	SLOTCVMODE slotCvModeBak = SLOTCVMODE::OFF;
	int slotCvModeDir = 1;
	int slotCvModeAlt = 1;
	std::vector<int> slotCvModeShuffle;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist;

	int connected = 0;
	int presetPrev = -1;
	int presetNext = -1;
	float modeLight = 0;

	std::mutex workerMutex;
	std::condition_variable workerCondVar;
	std::thread* worker;
	Context* workerContext;
	bool workerIsRunning = true;
	bool workerDoProcess = false;
	int workerPreset = -1;
	ModuleWidget* workerModuleWidget;
	bool workerGui = false;
	ModuleWidget* workerGuiModuleWidget = NULL;

	LongPressButton typeButtons[NUM_PRESETS];
	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;

	dsp::ClockDivider lightDivider;
	dsp::ClockDivider buttonDivider;

	EightFaceModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(SLOT_INPUT, "Slot selection");
		inputInfos[SLOT_INPUT]->description = "Operating mode is set on the context menu.";
		configInput(RESET_INPUT, "Reset");
		configParam<TriggerParamQuantity>(CTRLMODE_PARAM, 0, 2, 0, "Read/Auto/Write mode");
		for (int i = 0; i < NUM_PRESETS; i++) {
			configParam(PRESET_PARAM + i, 0, 1, 0, string::f("Preset slot %d", i + 1));
			typeButtons[i].param = &params[PRESET_PARAM + i];
			presetSlotUsed[i] = false;
		}

		lightDivider.setDivision(512);
		buttonDivider.setDivision(4);
		onReset();
		workerContext = contextGet();
		worker = new std::thread(&EightFaceModule::processWorker, this);
	}

	~EightFaceModule() {
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (presetSlotUsed[i])
				json_decref(presetSlot[i]);
		}

		workerIsRunning = false;
		workerDoProcess = true;
		workerCondVar.notify_one();
		worker->join();
		workerContext = NULL;
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
		presetPrev = -1;
		presetNext = -1;
		modelSlug = "";
		pluginSlug = "";
		realModelSlug = "";
		realPluginSlug = "";
		moduleName = "";
		connected = 0;
		autoload = AUTOLOAD::OFF;
	}

	void process(const ProcessArgs& args) override {
		Expander* exp = side == SIDE::LEFT ? &leftExpander : &rightExpander;
		if (exp->moduleId >= 0 && exp->module) {
			Module* t = exp->module;
			bool c = modelSlug == "" || (t->model->name == modelSlug && t->model->plugin->name == pluginSlug);
			connected = c ? 2 : 1;

			if (connected == 2) {
				ctrlMode = (CTRLMODE)params[CTRLMODE_PARAM].getValue();

				// Read & Auto modes
				if (ctrlMode == CTRLMODE::READ || ctrlMode == CTRLMODE::AUTO) {
					// RESET input
					if (slotCvMode == SLOTCVMODE::TRIG_FWD || slotCvMode == SLOTCVMODE::TRIG_REV || slotCvMode == SLOTCVMODE::TRIG_PINGPONG) {
						if (inputs[RESET_INPUT].isConnected() && resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
							resetTimer.reset();
							presetLoad(t, 0);
						}
					}

					// SLOT input
					if (resetTimer.process(args.sampleTime) >= 1e-3f && inputs[SLOT_INPUT].isConnected()) {
						switch (slotCvMode) {
							case SLOTCVMODE::VOLT:
								presetLoad(t, std::floor(rescale(inputs[SLOT_INPUT].getVoltage(), 0.f, 10.f, 0, presetCount)));
								break;
							case SLOTCVMODE::C4:
								presetLoad(t, std::round(clamp(inputs[SLOT_INPUT].getVoltage() * 12.f, 0.f, NUM_PRESETS - 1.f)));
								break;
							case SLOTCVMODE::TRIG_FWD:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, (preset + 1) % presetCount);
								break;
							case SLOTCVMODE::TRIG_REV:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
									presetLoad(t, (preset - 1 + presetCount) % presetCount);
								break;
							case SLOTCVMODE::TRIG_PINGPONG:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									int n = preset + slotCvModeDir;
									if (n >= presetCount - 1) 
										slotCvModeDir = -1;
									if (n <= 0) 
										slotCvModeDir = 1;
									presetLoad(t, n);
								}
								break;
							case SLOTCVMODE::TRIG_ALT:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									int n = 0;
									if (preset == 0) {
										n = slotCvModeAlt + slotCvModeDir;
										if (n >= presetCount - 1)
											slotCvModeDir = -1;
										if (n <= 1)
											slotCvModeDir = 1;
										slotCvModeAlt = std::min(n, presetCount - 1);
									}
									presetLoad(t, n);
								}
								break;
							case SLOTCVMODE::TRIG_RANDOM:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									if (randDist.max() != presetCount - 1) randDist = std::uniform_int_distribution<int>(0, presetCount - 1);
									presetLoad(t, randDist(randGen));
								}
								break;
							case SLOTCVMODE::TRIG_RANDOM_WO_REPEATS:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									if (randDist.max() != presetCount - 2) randDist = std::uniform_int_distribution<int>(0, presetCount - 2);
									int p = randDist(randGen);
									if (p >= preset) p++;
									presetLoad(t, p);
								}
								break;
							case SLOTCVMODE::TRIG_RANDOM_WALK:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									int p = std::min(std::max(0, preset + (random::u32() % 2 == 0 ? -1 : 1)), presetCount - 1);
									presetLoad(t, p);
								}
								break;
							case SLOTCVMODE::TRIG_SHUFFLE:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									if (slotCvModeShuffle.size() == 0) {
										for (int i = 0; i < presetCount; i++) {
											slotCvModeShuffle.push_back(i);
										}
										std::random_shuffle(std::begin(slotCvModeShuffle), std::end(slotCvModeShuffle));
									}
									int p = std::min(std::max(0, slotCvModeShuffle.back()), presetCount - 1);
									slotCvModeShuffle.pop_back();
									presetLoad(t, p);
								}
								break;
							case SLOTCVMODE::ARM:
								if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
									presetLoad(t, presetNext, false, true);
								}
								break;
							case SLOTCVMODE::OFF:
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
									presetLoad(t, i, slotCvMode == SLOTCVMODE::ARM, true); break;
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

			if (side == SIDE::LEFT) {
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
				if (ctrlMode == CTRLMODE::READ || ctrlMode == CTRLMODE::AUTO) {
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


	void processWorker() {
		contextSet(workerContext);
		while (true) {
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondVar.wait(lock, std::bind(&EightFaceModule::workerDoProcess, this));
			if (!workerIsRunning || workerPreset < 0) return;
			if (ctrlMode == CTRLMODE::AUTO && presetPrev >= 0 && presetSlotUsed[presetPrev]) {
				json_decref(presetSlot[presetPrev]);
				presetSlot[presetPrev] = workerModuleWidget->toJson();
			}
			workerModuleWidget->fromJson(presetSlot[workerPreset]);
			workerDoProcess = false;
		}
	}

	void processGui() {
		if (workerGuiModuleWidget) {
			if (ctrlMode == CTRLMODE::AUTO && presetPrev >= 0 && presetSlotUsed[presetPrev]) {
				json_decref(presetSlot[presetPrev]);
				presetSlot[presetPrev] = workerModuleWidget->toJson();
			}
			workerGuiModuleWidget->fromJson(presetSlot[workerPreset]);
			workerGuiModuleWidget = NULL;
		}
	}

	void presetLoad(Module* m, int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		if (!isNext) {
			if (p != preset || force) {
				presetPrev = preset;
				preset = p;
				presetNext = -1;
				if (!presetSlotUsed[p]) return;
				ModuleWidget* mw = APP->scene->rack->getModule(m->id);
				if (mw) {
					workerPreset = p;
					if (workerGui) {
						workerGuiModuleWidget = mw;
					}
					else {
						workerModuleWidget = mw;
						workerDoProcess = true;
						workerCondVar.notify_one();
					}
				}
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
		realPluginSlug = m->model->plugin->slug;
		realModelSlug = m->model->slug;
		auto it = guiModuleSlugs.find(std::make_tuple(realPluginSlug, realModelSlug));
		workerGui = it != guiModuleSlugs.end();

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
		presetPrev = -1;
		presetNext = -1;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mode", json_integer((int)side));
		json_object_set_new(rootJ, "pluginSlug", json_string(pluginSlug.c_str()));
		json_object_set_new(rootJ, "modelSlug", json_string(modelSlug.c_str()));
		json_object_set_new(rootJ, "realPluginSlug", json_string(realPluginSlug.c_str()));
		json_object_set_new(rootJ, "realModelSlug", json_string(realModelSlug.c_str()));
		json_object_set_new(rootJ, "moduleName", json_string(moduleName.c_str()));
		json_object_set_new(rootJ, "slotCvMode", json_integer((int)slotCvMode));
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

		json_t* sideJ = json_object_get(rootJ, "mode");
		if (sideJ) side = (SIDE)json_integer_value(sideJ);
		pluginSlug = json_string_value(json_object_get(rootJ, "pluginSlug"));
		modelSlug = json_string_value(json_object_get(rootJ, "modelSlug"));

		json_t* realPluginSlugJ = json_object_get(rootJ, "realPluginSlug");
		if (realPluginSlugJ) realPluginSlug = json_string_value(realPluginSlugJ);
		json_t* realModelSlugJ = json_object_get(rootJ, "realModelSlug");
		if (realModelSlugJ) realModelSlug = json_string_value(realModelSlugJ);
		auto it = guiModuleSlugs.find(std::make_tuple(realPluginSlug, realModelSlug));
		workerGui = it != guiModuleSlugs.end();

		json_t* moduleNameJ = json_object_get(rootJ, "moduleName");
		if (moduleNameJ) moduleName = json_string_value(json_object_get(rootJ, "moduleName"));
		slotCvMode = (SLOTCVMODE)json_integer_value(json_object_get(rootJ, "slotCvMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		for (int i = 0; i < NUM_PRESETS; i++) {
			if (presetSlotUsed[i]) {
				json_decref(presetSlot[i]);
				presetSlot[i] = NULL;
			}
			presetSlotUsed[i] = false;
		}

		json_t* presetsJ = json_object_get(rootJ, "presets");
		json_t* presetJ;
		size_t presetIndex;
		json_array_foreach(presetsJ, presetIndex, presetJ) {
			presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			presetSlot[presetIndex] = json_deep_copy(json_object_get(presetJ, "slot"));
		}

		presetPrev = -1;
		if (preset >= presetCount)
			preset = 0;

		// TODO: This needs to be reviewed as presetLoad might fail on patch-load if this module
		// is loaded before the expanded module
		switch (autoload) {
			case AUTOLOAD::FIRST: {
				Expander* exp = side == SIDE::LEFT ? &leftExpander : &rightExpander;
				if (exp->moduleId >= 0 && exp->module) {
					Module* t = exp->module;
					presetLoad(t, 0, false, true);
				}
				break;
			}
			case AUTOLOAD::LASTACTIVE: {
				Expander* exp = side == SIDE::LEFT ? &leftExpander : &rightExpander;
				if (exp->moduleId >= 0 && exp->module) {
					Module* t = exp->module;
					presetLoad(t, preset, false, true);
				}
				break;
			}
			default:
				break;
		}

		params[CTRLMODE_PARAM].setValue(0.f);
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
			menu->addChild(createMenuLabel("Configured for..."));
			menu->addChild(createMenuLabel(module->moduleName));
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Port SLOT mode", "",
			[=](Menu* menu) {	
				struct SlotCvModeItem : MenuItem {
					MODULE* module;
					SLOTCVMODE slotCvMode;
					std::string rightTextEx = "";
					void onAction(const event::Action& e) override {
						module->slotCvMode = module->slotCvModeBak = slotCvMode;
					}
					void step() override {
						rightText = string::f("%s %s", module->slotCvMode == slotCvMode ? "✔" : "", rightTextEx.c_str());
						MenuItem::step();
					}
				};

				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger forward", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_FWD));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger reverse", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_REV));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pingpong", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_PINGPONG));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger alternating", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_ALT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pseudo-random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WO_REPEATS));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random walk", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WALK));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger shuffle", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_SHUFFLE));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "0..10V", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::VOLT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "C4", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::C4));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Arm", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::ARM));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Off", &SlotCvModeItem::rightTextEx, RACK_MOD_SHIFT_NAME "+Q", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::OFF));
			}
		));

		struct SideItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->side = module->side == SIDE::LEFT ? SIDE::RIGHT : SIDE::LEFT;
			}
			void step() override {
				rightText = module->side == SIDE::LEFT ? "Left" : "Right";
				MenuItem::step();
			}
		};

		menu->addChild(construct<SideItem>(&MenuItem::text, "Module", &SideItem::module, module));
		menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem<AUTOLOAD>("Autoload", 
			{
				{ AUTOLOAD::OFF, "Off" },
				{ AUTOLOAD::FIRST, "First preset" },
				{ AUTOLOAD::LASTACTIVE, "Last active preset" }
			},
			&module->autoload,
			false
		));
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			switch (e.key) {
				case GLFW_KEY_Q:
					MODULE* module = dynamic_cast<MODULE*>(this->module);
					module->slotCvMode = module->slotCvMode == SLOTCVMODE::OFF ? module->slotCvModeBak : SLOTCVMODE::OFF;
					e.consume(this);
					break;
			}
		}
		ModuleWidget::onHoverKey(e);
	}
};

struct EightFaceWidget : ThemedModuleWidget<EightFaceModule<8>, EightFaceWidgetTemplate<EightFaceModule<8>>> {
	typedef EightFaceModule<8> MODULE;
	MODULE* module;

	EightFaceWidget(MODULE* module)
		: ThemedModuleWidget<MODULE, EightFaceWidgetTemplate<MODULE>>(module, "EightFace") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 58.9f), module, MODULE::SLOT_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 95.2f), module, MODULE::RESET_INPUT));

		addChild(createLightCentered<TriangleLeftLight<WhiteRedLight>>(Vec(13.8f, 119.1f), module, MODULE::LEFT_LIGHT));
		addChild(createLightCentered<TriangleRightLight<WhiteRedLight>>(Vec(31.2f, 119.1f), module, MODULE::RIGHT_LIGHT));

		addParam(createParamCentered<LEDButton>(Vec(22.5f, 140.6f), module, MODULE::PRESET_PARAM + 0));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 164.1f), module, MODULE::PRESET_PARAM + 1));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 187.7f), module, MODULE::PRESET_PARAM + 2));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 211.2f), module, MODULE::PRESET_PARAM + 3));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 234.8f), module, MODULE::PRESET_PARAM + 4));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 258.3f), module, MODULE::PRESET_PARAM + 5));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 281.9f), module, MODULE::PRESET_PARAM + 6));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 305.4f), module, MODULE::PRESET_PARAM + 7));

		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 140.6f), module, MODULE::PRESET_LIGHT + 0 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 164.1f), module, MODULE::PRESET_LIGHT + 1 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 187.7f), module, MODULE::PRESET_LIGHT + 2 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 211.2f), module, MODULE::PRESET_LIGHT + 3 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 234.8f), module, MODULE::PRESET_LIGHT + 4 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 258.3f), module, MODULE::PRESET_LIGHT + 5 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 281.9f), module, MODULE::PRESET_LIGHT + 6 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 305.4f), module, MODULE::PRESET_LIGHT + 7 * 3));

		addParam(createParamCentered<CKSSThreeH>(Vec(22.5f, 336.2f), module, MODULE::CTRLMODE_PARAM));
	}

	void step() override {
		if (module) {
			module->processGui();
		}
		ThemedModuleWidget<MODULE, EightFaceWidgetTemplate<MODULE>>::step();
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

		addChild(createLightCentered<TriangleLeftLight<WhiteRedLight>>(Vec(21.3f, 119.1f), module, MODULE::LEFT_LIGHT));
		addChild(createLightCentered<TriangleRightLight<WhiteRedLight>>(Vec(38.7f, 119.1f), module, MODULE::RIGHT_LIGHT));

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

		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 140.6f), module, MODULE::PRESET_LIGHT + 0 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 164.1f), module, MODULE::PRESET_LIGHT + 1 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 187.7f), module, MODULE::PRESET_LIGHT + 2 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 211.2f), module, MODULE::PRESET_LIGHT + 3 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 234.8f), module, MODULE::PRESET_LIGHT + 4 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 258.3f), module, MODULE::PRESET_LIGHT + 5 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 281.9f), module, MODULE::PRESET_LIGHT + 6 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.7f, 305.4f), module, MODULE::PRESET_LIGHT + 7 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 140.6f), module, MODULE::PRESET_LIGHT + 8 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 164.1f), module, MODULE::PRESET_LIGHT + 9 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 187.7f), module, MODULE::PRESET_LIGHT + 10 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 211.2f), module, MODULE::PRESET_LIGHT + 11 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 234.8f), module, MODULE::PRESET_LIGHT + 12 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 258.3f), module, MODULE::PRESET_LIGHT + 13 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 281.9f), module, MODULE::PRESET_LIGHT + 14 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(42.3f, 305.4f), module, MODULE::PRESET_LIGHT + 15 * 3));

		addParam(createParamCentered<CKSSThreeH>(Vec(30.0f, 336.2f), module, MODULE::CTRLMODE_PARAM));
	}
};

} // namespace EightFace
} // namespace StoermelderPackOne

Model* modelEightFace = createModel<StoermelderPackOne::EightFace::EightFaceModule<8>, StoermelderPackOne::EightFace::EightFaceWidget>("EightFace");
Model* modelEightFaceX2 = createModel<StoermelderPackOne::EightFace::EightFaceModule<16>, StoermelderPackOne::EightFace::EightFaceX2Widget>("EightFaceX2");