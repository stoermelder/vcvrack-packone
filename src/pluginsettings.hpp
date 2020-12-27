#pragma once


struct StoermelderSettings {
	int panelThemeDefault = 0;

	json_t* mbModelsJ;
	float mbV1zoom = 0.85f;
	int mbV1sort = 0;
	bool mbV1hideBrands = false;

	void saveToJson();
	void readFromJson();
};