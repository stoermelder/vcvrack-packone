#include "plugin.hpp"
#include <osdialog.h>
#include <plugin.hpp>
#include <thread>

static const char PRESET_FILTERS[] = "stoermelder STRIP group preset (.vcvss):vcvss";

const int STRIP_ONMODE_DEFAULT = 0;
const int STRIP_ONMODE_TOGGLE = 1;
const int STRIP_ONMODE_HIGHLOW = 2;


struct Strip : Module {
	enum ParamIds {
		ON_PARAM,
		OFF_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ON_INPUT,
		OFF_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int onMode = STRIP_ONMODE_DEFAULT;

	bool lastState = false;

	dsp::SchmittTrigger onTrigger;
	dsp::SchmittTrigger offPTrigger;

	Strip() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void process(const ProcessArgs &args) override {
		if (offPTrigger.process(params[OFF_PARAM].getValue() + inputs[OFF_INPUT].getVoltage())) {
			traverseDisable(true);
		}

		switch (onMode) {
			case STRIP_ONMODE_DEFAULT:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					traverseDisable(false);
				break;
			case STRIP_ONMODE_TOGGLE:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					traverseDisable(!lastState);
				break;
			case STRIP_ONMODE_HIGHLOW:
				traverseDisable(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage() < 1.f);
				break;
		}
	}

	void traverseDisable(bool val) {
		if (lastState == val) return;
		lastState = val;
		Module *m;
		m = this;
		while (m) {
			if (m->rightExpander.moduleId < 0) break;
			m->rightExpander.module->bypass = val;
			m = m->rightExpander.module;
		}
		m = this;
		while (m) {
			if (m->leftExpander.moduleId < 0) break;
			m->leftExpander.module->bypass = val;
			m = m->leftExpander.module;
		}
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "onMode", json_integer(onMode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *onModeJ = json_object_get(rootJ, "onMode");
		onMode = json_integer_value(onModeJ);
	}
};


struct StripOnModeMenuItem : MenuItem {
	struct StripOnModeItem : MenuItem {
		Strip *module;
		int onMode;

		void onAction(const event::Action &e) override {
			module->onMode = onMode;
		}

		void step() override {
			rightText = module->onMode == onMode ? "✔" : "";
			MenuItem::step();
		}
	};

	Strip *module;
	Menu *createChildMenu() override {
		Menu *menu = new Menu;
		menu->addChild(construct<StripOnModeItem>(&MenuItem::text, "Default", &StripOnModeItem::module, module, &StripOnModeItem::onMode, STRIP_ONMODE_DEFAULT));
		menu->addChild(construct<StripOnModeItem>(&MenuItem::text, "Toggle", &StripOnModeItem::module, module, &StripOnModeItem::onMode, STRIP_ONMODE_TOGGLE));
		menu->addChild(construct<StripOnModeItem>(&MenuItem::text, "High/Low", &StripOnModeItem::module, module, &StripOnModeItem::onMode, STRIP_ONMODE_HIGHLOW));
		return menu;
	}
};


struct StripWidget : ModuleWidget {
	StripWidget(Strip *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Strip.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 59.3f), module, Strip::ON_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 82.6f), module, Strip::ON_PARAM));
		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 126.1f), module, Strip::OFF_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 149.4f), module, Strip::OFF_PARAM));
	}

	void groupClear() {
		std::vector<int> toBeRemoved;
		Module *m;
		m = module;
		while (m) {
			if (m->rightExpander.moduleId < 0) break;
			toBeRemoved.push_back(m->rightExpander.moduleId);
			m = m->rightExpander.module;
		}
		m = module;
		while (m) {
			if (m->leftExpander.moduleId < 0) break;
			toBeRemoved.push_back(m->leftExpander.moduleId);
			m = m->leftExpander.module;
		}
		for (std::vector<int>::iterator it = toBeRemoved.begin() ; it != toBeRemoved.end(); ++it) {
			ModuleWidget *mw = APP->scene->rack->getModule(*it);
			APP->scene->rack->removeModule(mw);
			delete mw;
		}
	}

	/** Creates a module from json data, also retrieved the previous id of the module */
	ModuleWidget *moduleFromJson(json_t *moduleJ, int &oldId) {
		// Get slugs
		json_t *pluginSlugJ = json_object_get(moduleJ, "plugin");
		if (!pluginSlugJ) 
			return NULL;
		json_t *modelSlugJ = json_object_get(moduleJ, "model");
		if (!modelSlugJ) 
			return NULL;
		std::string pluginSlug = json_string_value(pluginSlugJ);
		std::string modelSlug = json_string_value(modelSlugJ);

		json_t *idJ = json_object_get(moduleJ, "id");
		oldId = idJ ? json_integer_value(idJ) : -1;

		// Get Model
		plugin::Model *model = plugin::getModel(pluginSlug, modelSlug);
		if (!model)
			return NULL;

		// Create ModuleWidget
		ModuleWidget *moduleWidget = model->createModuleWidget();
		assert(moduleWidget);
		return moduleWidget;
	}

	ModuleWidget *moduleToRack(json_t *moduleJ, bool left, Rect &box, int &oldId, int &newId) {
		ModuleWidget *moduleWidget = moduleFromJson(moduleJ, oldId);
		if (moduleWidget) {
			moduleWidget->box.pos = left ? box.pos.minus(Vec(moduleWidget->box.size.x, 0)) : box.pos;
			moduleWidget->module->id = -1;
			APP->scene->rack->addModule(moduleWidget);
			APP->scene->rack->setModulePosForce(moduleWidget, moduleWidget->box.pos);
			box.size = moduleWidget->box.size;
			box.pos = moduleWidget->box.pos;
			newId = moduleWidget->module->id;
			return moduleWidget;
		}
		else {
			json_t *pluginSlugJ = json_object_get(moduleJ, "plugin");
			json_t *modelSlugJ = json_object_get(moduleJ, "model");
			std::string pluginSlug = json_string_value(pluginSlugJ);
			std::string modelSlug = json_string_value(modelSlugJ);
			//APP->patch->warningLog += string::f("Could not find module \"%s\" of plugin \"%s\"\n", modelSlug.c_str(), pluginSlug.c_str());
			box = Rect(box.pos, Vec(0, 0));
			return NULL;
		}
	}

	void groupFromJson_modules(json_t *rootJ, std::map<int, ModuleWidget*> &modules, std::map<int, int> &moduleIds) {
		int mc = 0;
		Rect box = this->box;
		json_t *rightModulesJ = json_object_get(rootJ, "rightModules");
		if (rightModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(rightModulesJ, moduleIndex, moduleJ) {
				int oldId, newId;
				box.pos = box.pos.plus(Vec(box.size.x, 0));
				ModuleWidget *mw = moduleToRack(moduleJ, false, box, oldId, newId);
				// could be NULL, just move on
				modules[mc++] = mw;
				moduleIds[oldId] = newId;
			}
		}
		box = this->box;
		json_t *leftModulesJ = json_object_get(rootJ, "leftModules");
		if (leftModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(leftModulesJ, moduleIndex, moduleJ) {
				int oldId, newId;
				ModuleWidget *mw = moduleToRack(moduleJ, true, box, oldId, newId);
				modules[mc++] = mw;
				moduleIds[oldId] = newId;
			}
		}
	}

	void groupFromJson_presets_fixMapping(json_t *moduleJ, std::map<int, int> &moduleIds) {
		std::string pluginSlug = json_string_value(json_object_get(moduleJ, "plugin"));
		std::string modelSlug = json_string_value(json_object_get(moduleJ, "model"));

		if (!(pluginSlug == "Stoermelder-P1" || pluginSlug == "VCV")) 
			return;
		if (!(modelSlug == "CVMap" || modelSlug == "CVMapMicro" || modelSlug == "CVPam" || modelSlug == "MIDI-Map")) 
			return;

		json_t *dataJ = json_object_get(moduleJ, "data");
		json_t *mapsJ = json_object_get(dataJ, "maps");
		if (mapsJ) {
			json_t *mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				json_t *moduleIdJ = json_object_get(mapJ, "moduleId");
				if (!moduleIdJ)
					continue;
				int oldId = json_integer_value(moduleIdJ);
				if (oldId >= 0) {
					int newId = moduleIds[oldId];
					json_object_set_new(mapJ, "moduleId", json_integer(newId));
				}
			}
		}
	}

	void groupFromJson_presets(json_t *rootJ, std::map<int, ModuleWidget*> &modules, std::map<int, int> &moduleIds) {
		int mc = 0;
		json_t *rightModulesJ = json_object_get(rootJ, "rightModules");
		if (rightModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(rightModulesJ, moduleIndex, moduleJ) {
				groupFromJson_presets_fixMapping(moduleJ, moduleIds);
				modules[mc]->fromJson(moduleJ);
				mc++;
			}
		}
		json_t *leftModulesJ = json_object_get(rootJ, "leftModules");
		if (leftModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(leftModulesJ, moduleIndex, moduleJ) {
				groupFromJson_presets_fixMapping(moduleJ, moduleIds);
				modules[mc]->fromJson(moduleJ);
				mc++;
			}
		}
	}

	void groupFromJson_cables(json_t *rootJ, std::map<int, ModuleWidget*> &modules) {
		json_t *cablesJ = json_object_get(rootJ, "cables");
		if (cablesJ) {
			json_t *cableJ;
			size_t cableIndex;
			json_array_foreach(cablesJ, cableIndex, cableJ) {
				int outputIndex = json_integer_value(json_object_get(cableJ, "outputModuleIndex"));
				int outputPortId = json_integer_value(json_object_get(cableJ, "outputId"));
				int inputIndex = json_integer_value(json_object_get(cableJ, "inputModuleIndex"));
				int inputPortId = json_integer_value(json_object_get(cableJ, "inputId"));
				const char *colorStr = json_string_value(json_object_get(cableJ, "color"));

				ModuleWidget *outputModule = modules[outputIndex];
				ModuleWidget *inputModule = modules[inputIndex];
				// in case one of the modules could not be loaded
				if (!outputModule || !inputModule) continue;

				CableWidget *cw = new CableWidget;
				if (colorStr) {
					cw->color = color::fromHexString(colorStr);
				}
				for (PortWidget *port : outputModule->outputs) {
					if (port->portId == outputPortId) {
						cw->setOutput(port);
						break;
					}
				}
				for (PortWidget *port : inputModule->inputs) {
					if (port->portId == inputPortId) {
						cw->setInput(port);
						break;
					}
				}
				if (cw->isComplete()) {
					APP->scene->rack->addCable(cw);
				}
			}
		}
	}


	void groupToJson(json_t *rootJ) {
		// add modules
		std::map<ModuleWidget*, int> modules;
		int mc = 0;
		json_t *rightModulesJ = json_array();
		Module *m = module;
		while (m) {
			if (m->rightExpander.moduleId < 0) break;
			ModuleWidget *mw = APP->scene->rack->getModule(m->rightExpander.moduleId);
			json_t *moduleJ = mw->toJson();
			assert(moduleJ);
			json_array_append_new(rightModulesJ, moduleJ);
			modules[mw] = mc++;
			m = m->rightExpander.module;
		}

		json_t *leftModulesJ = json_array();
		m = module;
		while (m) {
			if (m->leftExpander.moduleId < 0) break;
			ModuleWidget *mw = APP->scene->rack->getModule(m->leftExpander.moduleId);
			json_t *moduleJ = mw->toJson();
			assert(moduleJ);
			json_array_append_new(leftModulesJ, moduleJ);
			modules[mw] = mc++;
			m = m->leftExpander.module;
		}

		// add cables
		json_t *cablesJ = json_array();
		for (auto it1 = modules.begin(); it1 != modules.end(); ++it1) {
			int outputIndex = it1->second;
			// it is enough to check the inputs, as outputs don't matter when the other end outside of the group
			for (PortWidget* output : it1->first->outputs) {
				for (CableWidget *cw : APP->scene->rack->getCablesOnPort(output)) {
					if (!cw->isComplete()) continue;

					PortWidget* input = cw->inputPort;
					ModuleWidget *inputModule = APP->scene->rack->getModule(input->module->id);
					auto it2 = modules.find(inputModule);
					if (it2 == modules.end()) continue;
					int inputIndex = it2->second;

					std::string colorStr = color::toHexString(cw->color);

					json_t *cableJ = json_object();
					json_object_set_new(cableJ, "outputModuleIndex", json_integer(outputIndex));
					json_object_set_new(cableJ, "outputId", json_integer(output->portId));
					json_object_set_new(cableJ, "inputModuleIndex", json_integer(inputIndex));
					json_object_set_new(cableJ, "inputId", json_integer(input->portId));
					json_object_set_new(cableJ, "color", json_string(colorStr.c_str()));
					json_array_append_new(cablesJ, cableJ);
				}
			}
		}

		json_object_set_new(rootJ, "stripVersion", json_integer(1));
		json_object_set_new(rootJ, "rightModules", rightModulesJ);
		json_object_set_new(rootJ, "leftModules", leftModulesJ);
		json_object_set_new(rootJ, "cables", cablesJ);

		json_t *versionJ = json_string(app::APP_VERSION.c_str());
		json_object_set_new(rootJ, "version", versionJ);
	}

	void groupCopyClipboard() {
		json_t *rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});
		char *moduleJson = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		DEFER({
			free(moduleJson);
		});
		glfwSetClipboardString(APP->window->win, moduleJson);
	}

	void groupSaveFile(std::string filename) {
		INFO("Saving preset %s", filename.c_str());

		json_t *rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});

		FILE *file = fopen(filename.c_str(), "w");
		if (!file) {
			WARN("Could not write to patch file %s", filename.c_str());
		}
		DEFER({
			fclose(file);
		});

		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
	}

	void groupSaveFileDialog() {
		osdialog_filters *filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		char *path = osdialog_file(OSDIALOG_SAVE, "", "Untitled.vcvss", filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		std::string pathStr = path;
		std::string extension = string::filenameExtension(string::filename(pathStr));
		if (extension.empty()) {
			pathStr += ".vcvss";
		}

		groupSaveFile(pathStr);
	}

	void groupFromJson(json_t *rootJ) {
		// clear modules next to STRIP
		groupClear();

		// add modules, order matters here, right then left!
		std::map<int, ModuleWidget*> modules;
		std::map<int, int> moduleIds;
		groupFromJson_modules(rootJ, modules, moduleIds);
		groupFromJson_presets(rootJ, modules, moduleIds);

		// add cables
		groupFromJson_cables(rootJ, modules);
	}

	void groupPasteClipboard() {
		const char *moduleJson = glfwGetClipboardString(APP->window->win);
		if (!moduleJson) {
			WARN("Could not get text from clipboard.");
			return;
		}

		json_error_t error;
		json_t *rootJ = json_loads(moduleJson, 0, &error);
		if (!rootJ) {
			WARN("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupFromJson(rootJ);
	}

	void groupLoadFile(std::string filename) {
		INFO("Loading preset %s", filename.c_str());

		FILE *file = fopen(filename.c_str(), "r");
		if (!file) {
			WARN("Could not load file %s", filename.c_str());
			return;
		}
		DEFER({
			fclose(file);
		});

		json_error_t error;
		json_t *rootJ = json_loadf(file, 0, &error);
		if (!rootJ) {
			std::string message = string::f("File is not a valid file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupFromJson(rootJ);
	}

	void groupLoadFileDialog() {
		osdialog_filters *filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		groupLoadFile(path);
	}


	void onHoverKey(const event::HoverKey &e) override {
		ModuleWidget::onHoverKey(e);
		if (e.isConsumed())
			return;

		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			switch (e.key) {
				case GLFW_KEY_C: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						groupCopyClipboard();
						e.consume(this);
					}
				} break;
				case GLFW_KEY_V: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						groupPasteClipboard();
						e.consume(this);
					}
				} break;
			}
		}
	}

	void appendContextMenu(Menu *menu) override {
		Strip *module = dynamic_cast<Strip*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action &e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Strip.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());

		StripOnModeMenuItem *stripOnModeMenuItem = construct<StripOnModeMenuItem>(&MenuItem::text, "ON mode", &StripOnModeMenuItem::module, module);
		stripOnModeMenuItem->rightText = RIGHT_ARROW;
		menu->addChild(stripOnModeMenuItem);
		menu->addChild(new MenuSeparator());

		struct CopyGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupCopyClipboard();
			}
		};

		struct PasteGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupPasteClipboard();
			}
		};

		struct LoadGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupLoadFileDialog();
			}
		};

		struct SaveGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupSaveFileDialog();
			}
		};

		CopyGroupMenuItem *copyGroupMenuItem = construct<CopyGroupMenuItem>(&MenuItem::text, "Copy strip", &MenuItem::rightText, "Shift+C", &CopyGroupMenuItem::moduleWidget, this);
		menu->addChild(copyGroupMenuItem);
		PasteGroupMenuItem *pasteGroupMenuItem = construct<PasteGroupMenuItem>(&MenuItem::text, "Paste strip", &MenuItem::rightText, "Shift+V", &PasteGroupMenuItem::moduleWidget, this);
		menu->addChild(pasteGroupMenuItem);
		LoadGroupMenuItem *loadGroupMenuItem = construct<LoadGroupMenuItem>(&MenuItem::text, "Load strip", &LoadGroupMenuItem::moduleWidget, this);
		menu->addChild(loadGroupMenuItem);
		SaveGroupMenuItem *saveGroupMenuItem = construct<SaveGroupMenuItem>(&MenuItem::text, "Save strip", &SaveGroupMenuItem::moduleWidget, this);
		menu->addChild(saveGroupMenuItem);
	}
};


Model *modelStrip = createModel<Strip, StripWidget>("Strip");