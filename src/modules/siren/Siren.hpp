#pragma once
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Siren {

// ─── helper: settings file paths ─────────────────────────────────────────────

inline std::string settingsDirPath() {
	return rack::asset::user("Stoermelder-P1");
}

inline std::string sirenFilePath() {
	return settingsDirPath() + "/siren.json";
}

inline std::string sirenCacheDirPath() {
	return settingsDirPath() + "/siren-cache";
}


// ─── global siren settings ───────────────────────────────────────────────────

struct SirenSettings {
	std::vector<std::string> rootContainers;
	int activeRootIdx = -1;
	std::string lastFile;
	float lastPlayheadPos = 0.f;
	bool resampleOnPlayback = true;
	bool resampleOnDrop     = true;
	bool convertToWavOnDrop = false;

	void save() const {
		if (isTesting()) return;
		json_t* j = toJson();
		rack::system::createDirectories(settingsDirPath());
		FILE* f = fopen(sirenFilePath().c_str(), "w");
		if (f) { json_dumpf(j, f, JSON_INDENT(2) | JSON_REAL_PRECISION(9)); fclose(f); }
		json_decref(j);
	}

	void load() {
		if (isTesting()) return;
		FILE* f = fopen(sirenFilePath().c_str(), "r");
		if (!f) return;
		json_error_t err;
		json_t* j = json_loadf(f, 0, &err);
		fclose(f);
		if (!j) return;
		fromJson(j);
		json_decref(j);
	}

	json_t* toJson() const {
		json_t* j = json_object();
		json_t* rootsJ = json_array();
		for (const std::string& r : rootContainers)
			json_array_append_new(rootsJ, json_string(r.c_str()));
		json_object_set_new(j, "rootContainers", rootsJ);
		json_object_set_new(j, "activeRootIdx", json_integer(activeRootIdx));
		json_object_set_new(j, "lastFile", json_string(lastFile.c_str()));
		json_object_set_new(j, "lastPlayheadPos", json_real(lastPlayheadPos));
		json_object_set_new(j, "resampleOnPlayback", json_boolean(resampleOnPlayback));
		json_object_set_new(j, "resampleOnDrop",     json_boolean(resampleOnDrop));
		json_object_set_new(j, "convertToWavOnDrop", json_boolean(convertToWavOnDrop));
		return j;
	}

	void fromJson(json_t* j) {
		rootContainers.clear();
		json_t* rootsJ = json_object_get(j, "rootContainers");
		if (rootsJ && json_is_array(rootsJ)) {
			size_t i; json_t* v;
			json_array_foreach(rootsJ, i, v) {
				if (json_is_string(v)) rootContainers.push_back(json_string_value(v));
			}
		}
		json_t* v;
		v = json_object_get(j, "activeRootIdx");      if (v) activeRootIdx = (int)json_integer_value(v);
		v = json_object_get(j, "lastFile");            if (v) lastFile = json_string_value(v);
		v = json_object_get(j, "lastPlayheadPos");     if (v) lastPlayheadPos = (float)json_real_value(v);
		v = json_object_get(j, "resampleOnPlayback");  if (v) resampleOnPlayback = json_boolean_value(v);
		v = json_object_get(j, "resampleOnDrop");      if (v) resampleOnDrop = json_boolean_value(v);
		v = json_object_get(j, "convertToWavOnDrop");  if (v) convertToWavOnDrop = json_boolean_value(v);
	}
} sirenSettings;

} // namespace Siren
} // namespace StoermelderPackOne
