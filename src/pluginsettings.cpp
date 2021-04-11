#include "rack.hpp"
#include "pluginsettings.hpp"


StoermelderSettings pluginSettings;


void StoermelderSettings::saveToJson() {
    json_t* settingsJ = json_object();
    json_object_set_new(settingsJ, "panelThemeDefault", json_integer(panelThemeDefault));

    json_object_set(settingsJ, "mbModels", mbModelsJ);
    json_object_set(settingsJ, "mbV1zoom", json_real(mbV1zoom));
    json_object_set(settingsJ, "mbV1sort", json_integer(mbV1sort));
    json_object_set(settingsJ, "mbV1hideBrands", json_boolean(mbV1hideBrands));

    json_object_set(settingsJ, "midiLoopbackDriverEnabled", json_boolean(midiLoopbackDriverEnabled));

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

    json_t* fmJ = json_object_get(settingsJ, "mbModels");
    if (fmJ) mbModelsJ = json_copy(fmJ);
    json_t* mbV1zoomJ = json_object_get(settingsJ, "mbV1zoom");
    if (mbV1zoomJ) mbV1zoom = json_real_value(mbV1zoomJ);
    json_t* mbV1sortJ = json_object_get(settingsJ, "mbV1sort");
    if (mbV1sortJ) mbV1sort = json_integer_value(mbV1sortJ);
    json_t* mbV1hideBrandsJ = json_object_get(settingsJ, "mbV1hideBrands");
    if (mbV1hideBrandsJ) mbV1hideBrands = json_boolean_value(mbV1hideBrandsJ);

    json_t* midiLoopbackDriverEnabledJ = json_object_get(settingsJ, "midiLoopbackDriverEnabled");
    if (midiLoopbackDriverEnabledJ) midiLoopbackDriverEnabled = json_boolean_value(midiLoopbackDriverEnabledJ);

    fclose(file);
    json_decref(settingsJ);
}