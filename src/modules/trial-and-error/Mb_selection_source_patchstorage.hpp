#pragma once
#include <string>
#include <vector>
#include <map>
#include <rack.hpp>
#include "Mb_selection_source.hpp"
#include "Mb_selection_source_index.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {
namespace patchstorage {

static constexpr const char* SLUG = "patchstorage";


// Cache for patch metadata
struct PatchInfo {
	int id;
	int fileId;
	std::string title;
	std::string slug;
	std::string description;
	int categoryId;
	std::string categorySlug;
	std::string downloadUrl;
	std::string filename;
	int filesize;
	std::vector<std::string> tags;
};

/**
 * PatchStorageSourceIndex - minimal index for remote patches.
 * Since patches are downloaded on-demand, we store only basic metadata.
 */
struct PatchStorageSourceIndex : SelectionSourceIndex {
	std::shared_ptr<std::map<std::string, PatchInfo>> patchInfo;

	// Shared pointer to all tags (loaded from API via source)
	std::shared_ptr<std::set<std::string>> allTags;

	json_t* toJson() const {
		json_t* j = json_object();
		/*
		for (const auto& pair : entries) {
			json_t* entryJ = json_object();
			json_object_set_new(entryJ, "description", json_string(pair.second.description.c_str()));
			json_t* tagsJ = json_array();
			for (const std::string& tag : pair.second.tags) {
				json_array_append_new(tagsJ, json_string(tag.c_str()));
			}
			json_object_set_new(entryJ, "tags", tagsJ);
			json_t* customJ = json_array();
			for (const std::string& tag : pair.second.customTags) {
				json_array_append_new(customJ, json_string(tag.c_str()));
			}
			json_object_set_new(entryJ, "customTags", customJ);
			json_object_set_new(entryJ, "favorite", json_boolean(pair.second.favorite));
			json_object_set_new(j, pair.first.c_str(), entryJ);
		}
		*/
		return j;
	}

	bool fromJson(json_t* indexJ) {
		return true;
		/*
		if (!indexJ || !json_is_object(indexJ)) return false;

		patchInfo.clear();
		const char* fileId;
		json_t* entryJ;
		json_object_foreach(indexJ, fileId, entryJ) {
			if (!json_is_object(entryJ)) continue;
			FileIndexEntry entry;

			json_t* descJ = json_object_get(entryJ, "description");
			if (descJ) entry.description = json_string_value(descJ);

			json_t* tagsJ = json_object_get(entryJ, "tags");
			if (tagsJ && json_is_array(tagsJ)) {
				size_t i;
				json_t* val;
				json_array_foreach(tagsJ, i, val) {
					if (json_is_string(val)) {
						entry.tags.push_back(json_string_value(val));
					}
				}
			}

			json_t* customJ = json_object_get(entryJ, "customTags");
			if (customJ && json_is_array(customJ)) {
				size_t i;
				json_t* val;
				json_array_foreach(customJ, i, val) {
					if (json_is_string(val)) {
						entry.customTags.push_back(json_string_value(val));
					}
				}
			}

			json_t* favJ = json_object_get(entryJ, "favorite");
			if (favJ) entry.favorite = json_boolean_value(favJ);

			entries[fileId] = entry;
		}
		*/
		return true;
	}

	const std::string getDescription(const std::string& fileId) const override {
		auto it = patchInfo->find(fileId);
		return it != patchInfo->end() ? it->second.description : "";
	}
	void setDescription(const std::string& fileId, const std::string& description) override {
		// Readonly
	}

	bool hasTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = (*patchInfo)[fileId].tags;
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}
	std::vector<std::string> getTags(const std::string& fileId) const override {
		auto it = patchInfo->find(fileId);
		return it != patchInfo->end() ? it->second.tags : std::vector<std::string>();
	}
	void addTag(const std::string& fileId, const std::string& tag) override {
		//auto& tags = (*patchInfo)[fileId].tags;
		//if (std::find(tags.begin(), tags.end(), tag) == tags.end())
		//	tags.push_back(tag);
	}
	void removeTag(const std::string& fileId, const std::string& tag) override {
		//auto& tags = (*patchInfo)[fileId].tags;
		//tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
	}

	bool hasCustomTag(const std::string& fileId, const std::string& tag) override {
		return false;
		//auto& tags = (*patchInfo)[fileId].customTags;
		//return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}

	std::vector<std::string> getCustomTags(const std::string& fileId) const override {
		return {};
		//auto it = patchInfo->find(fileId);
		//return it != patchInfo->end() ? it->second.customTags : std::vector<std::string>();
	}

	void addCustomTag(const std::string& fileId, const std::string& tag) override {
		return;
		//auto& tags = (*patchInfo)[fileId].customTags;
		//if (std::find(tags.begin(), tags.end(), tag) == tags.end())
		//	tags.push_back(tag);
	}

	void removeCustomTag(const std::string& fileId, const std::string& tag) override {
		return;
		//auto& tags = (*patchInfo)[fileId].customTags;
		//tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
	}

	bool isFavorite(const std::string& fileId) const override {
		return false;
		//auto it = patchInfo->find(fileId);
		//return it != patchInfo->end() && it->second.favorite;
	}
	void setFavorite(const std::string& fileId, bool favorite) override {
		//(*patchInfo)[fileId].favorite = favorite;
	}

	bool isReadOnly() const override { 
		return true;
	}

	std::set<std::string> getTagsAll() const override {
		if (allTags) {
			return *allTags;
		}
		return {};
	}

	std::set<std::string> getCustomTagsAll() const override {
		if (allTags) {
			return *allTags;
		}
		return {};
	}
};

/**
 * PatchStorageSource - SelectionSource implementation for patchstorage.com API.
 * Uses categories as containers, lists patches filtered by VCV Rack platform.
 */
struct PatchStorageSource : SelectionSource {
	static constexpr const char* API_BASE = "https://patchstorage.com/api/beta";
	static constexpr const char* PLATFORM_SLUG = "vcv-rack";

	std::string currentContainer;
	PatchStorageSourceIndex index;
	SelectionBrowserHelper* helper;

	int platformId = -1; // Lazily fetched

	// Current status - use "0:message" for indefinite, "2:message" for 2 seconds
	std::string status = "";

	// Cache for categories (category slugs mapped to category info)
	struct CategoryInfo {
		int id;
		std::string name;
		std::string slug;
	};
	// Shared category cache (loaded from API)
	std::shared_ptr<std::vector<CategoryInfo>> categories;

	// Cache for patches per category
	std::shared_ptr<std::map<std::string, std::vector<std::string>>> categoryPatches;

	std::shared_ptr<std::map<std::string, PatchInfo>> patchInfo;

	// Download cache
	std::shared_ptr<std::map<std::string, std::string>> patches;

	// Cache for all tags (loaded lazily from API)
	std::shared_ptr<std::set<std::string>> allTags;

	/** Generate a random cache folder name. */
	std::string generateCacheName() const {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		static std::uniform_int_distribution<> dis(0, 15);
		std::string name = "ps_cache_";
		for (int i = 0; i < 16; ++i) {
			int v = dis(gen);
			name += (v < 10) ? char('0' + v) : char('a' + v - 10);
		}
		return name;
	}

	/** Get the cache directory from the helper. */
	const std::string& getCacheDir() const {
		return helper->cacheDir;
	}

	void setCacheDir(const std::string& dir) override {
		helper->cacheDir = dir;
		system::createDirectories(helper->cacheDir);
	}

	/** Fetch JSON from a URL using the Rack network API. */
	json_t* fetchJson(const std::string& url) const {
		json_t* result = rack::network::requestJson(rack::network::METHOD_GET, url);
		return result;
	}

	/** Get platform ID for VCV Rack, fetching lazily on first use. Returns -1 if not found. */
	int getVcvRackPlatformId() {
		if (platformId >= 0) return platformId;
		
		status = "0:Fetching platform info...";
		json_t* platformsJ = fetchJson(string::f("%s/platforms?per_page=100", API_BASE));
		if (!platformsJ) {
			status = "2:Failed to fetch platforms";
			return -1;
		}
		DEFER({ json_decref(platformsJ); });

		if (!json_is_array(platformsJ)) {
			status = "";
			return -1;
		}

		size_t i;
		json_t* val;
		json_array_foreach(platformsJ, i, val) {
			if (!json_is_object(val)) continue;
			json_t* slugJ = json_object_get(val, "slug");
			if (!slugJ) continue;
			std::string slug = json_string_value(slugJ);
			if (slug == PLATFORM_SLUG) {
				json_t* idJ = json_object_get(val, "id");
				if (idJ) platformId = json_integer_value(idJ);
				break;
			}
		}

		// Async operation complete, clear status
		status = "";
		return platformId;
	}

	/** Load all categories from patchstorage. Lazily fetched on first access. */
	bool loadCategories() {
		if (categories->size() > 0) {
			return true;
		}

		status = "0:Loading categories...";
		int pid = getVcvRackPlatformId();
		if (pid < 0) {
			DEBUG("PatchStorageSource: Could not find VCV Rack platform");
			status = "2:Could not find VCV Rack platform";
			return false;
		}

		// Fetch categories
		status = "0:Fetching categories...";
		json_t* categoriesJ = fetchJson(string::f("%s/categories?per_page=100&hide_empty=true", API_BASE));
		if (!categoriesJ) {
			DEBUG("PatchStorageSource: Failed to fetch categories");
			status = "2:Failed to fetch categories";
			return false;
		}
		DEFER({ json_decref(categoriesJ); });

		if (!json_is_array(categoriesJ)) {
			status = "";
			return false;
		}

		// Load all categories
		size_t i;
		json_t* val;
		json_array_foreach(categoriesJ, i, val) {
			if (!json_is_object(val)) continue;
			
			CategoryInfo info;
			json_t* idJ = json_object_get(val, "id");
			if (!idJ) continue;
			info.id = json_integer_value(idJ);

			json_t* nameJ = json_object_get(val, "name");
			if (nameJ) info.name = json_string_value(nameJ);

			json_t* slugJ = json_object_get(val, "slug");
			if (slugJ) info.slug = json_string_value(slugJ);

			categories->push_back(info);
		}

		// Sort categories by name
		std::sort(categories->begin(), categories->end(), [](const CategoryInfo& a, const CategoryInfo& b) {
			return string::lowercase(a.name) < string::lowercase(b.name);
		});

		// Also load tags when loading categories (tags are fetched from API)
		//loadTags();

		// Async operation complete, clear status
		status = "";
		return true;
	}

	/** Load all tags from the API. */
	bool loadTags() {
		if (allTags->size() > 0) {
			return true;
		}

		status = "0:Loading tags...";
		json_t* tagsJ = fetchJson(string::f("%s/tags?per_page=100", API_BASE));
		if (!tagsJ) {
			DEBUG("PatchStorageSource: Failed to fetch tags");
			status = "2:Failed to fetch tags";
			return false;
		}
		DEFER({ json_decref(tagsJ); });

		if (!json_is_array(tagsJ)) {
			status = "";
			return false;
		}

		size_t i;
		json_t* val;
		json_array_foreach(tagsJ, i, val) {
			if (!json_is_object(val)) continue;
			json_t* nameJ = json_object_get(val, "name");
			if (nameJ) {
				const char* tagName = json_string_value(nameJ);
				if (tagName) allTags->insert(tagName);
			}
		}

		// Async operation complete, clear status
		status = "";
		return true;
	}

	/** Fetch patches for a specific category. */
	std::vector<PatchInfo> fetchPatchesForCategory(const std::string& categorySlug) {
		std::vector<PatchInfo> patches;

		status = string::f("0:Loading %s...", categorySlug.c_str());

		int platformId = getVcvRackPlatformId();
		DEBUG("PatchStorageSource: fetchPatchesForCategory(%s) platformId=%d", categorySlug.c_str(), platformId);
		if (platformId < 0) {
			status = "2:Could not find VCV Rack platform";
			return patches;
		}

		// Find category ID from slug
		int categoryId = -1;
		for (const auto& cat : *categories) {
			if (cat.slug == categorySlug) {
				categoryId = cat.id;
				break;
			}
		}
		DEBUG("PatchStorageSource: categoryId=%d for slug=%s", categoryId, categorySlug.c_str());
		if (categoryId < 0) {
			status = "2:Category not found";
			return patches;
		}

		// Fetch patches for this category
		std::string url = string::f("%s/patches?platforms=%d&categories=%d&per_page=100&order=desc&orderby=date",
			API_BASE, platformId, categoryId);
		DEBUG("PatchStorageSource: fetching URL: %s", url.c_str());
		status = string::f("0:Fetching %s patches...", categorySlug.c_str());
		json_t* patchesJ = fetchJson(url);
		DEBUG("PatchStorageSource: patchesJ=%s", patchesJ ? "valid" : "NULL");
		if (!patchesJ) {
			status = "2:Failed to fetch patches";
			return patches;
		}
		DEFER({ json_decref(patchesJ); });

		if (!json_is_array(patchesJ)) {
			DEBUG("PatchStorageSource: patchesJ is not an array");
			status = "";
			return patches;
		}
		
		DEBUG("PatchStorageSource: patchesJ array size=%zu", json_array_size(patchesJ));

		size_t i;
		json_t* val;
		json_array_foreach(patchesJ, i, val) {
			if (!json_is_object(val)) continue;

			PatchInfo info;
			parsePatchFromJson(val, info, categorySlug);

			DEBUG("PatchStorageSource: patch id=%d downloadUrl='%s'", info.id, info.downloadUrl.c_str());
			if (info.id > 0) {
				patches.push_back(info);
				DEBUG("PatchStorageSource: added patch id=%d title='%s'", info.id, info.title.c_str());
			}
		}

		// Async operation complete, clear status
		status = "";
		return patches;
	}

	void onAttach() override {
		// Initialize shared caches from global cache
		std::string categoriesCacheKey = "patchstorage:categories";
		auto _categories = helper->getGlobalCache<std::vector<CategoryInfo>>(categoriesCacheKey);
		if (_categories) {
			categories = _categories;
		}
		else {
			categories = std::make_shared<std::vector<CategoryInfo>>();
			helper->setGlobalCache(categoriesCacheKey, categories, nullptr);
		}

		std::string categoryPatchesCacheKey = "patchstorage:categoryPatches";
		auto _categoryPatches = helper->getGlobalCache<std::map<std::string, std::vector<std::string>>>(categoryPatchesCacheKey);
		if (_categoryPatches) {
			categoryPatches = _categoryPatches;
		} 
		else {
			categoryPatches = std::make_shared<std::map<std::string, std::vector<std::string>>>();
			helper->setGlobalCache(categoryPatchesCacheKey, categoryPatches, nullptr);
		}

		std::string patchInfoCacheKey = "patchstorage:patchInfo";
		auto _patchInfo = helper->getGlobalCache<std::map<std::string, PatchInfo>>(patchInfoCacheKey);
		if (_patchInfo) {
			patchInfo = _patchInfo;
			index.patchInfo = patchInfo;
		} 
		else {
			patchInfo = std::make_shared<std::map<std::string, PatchInfo>>();
			index.patchInfo = patchInfo;
			helper->setGlobalCache(patchInfoCacheKey, patchInfo, nullptr);
		}

		std::string downloadCacheKey = "patchstorage:patches";
		auto _patches = helper->getGlobalCache<std::map<std::string, std::string>>(downloadCacheKey);
		if (_patches) {
			patches = _patches;
		} 
		else {
			patches = std::make_shared<std::map<std::string, std::string>>();
			helper->setGlobalCache(downloadCacheKey, patches, nullptr);
		}

		std::string tagsCacheKey = "patchstorage:allTags";
		auto _allTags = helper->getGlobalCache<std::set<std::string>>(tagsCacheKey);
		if (_allTags) {
			allTags = _allTags;
			index.allTags = allTags;
		}
		else {
			allTags = std::make_shared<std::set<std::string>>();
			index.allTags = allTags;
			helper->setGlobalCache(tagsCacheKey, allTags, nullptr);
		}
	}

	void onDetach() override {
		// Nothing to do there - caches persist across sessions
	}

	void setHelper(SelectionBrowserHelper* h) override {
		helper = h;
	}

	const std::string getContainer() const override {
		return currentContainer;
	}

	void setContainer(const std::string& container) override {
		currentContainer = container;
	}

	const std::string getRootContainer() const override {
		return "PatchStorage";
	}

	const std::vector<ContainerEntry>& getContainers(const std::string& container) override {
		// Lazy load categories on first access
		loadCategories();
		static std::vector<ContainerEntry> result;
		result.clear();

		// PatchStorage has a flat category structure - no sub-containers
		// Return categories when at root (empty container or "PatchStorage")
		if (container.empty() || container == "PatchStorage") {
			DEBUG("PatchStorageSource: getContainers() for root, %d categories loaded", (int)categories->size());
			for (const auto& cat : *categories) {
				result.push_back({ cat.slug, cat.name });
			}
		}
		return result;
	}

	const std::vector<ContainerEntry>& getFiles(const std::string& container) override {
		static std::vector<ContainerEntry> result;
		result.clear();

		// At root level (empty or "PatchStorage"), return no files - only categories
		if (container.empty() || container == "PatchStorage") {
			return result;
		}

		// Check cache first
		auto it = categoryPatches->find(container);
		if (it != categoryPatches->end()) {
			DEBUG("PatchStorageSource: getFiles() cache hit for %s with %d patches", container.c_str(), (int)it->second.size());
			for (const auto& patchId : it->second) {
				const auto& patch = (*patchInfo)[patchId];
				result.push_back({ patchId, patch.title });
			}
			return result;
		}

		DEBUG("PatchStorageSource: getFiles() cache miss for %s, fetching...", container.c_str());
		// Fetch patches for this category
		auto patches = fetchPatchesForCategory(container);
		DEBUG("PatchStorageSource: fetchPatchesForCategory(%s) returned %d patches", container.c_str(), (int)patches.size());

		std::vector<std::string> patchIds;

		for (const auto& patch : patches) {
			std::string patchIdStr = string::f("%d", patch.id);
			patchIds.push_back(patchIdStr);
			(*patchInfo)[patchIdStr] = patch;
			result.push_back({ patchIdStr, patch.title });
		}

		(*categoryPatches)[container] = patchIds;
		return result;
	}

	bool isContainer(const std::string& path) override {
		DEBUG("PatchStorageSource: isContainer(%s) checking %d categories", path.c_str(), (int)categories->size());
		for (const auto& cat : *categories) {
			if (cat.slug == path) return true;
		}
		return false;
	}

	bool isFile(const std::string& path) override {
		bool isFile = patchInfo->find(path) != patchInfo->end();
		DEBUG("PatchStorageSource: isFile(%s) = %s", path.c_str(), isFile ? "true" : "false");
		return isFile;
	}

	const std::string getParentContainer(const std::string& path) override {
		// First check if this is a category (container)
		for (const auto& cat : *categories) {
			if (cat.slug == path) {
				return "PatchStorage";
			}
		}
		// Otherwise check if it's a patch ID
		auto it = patchInfo->find(path);
		if (it != patchInfo->end()) {
			return it->second.categorySlug;
		}
		return "PatchStorage";
	}

	const std::string getFilename(const std::string& path) override {
		auto it = patchInfo->find(path);
		if (it != patchInfo->end()) {
			if (!it->second.filename.empty()) return it->second.filename;
			return it->second.slug + ".vcv";
		}
		return path;
	}

	const std::string getAbsoluteFilePath(const std::string& path) override {
		// For PatchStorage, we need to download the file first
		auto it = patches->find(path);
		if (it != patches->end()) {
			DEBUG("PatchStorageSource: getAbsoluteFilePath(%s) - cached at %s", path.c_str(), it->second.c_str());
			return it->second;
		}

		auto pit = patchInfo->find(path);
		if (pit == patchInfo->end()) {
			DEBUG("PatchStorageSource: getAbsoluteFilePath(%s) - not in patchInfo", path.c_str());
			status = "2:Unknown patch";
			return "";
		}

		PatchInfo& patchInfo = pit->second;
		DEBUG("PatchStorageSource: getAbsoluteFilePath(%s) found patch id=%d fileId=%d downloadUrl='%s'", path.c_str(), patchInfo.id, patchInfo.fileId, patchInfo.downloadUrl.c_str());
		
		// If no download URL, fetch individual patch details
		if (patchInfo.downloadUrl.empty() && patchInfo.id > 0) {
			DEBUG("PatchStorageSource: no downloadUrl, fetching patch details for id=%d", patchInfo.id);
			status = "0:Loading patch info...";
			std::string detailUrl = string::f("%s/patches/%d", API_BASE, patchInfo.id);
			json_t* patchJ = fetchJson(detailUrl);
			if (patchJ) {
				// Get files array
				json_t* filesJ = json_object_get(patchJ, "files");
				if (filesJ && json_is_array(filesJ) && json_array_size(filesJ) > 0) {
					json_t* firstFile = json_array_get(filesJ, 0);
					if (firstFile && json_is_object(firstFile)) {
						json_t* fileIdJ = json_object_get(firstFile, "id");
						if (fileIdJ) patchInfo.fileId = json_integer_value(fileIdJ);
						
						json_t* fileUrlJ = json_object_get(firstFile, "url");
						if (fileUrlJ) patchInfo.downloadUrl = json_string_value(fileUrlJ);

						json_t* filenameJ = json_object_get(firstFile, "filename");
						if (filenameJ) patchInfo.filename = json_string_value(filenameJ);
						
						json_t* filesizeJ = json_object_get(firstFile, "filesize");
						if (filesizeJ) patchInfo.filesize = json_integer_value(filesizeJ);

						DEBUG("PatchStorageSource: fetched details: fileId=%d url='%s' filename='%s'", 
							patchInfo.fileId, patchInfo.downloadUrl.c_str(), patchInfo.filename.c_str());
					}
				}

				json_t* contentJ = json_object_get(patchJ, "content");
				if (contentJ) patchInfo.description = json_string_value(contentJ);

				json_decref(patchJ);
			}
			// Successfully fetched patch details
			status = "";
		}

		if (patchInfo.downloadUrl.empty()) {
			DEBUG("PatchStorageSource: getAbsoluteFilePath(%s) - empty downloadUrl", path.c_str());
			status = "2:Download URL not found";
			return "";
		}

		status = string::f("0:Downloading: %s... (%ikb)", patchInfo.title.c_str(), patchInfo.filesize / 1024);
		DEBUG("PatchStorageSource: getAbsoluteFilePath(%s) - downloading from %s", path.c_str(), patchInfo.downloadUrl.c_str());
		
		// Generate cache path
		std::string cacheName = generateCacheName();
		std::string cachePath = system::join(getCacheDir(), cacheName);
		system::createDirectories(cachePath);
		std::string archivePath = system::join(cachePath, patchInfo.filename.empty() ? "patch.vcv" : patchInfo.filename);

		DEBUG("PatchStorageSource: archivePath=%s", archivePath.c_str());

		// Download the file
		if (!rack::network::requestDownload(patchInfo.downloadUrl, archivePath)) {
			DEBUG("PatchStorageSource: download FAILED for %s", patchInfo.downloadUrl.c_str());
			system::removeRecursively(cachePath);
			status = "2:Download failed";
			return "";
		}

		DEBUG("PatchStorageSource: download SUCCESS, archive at %s", archivePath.c_str());
		(*patches)[path] = archivePath;
		status = "";
		return archivePath;
	}

	json_t* getFileJson(const std::string& path) const override {
		// Download and extract if not cached
		DEBUG("PatchStorageSource: getFileJson(%s) called", path.c_str());
		std::string archivePath = const_cast<PatchStorageSource*>(this)->getAbsoluteFilePath(path);
		DEBUG("PatchStorageSource: getFileJson archivePath=%s", archivePath.c_str());
		if (archivePath.empty()) return nullptr;

		// Check if it's a legacy v1 .vcv (plain JSON) or v2+ (zstd compressed tar)
		std::string ext = system::getExtension(archivePath);
		DEBUG("PatchStorageSource: getFileJson ext=%s", ext.c_str());
		if (ext == ".json") {
			// Legacy v1 format
			FILE* f = fopen(archivePath.c_str(), "rb");
			if (!f) return nullptr;
			json_error_t error;
			json_t* rootJ = json_loadf(f, 0, &error);
			fclose(f);
			return rootJ;
		} 
		else {
			// v2+ format: extract from archive
			return extractPatchJson(archivePath);
		}
	}

	/**
	 * Extract and parse patch.json from a .vcv archive.
	 */
	json_t* extractPatchJson(const std::string& archivePath) const {
		// Extract to temp directory
		std::string cacheName = generateCacheName();
		std::string extractDir = system::join(getCacheDir(), cacheName + "_extract");
		system::createDirectories(extractDir);

		try {
			system::unarchiveToDirectory(archivePath, extractDir);
		}
		catch (...) {
			system::removeRecursively(extractDir);
			return nullptr;
		}

		std::string patchJsonPath = system::join(extractDir, "patch.json");
		FILE* f = fopen(patchJsonPath.c_str(), "rb");
		if (!f) {
			system::removeRecursively(extractDir);
			return nullptr;
		}

		json_error_t error;
		json_t* rootJ = json_loadf(f, 0, &error);
		fclose(f);
		system::removeRecursively(extractDir);

		return rootJ;
	}

	/** Simple URL encoding for search query - encode spaces and special chars. */
	std::string urlEncode(const std::string& s) {
		std::string result;
		for (char c : s) {
			if (c == ' ') {
				result += "%20";
			} else if (c == '&' || c == '=' || c == '%' || c == '+') {
				char buf[4];
				snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
				result += buf;
			} else {
				result += c;
			}
		}
		return result;
	}

	/** Parse a patch object from JSON into a PatchInfo struct. */
	void parsePatchFromJson(json_t* val, PatchInfo& info, const std::string& categoryOverride = "") {
		json_t* idJ = json_object_get(val, "id");
		if (idJ) info.id = json_integer_value(idJ);

		json_t* titleJ = json_object_get(val, "title");
		if (titleJ) info.title = json_string_value(titleJ);

		json_t* slugJ = json_object_get(val, "slug");
		if (slugJ) info.slug = json_string_value(slugJ);

		json_t* excerptJ = json_object_get(val, "excerpt");
		if (excerptJ) info.description = json_string_value(excerptJ);

		// Parse tags from the patch and collect into allTags
		json_t* tagsJ = json_object_get(val, "tags");
		if (tagsJ && json_is_array(tagsJ)) {
			size_t ti;
			json_t* tagVal;
			json_array_foreach(tagsJ, ti, tagVal) {
				if (json_is_object(tagVal)) {
					json_t* nameJ = json_object_get(tagVal, "name");
					if (nameJ) {
						const char* tagName = json_string_value(nameJ);
						if (tagName) {
							info.tags.push_back(tagName);
							// Collect tag into allTags
							if (allTags) allTags->insert(tagName);
						}
					}
				}
			}
		}

		// Get first file's ID and download URL
		json_t* filesJ = json_object_get(val, "files");
		DEBUG("PatchStorageSource: patch %d '%s' has files=%s", info.id, info.title.c_str(), filesJ ? "yes" : "no");
		if (filesJ && json_is_array(filesJ) && json_array_size(filesJ) > 0) {
			json_t* firstFile = json_array_get(filesJ, 0);
			if (firstFile && json_is_object(firstFile)) {
				json_t* fileIdJ = json_object_get(firstFile, "id");
				if (fileIdJ) info.fileId = json_integer_value(fileIdJ);

				json_t* fileUrlJ = json_object_get(firstFile, "url");
				if (fileUrlJ) info.downloadUrl = json_string_value(fileUrlJ);

				json_t* filenameJ = json_object_get(firstFile, "filename");
				if (filenameJ) info.filename = json_string_value(filenameJ);
			}
		}


		// Get category info - use override if provided, otherwise parse from JSON
		if (!categoryOverride.empty()) {
			info.categorySlug = categoryOverride;
		} 
		else {
			json_t* categoriesJ = json_object_get(val, "categories");
			if (categoriesJ && json_is_array(categoriesJ) && json_array_size(categoriesJ) > 0) {
				json_t* firstCat = json_array_get(categoriesJ, 0);
				if (firstCat && json_is_object(firstCat)) {
					json_t* catSlugJ = json_object_get(firstCat, "slug");
					if (catSlugJ) info.categorySlug = json_string_value(catSlugJ);
				}
			}
		}
	}

	std::vector<ContainerEntry> search(const std::string& query) override {
		std::vector<ContainerEntry> results;

		status = string::f("0:Searching: %s...", query.c_str());

		int platformId = getVcvRackPlatformId();
		if (platformId < 0) {
			status = "2:Could not find VCV Rack platform";
			return results;
		}

		// Search patches using the API
		std::string url = string::f("%s/patches?platforms=%d&search=%s&per_page=100&order=desc&orderby=relevance",
			API_BASE, platformId, urlEncode(query).c_str());
		DEBUG("PatchStorageSource: search(%s) URL: %s", query.c_str(), url.c_str());

		json_t* patchesJ = fetchJson(url);
		if (!patchesJ) {
			status = "2:Search failed";
			return results;
		}
		DEFER({ json_decref(patchesJ); });

		if (!json_is_array(patchesJ)) {
			status = "";
			return results;
		}

		size_t i;
		json_t* val;
		json_array_foreach(patchesJ, i, val) {
			if (!json_is_object(val)) continue;

			PatchInfo info;
			parsePatchFromJson(val, info);

			if (info.id > 0) {
				std::string patchIdStr = string::f("%d", info.id);
				(*patchInfo)[patchIdStr] = info;
				results.push_back({ patchIdStr, info.title });
			}
		}

		status = "";
		return results;
	}

	// Clear only runtime caches (downloaded files), not the persistent API caches
	void clearCache() {
		if (!helper->cacheDir.empty() && system::exists(helper->cacheDir)) {
			system::removeRecursively(helper->cacheDir);
			helper->cacheDir.clear();
		}
		patches->clear();
	}

	const std::string getSourceType() const override {
		return SLUG;
	}

	const std::string getSourceName() const override {
		return "PatchStorage.com";
	}

	bool isPatchSource() const override {
		return true;
	}

	SelectionSourceIndex* getIndex() const override {
		return const_cast<PatchStorageSourceIndex*>(&index);
	}

	json_t* toJson() const override {
		json_t* j = json_object();
		json_object_set_new(j, "slug", json_string(SLUG));
		return j;
	}

	bool fromJson(json_t* sourceJ) override {
		json_t* slugJ = json_object_get(sourceJ, "slug");
		if (!slugJ) return false;
		return std::string(json_string_value(slugJ)) == SLUG;
	}

	const std::string& getStatusText() override {
		return status;
	}

	void appendSourceMenuItems(ui::Menu* menu) override {
		menu->addChild(createMenuLabel("PatchStorage.com - VCV Rack"));
	}

	void appendPreviewMenuItems(ui::Menu* menu, std::string fileId) override {
		auto it = patchInfo->find(fileId);
		if (it == patchInfo->end()) return;

		const PatchInfo& patchInfo = it->second;

		menu->addChild(createMenuLabel(patchInfo.title));
		menu->addChild(createMenuItem("Open in web browser", "", [patchInfo]() {
			std::string url = string::f("https://patchstorage.com/patch/%s", patchInfo.slug.c_str());
			system::openBrowser(url);
		}));
	}
};

static bool patchstorageCreated_ = false;

inline std::string getSlug() {
	return SLUG;
}

inline bool canCreate() {
	return !patchstorageCreated_;
}

inline SelectionSource* initSource() {
	patchstorageCreated_ = true;
	PatchStorageSource* src = new PatchStorageSource;
	return src;
}

} // namespace patchstorage
} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne
