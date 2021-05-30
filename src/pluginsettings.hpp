#pragma once


struct StoermelderSettings {
	int panelThemeDefault = 0;

	json_t* mbModelsJ;
	float mbV1zoom = 0.85f;
	int mbV1sort = 0;
	bool mbV1hideBrands = false;

	bool midiLoopbackDriverEnabled = false;

	NVGcolor overlayTextColor = bndGetTheme()->menuTheme.textColor;
	int overlayHpos = 0;
	int overlayVpos = 0;

	void saveToJson();
	void readFromJson();
};