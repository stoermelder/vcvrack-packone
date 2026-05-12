#pragma once
#include <rack.hpp>
#include <osdialog.h>
#include <ghc/filesystem.hpp>
#include "Mb_selection_source.hpp"
#include "Mb_selection_source_index.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {
namespace filesystem {


/**
 * Index for a FileSystemSource, stored as mb-index.json
 * in the root of the source folder.
 * This is a read-only index; set methods have no effect.
 */
struct FileSystemSourceIndex : SelectionSourceIndex {
	struct FileIndexEntry {
		std::string description;
		std::vector<std::string> tags;
		std::vector<std::string> customTags;
		bool favorite = false;
	};

	std::map<std::string, FileIndexEntry> entries_;
	bool readOnly_ = false;

	json_t* toJson() const {
		json_t* j = json_object();
		for (const auto& pair : entries_) {
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
		return j;
	}

	bool fromJson(json_t* indexJ) {
		if (!indexJ || !json_is_object(indexJ)) return false;

		entries_.clear();
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

			entries_[fileId] = entry;
		}
		return true;
	}

	std::string getDescription(const std::string& fileId) const override {
		auto it = entries_.find(fileId);
		return it != entries_.end() ? it->second.description : "";
	}
	void setDescription(const std::string& fileId, const std::string& description) override {
		if (!readOnly_) entries_[fileId].description = description;
	}

	bool hasTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries_[fileId].tags;
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}
	std::vector<std::string> getTags(const std::string& fileId) const override {
		auto it = entries_.find(fileId);
		return it != entries_.end() ? it->second.tags : std::vector<std::string>();
	}
	void addTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly_) {
			auto& tags = entries_[fileId].tags;
			if (std::find(tags.begin(), tags.end(), tag) == tags.end())
				tags.push_back(tag);
		}
	}
	void removeTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly_) {
			auto& tags = entries_[fileId].tags;
			tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
		}
	}

	bool hasCustomTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries_[fileId].customTags;
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}
	std::vector<std::string> getCustomTags(const std::string& fileId) const override {
		auto it = entries_.find(fileId);
		return it != entries_.end() ? it->second.customTags : std::vector<std::string>();
	}
	void addCustomTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly_) {
			auto& tags = entries_[fileId].customTags;
			if (std::find(tags.begin(), tags.end(), tag) == tags.end())
				tags.push_back(tag);
		}
	}
	void removeCustomTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly_) {
			auto& tags = entries_[fileId].customTags;
			tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
		}
	}

	bool isFavorite(const std::string& fileId) const override {
		auto it = entries_.find(fileId);
		return it != entries_.end() && it->second.favorite;
	}
	void setFavorite(const std::string& fileId, bool favorite) override {
		if (!readOnly_) {
			entries_[fileId].favorite = favorite;
		}
	}

	bool isReadOnly() const override { return readOnly_; }

	std::vector<std::string> getTagsAll() const override {
		std::set<std::string> uniqueTags;
		for (const auto& pair : entries_) {
			for (const std::string& tag : pair.second.tags) {
				uniqueTags.insert(tag);
			}
		}
		return std::vector<std::string>(uniqueTags.begin(), uniqueTags.end());
	}

	std::vector<std::string> getCustomTagsAll() const override {
		std::set<std::string> uniqueTags;
		for (const auto& pair : entries_) {
			for (const std::string& tag : pair.second.customTags) {
				uniqueTags.insert(tag);
			}
		}
		return std::vector<std::string>(uniqueTags.begin(), uniqueTags.end());
	}
};

struct FileSystemSource : SelectionSource {
	std::string rootContainer;
	std::string currentContainer;
	FileSystemSourceIndex index;

	struct CacheEntry {
		std::string cachePath;
		std::chrono::system_clock::time_point lastWriteTime;
	};
	mutable std::map<std::string, CacheEntry> cache_;
	mutable std::string cacheDir_;

	/** Generate a random cache folder name. */
	std::string generateCacheName() const {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		static std::uniform_int_distribution<> dis(0, 15);
		std::string name = "vcv_cache_";
		for (int i = 0; i < 16; ++i) {
			int v = dis(gen);
			name += (v < 10) ? char('0' + v) : char('a' + v - 10);
		}
		return name;
	}

	/** Get or create the cache directory. */
	const std::string& getCacheDir() const {
		if (cacheDir_.empty()) {
			cacheDir_ = system::join(system::getTempDirectory(), "vcv_cache");
			system::createDirectories(cacheDir_);
		}
		return cacheDir_;
	}

	/**
	 * Extract a .vcv archive to a cached folder.
	 * Returns the path to the cached extraction, or empty on failure.
	 */
	std::string extractToCache(const std::string& archivePath) const {
		std::string cacheName = generateCacheName();
		std::string extractDir = system::join(getCacheDir(), cacheName);
		system::createDirectories(extractDir);

		try {
			system::unarchiveToDirectory(archivePath, extractDir);
		}
		catch (...) {
			system::removeRecursively(extractDir);
			return "";
		}
		return extractDir;
	}

	/** Clear all cached extractions. */
	void clearCache() const {
		if (!cacheDir_.empty() && system::exists(cacheDir_)) {
			system::removeRecursively(cacheDir_);
			cacheDir_.clear();
		}
		cache_.clear();
	}

	/** Invalidate a single cache entry if the file has changed. */
	void invalidateCacheEntry(const std::string& fileId) const {
		auto it = cache_.find(fileId);
		if (it != cache_.end()) {
			std::string fullPath = rootContainer + "/" + fileId;
			auto fileTime = ghc::filesystem::last_write_time(fullPath);
			if (fileTime != it->second.lastWriteTime) {
				system::removeRecursively(it->second.cachePath);
				cache_.erase(it);
			}
		}
	}

	static constexpr const char* SLUG_VCVS = "filesystem:vcvs";
	static constexpr const char* SLUG_VCV = "filesystem:vcv";
	std::string slug;

	/** Show a folder picker dialog, returning the chosen path or empty on cancel. */
	static std::string selectFolder() {
		std::string dir = asset::user("selections");
		char* path = osdialog_file(OSDIALOG_OPEN_DIR, dir.c_str(), NULL, NULL);
		if (!path) return "";
		std::string result(path);
		free(path);
		return result;
	}

	void onAttach() override {
		// If no root is set, default to the user's selections directory
		if (rootContainer.empty()) {
			rootContainer = currentContainer = asset::user("selections");
		}
		loadIndex();
	}

	void onDetach() override {
		saveIndex();
		clearCache();
	}

	const std::string getContainer() const override { 
		return currentContainer;
	}
	void setContainer(const std::string& path) override { 
		currentContainer = path;
	}

	const std::string getRootContainer() const override { 
		return rootContainer;
	}

	const std::vector<std::string> getContainers(const std::string& container) override {
		auto entries = system::getEntries(container);
		std::vector<std::string> containers;
		for (const std::string& entry : entries) {
			if (system::isDirectory(entry)) {
				// Strip rootContainer prefix to return relative path
				std::string relative = entry.substr(rootContainer.empty() ? 0 : rootContainer.size() + 1);
				containers.push_back(relative);
			}
		}
		std::sort(containers.begin(), containers.end(), [this](const std::string& a, const std::string& b) {
			return string::lowercase(getFilename(a)) < string::lowercase(getFilename(b));
		});
		return containers;
	}

	const std::vector<std::string> getFiles(const std::string& container) override {
		auto entries = system::getEntries(container);
		std::vector<std::string> files;
		for (const std::string& entry : entries) {
			std::string ext = slug == SLUG_VCVS ? ".vcvs" : ".vcv";
			if (system::isFile(entry) && SelectionSource::endsWith(entry, ext)) {
				// Strip rootContainer prefix to return relative path
				std::string relative = entry.substr(rootContainer.empty() ? 0 : rootContainer.size() + 1);
				files.push_back(relative);
			}
		}
		std::sort(files.begin(), files.end(), [this](const std::string& a, const std::string& b) {
			return string::lowercase(getFilename(a)) < string::lowercase(getFilename(b));
		});
		return files;
	}

	bool isContainer(const std::string& path) override {
		return system::isDirectory(resolve(path));
	}

	bool isFile(const std::string& path) override {
		return system::isFile(resolve(path));
	}

	const std::string getParentContainer(const std::string& path) override {
		std::string absolute = resolve(path);
		return system::getDirectory(absolute);
	}

	const std::string getFilename(const std::string& path) override {
		return system::getFilename(resolve(path));
	}

	const std::string getAbsoluteFilePath(const std::string& fileId) override {
		return rootContainer + "/" + fileId;
	}

	/** Convert a relative path to an absolute path using rootContainer. */
	const std::string resolve(const std::string& path) const {
		if (rootContainer.empty()) return path;
		if (path.empty()) return rootContainer;
		return rootContainer + "/" + path;
	}

	/** Load index from mb-index.json in the root container, if it exists. */
	void loadIndex() {
		if (rootContainer.empty()) return;
		std::string ext = slug == SLUG_VCVS ? "vcvs" : "vcv";
		std::string indexPath = rootContainer + "/mb-index." + ext + ".json";
		FILE* f = fopen(indexPath.c_str(), "rb");
		if (!f) return;
		json_error_t error;
		json_t* indexJ = json_loadf(f, 0, &error);
		fclose(f);
		if (!indexJ) return;
		index.fromJson(indexJ);
		json_decref(indexJ);
	}

	/** Save index to mb-index.json in the root container. */
	void saveIndex() const {
		if (rootContainer.empty()) return;
		std::string ext = slug == SLUG_VCVS ? "vcvs" : "vcv";
		std::string indexPath = rootContainer + "/mb-index." + ext + ".json";
		json_t* indexJ = index.toJson();
		FILE* f = fopen(indexPath.c_str(), "wb");
		if (!f) {
			json_decref(indexJ);
			return;
		}
		json_dumpf(indexJ, f, JSON_INDENT(2));
		fclose(f);
		json_decref(indexJ);
	}

	SelectionSourceIndex* getIndex() const override {
		return const_cast<FileSystemSourceIndex*>(&index);
	}

	const std::string getSourceType() const override {
		return slug;
	}

	const std::string getSourceName() const override {
		std::string ext = slug == SLUG_VCVS ? ".vcvs" : ".vcv";
		if (!rootContainer.empty()) return string::f("%s folder: %s", ext, rootContainer.c_str());
		return "(no folder)";
	}

	json_t* getFileJson(const std::string& fileId) const override {
		std::string fullPath = rootContainer + "/" + fileId;
		
		if (slug == SLUG_VCV) {
			// Check if .vcv file is legacy (plain JSON) or v2+ (zstd-compressed tar)
			if (isVcvLegacyV1(fullPath)) {
				// Legacy v1 format: plain JSON file
				FILE* f = fopen(fullPath.c_str(), "rb");
				if (!f) return nullptr;
				json_error_t error;
				json_t* rootJ = json_loadf(f, 0, &error);
				fclose(f);
				return rootJ;
			}
			else {
				// v2+ format: zstd-compressed tar archive
				return getFileJsonFromArchive(fullPath);
			}
		}
		else {
			// .vcvs files are plain JSON
			FILE* f = fopen(fullPath.c_str(), "rb");
			if (!f) return nullptr;
			json_error_t error;
			json_t* rootJ = json_loadf(f, 0, &error);
			fclose(f);
			return rootJ;
		}
	}

	/**
	 * Checks if a .vcv file is a legacy v1 format (plain JSON) by checking for zstd magic number.
	 * All Zstandard frames start with the magic bytes \x28\xb5\x2f\xfd.
	 * If the file doesn't begin with this magic number, it's a legacy v1 patch.
	 */
	static bool isVcvLegacyV1(const std::string& path) {
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f) return true; // Assume legacy on open failure
		DEFER({std::fclose(f);});
		char zstdMagic[] = "\x28\xb5\x2f\xfd";
		char buf[4] = {};
		std::fread(buf, 1, sizeof(buf), f);
		return std::memcmp(buf, zstdMagic, sizeof(buf)) != 0;
	}

	/**
	 * Extracts and parses the JSON file from a .vcv archive.
	 * Uses system::unarchiveToDirectory to extract to a temp directory,
	 * then reads patch.json from the extracted contents.
	 */
	json_t* getFileJsonFromArchive(const std::string& archivePath) const {
		// Check if we have a valid cache entry and the source file hasn't changed
		auto it = cache_.find(archivePath);
		if (it != cache_.end()) {
			auto fileTime = ghc::filesystem::last_write_time(archivePath);
			if (fileTime == it->second.lastWriteTime) {
				std::string patchJsonPath = system::join(it->second.cachePath, "patch.json");
				if (system::exists(patchJsonPath)) {
					FILE* f = fopen(patchJsonPath.c_str(), "rb");
					if (f) {
						json_error_t error;
						json_t* rootJ = json_loadf(f, 0, &error);
						fclose(f);
						return rootJ;
					}
				}
			}
			else {
				// File has changed, invalidate cache entry
				system::removeRecursively(it->second.cachePath);
				cache_.erase(it);
			}
		}

		// Extract to cache
		std::string extractDir = extractToCache(archivePath);
		if (extractDir.empty()) return nullptr;

		// Read the patch.json file from the extracted directory
		std::string patchJsonPath = system::join(extractDir, "patch.json");
		FILE* f = fopen(patchJsonPath.c_str(), "rb");
		if (!f) return nullptr;
		json_error_t error;
		json_t* rootJ = json_loadf(f, 0, &error);
		fclose(f);

		// Store in cache
		auto fileTime = ghc::filesystem::last_write_time(archivePath);
		cache_[archivePath] = { extractDir, fileTime };

		return rootJ;
	}

	json_t* toJson() const override {
		json_t* j = json_object();
		json_object_set_new(j, "slug", json_string(slug.c_str()));
		json_object_set_new(j, "rootContainer", json_string(rootContainer.c_str()));
		return j;
	}

	bool fromJson(json_t* sourceJ) override {
		json_t* slugJ = json_object_get(sourceJ, "slug");
		slug = std::string(json_string_value(slugJ));

		json_t* rootJ = json_object_get(sourceJ, "rootContainer");
		if (rootJ) rootContainer = currentContainer = json_string_value(rootJ);

		return true;
	}

	void appendMenuItems(ui::Menu* menu) override {
		menu->addChild(createMenuLabel(getRootContainer().empty() ? "(no folder selected)" : string::f("Root %s", getRootContainer().c_str())));
		menu->addChild(createMenuItem("Select root folder...", "", [=]() {
			std::string path = selectFolder();
			if (path.empty()) return;
			rootContainer = path;
			setContainer(path);
		}));
		if (!rootContainer.empty()) {
			menu->addChild(createMenuItem("Open in file explorer", "", [=]() {
				system::openDirectory(rootContainer);
			}));
		}
	}
};


namespace vcvs {
	inline extern std::string getSlug() {
		return FileSystemSource::SLUG_VCVS;
	}

	/**
	 * Creates a new empty file system data source.
	 */
	inline SelectionSource* initSource() {
		FileSystemSource* src = new FileSystemSource;
		src->slug = FileSystemSource::SLUG_VCVS;
		return src;
	}

	/**
	 * Interactively create a new source of the same type via a UI dialog.
	 * Returns the new source, or nullptr if the user cancelled.
	 * The caller takes ownership.
	 */
	inline SelectionSource* createSource() {
		std::string path = FileSystemSource::selectFolder();
		if (path.empty()) return nullptr;
		FileSystemSource* src = new FileSystemSource;
		src->slug = FileSystemSource::SLUG_VCVS;
		src->rootContainer = path;
		src->setContainer(path);
		return src;
	}
} // namespace vcvs

namespace vcv {
	inline extern std::string getSlug() {
		return FileSystemSource::SLUG_VCV;
	}

	/**
	 * Creates a new empty file system data source.
	 */
	inline SelectionSource* initSource() {
		FileSystemSource* src = new FileSystemSource;
		src->slug = FileSystemSource::SLUG_VCV;
		return src;
	}

	/**
	 * Interactively create a new source of the same type via a UI dialog.
	 * Returns the new source, or nullptr if the user cancelled.
	 * The caller takes ownership.
	 */
	inline SelectionSource* createSource() {
		std::string path = FileSystemSource::selectFolder();
		if (path.empty()) return nullptr;
		FileSystemSource* src = new FileSystemSource;
		src->slug = FileSystemSource::SLUG_VCV;
		src->rootContainer = path;
		src->setContainer(path);
		return src;
	}
} // namespace vcv


} // namespace filesystem
} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne