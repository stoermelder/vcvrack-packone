#pragma once
#include "../plugin.hpp"
#include "modules.hpp"   // for ModuleRef (lives in modules.hpp, not layer 1 — plan §4.1)
#include <functional>
#include <algorithm>
#include <limits>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <tuple>

namespace StoermelderPackOne {
namespace vcv {

// ---- Layer 1: pure JSON + geometry ----
// Every function here takes JSON or PODs and returns JSON or PODs. No `APP`, no
// `osdialog`, no `plugin::getModel` — the only Rack-world lookup (model existence /
// module width) arrives via an injected callback, so this whole header is directly
// testable in a `TestContext`-free binary.
//
// NB: the plan sketched std::optional return types; this plugin builds with -std=c++11,
// so the two "maybe" readers return bool + out-param instead.

// --- value types -----------------------------------------------------------

// One module's placement decision, resolved before anything touches Rack.
// `moduleJ` is borrowed (not owned); `pos` is absolute, in pixels.
struct ModulePlacement {
	json_t* moduleJ = nullptr;   // borrowed, not owned
	int64_t oldId   = -1;
	Vec     pos     = Vec(0.f, 0.f);

	ModulePlacement() {}
	ModulePlacement(json_t* mj, int64_t id, Vec p) : moduleJ(mj), oldId(id), pos(p) {}
};

// Identity of a module in a patch file. Defined in modules.hpp; layer 1 includes it
// from there rather than redefining it (plan §4.1).
// struct ModuleRef { std::string pluginSlug, modelSlug; };   // see modules.hpp

// Module width in pixels, 0 if the model is unknown. In production this is backed by
// plugin::getModel; in tests it is a fixed slug→width table.
using WidthLookup = std::function<float(const ModuleRef&)>;

// Whether a module's model exists in the running plugin. Injected so findUnavailableModules
// stays pure — production passes a callback backed by plugin::getModel / modelFromJson.
using ModelLookup = std::function<bool(const ModuleRef&)>;

// Which side of the strip anchor `layoutStrip` lays modules out on.
enum class StripSide { LEFT, RIGHT };

// --- readers ---------------------------------------------------------------

// false when "plugin" or "model" is missing or not a string. Centralises the missing
// null-guard that moduleToRack's failure branch was missing.
inline bool readModuleRef(json_t* moduleJ, ModuleRef& out) {
	json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
	if (!pluginSlugJ || !json_is_string(pluginSlugJ)) return false;
	json_t* modelSlugJ = json_object_get(moduleJ, "model");
	if (!modelSlugJ || !json_is_string(modelSlugJ)) return false;
	out.pluginSlug = json_string_value(pluginSlugJ);
	out.modelSlug = json_string_value(modelSlugJ);
	return true;
}

// -1 when "id" is absent.
inline int64_t readModuleId(json_t* moduleJ) {
	json_t* idJ = json_object_get(moduleJ, "id");
	return idJ ? json_integer_value(idJ) : -1;
}

// Reads "pos" in grid units. false when "pos" is absent or malformed; `out` is then reset
// to (0,0), matching the json_unpack behaviour of the code this was extracted from.
inline bool readModulePos(json_t* moduleJ, Vec& out) {
	json_t* posJ = json_object_get(moduleJ, "pos");
	if (!posJ) {
		out = Vec(0.f, 0.f);
		return false;
	}
	double x = 0.0, y = 0.0;
	if (json_unpack(posJ, "[F, F]", &x, &y)) {
		out = Vec(0.f, 0.f);
		return false;
	}
	out = Vec(x, y);
	return true;
}

// --- layout -----------------------------------------------------------------

// Was vcvsFromJson_modules' first half. Given the "modules" array and an origin, returns
// one ModulePlacement per entry, positions normalized to the array's top-left and offset
// to `origin` (grid units → pixels via RACK_GRID_SIZE).
inline std::vector<ModulePlacement> layoutSelection(json_t* modulesJ, Vec origin) {
	std::vector<ModulePlacement> placements;
	if (!modulesJ || !json_is_array(modulesJ)) return placements;

	double minX = std::numeric_limits<float>::infinity();
	double minY = std::numeric_limits<float>::infinity();
	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		Vec pos;
		readModulePos(moduleJ, pos);
		minX = std::min(minX, (double) pos.x);
		minY = std::min(minY, (double) pos.y);
	}

	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		Vec pos;
		readModulePos(moduleJ, pos);
		pos = pos.minus(Vec(minX, minY)).mult(RACK_GRID_SIZE);
		pos = origin.plus(pos);
		placements.push_back(ModulePlacement(moduleJ, readModuleId(moduleJ), pos));
	}
	return placements;
}

// Was Strip's groupFromJson_modules geometry: place `rightModules` rightward /
// `leftModules` leftward of `anchor`, using widths from `lookup`.
inline std::vector<ModulePlacement> layoutStrip(json_t* rootJ, Rect anchor, StripSide side, WidthLookup lookup) {
	std::vector<ModulePlacement> placements;
	if (!rootJ || !json_is_object(rootJ)) return placements;

	const char* key = (side == StripSide::RIGHT) ? "rightModules" : "leftModules";
	json_t* modulesJ = json_object_get(rootJ, key);
	if (!modulesJ || !json_is_array(modulesJ)) return placements;

	float x = (side == StripSide::RIGHT) ? anchor.pos.x + anchor.size.x : anchor.pos.x;
	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		ModuleRef ref;
		float width = readModuleRef(moduleJ, ref) ? lookup(ref) : 0.f;
		if (side == StripSide::LEFT) x -= width;
		placements.push_back(ModulePlacement(moduleJ, readModuleId(moduleJ), Vec(x, anchor.pos.y)));
		if (side == StripSide::RIGHT) x += width;
	}
	return placements;
}

// --- vcvss → vcvs -----------------------------------------------------------

// Was convertVcvssToVcvs, minus the widget construction: widths come from `lookup`
// instead of building a throwaway ModuleWidget. Returns a new object (caller decrefs),
// or nullptr on failure.
inline json_t* convertVcvssToVcvs(json_t* vcvssJ, WidthLookup lookup) {
	if (!vcvssJ || !json_is_object(vcvssJ)) return nullptr;

	json_t* vcvsJ = json_object();
	if (!vcvsJ) return nullptr;

	json_t* modulesJ = json_array();
	if (!modulesJ) {
		json_decref(vcvsJ);
		return nullptr;
	}

	// Left modules are stored right-to-left (first element = rightmost); emit left-to-right.
	float currentX = 0.f;
	json_t* leftModulesJ = json_object_get(vcvssJ, "leftModules");
	if (leftModulesJ && json_is_array(leftModulesJ)) {
		size_t leftCount = json_array_size(leftModulesJ);

		// First pass: collect widths (in grid units) and calculate total left width.
		std::vector<float> leftWidths;
		leftWidths.reserve(leftCount);
		for (size_t i = 0; i < leftCount; i++) {
			json_t* moduleJ = json_array_get(leftModulesJ, i);
			ModuleRef ref;
			float width = readModuleRef(moduleJ, ref) ? lookup(ref) : 0.f;
			leftWidths.push_back(width / RACK_GRID_SIZE.x);
		}

		// Start left modules at position 0 (leftmost will be at x=0).
		currentX = 0.f;

		// Add left modules in reverse order (last element = leftmost = first in array).
		for (size_t i = leftCount; i > 0; i--) {
			size_t idx = i - 1;
			json_t* moduleJ = json_array_get(leftModulesJ, idx);
			json_t* clonedModule = json_deep_copy(moduleJ);
			if (!clonedModule) continue;

			json_object_del(clonedModule, "pos");
			json_t* posJ = json_array();
			json_array_append_new(posJ, json_real(currentX));
			json_array_append_new(posJ, json_real(0.0));
			json_object_set_new(clonedModule, "pos", posJ);

			json_array_append_new(modulesJ, clonedModule);
			currentX += leftWidths[idx];
		}
	}

	// Right modules continue after the left modules (if any).
	json_t* rightModulesJ = json_object_get(vcvssJ, "rightModules");
	if (rightModulesJ && json_is_array(rightModulesJ)) {
		json_t* moduleJ;
		size_t idx;
		json_array_foreach(rightModulesJ, idx, moduleJ) {
			json_t* clonedModule = json_deep_copy(moduleJ);
			if (!clonedModule) continue;

			ModuleRef ref;
			float width = readModuleRef(moduleJ, ref) ? lookup(ref) : 0.f;
			width /= RACK_GRID_SIZE.x;

			json_object_del(clonedModule, "pos");
			json_t* posJ = json_array();
			json_array_append_new(posJ, json_real(currentX));
			json_array_append_new(posJ, json_real(0.0));
			json_object_set_new(clonedModule, "pos", posJ);

			json_array_append_new(modulesJ, clonedModule);
			currentX += width;
		}
	}

	json_object_set_new(vcvsJ, "modules", modulesJ);

	// Copy cables through unchanged.
	json_t* cablesJ = json_object_get(vcvssJ, "cables");
	if (cablesJ) {
		json_object_set(vcvsJ, "cables", cablesJ);
		json_incref(cablesJ);
	}
	else {
		json_object_set_new(vcvsJ, "cables", json_array());
	}

	return vcvsJ;
}

// --- preset rewriting --------------------------------------------------------

// Was vcvsFromJson_presets_fixMapping, now old-id → new-id only (no ModuleWidget*).
inline void fixParamMappings(json_t* moduleJ, const std::map<int64_t, int64_t>& idMap) {
	json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
	json_t* modelSlugJ = json_object_get(moduleJ, "model");
	if (!json_is_string(pluginSlugJ) || !json_is_string(modelSlugJ)) return;
	std::string pluginSlug = json_string_value(pluginSlugJ);
	std::string modelSlug = json_string_value(modelSlugJ);

	static const std::set<std::tuple<std::string, std::string>> moduleSlugs = {
		std::make_tuple("Core", "MIDI-Map"),
		std::make_tuple("MindMeldModular", "PatchMaster")
	};

	// Only handle some specific modules known to use mapping of parameters.
	if (moduleSlugs.find(std::make_tuple(pluginSlug, modelSlug)) == moduleSlugs.end())
		return;

	json_t* dataJ = json_object_get(moduleJ, "data");
	json_t* mapsJ = json_object_get(dataJ, "maps");
	if (!mapsJ) return;

	json_t* mapJ;
	size_t mapIndex;
	json_array_foreach(mapsJ, mapIndex, mapJ) {
		json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
		if (!moduleIdJ) continue;
		int64_t oldId = json_integer_value(moduleIdJ);
		if (oldId >= 0) {
			auto it = idMap.find(oldId);
			json_object_set_new(mapJ, "moduleId", json_integer(it != idMap.end() ? it->second : -1));
		}
	}
}

// Was jsonStripIds — already pure, moved here unchanged. NB: unlike Rack's version, "id"
// is intentionally kept — callers read it for id remapping and rely on it.
inline void jsonStripIds(json_t* moduleJ) {
	json_object_del(moduleJ, "leftModuleId");
	json_object_del(moduleJ, "rightModuleId");
	json_object_del(moduleJ, "automId");
}

// --- missing-module scan ------------------------------------------------------

// Scans one array of module JSON objects, adding "plugin/model" for every entry whose model
// is not installed. Accumulates into `out` so a caller with several arrays (a .vcvss has
// leftModules and rightModules) gets one combined set.
inline void findUnavailableModulesIn(json_t* modulesJ, const ModelLookup& modelExists, std::set<std::string>& out) {
	if (!modulesJ || !json_is_array(modulesJ)) return;

	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		ModuleRef ref;
		if (!readModuleRef(moduleJ, ref)) continue;
		if (!modelExists(ref)) {
			out.insert(ref.pluginSlug + "/" + ref.modelSlug);
		}
	}
}

// Was the scanning half of vcvsCheckUnavailable; the prompt/dialog stays in layer 3.
// `modelExists` is injected so this stays pure (production: plugin::getModel-backed).
inline std::set<std::string> findUnavailableModules(json_t* rootJ, const ModelLookup& modelExists) {
	std::set<std::string> pluginModuleSlugs;
	findUnavailableModulesIn(json_object_get(rootJ, "modules"), modelExists, pluginModuleSlugs);
	return pluginModuleSlugs;
}

// The .vcvss (STRIP group) form of the scan: the modules live in "leftModules" and
// "rightModules" instead of a single "modules" array. Was Strip's groupCheckUnavailable.
inline std::set<std::string> findUnavailableStripModules(json_t* rootJ, const ModelLookup& modelExists) {
	std::set<std::string> pluginModuleSlugs;
	findUnavailableModulesIn(json_object_get(rootJ, "rightModules"), modelExists, pluginModuleSlugs);
	findUnavailableModulesIn(json_object_get(rootJ, "leftModules"), modelExists, pluginModuleSlugs);
	return pluginModuleSlugs;
}

} // namespace vcv
} // namespace StoermelderPackOne
