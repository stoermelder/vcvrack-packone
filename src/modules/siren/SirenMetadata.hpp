#pragma once
#include <rack.hpp>
#include <string>
#include <map>
#include <vector>
#include <set>

namespace StoermelderPackOne {
namespace Siren {

// Suggested starter tags shown in the tag list before the user has assigned any.
// These are never stored unless explicitly assigned to a sample.
static const std::vector<std::string> STARTER_TAGS = {
	"drone", "percussion", "loop", "one-shot", "vocal",
	"field", "texture", "bass", "fx", "ambient"
};

struct SampleMetadata {
	std::string relativePath;  // relative to root, using '/' separator
	bool favorite = false;
	std::vector<std::string> tags;
};

struct RootMetadata {
	std::string rootPath;
	std::map<std::string, SampleMetadata> samples;  // key = relativePath

	void load(const std::string& jsonPath) {
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

		json_t* favsJ = json_array();
		json_t* tagsJ = json_object();
		for (const auto& pair : samples) {
			const SampleMetadata& meta = pair.second;
			if (meta.favorite) {
				json_array_append_new(favsJ, json_string(meta.relativePath.c_str()));
			}
			if (!meta.tags.empty()) {
				json_t* sampleTagsJ = json_array();
				for (const std::string& tag : meta.tags) {
					json_array_append_new(sampleTagsJ, json_string(tag.c_str()));
				}
				json_object_set_new(tagsJ, meta.relativePath.c_str(), sampleTagsJ);
			}
		}
		json_object_set_new(rootJ, "favorites", favsJ);
		json_object_set_new(rootJ, "tags", tagsJ);
		return rootJ;
	}

	void fromJson(json_t* rootJ) {
		samples.clear();
		json_t* rootPathJ = json_object_get(rootJ, "rootPath");
		if (rootPathJ) rootPath = json_string_value(rootPathJ);

		json_t* favsJ = json_object_get(rootJ, "favorites");
		if (favsJ && json_is_array(favsJ)) {
			size_t i; json_t* val;
			json_array_foreach(favsJ, i, val) {
				if (!json_is_string(val)) continue;
				std::string rel = json_string_value(val);
				samples[rel].relativePath = rel;
				samples[rel].favorite = true;
			}
		}

		json_t* tagsJ = json_object_get(rootJ, "tags");
		if (tagsJ && json_is_object(tagsJ)) {
			const char* rel;
			json_t* sampleTagsJ;
			json_object_foreach(tagsJ, rel, sampleTagsJ) {
				if (!json_is_array(sampleTagsJ)) continue;
				samples[rel].relativePath = rel;
				size_t i; json_t* val;
				json_array_foreach(sampleTagsJ, i, val) {
					if (json_is_string(val))
						samples[rel].tags.push_back(json_string_value(val));
				}
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
		for (const std::string& t : STARTER_TAGS) result.insert(t);
		for (const auto& pair : samples) {
			for (const std::string& tag : pair.second.tags)
				result.insert(tag);
		}
		return result;
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
