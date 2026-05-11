#include <rack.hpp>
#include "pluginsettings.hpp"

namespace StoermelderPackOne {

Settings pluginSettings;

void Settings::saveToJson() {
	json_t* settingsJ = json_object();
	json_object_set_new(settingsJ, "panelThemeDefault", json_integer(panelThemeDefault));

	json_object_set(settingsJ, "mbModels", mbModelsJ);
	json_object_set(settingsJ, "mbV1zoom", json_real(mbV1zoom));
	json_object_set(settingsJ, "mbV1sort", json_integer(mbV1sort));
	json_object_set(settingsJ, "mbV1hideBrands", json_boolean(mbV1hideBrands));
	json_object_set(settingsJ, "mbV1searchDescriptions", json_boolean(mbV1searchDescriptions));
	json_object_set(settingsJ, "mbSortBySearchScore", json_boolean(mbSortBySearchScore));
	json_object_set(settingsJ, "mbFavoriteHighlight", json_boolean(mbFavoriteHighlight));
	json_object_set(settingsJ, "mbSelectionSources", pluginSettings.mbSelectionSourcesJ);
	json_object_set(settingsJ, "mbSearchThreshold", json_real(pluginSettings.mbSearchThreshold));
	json_object_set(settingsJ, "mbMagnifierEnabled", json_boolean(mbMagnifierEnabled));

	json_object_set(settingsJ, "overlayTextColor", json_string(rack::color::toHexString(overlayTextColor).c_str()));
	json_object_set(settingsJ, "overlayHpos", json_integer(overlayHpos));
	json_object_set(settingsJ, "overlayVpos", json_integer(overlayVpos));
	json_object_set(settingsJ, "overlayOpacity", json_real(overlayOpacity));
	json_object_set(settingsJ, "overlayScale", json_real(overlayScale));

	json_object_set_new(settingsJ, "magnifierKey", json_integer(magnifierKey));
	json_object_set_new(settingsJ, "magnifierMods", json_integer(magnifierMods));
	json_object_set_new(settingsJ, "magnifierRadius", json_real(magnifierRadius));
	json_object_set_new(settingsJ, "magnifierZoom", json_real(magnifierZoom));

	json_object_set(settingsJ, "stripDirVcvss", json_string(stripDirVcvss.c_str()));
	json_object_set(settingsJ, "stripDirVcvs", json_string(stripDirVcvs.c_str()));

	json_object_set(settingsJ, "midiEsxDriverEnabled", json_boolean(midiEsxDriverEnabled));

	json_object_set(settingsJ, "ahabInfo", json_boolean(ahabInfo));
	json_object_set(settingsJ, "ahabMidiVirtualEnabled", json_boolean(ahabMidiVirtualEnabled));

#ifndef TESTING
	std::string settingsFilename = rack::asset::user("Stoermelder-P1.json");
	FILE* file = fopen(settingsFilename.c_str(), "w");
	if (file) {
		json_dumpf(settingsJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}
#endif
	json_decref(settingsJ);
}

void Settings::readFromJson() {
	std::string settingsFilename = rack::asset::user("Stoermelder-P1.json");
	FILE* file = fopen(settingsFilename.c_str(), "r");
	if (!file) {
		saveToJson();
		return;
	}

	json_error_t error;
	json_t* settingsJ = json_loadf(file, 0, &error);
	if (!settingsJ) {
		// invalid setting json file
		fclose(file);
		saveToJson();
		return;
	}

	json_t* panelThemeDefaultJ = json_object_get(settingsJ, "panelThemeDefault");
	if (panelThemeDefaultJ) panelThemeDefault = json_integer_value(panelThemeDefaultJ);

	json_t* fmJ = json_object_get(settingsJ, "mbModels");
	if (fmJ) mbModelsJ = json_copy(fmJ);
	json_t* mbV1zoomJ = json_object_get(settingsJ, "mbV1zoom");
	if (mbV1zoomJ) mbV1zoom = json_real_value(mbV1zoomJ);
	json_t* mbV1sortJ = json_object_get(settingsJ, "mbV1sort");
	if (mbV1sortJ) mbV1sort = json_integer_value(mbV1sortJ);
	json_t* mbV1hideBrandsJ = json_object_get(settingsJ, "mbV1hideBrands");
	if (mbV1hideBrandsJ) mbV1hideBrands = json_boolean_value(mbV1hideBrandsJ);
	json_t* mbV1searchDescriptionsJ = json_object_get(settingsJ, "mbV1searchDescriptions");
	if (mbV1searchDescriptionsJ) mbV1searchDescriptions = json_boolean_value(mbV1searchDescriptionsJ);
	json_t* mbSortBySearchScoreJ = json_object_get(settingsJ, "mbSortBySearchScore");
	if (mbSortBySearchScoreJ) mbSortBySearchScore = json_boolean_value(mbSortBySearchScoreJ);
	json_t* mbFavoriteHighlightJ = json_object_get(settingsJ, "mbFavoriteHighlight");
	if (mbFavoriteHighlightJ) mbFavoriteHighlight = json_boolean_value(mbFavoriteHighlightJ);
	json_t* mbSelectionSourcesJ = json_object_get(settingsJ, "mbSelectionSources");
	if (mbSelectionSourcesJ) pluginSettings.mbSelectionSourcesJ = json_copy(mbSelectionSourcesJ);
	json_t* mbSearchThresholdJ = json_object_get(settingsJ, "mbSearchThreshold");
	if (mbSearchThresholdJ) mbSearchThreshold = json_real_value(mbSearchThresholdJ);
	json_t* mbMagnifierEnabledJ = json_object_get(settingsJ, "mbMagnifierEnabled");
	if (mbMagnifierEnabledJ) mbMagnifierEnabled = json_boolean_value(mbMagnifierEnabledJ);

	json_t* overlayTextColorJ = json_object_get(settingsJ, "overlayTextColor");
	if (overlayTextColorJ) overlayTextColor = rack::color::fromHexString(json_string_value(overlayTextColorJ));
	json_t* overlayHposJ = json_object_get(settingsJ, "overlayHpos");
	if (overlayHposJ) overlayHpos = json_integer_value(overlayHposJ);
	json_t* overlayVposJ = json_object_get(settingsJ, "overlayVpos");
	if (overlayVposJ) overlayVpos = json_integer_value(overlayVposJ);
	json_t* overlayOpacityJ = json_object_get(settingsJ, "overlayOpacity");
	if (overlayOpacityJ) overlayOpacity = json_real_value(overlayOpacityJ);
	json_t* overlayScaleJ = json_object_get(settingsJ, "overlayScale");
	if (overlayScaleJ) overlayScale = json_real_value(overlayScaleJ);

	json_t* magnifierKeyJ = json_object_get(settingsJ, "magnifierKey");
	if (magnifierKeyJ) magnifierKey = json_integer_value(magnifierKeyJ);
	json_t* magnifierModsJ = json_object_get(settingsJ, "magnifierMods");
	if (magnifierModsJ) magnifierMods = json_integer_value(magnifierModsJ);
	json_t* magnifierRadiusJ = json_object_get(settingsJ, "magnifierRadius");
	if (magnifierRadiusJ) magnifierRadius = json_real_value(magnifierRadiusJ);
	json_t* magnifierZoomJ = json_object_get(settingsJ, "magnifierZoom");
	if (magnifierZoomJ) magnifierZoom = json_real_value(magnifierZoomJ);

	json_t* stripDirVcvssJ = json_object_get(settingsJ, "stripDirVcvss");
	if (stripDirVcvssJ) stripDirVcvss = json_string_value(stripDirVcvssJ);
	json_t* stripDirVcvsJ = json_object_get(settingsJ, "stripDirVcvs");
	if (stripDirVcvsJ) stripDirVcvs = json_string_value(stripDirVcvsJ);

	json_t* midiEsxDriverEnabledJ = json_object_get(settingsJ, "midiEsxDriverEnabled");
	if (midiEsxDriverEnabledJ) midiEsxDriverEnabled = json_boolean_value(midiEsxDriverEnabledJ);

	json_t* ahabInfoJ = json_object_get(settingsJ, "ahabInfo");
	if (ahabInfoJ) ahabInfo = json_boolean_value(ahabInfoJ);
	json_t* ahabMidiVirtualEnabledJ = json_object_get(settingsJ, "ahabMidiVirtualEnabled");
	if (ahabMidiVirtualEnabledJ) ahabMidiVirtualEnabled = json_boolean_value(ahabMidiVirtualEnabledJ);

	fclose(file);
	json_decref(settingsJ);
}

} // namespace StoermelderPackOne