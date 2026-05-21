#pragma once
#include <string>
#include <vector>
#include <rack.hpp>
#include "Mb_patch_helper.hpp"
#include "Mb_patch_sourceindex.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace patch {

/**
 * A container or file entry returned by getContainers() / getFiles(),
 * pairing a unique id with a human-readable display name.
 */
struct ContainerEntry {
	std::string id;
	std::string displayName;
};

/**
 * Abstract interface for a patch data source.
 * Decouples the Browser from the file system, enabling
 * alternative data sources (e.g. remote HTTP) to be plugged in later.
 */
struct PatchSource {
	virtual ~PatchSource() = default;

	PatchHelperWidget* helper = nullptr;
	mutable std::string status;

	// -- lifetime -----------------------------------------------------------

	/** Called when the source is attached to a sidebar. */
	virtual void onAttach() {}
	/** Called when the source is detached from a sidebar. */
	virtual void onDetach() {}

	/** Set the cache directory used for temporary downloads/extractions. */
	virtual void setCacheDir(const std::string& dir) {}
	/** Set the helper instance for cache access. */
	void setHelper(PatchHelperWidget* h) { helper = h; }

	// -- status --------------------------------------------------------------

	/** Set status message with duration prefix — 0 for indefinite, N for N seconds. */
	void setStatus(const std::string& msg = "", int duration = 0) const {
		status = string::f("%d:%s", duration, msg.c_str());
	}

	const std::string& getStatusText() const { return status; }

	// -- cache ---------------------------------------------------------------

	const std::string& getCacheDir() const { return helper->cacheDir; }

	std::string generateCacheName() const {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		static std::uniform_int_distribution<> dis(0, 15);
		std::string name = "mb_cache_";
		for (int i = 0; i < 16; ++i) {
			int v = dis(gen);
			name += (v < 10) ? char('0' + v) : char('a' + v - 10);
		}
		return name;
	}

	/** Returns true if `path` is a legacy v1 .vcv patch (plain JSON, not zstd-compressed). */
	static bool isVcvLegacyV1(const std::string& path) {
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f) return false;
		DEFER({std::fclose(f);});
		char zstdMagic[] = "\x28\xb5\x2f\xfd";
		char buf[4] = {};
		std::fread(buf, 1, sizeof(buf), f);
		return std::memcmp(buf, zstdMagic, sizeof(buf)) != 0;
	}

	// -- navigation ----------------------------------------------------------

	/** Return the current container path. */
	virtual const std::string getContainer() const = 0;
	/** Set the current container path. */
	virtual void setContainer(const std::string& container) = 0;

	/** Return the root container path. */
	virtual const std::string getRootContainer() const = 0;

	// -- queries -------------------------------------------------------------

	/** List all container entries inside `folder`. */
	virtual const std::vector<ContainerEntry>& getContainers(const std::string& container) = 0;
	/** List all file entries inside `folder`. */
	virtual const std::vector<ContainerEntry>& getFiles(const std::string& container) = 0;
	/**
	 * Search files by fuzzy matching against displayName and description.
	 * Returns all matching entries sorted by relevance score.
	 * When search is active, no containers should be shown.
	 * Default implementation returns empty (PatchStorage doesn't support search yet).
	 */
	virtual std::vector<ContainerEntry> search(const std::string& query) = 0;
	/** Return true if `path` is a container. */
	virtual bool isContainer(const std::string& entry) const = 0;
	/** Return true if `path` is a file. */
	virtual bool isFile(const std::string& entry) const = 0;

	/** Return the parent container of `path`. */
	virtual const std::string getParentContainer(const std::string& entry) const = 0;
	/** Return the filename portion of `path`. */
	virtual const std::string getFilename(const std::string& fileId) const = 0;
	/** Return the absolute file of the path */
	virtual const std::string getTempFilePath(const std::string& fileId) const = 0;

	/** Return true if `str` ends with `suffix`. */
	static bool endsWith(const std::string& str, const std::string& suffix) {
		return str.size() >= suffix.size()
			&& 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
	}

	/**
	 * Unique identifier for the concrete source type (e.g. "filesystem").
	 * Used during deserialization to instantiate the correct subclass.
	 */
	virtual const std::string getSourceType() const = 0;

	/**
	 * Returns true if the source provides vcv files (in contrast to vcvs)
	 */
	virtual bool isPatchSource() const = 0;

	/**
	 * Human-readable name for this source (e.g. the root path or a user-given label).
	 * Used in the UI to identify the source.
	 */
	virtual const std::string getSourceName() const = 0;

	/**
	 * Return the optional index for this source, or nullptr if unsupported.
	 * The caller does not take ownership; the index is owned by the source.
	 */
	virtual PatchSourceIndex* getIndex() const { return nullptr; }

	/**
	 * Load and return the JSON content of a file identified by `fileId`.
	 * The caller is responsible for calling json_decref when done.
	 * Returns nullptr if the file cannot be loaded or parsed.
	 */
	virtual json_t* getFileJson(const std::string& fileId) const = 0;

	// -- serialization -------------------------------------------------------

	/**
	 * Serialize the source state to a JSON object.
	 * The caller is responsible for calling json_decref when done.
	 */
	virtual json_t* toJson() const = 0;

	/**
	 * Restore the source state from a JSON object.
	 * Return true on success, false if the data is invalid or incompatible.
	 */
	virtual bool fromJson(json_t* sourceJ) = 0;

	// -- ui ------------------------------------------------------------------

	/**
	 * Append source-specific menu items to a context menu.
	 * The menu is owned by the caller; subclasses add items to it.
	 */
	virtual void appendSourceMenuItems(ui::Menu* menu) = 0;

	virtual void appendPreviewMenuItems(ui::Menu* menu, std::string fileId) = 0;
};


namespace filesystem {
	namespace vcvs {
		extern std::string getSlug();
		extern PatchSource* initSource();
	}
	namespace vcv {
		extern std::string getSlug();
		extern PatchSource* initSource();
	}
}

namespace patchstorage {
	extern std::string getSlug();
	extern bool canCreate();
	extern PatchSource* initSource();
}

/**
 * Factory function: create the correct PatchSource subclass
 * from a JSON snapshot. Returns nullptr if the type is unknown.
 */
inline PatchSource* createSourceFromJson(json_t* sourceJ) {
	static std::map<std::string, std::function<PatchSource*()>> sourceSlugs {
		{ filesystem::vcvs::getSlug(), filesystem::vcvs::initSource },
		{ filesystem::vcv::getSlug(), filesystem::vcv::initSource },
		{ patchstorage::getSlug(), patchstorage::initSource }
	};

	json_t* slugJ = json_object_get(sourceJ, "slug");
	if (!slugJ) return nullptr;

	std::string slug = json_string_value(slugJ);
	if (sourceSlugs.find(slug) != sourceSlugs.end()) {
		PatchSource* src = sourceSlugs[slug]();
		if (!src->fromJson(sourceJ)) {
			delete src;
			return nullptr;
		}
		src->setHelper(PatchHelperWidget::getInstance());
		return src;
	}
	return nullptr;
}

} // namespace patch
} // namespace Mb
} // namespace StoermelderPackOne