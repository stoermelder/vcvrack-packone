#pragma once

namespace StoermelderPackOne {

struct Settings {
	int panelThemeDefault = -1;

	json_t* mbModelsJ;
	float mbV1zoom = 0.85f;
	int mbV1sort = 0;
	bool mbV1hideBrands = false;
	bool mbV1searchDescriptions = false;
	bool mbSortBySearchScore = true;
	bool mbFavoriteHighlight = true;
	float mbSearchThreshold = 0.5f;
	bool mbMagnifierEnabled = false;

	NVGcolor overlayTextColor = bndGetTheme()->menuTheme.textColor;
	int overlayHpos = 0;
	int overlayVpos = 0;
	float overlayOpacity = 1.f;
	float overlayScale = 1.f;

	int magnifierKey = -1;
	int magnifierMods = 0;
	float magnifierRadius = 120.f;
	float magnifierZoom = 3.f;

	std::string stripDirVcvss = rack::asset::user("patches");
	std::string stripDirVcvs = rack::asset::user("selections");

	bool ahabInfo = true;
	bool ahabMidiVirtualEnabled = false;

	void saveToJson();
	void readFromJson();
};

extern Settings pluginSettings;

} // namespace StoermelderPackOne