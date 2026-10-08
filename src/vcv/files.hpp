#pragma once
#include "../plugin.hpp"
#include "api.hpp"        // modules, cables, scene, ui, fs accesses
#include "history.hpp"    // historyAccessFor
#include "selection.hpp"  // layer-1 pure functions
#include <set>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace StoermelderPackOne {
namespace vcv {

// ---- Layer 3: orchestration ----
// The vcvs/strip load entry points, rebuilt on the swappable accesses (plan §5). Every
// Rack-world / I/O decision routes through an access, so each entry point is a short,
// fully mockable sequence. This is a refactor of utils/vcv_files.hpp; it does NOT touch
// that file or any call site yet — it exists to be adopted once the accesses are proven.

static const char SELECTION_FILTERS[] = "VCV Rack module selection (.vcvs):vcvs";

// The production "does this module's model exist?" check, used by vcvsLoadFile. Injected
// into findUnavailableModules (layer 1) so that stays pure.
inline ModelLookup productionModelLookup() {
	return [](const ModuleRef& ref) {
		return plugin::getModel(ref.pluginSlug, ref.modelSlug) != nullptr;
	};
}

// The production module-width lookup, injected into layoutStrip / convertVcvssToVcvs (layer 1)
// so those stay pure. Widths come from a throwaway ModuleWidget — the only way Rack exposes a
// model's panel width — so the results are cached per model: a 60-module strip measured each
// model once instead of constructing and destroying 60 widgets.
inline WidthLookup productionWidthLookup() {
	auto cache = std::make_shared<std::map<std::pair<std::string, std::string>, float>>();
	return [cache](const ModuleRef& ref) -> float {
		auto key = std::make_pair(ref.pluginSlug, ref.modelSlug);
		auto it = cache->find(key);
		if (it != cache->end()) return it->second;

		float width = 0.f;
		plugin::Model* model = plugin::getModel(ref.pluginSlug, ref.modelSlug);
		if (model) {
			ModuleWidget* mw = model->createModuleWidget(NULL);
			if (mw) {
				width = mw->box.size.x;
				delete mw;
			}
		}
		(*cache)[key] = width;
		return width;
	};
}

// Prompts the user to view missing modules on the VCV Library, if any.
inline void promptUnavailableModules(const std::set<std::string>& slugs) {
	if (slugs.empty()) return;

	std::string msg = "This selection/strip includes modules that are not installed. Show missing modules on the VCV Library?";
	if (uiAccessFor().message(MessageType::WARNING, MessageButtons::YES_NO, msg)) {
		std::string url = "https://library.vcvrack.com/?modules=";
		url += string::join(slugs, ",");
		uiAccessFor().openBrowser(url);
	}
}

/**
 * Loads a vcvs selection from an already-parsed root JSON object.
 * Orchestrates module placement/creation, preset application, cables and a single undo
 * entry — each step through the swappable accesses or a layer-1 pure function.
 * @param rootJ JSON representation of the vcvs file (borrowed)
 * @param undoActionName Undo entry name; "Load selection" when empty
 */
inline void vcvsFromJson(json_t* rootJ, const std::string& undoActionName = "") {
	// Maps old moduleId → the newly created module's new id.
	std::map<int64_t, int64_t> idMap;
	::rack::history::ComplexAction* complexAction = new ::rack::history::ComplexAction;
	complexAction->name = undoActionName.empty() ? "Load selection" : undoActionName;

	// 1. Place and create modules.
	Vec origin = APP->scene->rack->getMousePos();   // transient input, not an access (§4.3)
	auto placements = layoutSelection(json_object_get(rootJ, "modules"), origin);
	for (const ModulePlacement& p : placements) {
		ModuleRef ref;
		if (!readModuleRef(p.moduleJ, ref)) continue;
		int64_t newId = moduleAccessFor().addModule(ref, p.pos);
		if (newId < 0) continue;
		idMap[p.oldId] = newId;
		sceneAccessFor().select(newId);
		if (ModuleWidget* mw = moduleAccessFor().getModuleWidget(newId)) {
			::rack::history::ModuleAdd* h = new ::rack::history::ModuleAdd;
			h->setModule(mw);
			complexAction->push(h);
		}
	}

	// 2. Apply presets (and fix parameter mappings).
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (modulesJ && json_is_array(modulesJ)) {
		size_t moduleIndex;
		json_t* moduleJ;
		json_array_foreach(modulesJ, moduleIndex, moduleJ) {
			fixParamMappings(moduleJ, idMap);
			jsonStripIds(moduleJ);
			int64_t oldId = readModuleId(moduleJ);
			auto it = idMap.find(oldId);
			if (it == idMap.end()) continue;
			int64_t newId = it->second;

			ModuleWidget* mw = moduleAccessFor().getModuleWidget(newId);
			json_t* oldModuleJ = mw ? mw->toJson() : nullptr;
			moduleAccessFor().applyPreset(newId, moduleJ);
			json_t* newModuleJ = mw ? mw->toJson() : nullptr;
			if (mw) {
				::rack::history::ModuleChange* h = new ::rack::history::ModuleChange;
				h->moduleId = mw->module->id;
				h->oldModuleJ = oldModuleJ;
				h->newModuleJ = newModuleJ;
				complexAction->push(h);
			}
		}
	}

	// 3. Add cables.
	json_t* cablesJ = json_object_get(rootJ, "cables");
	if (cablesJ && json_is_array(cablesJ)) {
		size_t cableIndex;
		json_t* cableJ;
		json_array_foreach(cablesJ, cableIndex, cableJ) {
			int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
			int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
			int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
			int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
			const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

			// Skip cables whose modules failed to load.
			auto outIt = idMap.find(outputModuleId);
			auto inIt = idMap.find(inputModuleId);
			if (outIt == idMap.end() || inIt == idMap.end()) continue;

			NVGcolor color = color::BLACK_TRANSPARENT;
			if (colorStr) color = color::fromHexString(colorStr);
			// addToHistory=false: the CableAdd is handed back unowned and folded into the
			// load's single ComplexAction, rather than becoming its own undo entry.
			if (::rack::history::CableAdd* h = addCableToPort(outIt->second, outputId, inIt->second, inputId, false, color)) {
				complexAction->push(h);
			}
		}
	}

	// 4. One undo entry for the whole load.
	historyAccessFor().push(complexAction);
}

/**
 * Loads a vcvs selection file from the given path.
 * @param path Full path to the .vcvs file
 * @param undoActionName Undo entry name
 */
inline void vcvsLoadFile(const std::string& path, const std::string& undoActionName = "") {
	std::string data;
	if (!fileAccessFor().read(path, data)) {
		uiAccessFor().message(MessageType::WARNING, MessageButtons::OK, "Could not open " + path);
		return;
	}
	INFO("Loading selection %s", path.c_str());

	std::string err;
	json_t* rootJ = parseJson(data, err);
	if (!rootJ) {
		uiAccessFor().message(MessageType::WARNING, MessageButtons::OK, err);
		return;
	}
	DEFER({ json_decref(rootJ); });

	promptUnavailableModules(findUnavailableModules(rootJ, productionModelLookup()));
	vcvsFromJson(rootJ, undoActionName);
}

/**
 * Loads a vcvs selection from the system clipboard.
 * @param undoActionName Undo entry name
 */
inline void vcvsPasteClipboard(const std::string& undoActionName = "") {
	sceneAccessFor().deselectAll();

	std::string moduleJson = uiAccessFor().getClipboard();
	if (moduleJson.empty()) {
		uiAccessFor().message(MessageType::WARNING, MessageButtons::OK, "Could not get text from clipboard.");
		return;
	}

	std::string err;
	json_t* rootJ = parseJson(moduleJson, err);
	if (!rootJ) {
		uiAccessFor().message(MessageType::WARNING, MessageButtons::OK, err);
		return;
	}
	DEFER({ json_decref(rootJ); });

	vcvsFromJson(rootJ, undoActionName);
}

/**
 * Shows an open file dialog and loads a vcvs selection from the selected file.
 * @param load If true, actually loads the file; if false, just returns the selected path
 * @param undoActionName Undo entry name
 * @return The selected file path, or empty string if cancelled
 */
inline std::string vcvsLoadFileDialog(bool load, const std::string& undoActionName = "") {
	std::string path = uiAccessFor().openDialog(SELECTION_FILTERS, pluginSettings.stripDirVcvs);
	if (path.empty()) {
		// No path selected
		return "";
	}

	if (load) vcvsLoadFile(path, undoActionName);

	pluginSettings.stripDirVcvs = fileAccessFor().getDirectory(path);
	pluginSettings.saveToJson();
	return path;
}

} // namespace vcv
} // namespace StoermelderPackOne
