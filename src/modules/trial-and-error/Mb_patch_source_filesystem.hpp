#pragma once
#include <rack.hpp>
#include <tag.hpp>
#include <ghc/filesystem.hpp>
#include "Mb.hpp"
#include "Mb_patch_source.hpp"
#include "Mb_patch_sourceindex.hpp"
#include "Mb_patch_helper.hpp"
#include "../../vcv/api.hpp"
#include "../../vcv/files.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace patch {
namespace filesystem {


/**
 * Index for a FileSystemSource, stored as mb-index.json
 * in the root of the source folder.
 * This is a read-only index; set methods have no effect.
 */
struct FileSystemPatchSourceIndex : PatchSourceIndex {
	struct FileIndexEntry {
		std::string description;
		std::vector<std::string> tags;
		std::vector<std::string> customTags;
		bool favorite = false;
	};
	std::map<std::string, FileIndexEntry> entries;

	/** Cached fuzzy search database for file search - rebuilt when index changes. */
	mutable fuzzysearch::Database<std::string> searchDb;
	mutable bool searchDbValid = false;

	/** Rebuild the search database from current index entries.
	 * Note: fileId is already a relative path, so we extract filename manually. */
	void rebuildSearchDb() const {
		searchDb = fuzzysearch::Database<std::string>();
		searchDb.setWeights({1.0f, 0.9f}); // filename weighted higher than description
		for (const auto& pair : entries) {
			const std::string& fileId = pair.first;
			const auto& entry = pair.second;
			// Extract filename from relative path without filesystem access
			std::string filename = fileId.substr(fileId.find_last_of('/') + 1);
			std::vector<std::string> fields = {
				filename,
				entry.description
			};
			searchDb.addEntry(fileId, fields);
		}
		searchDbValid = true;
	}

	/** Search the cached database, returning file IDs sorted by relevance. */
	std::vector<std::string> search(const std::string& query) const {
		if (!searchDbValid) {
			rebuildSearchDb();
		}
		auto results = searchDb.search(query);
		std::vector<std::string> fileIds;
		for (const auto& r : results) {
			fileIds.push_back(r.key);
		}
		return fileIds;
	}

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
		// Rebuild search DB lazily on first search
		searchDbValid = false;
		return true;
	}

	const std::string getDescription(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() ? it->second.description : "";
	}
	void setDescription(const std::string& fileId, const std::string& description) override {
		searchDbValid = false;
		entries[fileId].description = description;
	}

	bool hasTag(const std::string& fileId, const std::string& tag) const override {
		auto it = entries.find(fileId);
		if (it == entries.end()) return false;
		auto& tags = it->second.tags;
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}
	std::vector<std::string> getTags(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() ? it->second.tags : std::vector<std::string>();
	}
	void addTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries[fileId].tags;
		if (std::find(tags.begin(), tags.end(), tag) == tags.end())
			tags.push_back(tag);
	}
	void removeTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries[fileId].tags;
		tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
	}

	bool hasCustomTag(const std::string& fileId, const std::string& tag) const override {
		auto it = entries.find(fileId);
		if (it == entries.end()) return false;
		auto& tags = it->second.customTags;
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}
	std::vector<std::string> getCustomTags(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() ? it->second.customTags : std::vector<std::string>();
	}
	void addCustomTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries[fileId].customTags;
		if (std::find(tags.begin(), tags.end(), tag) == tags.end())
			tags.push_back(tag);
	}
	void removeCustomTag(const std::string& fileId, const std::string& tag) override {
		auto& tags = entries[fileId].customTags;
		tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
	}

	bool isFavorite(const std::string& fileId) const override {
		auto it = entries.find(fileId);
		return it != entries.end() && it->second.favorite;
	}
	void setFavorite(const std::string& fileId, bool favorite) override {
		entries[fileId].favorite = favorite;
	}

	bool isReadOnly() const override { return false; }

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
		// Step 1: Collect all files currently on disk (relative paths)
		std::set<std::string> currentFiles;
		collectFilesRecursive(rootContainer, "", currentFiles, ext);

		// Step 2: Build a map of filename -> fileId for moved file detection
		// (filename without path, so we can detect relocations)
		std::map<std::string, std::string> filenameToFileId;
		for (const std::string& fileId : currentFiles) {
			std::string filename = vcv::fs::getFilename(fileId);
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
			std::string existingFilename = vcv::fs::getFilename(existingId);
			auto it = filenameToFileId.find(existingFilename);
			if (it != filenameToFileId.end()) {
				// File was moved — transfer metadata to the new fileId
				FileIndexEntry entry = entries[existingId];
				entries.erase(existingId);
				entries[it->second] = entry;
				currentFiles.erase(it->second);
			} 
			else {
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
		rebuildSearchDb();
	}

	/** Recursively collect file paths with the given extension under baseDir.
	 * @param baseDir  Absolute path of the directory to scan
	 * @param prefix   Root-relative path to baseDir, starting with "/" (empty string for the root itself)
	 * @param files    Output set of root-relative file paths, each starting with "/"
	 */
	void collectFilesRecursive(const std::string& baseDir, const std::string& prefix, std::set<std::string>& files, const std::string& ext) const {
		auto entries = vcv::fs::getEntries(baseDir);
		for (const auto& entry : entries) {
			if (vcv::fs::isDirectory(entry)) {
				std::string subdirPrefix = prefix + "/" + vcv::fs::getFilename(entry);
				collectFilesRecursive(entry, subdirPrefix, files, ext);
			}
			else if (vcv::fs::isFile(entry) && PatchSource::endsWith(entry, ext)) {
				files.insert(prefix + "/" + vcv::fs::getFilename(entry));
			}
		}
	}
};

struct FileSystemSource : PatchSource {
	std::string rootContainer;
	std::string currentContainer;
	static constexpr const char* SLUG_VCVS = "filesystem:vcvs";
	static constexpr const char* SLUG_VCV = "filesystem:vcv";
	std::string slug;

	/** Shared archive cache - stores extraction paths for .vcv archives. 
	 * Shared via global cache to allow reuse across sources with same rootContainer.
	 * Key is archive path, value is extraction path + file modification time.
	 */
	struct ArchiveCacheEntry {
		std::string cachePath;
		std::chrono::system_clock::time_point lastWriteTime;
	};
	std::shared_ptr<std::map<std::string, ArchiveCacheEntry>> archiveCache;
	std::shared_ptr<FileSystemPatchSourceIndex> index;

	PatchSourceIndex* getIndex() const override {
		return index.get();
	}

	void onAttach() override {
		// If no root is set, default to the user's selections directory
		if (rootContainer.empty()) {
			rootContainer = asset::user("selections");
			currentContainer = "/";
		}

		// Register our index in the global cache for sharing with other sources
		// that have the same rootContainer
		std::string cacheKey = slug + ":index:" + rootContainer;
		auto existingIndex = helper->getGlobalCache<FileSystemPatchSourceIndex>(cacheKey);
		if (existingIndex) {
			// Another source already created an index for this root - reuse it
			index = existingIndex;
		} 
		else {
			// Create and store our index
			index = std::make_shared<FileSystemPatchSourceIndex>();
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

	/**
	 * Extract a .vcv archive to a cached folder.
	 * Returns the path to the cached extraction, or empty on failure.
	 */
	std::string extractToCache(const std::string& archivePath) const {
		std::string cacheName = generateCacheName();
		std::string extractDir = vcv::fs::join(getCacheDir(), cacheName);
		vcv::fs::createDirectories(extractDir);

		try {
			system::unarchiveToDirectory(archivePath, extractDir);
		}
		catch (...) {
			vcv::fs::removeRecursively(extractDir);
			setStatus("Failed to extract archive", 3);
			return "";
		}
		return extractDir;
	}

	/** Clear all cached extractions. */
	void clearCache() const {
		archiveCache->clear();
	}

	/** Invalidate a single cache entry if the file has changed. */
	void invalidateCacheEntry(const std::string& fileId) const {
		std::string fullPath = resolve(fileId);
		auto it = archiveCache->find(fullPath);
		if (it != archiveCache->end()) {
			auto fileTime = ghc::filesystem::last_write_time(fullPath);
			if (fileTime != it->second.lastWriteTime) {
				vcv::fs::removeRecursively(it->second.cachePath);
				archiveCache->erase(it);
			}
		}
	}



	/** Show a folder picker dialog, returning the chosen path or empty on cancel. */
	static std::string selectFolder() {
		std::string dir = asset::user("selections");
		(void)dir;
		return vcv::ui::openDirectoryDialog();
	}


	const std::string getContainer() const override { 
		return currentContainer;
	}
	void setContainer(const std::string& path) override { 
		currentContainer = path;
	}

	const std::string getRootContainer() const override { 
		return "/";
	}

	const std::vector<ContainerEntry>& getContainers(const std::string& container) override {
		static std::vector<ContainerEntry> containers;
		containers.clear();

		setStatus("Loading folders...");
		// Resolve container path to file system path (container is absolute-style like "/subfolder" or "/")
		std::string folder = resolve(container);
		auto entries = vcv::fs::getEntries(folder);
		for (const std::string& entry : entries) {
			if (vcv::fs::isDirectory(entry)) {
				// Build relative path starting with "/"
				std::string name = vcv::fs::getFilename(entry);
				std::string relPath = container == "/" ? "/" + name : container + "/" + name;
				containers.push_back({ relPath, name });
			}
		}
		std::sort(containers.begin(), containers.end(), [](const ContainerEntry& a, const ContainerEntry& b) {
			return string::lowercase(a.displayName) < string::lowercase(b.displayName);
		});

		setStatus();
		return containers;
	}

	const std::vector<ContainerEntry>& getFiles(const std::string& container) override {
		static std::vector<ContainerEntry> files;
		files.clear();

		setStatus("Loading files...");
		// Resolve container path to file system path (container is absolute-style like "/subfolder" or "/")
		std::string folder = resolve(container);
		auto entries = vcv::fs::getEntries(folder);
		for (const std::string& entry : entries) {
			// In vcvs mode, include both .vcvs and .vcvss files (vcvss is converted to vcvs)
			if (slug == SLUG_VCVS) {
				if (vcv::fs::isFile(entry) && (PatchSource::endsWith(entry, ".vcvs") || PatchSource::endsWith(entry, ".vcvss"))) {
					// Build relative path starting with "/"
					std::string name = vcv::fs::getFilename(entry);
					std::string relPath = container == "/" ? "/" + name : container + "/" + name;
					files.push_back({ relPath, name });
				}
			}
			else {
				// vcv mode: only .vcv files
				if (vcv::fs::isFile(entry) && PatchSource::endsWith(entry, ".vcv")) {
					// Build relative path starting with "/"
					std::string name = vcv::fs::getFilename(entry);
					std::string relPath = container == "/" ? "/" + name : container + "/" + name;
					files.push_back({ relPath, name });
				}
			}
		}
		std::sort(files.begin(), files.end(), [](const ContainerEntry& a, const ContainerEntry& b) {
			return string::lowercase(a.displayName) < string::lowercase(b.displayName);
		});

		setStatus();
		return files;
	}

	/**
	 * Search files by fuzzy matching against displayName and description.
	 * Uses cached FuzzySearchDatabase for consistent search quality with module browser.
	 * Returns all matching entries sorted by relevance score.
	 */
	std::vector<ContainerEntry> search(const std::string& query) override {
		setStatus(string::f("Searching: %s...", query.c_str()));
		std::vector<std::string> fileIds = index->search(query);

		std::vector<ContainerEntry> result;
		for (const std::string& fileId : fileIds) {
			// Extract filename from relative path without filesystem access
			std::string filename = fileId.substr(fileId.find_last_of('/') + 1);
			result.push_back({fileId, filename});
		}

		setStatus();
		return result;
	}

	bool isContainer(const std::string& path) const override {
		return vcv::fs::isDirectory(resolve(path));
	}

	bool isFile(const std::string& path) const override {
		return vcv::fs::isFile(resolve(path));
	}

	const std::string getParentContainer(const std::string& path) const override {
		// path is relative starting with "/" like "/subfolder" - find parent
		if (path.empty() || path == "/") return "/";
		std::string::size_type lastSlash = path.find_last_of('/');
		if (lastSlash == 0) return "/";
		if (lastSlash == std::string::npos) return "/";
		return path.substr(0, lastSlash);
	}

	const std::string getFilename(const std::string& path) const override {
		return vcv::fs::getFilename(resolve(path));
	}

	const std::string getTempFilePath(const std::string& fileId) const override {
		return resolve(fileId);
	}

	/** Convert an absolute-style path (like "/subfolder") to file system path using rootContainer. */
	const std::string resolve(const std::string& path) const {
		if (path.empty() || path == "/") return rootContainer;
		// path is like "/subfolder" - strip leading "/" and join with rootContainer
		std::string relPath = path.substr(1);
		return rootContainer + "/" + relPath;
	}

	/** Load index from mb-index.json in the root container, if it exists. */
	void loadIndex() {
		if (rootContainer.empty() || !index) return;
		std::string ext = slug == SLUG_VCVS ? "vcvs" : "vcv";
		std::string indexPath = rootContainer + "/mb-index." + ext + ".json";
		std::string data;
		if (!vcv::fs::read(indexPath, data)) return;
		std::string err;
		json_t* indexJ = vcv::parseJson(data, err);
		if (!indexJ) {
			setStatus("Failed to load index file", 3);
			return;
		}
		index->fromJson(indexJ);
		json_decref(indexJ);
	}

	/** Save index to mb-index.json in the root container. */
	void saveIndex() const {
		if (rootContainer.empty() || !index) return;
		std::string ext = slug == SLUG_VCVS ? "vcvs" : "vcv";
		std::string indexPath = rootContainer + "/mb-index." + ext + ".json";
		json_t* indexJ = index->toJson();
		char* dumped = json_dumps(indexJ, JSON_INDENT(2));
		json_decref(indexJ);
		if (!dumped) {
			setStatus("Failed to save the index file", 3);
			return;
		}
		std::string data(dumped);
		free(dumped);
		if (!vcv::fs::write(indexPath, data)) {
			setStatus("Failed to save the index file", 3);
			return;
		}
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
		std::string fullPath = resolve(fileId);

		// Read the whole file through the swappable fs layer, then parse.
		auto readJsonFile = [](const std::string& path) -> json_t* {
			std::string data;
			if (!vcv::fs::read(path, data)) return nullptr;
			std::string err;
			return vcv::parseJson(data, err);
		};

		if (slug == SLUG_VCV) {
			// .vcv files: legacy (plain JSON) or v2+ (zstd-compressed tar)
			if (isVcvLegacyV1(fullPath)) {
				// Legacy v1 format: plain JSON file
				return readJsonFile(fullPath);
			}
			else {
				// v2+ format: zstd-compressed tar archive
				return getFileJsonFromArchive(fullPath);
			}
		}
		else {
			// .vcvs mode: also supports .vcvss (STRIP) files which are converted to vcvs format
			if (PatchSource::endsWith(fullPath, ".vcvss")) {
				// Load vcvss file and convert to vcvs format
				json_t* vcvssJ = readJsonFile(fullPath);
				if (!vcvssJ) return nullptr;

				// Convert to vcvs format; widths come from the production lookup
				json_t* vcvsJ = vcv::convertVcvssToVcvs(vcvssJ, vcv::productionWidthLookup());
				json_decref(vcvssJ);
				return vcvsJ;
			}

			// .vcvs files are plain JSON
			return readJsonFile(fullPath);
		}
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
				std::string patchJsonPath = vcv::fs::join(it->second.cachePath, "patch.json");
				if (vcv::fs::exists(patchJsonPath)) {
					std::string data;
					if (vcv::fs::read(patchJsonPath, data)) {
						std::string err;
						if (json_t* rootJ = vcv::parseJson(data, err))
							return rootJ;
					}
				}
			}
			else {
				// File has changed, invalidate cache entry
				vcv::fs::removeRecursively(it->second.cachePath);
				archiveCache->erase(it);
			}
		}

		// Extract to cache
		setStatus("Extracting archive...");
		std::string extractDir = extractToCache(archivePath);
		if (extractDir.empty()) return nullptr;

		// Read the patch.json file from the extracted directory
		setStatus("Loading patch...");
		std::string patchJsonPath = vcv::fs::join(extractDir, "patch.json");
		std::string data;
		if (!vcv::fs::read(patchJsonPath, data)) {
			vcv::fs::removeRecursively(extractDir);
			setStatus("Failed to read patch file", 3);
			return nullptr;
		}
		std::string err;
		json_t* rootJ = vcv::parseJson(data, err);

		if (!rootJ) {
			vcv::fs::removeRecursively(extractDir);
			setStatus("Failed to parse patch JSON", 3);
			return nullptr;
		}

		// Store in cache
		auto fileTime = ghc::filesystem::last_write_time(archivePath);
		(*archiveCache)[archivePath] = { extractDir, fileTime };

		setStatus();
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
		if (rootJ) {
			rootContainer = json_string_value(rootJ);
			currentContainer = "/";
		}

		return true;
	}

	void appendSourceMenuItems(ui::Menu* menu) override {
		menu->addChild(createMenuLabel(rootContainer.empty() ? "(no folder selected)" : string::f("Root %s", rootContainer.c_str())));
		menu->addChild(createMenuItem("Select root folder...", "", [=]() {
			std::string path = selectFolder();
			if (path.empty()) return;
			rootContainer = path;
			// Get or create a fresh index for the new root to avoid overwriting its existing index file
			std::string cacheKey = slug + ":index:" + rootContainer;
			auto existingIndex = helper->getGlobalCache<FileSystemPatchSourceIndex>(cacheKey);
			if (existingIndex) {
				index = existingIndex;
			} 
			else {
				index = std::make_shared<FileSystemPatchSourceIndex>();
				helper->setGlobalCache(cacheKey, index, nullptr);
				loadIndex();
			}
			// Also swap in the archive cache for the new root
			std::string archiveCacheKey = slug + ":archiveCache:" + rootContainer;
			auto existingArchiveCache = helper->getGlobalCache<std::map<std::string, ArchiveCacheEntry>>(archiveCacheKey);
			if (existingArchiveCache) {
				archiveCache = existingArchiveCache;
			} 
			else {
				archiveCache = std::make_shared<std::map<std::string, ArchiveCacheEntry>>();
				helper->setGlobalCache(archiveCacheKey, archiveCache, nullptr);
			}
			setContainer("/");
		}));
		if (!rootContainer.empty()) {
			menu->addChild(createMenuItem("Open in file explorer", "", [=]() {
				vcv::fs::openDirectory(rootContainer);
			}));
			menu->addChild(createMenuItem("Update index", "", [this]() {
				std::string ext = slug == SLUG_VCVS ? ".vcvs" : ".vcv";
				index->updateIndex(rootContainer, ext);
				saveIndex();
			}));
		}
	}

	void appendPreviewMenuItems(ui::Menu* menu, std::string fileId) override {
		menu->addChild(createMenuLabel(getTempFilePath(fileId)));
		menu->addChild(createMenuItem("Open containing folder", "", [this, fileId]() {
			std::string path = getTempFilePath(fileId);
			std::string dir = vcv::fs::getDirectory(path);
			vcv::fs::openDirectory(dir);
		}));

		// Only show "Convert to .vcvs" for .vcvss files
		if (PatchSource::endsWith(fileId, ".vcvss")) {
			menu->addChild(createMenuItem("Convert to .vcvs", "", [this, fileId]() {
				convertVcvssFile(fileId);
			}));
		}
	}

	/** Convert a .vcvss (STRIP) file to .vcvs via the save dialog. Split out so tests
	 * can drive the conversion logic with scripted dialogs and an in-memory fs. */
	void convertVcvssFile(const std::string& fileId) {
		std::string fullPath = resolve(fileId);
		std::string dir = vcv::fs::getDirectory(fullPath);

		std::string newPath = vcv::ui::saveDialog(vcv::SELECTION_FILTERS, dir, "");
		if (newPath.empty()) return;

		setStatus("Converting to .vcvs...");
		// Load vcvss file
		std::string data;
		if (!vcv::fs::read(fullPath, data)) {
			setStatus("Failed to open file", 3);
			return;
		}
		std::string err;
		json_t* vcvssJ = vcv::parseJson(data, err);
		if (!vcvssJ) {
			setStatus("Failed to parse file", 3);
			return;
		}

		// Convert to vcvs format; widths come from the production lookup
		json_t* vcvsJ = vcv::convertVcvssToVcvs(vcvssJ, vcv::productionWidthLookup());
		json_decref(vcvssJ);
		if (!vcvsJ) {
			setStatus("Failed to convert", 3);
			return;
		}

		// Save to new file
		char* dumped = json_dumps(vcvsJ, JSON_INDENT(2));
		json_decref(vcvsJ);
		if (!dumped) {
			setStatus("Failed to convert", 3);
			return;
		}
		std::string out(dumped);
		free(dumped);
		if (!vcv::fs::write(newPath, out)) {
			setStatus("Failed to create file", 3);
			return;
		}
		setStatus("Converted successfully");
	}
};


namespace vcvs {

inline extern std::string getSlug() {
	return FileSystemSource::SLUG_VCVS;
}

/**
 * Creates a new empty file system data source.
 */
inline PatchSource* initSource() {
	FileSystemSource* src = new FileSystemSource;
	src->slug = FileSystemSource::SLUG_VCVS;
	return src;
}

/**
 * Interactively create a new source of the same type via a UI dialog.
 * Returns the new source, or nullptr if the user cancelled.
 * The caller takes ownership.
 */
inline PatchSource* createSource() {
	std::string path = FileSystemSource::selectFolder();
	if (path.empty()) return nullptr;
	FileSystemSource* src = new FileSystemSource;
	src->slug = FileSystemSource::SLUG_VCVS;
	src->rootContainer = path;
	src->setContainer("/");
	return src;
}

} // namespace vcvs

namespace vcvpatch {

inline extern std::string getSlug() {
	return FileSystemSource::SLUG_VCV;
}

/**
 * Creates a new empty file system data source.
 */
inline PatchSource* initSource() {
	FileSystemSource* src = new FileSystemSource;
	src->slug = FileSystemSource::SLUG_VCV;
	return src;
}

/**
 * Interactively create a new source of the same type via a UI dialog.
 * Returns the new source, or nullptr if the user cancelled.
 * The caller takes ownership.
 */
inline PatchSource* createSource() {
	std::string path = FileSystemSource::selectFolder();
	if (path.empty()) return nullptr;
	FileSystemSource* src = new FileSystemSource;
	src->slug = FileSystemSource::SLUG_VCV;
	src->rootContainer = path;
	src->setContainer("/");
	return src;
}

} // namespace vcvpatch


} // namespace filesystem
} // namespace patch
} // namespace Mb
} // namespace StoermelderPackOne