#include "Strip.hpp"
#include "digital.hpp"
#include "helpers/TaskWorker.hpp"

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
	ChangeTrigger<float> highLowTrigger;
	dsp::SchmittTrigger randTrigger;

	dsp::ClockDivider lightDivider;

	TaskWorker taskWorker;

	StripModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(MODE_PARAM, 0, 1, 0, "Toggle left/right mode");
		configInput(ON_INPUT, "Strip on/toggle trigger");
		configParam<TriggerParamQuantity>(ON_PARAM, 0, 1, 0, "Switch/toggle strip on");
		configInput(OFF_INPUT, "Strip off trigger");
		configParam<TriggerParamQuantity>(OFF_PARAM, 0, 1, 0, "Switch strip off");
		configInput(RAND_INPUT, "Strip randomization trigger");
		configParam<TriggerParamQuantity>(RAND_PARAM, 0, 1, 0, "Randomize strip");
		configParam(EXCLUDE_PARAM, 0, 1, 0, "Parameter randomization include/exclude");

		lightDivider.setDivision(1024);
		onReset();
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
				if (highLowTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
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

	void groupDisable(bool val, bool useHistory) {
		//taskWorker.work([=]() { groupDisableWorker(val, useHistory); });
		taskWorker.work([=]() { groupDisableWorker(val, false); });
	}

	/** 
	 * Disables/enables all modules of the current strip.
	 * To be called from engine-thread only.
	 */
	void groupDisableWorker(bool val, bool useHistory) {
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
				if (!m->rightExpander.module) break;
				// This is what "Module.hpp" says about bypass:
				// "Module subclasses should not read/write this variable."
				APP->engine->bypassModule(m->rightExpander.module, val);

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
				if (!m->leftExpander.module) break;
				// This is what "Module.hpp" says about bypass:
				// "Module subclasses should not read/write this variable."
				APP->engine->bypassModule(m->leftExpander.module, val);

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
				if (!mw) return;
				for (ParamWidget* param : mw->getParams()) {
					ParamQuantity* paramQuantity = param->getParamQuantity();
					if (!paramQuantity || !paramQuantity->randomizeEnabled) continue;

					switch (randomExcl) {
						case RANDOMEXCL::NONE:
							paramQuantity->randomize();
							break;
						case RANDOMEXCL::EXC:
							if (excludedParams.find(std::make_tuple(m->rightExpander.moduleId, paramQuantity->paramId)) == excludedParams.end())
								paramQuantity->randomize();
							break;
						case RANDOMEXCL::INC:
							if (excludedParams.find(std::make_tuple(m->rightExpander.moduleId, paramQuantity->paramId)) != excludedParams.end())
								paramQuantity->randomize();
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
				if (!mw) return;
				for (ParamWidget* param : mw->getParams()) {
					ParamQuantity* paramQuantity = param->getParamQuantity();
					if (!paramQuantity || !paramQuantity->randomizeEnabled) continue;

					switch (randomExcl) {
						case RANDOMEXCL::NONE:
							paramQuantity->randomize();
							break;
						case RANDOMEXCL::EXC:
							if (excludedParams.find(std::make_tuple(m->leftExpander.moduleId, paramQuantity->paramId)) == excludedParams.end())
								paramQuantity->randomize();
							break;
						case RANDOMEXCL::INC:
							if (excludedParams.find(std::make_tuple(m->leftExpander.moduleId, paramQuantity->paramId)) != excludedParams.end())
								paramQuantity->randomize();
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
		if (touchedParam) {
			ParamQuantity* paramQuantity = touchedParam->getParamQuantity();
			if (paramQuantity && paramQuantity->module != module) {
				int64_t moduleId = paramQuantity->module->id;
				int paramId = paramQuantity->paramId;
				groupExcludeParam(moduleId, paramId);
			}
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
						ParamQuantity* paramQuantity = param->getParamQuantity();
						if (paramQuantity && paramQuantity->paramId == paramId) {
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
						ParamQuantity* paramQuantity = param->getParamQuantity();
						if (paramQuantity && paramQuantity->paramId == paramId) {
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
			ParamQuantity* paramQuantity = paramWidget->getParamQuantity();
			if (!paramQuantity) continue;

			std::string text = "Parameter \"";
			text += moduleWidget->model->name;
			text += " ";
			text += paramQuantity->getLabel();
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
		menu->addChild(createSubmenuItem("Port/Switch ON mode", "",
			[=](Menu* menu) {
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Default", &module->onMode, ONMODE::DEFAULT));
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Toggle", &module->onMode, ONMODE::TOGGLE));
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("High/Low", &module->onMode, ONMODE::HIGHLOW));
			}
		));
		menu->addChild(createBoolPtrMenuItem("Randomize parameters only", "", &module->randomParamsOnly));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Strip"));
		menu->addChild(construct<PresetMenuItem>(&MenuItem::text, "Preset", &PresetMenuItem::module, module, &PresetMenuItem::mw, this));
		menu->addChild(createMenuItem("Cut", RACK_MOD_SHIFT_NAME "+X", [=]() { groupCutClipboard(); }));
		menu->addChild(createMenuItem("Copy", RACK_MOD_SHIFT_NAME "+C", [=]() { groupCopyClipboard(); }));
		menu->addChild(createMenuItem("Paste", RACK_MOD_SHIFT_NAME "+V", [=]() { groupPasteClipboard(); }));
		menu->addChild(createMenuItem("Load", RACK_MOD_SHIFT_NAME "+L", [=]() { groupLoadFileDialog(false); }));
		menu->addChild(createMenuItem("Load with replace", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+L", [=]() { groupLoadFileDialog(true); }));
		menu->addChild(createMenuItem("Save as", RACK_MOD_SHIFT_NAME "+S", [=]() { groupSaveFileDialog(); }));
	}
};

} // namespace Strip
} // namespace StoermelderPackOne

Model* modelStrip = createModel<StoermelderPackOne::Strip::StripModule, StoermelderPackOne::Strip::StripWidget>("Strip");