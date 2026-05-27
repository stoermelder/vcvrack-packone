#pragma once
#include <string>
#include <vector>
#include <functional>
#include <rack.hpp>
#include "../../utils/TaskWorker.hpp"
#include "SirenMetadata.hpp"

namespace StoermelderPackOne {
namespace Siren {

struct DataSourceNode {
	std::string name;
	std::string fullPath;
	std::string relativePath;  // relative to root, '/' separated
	bool isDirectory = false;
	bool childrenLoaded = false;
	float durationSeconds = 0.f;
	std::vector<DataSourceNode> children;
};

enum class LoadState { IDLE, LOADING, READY };

struct DataSource {
	virtual ~DataSource() = default;

	virtual std::string rootPath() const = 0;
	virtual bool isSupportedFile(const std::string& path) const = 0;

	// Load the top-level children of a path asynchronously via TaskWorker.
	// Calls onDone on the main thread (caller polls loadState).
	virtual void loadChildrenAsync(
		const std::string& path,
		TaskWorker& worker,
		std::function<void(std::vector<DataSourceNode>)> onDone) = 0;

	// Sync version for testing
	virtual std::vector<DataSourceNode> loadChildrenSync(const std::string& path) = 0;

	// Per-file metadata (tags, favorites). Returns nullptr if unsupported.
	virtual RootMetadata* getMetadata() { return nullptr; }

	// Persist metadata to disk. Called automatically on destruction.
	virtual void saveMetadata() {}

	// Append source-specific context menu items for a tree node.
	virtual void appendNodeMenuItems(ui::Menu* menu, const DataSourceNode& node) {}
};

} // namespace Siren
} // namespace StoermelderPackOne
