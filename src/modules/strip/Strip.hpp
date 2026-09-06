#pragma once
#include "../../plugin.hpp"
#include "../../utils/StripIdFixModule.hpp"
#include "../../vcv/api.hpp"       // modules, cables, scene, ui, fs, history accesses
#include "../../vcv/files.hpp"     // layer-3 orchestration + production lookups
#include "../../vcv/selection.hpp" // layer-1 pure JSON/geometry
#include <plugin.hpp>

namespace StoermelderPackOne {
namespace Strip {

static const char PRESET_FILTERS[] = "stoermelder STRIP group preset (.vcvss):vcvss";


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
	/** Collects per-module load failures; shown to the user once at the end of a load. */
	std::string warningLog;

	StripWidgetBase(MODULE* module, std::string baseName)
	: ThemedModuleWidget<MODULE>(module, baseName) { }

	/** Asks the user about modules in a .vcvss group file that are not installed. */
	void groupCheckUnavailable(json_t* rootJ) {
		vcv::promptUnavailableModules(vcv::findUnavailableStripModules(rootJ, vcv::productionModelLookup()));
	}

	/** Asks the user about modules in a .vcvs selection file that are not installed. */
	void groupSelectionCheckUnavailable(json_t* rootJ) {
		vcv::promptUnavailableModules(vcv::findUnavailableModules(rootJ, vcv::productionModelLookup()));
	}


	/**
	 * Removes all modules in the group. Used for "cut" in cut & paste.
	 */
	void groupRemove() {
		// Collect all modules right next to this instance of STRIP.
		std::vector<int64_t> toBeRemoved;
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;
				toBeRemoved.push_back(m->rightExpander.moduleId);
				m = m->rightExpander.module;
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				toBeRemoved.push_back(m->leftExpander.moduleId);
				m = m->leftExpander.module;
			}
		}

		if (toBeRemoved.size() > 0) {
			history::ComplexAction* complexAction = new history::ComplexAction;
			complexAction->name = "stoermelder STRIP remove";

			for (int64_t id : toBeRemoved) {
				ModuleWidget* mw = vcv::getModuleWidget(id);
				if (!mw) continue;

				mw->appendDisconnectActions(complexAction);

				// history::ModuleRemove
				history::ModuleRemove* h = new history::ModuleRemove;
				h->setModule(mw);
				complexAction->push(h);

				// Removes the widget from the rack and deletes it; the ModuleRemove action
				// above holds the JSON needed to restore it.
				vcv::removeModule(id);
			}

			vcv::history::push(complexAction);
		}
	}

	/**
	 *  Make enough space directly next to this instance of STRIP for the new modules.
	 */
	std::vector<history::Action*> groupClearSpace(json_t* rootJ) {
		// To make sure there is enough space for the modules shove the existing modules to the
		// left and to the right. This is done by moving this instance of STRIP stepwise 1HP until enough
		// space is cleared on both sides. Why this stupid and not just use setModulePosForce?
		// Because setModulePosForce will clear the space, but is not certain in which direction the
		// existing modules will be moved because a new big module will push a small module to its closer
		// side. This would result to foreign modules within the loaded strip.

		std::vector<history::Action*> undoActions;
		std::map<int64_t, math::Vec> moduleMovePositions;

		for (ModuleWidget* mw : vcv::getModuleWidgets()) {
			if (!mw->module) continue;
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

		for (ModuleWidget* mw : vcv::getModuleWidgets()) {
			if (!mw->module) continue;
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
				undoActions.push_back(mmh);
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
				if (!m || m->rightExpander.moduleId < 0) break;
				m = m->rightExpander.module;
				assert(m);
				StripBayBase* sc = dynamic_cast<StripBayBase*>(m);
				if (sc) toDo.push_back(sc);
				moduleIds.insert(m->id);
			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				m = m->leftExpander.module;
				assert(m);
				StripBayBase* sc = dynamic_cast<StripBayBase*>(m);
				if (sc) toDo.push_back(sc);
				moduleIds.insert(m->id);
			}
		}

		for (StripBayBase* sc : toDo) {
			ModuleWidget* mw = vcv::getModuleWidget(sc->id);
			if (!mw) continue;
			for (PortWidget* in : mw->getInputs()) {
				std::vector<CableWidget*> cs = APP->scene->rack->getCablesOnPort(in);
				for (CableWidget* c : cs) {
					auto it = moduleIds.find(c->outputPort->module->id);
					// Other end is outside of this strip
					if (it == moduleIds.end()) {
						conn.push_back(std::make_tuple(sc->getConnId(), c->inputPort->portId, c->outputPort, c->color));
					}
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

	std::vector<history::Action*> groupConnectionsRestore(std::list<std::tuple<std::string, int, PortWidget*, NVGcolor>>& conn) {
		std::vector<history::Action*> undoActions;
		std::map<std::string, StripBayBase*> toDo;

		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			Module* m = module;
			while (true) {
				if (!m || m->rightExpander.moduleId < 0) break;
				ModuleWidget* mw = vcv::getModuleWidget(m->rightExpander.moduleId);
				assert(mw);
				m = mw->getModule();
				StripBayBase* sc = dynamic_cast<StripBayBase*>(m);
				if (sc) toDo[sc->getConnId()] = sc;

			}
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			Module* m = module;
			while (true) {
				if (!m || m->leftExpander.moduleId < 0) break;
				ModuleWidget* mw = vcv::getModuleWidget(m->leftExpander.moduleId);
				assert(mw);
				m = mw->getModule();
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

			ModuleWidget* mw = vcv::getModuleWidget((*it).second->id);
			if (!mw) continue;
			PortWidget* pw2 = pw1->type == engine::Port::INPUT ? mw->getOutput(portId) : mw->getInput(portId);
			assert(pw2);

			// addToHistory=false: the CableAdd is folded into the enclosing load's single
			// ComplexAction by the caller, not pushed as its own undo entry.
			history::CableAdd* h = (pw1->type == engine::Port::INPUT)
				? vcv::addCableToPort(pw2->module->id, pw2->portId, pw1->module->id, pw1->portId, false, color)
				: vcv::addCableToPort(pw1->module->id, pw1->portId, pw2->module->id, pw2->portId, false, color);
			if (h) undoActions.push_back(h);
		}

		return undoActions;
	}

	/**
	 * Adds modules next to this module according to the supplied json-representation.
	 * @rootJ json-representation of the STRIP-file
	 * @idMap maps old module ids to the new module ids
	 */
	std::vector<history::Action*> groupFromJson_modules(json_t* rootJ, std::map<int64_t, int64_t>& idMap) {
		std::vector<history::Action*> undoActions;
		vcv::WidthLookup widthLookup = vcv::productionWidthLookup();

		auto place = [&](vcv::StripSide side) {
			Rect anchor = this->box;
			for (const vcv::ModulePlacement& p : vcv::layoutStrip(rootJ, anchor, side, widthLookup)) {
				vcv::ModuleRef ref;
				if (!vcv::readModuleRef(p.moduleJ, ref)) {
					warningLog += "Could not read a module entry of the strip\n";
					continue;
				}

				int64_t newId = vcv::addModule(ref, p.pos);
				if (newId < 0) {
					warningLog += string::f("Could not find module \"%s\" of plugin \"%s\"\n",
					                        ref.modelSlug.c_str(), ref.pluginSlug.c_str());
					continue;
				}
				idMap[p.oldId] = newId;

				// ModuleAdd history action
				if (ModuleWidget* mw = vcv::getModuleWidget(newId)) {
					history::ModuleAdd* h = new history::ModuleAdd;
					h->name = "create module";
					h->setModule(mw);
					undoActions.push_back(h);
				}
			}
		};

		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			place(vcv::StripSide::RIGHT);
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			place(vcv::StripSide::LEFT);
		}

		return undoActions;
	}

	/**
	 * Adds the modules of a .vcvs selection to the rack, positioned relative to the mouse.
	 * @rootJ json-representation of the selection file
	 * @idMap maps old module ids to the new module ids
	 */
	std::vector<history::Action*> groupSelectionFromJson_modules(json_t* rootJ, std::map<int64_t, int64_t>& idMap) {
		std::vector<history::Action*> undoActions;

		// Transient pointer input, deliberately not behind an access (plan §4.3): read here
		// and passed into the pure layout function as the origin.
		Vec origin = APP->scene->rack->getMousePos();
		for (const vcv::ModulePlacement& p : vcv::layoutSelection(json_object_get(rootJ, "modules"), origin)) {
			vcv::ModuleRef ref;
			if (!vcv::readModuleRef(p.moduleJ, ref)) {
				warningLog += "Could not read a module entry of the selection\n";
				continue;
			}

			int64_t newId = vcv::addModule(ref, p.pos);
			if (newId < 0) {
				warningLog += string::f("Could not find module \"%s\" of plugin \"%s\"\n",
				                        ref.modelSlug.c_str(), ref.pluginSlug.c_str());
				continue;
			}
			idMap[p.oldId] = newId;

			// ModuleAdd history action
			if (ModuleWidget* mw = vcv::getModuleWidget(newId)) {
				history::ModuleAdd* h = new history::ModuleAdd;
				h->name = "create module";
				h->setModule(mw);
				undoActions.push_back(h);
			}

			vcv::scene::select(newId);
		}

		return undoActions;
	}

	/**
	 * Applies one module's preset from a loaded json-representation, fixing parameter mappings
	 * and old module ids on the way. Appends the resulting undo action to @undoActions.
	 * @moduleJ json-representation of the module (modified in place)
	 * @idMap maps old module ids to the new module ids
	 */
	void groupFromJson_preset(json_t* moduleJ, std::map<int64_t, int64_t>& idMap, std::vector<history::Action*>& undoActions) {
		vcv::fixParamMappings(moduleJ, idMap);

		int64_t oldId = vcv::readModuleId(moduleJ);
		auto it = idMap.find(oldId);
		if (it == idMap.end()) return;
		int64_t newId = it->second;

		ModuleWidget* mw = vcv::getModuleWidget(newId);

		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "load module preset";
		h->moduleId = newId;
		h->oldModuleJ = vcv::toJson(newId);

		// Hand the old→new id map to modules that store module ids themselves (mappings,
		// module restrictions, ...) so their dataFromJson can rewrite them.
		StripIdFixModule* m = mw ? dynamic_cast<StripIdFixModule*>(mw->module) : nullptr;
		if (m) m->idFixDataFromJson(idMap);

		// Drop engine-runtime binding fields, like Rack's own preset loader.
		vcv::jsonStripIds(moduleJ);

		vcv::applyPreset(newId, moduleJ);

		h->newModuleJ = vcv::toJson(newId);
		undoActions.push_back(h);
	}

	/**
	 * Loads all the presets from a json-representation generated by STRIP. Assumes the modules are there.
	 * Presets of non-existing modules will be skipped.
	 * @rootJ json-representation of the STRIP-file
	 * @idMap maps old module ids to the new module ids
	 */
	std::vector<history::Action*> groupFromJson_presets(json_t* rootJ, std::map<int64_t, int64_t>& idMap) {
		std::vector<history::Action*> undoActions;

		auto applyAll = [&](const char* key) {
			json_t* modulesJ = json_object_get(rootJ, key);
			if (!modulesJ) return;
			json_t* moduleJ;
			size_t moduleIndex;
			json_array_foreach(modulesJ, moduleIndex, moduleJ) {
				groupFromJson_preset(moduleJ, idMap, undoActions);
			}
		};

		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::RIGHT) {
			applyAll("rightModules");
		}
		if (module->mode == MODE::LEFTRIGHT || module->mode == MODE::LEFT) {
			applyAll("leftModules");
		}

		return undoActions;
	}

	std::vector<history::Action*> groupSelectionFromJson_presets(json_t* rootJ, std::map<int64_t, int64_t>& idMap) {
		std::vector<history::Action*> undoActions;

		json_t* modulesJ = json_object_get(rootJ, "modules");
		json_t* moduleJ;
		size_t moduleIndex;
		json_array_foreach(modulesJ, moduleIndex, moduleJ) {
			groupFromJson_preset(moduleJ, idMap, undoActions);
		}

		return undoActions;
	}

	/**
	 * Adds cables loaded from a json-representation generated by STRIP.
	 * If a module is missing the cable will be obviously skipped.
	 * @rootJ json-representation of the STRIP-file
	 * @idMap maps old module ids to the new module ids
	 */
	std::vector<history::Action*> groupFromJson_cables(json_t* rootJ, std::map<int64_t, int64_t>& idMap) {
		std::vector<history::Action*> undoActions;

		json_t* cablesJ = json_object_get(rootJ, "cables");
		if (!cablesJ) return undoActions;

		json_t* cableJ;
		size_t cableIndex;
		json_array_foreach(cablesJ, cableIndex, cableJ) {
			int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
			int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
			int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
			int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
			const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

			// In case one of the modules could not be loaded
			auto outIt = idMap.find(outputModuleId);
			auto inIt = idMap.find(inputModuleId);
			if (outIt == idMap.end() || inIt == idMap.end()) continue;

			NVGcolor color = color::BLACK_TRANSPARENT;
			if (colorStr) color = color::fromHexString(colorStr);

			// addToHistory=false: the CableAdd is handed back unowned and folded into the
			// load's single ComplexAction, rather than becoming its own undo entry.
			if (history::CableAdd* h = vcv::addCableToPort(outIt->second, outputId, inIt->second, inputId, false, color)) {
				undoActions.push_back(h);
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
				if (!m || m->rightExpander.moduleId < 0) break;
				ModuleWidget* mw = vcv::getModuleWidget(m->rightExpander.moduleId);
				if (!mw) break;
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
				if (!m || m->leftExpander.moduleId < 0) break;
				ModuleWidget* mw = vcv::getModuleWidget(m->leftExpander.moduleId);
				if (!mw) break;
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
					ModuleWidget* inputModule = vcv::getModuleWidget(input->module->id);
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

	/** Serializes the current group and returns it as a JSON string. */
	std::string groupToJsonString() {
		json_t* rootJ = json_object();
		groupToJson(rootJ);
		DEFER({
			json_decref(rootJ);
		});

		char* moduleJson = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		if (!moduleJson) return "";
		DEFER({
			free(moduleJson);
		});
		return std::string(moduleJson);
	}

	void groupCopyClipboard() {
		vcv::ui::setClipboard(groupToJsonString());
	}

	void groupCutClipboard() {
		vcv::ui::setClipboard(groupToJsonString());
		groupRemove();
	}

	void groupSaveFile(std::string filename) {
		INFO("Saving preset %s", filename.c_str());

		std::string data = groupToJsonString();
		if (!vcv::fs::write(filename, data)) {
			std::string message = string::f("Could not write to patch file %s", filename.c_str());
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, message);
		}
	}

	void groupSaveFileDialog() {
		std::string path = vcv::ui::saveDialog(PRESET_FILTERS, pluginSettings.stripDirVcvss, "Untitled.vcvss");
		if (path.empty()) {
			// No path selected
			return;
		}
		pluginSettings.stripDirVcvss = vcv::fs::getDirectory(path);
		pluginSettings.saveToJson();

		std::string extension = vcv::fs::getExtension(vcv::fs::getFilename(path));
		if (extension.empty()) {
			path += ".vcvss";
		}

		groupSaveFile(path);
	}

	/** Shows the accumulated per-module load failures, if any, and clears the log. */
	void groupWarningLogFlush() {
		if (warningLog.empty()) return;
		vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, warningLog);
		warningLog = "";
	}

	void groupFromJson(json_t* rootJ) {
		warningLog = "";

		// Clear modules next to STRIP
		std::vector<history::Action*> h1 = groupClearSpace(rootJ);

		// Maps old moduleId to the id of the newly created module
		std::map<int64_t, int64_t> idMap;
		// Add modules
		std::vector<history::Action*> h2 = groupFromJson_modules(rootJ, idMap);
		// Load presets for modules, also fixes parameter mappings
		std::vector<history::Action*> h3 = groupFromJson_presets(rootJ, idMap);

		// Add cables
		std::vector<history::Action*> h4 = groupFromJson_cables(rootJ, idMap);

		// Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
		APP->scene->rack->requestModulePos(this, this->box.pos);

		groupWarningLogFlush();

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP load";
		for (history::Action* h : h1) complexAction->push(h);
		for (history::Action* h : h2) complexAction->push(h);
		for (history::Action* h : h3) complexAction->push(h);
		for (history::Action* h : h4) complexAction->push(h);
		vcv::history::push(complexAction);
	}

	void groupSelectionFromJson(json_t* rootJ) {
		warningLog = "";

		// Maps old moduleId to the id of the newly created module
		std::map<int64_t, int64_t> idMap;
		// Add modules
		std::vector<history::Action*> h2 = groupSelectionFromJson_modules(rootJ, idMap);
		// Load presets for modules, also fixes parameter mappings
		std::vector<history::Action*> h3 = groupSelectionFromJson_presets(rootJ, idMap);

		// Add cables
		std::vector<history::Action*> h4 = groupFromJson_cables(rootJ, idMap);

		// Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
		APP->scene->rack->requestModulePos(this, this->box.pos);

		groupWarningLogFlush();

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP selection load";
		for (history::Action* h : h2) complexAction->push(h);
		for (history::Action* h : h3) complexAction->push(h);
		for (history::Action* h : h4) complexAction->push(h);
		vcv::history::push(complexAction);
	}

	void groupReplaceFromJson(json_t* rootJ) {
		warningLog = "";

		std::list<std::tuple<std::string, int, PortWidget*, NVGcolor>> conn;

		// Collect all connections outside the strip using StripCon modules
		groupConnectionsCollect(conn);

		// Remove all modules adjacent to STRIP
		groupRemove();

		// Clear modules next to STRIP
		std::vector<history::Action*> h1 = groupClearSpace(rootJ);

		// Maps old moduleId to the id of the newly created module
		std::map<int64_t, int64_t> idMap;
		// Add modules
		std::vector<history::Action*> h2 = groupFromJson_modules(rootJ, idMap);
		// Load presets for modules, also fixes parameter mappings
		std::vector<history::Action*> h3 = groupFromJson_presets(rootJ, idMap);

		// Add cables
		std::vector<history::Action*> h4 = groupFromJson_cables(rootJ, idMap);

		// Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
		APP->scene->rack->requestModulePos(this, this->box.pos);

		// Restore cables from StripCon-modules
		std::vector<history::Action*> h5 = groupConnectionsRestore(conn);

		groupWarningLogFlush();

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder STRIP load";
		for (history::Action* h : h1) complexAction->push(h);
		for (history::Action* h : h2) complexAction->push(h);
		for (history::Action* h : h3) complexAction->push(h);
		for (history::Action* h : h4) complexAction->push(h);
		for (history::Action* h : h5) complexAction->push(h);
		vcv::history::push(complexAction);
	}

	void groupPasteClipboard() {
		std::string moduleJson = vcv::ui::getClipboard();
		if (moduleJson.empty()) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, "Could not get text from clipboard.");
			return;
		}

		std::string error;
		json_t* rootJ = vcv::parseJson(moduleJson, error);
		if (!rootJ) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, error);
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupFromJson(rootJ);
	}

	void groupSelectionPasteClipboard() {
		vcv::scene::deselectAll();

		std::string moduleJson = vcv::ui::getClipboard();
		if (moduleJson.empty()) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, "Could not get text from clipboard.");
			return;
		}

		std::string error;
		json_t* rootJ = vcv::parseJson(moduleJson, error);
		if (!rootJ) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, error);
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupSelectionFromJson(rootJ);
	}

	void groupLoadFile(std::string filename, bool replace) {
		INFO("Loading preset %s", filename.c_str());

		std::string data;
		if (!vcv::fs::read(filename, data)) {
			std::string message = string::f("Could not load file %s", filename.c_str());
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, message);
			return;
		}

		std::string error;
		json_t* rootJ = vcv::parseJson(data, error);
		if (!rootJ) {
			std::string message = string::f("File is not a valid file. %s", error.c_str());
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, message);
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupCheckUnavailable(rootJ);
		if (replace) groupReplaceFromJson(rootJ);
		else groupFromJson(rootJ);
	}

	void groupLoadFileDialog(bool replace) {
		std::string path = vcv::ui::openDialog(PRESET_FILTERS, pluginSettings.stripDirVcvss);
		if (path.empty()) {
			// No path selected
			return;
		}
		pluginSettings.stripDirVcvss = vcv::fs::getDirectory(path);
		pluginSettings.saveToJson();

		groupLoadFile(path, replace);
	}

	void groupSelectionLoadFile(std::string path) {
		std::string data;
		if (!vcv::fs::read(path, data)) return;
		INFO("Loading selection %s", path.c_str());

		std::string error;
		json_t* rootJ = vcv::parseJson(data, error);
		if (!rootJ) {
			throw Exception("File is not a valid selection file. %s", error.c_str());
		}
		DEFER({json_decref(rootJ);});

		groupSelectionCheckUnavailable(rootJ);
		groupSelectionFromJson(rootJ);
	}

	std::string groupSelectionLoadFileDialog(bool load) {
		std::string path = vcv::ui::openDialog(vcv::SELECTION_FILTERS, pluginSettings.stripDirVcvs);
		if (path.empty()) {
			// No path selected
			return "";
		}
		pluginSettings.stripDirVcvs = vcv::fs::getDirectory(path);
		pluginSettings.saveToJson();

		try {
			if (load) groupSelectionLoadFile(path);
		}
		catch (Exception& e) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, e.what());
		}

		return path;
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
			for (const std::string& presetPath : vcv::fs::getEntries(dir)) {
				presetPaths.push_back(presetPath);
			}

			for (const std::string& presetPath : presetPaths) {
				if (vcv::fs::isDirectory(presetPath)) {
					menu->addChild(construct<PresetSubItem>(&MenuItem::text, vcv::fs::getFilename(presetPath), &PresetSubItem::dir, presetPath, &PresetSubItem::module, module, &PresetSubItem::mw, mw));
				}
			}
			for (const std::string& presetPath : presetPaths) {
				if (vcv::fs::isFile(presetPath)) {
					if (!endsWith(presetPath, ".vcvss")) continue;
					std::string presetName = vcv::fs::getStem(vcv::fs::getFilename(presetPath));
					menu->addChild(construct<PresetItem>(&MenuItem::text, presetName, &PresetItem::presetPath, presetPath, &PresetItem::module, module, &PresetItem::mw, mw));
				}
			}
		}
	};

	struct PresetMenuItem : MenuItem {
		struct PresetFolderItem : MenuItem {
			std::string path;
			void onAction(const event::Action& e) override {
				vcv::fs::openDirectory(path);
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