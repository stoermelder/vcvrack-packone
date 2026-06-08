#pragma once
#include "../../plugin.hpp"
#include <rack.hpp>
#include <ghc/filesystem.hpp>
#include <map>
#include <vector>


namespace StoermelderPackOne {
namespace Siren {

// ─── Starter tag list ────────────────────────────────────────────────────────
//
// The list of "starter" tags is the single source of truth in
// `res/data/SirenTags.json`. We read it once per module load
// and expose it as a `std::vector<std::string>` via `starterTags()`.
//
// Order in the returned vector matches the order in the JSON. The Python
// training pipeline reads the same JSON file in the same order, so the
// index of a tag in this list == the index of that class in the trained
// model's output vector.
//
// Why a function and not a constant: we want the list to be editable
// without recompiling the plugin, and the file is small (a few KB) so the
// per-load cost is negligible. `isTesting()` short-circuits the disk read
// in the test harness.

inline std::string tagManifestPath() {
	return rack::asset::plugin(pluginInstance, "res/data/SirenTags.json");
}

// Hard-coded 15-tag fallback used in tests and when the manifest cannot be
// read. Keep this list in sync with res/data/SirenTags.json.
static const std::vector<std::string>& fallbackTags() {
	static const std::vector<std::string> v = {
		"Bass", "Clap", "Cymbal", "Drone", "Drums",
		"HiHat", "Kick", "Lead", "Loop", "Noise",
		"One-Shot", "Pad", "Snare", "Stab", "Vocal",
	};
	return v;
}

inline const std::vector<std::string>& starterTags() {
	static std::vector<std::string> cache;
	static bool loaded = false;
	if (loaded) return cache;
	loaded = true;  // set first so a JSON parse failure doesn't retry every call

	if (isTesting()) {
		// In tests we can't reach `rack::asset::plugin` without a full
		// plugin instance. Use the fallback.
		cache = fallbackTags();
		return cache;
	}

	std::string path = tagManifestPath();
	FILE* f = std::fopen(path.c_str(), "r");
	if (!f) {
		WARN("Siren: failed to read SirenTags.json at %s; using built-in fallback.", path.c_str());
		cache = fallbackTags();
		return cache;
	}
	json_error_t err;
	json_t* rootJ = json_loadf(f, 0, &err);
	std::fclose(f);
	if (!rootJ) {
		WARN("Siren: failed to parse SirenTags.json: %s; using built-in fallback.", err.text);
		cache = fallbackTags();
		return cache;
	}
	DEFER({ json_decref(rootJ); });

	json_t* tagsJ = json_object_get(rootJ, "tags");
	if (!tagsJ || !json_is_array(tagsJ)) {
		WARN("Siren: SirenTags.json missing 'tags' array; using built-in fallback.");
		cache = fallbackTags();
		return cache;
	}
	size_t i;
	json_t* entryJ;
	json_array_foreach(tagsJ, i, entryJ) {
		json_t* nameJ = json_object_get(entryJ, "name");
		if (nameJ && json_is_string(nameJ)) {
			cache.emplace_back(json_string_value(nameJ));
		}
	}
	if (cache.empty()) {
		cache = fallbackTags();
	}
	return cache;
}

// Returns a map from tag name to its filename keywords, loaded from the same
// SirenTags.json. Empty map on parse failure (filename boosting is just skipped).
inline const std::map<std::string, std::vector<std::string>>& starterTagKeywords() {
	static std::map<std::string, std::vector<std::string>> cache;
	static bool loaded = false;
	if (loaded) return cache;
	loaded = true;

	if (isTesting()) return cache;

	std::string path = tagManifestPath();
	FILE* f = std::fopen(path.c_str(), "r");
	if (!f) return cache;
	json_error_t err;
	json_t* rootJ = json_loadf(f, 0, &err);
	std::fclose(f);
	if (!rootJ) return cache;
	DEFER({ json_decref(rootJ); });

	json_t* tagsJ = json_object_get(rootJ, "tags");
	if (!tagsJ || !json_is_array(tagsJ)) return cache;
	size_t i;
	json_t* entryJ;
	json_array_foreach(tagsJ, i, entryJ) {
		json_t* nameJ = json_object_get(entryJ, "name");
		json_t* kwJ   = json_object_get(entryJ, "keywords");
		if (!nameJ || !json_is_string(nameJ)) continue;
		if (!kwJ || !json_is_array(kwJ)) continue;
		std::string name = json_string_value(nameJ);
		std::vector<std::string> kws;
		size_t j;
		json_t* kwEntry;
		json_array_foreach(kwJ, j, kwEntry) {
			if (json_is_string(kwEntry))
				kws.emplace_back(json_string_value(kwEntry));
		}
		if (!kws.empty())
			cache[name] = std::move(kws);
	}
	return cache;
}

struct SampleMetadata {
	std::string relativePath;  // relative to root, using '/' separator
	bool favorite = false;
	std::vector<std::string> tags;
	float bpm = 0.f;           // detected BPM, 0 if not detected
	float bpmConfidence = 0.f; // confidence of BPM detection
};

// Compute 8-char hex hash of a string (for JSON filename derivation)
inline std::string hashPath(const std::string& path) {
	uint32_t h = 2166136261u;
	for (unsigned char c : path) {
		h ^= c;
		h *= 16777619u;
	}
	char buf[9];
	snprintf(buf, sizeof(buf), "%08x", h);
	return std::string(buf);
}

struct MetadataStore {
	std::string rootPath;
	std::map<std::string, SampleMetadata> samples;  // key = relativePath

	// Single source of truth for where this root's metadata lives on disk.
	// Callers (data sources) must not derive or guess this path themselves.
	std::string filePath() const {
		return rack::asset::user("Stoermelder-P1") + "/siren-" + hashPath(rootPath) + ".json";
	}

	void load() {
		if (isTesting()) return;
		FILE* file = fopen(filePath().c_str(), "r");
		if (!file) return;
		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		fclose(file);
		if (!rootJ) return;
		DEFER({ json_decref(rootJ); });
		fromJson(rootJ);
	}

	// Saves via rename-write-verify-delete so a crash mid-write can never destroy
	// the previous metadata: the existing file is first moved aside to ".bak",
	// the new file is written and parsed back to confirm it's valid JSON, and
	// only then is the backup removed. On verification failure the backup is restored.
	void save() const {
		if (isTesting()) return;
		rack::system::createDirectories(rack::asset::user("Stoermelder-P1"));

		std::string jsonPath = filePath();
		json_t* rootJ = toJson();
		DEFER({ json_decref(rootJ); });

		std::string bakPath = jsonPath + ".bak";
		std::error_code ec;
		bool hadExisting = ghc::filesystem::exists(jsonPath, ec);
		if (hadExisting) {
			ghc::filesystem::rename(jsonPath, bakPath, ec);
		}

		FILE* file = fopen(jsonPath.c_str(), "w");
		if (file) {
			json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
			fclose(file);
		}

		bool verified = false;
		FILE* check = fopen(jsonPath.c_str(), "r");
		if (check) {
			json_error_t error;
			json_t* verifyJ = json_loadf(check, 0, &error);
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

	json_t* toJson() const {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "rootPath", json_string(rootPath.c_str()));

		json_t* filesJ = json_array();
		for (const auto& pair : samples) {
			const SampleMetadata& meta = pair.second;
			json_t* fileJ = json_object();
			json_object_set_new(fileJ, "path", json_string(meta.relativePath.c_str()));
			if (meta.favorite)
				json_object_set_new(fileJ, "fav", json_true());
			if (!meta.tags.empty()) {
				json_t* tagsJ = json_array();
				for (const std::string& tag : meta.tags)
					json_array_append_new(tagsJ, json_string(tag.c_str()));
				json_object_set_new(fileJ, "tags", tagsJ);
			}
			if (meta.bpm > 0.f) {
				json_object_set_new(fileJ, "bpm", json_real(meta.bpm));
				json_object_set_new(fileJ, "bpmConfidence", json_real(meta.bpmConfidence));
			}
			json_array_append_new(filesJ, fileJ);
		}
		json_object_set_new(rootJ, "files", filesJ);
		return rootJ;
	}

	void fromJson(json_t* rootJ) {
		samples.clear();
		json_t* rootPathJ = json_object_get(rootJ, "rootPath");
		if (rootPathJ) rootPath = json_string_value(rootPathJ);

		json_t* filesJ = json_object_get(rootJ, "files");
		if (filesJ && json_is_array(filesJ)) {
			size_t i; json_t* fileJ;
			json_array_foreach(filesJ, i, fileJ) {
				if (!json_is_object(fileJ)) continue;
				json_t* pathJ = json_object_get(fileJ, "path");
				if (!pathJ || !json_is_string(pathJ)) continue;
				std::string rel = json_string_value(pathJ);
				SampleMetadata& meta = samples[rel];
				meta.relativePath = rel;

				json_t* favJ = json_object_get(fileJ, "fav");
				if (favJ && json_is_true(favJ)) meta.favorite = true;

				json_t* tagsJ = json_object_get(fileJ, "tags");
				if (tagsJ && json_is_array(tagsJ)) {
					size_t j; json_t* tagJ;
					json_array_foreach(tagsJ, j, tagJ) {
						if (json_is_string(tagJ)) {
							std::string t = rack::string::trim(json_string_value(tagJ));
							if (!t.empty()) meta.tags.push_back(t);
						}
					}
				}

				json_t* bpmJ = json_object_get(fileJ, "bpm");
				json_t* confJ = json_object_get(fileJ, "bpmConfidence");
				if (bpmJ && json_is_number(bpmJ))
					meta.bpm = (float)json_number_value(bpmJ);
				if (confJ && json_is_number(confJ))
					meta.bpmConfidence = (float)json_number_value(confJ);
			}
		}
	}

	void setFavorite(const std::string& rel, bool fav) {
		samples[rel].relativePath = rel;
		samples[rel].favorite = fav;
	}

	// Ensure a metadata entry exists for `rel`. Called when a sample is opened
	// in the browser so cleanup() can later find and remove the matching
	// waveform cache file, even when the user never tags/favorites/BPM-detects
	// the sample. Presence in `samples` itself is the "seen" marker — the map
	// only ever grows, so once a sample is in here it stays in here.
	void markSeen(const std::string& rel) {
		samples[rel].relativePath = rel;
	}

	bool isFavorite(const std::string& rel) const {
		auto it = samples.find(rel);
		return it != samples.end() && it->second.favorite;
	}

	void addTag(const std::string& rel, const std::string& tag) {
		std::string trimmed = rack::string::trim(tag);
		if (trimmed.empty()) return;
		auto& meta = samples[rel];
		meta.relativePath = rel;
		std::string tagLow = rack::string::lowercase(trimmed);
		for (const std::string& t : meta.tags)
			if (rack::string::lowercase(rack::string::trim(t)) == tagLow) return;
		meta.tags.push_back(trimmed);
	}

	void removeTag(const std::string& rel, const std::string& tag) {
		auto it = samples.find(rel);
		if (it == samples.end()) return;
		auto& tags = it->second.tags;
		std::string tagLow = rack::string::lowercase(rack::string::trim(tag));
		tags.erase(std::remove_if(tags.begin(), tags.end(), [&](const std::string& t) {
			return rack::string::lowercase(rack::string::trim(t)) == tagLow;
		}), tags.end());
	}

	std::vector<std::string> getTags(const std::string& rel) const {
		auto it = samples.find(rel);
		if (it == samples.end()) return {};
		return it->second.tags;
	}

	// All tags: starter tags always shown, plus any user-assigned tags.
	// Deduplicates case-insensitively: first occurrence (starter order, then file order) wins.
	std::set<std::string> allTags() const {
		std::set<std::string> seen;   // lowercase keys for dedup
		std::set<std::string> result;
		auto insert = [&](const std::string& t) {
			std::string low = rack::string::lowercase(t);
			if (seen.insert(low).second) result.insert(t);
		};
		for (const std::string& t : starterTags()) insert(t);
		for (const auto& pair : samples)
			for (const std::string& tag : pair.second.tags) insert(tag);
		return result;
	}

	// Get BPM for a relative path (returns 0 if not set)
	float getBpm(const std::string& rel) const {
		auto it = samples.find(rel);
		if (it != samples.end()) return it->second.bpm;
		return 0.f;
	}

	float getBpmConfidence(const std::string& rel) const {
		auto it = samples.find(rel);
		if (it != samples.end()) return it->second.bpmConfidence;
		return 0.f;
	}

	// Set BPM for a relative path
	void setBpm(const std::string& rel, float bpmValue, float confidence = 0.f) {
		auto& meta = samples[rel];
		meta.relativePath = rel;
		meta.bpm = bpmValue;
		meta.bpmConfidence = confidence;
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
