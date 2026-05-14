#pragma once
#include <rack.hpp>
#include <osdialog.h>
#include <tag.hpp>
#include <ghc/filesystem.hpp>
#include "Mb.hpp"
#include "Mb_selection_source.hpp"
#include "Mb_selection_source_index.hpp"
#include "Mb_selection_helper.hpp"

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
	std::map<std::string, FileIndexEntry> entries;
	bool readOnly = false;

	json_t* toJson() const {
		json_t* j = json_object();
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
		return j;
	}

	bool fromJson(json_t* indexJ) {
		if (!indexJ || !json_is_object(indexJ)) return false;

		entries.clear();
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
		return true;
	}

	const std::string getDescription(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() ? it->second.description : "";
	}
	void setDescription(const std::string& fileId, const std::string& description) override {
		if (!readOnly) entries[fileId].description = description;
	}

	bool hasTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries[fileId].tags;
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}
	std::vector<std::string> getTags(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() ? it->second.tags : std::vector<std::string>();
	}
	void addTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly) {
			auto& tags = entries[fileId].tags;
			if (std::find(tags.begin(), tags.end(), tag) == tags.end())
				tags.push_back(tag);
		}
	}
	void removeTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly) {
			auto& tags = entries[fileId].tags;
			tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
		}
	}

	bool hasCustomTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries[fileId].customTags;
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}
	std::vector<std::string> getCustomTags(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() ? it->second.customTags : std::vector<std::string>();
	}
	void addCustomTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly) {
			auto& tags = entries[fileId].customTags;
			if (std::find(tags.begin(), tags.end(), tag) == tags.end())
				tags.push_back(tag);
		}
	}
	void removeCustomTag(const std::string& fileId, const std::string& tag) override {
		if (!readOnly) {
			auto& tags = entries[fileId].customTags;
			tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
		}
	}

	bool isFavorite(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() && it->second.favorite;
	}
	void setFavorite(const std::string& fileId, bool favorite) override {
		if (!readOnly) {
			entries[fileId].favorite = favorite;
		}
	}

	bool isReadOnly() const override { return readOnly; }

	std::set<std::string> getTagsAll() const override {
		std::set<std::string> items;
		for (int id = 0; id < (int)tag::tagAliases.size(); id++) {
			items.insert(rack::tag::tagAliases[id][0]);
		}
		return items;
	}

	std::set<std::string> getCustomTagsAll() const override {
		// Use the default custom tags
		return customTagsAll();
	}

	/**
	 * Update the index by scanning the filesystem and syncing with stored entries.
	 * - New files are added to the index.
	 * - Files that no longer exist are removed.
	 * - Moved files are detected by filename matching; metadata is preserved but fileId is updated.
	 */
	void updateIndex(const std::string& rootContainer, const std::string& ext) {
		if (readOnly) return;

		// Step 1: Collect all files currently on disk (relative paths)
		std::set<std::string> currentFiles;
		collectFilesRecursive(rootContainer, "", currentFiles, ext);

		// Step 2: Build a map of filename -> fileId for moved file detection
		// (filename without path, so we can detect relocations)
		std::map<std::string, std::string> filenameToFileId;
		for (const std::string& fileId : currentFiles) {
			std::string filename = system::getFilename(fileId);
			filenameToFileId[filename] = fileId;
		}

		// Step 3: Update existing index entries — handle moved files and removed files
		std::set<std::string> existingFileIds;
		for (const auto& pair : entries) {
			existingFileIds.insert(pair.first);
		}

		for (const std::string& existingId : existingFileIds) {
			if (currentFiles.find(existingId) != currentFiles.end()) {
				// File still exists at the same path — no action needed
				continue;
			}

			// File is gone from its known location — check if it was moved
			std::string existingFilename = system::getFilename(existingId);
			auto it = filenameToFileId.find(existingFilename);
			if (it != filenameToFileId.end()) {
				// File was moved — transfer metadata to the new fileId
				FileIndexEntry entry = entries[existingId];
				entries.erase(existingId);
				entries[it->second] = entry;
				currentFiles.erase(it->second);
			} else {
				// File no longer exists at all — remove from index
				entries.erase(existingId);
			}
		}

		// Step 4: Add brand-new files not yet in the index
		for (const std::string& fileId : currentFiles) {
			if (entries.find(fileId) == entries.end()) {
				entries[fileId] = FileIndexEntry();
			}
		}
	}

private:
	/** Recursively collect file paths with the given extension under `dir`.
	 * @param baseDir  Absolute path of the directory to scan
	 * @param prefix   Relative path from rootContainer to baseDir (without trailing slash)
	 * @param files    Output set of relative file paths from rootContainer
	 */
	void collectFilesRecursive(const std::string& baseDir, const std::string& prefix, std::set<std::string>& files, const std::string& ext) const {
		auto entries = system::getEntries(baseDir);
		for (const auto& entry : entries) {
			if (system::isDirectory(entry)) {
				std::string subdirName = system::getFilename(entry);
				std::string subdirPrefix = prefix.empty() ? subdirName : prefix + "/" + subdirName;
				collectFilesRecursive(entry, subdirPrefix, files, ext);
			} else if (system::isFile(entry) && SelectionSource::endsWith(entry, ext)) {
				std::string filename = system::getFilename(entry);
				std::string relative = prefix.empty() ? filename : prefix + "/" + filename;
				files.insert(relative);
			}
		}
	}
};

struct FileSystemSource : SelectionSource {
	std::string rootContainer;
	std::string currentContainer;
	SelectionBrowserHelper* helper;
	mutable std::string status = "";

	/** Shared archive cache - stores extraction paths for .vcv archives. 
	 * Shared via global cache to allow reuse across sources with same rootContainer.
	 * Key is archive path, value is extraction path + file modification time.
	 */
	struct ArchiveCacheEntry {
		std::string cachePath;
		std::chrono::system_clock::time_point lastWriteTime;
	};
	std::shared_ptr<std::map<std::string, ArchiveCacheEntry>> archiveCache;
	std::shared_ptr<FileSystemSourceIndex> index;

	SelectionSourceIndex* getIndex() const override {
		return index.get();
	}

	void onAttach() override {
		// If no root is set, default to the user's selections directory
		if (rootContainer.empty()) {
			rootContainer = currentContainer = asset::user("selections");
		}

		// Register our index in the global cache for sharing with other sources
		// that have the same rootContainer
		std::string cacheKey = slug + ":index:" + rootContainer;
		auto existingIndex = helper->getGlobalCache<FileSystemSourceIndex>(cacheKey);
		if (existingIndex) {
			// Another source already created an index for this root - reuse it
			index = existingIndex;
		} 
		else {
			// Create and store our index
			index = std::make_shared<FileSystemSourceIndex>();
			helper->setGlobalCache(cacheKey, index, nullptr);
		}

		// Register our archive cache in the global cache for sharing
		std::string archiveCacheKey = slug + ":archiveCache:" + rootContainer;
		auto existingArchiveCache = helper->getGlobalCache<std::map<std::string, ArchiveCacheEntry>>(archiveCacheKey);
		if (existingArchiveCache) {
			// Another source already created an archive cache for this root - reuse it
			archiveCache = existingArchiveCache;
		} 
		else {
			// Create and store our archive cache
			archiveCache = std::make_shared<std::map<std::string, ArchiveCacheEntry>>();
			helper->setGlobalCache(archiveCacheKey, archiveCache, nullptr);
		}

		loadIndex();
	}

	void onDetach() override {
		saveIndex();
	}


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
			status = "2:Failed to extract archive";
			return "";
		}
		return extractDir;
	}

	/** Get the cache directory from the helper. */
	const std::string& getCacheDir() const {
		return helper->cacheDir;
	}

	/** Clear all cached extractions. */
	void clearCache() const {
		archiveCache->clear();
	}

	/** Invalidate a single cache entry if the file has changed. */
	void invalidateCacheEntry(const std::string& fileId) const {
		auto it = archiveCache->find(fileId);
		if (it != archiveCache->end()) {
			std::string fullPath = rootContainer + "/" + fileId;
			auto fileTime = ghc::filesystem::last_write_time(fullPath);
			if (fileTime != it->second.lastWriteTime) {
				system::removeRecursively(it->second.cachePath);
				archiveCache->erase(it);
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

	void setHelper(SelectionBrowserHelper* h) override {
		helper = h;
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
		std::string folder = container == rootContainer ? rootContainer : string::f("%s/%s", rootContainer, container);
		auto entries = system::getEntries(folder);
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
		if (rootContainer.empty() || !index) return;
		std::string ext = slug == SLUG_VCVS ? "vcvs" : "vcv";
		std::string indexPath = rootContainer + "/mb-index." + ext + ".json";
		FILE* f = fopen(indexPath.c_str(), "rb");
		if (!f) return;
		json_error_t error;
		json_t* indexJ = json_loadf(f, 0, &error);
		fclose(f);
		if (!indexJ) return;
		index->fromJson(indexJ);
		json_decref(indexJ);
	}

	/** Save index to mb-index.json in the root container. */
	void saveIndex() const {
		if (rootContainer.empty() || !index) return;
		std::string ext = slug == SLUG_VCVS ? "vcvs" : "vcv";
		std::string indexPath = rootContainer + "/mb-index." + ext + ".json";
		json_t* indexJ = index->toJson();
		FILE* f = fopen(indexPath.c_str(), "wb");
		if (!f) {
			json_decref(indexJ);
			return;
		}
		json_dumpf(indexJ, f, JSON_INDENT(2));
		fclose(f);
		json_decref(indexJ);
	}

	const std::string getSourceType() const override {
		return slug;
	}

	bool isPatchSource() const override {
		return slug == SLUG_VCV;
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
		// Check if we have a valid cache entry and the source file hasn't changed();
		auto it = archiveCache->find(archivePath);
		if (it != archiveCache->end()) {
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
				archiveCache->erase(it);
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
		(*archiveCache)[archivePath] = { extractDir, fileTime };

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

	const std::string& getStatusText() override {
		return status;
	}

	void appendSourceMenuItems(ui::Menu* menu) override {
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
			menu->addChild(createMenuItem("Update index", "", [this]() {
				std::string ext = slug == SLUG_VCVS ? ".vcvs" : ".vcv";
				index->updateIndex(rootContainer, ext);
				saveIndex();
			}));
		}
	}

	void appendPreviewMenuItems(ui::Menu* menu, std::string fileId) override {
		menu->addChild(createMenuItem("Open containing folder", "", [this, fileId]() {
			std::string path = getAbsoluteFilePath(fileId);
			std::string dir = system::getDirectory(path);
			system::openDirectory(dir);
		}));
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