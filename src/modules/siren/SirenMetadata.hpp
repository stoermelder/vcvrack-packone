#pragma once
#include "../../plugin.hpp"
#include "../../vcv/api.hpp"
#include <rack.hpp>
#include <map>
#include <utility>
#include <vector>
#include "SirenPaths.hpp"


namespace StoermelderPackOne {
namespace Siren {

// Starter tag list
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
// per-load cost is negligible.
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

// Default prior assigned to bare-string keywords (those given as a plain
// string in SirenTags.json rather than a {"word": prior} object). Matches the
// historical filename-boost strength. Weak/ambiguous keywords (e.g. "loop",
// "fx") should override this with a lower explicit prior.
static constexpr float DEFAULT_KEYWORD_PRIOR = 0.9f;

struct TagManifest {
	std::vector<std::string> tags;
	// tag name → list of (filename keyword, prior strength in (0, 1])
	std::map<std::string, std::vector<std::pair<std::string, float>>> keywords;
};

// Loads SirenTags.json exactly once and populates both caches in a single pass.
inline const TagManifest& tagManifest() {
	static TagManifest manifest;
	static bool loaded = false;
	if (loaded) return manifest;
	loaded = true;

	std::string path = tagManifestPath();
	std::string data;
	if (!vcv::fs::read(path, data)) {
		WARN("Siren: failed to read SirenTags.json at %s; using built-in fallback.", path.c_str());
		manifest.tags = fallbackTags();
		return manifest;
	}
	json_error_t err;
	json_t* rootJ = json_loads(data.c_str(), 0, &err);
	if (!rootJ) {
		WARN("Siren: failed to parse SirenTags.json: %s; using built-in fallback.", err.text);
		manifest.tags = fallbackTags();
		return manifest;
	}
	DEFER({ json_decref(rootJ); });

	json_t* tagsJ = json_object_get(rootJ, "tags");
	if (!tagsJ || !json_is_array(tagsJ)) {
		WARN("Siren: SirenTags.json missing 'tags' array; using built-in fallback.");
		manifest.tags = fallbackTags();
		return manifest;
	}

	size_t i;
	json_t* entryJ;
	json_array_foreach(tagsJ, i, entryJ) {
		json_t* nameJ = json_object_get(entryJ, "name");
		if (!nameJ || !json_is_string(nameJ)) continue;
		std::string name = json_string_value(nameJ);
		manifest.tags.emplace_back(name);

		// `keywords` is an object mapping each filename keyword to its
		// reliability prior in (0, 1]. A non-number value falls back to
		// DEFAULT_KEYWORD_PRIOR; out-of-range priors are clamped.
		json_t* kwJ = json_object_get(entryJ, "keywords");
		if (!kwJ || !json_is_object(kwJ)) continue;
		std::vector<std::pair<std::string, float>> kws;
		const char* word;
		json_t* priorJ;
		json_object_foreach(kwJ, word, priorJ) {
			if (!word) continue;
			float prior = json_is_number(priorJ) ? (float) json_number_value(priorJ)
			                                     : DEFAULT_KEYWORD_PRIOR;
			prior = prior < 0.f ? 0.f : (prior > 1.f ? 1.f : prior);
			kws.emplace_back(word, prior);
		}
		if (!kws.empty()) {
			manifest.keywords[name] = std::move(kws);
		}
	}

	if (manifest.tags.empty()) {
		manifest.tags = fallbackTags();
	}
	return manifest;
}

inline const std::vector<std::string>& starterTags() {
	return tagManifest().tags;
}

// Returns a map from tag name to its (filename keyword, prior) pairs.
// Empty map on parse failure (filename boosting is just skipped).
inline const std::map<std::string, std::vector<std::pair<std::string, float>>>& starterTagKeywords() {
	return tagManifest().keywords;
}


struct SampleMetadata {
	std::string relativePath; // relative to root, using '/' separator
	bool favorite = false;
	std::vector<std::string> tags;
	float bpm = 0.f; // detected BPM, 0 if not detected
	float bpmConfidence = 0.f; // confidence of BPM detection
	float durationSeconds = 0.f; // length of the audio file, 0 if unknown
	int sampleRate = 0; // sample rate in Hz, 0 if unknown
	int bitDepth = 0; // bits per sample, 0 if unknown
	int channels = 0; // channel count, 0 if unknown
	int64_t fileTimestamp = 0; // file mtime when the audio info above was last read, 0 if unknown
};

struct MetadataStore {
	std::string rootPath;
	std::map<std::string, SampleMetadata> samples; // key = relativePath

	virtual ~MetadataStore() = default;

	// Single source of truth for where this root's metadata lives on disk.
	// Callers (data sources) must not derive or guess this path themselves.
	// Virtual so subclasses (e.g. in tests) can redirect persistence elsewhere,
	// such as a scratch folder, without touching the user's real settings folder.
	virtual std::string filePath() const {
		return settingsDirPath() + "/siren-" + hashPath(rootPath) + ".json";
	}

	void load() {
		std::string data;
		if (!vcv::fs::read(filePath(), data)) return;
		json_error_t error;
		json_t* rootJ = json_loads(data.c_str(), 0, &error);
		if (!rootJ) return;
		DEFER({ json_decref(rootJ); });
		fromJson(rootJ);
	}

	// Saves via rename-write-verify-delete so a crash mid-write can never destroy
	// the previous metadata: the existing file is first moved aside to ".bak",
	// the new file is written and parsed back to confirm it's valid JSON, and
	// only then is the backup removed. On verification failure the backup is restored.
	void save() const {
		std::string jsonPath = filePath();
		vcv::fs::createDirectories(vcv::fs::getDirectory(jsonPath));

		json_t* rootJ = toJson();
		DEFER({ json_decref(rootJ); });

		std::string bakPath = jsonPath + ".bak";
		bool hadExisting = vcv::fs::exists(jsonPath);
		if (hadExisting) {
			vcv::fs::rename(jsonPath, bakPath);
		}

		char* dumped = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		if (dumped) {
			vcv::fs::write(jsonPath, dumped);
			free(dumped);
		}

		bool verified = false;
		std::string checkData;
		if (vcv::fs::read(jsonPath, checkData)) {
			json_error_t error;
			json_t* verifyJ = json_loads(checkData.c_str(), 0, &error);
			if (verifyJ) {
				verified = true;
				json_decref(verifyJ);
			}
		}

		if (hadExisting) {
			if (verified) {
				vcv::fs::remove(bakPath);
			}
			else {
				vcv::fs::remove(jsonPath);
				vcv::fs::rename(bakPath, jsonPath);
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
			if (meta.favorite) {
				json_object_set_new(fileJ, "fav", json_true());
			}
			if (!meta.tags.empty()) {
				json_t* tagsJ = json_array();
				for (const std::string& tag : meta.tags) {
					json_array_append_new(tagsJ, json_string(tag.c_str()));
				}
				json_object_set_new(fileJ, "tags", tagsJ);
			}
			if (meta.bpm > 0.f) {
				json_object_set_new(fileJ, "bpm", json_real(meta.bpm));
				json_object_set_new(fileJ, "bpmConfidence", json_real(meta.bpmConfidence));
			}
			if (meta.durationSeconds > 0.f) {
				json_object_set_new(fileJ, "duration", json_real(meta.durationSeconds));
			}
			if (meta.sampleRate > 0) {
				json_object_set_new(fileJ, "sampleRate", json_integer(meta.sampleRate));
			}
			if (meta.bitDepth > 0) {
				json_object_set_new(fileJ, "bitDepth", json_integer(meta.bitDepth));
			}
			if (meta.channels > 0) {
				json_object_set_new(fileJ, "channels", json_integer(meta.channels));
			}
			if (meta.fileTimestamp != 0) {
				json_object_set_new(fileJ, "mtime", json_integer((json_int_t)meta.fileTimestamp));
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
			size_t i;
			json_t* fileJ;
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
					size_t j;
					json_t* tagJ;
					json_array_foreach(tagsJ, j, tagJ) {
						if (json_is_string(tagJ)) {
							std::string t = rack::string::trim(json_string_value(tagJ));
							if (!t.empty()) meta.tags.push_back(t);
						}
					}
				}

				json_t* bpmJ = json_object_get(fileJ, "bpm");
				if (bpmJ) meta.bpm = (float)json_number_value(bpmJ);
				json_t* confJ = json_object_get(fileJ, "bpmConfidence");
				if (confJ) meta.bpmConfidence = (float)json_number_value(confJ);
				json_t* durationJ = json_object_get(fileJ, "duration");
				if (durationJ) meta.durationSeconds = (float)json_number_value(durationJ);
				json_t* sampleRateJ = json_object_get(fileJ, "sampleRate");
				if (sampleRateJ) meta.sampleRate = (int)json_integer_value(sampleRateJ);
				json_t* bitDepthJ = json_object_get(fileJ, "bitDepth");
				if (bitDepthJ) meta.bitDepth = (int)json_integer_value(bitDepthJ);
				json_t* channelsJ = json_object_get(fileJ, "channels");
				if (channelsJ) meta.channels = (int)json_integer_value(channelsJ);
				json_t* mtimeJ = json_object_get(fileJ, "mtime");
				if (mtimeJ) meta.fileTimestamp = (int64_t)json_integer_value(mtimeJ);
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
		for (const std::string& t : meta.tags) {
			if (rack::string::lowercase(rack::string::trim(t)) == tagLow) return;
		}
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

	void clearTags(const std::string& rel) {
		auto it = samples.find(rel);
		if (it != samples.end()) it->second.tags.clear();
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
		for (const auto& pair : samples) {
			for (const std::string& tag : pair.second.tags) insert(tag);
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

	// Set audio file properties (length, sample rate, bit depth, channels) for a relative path,
	// recording the file's mtime at the time of reading so a later loadChildrenSync/Async can
	// skip re-reading the audio header as long as the file hasn't changed since.
	void setAudioInfo(const std::string& rel, float durationSeconds, int sampleRate, int bitDepth, int channels, int64_t fileTimestamp = 0) {
		auto& meta = samples[rel];
		meta.relativePath = rel;
		meta.durationSeconds = durationSeconds;
		meta.sampleRate = sampleRate;
		meta.bitDepth = bitDepth;
		meta.channels = channels;
		meta.fileTimestamp = fileTimestamp;
	}

	// Returns true if cached audio info exists for rel and is still valid for fileTimestamp,
	// i.e. the file hasn't changed since the cached info was read.
	bool hasValidAudioInfo(const std::string& rel, int64_t fileTimestamp) const {
		if (fileTimestamp == 0) return false;
		auto it = samples.find(rel);
		if (it == samples.end()) return false;
		return it->second.fileTimestamp == fileTimestamp;
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
