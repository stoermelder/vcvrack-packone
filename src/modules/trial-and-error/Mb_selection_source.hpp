#pragma once
#include <string>
#include <vector>
#include <rack.hpp>
#include "Mb_selection_source_index.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {

/**
 * Abstract interface for a selection data source.
 * Decouples the SelectionBrowser from the file system, enabling
 * alternative data sources (e.g. remote HTTP) to be plugged in later.
 */
struct SelectionSource {
	virtual ~SelectionSource() = default;

	// -- lifetime -----------------------------------------------------------

	/** Called when the source is attached to a sidebar. */
	virtual void onAttach() {}
	/** Called when the source is detached from a sidebar. */
	virtual void onDetach() {}

	// -- navigation ----------------------------------------------------------

	/** Return the current container path. */
	virtual std::string getContainer() const = 0;
	/** Set the current container path. */
	virtual void setContainer(const std::string& container) = 0;

	/** Return the root container path. */
	virtual const std::string getRootContainer() const = 0;

	// -- queries -------------------------------------------------------------

	/** List all container entries inside `folder`. */
	virtual std::vector<std::string> getContainers(const std::string& container) = 0;
	/** List all file entries inside `folder`. */
	virtual std::vector<std::string> getFiles(const std::string& container) = 0;
	/** Return true if `path` is a container. */
	virtual bool isContainer(const std::string& entry) = 0;
	/** Return true if `path` is a file. */
	virtual bool isFile(const std::string& entry) = 0;

	/** Return the parent container of `path`. */
	virtual std::string getParentContainer(const std::string& entry) = 0;
	/** Return the filename portion of `path`. */
	virtual std::string getFilename(const std::string& fileId) = 0;

	/** Return true if `str` ends with `suffix`. */
	static bool endsWith(const std::string& str, const std::string& suffix) {
		return str.size() >= suffix.size()
			&& 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
	}

	/**
	 * Unique identifier for the concrete source type (e.g. "filesystem").
	 * Used during deserialization to instantiate the correct subclass.
	 */
	virtual std::string getSourceType() const = 0;

	/**
	 * Human-readable name for this source (e.g. the root path or a user-given label).
	 * Used in the UI to identify the source.
	 */
	virtual std::string getName() const = 0;

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

	// -- ui ------------------------------------------------------------------

	/**
	 * Append source-specific menu items to a context menu.
	 * The menu is owned by the caller; subclasses add items to it.
	 */
	virtual void appendMenuItems(ui::Menu* menu) = 0;

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
};


namespace filesystem {
	extern std::string getSlug();
	extern SelectionSource* initSource();
}

/**
 * Factory function: create the correct SelectionSource subclass
 * from a JSON snapshot. Returns nullptr if the type is unknown.
 */
inline SelectionSource* createSelectionSourceFromJson(json_t* sourceJ) {
	static std::map<std::string, std::function<SelectionSource*()>> sourceTypes {
		{ filesystem::getSlug(), filesystem::initSource }
	};

	json_t* typeJ = json_object_get(sourceJ, "type");
	if (!typeJ) return nullptr;

	std::string type = json_string_value(typeJ);
	if (sourceTypes.find(type) != sourceTypes.end()) {
		SelectionSource* src = sourceTypes[type]();
		if (!src->fromJson(sourceJ)) {
			delete src;
			return nullptr;
		}
		return src;
	}
	return nullptr;
}

} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne