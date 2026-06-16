#pragma once
#include "../../plugin.hpp"
#include "SirenDataSource.hpp"


namespace StoermelderPackOne {
namespace Siren {

struct SirenSettings {
	// Where to write generated copies of audio files when a drop needs them.
	//   CT_SOURCE — next to the source file (the default)
	//   CT_CUSTOM — into customConvertDir (chosen by the user)
	//   CT_PATCH  — into the module's patch storage directory
	enum ConvertTarget { CT_SOURCE = 0, CT_CUSTOM = 1, CT_PATCH = 2 };
	std::vector<RootContainer> rootContainers;
	int activeRootIdx = -1;
	std::string lastFile;
	float lastPlayheadPos = 0.f;
	bool loopPlayback = false;
	bool resampleOnPlayback = true;
	bool resampleOnDrop = true;
	// Speex resampler quality used during "resample on drop" (SPEEX_RESAMPLER_QUALITY_*).
	// Three discrete options surfaced in the context menu; values 1, 6 and 10 map to
	// linear / default / best in the speex preset table.
	int resampleQuality = 6;
	bool convertToWavOnDrop = false;
	// When non-empty and convertTarget == CT_CUSTOM, converted/trimmed files are
	// written here instead of alongside the source file. For CT_SOURCE/older
	// settings (convertTarget == 0 with this empty) the output lands beside the
	// source file.
	std::string customConvertDir;
	ConvertTarget convertTarget = CT_SOURCE;
	// When true, the file is always copied to the target location on drop, even
	// when no conversion/trim/resample/loop/repitch is needed. No-op for
	// CT_SOURCE — copying a file on top of itself is pointless.
	bool alwaysCopy = false;

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
		bool hadExisting = rack::system::exists(jsonPath);
		if (hadExisting) {
			rack::system::rename(jsonPath, bakPath);
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
				rack::system::remove(bakPath);
			}
			else {
				rack::system::remove(jsonPath);
				rack::system::rename(bakPath, jsonPath);
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
	bool removeActiveRoot(DataSource* activeDs) {
		int idx = activeRootIdx;
		if (idx < 0 || idx >= (int)rootContainers.size()) return false;
		if (activeDs) activeDs->cleanup();
		rootContainers.erase(rootContainers.begin() + idx);
		activeRootIdx = rootContainers.empty() ? -1 : 0;
		return true;
	}

	json_t* toJson() const {
		json_t* j = json_object();
		json_t* rootsJ = json_array();
		for (const RootContainer& r : rootContainers) {
			json_t* rj = json_object();
			json_object_set_new(rj, "path", json_string(r.path.c_str()));
			json_object_set_new(rj, "type", json_string(r.type.c_str()));
			json_array_append_new(rootsJ, rj);
		}
		json_object_set_new(j, "rootContainers", rootsJ);
		json_object_set_new(j, "activeRootIdx", json_integer(activeRootIdx));
		json_object_set_new(j, "lastFile", json_string(lastFile.c_str()));
		json_object_set_new(j, "lastPlayheadPos", json_real(lastPlayheadPos));
		json_object_set_new(j, "loopPlayback", json_boolean(loopPlayback));
		json_object_set_new(j, "resampleOnPlayback", json_boolean(resampleOnPlayback));
		json_object_set_new(j, "resampleOnDrop", json_boolean(resampleOnDrop));
		json_object_set_new(j, "resampleQuality", json_integer(resampleQuality));
		json_object_set_new(j, "convertToWavOnDrop", json_boolean(convertToWavOnDrop));
		json_object_set_new(j, "customConvertDir", json_string(customConvertDir.c_str()));
		json_object_set_new(j, "convertTarget", json_integer(convertTarget));
		json_object_set_new(j, "alwaysCopy", json_boolean(alwaysCopy));
		return j;
	}

	void fromJson(json_t* j) {
		rootContainers.clear();
		json_t* rootsJ = json_object_get(j, "rootContainers");
		if (rootsJ && json_is_array(rootsJ)) {
			size_t i; json_t* v;
			json_array_foreach(rootsJ, i, v) {
				// Backward compatibility: old settings files stored rootContainers
				// as a plain array of path strings, implicitly type "fs".
				if (json_is_string(v)) {
					rootContainers.push_back(createRootContainer(json_string_value(v), "fs"));
				}
				else if (json_is_object(v)) {
					json_t* pathJ = json_object_get(v, "path");
					json_t* typeJ = json_object_get(v, "type");
					std::string path = (pathJ && json_is_string(pathJ)) ? json_string_value(pathJ) : "";
					std::string type = (typeJ && json_is_string(typeJ)) ? json_string_value(typeJ) : "fs";
					rootContainers.push_back(createRootContainer(path, type));
				}
			}
		}
		// Identify which root was active before sorting so we can re-find it by
		// identity afterwards. Old files persisted insertion-order indices; new
		// files persist sorted-order indices — either way, capturing the root
		// object and searching for it post-sort handles both cases correctly.
		json_t* v;
		v = json_object_get(j, "activeRootIdx");
		int savedIdx = v ? (int)json_integer_value(v) : -1;
		RootContainer activeRoot;
		bool haveActive = savedIdx >= 0 && savedIdx < (int)rootContainers.size();
		if (haveActive) activeRoot = rootContainers[savedIdx];

		// Keep roots in sorted order as the single canonical representation —
		// no dual insertion-order vs. display-order vectors to keep in sync.
		std::sort(rootContainers.begin(), rootContainers.end(),
			[](const RootContainer& a, const RootContainer& b) {
				return rack::string::lowercase(a.name) < rack::string::lowercase(b.name);
			});

		if (haveActive) {
			activeRootIdx = -1;
			for (int i = 0; i < (int)rootContainers.size(); i++) {
				if (rootContainers[i] == activeRoot) { activeRootIdx = i; break; }
			}
		}

		v = json_object_get(j, "lastFile"); if (v) lastFile = json_string_value(v);
		v = json_object_get(j, "lastPlayheadPos"); if (v) lastPlayheadPos = (float)json_real_value(v);
		v = json_object_get(j, "loopPlayback"); if (v) loopPlayback = json_boolean_value(v);
		v = json_object_get(j, "resampleOnPlayback"); if (v) resampleOnPlayback = json_boolean_value(v);
		v = json_object_get(j, "resampleOnDrop"); if (v) resampleOnDrop = json_boolean_value(v);
		v = json_object_get(j, "resampleQuality"); if (v) resampleQuality = (int)json_integer_value(v);
		v = json_object_get(j, "convertToWavOnDrop"); if (v) convertToWavOnDrop = json_boolean_value(v);
		v = json_object_get(j, "customConvertDir"); if (v) customConvertDir = json_string_value(v);
		v = json_object_get(j, "convertTarget");
		// Backward compatibility: older settings stored the source-vs-custom choice
		// implicitly via customConvertDir being empty/non-empty. The new explicit
		// field takes precedence when present.
		if (v) convertTarget = (ConvertTarget)(int)json_integer_value(v);
		else convertTarget = customConvertDir.empty() ? CT_SOURCE : CT_CUSTOM;
		v = json_object_get(j, "alwaysCopy"); if (v) alwaysCopy = json_boolean_value(v);
	}
};

extern SirenSettings sirenSettings;

} // namespace Siren
} // namespace StoermelderPackOne
