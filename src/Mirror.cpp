#include "plugin.hpp"
#include "helpers/StripIdFixModule.hpp"
#include "helpers/TaskProcessor.hpp"
#include <plugin.hpp>

namespace StoermelderPackOne {
namespace Mirror {

struct MirrorModule : Module, StripIdFixModule {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT_CV, 8),
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	std::string sourcePluginSlug;
	/** [Stored to JSON] */
	std::string sourcePluginName;
	/** [Stored to JSON] */
	std::string sourceModelSlug;
	/** [Stored to JSON] */
	std::string sourceModelName;
	/** [Stored to JSON] */
	int64_t sourceModuleId;
	/** [Stored to JSON] */
	std::vector<int64_t> targetModuleIds;

	/** [Stored to JSON] */
	bool audioRate;
	/** [Stored to JSON] */
	bool mappingIndicatorHidden = false;

	bool inChange = false;

	/** [Stored to JSON] */
	std::vector<ParamHandle*> sourceHandles;
	/** [Stored to JSON] */
	std::vector<ParamHandle*> targetHandles;
	/** [Stored to JSON] */
	int cvParamId[8];

	dsp::ClockDivider processDivider;
	dsp::ClockDivider handleDivider;

	TaskProcessor<> taskProcessorUi;

	MirrorModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 8; i++) {
			configInput(INPUT_CV + i, string::f("CV %i", i + 1));
		}

		processDivider.setDivision(32);
		handleDivider.setDivision(4096);
		reset(true);
	}

	~MirrorModule() {
		for (ParamHandle* sourceHandle : sourceHandles) {
			APP->engine->removeParamHandle(sourceHandle);
			delete sourceHandle;
		}
		for (ParamHandle* targetHandle : targetHandles) {
			APP->engine->removeParamHandle(targetHandle);
			delete targetHandle;
		}
	}

	void onReset() override {
		reset(false, true);
	}

	void reset(bool stateOnly, bool createUiTask = false) {
		if (!stateOnly) {
			inChange = true;
			auto cleanHandles = [=]() {
				for (ParamHandle* sourceHandle : sourceHandles) {
					APP->engine->removeParamHandle(sourceHandle);
					delete sourceHandle;
				}
				for (ParamHandle* targetHandle : targetHandles) {
					APP->engine->removeParamHandle(targetHandle);
					delete targetHandle;
				}

				sourceHandles.clear();
				targetHandles.clear();
				inChange = false;
			};

			// Enqueue on the UI-thread as the engine's mutex could already be locked
			if (createUiTask) {
				taskProcessorUi.enqueue(cleanHandles);
			}
			else {
				cleanHandles();
			}
		}

		for (int i = 0; i < 8; i++) {
			cvParamId[i] = -1;
		}

		targetModuleIds.clear();

		sourcePluginSlug = "";
		sourcePluginName = "";
		sourceModelSlug = "";
		sourceModelName = "";
		sourceModuleId = -1;
		audioRate = false;
	}

	void process(const ProcessArgs& args) override {
		if (inChange) return;

		// Sync source paramId to target handles in case a parameter has been unmapped
		if (handleDivider.process()) {
			for (size_t i = 0; i < sourceHandles.size(); i++) {
				ParamHandle* sourceHandle = sourceHandles[i];
				sourceHandle->color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0x40, 0xff, 0xff);

				size_t j = i;
				std::list<std::function<void()>> handleList;
				while (j < targetHandles.size()) {
					ParamHandle* targetHandle = targetHandles[j];
					targetHandle->color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0xff, 0x40, 0xff);
					if (sourceHandle->moduleId < 0 && targetHandle->moduleId >= 0) {
						// Unmap target parameter
						// This might cause a deadlock as the engine's mutex could already be locked
						handleList.push_back([targetHandle]() {
							APP->engine->updateParamHandle(targetHandle, -1, 0, true);
						});
					}

					j += sourceHandles.size();
				}

				// Enqueue on the UI-thread for cleaning up ParamHandles
				if (handleList.size() > 0) {
					taskProcessorUi.enqueue([handleList]() {
						for (std::function<void()> f : handleList) f();
					});
				}
			}
		}

		if (audioRate || processDivider.process()) {
			for (int i = 0; i < 8; i++) {
				if (cvParamId[i] >= 0 && inputs[INPUT_CV + i].isConnected()) {
					float v = clamp(inputs[INPUT_CV + i].getVoltage(), 0.f, 10.f);
					ParamHandle* sourceHandle = sourceHandles[cvParamId[i]];
					ParamQuantity* sourceParamQuantity = getParamQuantity(sourceHandle);
					if (sourceParamQuantity)
						sourceParamQuantity->setScaledValue(v / 10.f);
					else 
						cvParamId[i] = -1;
				}
			}

			for (ParamHandle* sourceHandle : sourceHandles) {
				ParamQuantity* sourceParamQuantity = getParamQuantity(sourceHandle);
				if (!sourceParamQuantity) continue;

				float v = sourceParamQuantity->getValue();

				int i = sourceHandle->paramId;
				while (i < (int)targetHandles.size()) {
					ParamHandle* targetHandle = targetHandles[i];
					ParamQuantity* targetParamQuantity = getParamQuantity(targetHandle);
					if (targetParamQuantity)
						targetParamQuantity->setValue(v);

					i += sourceHandles.size();
				}
			}
		}
	}

	ParamQuantity* getParamQuantity(ParamHandle* handle) {
		if (handle->moduleId < 0)
			return NULL;
		// Get Module
		Module* module = handle->module;
		if (!module)
			return NULL;
		// Get ParamQuantity
		int paramId = handle->paramId;
		ParamQuantity* paramQuantity = module->paramQuantities[paramId];
		if (!paramQuantity)
			return NULL;
		if (!paramQuantity->isBounded())
			return NULL;
		return paramQuantity;
	}

	void bindToSource() {
		// Always called from the UI-thread
		Expander* exp = &leftExpander;
		if (exp->moduleId < 0) return;

		inChange = true;
		reset(false, false);
		Module* m = exp->module;
		sourcePluginSlug = m->model->plugin->slug;
		sourcePluginName = m->model->plugin->name;
		sourceModelSlug = m->model->slug;
		sourceModelName = m->model->name;
		sourceModuleId = m->id;

		for (size_t i = 0; i < m->params.size(); i++) {
			ParamHandle* sourceHandle = new ParamHandle;
			sourceHandle->text = "stoermelder MIRROR";
			APP->engine->addParamHandle(sourceHandle);
			APP->engine->updateParamHandle(sourceHandle, m->id, i, true);
			sourceHandles.push_back(sourceHandle);
		}

		inChange = false;
	}

	void bindToTarget() {
		// Always called from the UI-thread
		Expander* exp = &rightExpander;
		if (exp->moduleId < 0) return;
		// This is called from the UI-thread, so get the Module from the scene
		Module* m = APP->scene->rack->getModule(exp->moduleId)->getModule();
		if (sourcePluginSlug != m->model->plugin->slug || sourceModelSlug != m->model->slug) return;

		inChange = true;
		for (ParamHandle* sourceHandle : sourceHandles) {
			ParamHandle* targetHandle = new ParamHandle;
			targetHandle->text = "stoermelder MIRROR";
			APP->engine->addParamHandle(targetHandle);
			APP->engine->updateParamHandle(targetHandle, m->id, sourceHandle->paramId, true);
			targetHandles.push_back(targetHandle);
		}

		targetModuleIds.push_back(m->id);
		inChange = false;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));

		json_object_set_new(rootJ, "sourcePluginSlug", json_string(sourcePluginSlug.c_str()));
		json_object_set_new(rootJ, "sourcePluginName", json_string(sourcePluginName.c_str()));
		json_object_set_new(rootJ, "sourceModelSlug", json_string(sourceModelSlug.c_str()));
		json_object_set_new(rootJ, "sourceModelName", json_string(sourceModelName.c_str()));
		json_object_set_new(rootJ, "sourceModuleId", json_integer(sourceModuleId));

		json_t* sourceMapsJ = json_array();
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			json_t* sourceMapJ = json_object();
			json_object_set_new(sourceMapJ, "moduleId", json_integer(sourceHandles[i]->moduleId));
			json_object_set_new(sourceMapJ, "paramId", json_integer(sourceHandles[i]->paramId));
			json_array_append_new(sourceMapsJ, sourceMapJ);
		}
		json_object_set_new(rootJ, "sourceMaps", sourceMapsJ);

		json_t* targetMapsJ = json_array();
		for (size_t i = 0; i < targetHandles.size(); i++) {
			json_t* targetMapJ = json_object();
			json_object_set_new(targetMapJ, "moduleId", json_integer(targetHandles[i]->moduleId));
			json_object_set_new(targetMapJ, "paramId", json_integer(targetHandles[i]->paramId));
			json_array_append_new(targetMapsJ, targetMapJ);
		}
		json_object_set_new(rootJ, "targetMaps", targetMapsJ);

		json_t* cvInputsJ = json_array();
		for (int i = 0; i < 8; i++) {
			json_t* cvInputJ = json_object();
			json_object_set_new(cvInputJ, "paramId", json_integer(cvParamId[i]));
			json_array_append_new(cvInputsJ, cvInputJ);
		}
		json_object_set_new(rootJ, "cvInputs", cvInputsJ);

		json_t* targetModulesJ = json_array();
		for (size_t i = 0; i < targetModuleIds.size(); i++) {
			json_t* targetModuleJ = json_object();
			json_object_set_new(targetModuleJ, "moduleId", json_integer(targetModuleIds[i]));
			json_array_append_new(targetModulesJ, targetModuleJ);
		}
		json_object_set_new(rootJ, "targetModules", targetModulesJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// Hack for preventing duplicating this module
		//if (APP->engine->getModule(id) != NULL && !idFixHasMap()) return;

		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		audioRate = json_boolean_value(json_object_get(rootJ, "audioRate"));
		mappingIndicatorHidden = json_boolean_value(json_object_get(rootJ, "mappingIndicatorHidden"));

		json_t* sourcePluginSlugJ = json_object_get(rootJ, "sourcePluginSlug");
		if (sourcePluginSlugJ) sourcePluginSlug = json_string_value(sourcePluginSlugJ);
		json_t* sourcePluginNameJ = json_object_get(rootJ, "sourcePluginName");
		if (sourcePluginNameJ) sourcePluginName = json_string_value(sourcePluginNameJ);
		json_t* sourceModelSlugJ = json_object_get(rootJ, "sourceModelSlug");
		if (sourceModelSlugJ) sourceModelSlug = json_string_value(sourceModelSlugJ);
		json_t* sourceModelNameJ = json_object_get(rootJ, "sourceModelName");
		if (sourceModelNameJ) sourceModelName = json_string_value(sourceModelNameJ);
		json_t* sourceModuleIdJ = json_object_get(rootJ, "sourceModuleId");

		if (sourceModuleIdJ) {
			sourceModuleId = json_integer_value(sourceModuleIdJ);
		}
		else {
			sourcePluginSlug = "";
			sourcePluginName = "";
			sourceModelSlug = "";
			sourceModelName = "";
			return;
		}

		inChange = true;
		std::list<std::function<void()>> handleList;

		json_t* sourceMapsJ = json_object_get(rootJ, "sourceMaps");
		if (sourceMapsJ) {
			json_t* sourceMapJ;
			size_t sourceMapIndex;
			json_array_foreach(sourceMapsJ, sourceMapIndex, sourceMapJ) {
				json_t* moduleIdJ = json_object_get(sourceMapJ, "moduleId");
				int64_t moduleId = json_integer_value(moduleIdJ);
				json_t* paramIdJ = json_object_get(sourceMapJ, "paramId");
				int paramId = json_integer_value(paramIdJ);
				moduleId = idFix(moduleId);

				// This might cause a deadlock as the engine's mutex could already be locked
				handleList.push_back([=]() {
					ParamHandle* sourceHandle = new ParamHandle;
					sourceHandle->text = "stoermelder MIRROR";
					APP->engine->addParamHandle(sourceHandle);
					APP->engine->updateParamHandle(sourceHandle, moduleId, paramId, false);
					sourceHandles.push_back(sourceHandle);
				});
			}
		}

		json_t* targetMapsJ = json_object_get(rootJ, "targetMaps");
		if (targetMapsJ) {
			json_t* targetMapJ;
			size_t targetMapIndex;
			json_array_foreach(targetMapsJ, targetMapIndex, targetMapJ) {
				json_t* moduleIdJ = json_object_get(targetMapJ, "moduleId");
				int64_t moduleId = json_integer_value(moduleIdJ);
				json_t* paramIdJ = json_object_get(targetMapJ, "paramId");
				int paramId = json_integer_value(paramIdJ);
				moduleId = idFix(moduleId);

				// This might cause a deadlock as the engine's mutex could already be locked
				handleList.push_back([=]() {
					ParamHandle* targetHandle = new ParamHandle;
					targetHandle->text = "stoermelder MIRROR";
					APP->engine->addParamHandle(targetHandle);
					APP->engine->updateParamHandle(targetHandle, moduleId, paramId, false);
					targetHandles.push_back(targetHandle);
				});
			}
		}

		json_t* cvInputsJ = json_object_get(rootJ, "cvInputs");
		if (cvInputsJ) {
			json_t* cvInputJ;
			size_t cvInputIndex;
			json_array_foreach(cvInputsJ, cvInputIndex, cvInputJ) {
				json_t* paramIdJ = json_object_get(cvInputJ, "paramId");
				cvParamId[cvInputIndex] = json_integer_value(paramIdJ);
			}
		}

		targetModuleIds.clear();
		json_t* targetModulesJ = json_object_get(rootJ, "targetModules");
		if (targetModulesJ) {
			json_t* targetModuleJ;
			size_t targetModuleIndex;
			json_array_foreach(targetModulesJ, targetModuleIndex, targetModuleJ) {
				json_t* moduleIdJ = json_object_get(targetModuleJ, "moduleId");
				int64_t moduleId = json_integer_value(moduleIdJ);
				moduleId = idFix(moduleId);
				targetModuleIds.push_back(moduleId);
			}
		}

		idFixClearMap();

		// Enqueue on the UI-thread for creating ParamHandles
		taskProcessorUi.enqueue([=]() {
			for (std::function<void()> f : handleList) f();
			inChange = false;
		});
	}
};


struct MirrorWidget : ThemedModuleWidget<MirrorModule> {
	MirrorWidget(MirrorModule* module)
		: ThemedModuleWidget<MirrorModule>(module, "Mirror") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < 8; i++) {
			addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 134.5f + i * 27.4f), module, MirrorModule::INPUT_CV + i));
		}
	}

	void step() override {
		ThemedModuleWidget<MirrorModule>::step();
		if (module) module->taskProcessorUi.process();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MirrorModule>::appendContextMenu(menu);
		MirrorModule* module = dynamic_cast<MirrorModule*>(this->module);

		if (module->sourceModelSlug != "") {
			menu->addChild(new MenuSeparator());
			menu->addChild(createMenuLabel("Configured for..."));
			menu->addChild(createMenuLabel(module->sourcePluginName + " " + module->sourceModelName));
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Audio rate processing", &module->audioRate));
		menu->addChild(createBoolPtrMenuItem("Hide mapping indicators", &module->mappingIndicatorHidden));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Bind source module (left)", "", [=]() { module->bindToSource(); }));
		menu->addChild(createMenuItem("Map module (right)", "", [=]() { module->bindToTarget(); }));
		menu->addChild(createMenuItem("Add and map new module", "", [=]() { addNewModule(); module->bindToTarget(); }));
		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("CV inputs", "",
			[=](Menu* menu) {
				for (int pId = 0; pId < 8; pId++) {
					menu->addChild(createSubmenuItem(string::f("CV %i input", pId + 1), "",
						[=](Menu* menu) {
							menu->addChild(createCheckMenuItem("None",
								[=]() { return module->cvParamId[pId] == -1; },
								[=]() { module->cvParamId[pId] = -1; }
							));

							for (size_t i = 0; i < module->sourceHandles.size(); i++) {
								ParamHandle* sourceHandle = module->sourceHandles[i];
								if (!sourceHandle) continue;
								ModuleWidget* moduleWidget = APP->scene->rack->getModule(sourceHandle->moduleId);
								if (!moduleWidget) continue;
								ParamWidget* paramWidget = moduleWidget->getParam(sourceHandle->paramId);
								if (!paramWidget) continue;
								
								menu->addChild(createCheckMenuItem("Parameter " + paramWidget->getParamQuantity()->getLabel(),
									[=]() { return module->cvParamId[pId] == sourceHandle->paramId; },
									[=]() {	module->cvParamId[pId] = sourceHandle->paramId; }
								));
							}
						}
					));
				}
			}
		));
		menu->addChild(createMenuItem("Sync module presets", RACK_MOD_SHIFT_NAME "+S", [=]() { syncPresets(); }));
	}

	void syncPresets() {
		ModuleWidget* mw = APP->scene->rack->getModule(module->sourceModuleId);
		if (!mw) return;
		json_t* preset = mw->toJson();

		for (int64_t moduleId : module->targetModuleIds) {
			mw = APP->scene->rack->getModule(moduleId);
			if (mw) mw->fromJson(preset);
		}

		json_decref(preset);
	}

	void addNewModule() {
		if (module->sourceModuleId < 0) return;
		ModuleWidget* mw = APP->scene->rack->getModule(module->sourceModuleId);
		if (!mw) return;

		// Make free space on the right side
		float rightWidth = mw->box.size.x;
		Vec pos = box.pos;
		for (int i = 0; i < (rightWidth / RACK_GRID_WIDTH); i++) {
			Vec np = box.pos.plus(Vec(RACK_GRID_WIDTH, 0));
			APP->scene->rack->setModulePosForce(this, np);
		}
		APP->scene->rack->setModulePosForce(this, pos);

		// Get Model
		plugin::Model* model = plugin::getModel(module->sourcePluginSlug, module->sourceModelSlug);
		if (!model) return;

		// Create Module
		engine::Module* addedModule = model->createModule();
		APP->engine->addModule(addedModule);

		// Create ModuleWidget
		ModuleWidget* newMw = model->createModuleWidget(addedModule);
		assert(newMw);
		newMw->box.pos = box.pos;
		newMw->box.pos.x += box.size.x;
		APP->scene->rack->addModule(newMw);
		APP->scene->rack->setModulePosForce(newMw, newMw->box.pos);

		// Apply preset
		json_t* preset = mw->toJson();
		newMw->fromJson(preset);
		json_decref(preset);
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_S && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			syncPresets();
			e.consume(this);
		}
		ThemedModuleWidget<MirrorModule>::onHoverKey(e);
	}
};

} // namespace Mirror
} // namespace StoermelderPackOne

Model* modelMirror = createModel<StoermelderPackOne::Mirror::MirrorModule, StoermelderPackOne::Mirror::MirrorWidget>("Mirror");