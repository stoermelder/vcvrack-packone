#pragma once
#include "SirenMetadata.hpp"
#include "SirenDataSource.hpp"
#include <ghc/filesystem.hpp>


namespace StoermelderPackOne {
namespace Siren {

// MetadataStore subclass used by tests to redirect persistence to a scratch
// folder under the system's temp directory, so load()/save() — including the
// rename-write-verify-restore sequence — can be exercised against real files
// without ever touching the user's actual settings folder.
struct ScratchMetadataStore : MetadataStore {
	std::string filePath() const override {
		return (ghc::filesystem::temp_directory_path() / "siren_test_metadata"
				/ ("siren-" + hashPath(rootPath) + ".json")).string();
	}
};

inline std::unique_ptr<MetadataStore> scratchMetadataStore() {
	return std::make_unique<ScratchMetadataStore>();
}

// Minimal in-memory DataSource mock for tests that only care about the
// DataSource/MetadataStore contract (e.g. "getMetadata returns a stable,
// mutable pointer") and have no business touching the filesystem at all —
// keeps such tests decoupled from FileSystemDataSource's I/O behavior.
struct TestDataSource : DataSource {
	std::string root;
	MetadataStore meta_;

	explicit TestDataSource(const std::string& rootPath) : root(rootPath) {
		meta_.rootPath = root;
	}

	std::string rootId() const override { return root; }
	bool isSupportedFile(const std::string& path) const override { return true; }

	void loadChildrenAsync(const std::string& id, TaskWorker& worker,
			std::function<void(std::vector<DataSourceNode>)> onDone) override {
		onDone(loadChildrenSync(id));
	}
	std::vector<DataSourceNode> loadChildrenSync(const std::string& id, bool withAudioInfo = true) override { return {}; }

	MetadataStore* getMetadata() override { return &meta_; }
};

} // namespace Siren
} // namespace StoermelderPackOne
