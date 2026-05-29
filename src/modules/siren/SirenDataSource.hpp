#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <rack.hpp>
#include "../../utils/TaskWorker.hpp"
#include "SirenMetadata.hpp"
#include "SirenAudio.hpp"

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

// ─── AudioStream ─────────────────────────────────────────────────────────────
// Streaming decoder kept open by the data source — no full-file buffering needed.

struct AudioStream {
	virtual ~AudioStream() = default;
	virtual int     channels()    const = 0;
	virtual int     sampleRate()  const = 0;
	virtual int64_t totalFrames() const = 0;  // 0 if unknown (e.g. live stream)

	// Read up to frameCount interleaved PCM frames normalised to [-1, 1].
	// Returns the number of frames actually read (< frameCount at EOF).
	virtual int64_t readF32(float* buffer, int64_t frameCount) = 0;

	// Seek to an absolute frame index. Returns true on success.
	virtual bool seekTo(int64_t frameIndex) = 0;
};

// ─── DataSource ──────────────────────────────────────────────────────────────

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
	// onChanged is called after any metadata modification so the browser can refresh.
	virtual void appendNodeMenuItems(ui::Menu* menu, const DataSourceNode& node,
	                                 std::function<void()> onChanged = {}) {}

	// ── audio provision (abstracts format / transport) ────────────────────────

	// Human-readable display name for an item id (e.g. filename without directory).
	virtual std::string getDisplayName(const std::string& id) const { return id; }

	// Relative path within this source (used for metadata key lookup).
	virtual std::string getRelativePath(const std::string& id) const { return id; }

	// Modification timestamp for cache invalidation. 0 = caching disabled.
	virtual int64_t getTimestamp(const std::string& id) const { return 0; }

	// Load audio header metadata only (fast, no PCM decode).
	virtual bool loadAudioInfo(const std::string& id, AudioInfo& out) const { return false; }

	// Decode full PCM audio to interleaved float samples normalised to [-1, 1].
	// Used for waveform cache building; prefer openAudioStream for playback.
	virtual bool decodeAudioF32(const std::string& id,
	                            std::vector<float>& samples,
	                            int& channels, int& sampleRate) const { return false; }

	// Open a streaming decoder for the given item id.
	// Returns nullptr if streaming is unsupported or the item cannot be opened.
	virtual std::unique_ptr<AudioStream> openAudioStream(const std::string& id) const {
		return nullptr;
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
