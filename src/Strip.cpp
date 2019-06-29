#include "plugin.hpp"
#include <osdialog.h>
#include <plugin.hpp>
#include <thread>

static const char PRESET_FILTERS[] = "stoermelder STRIP group preset (.vcvss):vcvss";

const int STRIP_ONMODE_DEFAULT = 0;
const int STRIP_ONMODE_TOGGLE = 1;


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
        if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage())) {
            traverseDisable(onMode == STRIP_ONMODE_DEFAULT ? false : !lastState);
        }
	}

    void traverseDisable(bool val) {
        lastState = val;
        Module *m = this;
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
		json_object_set_new(rootJ, "onMode", json_boolean(onMode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *onModeJ = json_object_get(rootJ, "onMode");
		onMode = json_boolean_value(onModeJ);
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

	ModuleWidget *moduleFromJson(json_t *moduleJ) {
		// Get slugs
		json_t *pluginSlugJ = json_object_get(moduleJ, "plugin");
		if (!pluginSlugJ)
			return NULL;
		json_t *modelSlugJ = json_object_get(moduleJ, "model");
		if (!modelSlugJ)
			return NULL;
		std::string pluginSlug = json_string_value(pluginSlugJ);
		std::string modelSlug = json_string_value(modelSlugJ);

		// Get Model
		plugin::Model *model = plugin::getModel(pluginSlug, modelSlug);
		if (!model)
			return NULL;

		// Create ModuleWidget
		ModuleWidget *moduleWidget = model->createModuleWidget();
		assert(moduleWidget);
		moduleWidget->fromJson(moduleJ);
		return moduleWidget;
	}

	ModuleWidget *moduleToRack(json_t *moduleJ, Rect &box, bool left) {
		ModuleWidget *moduleWidget = moduleFromJson(moduleJ);
		if (moduleWidget) {
			moduleWidget->box.pos = left ? box.pos.minus(Vec(moduleWidget->box.size.x, 0)) : box.pos;
			APP->scene->rack->addModule(moduleWidget);
			APP->scene->rack->setModulePosForce(moduleWidget, moduleWidget->box.pos);
			box.size = moduleWidget->box.size;
			box.pos = moduleWidget->box.pos;
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

	void loadGroup(std::string filename) {
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

		// remove modules next to STRIP
		std::vector<int> toBeRemoved;
        Module *m = module;
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
		}

		// add modules, right then left matters here
		std::map<int, ModuleWidget*> modules;
		int mc = 0;
		Rect box = this->box;
		json_t *rightModulesJ = json_object_get(rootJ, "rightModules");
		if (rightModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(rightModulesJ, moduleIndex, moduleJ) {
				box.pos = box.pos.plus(Vec(box.size.x, 0));
				ModuleWidget *mw = moduleToRack(moduleJ, box, false);
				// could be NULL, just move on
				modules[mc++] = mw;
			}
		}
		box = this->box;
		json_t *leftModulesJ = json_object_get(rootJ, "leftModules");
		if (leftModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(leftModulesJ, moduleIndex, moduleJ) {
				ModuleWidget *mw = moduleToRack(moduleJ, box, true);
				modules[mc++] = mw;
			}
		}

		// add cables
		json_t *cablesJ = json_object_get(rootJ, "cables");
		if (cablesJ) {
			json_t *cableJ;
			size_t cableIndex;
			json_array_foreach(cablesJ, cableIndex, cableJ) {
				int outputIndex, outputPortId, inputIndex, inputPortId;
				json_unpack(cableJ, "[i, i, i, i]", &outputIndex, &outputPortId, &inputIndex, &inputPortId);

				ModuleWidget *mw1 = modules[outputIndex];
				ModuleWidget *mw2 = modules[inputIndex];
				// maybe modules could not be loaded
				if (!mw1 || !mw2) continue;
				PortWidget *pw1 = mw1->outputs[outputPortId];
				PortWidget *pw2 = mw2->inputs[inputPortId];
				// maybe there is something wrong with the ports
				if (!pw1 || !pw2) continue;

				CableWidget *cw = new CableWidget;
				cw->setOutput(pw1);
				cw->setInput(pw2);
				APP->scene->rack->addCable(cw);
			}
		}
	}

	void saveGroup(std::string filename) {
		INFO("Saving preset %s", filename.c_str());

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
					if (!cw->isComplete())
						continue;

					PortWidget* input = cw->inputPort;
					ModuleWidget *mw2 = APP->scene->rack->getModule(input->module->id);
					auto it2 = modules.find(mw2);
					if (it2 == modules.end())
						continue;

					int inputIndex = it2->second;
					json_t *cableJ = json_pack("[i, i, i, i]", outputIndex, output->portId, inputIndex, input->portId);
					json_array_append_new(cablesJ, cableJ);
				}
			}
		}

		// save json
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "version", json_integer(1));
		json_object_set_new(rootJ, "rightModules", rightModulesJ);
		json_object_set_new(rootJ, "leftModules", leftModulesJ);
		json_object_set_new(rootJ, "cables", cablesJ);

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

	void loadGroupDialog() {
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

		loadGroup(path);
	}

	void saveGroupDialog() {
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

		saveGroup(pathStr);
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

		struct OnModeMenuItem : MenuItem {
			Strip *module;

			void onAction(const event::Action &e) override {
				module->onMode ^= true;
			}

			void step() override {
				rightText = module->onMode == STRIP_ONMODE_DEFAULT ? "Default" : "Toggle";
				MenuItem::step();
			}
		};

        OnModeMenuItem *onModeMenuItem = construct<OnModeMenuItem>(&MenuItem::text, "ON mode", &OnModeMenuItem::module, module);
        menu->addChild(onModeMenuItem);
		menu->addChild(new MenuSeparator());

		struct LoadGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->loadGroupDialog();
			}
		};

		struct SaveGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->saveGroupDialog();
			}
		};

		LoadGroupMenuItem *loadGroupMenuItem = construct<LoadGroupMenuItem>(&MenuItem::text, "Load strip of modules", &LoadGroupMenuItem::moduleWidget, this);
		menu->addChild(loadGroupMenuItem);
        SaveGroupMenuItem *saveGroupMenuItem = construct<SaveGroupMenuItem>(&MenuItem::text, "Save strip of modules", &SaveGroupMenuItem::moduleWidget, this);
		menu->addChild(saveGroupMenuItem);
  	}
};


Model *modelStrip = createModel<Strip, StripWidget>("Strip");