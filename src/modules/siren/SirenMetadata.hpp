#pragma once
#include "../../plugin.hpp"
#include <rack.hpp>


namespace StoermelderPackOne {
namespace Siren {

// ─── Starter tag list ────────────────────────────────────────────────────────
//
// The list of "starter" tags is the single source of truth in
// `data/SirenTags.json`. We read it once per module load
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
	return rack::asset::plugin(pluginInstance, "data/SirenTags.json");
}

// Hard-coded 15-tag fallback used in tests and when the manifest cannot be
// read. Keep this list in sync with data/SirenTags.json.
static const std::vector<std::string>& fallbackTags() {
	static const std::vector<std::string> v = {
		"bass", "bright", "dark", "drone", "field", "lead", "loop",
		"noise", "one-shot", "pad", "percussion", "stab", "texture",
		"tonal", "vocal",
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

struct SampleMetadata {
	std::string relativePath;  // relative to root, using '/' separator
	bool favorite = false;
	std::vector<std::string> tags;
	float bpm = 0.f;           // detected BPM, 0 if not detected
	float bpmConfidence = 0.f; // confidence of BPM detection
};

struct RootMetadata {
	std::string rootPath;
	std::map<std::string, SampleMetadata> samples;  // key = relativePath

	void load(const std::string& jsonPath) {
		if (isTesting()) return;
		FILE* file = fopen(jsonPath.c_str(), "r");
		if (!file) return;
		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		fclose(file);
		if (!rootJ) return;
		DEFER({ json_decref(rootJ); });
		fromJson(rootJ);
	}

	void save(const std::string& jsonPath) const {
		if (isTesting()) return;
		json_t* rootJ = toJson();
		FILE* file = fopen(jsonPath.c_str(), "w");
		if (!file) { json_decref(rootJ); return; }
		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
		json_decref(rootJ);
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
						if (json_is_string(tagJ))
							meta.tags.push_back(json_string_value(tagJ));
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
		if (!fav && samples[rel].tags.empty()) samples.erase(rel);
	}

	bool isFavorite(const std::string& rel) const {
		auto it = samples.find(rel);
		return it != samples.end() && it->second.favorite;
	}

	void addTag(const std::string& rel, const std::string& tag) {
		auto& meta = samples[rel];
		meta.relativePath = rel;
		// Case-insensitive duplicate check; preserve exact spelling of first occurrence
		std::string tagLow = rack::string::lowercase(tag);
		for (const std::string& t : meta.tags)
			if (rack::string::lowercase(t) == tagLow) return;
		meta.tags.push_back(tag);
	}

	void removeTag(const std::string& rel, const std::string& tag) {
		auto it = samples.find(rel);
		if (it == samples.end()) return;
		auto& tags = it->second.tags;
		tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
		if (!it->second.favorite && tags.empty()) samples.erase(it);
	}

	std::vector<std::string> getTags(const std::string& rel) const {
		auto it = samples.find(rel);
		if (it == samples.end()) return {};
		return it->second.tags;
	}

	// All tags: starter tags always shown, plus any user-assigned tags
	std::set<std::string> allTags() const {
		std::set<std::string> result;
		for (const std::string& t : starterTags()) result.insert(t);
		for (const auto& pair : samples) {
			for (const std::string& tag : pair.second.tags)
				result.insert(tag);
		}
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

// Title-case a tag string: spaces, underscores, and hyphens become word breaks.
inline std::string toTitleCase(const std::string& s) {
	std::string r = s;
	bool cap = true;
	for (char& c : r) {
		if (c == ' ' || c == '_' || c == '-') { cap = true; c = ' '; }
		else if (cap) { c = (char)::toupper(c); cap = false; }
		else c = (char)::tolower(c);
	}
	return r;
}

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

} // namespace Siren
} // namespace StoermelderPackOne
