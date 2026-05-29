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
	bool isContainer = false;
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

// Build a peak waveform by streaming from an open AudioStream — no full-file buffer needed.
inline bool buildWaveformCache(int64_t timestamp, AudioStream& stream,
                               int pixelWidth, WaveformCache& out) {
	int     channels = stream.channels();
	int64_t total    = stream.totalFrames();
	if (pixelWidth <= 0 || total <= 0 || channels <= 0) return false;

	out.bucketCount = pixelWidth;
	out.peaks.assign(channels, std::vector<std::pair<float,float>>(pixelWidth, {0.f, 0.f}));
	out.fileTimestamp = timestamp;

	const int64_t BUF_FRAMES = 4096;
	std::vector<float> buf((size_t)(BUF_FRAMES * channels));
	std::vector<float> mn(channels), mx(channels);
	double  framesPerBucket = (double)total / (double)pixelWidth;
	int64_t framePos = 0;

	for (int b = 0; b < pixelWidth; b++) {
		int64_t bucketStart = (int64_t)(b * framesPerBucket);
		int64_t bucketEnd   = (int64_t)((b + 1) * framesPerBucket);
		if (bucketEnd > total) bucketEnd = total;
		int64_t bucketLen = bucketEnd - bucketStart;
		if (bucketLen <= 0) continue;

		if (framePos != bucketStart) {
			stream.seekTo(bucketStart);
			framePos = bucketStart;
		}

		std::fill(mn.begin(), mn.end(), 0.f);
		std::fill(mx.begin(), mx.end(), 0.f);
		int64_t remaining = bucketLen;
		while (remaining > 0) {
			int64_t toRead = std::min(remaining, BUF_FRAMES);
			int64_t got = stream.readF32(buf.data(), toRead);
			if (got <= 0) break;
			for (int64_t f = 0; f < got; f++) {
				for (int ch = 0; ch < channels; ch++) {
					float s = buf[(size_t)(f * channels + ch)];
					if (s < mn[ch]) mn[ch] = s;
					if (s > mx[ch]) mx[ch] = s;
				}
			}
			framePos  += got;
			remaining -= got;
		}
		for (int ch = 0; ch < channels; ch++)
			out.peaks[ch][b] = {mn[ch], mx[ch]};
	}
	return true;
}

// ─── DataSource ──────────────────────────────────────────────────────────────

struct DataSource {
	virtual ~DataSource() = default;

	virtual std::string rootPath() const = 0;
	virtual bool isSupportedFile(const std::string& path) const = 0;

	// Load the top-level children of a path asynchronously via TaskWorker.
	// Calls onDone on the main thread (caller polls loadState).
	virtual void loadChildrenAsync(const std::string& path, TaskWorker& worker,
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

	// Append source-level settings to the source button dropdown (e.g. conversion options).
	virtual void appendSourceMenuItems(ui::Menu* menu) {}

	// Returns a callable that produces the path to drop when invoked.
	// Trivial cases return a lightweight lambda; heavy work (e.g. audio transcoding)
	// is deferred inside the returned lambda so the caller can dispatch it to a worker.
	// The lambda is always called on the worker thread by SirenDropHandler.
	//   targetSampleRate — resample to this rate if > 0 and it differs from the file rate
	//   trimIn / trimOut — normalised [0, 1] region to keep; defaults retain the full file
	virtual std::function<std::string()> prepareForDrop(const std::string& id, bool convertToWav,
			int targetSampleRate = 0, float trimIn = 0.f, float trimOut = 1.f) {
		return [id]() { return id; };
	}

	// ── audio provision (abstracts format / transport) ────────────────────────

	// Human-readable display name for an item id (e.g. filename without directory).
	virtual std::string getDisplayName(const std::string& id) const { return id; }

	// Relative path within this source (used for metadata key lookup).
	virtual std::string getRelativePath(const std::string& id) const { return id; }

	// Modification timestamp for cache invalidation. 0 = caching disabled.
	virtual int64_t getTimestamp(const std::string& id) const { return 0; }

	// Load audio header metadata only (fast, no PCM decode).
	virtual bool loadAudioInfo(const std::string& id, AudioInfo& out) const { return false; }

	// Open a streaming decoder for the given item id.
	// Returns nullptr if streaming is unsupported or the item cannot be opened.
	virtual std::unique_ptr<AudioStream> openAudioStream(const std::string& id) const {
		return nullptr;
	}

	// Build a waveform cache by streaming audio frames — avoids full decode into memory.
	virtual bool buildWaveformCache(const std::string& id, int64_t timestamp,
			int pixelWidth, WaveformCache& out) const {
		auto stream = openAudioStream(id);
		if (!stream) return false;
		return ::StoermelderPackOne::Siren::buildWaveformCache(timestamp, *stream, pixelWidth, out);
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
