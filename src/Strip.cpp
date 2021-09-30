#include "Strip.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>

namespace StoermelderPackOne {
namespace Strip {

enum class ONMODE {
	DEFAULT = 0,
	TOGGLE = 1,
	HIGHLOW = 2
};

enum class RANDOMEXCL {
	NONE = 0,
	EXC = 1,
	INC = 2
};


struct StripModule : StripModuleBase {
	enum ParamIds {
		MODE_PARAM,
		ON_PARAM,
		OFF_PARAM,
		RAND_PARAM,
		EXCLUDE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ON_INPUT,
		OFF_INPUT,
		RAND_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LEFT_LIGHT,
		RIGHT_LIGHT,
		ENUMS(EXCLUDE_LIGHT, 2),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] usage of switch+port in "ON"-section */
	ONMODE onMode = ONMODE::DEFAULT;

	bool lastState = false;

	std::mutex excludeMutex;
	bool excludeLearn = false;
	/** [Stored to JSON] */ 
	std::set<std::tuple<int64_t, int>> excludedParams;
	/** [Stored to JSON] */
	RANDOMEXCL randomExcl = RANDOMEXCL::EXC;
	/** [Stored to JSON] */
	bool randomParamsOnly;
	/** [Stored to JSON] */
	bool presetLoadReplace;

	dsp::SchmittTrigger modeTrigger;
	dsp::SchmittTrigger onTrigger;
	dsp::SchmittTrigger offPTrigger;
	dsp::SchmittTrigger randTrigger;

	dsp::ClockDivider lightDivider;

	std::mutex workerMutex;
	std::condition_variable workerCondVar;
	std::thread* worker;
	Context* workerContext;
	bool workerIsRunning = true;
	bool worker_val;
	bool worker_useHistory;

	StripModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(MODE_PARAM, 0, 1, 0, "Toggle left/right mode");
		configParam<TriggerParamQuantity>(ON_PARAM, 0, 1, 0, "Switch/toggle strip on");
		configParam<TriggerParamQuantity>(OFF_PARAM, 0, 1, 0, "Switch strip off");
		configParam<TriggerParamQuantity>(RAND_PARAM, 0, 1, 0, "Randomize strip");
		configParam(EXCLUDE_PARAM, 0, 1, 0, "Parameter randomization include/exclude");

		lightDivider.setDivision(1024);
		onReset();
		workerContext = contextGet();
		worker = new std::thread(&StripModule::processWorker, this);
	}

	~StripModule() {
		workerIsRunning = false;
		workerCondVar.notify_one();
		worker->join();
		workerContext = NULL;
		delete worker;
	}

	void onReset() override {
		// Aquire excludeMutex to get exclusive access to excludedParams
		std::lock_guard<std::mutex> lockGuard(excludeMutex);
		excludedParams.clear();
		randomParamsOnly = false;
		presetLoadReplace = false;
		// Release excludeMutex
	}

	void process(const ProcessArgs& args) override {
		if (modeTrigger.process(params[MODE_PARAM].getValue())) {
			mode = (MODE)(((int)mode + 1) % 3);
			lastState = true;
		}

		if (offPTrigger.process(params[OFF_PARAM].getValue() + inputs[OFF_INPUT].getVoltage())) {
			groupDisable(true, params[OFF_PARAM].getValue() > 0.f);
		}

		switch (onMode) {
			case ONMODE::DEFAULT:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					groupDisable(false, params[ON_PARAM].getValue() > 0.f);
				break;
			case ONMODE::TOGGLE:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					groupDisable(!lastState, params[ON_PARAM].getValue() > 0.f);
				break;
			case ONMODE::HIGHLOW:
				groupDisable(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage() < 1.f, params[ON_PARAM].getValue() > 0.f);
				break;
		}

		if (randTrigger.process(params[RAND_PARAM].getValue() + inputs[RAND_INPUT].getVoltage())) {
			groupRandomize(params[RAND_PARAM].getValue() > 0.f);
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			lights[RIGHT_LIGHT].setBrightness(mode == MODE::LEFTRIGHT || mode == MODE::RIGHT);
			lights[LEFT_LIGHT].setBrightness(mode == MODE::LEFTRIGHT || mode == MODE::LEFT);

			lights[EXCLUDE_LIGHT + 0].setBrightness(!excludeLearn && excludedParams.size() > 0 ? 1.f : 0.f);
			lights[EXCLUDE_LIGHT + 1].setBrightness(excludeLearn ? 1.f : 0.f);
		}
	}

	void processWorker() {
		contextSet(workerContext);
		while (true) {
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondVar.wait(lock);
			if (!workerIsRunning) return;
			groupDisable_worker(worker_val, worker_useHistory);
		}
	}

	void groupDisable(bool val, bool useHistory) {
		worker_val = val;
		worker_useHistory = useHistory;
		workerCondVar.notify_one();
	}

	/** 
	 * Disables/enables all modules of the current strip.
	 * To be called from engine-thread only.
	 */
	void groupDisable_worker(bool val, bool useHistory) {
		if (lastState == val) return;
		lastState = val;

		history::ComplexAction* complexAction;
		if (useHistory) {
			complexAction = new history::ComplexAction;
			complexAction->name = "stoermelder STRIP bypass";
			APP->history->push(complexAction);
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->model == modelStripBlock) break;
				// This is what "Module.hpp" says about bypass:
				// "Module subclasses should not read/write this variable."
				APP->engine->bypassModule(m->rightExpander.module, val);
				// Clear outputs and set to 1 channel
				for (Output& output : m->rightExpander.module->outputs) {
					// This zeros all voltages, but the channel is set to 1 if connected
					output.setChannels(0);
				}

				if (useHistory) {
					// history::ModuleBypass
					history::ModuleBypass* h = new history::ModuleBypass;
					h->moduleId = m->rightExpander.module->id;
					h->bypassed = m->rightExpander.module->isBypassed();
					complexAction->push(h);
				}

				m = m->rightExpander.module;
			}
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->model == modelStripBlock) break;
				// This is what "Module.hpp" says about bypass:
				// "Module subclasses should not read/write this variable."
				APP->engine->bypassModule(m->leftExpander.module, val);
				// Clear outputs and set to 1 channel
				for (Output& output : m->leftExpander.module->outputs) {
					// This zeros all voltages, but the channel is set to 1 if connected
					output.setChannels(0);
				}

				if (useHistory) {
					// history::ModuleBypass
					history::ModuleBypass* h = new history::ModuleBypass;
					h->moduleId = m->leftExpander.module->id;
					h->bypassed = m->leftExpander.module->isBypassed();
					complexAction->push(h);
				}

				m = m->leftExpander.module;
			}
		}
	}

	/** 
	 * Randomizes all modules of the current strip.
	 * To be called from engine-thread only.
	 */
	void groupRandomize(bool useHistory) {
		//std::lock_guard<std::mutex> lockGuard(excludeMutex);
		// Do not lock the mutex as changes on excludedParams are rare events

		history::ComplexAction* complexAction;
		if (useHistory) {
			complexAction = new history::ComplexAction;
			complexAction->name = "stoermelder STRIP randomize";
			APP->history->push(complexAction);
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->model == modelStripBlock) break;
				// Be careful: this function is called from the dsp-thread, but widgets belong
				// to the app-world!

				history::ModuleChange* h;
				if (useHistory) {
					// history::ModuleChange
					h = new history::ModuleChange;
					h->moduleId = m->rightExpander.moduleId;
					h->oldModuleJ = m->rightExpander.module->toJson();
					complexAction->push(h);
				}

				ModuleWidget* mw = APP->scene->rack->getModule(m->rightExpander.moduleId);
				for (ParamWidget* param : mw->getParams()) {
					switch (randomExcl) {
						case RANDOMEXCL::NONE:
							param->getParamQuantity()->randomize();
							break;
						case RANDOMEXCL::EXC:
							if (excludedParams.find(std::make_tuple(m->rightExpander.moduleId, param->getParamQuantity()->paramId)) == excludedParams.end())
								param->getParamQuantity()->randomize();
							break;
						case RANDOMEXCL::INC:
							if (excludedParams.find(std::make_tuple(m->rightExpander.moduleId, param->getParamQuantity()->paramId)) != excludedParams.end())
								param->getParamQuantity()->randomize();
							break;
					}
				}
				if (!randomParamsOnly) {
					mw->module->onRandomize();
				}
				if (useHistory) {
					h->newModuleJ = m->rightExpander.module->toJson();
				}

				m = m->rightExpander.module;
			}
		}
		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->model == modelStripBlock) break;
				// Be careful: this function is called from the dsp-thread, but widgets belong
				// to the app-world!

				history::ModuleChange* h;
				if (useHistory) {
					// history::ModuleChange
					h = new history::ModuleChange;
					h->moduleId = m->leftExpander.moduleId;
					h->oldModuleJ = m->leftExpander.module->toJson();
					complexAction->push(h);
				}

				ModuleWidget* mw = APP->scene->rack->getModule(m->leftExpander.moduleId);
				for (ParamWidget* param : mw->getParams()) {
					switch (randomExcl) {
						case RANDOMEXCL::NONE:
							param->getParamQuantity()->randomize();
							break;
						case RANDOMEXCL::EXC:
							if (excludedParams.find(std::make_tuple(m->leftExpander.moduleId, param->getParamQuantity()->paramId)) == excludedParams.end())
								param->getParamQuantity()->randomize();
							break;
						case RANDOMEXCL::INC:
							if (excludedParams.find(std::make_tuple(m->leftExpander.moduleId, param->getParamQuantity()->paramId)) != excludedParams.end())
								param->getParamQuantity()->randomize();
							break;
					}
				}
				if (!randomParamsOnly) {
					mw->module->onRandomize();
				}
				if (useHistory) {
					h->newModuleJ = m->leftExpander.module->toJson();
				}

				m = m->leftExpander.module;
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = StripModuleBase::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "onMode", json_integer((int)onMode));

		json_t* excludedParamsJ = json_array();
		// Aquire excludeMutex to get exclusive access to excludedParams
		std::lock_guard<std::mutex> lockGuard(excludeMutex);
		for (auto t : excludedParams) {
			json_t* excludedParamJ = json_object();
			json_object_set_new(excludedParamJ, "moduleId", json_integer(std::get<0>(t)));
			json_object_set_new(excludedParamJ, "paramId", json_integer(std::get<1>(t)));
			json_array_append_new(excludedParamsJ, excludedParamJ);
		} 
		json_object_set_new(rootJ, "excludedParams", excludedParamsJ);
		json_object_set_new(rootJ, "randomExcl", json_integer((int)randomExcl));
		json_object_set_new(rootJ, "randomParamsOnly", json_boolean(randomParamsOnly));
		json_object_set_new(rootJ, "presetLoadReplace", json_boolean(presetLoadReplace));
		return rootJ;
		// Release excludeMutex
	}

	void dataFromJson(json_t* rootJ) override {
		StripModuleBase::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* onModeJ = json_object_get(rootJ, "onMode");
		onMode = (ONMODE)json_integer_value(onModeJ);

		json_t* excludedParamsJ = json_object_get(rootJ, "excludedParams");
		// Aquire excludeMutex to get exclusive access to excludedParams
		std::lock_guard<std::mutex> lockGuard(excludeMutex);
		excludedParams.clear();
		if (excludedParamsJ) {
			json_t* excludedParamJ;
			size_t i; 
			json_array_foreach(excludedParamsJ, i, excludedParamJ) {
				json_t* moduleIdJ = json_object_get(excludedParamJ, "moduleId");
				json_t* paramIdJ = json_object_get(excludedParamJ, "paramId");
				if (!(moduleIdJ && paramIdJ)) 
					continue;
				int64_t moduleId = json_integer_value(moduleIdJ); 
				int paramId = json_integer_value(paramIdJ); 
				excludedParams.insert(std::make_tuple(moduleId, paramId));
			}
		}
		json_t* randomExclJ = json_object_get(rootJ, "randomExcl");
		randomExcl = (RANDOMEXCL)json_integer_value(randomExclJ);
		json_t* randomParamsOnlyJ = json_object_get(rootJ, "randomParamsOnly");
		if (randomParamsOnlyJ) randomParamsOnly = json_boolean_value(randomParamsOnlyJ);
		json_t* presetLoadReplaceJ = json_object_get(rootJ, "presetLoadReplace");
		if (presetLoadReplaceJ) presetLoadReplace = json_boolean_value(presetLoadReplaceJ);
		// Release excludeMutex
	}
};


struct RandomExclMenuItem : MenuItem {
	struct RandomExclItem : MenuItem {
		StripModule* module;
		RANDOMEXCL randomExcl;
		void onAction(const event::Action& e) override {
			module->randomExcl = randomExcl;
		}
		void step() override {
			rightText = module->randomExcl == randomExcl ? "✔" : "";
			MenuItem::step();
		}
	};

	StripModule* module;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<RandomExclItem>(&MenuItem::text, "All", &RandomExclItem::module, module, &RandomExclItem::randomExcl, RANDOMEXCL::NONE));
		menu->addChild(construct<RandomExclItem>(&MenuItem::text, "Exclude", &RandomExclItem::module, module, &RandomExclItem::randomExcl, RANDOMEXCL::EXC));
		menu->addChild(construct<RandomExclItem>(&MenuItem::text, "Include", &RandomExclItem::module, module, &RandomExclItem::randomExcl, RANDOMEXCL::INC));
		return menu;
	}
};


struct ExcludeButton : TL1105 {
	StripModule* module;
	bool learn = false;
	bool pressed = false;
	std::chrono::time_point<std::chrono::system_clock> pressedTime;

	void step() override {
		if (!module)
			return;
		
		if (pressed) {
			auto now = std::chrono::system_clock::now();
			if (now - pressedTime >= std::chrono::milliseconds{1000}) {
				// Long press
				groupExcludeClear();
				pressed = false;
			}
		}
		
		module->excludeLearn = learn;
		TL1105::step();
		groupExcludeStep();
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module)
			return;
		if (!learn)
			return;
		// Check if a ParamWidget was touched
		// NB: unstable API
		ParamWidget* touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam && touchedParam->getParamQuantity() && touchedParam->getParamQuantity()->module != module) {
			int64_t moduleId = touchedParam->getParamQuantity()->module->id;
			int paramId = touchedParam->getParamQuantity()->paramId;
			groupExcludeParam(moduleId, paramId);
		}
	}

	void onButton(const event::Button& e) override {
		// Right click to open context menu
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == 0) {
			createContextMenu();
			e.consume(this);
		}
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0) {
			if (e.action == GLFW_PRESS) {
				pressed = true;
				pressedTime = std::chrono::system_clock::now();
			}
			if (e.action == GLFW_RELEASE) {
				if (pressed) {
					// Short press
					groupExcludeLearn();
					pressed = false;
				}
			}
			TL1105::onButton(e);
		}
	}

	void groupExcludeLearn() {
		learn ^= true;
		APP->scene->rack->touchedParam = NULL;
	}

	void groupExcludeClear() {
		// Aquire excludeMutex to get exclusive access to excludedParams
		std::lock_guard<std::mutex> lockGuard(module->excludeMutex);
		module->excludedParams.clear();
		// Release excludeMutex
	}

	/** 
	 * Adds a parameter to the randomization exclusion list.
	 */
	void groupExcludeParam(int64_t moduleId, int paramId) {
		learn = false;
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->model == modelStripBlock) break;
				if (m->rightExpander.moduleId == moduleId) {
					ModuleWidget* mw = APP->scene->rack->getModule(m->rightExpander.moduleId);
					for (ParamWidget* param : mw->getParams()) {
						if (param->getParamQuantity() && param->getParamQuantity()->paramId == paramId) {
							// Aquire excludeMutex to get exclusive access to excludedParams
							std::lock_guard<std::mutex> lockGuard(module->excludeMutex);
							module->excludedParams.insert(std::make_tuple(moduleId, paramId));
							return;
							// Release excludeMutex
						}
					}
					return;
				}
				m = m->rightExpander.module;
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->model == modelStripBlock) break;
				if (m->leftExpander.moduleId == moduleId) {
					ModuleWidget* mw = APP->scene->rack->getModule(m->leftExpander.moduleId);
					for (ParamWidget* param : mw->getParams()) {
						if (param->getParamQuantity() && param->getParamQuantity()->paramId == paramId) {
							// Aquire excludeMutex to get exclusive access to excludedParams
							std::lock_guard<std::mutex> lockGuard(module->excludeMutex);
							module->excludedParams.insert(std::make_tuple(moduleId, paramId));
							return;
							// Release excludeMutex
						}
					}
					return;
				}
				m = m->leftExpander.module;
			}
		}
	}

	/**
	 * Cleans the currently list of excluded parameters from modules that are no longer 
	 * within the current strip. Called on every frame to ensure the excluded parameter list
	 * matches the modules in the strip.
	 */
	void groupExcludeStep() {
		if (module->excludedParams.size() == 0)
			return;

		std::map<int, Module*> modules;
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->model == modelStripBlock) break;
				modules[m->rightExpander.moduleId] = m;
				m = m->rightExpander.module;
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->model == modelStripBlock) break;
				modules[m->leftExpander.moduleId] = m;
				m = m->leftExpander.module;
			}
		}

		std::vector<std::tuple<int, int>> toBeDeleted;
		// Aquire excludeMutex to get exclusive access to excludedParams
		std::lock_guard<std::mutex> lockGuard(module->excludeMutex);
		for (auto it : module->excludedParams) {
			int64_t moduleId = std::get<0>(it);
			auto m = modules.find(moduleId);
			if (m == modules.end()) {
				toBeDeleted.push_back(it);
			}
		}

		for (auto it : toBeDeleted) {
			module->excludedParams.erase(it);
		}
		// Release excludeMutex
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();

		ui::MenuLabel* modelLabel = new ui::MenuLabel;
		modelLabel->text = "Parameter randomization";
		menu->addChild(modelLabel);

		RandomExclMenuItem* randomExclMenuItem = construct<RandomExclMenuItem>(&MenuItem::text, "Mode", &RandomExclMenuItem::module, module);
		randomExclMenuItem->rightText = RIGHT_ARROW;
		menu->addChild(randomExclMenuItem);

		struct LabelButton : ui::MenuItem {
			void onButton(const event::Button& e) override { }
		};

		LabelButton* help1Label = new LabelButton;
		help1Label->rightText = "short press";
		help1Label->text = "Learn";
		menu->addChild(help1Label);

		LabelButton* help2Label = new LabelButton;
		help2Label->rightText = "long press";
		help2Label->text = "Clear";
		menu->addChild(help2Label);

		if (module->excludedParams.size() == 0)
			return;

		menu->addChild(new MenuSeparator());

		// Aquire excludeMutex to get exclusive access to excludedParams
		std::lock_guard<std::mutex> lockGuard(module->excludeMutex);
		for (auto it : module->excludedParams) {
			int64_t moduleId = std::get<0>(it);
			int paramId = std::get<1>(it);
			
			ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
			if (!moduleWidget) continue;
			ParamWidget* paramWidget = moduleWidget->getParam(paramId);
			if (!paramWidget) continue;
			
			std::string text = "Parameter \"";
			text += moduleWidget->model->name;
			text += " ";
			text += paramWidget->getParamQuantity()->getLabel();
			text += "\"";

			ui::MenuLabel* modelLabel = new ui::MenuLabel;
			modelLabel->text = text;
			menu->addChild(modelLabel);
		}
		// Release excludeMutex
	}
};



struct StripWidget : StripWidgetBase<StripModule> {
	StripWidget(StripModule* module)
		: StripWidgetBase<StripModule>(module, "Strip") {
		this->module = module;
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<CKD6>(Vec(22.5f, 67.7f), module, StripModule::MODE_PARAM));

		addChild(createLightCentered<TriangleLeftLight<GreenLight>>(Vec(14.8f, 91.2f), module, StripModule::LEFT_LIGHT));
		addChild(createLightCentered<TriangleRightLight<GreenLight>>(Vec(30.2f, 91.2f), module, StripModule::RIGHT_LIGHT));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 139.4f), module, StripModule::ON_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 162.7f), module, StripModule::ON_PARAM));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 205.1f), module, StripModule::OFF_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 228.5f), module, StripModule::OFF_PARAM));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 270.3f), module, StripModule::RAND_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 293.6f), module, StripModule::RAND_PARAM));

		addChild(createLightCentered<SmallLight<GreenRedLight>>(Vec(32.3f, 337.3f), module, StripModule::EXCLUDE_LIGHT));
		ExcludeButton* button = createParamCentered<ExcludeButton>(Vec(22.5f, 326.0f), module, StripModule::EXCLUDE_PARAM);
		button->module = module;
		addParam(button);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<StripModule>::appendContextMenu(menu);
		StripModule* module = dynamic_cast<StripModule*>(this->module);
		assert(module);
		menu->addChild(new MenuSeparator);

		struct OnModeMenuItem : MenuItem {
			struct OnModeItem : MenuItem {
				StripModule* module;
				ONMODE onMode;
				void onAction(const event::Action& e) override {
					module->onMode = onMode;
				}
				void step() override {
					rightText = module->onMode == onMode ? "✔" : "";
					MenuItem::step();
				}
			};

			StripModule* module;
			Menu* createChildMenu() override {
				Menu *menu = new Menu;
				menu->addChild(construct<OnModeItem>(&MenuItem::text, "Default", &OnModeItem::module, module, &OnModeItem::onMode, ONMODE::DEFAULT));
				menu->addChild(construct<OnModeItem>(&MenuItem::text, "Toggle", &OnModeItem::module, module, &OnModeItem::onMode, ONMODE::TOGGLE));
				menu->addChild(construct<OnModeItem>(&MenuItem::text, "High/Low", &OnModeItem::module, module, &OnModeItem::onMode, ONMODE::HIGHLOW));
				return menu;
			}
		};

		struct RandomParamsOnlyItem : MenuItem {
			StripModule* module;
			void onAction(const event::Action& e) override {
				module->randomParamsOnly ^= true;
			}
			void step() override {
				rightText = module->randomParamsOnly ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<OnModeMenuItem>(&MenuItem::text, "Port/Switch ON mode", &MenuItem::rightText, RIGHT_ARROW, &OnModeMenuItem::module, module));
		menu->addChild(construct<RandomParamsOnlyItem>(&MenuItem::text, "Randomize parameters only", &RandomParamsOnlyItem::module, module));
		menu->addChild(new MenuSeparator);

		struct CutGroupMenuItem : MenuItem {
			StripWidget* moduleWidget;
			void onAction(const event::Action& e) override {
				moduleWidget->groupCutClipboard();
			}
		};

		struct CopyGroupMenuItem : MenuItem {
			StripWidget* moduleWidget;
			void onAction(const event::Action& e) override {
				moduleWidget->groupCopyClipboard();
			}
		};

		struct PasteGroupMenuItem : MenuItem {
			StripWidget* moduleWidget;
			void onAction(const event::Action& e) override {
				moduleWidget->groupPasteClipboard();
			}
		};

		struct LoadGroupMenuItem : MenuItem {
			StripWidget* moduleWidget;
			void onAction(const event::Action& e) override {
				moduleWidget->groupLoadFileDialog(false);
			}
		};

		struct LoadReplaceGroupMenuItem : MenuItem {
			StripWidget* moduleWidget;
			void onAction(const event::Action& e) override {
				moduleWidget->groupLoadFileDialog(true);
			}
		};

		struct SaveGroupMenuItem : MenuItem {
			StripWidget* moduleWidget;
			void onAction(const event::Action& e) override {
				moduleWidget->groupSaveFileDialog();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Strip"));
		menu->addChild(construct<PresetMenuItem>(&MenuItem::text, "Preset", &PresetMenuItem::module, module, &PresetMenuItem::mw, this));
		menu->addChild(construct<CutGroupMenuItem>(&MenuItem::text, "Cut", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+X", &CutGroupMenuItem::moduleWidget, this));
		menu->addChild(construct<CopyGroupMenuItem>(&MenuItem::text, "Copy", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+C", &CopyGroupMenuItem::moduleWidget, this));
		menu->addChild(construct<PasteGroupMenuItem>(&MenuItem::text, "Paste", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+V", &PasteGroupMenuItem::moduleWidget, this));
		menu->addChild(construct<LoadGroupMenuItem>(&MenuItem::text, "Load", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+L", &LoadGroupMenuItem::moduleWidget, this));
		menu->addChild(construct<LoadReplaceGroupMenuItem>(&MenuItem::text, "Load with replace", &MenuItem::rightText, RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+L", &LoadReplaceGroupMenuItem::moduleWidget, this));
		menu->addChild(construct<SaveGroupMenuItem>(&MenuItem::text, "Save as", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+S", &SaveGroupMenuItem::moduleWidget, this));
	}
};

} // namespace Strip
} // namespace StoermelderPackOne

Model* modelStrip = createModel<StoermelderPackOne::Strip::StripModule, StoermelderPackOne::Strip::StripWidget>("Strip");