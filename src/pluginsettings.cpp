#include <rack.hpp>
#include "pluginsettings.hpp"

namespace StoermelderPackOne {

Settings pluginSettings;

static std::string settingsDirPath() {
	return rack::asset::user("Stoermelder-P1");
}

static std::string mbFilePath() {
	return settingsDirPath() + "/mb.json";
}

static std::string pluginFilePath() {
	return settingsDirPath() + "/plugin.json";
}

static std::string legacyFilePath() {
	return rack::asset::user("Stoermelder-P1.json");
}

static json_t* loadJsonFile(const std::string& path) {
	FILE* file = fopen(path.c_str(), "r");
	if (!file) return nullptr;
	json_error_t error;
	json_t* j = json_loadf(file, 0, &error);
	fclose(file);
	return j;
}

static bool saveJsonFile(const std::string& path, json_t* j) {
	FILE* file = fopen(path.c_str(), "w");
	if (!file) return false;
	json_dumpf(j, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
	fclose(file);
	return true;
}

static bool isTesting() {
	return getenv("TESTING") != nullptr;
}

// ── mb.json (models only) ─────────────────────────────────────────────────────

static json_t* buildMbJson(const Settings& s) {
	json_t* j = json_object();
	json_object_set(j, "models", s.mbModelsJ);
	return j;
}

static void parseMbJson(json_t* j, Settings& s) {
	if (!j) return;
	json_t* v = json_object_get(j, "models");
	if (v) s.mbModelsJ = json_copy(v);
}

// ── plugin.json ───────────────────────────────────────────────────────────────

static json_t* buildPluginJson(const Settings& s) {
	json_t* j = json_object();
	json_object_set_new(j, "panelThemeDefault", json_integer(s.panelThemeDefault));

	json_t* mbJ = json_object();
	json_object_set_new(mbJ, "zoom", json_real(s.mbZoom));
	json_object_set_new(mbJ, "sort", json_integer(s.mbSort));
	json_object_set_new(mbJ, "hideBrands", json_boolean(s.mbHideBrands));
	json_object_set_new(mbJ, "searchDescriptions", json_boolean(s.mbSearchDescriptions));
	json_object_set_new(mbJ, "sortBySearchScore", json_boolean(s.mbSortBySearchScore));
	json_object_set_new(mbJ, "favoriteHighlight", json_boolean(s.mbFavoriteHighlight));
	json_object_set_new(mbJ, "searchThreshold", json_real(s.mbSearchThreshold));
	json_object_set_new(mbJ, "magnifierEnabled", json_boolean(s.mbMagnifierEnabled));
	json_object_set_new(mbJ, "applyLibraryWhitelist", json_boolean(s.mbApplyLibraryWhitelist));
	json_object_set_new(mbJ, "showDeprecated", json_boolean(s.mbShowDeprecated));
	json_object_set_new(j, "mb", mbJ);

	json_t* overlayJ = json_object();
	json_object_set_new(overlayJ, "textColor", json_string(rack::color::toHexString(s.overlayTextColor).c_str()));
	json_object_set_new(overlayJ, "hpos", json_integer(s.overlayHpos));
	json_object_set_new(overlayJ, "vpos", json_integer(s.overlayVpos));
	json_object_set_new(overlayJ, "opacity", json_real(s.overlayOpacity));
	json_object_set_new(overlayJ, "scale", json_real(s.overlayScale));
	json_object_set_new(j, "overlay", overlayJ);

	json_t* magnifierJ = json_object();
	json_object_set_new(magnifierJ, "key", json_integer(s.magnifierKey));
	json_object_set_new(magnifierJ, "mods", json_integer(s.magnifierMods));
	json_object_set_new(magnifierJ, "radius", json_real(s.magnifierRadius));
	json_object_set_new(magnifierJ, "zoom", json_real(s.magnifierZoom));
	json_object_set_new(j, "magnifier", magnifierJ);

	json_t* stripJ = json_object();
	json_object_set_new(stripJ, "dirVcvss", json_string(s.stripDirVcvss.c_str()));
	json_object_set_new(stripJ, "dirVcvs", json_string(s.stripDirVcvs.c_str()));
	json_object_set_new(j, "strip", stripJ);

	json_t* midiEsxJ = json_object();
	json_object_set_new(midiEsxJ, "driverEnabled", json_boolean(s.midiEsxDriverEnabled));
	json_object_set_new(j, "midiEsx", midiEsxJ);

	json_t* ahabJ = json_object();
	json_object_set_new(ahabJ, "info", json_boolean(s.ahabInfo));
	json_object_set_new(ahabJ, "midiVirtualEnabled", json_boolean(s.ahabMidiVirtualEnabled));
	json_object_set_new(j, "ahab", ahabJ);

	return j;
}

static void parsePluginJson(json_t* j, Settings& s) {
	if (!j) return;
	json_t* v;
	v = json_object_get(j, "panelThemeDefault"); if (v) s.panelThemeDefault = json_integer_value(v);

	json_t* mbJ = json_object_get(j, "mb");
	if (mbJ) {
		v = json_object_get(mbJ, "zoom");              if (v) s.mbZoom = json_real_value(v);
		v = json_object_get(mbJ, "sort");              if (v) s.mbSort = json_integer_value(v);
		v = json_object_get(mbJ, "hideBrands");        if (v) s.mbHideBrands = json_boolean_value(v);
		v = json_object_get(mbJ, "searchDescriptions");if (v) s.mbSearchDescriptions = json_boolean_value(v);
		v = json_object_get(mbJ, "sortBySearchScore");   if (v) s.mbSortBySearchScore = json_boolean_value(v);
		v = json_object_get(mbJ, "favoriteHighlight");   if (v) s.mbFavoriteHighlight = json_boolean_value(v);
		v = json_object_get(mbJ, "searchThreshold");     if (v) s.mbSearchThreshold = json_real_value(v);
		v = json_object_get(mbJ, "magnifierEnabled");    if (v) s.mbMagnifierEnabled = json_boolean_value(v);
		v = json_object_get(mbJ, "applyLibraryWhitelist"); if (v) s.mbApplyLibraryWhitelist = json_boolean_value(v);
		v = json_object_get(mbJ, "showDeprecated");     if (v) s.mbShowDeprecated = json_boolean_value(v);
	}

	json_t* overlayJ = json_object_get(j, "overlay");
	if (overlayJ) {
		v = json_object_get(overlayJ, "textColor"); if (v) s.overlayTextColor = rack::color::fromHexString(json_string_value(v));
		v = json_object_get(overlayJ, "hpos");      if (v) s.overlayHpos = json_integer_value(v);
		v = json_object_get(overlayJ, "vpos");      if (v) s.overlayVpos = json_integer_value(v);
		v = json_object_get(overlayJ, "opacity");   if (v) s.overlayOpacity = json_real_value(v);
		v = json_object_get(overlayJ, "scale");     if (v) s.overlayScale = json_real_value(v);
	}

	json_t* magnifierJ = json_object_get(j, "magnifier");
	if (magnifierJ) {
		v = json_object_get(magnifierJ, "key");    if (v) s.magnifierKey = json_integer_value(v);
		v = json_object_get(magnifierJ, "mods");   if (v) s.magnifierMods = json_integer_value(v);
		v = json_object_get(magnifierJ, "radius"); if (v) s.magnifierRadius = json_real_value(v);
		v = json_object_get(magnifierJ, "zoom");   if (v) s.magnifierZoom = json_real_value(v);
	}

	json_t* stripJ = json_object_get(j, "strip");
	if (stripJ) {
		v = json_object_get(stripJ, "dirVcvss"); if (v) s.stripDirVcvss = json_string_value(v);
		v = json_object_get(stripJ, "dirVcvs");  if (v) s.stripDirVcvs = json_string_value(v);
	}

	json_t* midiEsxJ = json_object_get(j, "midiEsx");
	if (midiEsxJ) {
		v = json_object_get(midiEsxJ, "driverEnabled"); if (v) s.midiEsxDriverEnabled = json_boolean_value(v);
	}

	json_t* ahabJ = json_object_get(j, "ahab");
	if (ahabJ) {
		v = json_object_get(ahabJ, "info");               if (v) s.ahabInfo = json_boolean_value(v);
		v = json_object_get(ahabJ, "midiVirtualEnabled"); if (v) s.ahabMidiVirtualEnabled = json_boolean_value(v);
	}
}

// ── legacy flat format (Stoermelder-P1.json) ─────────────────────────────────

static void parseLegacyJson(json_t* j, Settings& s) {
	if (!j) return;
	json_t* v;
	v = json_object_get(j, "panelThemeDefault");     if (v) s.panelThemeDefault = json_integer_value(v);

	v = json_object_get(j, "mbModels");              if (v) s.mbModelsJ = json_copy(v);
	v = json_object_get(j, "mbV1zoom");              if (v) s.mbZoom = json_real_value(v);
	v = json_object_get(j, "mbV1sort");              if (v) s.mbSort = json_integer_value(v);
	v = json_object_get(j, "mbV1hideBrands");        if (v) s.mbHideBrands = json_boolean_value(v);
	v = json_object_get(j, "mbV1searchDescriptions");if (v) s.mbSearchDescriptions = json_boolean_value(v);
	v = json_object_get(j, "mbSortBySearchScore");   if (v) s.mbSortBySearchScore = json_boolean_value(v);
	v = json_object_get(j, "mbFavoriteHighlight");   if (v) s.mbFavoriteHighlight = json_boolean_value(v);
	v = json_object_get(j, "mbSearchThreshold");     if (v) s.mbSearchThreshold = json_real_value(v);
	v = json_object_get(j, "mbMagnifierEnabled");    if (v) s.mbMagnifierEnabled = json_boolean_value(v);

	v = json_object_get(j, "overlayTextColor");      if (v) s.overlayTextColor = rack::color::fromHexString(json_string_value(v));
	v = json_object_get(j, "overlayHpos");           if (v) s.overlayHpos = json_integer_value(v);
	v = json_object_get(j, "overlayVpos");           if (v) s.overlayVpos = json_integer_value(v);
	v = json_object_get(j, "overlayOpacity");        if (v) s.overlayOpacity = json_real_value(v);
	v = json_object_get(j, "overlayScale");          if (v) s.overlayScale = json_real_value(v);

	v = json_object_get(j, "magnifierKey");          if (v) s.magnifierKey = json_integer_value(v);
	v = json_object_get(j, "magnifierMods");         if (v) s.magnifierMods = json_integer_value(v);
	v = json_object_get(j, "magnifierRadius");       if (v) s.magnifierRadius = json_real_value(v);
	v = json_object_get(j, "magnifierZoom");         if (v) s.magnifierZoom = json_real_value(v);

	v = json_object_get(j, "stripDirVcvss");         if (v) s.stripDirVcvss = json_string_value(v);
	v = json_object_get(j, "stripDirVcvs");          if (v) s.stripDirVcvs = json_string_value(v);

	v = json_object_get(j, "midiEsxDriverEnabled");  if (v) s.midiEsxDriverEnabled = json_boolean_value(v);

	v = json_object_get(j, "ahabInfo");              if (v) s.ahabInfo = json_boolean_value(v);
	v = json_object_get(j, "ahabMidiVirtualEnabled");if (v) s.ahabMidiVirtualEnabled = json_boolean_value(v);
}

// ── public API ────────────────────────────────────────────────────────────────

void Settings::saveToJson() {
	if (isTesting()) return;

	rack::system::createDirectory(settingsDirPath());

	json_t* mbJ = buildMbJson(*this);
	saveJsonFile(mbFilePath(), mbJ);
	json_decref(mbJ);

	json_t* plugJ = buildPluginJson(*this);
	saveJsonFile(pluginFilePath(), plugJ);
	json_decref(plugJ);
}

void Settings::readFromJson() {
	if (isTesting()) return;
	// Migrate from legacy single-file format if it exists.
	std::string legacy = legacyFilePath();
	FILE* legacyFile = fopen(legacy.c_str(), "r");
	if (legacyFile) {
		json_error_t error;
		json_t* j = json_loadf(legacyFile, 0, &error);
		fclose(legacyFile);
		if (j) {
			parseLegacyJson(j, *this);
			json_decref(j);
		}
		saveToJson();
		std::remove(legacy.c_str());
		return;
	}

	json_t* mbJ = loadJsonFile(mbFilePath());
	if (mbJ) {
		parseMbJson(mbJ, *this);
		json_decref(mbJ);
	}

	json_t* plugJ = loadJsonFile(pluginFilePath());
	if (plugJ) {
		parsePluginJson(plugJ, *this);
		json_decref(plugJ);
	}

	// Neither file existed — write defaults.
	if (!mbJ && !plugJ) {
		saveToJson();
	}
}

} // namespace StoermelderPackOne