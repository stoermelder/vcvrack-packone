#pragma once
#include "../../plugin.hpp"
#include "SirenDataSource.hpp"


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
	bool loopPlayback       = false;
	bool resampleOnPlayback = true;
	bool resampleOnDrop     = true;
	// Speex resampler quality used during "resample on drop" (SPEEX_RESAMPLER_QUALITY_*).
	// Three discrete options surfaced in the context menu; values 1, 6 and 10 map to
	// linear / default / best in the speex preset table.
	int  resampleQuality    = 6;
	bool convertToWavOnDrop = false;
	// When non-empty, converted/trimmed files are written here instead of
	// alongside the source file.
	std::string customConvertDir;
	// Apply rotation+crossfade loop processing on drop, producing a loop-ready WAV.
	bool loopOnDrop = false;
	// Length of the crossfade applied at the rotation join point (seconds).
	float loopCrossfadeDuration = 6.f;

	// Saves via rename-write-verify-delete so a crash mid-write can never destroy
	// the previous settings: the existing file is first moved aside to ".bak",
	// the new file is written and parsed back to confirm it's valid JSON, and
	// only then is the backup removed. On verification failure the backup is restored.
	void save() const {
		if (isTesting()) return;
		std::string jsonPath = sirenFilePath();
		rack::system::createDirectories(settingsDirPath());

		json_t* j = toJson();
		DEFER({ json_decref(j); });

		std::string bakPath = jsonPath + ".bak";
		std::error_code ec;
		bool hadExisting = ghc::filesystem::exists(jsonPath, ec);
		if (hadExisting) {
			ghc::filesystem::rename(jsonPath, bakPath, ec);
		}

		FILE* f = fopen(jsonPath.c_str(), "w");
		if (f) {
			json_dumpf(j, f, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
			fclose(f);
		}

		bool verified = false;
		FILE* check = fopen(jsonPath.c_str(), "r");
		if (check) {
			json_error_t err;
			json_t* verifyJ = json_loadf(check, 0, &err);
			fclose(check);
			if (verifyJ) {
				verified = true;
				json_decref(verifyJ);
			}
		}

		if (hadExisting) {
			if (verified) {
				ghc::filesystem::remove(bakPath, ec);
			}
			else {
				ghc::filesystem::remove(jsonPath, ec);
				ghc::filesystem::rename(bakPath, jsonPath, ec);
			}
		}
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

	// Remove the active root from the list of configured roots. The active
	// data source's cache is cleaned up before the entry is erased, so cache
	// files for the removed root are deleted while its on-disk metadata file
	// (tags/favorites/BPM) is preserved. Returns true on success, false if
	// the index is out of range.
	bool removeActiveRoot(DataSource* activeDataSource) {
		int idx = activeRootIdx;
		if (idx < 0 || idx >= (int)rootContainers.size()) return false;
		if (activeDataSource) activeDataSource->cleanup();
		rootContainers.erase(rootContainers.begin() + idx);
		activeRootIdx = rootContainers.empty() ? -1 : 0;
		return true;
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
		json_object_set_new(j, "loopPlayback",        json_boolean(loopPlayback));
		json_object_set_new(j, "resampleOnPlayback", json_boolean(resampleOnPlayback));
		json_object_set_new(j, "resampleOnDrop",     json_boolean(resampleOnDrop));
		json_object_set_new(j, "resampleQuality",    json_integer(resampleQuality));
		json_object_set_new(j, "convertToWavOnDrop", json_boolean(convertToWavOnDrop));
		json_object_set_new(j, "customConvertDir", json_string(customConvertDir.c_str()));
		json_object_set_new(j, "loopOnDrop",             json_boolean(loopOnDrop));
		json_object_set_new(j, "loopCrossfadeDuration",  json_real(loopCrossfadeDuration));
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
		v = json_object_get(j, "loopPlayback");         if (v) loopPlayback = json_boolean_value(v);
		v = json_object_get(j, "resampleOnPlayback");  if (v) resampleOnPlayback = json_boolean_value(v);
		v = json_object_get(j, "resampleOnDrop");      if (v) resampleOnDrop = json_boolean_value(v);
		v = json_object_get(j, "resampleQuality");     if (v) resampleQuality = (int)json_integer_value(v);
		v = json_object_get(j, "convertToWavOnDrop");  if (v) convertToWavOnDrop = json_boolean_value(v);
		v = json_object_get(j, "customConvertDir");    if (v) customConvertDir = json_string_value(v);
		v = json_object_get(j, "loopOnDrop");             if (v) loopOnDrop = json_boolean_value(v);
		v = json_object_get(j, "loopCrossfadeDuration");  if (v) loopCrossfadeDuration = (float)json_real_value(v);
	}
} sirenSettings;

} // namespace Siren
} // namespace StoermelderPackOne
