#pragma once
#include <string>
#include <vector>
#include <functional>
#include "../../utils/TaskWorker.hpp"

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
};

} // namespace Siren
} // namespace StoermelderPackOne
