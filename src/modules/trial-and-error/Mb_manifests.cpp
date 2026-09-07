#include "Mb_manifests.hpp"
#include <ghc/filesystem.hpp>
#include <mutex>
#include <thread>

namespace StoermelderPackOne {
namespace Mb {

static std::mutex manifestsMutex;
static std::map<Model*, int64_t> manifestCreationTimestamps;
static std::atomic<bool> manifestsCacheLoaded{false};

static std::string mbManifestsCacheFilePath() {
	return rack::asset::user("Stoermelder-P1/mb-manifests-cache.json");
}

bool manifestsCacheExists() {
	return manifestsCacheLoaded.load(std::memory_order_relaxed);
}

// Finds a model by plugin/model slug within the given plugin list, mirroring
// plugin::getModel() but parameterized for testability.
static Model* findModel(const std::vector<plugin::Plugin*>& plugins, const std::string& pluginSlug, const std::string& modelSlug) {
	for (plugin::Plugin* p : plugins) {
		if (p->slug != pluginSlug) continue;
		for (plugin::Model* m : p->models) {
			if (m->slug == modelSlug) return m;
		}
	}
	return nullptr;
}

// Parses a manifests-cache JSON root into a Model*->creationTimestamp map.
// Pure function (no file/global-state access) so it can be unit-tested directly.
//
// Format (see https://github.com/VCVRack/library/blob/v2/manifests-cache.json):
// { "<pluginSlug>": { "creationTimestamp": <double>, "modules": { "<modelSlug>": { "creationTimestamp": <double> }, ... } }, ... }
static std::map<Model*, int64_t> manifestsCacheParseJson(json_t* rootJ, const std::vector<plugin::Plugin*>& plugins = rack::plugin::plugins) {
	std::map<Model*, int64_t> parsed;

	const char* pluginSlug;
	json_t* pluginJ;
	json_object_foreach(rootJ, pluginSlug, pluginJ) {
		json_t* pluginCreatedJ = json_object_get(pluginJ, "creationTimestamp");
		double pluginCreated = pluginCreatedJ ? json_number_value(pluginCreatedJ) : 0.0;

		json_t* modulesJ = json_object_get(pluginJ, "modules");
		if (!modulesJ) continue;
		const char* modelSlug;
		json_t* modelJ;
		json_object_foreach(modulesJ, modelSlug, modelJ) {
			Model* model = findModel(plugins, pluginSlug, modelSlug);
			if (!model) continue;
			json_t* createdJ = json_object_get(modelJ, "creationTimestamp");
			double created = createdJ ? json_number_value(createdJ) : pluginCreated;
			parsed[model] = (int64_t)created;
		}
	}
	return parsed;
}

// Parses the local manifests cache file into memory. Runs on the worker thread.
static void manifestsCacheFromJson() {
	FILE* file = fopen(mbManifestsCacheFilePath().c_str(), "r");
	if (!file) {
		manifestsCacheLoaded.store(false, std::memory_order_relaxed);
		return;
	}
	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	fclose(file);
	if (!rootJ) {
		manifestsCacheLoaded.store(false, std::memory_order_relaxed);
		return;
	}
	DEFER({ json_decref(rootJ); });

	std::map<Model*, int64_t> parsed = manifestsCacheParseJson(rootJ);

	{
		std::lock_guard<std::mutex> lock(manifestsMutex);
		manifestCreationTimestamps = std::move(parsed);
	}
	manifestsCacheLoaded.store(true, std::memory_order_relaxed);
}

int64_t manifestCreationTimestampGet(Model* model) {
	std::lock_guard<std::mutex> lock(manifestsMutex);
	auto it = manifestCreationTimestamps.find(model);
	return (it != manifestCreationTimestamps.end()) ? it->second : -1;
}

// True if any installed plugin's manifest (plugin.json) was modified after the
// local cache was last downloaded — covers both newly installed plugins and
// existing plugins that gained (or updated) modules, unlike a plain slug diff.
// Parameterized (cache path + plugin list) for testability; runs on the worker thread.
static bool manifestsCacheIsStale(const std::string& cacheFilePath, const std::vector<plugin::Plugin*>& plugins = rack::plugin::plugins) {
	ghc::filesystem::file_time_type cacheTime;
	try {
		cacheTime = ghc::filesystem::last_write_time(cacheFilePath);
	}
	catch (...) {
		return true; // no local cache yet
	}

	for (plugin::Plugin* p : plugins) {
		if (p->path.empty()) continue; // Core has no plugin.json on disk
		try {
			auto manifestTime = ghc::filesystem::last_write_time(rack::system::join(p->path, "plugin.json"));
			if (manifestTime > cacheTime) return true;
		}
		catch (...) {
			continue;
		}
	}
	return false;
}

// Downloads the manifests cache from the VCV Rack Library. Runs on the worker thread.
static bool manifestsCacheDownload() {
	std::string url = "https://raw.githubusercontent.com/VCVRack/library/v2/manifests-cache.json";
	std::string tmpFile = rack::system::getTempDirectory() + "/mb-manifests-cache.json";

	if (!rack::network::requestDownload(url, tmpFile)) {
		WARN("MB: could not download manifests cache from %s", url.c_str());
		return false;
	}

	rack::system::createDirectory(rack::asset::user("Stoermelder-P1"));
	rack::system::remove(mbManifestsCacheFilePath());
	if (!rack::system::rename(tmpFile, mbManifestsCacheFilePath())) {
		WARN("MB: could not store manifests cache at %s", mbManifestsCacheFilePath().c_str());
		return false;
	}

	return true;
}

// Checks whether the local cache is stale relative to installed plugins and,
// if so, downloads an update. Runs on the worker thread.
static void manifestsCacheAutoUpdateCheck() {
	if (manifestsCacheIsStale(mbManifestsCacheFilePath())) {
		manifestsCacheDownload();
	}
}

void manifestsCacheInit() {
	bool autoUpdate = pluginSettings.mbNewestAutoUpdate;
	std::thread([autoUpdate]() {
		if (autoUpdate) {
			manifestsCacheAutoUpdateCheck();
		}
		manifestsCacheFromJson();
	}).detach();
}

} // namespace Mb
} // namespace StoermelderPackOne
