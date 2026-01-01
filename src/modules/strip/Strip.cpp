#include "Strip.hpp"
#include "../../utils/digital.hpp"
#include "../../utils/TaskWorker.hpp"
#include <atomic>
#include <memory>

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

enum class TASKQUEUECMD {
	RANDOMIZE,
	BYPASS_ON,
	BYPASS_OFF,
	EXCLUDED_PARAMS_CLEANUP,
	EXCLUDED_PARAMS_CLEAR,
	EXCLUDED_PARAMS_ADD,
	EXCLUDED_PARAMS_REMOVE,
	EXCLUDED_PARAMS_LOAD
};


struct StripModule : StripModuleBase, StripIdFixModule {
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

	/** Last known bypass state of the strip */
	std::atomic<bool> lastBypassState{false};

	/** [Stored to JSON] */
	/*	This is owned be the engine thread */
	std::set<std::tuple<int64_t, int>> excludedParams;
	/*  Snapshot published for UI thread: shared_ptr to immutable set. Use std::atomic_load/store for atomic access. */
	std::shared_ptr<const std::set<std::tuple<int64_t, int>>> excludedParamsPtr;
	/* 	Snapshot published by UI Thread, only used for loading presets */
	std::shared_ptr<const std::set<std::tuple<int64_t, int>>> excludedParamsPtrUi;

	/*  Indicates that learn mode is ready - only used for LED */
	std::atomic<bool> excludeLearn{false};

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

	ClockDividerEx lightDivider;

	TaskWorker taskWorker;
	dsp::RingBuffer<std::tuple<TASKQUEUECMD, int64_t, int>, 16> taskQueue;

	StripModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Toggle left/right mode");
		configInput(ON_INPUT, "Strip on/toggle trigger");
		configSwitch(ON_PARAM, 0.f, 1.f, 0.f, "Switch/toggle strip on");
		configInput(OFF_INPUT, "Strip off trigger");
		configSwitch(OFF_PARAM, 0.f, 1.f, 0.f, "Switch strip off");
		configInput(RAND_INPUT, "Strip randomization trigger");
		configSwitch(RAND_PARAM, 0.f, 1.f, 0.f, "Randomize strip");
		configSwitch(EXCLUDE_PARAM, 0.f, 1.f, 0.f, "Parameter randomization include/exclude");
		onReset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onReset() override {
		randomParamsOnly = false;
		presetLoadReplace = false;
		// Initialize snapshot to empty set so UI can read safely immediately
		excludedParams.clear();
		std::atomic_store(&excludedParamsPtr, std::make_shared<const std::set<std::tuple<int64_t, int>>>());
	}

	void process(const ProcessArgs& args) override {
		if (modeTrigger.process(params[MODE_PARAM].getValue())) {
			mode = (MODE)(((int)mode + 1) % 3);
			lastBypassState.store(true);
		}

		if (offPTrigger.process(params[OFF_PARAM].getValue() + inputs[OFF_INPUT].getVoltage())) {
			groupBypass(true);
		}

		switch (onMode) {
			case ONMODE::DEFAULT:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					groupBypass(false);
				break;
			case ONMODE::TOGGLE:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
				groupBypass(!lastBypassState.load());
				break;
			case ONMODE::HIGHLOW:
				if (highLowTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					groupBypass(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage() < 1.f);
				break;
		}

		if (randTrigger.process(inputs[RAND_INPUT].getVoltage())) {
			groupRandomize();
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			lights[RIGHT_LIGHT].setBrightness(mode == MODE::LEFTRIGHT || mode == MODE::RIGHT);
			lights[LEFT_LIGHT].setBrightness(mode == MODE::LEFTRIGHT || mode == MODE::LEFT);

			lights[EXCLUDE_LIGHT + 0].setBrightness(!excludeLearn.load() && excludedParams.size() > 0 ? 1.f : 0.f);
			lights[EXCLUDE_LIGHT + 1].setBrightness(excludeLearn.load() ? 1.f : 0.f);
		}

		processTaskQueue();
	}

	/** 
	 * Processes all pending tasks in the task queue.
	 * To be called from the engine thread only.
	 */
	void processTaskQueue() {	
		while (!taskQueue.empty()) {
			auto task = taskQueue.shift();
			switch (std::get<0>(task)) {
				case TASKQUEUECMD::RANDOMIZE:
					groupRandomize();
					break;
				case TASKQUEUECMD::BYPASS_ON:
					groupBypass(true);
					break;
				case TASKQUEUECMD::BYPASS_OFF:
					groupBypass(false);
					break;
				case TASKQUEUECMD::EXCLUDED_PARAMS_CLEANUP:
					groupExcludeCleanup();
					break;
				case TASKQUEUECMD::EXCLUDED_PARAMS_CLEAR:
					groupExcludeClear();
					break;
				case TASKQUEUECMD::EXCLUDED_PARAMS_ADD:
					groupExcludeAdd(std::get<1>(task), std::get<2>(task));
					break;
				case TASKQUEUECMD::EXCLUDED_PARAMS_REMOVE:
					groupExcludeRemove(std::get<1>(task), std::get<2>(task));
					break;
				case TASKQUEUECMD::EXCLUDED_PARAMS_LOAD:
					groupExcludeLoad();
					break;
			}
		}
	}

	/** 
	 * Requests setting bypass state of all modules of the current strip.
	 * Adds a undo history entry.
	 * Called from the UI-thread to schedule the execution on the dsp thread.
	 */
	void groupBypassRequest(bool val) {
		history::ComplexAction* complexAction;	
		complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP bypass";
		APP->history->push(complexAction);

		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;
				if (!m->rightExpander.module) break;

				// history::ModuleBypass
				history::ModuleBypass* h = new history::ModuleBypass;
				h->moduleId = m->rightExpander.module->id;
				h->bypassed = m->rightExpander.module->isBypassed();
				complexAction->push(h);

				m = m->rightExpander.module;
			}
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				if (!m->leftExpander.module) break;

				// history::ModuleBypass
				history::ModuleBypass* h = new history::ModuleBypass;
				h->moduleId = m->leftExpander.module->id;
				h->bypassed = m->leftExpander.module->isBypassed();
				complexAction->push(h);

				m = m->leftExpander.module;
			}
		}

		taskQueue.push(std::make_tuple(val ? TASKQUEUECMD::BYPASS_ON : TASKQUEUECMD::BYPASS_OFF, -1, -1));
	}

	/** 
	 * Sets bypass state of all modules of the current strip.
	 * To be called from the engine thread only.
	 */
	void groupBypass(bool val) {
		taskWorker.work([=]() { groupBypassWorker(val); });
	}

	/** 
	 * Sets bypass state of all modules of the current strip.
	 * To be called from a worker-thread only, as the engine will lock.
	 */
	void groupBypassWorker(bool val) {
		if (lastBypassState.load() == val) return;
		lastBypassState.store(val);

		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;
				if (!m->rightExpander.module) break;
				// This is what "Engine.hpp" says about bypass:
				// Sets the bypassed state and triggers a BypassEvent or UnBypassEvent of the given Module.
				// Exclusively locks.
				APP->engine->bypassModule(m->rightExpander.module, val);
				m = m->rightExpander.module;
			}
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				if (!m->leftExpander.module) break;
				// This is what "Engine.hpp" says about bypass:
				// Sets the bypassed state and triggers a BypassEvent or UnBypassEvent of the given Module.
				// Exclusively locks.
				APP->engine->bypassModule(m->leftExpander.module, val);
				m = m->leftExpander.module;
			}
		}
	}

	/** 
	 * Randomizes all modules of the current strip. Adds a undo history entry.
	 * Called from the UI-thread to schedule the execution on the dsp thread.
	 */
	void groupRandomizeRequest() {
		history::ComplexAction* complexAction = nullptr;	
		complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP randomize";
		APP->history->push(complexAction);

		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;

				history::ModuleChange* h = nullptr;
				// history::ModuleChange
				h = new history::ModuleChange;
				h->moduleId = m->rightExpander.moduleId;
				h->oldModuleJ = m->rightExpander.module->toJson();
				complexAction->push(h);
				h->newModuleJ = m->rightExpander.module->toJson();

				m = m->rightExpander.module;
			}
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;

				history::ModuleChange* h;
				// history::ModuleChange
				h = new history::ModuleChange;
				h->moduleId = m->leftExpander.moduleId;
				h->oldModuleJ = m->leftExpander.module->toJson();
				complexAction->push(h);
				h->newModuleJ = m->leftExpander.module->toJson();

				m = m->leftExpander.module;
			}
		}

		// Notify dsp thread to randomize the strip
		taskQueue.push(std::make_tuple(TASKQUEUECMD::RANDOMIZE, -1, -1));
	}

	/** 
	 * Randomizes all modules of the current strip.
	 * To be called from engine-thread only.
	 */
	void groupRandomize() {
		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;
				Module* mNext = m->rightExpander.module;
				if (!mNext) mNext = APP->engine->getModule_NoLock(m->rightExpander.moduleId);

				for (int paramId = 0; paramId < mNext->getNumParams(); paramId++) {
					ParamQuantity* paramQuantity = mNext->getParamQuantity(paramId);
					if (!paramQuantity || !paramQuantity->randomizeEnabled) continue;

					switch (randomExcl) {
						case RANDOMEXCL::NONE:
							paramQuantity->randomize();
							break;
						case RANDOMEXCL::EXC:
							if (excludedParams.find(std::make_tuple(mNext->getId(), paramId)) == excludedParams.end())
								paramQuantity->randomize();
							break;
						case RANDOMEXCL::INC:
							if (excludedParams.find(std::make_tuple(mNext->getId(), paramId)) != excludedParams.end())
								paramQuantity->randomize();
							break;
					}
				}
				if (!randomParamsOnly) {
					mNext->onRandomize();
				}

				m = mNext;
			}
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				Module* mNext = m->leftExpander.module;
				if (!mNext) mNext = APP->engine->getModule_NoLock(m->leftExpander.moduleId);

				for (int paramId = 0; paramId < mNext->getNumParams(); paramId++) {
					ParamQuantity* paramQuantity = mNext->getParamQuantity(paramId);
					if (!paramQuantity || !paramQuantity->randomizeEnabled) continue;

					switch (randomExcl) {
						case RANDOMEXCL::NONE:
							paramQuantity->randomize();
							break;
						case RANDOMEXCL::EXC:
							if (excludedParams.find(std::make_tuple(mNext->getId(), paramId)) == excludedParams.end())
								paramQuantity->randomize();
							break;
						case RANDOMEXCL::INC:
							if (excludedParams.find(std::make_tuple(mNext->getId(), paramId)) != excludedParams.end())
								paramQuantity->randomize();
							break;
					}
				}
				if (!randomParamsOnly) {
					mNext->onRandomize();
				}

				m = mNext;
			}
		}
	}

	/**
	 * Requests cleanup of the currently list of excluded parameters from modules that are no longer 
	 * within the current strip. 
	 * Called from the UI-thread to schedule the execution on the dsp thread.
	 */
	void groupExcludeCleanupRequest() {
		taskQueue.push(std::make_tuple(TASKQUEUECMD::EXCLUDED_PARAMS_CLEANUP, -1, -1));
	}

	/**
	 * Cleans the currently list of excluded parameters from modules that are no longer 
	 * within the current strip. Called on every frame to ensure the excluded parameter list
	 * matches the modules in the strip.
	 * To be called from a engine thread only.
	 */
	void groupExcludeCleanup() {
		if (excludedParams.size() == 0)
			return;

		std::map<int, Module*> modules;
		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;
				modules[m->rightExpander.moduleId] = m;
				m = m->rightExpander.module;
			}
		}
		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				modules[m->leftExpander.moduleId] = m;
				m = m->leftExpander.module;
			}
		}

		std::vector<std::tuple<int, int>> toBeDeleted;
		for (auto it : excludedParams) {
			int64_t moduleId = std::get<0>(it);
			auto m = modules.find(moduleId);
			if (m == modules.end()) {
				toBeDeleted.push_back(it);
			}
		}

		if (toBeDeleted.size() > 0) {
			for (auto it : toBeDeleted) {
				excludedParams.erase(it);
			}
			// Publish updated snapshot for UI readers
			std::atomic_store(&excludedParamsPtr, std::make_shared<const std::set<std::tuple<int64_t, int>>>(excludedParams));
		}
	}

	/** 
	 * Requests clearing the list of excluded parameters.
	 * Called from the UI-thread to schedule the execution on the dsp thread.
	 */
	void groupExcludeClearRequest() {
		taskQueue.push(std::make_tuple(TASKQUEUECMD::EXCLUDED_PARAMS_CLEAR, -1, -1));
	}

	/** 
	 * Clears the list of excluded parameters.
	 * To be called from engine-thread only.
	 */
	void groupExcludeClear() {
		excludedParams.clear();
		std::atomic_store(&excludedParamsPtr, std::make_shared<const std::set<std::tuple<int64_t, int>>>());
	}

	/** 
	 * Requests adding a parameter to the randomization exclusion list.
	 * Called from the UI-thread to schedule the execution on the dsp thread.
	 */
	void groupExcludeAddRequest(int64_t moduleId, int paramId) {
		taskQueue.push(std::make_tuple(TASKQUEUECMD::EXCLUDED_PARAMS_ADD, moduleId, paramId));
	}

	/** 
	 * Adds a parameter to the randomization exclusion list.
	 * To be called from engine-thread only.
	 */
	void groupExcludeAdd(int64_t moduleId, int paramId) {
		if (mode == MODE::LEFTRIGHT || mode == MODE::RIGHT) {
			Module* m = this;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;
				// This should never be NULL, but just in case...
				Module* mNext = m->rightExpander.module;
				if (!mNext) mNext = APP->engine->getModule(m->rightExpander.moduleId);
				if (mNext->getId() == moduleId) {
					excludedParams.insert(std::make_tuple(moduleId, paramId));
					// Publish updated snapshot for UI readers
					std::atomic_store(&excludedParamsPtr, std::make_shared<const std::set<std::tuple<int64_t, int>>>(excludedParams));
					return;
				}
				m = mNext;
			}
		}

		if (mode == MODE::LEFTRIGHT || mode == MODE::LEFT) {
			Module* m = this;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				Module* mNext = m->leftExpander.module;
				// This should never be NULL, but just in case...
				if (!mNext) mNext = APP->engine->getModule(m->leftExpander.moduleId);
				if (mNext->getId() == moduleId) {
					excludedParams.insert(std::make_tuple(moduleId, paramId));
					// Publish updated snapshot for UI readers
					std::atomic_store(&excludedParamsPtr, std::make_shared<const std::set<std::tuple<int64_t, int>>>(excludedParams));
					return;
				}
				m = mNext;
			}
		}
	}

	/** 
	 * Requests removal a parameter from the randomization exclusion list.
	 * Called from the UI-thread to schedule the execution on the dsp thread.
	 */
	void groupExcludeRemoveRequest(int64_t moduleId, int paramId) {
		taskQueue.push(std::make_tuple(TASKQUEUECMD::EXCLUDED_PARAMS_REMOVE, moduleId, paramId));
	}

	/** 
	 * Removes a parameter from the randomization exclusion list.
	 * To be called from engine-thread only.
	 */
	void groupExcludeRemove(int64_t moduleId, int paramId) {
		excludedParams.erase(std::make_tuple(moduleId, paramId));
		std::atomic_store(&excludedParamsPtr, std::make_shared<const std::set<std::tuple<int64_t, int>>>(excludedParams));
	}

	/** 
	 * Requests loading the excluded parameters from the UI-published snapshot.
	 * Called from the UI-thread to schedule the execution on the dsp thread.
	 */
	void groupExcludeLoadRequest() {
		taskQueue.push(std::make_tuple(TASKQUEUECMD::EXCLUDED_PARAMS_LOAD, -1, -1));
	}

	/** 
	 * Loads the excluded parameters from the UI-published snapshot.
	 * To be called from engine-thread only.
	 */
	void groupExcludeLoad() {
		auto snapUi = std::atomic_load(&excludedParamsPtrUi);
		if (snapUi) {
			excludedParams = *snapUi;
			// Publish updated snapshot for UI readers
			std::atomic_store(&excludedParamsPtr, std::make_shared<const std::set<std::tuple<int64_t, int>>>(excludedParams));
			std::atomic_store(&excludedParamsPtrUi, std::shared_ptr<const std::set<std::tuple<int64_t, int>>>{});
		}
	}

	/** 
	 * Serialization
	 * This is called from the UI-thread only, access to excludedParams must be synchronized.
	 */
	json_t* dataToJson() override {
		json_t* rootJ = StripModuleBase::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "onMode", json_integer((int)onMode));

		// Use atomic snapshot published by the engine thread
		auto snap = std::atomic_load(&excludedParamsPtr);
		json_t* excludedParamsJ = json_array();
		if (snap) {
			for (auto t : *snap) {
				json_t* excludedParamJ = json_object();
				json_object_set_new(excludedParamJ, "moduleId", json_integer(std::get<0>(t)));
				json_object_set_new(excludedParamJ, "paramId", json_integer(std::get<1>(t)));
				json_array_append_new(excludedParamsJ, excludedParamJ);
			}
		}
		json_object_set_new(rootJ, "excludedParams", excludedParamsJ);

		json_object_set_new(rootJ, "randomExcl", json_integer((int)randomExcl));
		json_object_set_new(rootJ, "randomParamsOnly", json_boolean(randomParamsOnly));
		json_object_set_new(rootJ, "presetLoadReplace", json_boolean(presetLoadReplace));
		return rootJ;
	}

	/** 
	 * Deserialization
	 * This is called from the UI-thread only, access to excludedParams must be synchronized.
	 */
	void dataFromJson(json_t* rootJ) override {
		StripModuleBase::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* onModeJ = json_object_get(rootJ, "onMode");
		onMode = (ONMODE)json_integer_value(onModeJ);

		json_t* excludedParamsJ = json_object_get(rootJ, "excludedParams");
		if (excludedParamsJ) {
			auto snap = std::set<std::tuple<int64_t, int>>();
			json_t* excludedParamJ;
			size_t i; 
			json_array_foreach(excludedParamsJ, i, excludedParamJ) {
				json_t* moduleIdJ = json_object_get(excludedParamJ, "moduleId");
				json_t* paramIdJ = json_object_get(excludedParamJ, "paramId");
				if (!(moduleIdJ && paramIdJ)) 
					continue;
				int64_t moduleId = json_integer_value(moduleIdJ);
				int paramId = json_integer_value(paramIdJ);
				moduleId = idFix(moduleId);
				snap.insert(std::make_tuple(moduleId, paramId));
			}
			// Publish snapshot for engine thread
			std::atomic_store(&excludedParamsPtrUi, std::make_shared<const std::set<std::tuple<int64_t, int>>>(snap));
			groupExcludeLoadRequest();
		}
	
		json_t* randomExclJ = json_object_get(rootJ, "randomExcl");
		randomExcl = (RANDOMEXCL)json_integer_value(randomExclJ);
		json_t* randomParamsOnlyJ = json_object_get(rootJ, "randomParamsOnly");
		if (randomParamsOnlyJ) randomParamsOnly = json_boolean_value(randomParamsOnlyJ);
		json_t* presetLoadReplaceJ = json_object_get(rootJ, "presetLoadReplace");
		if (presetLoadReplaceJ) presetLoadReplace = json_boolean_value(presetLoadReplaceJ);
		idFixClearMap();
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
				module->groupExcludeClearRequest();
				pressed = false;
			}
		}
		
		module->excludeLearn.store(learn);
		TL1105::step();
		module->groupExcludeCleanupRequest();
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module) return;
		if (!learn) return;

		DEFER({
			toggleParamLearn();
		});

		// Check if a ParamWidget was touched
		ParamWidget* touchedParam = APP->scene->rack->getTouchedParam();
		if (touchedParam) {
			ParamQuantity* paramQuantity = touchedParam->getParamQuantity();
			if (paramQuantity && paramQuantity->module != module) {
				int64_t moduleId = paramQuantity->module->id;
				int paramId = paramQuantity->paramId;
				module->groupExcludeAddRequest(moduleId, paramId);
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
					toggleParamLearn();
					pressed = false;
				}
			}
			TL1105::onButton(e);
		}
	}

	void toggleParamLearn() {
		learn ^= true;
		if (learn) {
			GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
			glfwSetCursor(APP->window->win, cursor);
		}
		else {
			glfwSetCursor(APP->window->win, NULL);
		}
		APP->scene->rack->setTouchedParam(NULL);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("Parameter randomization"));
		menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem("Mode",
			{
				{ RANDOMEXCL::NONE, "All" },
				{ RANDOMEXCL::EXC, "Exclude" },
				{ RANDOMEXCL::INC, "Include" }
			},
			&module->randomExcl
		));
		menu->addChild(createMenuItem("Learn", "short press", [this]() {
			APP->event->setSelectedWidget(this);
			toggleParamLearn();
		}));
		menu->addChild(createMenuItem("Clear", "long press", [this]() {
			module->groupExcludeClearRequest();
		}));

		// Use atomic snapshot published by the engine thread to avoid racing with engine mutations
		auto snap = std::atomic_load(&module->excludedParamsPtr);

		if (!snap || snap->size() == 0)
			return;

		menu->addChild(new MenuSeparator());
		for (auto it : *snap) {
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

			menu->addChild(createSubmenuItem(text, "", [this, it](Menu* menu) {
				menu->addChild(createMenuItem("Remove from list", "", [this, it]() {
					module->groupExcludeRemoveRequest(std::get<0>(it), std::get<1>(it));
				}));
			}));
		}
	}
};



struct StripWidget : StripWidgetBase<StripModule> {
	template<bool IS_ON>
	struct BypassTL1105 : TL1105 {
		void onButton(const event::Button& e) override {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				// Notify dsp thread to set	the bypass state of the strip
				dynamic_cast<StripModule*>(module)->groupBypassRequest(IS_ON);	
				e.consume(this);
			}
			TL1105::onButton(e);
		}
	};

	struct RandTL1105 : TL1105 {
		void onButton(const event::Button& e) override {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				// Notify dsp thread to randomize the strip
				dynamic_cast<StripModule*>(module)->groupRandomizeRequest();	
				e.consume(this);
			}
			TL1105::onButton(e);
		}
	};

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
		addParam(createParamCentered<BypassTL1105<false>>(Vec(22.5f, 162.7f), module, StripModule::ON_PARAM));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 205.1f), module, StripModule::OFF_INPUT));
		addParam(createParamCentered<BypassTL1105<true>>(Vec(22.5f, 228.5f), module, StripModule::OFF_PARAM));
		
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 270.3f), module, StripModule::RAND_INPUT));
		addParam(createParamCentered<RandTL1105>(Vec(22.5f, 293.6f), module, StripModule::RAND_PARAM));

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