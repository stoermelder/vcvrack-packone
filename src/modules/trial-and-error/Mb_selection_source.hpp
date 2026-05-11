#pragma once
#include <string>
#include <vector>
#include <rack.hpp>

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
	virtual void setContainer(const std::string& path) = 0;

	/** Return the root container path. */
	virtual std::string getRootContainer() const = 0;
	/** Set the root container path. */
	virtual void setRootContainer(const std::string& path) = 0;

	// -- queries -------------------------------------------------------------

	/** List all entries (files + containers) inside `folder`. */
	virtual std::vector<std::string> getEntries(const std::string& folder) = 0;
	/** Return true if `path` is a directory. */
	virtual bool isDirectory(const std::string& path) = 0;
	/** Return true if `path` is a file. */
	virtual bool isFile(const std::string& path) = 0;

	/** Return the parent container of `path`. */
	virtual std::string getParentContainer(const std::string& path) = 0;
	/** Return the filename portion of `path`. */
	virtual std::string getFilename(const std::string& path) = 0;

	/** Return true if `str` ends with `suffix`. */
	static bool endsWith(const std::string& str, const std::string& suffix) {
		return str.size() >= suffix.size()
			&& 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
	}

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
	 * Interactively create a new source of the same type via a UI dialog.
	 * Returns the new source, or nullptr if the user cancelled.
	 * The caller takes ownership.
	 */
	virtual SelectionSource* createSource() const = 0;

	/**
	 * Append source-specific menu items to a context menu.
	 * The menu is owned by the caller; subclasses add items to it.
	 */
	virtual void appendMenuItems(ui::Menu* menu) = 0;
};


namespace filesystem {
	extern std::string getSlug();
	extern SelectionSource* getSource();
}

/**
 * Factory function: create the correct SelectionSource subclass
 * from a JSON snapshot. Returns nullptr if the type is unknown.
 */
inline SelectionSource* createSelectionSourceFromJson(json_t* sourceJ) {
	static std::map<std::string, std::function<SelectionSource*()>> sourceTypes {
		{ filesystem::getSlug(), filesystem::getSource }
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