#pragma once


struct StoermelderSettings {
	int panelThemeDefault = 0;

	void saveToJson();
	void readFromJson();
};