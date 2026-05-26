#pragma once
#include <rack.hpp>
#include <ghc/filesystem.hpp>
#include "SirenDataSource.hpp"
#include "SirenAudio.hpp"

namespace StoermelderPackOne {
namespace Siren {

static const std::vector<std::string> SUPPORTED_EXTENSIONS = { ".wav", ".WAV", ".flac", ".FLAC", ".mp3", ".MP3" };

inline bool isSupportedAudioFile(const std::string& path) {
	std::string ext = rack::system::getExtension(rack::system::getFilename(path));
	for (const std::string& e : SUPPORTED_EXTENSIONS)
		if (ext == e) return true;
	return false;
}

struct FileSystemDataSource : DataSource {
	std::string root;

	explicit FileSystemDataSource(const std::string& rootPath) : root(rootPath) {}

	std::string rootPath() const override { return root; }

	bool isSupportedFile(const std::string& path) const override {
		return isSupportedAudioFile(path);
	}

	std::vector<DataSourceNode> loadChildrenSync(const std::string& dirPath) override {
		std::vector<DataSourceNode> result;
		ghc::filesystem::path base(root);

		auto scan = [&](const std::string& path) {
			try {
				for (const auto& entry : ghc::filesystem::directory_iterator(path)) {
					DataSourceNode node;
					node.fullPath = entry.path().string();
					node.name = entry.path().filename().string();
					node.isDirectory = entry.is_directory();
					// Relative path from root, starting with '/'
					auto rel = entry.path().lexically_relative(base);
					node.relativePath = "/" + rel.generic_string();
					if (node.isDirectory || isSupportedAudioFile(node.fullPath)) {
						if (!node.isDirectory) {
							AudioInfo ai;
							if (loadAudioInfo(node.fullPath, ai))
								node.durationSeconds = ai.durationSeconds;
						}
						result.push_back(std::move(node));
					}
				}
			}
			catch (...) {}
			std::sort(result.begin(), result.end(), [](const DataSourceNode& a, const DataSourceNode& b) {
				// Directories first, then files; both alphabetical
				if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
				return rack::string::lowercase(a.name) < rack::string::lowercase(b.name);
			});
		};
		scan(dirPath.empty() ? root : dirPath);
		return result;
	}

	void loadChildrenAsync(
		const std::string& path,
		TaskWorker& worker,
		std::function<void(std::vector<DataSourceNode>)> onDone) override
	{
		std::string scanPath = path.empty() ? root : path;
		std::string rootCopy = root;
		worker.work([scanPath, rootCopy, onDone]() {
			std::vector<DataSourceNode> result;
			ghc::filesystem::path base(rootCopy);
			try {
				for (const auto& entry : ghc::filesystem::directory_iterator(scanPath)) {
					DataSourceNode node;
					node.fullPath = entry.path().string();
					node.name = entry.path().filename().string();
					node.isDirectory = entry.is_directory();
					auto rel = entry.path().lexically_relative(base);
					node.relativePath = "/" + rel.generic_string();
					if (node.isDirectory || isSupportedAudioFile(node.fullPath)) {
						if (!node.isDirectory) {
							AudioInfo ai;
							if (loadAudioInfo(node.fullPath, ai))
								node.durationSeconds = ai.durationSeconds;
						}
						result.push_back(std::move(node));
					}
				}
			}
			catch (...) {}
			std::sort(result.begin(), result.end(), [](const DataSourceNode& a, const DataSourceNode& b) {
				if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
				return rack::string::lowercase(a.name) < rack::string::lowercase(b.name);
			});
			onDone(std::move(result));
		});
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
