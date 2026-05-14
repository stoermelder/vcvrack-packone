#pragma once
#include <string>
#include <vector>
#include <rack.hpp>
#include "Mb_selection_helper.hpp"
#include "Mb_selection_source_index.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {

/**
 * Abstract interface for a selection data source.
 * Decouples the Browser from the file system, enabling
 * alternative data sources (e.g. remote HTTP) to be plugged in later.
 */
struct SelectionSource {
	virtual ~SelectionSource() = default;

	// -- lifetime -----------------------------------------------------------

	/** Called when the source is attached to a sidebar. */
	virtual void onAttach() {}
	/** Called when the source is detached from a sidebar. */
	virtual void onDetach() {}

	/** Set the cache directory used for temporary downloads/extractions. */
	virtual void setCacheDir(const std::string& dir) {}
	/** Set the helper instance for cache access. */
	virtual void setHelper(SelectionBrowserHelper* helper) {}

	// -- navigation ----------------------------------------------------------

	/** Return the current container path. */
	virtual const std::string getContainer() const = 0;
	/** Set the current container path. */
	virtual void setContainer(const std::string& container) = 0;

	/** Return the root container path. */
	virtual const std::string getRootContainer() const = 0;

	// -- queries -------------------------------------------------------------

	/** List all container entries inside `folder`. */
	virtual const std::vector<std::string> getContainers(const std::string& container) = 0;
	/** List all file entries inside `folder`. */
	virtual const std::vector<std::string> getFiles(const std::string& container) = 0;
	/** Return true if `path` is a container. */
	virtual bool isContainer(const std::string& entry) = 0;
	/** Return true if `path` is a file. */
	virtual bool isFile(const std::string& entry) = 0;

	/** Return the parent container of `path`. */
	virtual const std::string getParentContainer(const std::string& entry) = 0;
	/** Return the filename portion of `path`. */
	virtual const std::string getFilename(const std::string& fileId) = 0;
	/** Return the absolute file of the path */
	virtual const std::string getAbsoluteFilePath(const std::string& fileId) = 0;

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
	virtual SelectionSourceIndex* getIndex() const { return nullptr; }

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
	 * Returns the current task status of the data source.
	 */
	virtual const std::string& getStatusText() = 0;

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
		extern SelectionSource* initSource();
	}
	namespace vcv {
		extern std::string getSlug();
		extern SelectionSource* initSource();
	}
}

namespace patchstorage {
	extern std::string getSlug();
	extern bool canCreate();
	extern SelectionSource* initSource();
}

/**
 * Factory function: create the correct SelectionSource subclass
 * from a JSON snapshot. Returns nullptr if the type is unknown.
 */
inline SelectionSource* createSourceFromJson(json_t* sourceJ) {
	static std::map<std::string, std::function<SelectionSource*()>> sourceSlugs {
		{ filesystem::vcvs::getSlug(), filesystem::vcvs::initSource },
		{ filesystem::vcv::getSlug(), filesystem::vcv::initSource },
		{ patchstorage::getSlug(), patchstorage::initSource }
	};

	json_t* slugJ = json_object_get(sourceJ, "slug");
	if (!slugJ) return nullptr;

	std::string slug = json_string_value(slugJ);
	if (sourceSlugs.find(slug) != sourceSlugs.end()) {
		SelectionSource* src = sourceSlugs[slug]();
		if (!src->fromJson(sourceJ)) {
			delete src;
			return nullptr;
		}
		src->setHelper(SelectionBrowserHelper::getInstance());
		return src;
	}
	return nullptr;
}

} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne