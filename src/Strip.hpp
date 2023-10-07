#pragma once
#include "plugin.hpp"
#include "helpers/StripIdFixModule.hpp"
#include <osdialog.h>
#include <plugin.hpp>

namespace StoermelderPackOne {
namespace Strip {

static const char PRESET_FILTERS[] = "stoermelder STRIP group preset (.vcvss):vcvss";
static const char SELECTION_FILTERS[] = "VCV Rack module selection (.vcvs):vcvs";

static std::string dirVcvss = asset::user("patches");
static std::string dirVcvs = asset::user("selections");


enum class MODE {
	LEFTRIGHT = 0,
	RIGHT = 1,
	LEFT = 2
};


struct StripModuleBase : Module {
	/** [Stored to JSON] left? right? both? */
	MODE mode = MODE::LEFTRIGHT;

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "mode", json_integer((int)mode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* modeJ = json_object_get(rootJ, "mode");
		mode = (MODE)json_integer_value(modeJ);
	}
};


struct StripBayBase : Module {
	virtual size_t getPortNumber() { return 0; }
	virtual std::string getConnId() { return ""; }
};


template <class MODULE>
struct StripWidgetBase : ThemedModuleWidget<MODULE> {
	typedef ThemedModuleWidget<MODULE> BASE;

	MODULE* module;
	std::string warningLog;

	StripWidgetBase(MODULE* module, std::string baseName)
	: ThemedModuleWidget<MODULE>(module, baseName) { }

	/**
	 * Removes all modules in the group. Used for "cut" in cut & paste.
	 */
	void groupRemove() {
		// Collect all modules right next to this instance of STRIP.
		std::vector<int64_t> toBeRemoved;
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->model == modelStripBlock) break;
				toBeRemoved.push_back(m->rightExpander.moduleId);
				m = m->rightExpander.module;
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->model == modelStripBlock) break;
				toBeRemoved.push_back(m->leftExpander.moduleId);
				m = m->leftExpander.module;
			}
		}

		if (toBeRemoved.size() > 0) {
			history::ComplexAction* complexAction = new history::ComplexAction;
			complexAction->name = "stoermelder STRIP remove";

			/*
			for (int64_t id : toBeRemoved) {
				ModuleWidget* mw = APP->scene->rack->getModule(id);

				for (PortWidget* output : mw->getOutputs()) {
					for (CableWidget* cw : APP->scene->rack->getCablesOnPort(output)) {
						if (!cw->isComplete())
							continue;

						// history::CableRemove
						history::CableRemove* h = new history::CableRemove;
						h->setCable(cw);
						complexAction->push(h);

						APP->scene->rack->removeCable(cw);
					}
				}

				for (PortWidget* input : mw->getInputs()) {
					for (CableWidget* cw : APP->scene->rack->getCablesOnPort(input)) {
						if (!cw->isComplete())
							continue;

						// history::CableRemove
						history::CableRemove* h = new history::CableRemove;
						h->setCable(cw);
						complexAction->push(h);

						APP->scene->rack->removeCable(cw);
					}
				}
			}
			*/
		
			for (int64_t id : toBeRemoved) {
				ModuleWidget* mw = APP->scene->rack->getModule(id);

				mw->appendDisconnectActions(complexAction);

				// history::ModuleRemove
				history::ModuleRemove* h = new history::ModuleRemove;
				h->setModule(mw);
				complexAction->push(h);

				APP->scene->rack->removeModule(mw);
				delete mw;
			}

			APP->history->push(complexAction);
		}
	}

	/**
	 *  Make enough space directly next to this instance of STRIP for the new modules.
	 */
	std::vector<history::Action*>* groupClearSpace(json_t* rootJ) {
		// To make sure there is enough space for the modules shove the existing modules to the 
		// left and to the right. This is done by moving this instance of STRIP stepwise 1HP until enough
		// space is cleared on both sides. Why this stupid and not just use setModulePosForce?
		// Because setModulePosForce will clear the space, but is not certain in which direction the
		// existing modules will be moved because a new big module will push a small module to its closer 
		// side. This would result to foreign modules within the loaded strip.

		std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;
		std::map<int, math::Vec> moduleMovePositions;

		// NB: unstable API
		for (widget::Widget* w : APP->scene->rack->getModuleContainer()->children) {
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			assert(mw);
			moduleMovePositions[mw->module->id] = mw->box.pos;
		}

		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			float rightWidth = json_real_value(json_object_get(rootJ, "rightWidth"));
			if (rightWidth > 0.f) {
				Vec pos = BASE::box.pos;
				for (int i = 0; i < (rightWidth / RACK_GRID_WIDTH) + 4; i++) {
					Vec np = BASE::box.pos.plus(Vec(RACK_GRID_WIDTH, 0));
					APP->scene->rack->setModulePosForce(this, np);
				}
				APP->scene->rack->setModulePosForce(this, pos);
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			float leftWidth = json_real_value(json_object_get(rootJ, "leftWidth"));
				if (leftWidth > 0.f) {
				Vec pos = BASE::box.pos;
				for (int i = 0; i < (leftWidth / RACK_GRID_WIDTH) + 4; i++) {
					Vec np = BASE::box.pos.plus(Vec(-RACK_GRID_WIDTH, 0));
					APP->scene->rack->setModulePosForce(this, np);
				}
				APP->scene->rack->setModulePosForce(this, pos);
			}
		}

		// NB: unstable API
		for (widget::Widget* w : APP->scene->rack->getModuleContainer()->children) {
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			assert(mw);
			// It is possible to add modules to the rack while dragging, so ignore modules that don't exist.
			auto it = moduleMovePositions.find(mw->module->id);
			if (it == moduleMovePositions.end())
				continue;
			// Create ModuleMove action if the module was moved.
			math::Vec pos = it->second;
			if (!pos.isEqual(mw->box.pos)) {
				history::ModuleMove* mmh = new history::ModuleMove;
				mmh->moduleId = mw->module->id;
				mmh->oldPos = pos;
				mmh->newPos = mw->box.pos;
				undoActions->push_back(mmh);
			}
		}

		return undoActions;
	}

	void groupConnectionsCollect(std::list<std::tuple<std::string, int, PortWidget*, NVGcolor>>& conn) {
		std::list<StripBayBase*> toDo;
		std::set<int64_t> moduleIds;

		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->model == modelStripBlock) break;
				m = m->rightExpander.module;
				StripBayBase* sc = dynamic_cast<StripBayBase*>(m);
				if (sc) toDo.push_back(sc);
				moduleIds.insert(m->id);
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->model == modelStripBlock) break;
				m = m->leftExpander.module;
				StripBayBase* sc = dynamic_cast<StripBayBase*>(m);
				if (sc) toDo.push_back(sc);
				moduleIds.insert(m->id);
			}
		}

		for (StripBayBase* sc : toDo) {
			ModuleWidget* mw = APP->scene->rack->getModule(sc->id);
			for (PortWidget* in : mw->getInputs()) {
				std::vector<CableWidget*> cs = APP->scene->rack->getCablesOnPort(in);
				CableWidget* c = cs.front();
				if (!c) continue;
				auto it = moduleIds.find(c->outputPort->module->id);
				// Other end is outside of this strip
				if (it == moduleIds.end()) {
					conn.push_back(std::make_tuple(sc->getConnId(), c->inputPort->portId, c->outputPort, c->color));
				}
			}
			for (PortWidget* out : mw->getOutputs()) {
				std::vector<CableWidget*> cs = APP->scene->rack->getCablesOnPort(out);
				for (CableWidget* c : cs) {
					auto it = moduleIds.find(c->inputPort->module->id);
					// Other end is outside of this strip
					if (it == moduleIds.end()) {
						conn.push_back(std::make_tuple(sc->getConnId(), c->outputPort->portId, c->inputPort, c->color));
					}
				}
			}
		}
	}

	std::vector<history::Action*>* groupConnectionsRestore(std::list<std::tuple<std::string, int, PortWidget*, NVGcolor>>& conn) {
		std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;
		std::map<std::string, StripBayBase*> toDo;

		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->model == modelStripBlock) break;
				m = m->rightExpander.module;
				StripBayBase* sc = dynamic_cast<StripBayBase*>(m);
				if (sc) toDo[sc->getConnId()] = sc;

			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->model == modelStripBlock) break;
				m = m->leftExpander.module;
				StripBayBase* sc = dynamic_cast<StripBayBase*>(m);
				if (sc) toDo[sc->getConnId()] = sc;
			}
		}

		for (auto t : conn) {
			std::string connId = std::get<0>(t);
			int portId = std::get<1>(t);
			PortWidget* pw1 = std::get<2>(t);
			NVGcolor color = std::get<3>(t);
			assert(pw1);

			auto it = toDo.find(connId);
			if (it == toDo.end()) continue;

			ModuleWidget* mw = APP->scene->rack->getModule((*it).second->id);
			PortWidget* pw2 = pw1->type == engine::Port::INPUT ? mw->getOutput(portId) : mw->getInput(portId);
			assert(pw2);

			engine::Cable* c = new engine::Cable;
			if (pw1->type == engine::Port::INPUT) {
				c->inputModule = pw1->module;
				c->inputId = pw1->portId;
				//cw->setInput(pw1);
				c->outputModule = pw2->module;
				c->outputId = pw2->portId;
				//cw->setOutput(pw2);
			}
			else {
				c->outputModule = pw1->module;
				c->outputId = pw1->portId;
				//cw->setOutput(pw1);
				if (APP->scene->rack->getCablesOnPort(pw2).size() == 0) {
					c->inputModule = pw2->module;
					c->inputId = pw2->portId;
					//cw->setInput(pw2);
				}
			}
			APP->engine->addCable(c);

			CableWidget* cw = new CableWidget;
			cw->setCable(c);
			cw->color = color;
			APP->scene->rack->addCable(cw);

			// history::CableAdd
			history::CableAdd* h = new history::CableAdd;
			h->setCable(cw);
			undoActions->push_back(h);
		}

		return undoActions;
	}

	/**
	 * Creates a module from json data, also returns the previous id of the module
	 * @moduleJ
	 * @oldId
	 */
	ModuleWidget* moduleFromJson(json_t* moduleJ, int64_t& oldId) {
		// Get slugs
		json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
		if (!pluginSlugJ)
			return NULL;
		json_t* modelSlugJ = json_object_get(moduleJ, "model");
		if (!modelSlugJ)
			return NULL;
		std::string pluginSlug = json_string_value(pluginSlugJ);
		std::string modelSlug = json_string_value(modelSlugJ);

		json_t* idJ = json_object_get(moduleJ, "id");
		oldId = idJ ? json_integer_value(idJ) : -1;

		// Get Model
		plugin::Model* model = plugin::getModel(pluginSlug, modelSlug);
		if (!model) return NULL;

		// Create Module
		engine::Module* addedModule = model->createModule();
		APP->engine->addModule(addedModule);

		// Create ModuleWidget
		ModuleWidget* moduleWidget = model->createModuleWidget(addedModule);
		assert(moduleWidget);
		return moduleWidget;
	}

	enum class moduleToRackPos {
		LEFT,
		RIGHT,
		POS
	};

	/**
	 *  Adds a new module to the rack from a json-representation.
	 * @moduleJ
	 * @left Should the module placed left or right of @box?
	 * @box
	 * @oldId
	 */
	ModuleWidget* moduleToRack(json_t* moduleJ, moduleToRackPos modPos, Rect& box, int64_t& oldId) {
		ModuleWidget* moduleWidget = moduleFromJson(moduleJ, oldId);
		if (moduleWidget) {
			switch (modPos) {
				case moduleToRackPos::LEFT:
					moduleWidget->box.pos = box.pos.minus(Vec(moduleWidget->box.size.x, 0));
					break;
				case moduleToRackPos::RIGHT:
					moduleWidget->box.pos = box.pos;
					break;
				case moduleToRackPos::POS:
					//box.pos = box.pos.mult(RACK_GRID_SIZE);
					moduleWidget->box.pos = box.pos; //.plus(RACK_OFFSET);
					break;
			}

			APP->scene->rack->addModule(moduleWidget);
			APP->scene->rack->setModulePosForce(moduleWidget, moduleWidget->box.pos);
			box.size = moduleWidget->box.size;
			box.pos = moduleWidget->box.pos;
			return moduleWidget;
		}
		else {
			json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
			std::string pluginSlug = json_string_value(pluginSlugJ);
			json_t* modelSlugJ = json_object_get(moduleJ, "model");
			std::string modelSlug = json_string_value(modelSlugJ);
			warningLog += string::f("Could not find module \"%s\" of plugin \"%s\"\n", modelSlug.c_str(), pluginSlug.c_str());
			box = Rect(box.pos, Vec(0, 0));
			return NULL;
		}
	}

	/**
	 * Adds modules next to this module according to the supplied json-representation.
	 * @rootJ json-representation of the STRIP-file
	 * @modules maps old module ids the new modules
	 */
	std::vector<history::Action*>* groupFromJson_modules(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
		std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Rect box = this->box;
			json_t* rightModulesJ = json_object_get(rootJ, "rightModules");
			if (rightModulesJ) {
				json_t* moduleJ;
				size_t moduleIndex;
				json_array_foreach(rightModulesJ, moduleIndex, moduleJ) {
					int64_t oldId = -1;
					box.pos = box.pos.plus(Vec(box.size.x, 0));
					ModuleWidget* mw = moduleToRack(moduleJ, moduleToRackPos::RIGHT, box, oldId);
					// mw could be NULL, just move on
					modules[oldId] = mw;

					if (mw) {
						// ModuleAdd history action
						history::ModuleAdd* h = new history::ModuleAdd;
						h->name = "create module";
						h->setModule(mw);
						undoActions->push_back(h);
					}
				}
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Rect box = this->box;
			json_t* leftModulesJ = json_object_get(rootJ, "leftModules");
			if (leftModulesJ) {
				json_t* moduleJ;
				size_t moduleIndex;
				json_array_foreach(leftModulesJ, moduleIndex, moduleJ) {
					int64_t oldId = -1;
					ModuleWidget* mw = moduleToRack(moduleJ, moduleToRackPos::LEFT, box, oldId);
					modules[oldId] = mw;

					if (mw) {
						// ModuleAdd history action
						history::ModuleAdd* h = new history::ModuleAdd;
						h->name = "create module";
						h->setModule(mw);
						undoActions->push_back(h);
					}
				}
			}
		}

		return undoActions;
	}

	std::vector<history::Action*>* groupSelectionFromJson_modules(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
		std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

		Vec mousePos = APP->scene->rack->getMousePos();
		json_t* modulesJ = json_object_get(rootJ, "modules");
		if (modulesJ) {
			json_t* moduleJ;
			size_t moduleIndex;

			double minX = std::numeric_limits<float>::infinity();
			double minY = std::numeric_limits<float>::infinity();
			json_array_foreach(modulesJ, moduleIndex, moduleJ) {
				// pos
				json_t* posJ = json_object_get(moduleJ, "pos");
				double x = 0.0, y = 0.0;
				json_unpack(posJ, "[F, F]", &x, &y);
				minX = std::min(minX, x);
				minY = std::min(minY, y);
			}

			json_array_foreach(modulesJ, moduleIndex, moduleJ) {
				int64_t oldId = -1;

				// pos
				Rect box;
				json_t* posJ = json_object_get(moduleJ, "pos");
				double x = 0.0, y = 0.0;
				json_unpack(posJ, "[F, F]", &x, &y);
				box.pos = math::Vec(x, y);
				box.pos = box.pos.minus(Vec(minX, minY)).mult(RACK_GRID_SIZE);
				box.pos = mousePos.plus(box.pos);

				ModuleWidget* mw = moduleToRack(moduleJ, moduleToRackPos::POS, box, oldId);
				// mw could be NULL, just move on
				modules[oldId] = mw;

				if (mw) {
					// ModuleAdd history action
					history::ModuleAdd* h = new history::ModuleAdd;
					h->name = "create module";
					h->setModule(mw);
					undoActions->push_back(h);
				}

				APP->scene->rack->select(mw);
			}
		}

		return undoActions;
	}

	/**
	 * Fixes parameter mappings within a preset. This can be considered a hack because
	 * Rack v1/v2 offers no API for reading the mapping module of a parameter. This replaces the
	 * module id in the preset JSON with the new module id to preserve correct mapping.
	 * This means every module using mappings must be handled explicitly.
	 * @moduleJ json-representation of the module
	 * @modules maps old module ids the new modules
	 */
	void groupFromJson_presets_fixMapping(json_t* moduleJ, std::map<int64_t, ModuleWidget*>& modules) {
		std::string pluginSlug = json_string_value(json_object_get(moduleJ, "plugin"));
		std::string modelSlug = json_string_value(json_object_get(moduleJ, "model"));

		static const std::set<std::tuple<std::string, std::string>> moduleSlugs = {
			std::make_tuple("Core", "MIDI-Map"),
			std::make_tuple("MindMeldModular", "PatchMaster")
		};

		// Only handle some specific modules known to use mapping of parameters
		if (moduleSlugs.find(std::make_tuple(pluginSlug, modelSlug)) == moduleSlugs.end())
			return;

		json_t* dataJ = json_object_get(moduleJ, "data");
		json_t* mapsJ = json_object_get(dataJ, "maps");
		if (mapsJ) {
			json_t* mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
				if (!moduleIdJ)
					continue;
				int64_t oldId = json_integer_value(moduleIdJ);
				if (oldId >= 0) {
					int64_t newId = -1;
					ModuleWidget* mw = modules[oldId];
					if (mw != NULL) {
						newId = mw->module->id;
					}
					json_object_set_new(mapJ, "moduleId", json_integer(newId));
				}
			}
		}
	}

	/**
	 * Loads all the presets from a json-representation generated by STRIP. Assumes the modules are there.
	 * Presets of non-existing modules will be skipped.
	 * @json json-representation of the STRIP-file
	 * @modules maps old module ids the new modules
	 */
	std::vector<history::Action*>* groupFromJson_presets(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
		std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

		json_t* rightModulesJ = json_object_get(rootJ, "rightModules");
		if (rightModulesJ) {
			json_t* moduleJ;
			size_t moduleIndex;
			json_array_foreach(rightModulesJ, moduleIndex, moduleJ) {
				if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
					groupFromJson_presets_fixMapping(moduleJ, modules);
					int64_t oldId = json_integer_value(json_object_get(moduleJ, "id"));
					ModuleWidget* mw = modules[oldId];
					if (mw != NULL) {
						// history::ModuleChange
						history::ModuleChange* h = new history::ModuleChange;
						h->name = "load module preset";
						h->moduleId = mw->module->id;
						h->oldModuleJ = mw->toJson();

						StripIdFixModule* m = dynamic_cast<StripIdFixModule*>(mw->module);
						if (m) m->idFixDataFromJson(modules);

						mw->fromJson(moduleJ);

						h->newModuleJ = mw->toJson();
						undoActions->push_back(h);
					}
				}
			}
		}
		json_t* leftModulesJ = json_object_get(rootJ, "leftModules");
		if (leftModulesJ) {
			json_t* moduleJ;
			size_t moduleIndex;
			json_array_foreach(leftModulesJ, moduleIndex, moduleJ) {
				if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
					groupFromJson_presets_fixMapping(moduleJ, modules);
					int64_t oldId = json_integer_value(json_object_get(moduleJ, "id"));
					ModuleWidget* mw = modules[oldId];
					if (mw != NULL) {
						// history::ModuleChange
						history::ModuleChange* h = new history::ModuleChange;
						h->name = "load module preset";
						h->moduleId = mw->module->id;
						h->oldModuleJ = mw->toJson();

						StripIdFixModule* m = dynamic_cast<StripIdFixModule*>(mw->module);
						if (m) m->idFixDataFromJson(modules);

						mw->fromJson(moduleJ);

						h->newModuleJ = mw->toJson();
						undoActions->push_back(h);
					}
				}
			}
		}

		return undoActions;
	}

	std::vector<history::Action*>* groupSelectionFromJson_presets(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
		std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

		json_t* modulesJ = json_object_get(rootJ, "modules");
		json_t* moduleJ;
		size_t moduleIndex;
		json_array_foreach(modulesJ, moduleIndex, moduleJ) {
			groupFromJson_presets_fixMapping(moduleJ, modules);
			int64_t oldId = json_integer_value(json_object_get(moduleJ, "id"));
			ModuleWidget* mw = modules[oldId];
			if (mw != NULL) {
				// history::ModuleChange
				history::ModuleChange* h = new history::ModuleChange;
				h->name = "load module preset";
				h->moduleId = mw->module->id;
				h->oldModuleJ = mw->toJson();

				StripIdFixModule* m = dynamic_cast<StripIdFixModule*>(mw->module);
				if (m) m->idFixDataFromJson(modules);

				mw->fromJson(moduleJ);

				h->newModuleJ = mw->toJson();
				undoActions->push_back(h);
			}
		}

		return undoActions;
	}

	/**
	 * Adds cables loaded from a json-representation generated by STRIP.
	 * If a module is missing the cable will be obviously skipped.
	 * @rootJ json-representation of the STRIP-file
	 * @modules maps old module ids the new modules
	 */
	std::vector<history::Action*>* groupFromJson_cables(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
		std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

		json_t* cablesJ = json_object_get(rootJ, "cables");
		if (cablesJ) {
			json_t* cableJ;
			size_t cableIndex;
			json_array_foreach(cablesJ, cableIndex, cableJ) {
				int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
				int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
				int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
				int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
				const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

				ModuleWidget* outputModule = modules[outputModuleId];
				ModuleWidget* inputModule = modules[inputModuleId];
				// In case one of the modules could not be loaded
				if (!outputModule || !inputModule) continue;

				engine::Cable* c = new engine::Cable;
				c->outputModule = outputModule->module;
				c->outputId = outputId;
				//cw->setOutput(port);
				c->inputModule = inputModule->module;
				c->inputId = inputId;
				//cw->setInput(port);
				APP->engine->addCable(c);

				CableWidget* cw = new CableWidget;
				cw->setCable(c);
				if (colorStr) {
					cw->color = color::fromHexString(colorStr);
				}
				APP->scene->rack->addCable(cw);

				// history::CableAdd
				history::CableAdd* h = new history::CableAdd;
				h->setCable(cw);
				undoActions->push_back(h);
			}
		}

		return undoActions;
	}


	void groupToJson(json_t* rootJ) {
		// Add modules
		std::set<ModuleWidget*> modules;
		
		float rightWidth = 0.f;
		json_t* rightModulesJ = json_array();
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0 || m->rightExpander.module->model == modelStripBlock) break;
				ModuleWidget* mw = APP->scene->rack->getModule(m->rightExpander.moduleId);
				json_t* moduleJ = mw->toJson();
				assert(moduleJ);
				json_array_append_new(rightModulesJ, moduleJ);
				modules.insert(mw);
				rightWidth += mw->box.size.x;
				m = m->rightExpander.module;
			}
		}

		float leftWidth = 0.f;
		json_t* leftModulesJ = json_array();
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0 || m->leftExpander.module->model == modelStripBlock) break;
				ModuleWidget* mw = APP->scene->rack->getModule(m->leftExpander.moduleId);
				json_t* moduleJ = mw->toJson();
				assert(moduleJ);
				json_array_append_new(leftModulesJ, moduleJ);
				modules.insert(mw);
				leftWidth += mw->box.size.x;
				m = m->leftExpander.module;
			}
		}

		// Add cables
		json_t* cablesJ = json_array();
		for (auto i = modules.begin(); i != modules.end(); ++i) {
			ModuleWidget* outputModule = *i;
			// It is enough to check the outputs, as inputs don't matter when the other end is outside of the group
			for (PortWidget* output : outputModule->getOutputs()) {
				for (CableWidget* cw : APP->scene->rack->getCablesOnPort(output)) {
					if (!cw->isComplete())
						continue;

					PortWidget* input = cw->inputPort;
					ModuleWidget* inputModule = APP->scene->rack->getModule(input->module->id);
					if (modules.find(inputModule) == modules.end())
						continue;

					std::string colorStr = color::toHexString(cw->color);

					json_t* cableJ = json_object();
					json_object_set_new(cableJ, "outputModuleId", json_integer(output->module->id));
					json_object_set_new(cableJ, "outputId", json_integer(output->portId));
					json_object_set_new(cableJ, "inputModuleId", json_integer(input->module->id));
					json_object_set_new(cableJ, "inputId", json_integer(input->portId));
					json_object_set_new(cableJ, "color", json_string(colorStr.c_str()));
					json_array_append_new(cablesJ, cableJ);
				}
			}
		}

		json_object_set_new(rootJ, "stripVersion", json_integer(1));
		json_object_set_new(rootJ, "rightModules", rightModulesJ);
		json_object_set_new(rootJ, "rightWidth", json_real(rightWidth));
		json_object_set_new(rootJ, "leftModules", leftModulesJ);
		json_object_set_new(rootJ, "leftWidth", json_real(leftWidth));
		json_object_set_new(rootJ, "cables", cablesJ);

		json_t* versionJ = json_string(rack::APP_VERSION.c_str());
		json_object_set_new(rootJ, "version", versionJ);
	}

	void groupCopyClipboard() {
		json_t* rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});
		char* moduleJson = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		DEFER({
			free(moduleJson);
		});
		glfwSetClipboardString(APP->window->win, moduleJson);
	}

	void groupCutClipboard() {
		json_t* rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});
		char* moduleJson = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		DEFER({
			free(moduleJson);
		});
		glfwSetClipboardString(APP->window->win, moduleJson);
		groupRemove();
	}

	void groupSaveFile(std::string filename) {
		INFO("Saving preset %s", filename.c_str());

		json_t* rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});

		FILE* file = fopen(filename.c_str(), "w");
		if (!file) {
			std::string message = string::f("Could not write to patch file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
		}
		DEFER({
			fclose(file);
		});

		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
	}

	void groupSaveFileDialog() {
		osdialog_filters* filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		std::string dir = asset::user("patches");
		char* path = osdialog_file(OSDIALOG_SAVE, dirVcvss.c_str(), "Untitled.vcvss", filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			dirVcvss = system::getDirectory(std::string(path));
			free(path);
		});

		std::string pathStr = path;
		std::string extension = system::getExtension(system::getFilename(pathStr));
		if (extension.empty()) {
			pathStr += ".vcvss";
		}

		groupSaveFile(pathStr);
	}

	void groupFromJson(json_t* rootJ) {
		warningLog = "";

		// Clear modules next to STRIP
		std::vector<history::Action*>* h1 = groupClearSpace(rootJ);

		// Maps old moduleId to the newly created modules (with new id)
		std::map<int64_t, ModuleWidget*> modules;
		// Add modules
		std::vector<history::Action*>* h2 = groupFromJson_modules(rootJ, modules);
		// Load presets for modules, also fixes parameter mappings
		std::vector<history::Action*>* h3 = groupFromJson_presets(rootJ, modules);

		// Add cables
		std::vector<history::Action*>* h4 = groupFromJson_cables(rootJ, modules);

		// Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
		APP->scene->rack->requestModulePos(this, this->box.pos);

		if (!warningLog.empty()) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, warningLog.c_str());
		}

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP load";
		for (history::Action* h : *h1) complexAction->push(h);
		delete h1;
		for (history::Action* h : *h2) complexAction->push(h);
		delete h2;
		for (history::Action* h : *h3) complexAction->push(h);
		delete h3;
		for (history::Action* h : *h4) complexAction->push(h);
		delete h4;
		APP->history->push(complexAction);
	}

	void groupSelectionFromJson(json_t* rootJ) {
		warningLog = "";

		// Maps old moduleId to the newly created modules (with new id)
		std::map<int64_t, ModuleWidget*> modules;
		// Add modules
		std::vector<history::Action*>* h2 = groupSelectionFromJson_modules(rootJ, modules);
		// Load presets for modules, also fixes parameter mappings
		std::vector<history::Action*>* h3 = groupSelectionFromJson_presets(rootJ, modules);

		// Add cables
		std::vector<history::Action*>* h4 = groupFromJson_cables(rootJ, modules);

		// Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
		APP->scene->rack->requestModulePos(this, this->box.pos);

		if (!warningLog.empty()) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, warningLog.c_str());
		}

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP selection load";
		for (history::Action* h : *h2) complexAction->push(h);
		delete h2;
		for (history::Action* h : *h3) complexAction->push(h);
		delete h3;
		for (history::Action* h : *h4) complexAction->push(h);
		delete h4;
		APP->history->push(complexAction);
	}


	void groupReplaceFromJson(json_t* rootJ) {
		warningLog = "";

		std::list<std::tuple<std::string, int, PortWidget*, NVGcolor>> conn;

		// Collect all connections outside the strip using StripCon modules
		groupConnectionsCollect(conn);

		// Remove all modules adjacent to STRIP
		groupRemove();

		// Clear modules next to STRIP
		std::vector<history::Action*>* h1 = groupClearSpace(rootJ);

		// Maps old moduleId to the newly created module (with new id)
		std::map<int64_t, ModuleWidget*> modules;
		// Add modules
		std::vector<history::Action*>* h2 = groupFromJson_modules(rootJ, modules);
		// Load presets for modules, also fixes parameter mappings
		std::vector<history::Action*>* h3 = groupFromJson_presets(rootJ, modules);

		// Add cables
		std::vector<history::Action*>* h4 = groupFromJson_cables(rootJ, modules);

		// Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
		APP->scene->rack->requestModulePos(this, this->box.pos);

		// Restore cables from StripCon-modules
		std::vector<history::Action*>* h5 = groupConnectionsRestore(conn);

		if (!warningLog.empty()) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, warningLog.c_str());
		}

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP load";
		for (history::Action* h : *h1) complexAction->push(h);
		delete h1;
		for (history::Action* h : *h2) complexAction->push(h);
		delete h2;
		for (history::Action* h : *h3) complexAction->push(h);
		delete h3;
		for (history::Action* h : *h4) complexAction->push(h);
		delete h4;
		for (history::Action* h : *h5) complexAction->push(h);
		delete h5;
		APP->history->push(complexAction);
	}

	void groupPasteClipboard() {
		const char* moduleJson = glfwGetClipboardString(APP->window->win);
		if (!moduleJson) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not get text from clipboard.");
			return;
		}

		json_error_t error;
		json_t* rootJ = json_loads(moduleJson, 0, &error);
		if (!rootJ) {
			std::string message = string::f("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupFromJson(rootJ);
	}

	void groupSelectionPasteClipboard() {
		APP->scene->rack->deselectAll();

		const char* moduleJson = glfwGetClipboardString(APP->window->win);
		if (!moduleJson) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not get text from clipboard.");
			return;
		}

		json_error_t error;
		json_t* rootJ = json_loads(moduleJson, 0, &error);
		if (!rootJ) {
			std::string message = string::f("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupSelectionFromJson(rootJ);
	}

	void groupLoadFile(std::string filename, bool replace) {
		INFO("Loading preset %s", filename.c_str());

		FILE* file = fopen(filename.c_str(), "r");
		if (!file) {
			std::string message = string::f("Could not load file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			fclose(file);
		});

		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		if (!rootJ) {
			std::string message = string::f("File is not a valid file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		if (replace) groupReplaceFromJson(rootJ);
		else groupFromJson(rootJ);
	}

	void groupLoadFileDialog(bool replace) {
		osdialog_filters* filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		char* path = osdialog_file(OSDIALOG_OPEN, dirVcvss.c_str(), NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			dirVcvss = system::getDirectory(std::string(path));
			free(path);
		});

		groupLoadFile(path, replace);
	}

	void groupSelectionLoadFile(std::string path) {
		FILE* file = std::fopen(path.c_str(), "r");
		if (!file) return;
		DEFER({std::fclose(file);});
		INFO("Loading selection %s", path.c_str());

		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		if (!rootJ)
			throw Exception("File is not a valid selection file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
		DEFER({json_decref(rootJ);});

		groupSelectionFromJson(rootJ);
	}

	std::string groupSelectionLoadFileDialog(bool load) {
		osdialog_filters* filters = osdialog_filters_parse(SELECTION_FILTERS);
		DEFER({osdialog_filters_free(filters);});

		char* pathC = osdialog_file(OSDIALOG_OPEN, dirVcvs.c_str(), NULL, filters);
		if (!pathC) {
			// No path selected
			return "";
		}
		DEFER({
			dirVcvs = system::getDirectory(std::string(pathC));
			std::free(pathC);
		});

		try {
			if (load) groupSelectionLoadFile(pathC);
		}
		catch (Exception& e) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, e.what());
		}

		return std::string(pathC);
	}


	void onHoverKey(const event::HoverKey& e) override {
		ModuleWidget::onHoverKey(e);
		if (e.isConsumed())
			return;

		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			switch (e.key) {
				case GLFW_KEY_X: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						groupCutClipboard();
						e.consume(this);
					}
				} break;
				case GLFW_KEY_L: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						groupLoadFileDialog(false);
						e.consume(this);
					}
					if ((e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT | RACK_MOD_CTRL)) {
						groupLoadFileDialog(true);
						e.consume(this);
					}
				} break;
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
				case GLFW_KEY_S: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						groupSaveFileDialog();
						e.consume(this);
					}
				} break;
			}
		}
	}


	struct PresetItem : MenuItem {
		MODULE* module;
		StripWidgetBase* mw;
		std::string presetPath;
		void onAction(const event::Action& e) override {
			mw->groupLoadFile(presetPath,module->presetLoadReplace);
		}
	};

	struct PresetSubItem : MenuItem {
		MODULE* module;
		StripWidgetBase* mw;
		std::string dir;
		PresetSubItem() {
			rightText = RIGHT_ARROW;
		}
		Menu* createChildMenu() override {
			Menu* menu = new Menu;
			populatePresets(module, mw, menu, dir);
			return menu;
		}

		static void populatePresets(MODULE* module, StripWidgetBase* mw, Menu* menu, std::string dir) {
			auto endsWith = [](const std::string& str, const std::string& suffix) {
				return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
			};

			std::vector<std::string> presetPaths;
			for (const std::string& presetPath : system::getEntries(dir)) {
				presetPaths.push_back(presetPath);
			}

			for (const std::string& presetPath : presetPaths) {
				if (system::isDirectory(presetPath)) {
					menu->addChild(construct<PresetSubItem>(&MenuItem::text, system::getFilename(presetPath), &PresetSubItem::dir, presetPath, &PresetSubItem::module, module, &PresetSubItem::mw, mw));
				}
			}
			for (const std::string& presetPath : presetPaths) {
				if (system::isFile(presetPath)) {
					if (!endsWith(presetPath, ".vcvss")) continue;
					std::string presetName = system::getStem(system::getFilename(presetPath));
					menu->addChild(construct<PresetItem>(&MenuItem::text, presetName, &PresetItem::presetPath, presetPath, &PresetItem::module, module, &PresetItem::mw, mw));
				}
			}
		}
	};

	struct PresetMenuItem : MenuItem {
		struct PresetFolderItem : MenuItem {
			std::string path;
			void onAction(const event::Action& e) override {
				system::openDirectory(path);
			}
		};

		struct PresetLoadReplaceItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->presetLoadReplace ^= true;
				e.consume(NULL);
			}
			void step() override {
				rightText = CHECKMARK(module->presetLoadReplace);
				MenuItem::step();
				box.size.x = 140.f;
			}
		};

		MODULE* module;
		StripWidgetBase* mw;
		PresetMenuItem() {
			rightText = RIGHT_ARROW;
		}
		Menu* createChildMenu() override {
			Menu* menu = new Menu;

			std::string presetDir = mw->model->getFactoryPresetDirectory();
			menu->addChild(construct<PresetFolderItem>(&MenuItem::text, "Open folder", &PresetFolderItem::path, presetDir));
			menu->addChild(construct<PresetLoadReplaceItem>(&MenuItem::text, "Load and replace", &PresetLoadReplaceItem::module, module));
			menu->addChild(new MenuSeparator);
			PresetSubItem::populatePresets(module, mw, menu, presetDir);
			return menu;
		}
	};
};

} // namespace Strip
} // namespace StoermelderPackOne