#pragma once
#include <string>
#include <vector>
#include <rack.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace selection {


/**
 * Abstract interface for a data source index.
 * An index stores per-file metadata such as description, predefined tags, and custom tags.
 * Not every data source supports indexing; getIndex() on a source may return nullptr.
 *
 * The `fileId` identifies a file within the source. For a filesystem source it is the
 * relative path from the source root.
 */
struct SelectionSourceIndex {
	virtual ~SelectionSourceIndex() = default;

	// -- description ---------------------------------------------------------

	/** Return the description text for the given file, or empty if not set. */
	virtual const std::string getDescription(const std::string& fileId) const = 0;
	/** Set the description text for the given file. No effect if the index is read-only. */
	virtual void setDescription(const std::string& fileId, const std::string& description) = 0;

	// -- predefined tags -----------------------------------------------------

    virtual bool hasTag(const std::string& fileId, const std::string& tag) = 0;
	/** Return the list of predefined tag names assigned to the given file. */
	virtual std::vector<std::string> getTags(const std::string& fileId) const = 0;
	/** Add a single predefined tag to the given file. No effect if the index is read-only or tag already assigned. */
	virtual void addTag(const std::string& fileId, const std::string& tag) = 0;
	/** Remove a single predefined tag from the given file. No effect if the index is read-only or tag not assigned. */
	virtual void removeTag(const std::string& fileId, const std::string& tag) = 0;

	// -- custom tags ----------------------------------------------------------

    virtual bool hasCustomTag(const std::string& fileId, const std::string& tag) = 0;
	/** Return the list of custom tag strings assigned to the given file. */
	virtual std::vector<std::string> getCustomTags(const std::string& fileId) const = 0;
	/** Add a single custom tag to the given file. No effect if the index is read-only or tag already assigned. */
	virtual void addCustomTag(const std::string& fileId, const std::string& tag) = 0;
	/** Remove a single custom tag from the given file. No effect if the index is read-only or tag not assigned. */
	virtual void removeCustomTag(const std::string& fileId, const std::string& tag) = 0;

	// -- favorites ------------------------------------------------------------

	/** Return true if the given file is marked as a favorite. */
	virtual bool isFavorite(const std::string& fileId) const = 0;
	/** Set or clear the favorite flag for the given file. No effect if the index is read-only. */
	virtual void setFavorite(const std::string& fileId, bool favorite) = 0;

	// -- read-only status -----------------------------------------------------

	/** Return true if this index cannot be modified. */
	virtual bool isReadOnly() const = 0;

	// -- tag queries ----------------------------------------------------------

	/** Return all unique predefined tag names used across indexed files. */
	virtual std::set<std::string> getTagsAll() const = 0;
	/** Return all unique custom tag strings used across indexed files. */
	virtual std::set<std::string> getCustomTagsAll() const = 0;
};


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne