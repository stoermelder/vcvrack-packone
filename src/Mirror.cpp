#include "plugin.hpp"

namespace Mirror {

struct MirrorModule : Module {
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
	std::string pluginSlug;
	/** [Stored to JSON] */
	std::string modelSlug;
	/** [Stored to JSON] */
	std::string moduleName;
	/** [Stored to Json] */
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

	MirrorModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		processDivider.setDivision(32);
		handleDivider.setDivision(4096);
		onReset();
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
		inChange = true;
		for (ParamHandle* sourceHandle : sourceHandles) {
			APP->engine->removeParamHandle(sourceHandle);
			delete sourceHandle;
		}
		sourceHandles.clear();
		for (ParamHandle* targetHandle : targetHandles) {
			APP->engine->removeParamHandle(targetHandle);
			delete targetHandle;
		}
		targetHandles.clear();

		for (int i = 0; i < 8; i++) {
			cvParamId[i] = -1;
		}
		inChange = false;

		pluginSlug = "";
		modelSlug = "";
		moduleName = "";
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
				while (j < targetHandles.size()) {
					ParamHandle* targetHandle = targetHandles[j];
					targetHandle->color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0xff, 0x40, 0xff);
					if (sourceHandle->moduleId < 0 && targetHandle->moduleId >= 0) {
						APP->engine->updateParamHandle(targetHandle, sourceHandle->moduleId, sourceHandle->paramId, true);
					}
					j += sourceHandles.size();
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
		Expander* exp = &leftExpander;
		if (exp->moduleId < 0) return;

		inChange = true;
		onReset();
		Module* m = exp->module;
		pluginSlug = m->model->plugin->name;
		modelSlug = m->model->slug;
		moduleName = m->model->name;

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
		Expander* exp = &rightExpander;
		if (exp->moduleId < 0) return;
		Module* m = exp->module;
		if (pluginSlug != m->model->plugin->name || modelSlug != m->model->slug) return;

		inChange = true;
		for (ParamHandle* sourceHandle : sourceHandles) {
			ParamHandle* targetHandle = new ParamHandle;
			targetHandle->text = "stoermelder MIRROR";
			APP->engine->addParamHandle(targetHandle);
			APP->engine->updateParamHandle(targetHandle, m->id, sourceHandle->paramId, true);
			targetHandles.push_back(targetHandle);
		}
		inChange = false;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "pluginSlug", json_string(pluginSlug.c_str()));
		json_object_set_new(rootJ, "modelSlug", json_string(modelSlug.c_str()));
		json_object_set_new(rootJ, "moduleName", json_string(moduleName.c_str()));
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));

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

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		pluginSlug = json_string_value(json_object_get(rootJ, "pluginSlug"));
		modelSlug = json_string_value(json_object_get(rootJ, "modelSlug"));
		moduleName = json_string_value(json_object_get(rootJ, "moduleName"));
		audioRate = json_boolean_value(json_object_get(rootJ, "audioRate"));
		mappingIndicatorHidden = json_boolean_value(json_object_get(rootJ, "mappingIndicatorHidden"));

		inChange = true;
		json_t* sourceMapsJ = json_object_get(rootJ, "sourceMaps");
		if (sourceMapsJ) {
			json_t* sourceMapJ;
			size_t sourceMapIndex;
			json_array_foreach(sourceMapsJ, sourceMapIndex, sourceMapJ) {
				json_t* moduleIdJ = json_object_get(sourceMapJ, "moduleId");
				json_t* paramIdJ = json_object_get(sourceMapJ, "paramId");

				ParamHandle* sourceHandle = new ParamHandle;
				sourceHandle->text = "stoermelder MIRROR";
				APP->engine->addParamHandle(sourceHandle);
				APP->engine->updateParamHandle(sourceHandle, json_integer_value(moduleIdJ), json_integer_value(paramIdJ), false);
				sourceHandles.push_back(sourceHandle);
			}
		}

		json_t* targetMapsJ = json_object_get(rootJ, "targetMaps");
		if (targetMapsJ) {
			json_t* targetMapJ;
			size_t targetMapIndex;
			json_array_foreach(targetMapsJ, targetMapIndex, targetMapJ) {
				json_t* moduleIdJ = json_object_get(targetMapJ, "moduleId");
				json_t* paramIdJ = json_object_get(targetMapJ, "paramId");

				ParamHandle* targetHandle = new ParamHandle;
				targetHandle->text = "stoermelder MIRROR";
				APP->engine->addParamHandle(targetHandle);
				APP->engine->updateParamHandle(targetHandle, json_integer_value(moduleIdJ), json_integer_value(paramIdJ), false);
				targetHandles.push_back(targetHandle);
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

		inChange = false;
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

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MirrorModule>::appendContextMenu(menu);
		MirrorModule* module = dynamic_cast<MirrorModule*>(this->module);
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

		struct AudioRateItem : MenuItem {
			MirrorModule* module;
			void onAction(const event::Action& e) override {
				module->audioRate ^= true;
			}
			void step() override {
				rightText = module->audioRate ? "✔" : "";
				MenuItem::step();
			}
		};

		struct MappingIndicatorHiddenItem : MenuItem {
			MirrorModule* module;
			void onAction(const event::Action& e) override {
				module->mappingIndicatorHidden ^= true;
			}
			void step() override {
				rightText = module->mappingIndicatorHidden ? "✔" : "";
				MenuItem::step();
			}
		};

		struct CvInputPortMenuItem : MenuItem {
			MirrorModule* module;
			CvInputPortMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct CvInputPortItem : MenuItem {
				MirrorModule* module;
				int id;
				CvInputPortItem() {
					rightText = RIGHT_ARROW;
				}

				struct CvInputItem : MenuItem {
					MirrorModule* module;
					int id;
					int paramId;
					void onAction(const event::Action& e) override {
						module->cvParamId[id] = paramId;
					}
					void step() override {
						rightText = module->cvParamId[id] == paramId ? "✔" : "";
						MenuItem::step();
					}
				};

				Menu* createChildMenu() override {
					Menu* menu = new Menu;
					menu->addChild(construct<CvInputItem>(&MenuItem::text, "None", &CvInputItem::module, module, &CvInputItem::id, id, &CvInputItem::paramId, -1));
					for (size_t i = 0; i < module->sourceHandles.size(); i++) {
						ParamHandle* sourceHandle = module->sourceHandles[i];
						if (!sourceHandle) continue;
						ModuleWidget* moduleWidget = APP->scene->rack->getModule(sourceHandle->moduleId);
						if (!moduleWidget) continue;
						ParamWidget* paramWidget = moduleWidget->getParam(sourceHandle->paramId);
						if (!paramWidget) continue;
						
						std::string text = "Parameter " + paramWidget->paramQuantity->getLabel();
						menu->addChild(construct<CvInputItem>(&MenuItem::text, text, &CvInputItem::module, module, &CvInputItem::id, id, &CvInputItem::paramId, sourceHandle->paramId));
					}
					return menu;
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (int i = 0; i < 8; i++) {
					menu->addChild(construct<CvInputPortItem>(&MenuItem::text, string::f("CV port %i", i + 1), &CvInputPortItem::module, module, &CvInputPortItem::id, i));
				}
				return menu;
			}
		};

		struct BindSourceItem : MenuItem {
			MirrorModule* module;
			void onAction(const event::Action& e) override {
				module->bindToSource();
			}
		};

		struct BindTargetItem : MenuItem {
			MirrorModule* module;
			void onAction(const event::Action& e) override {
				module->bindToTarget();
			}
		};

		struct SyncPresetItem : MenuItem {
			MirrorWidget* mw;
			void onAction(const event::Action& e) override {
				mw->syncPresets();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<AudioRateItem>(&MenuItem::text, "Audio rate processing", &AudioRateItem::module, module));
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<CvInputPortMenuItem>(&MenuItem::text, "CV ports", &CvInputPortMenuItem::module, module));
		menu->addChild(construct<BindSourceItem>(&MenuItem::text, "Map source module", &BindSourceItem::module, module));
		menu->addChild(construct<BindTargetItem>(&MenuItem::text, "Map target module", &BindTargetItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SyncPresetItem>(&MenuItem::text, "Sync module settings", &SyncPresetItem::mw, this));
	}

	void syncPresets() {
		int moduleId = -1;
		for (ParamHandle* sourceHandle : module->sourceHandles) {
			if (sourceHandle->moduleId >= 0) {
				moduleId = sourceHandle->moduleId;
				break;
			}
		}

		if (moduleId < 0) return;
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		if (!mw) return;
		json_t* preset = mw->toJson();

		moduleId = -1;
		for (ParamHandle* targetHandle : module->targetHandles) {
			if (targetHandle->moduleId >= 0 && targetHandle->moduleId != moduleId) {
				moduleId = targetHandle->moduleId;
				mw = APP->scene->rack->getModule(moduleId);
				mw->fromJson(preset);
			}
		}

		json_decref(preset);
	}
};

} // namespace Mirror

Model* modelMirror = createModel<Mirror::MirrorModule, Mirror::MirrorWidget>("Mirror");