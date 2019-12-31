#include "rack.hpp"
#include "pluginsettings.hpp"


StoermelderSettings pluginSettings;


void StoermelderSettings::saveToJson() {
    json_t* settingsJ = json_object();
    json_object_set_new(settingsJ, "panelThemeDefault", json_integer(panelThemeDefault));

    std::string settingsFilename = rack::asset::user("Stoermelder-P1.json");
    FILE* file = fopen(settingsFilename.c_str(), "w");
    if (file) {
        json_dumpf(settingsJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
        fclose(file);
    }
    json_decref(settingsJ);
}

void StoermelderSettings::readFromJson() {
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

    fclose(file);
    json_decref(settingsJ);
}