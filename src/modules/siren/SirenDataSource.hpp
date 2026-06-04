#pragma once
#include <rack.hpp>
#include "../../utils/TaskWorker.hpp"
#include "SirenMetadata.hpp"
#include "SirenAudioStream.hpp"
#include "SirenAudio.hpp"


namespace StoermelderPackOne {
namespace Siren {

struct DataSourceNode {
	std::string name;
	std::string relativePath;  // relative to root, '/' separated; used as id
	bool isContainer = false;
	float durationSeconds = 0.f;
	std::vector<DataSourceNode> children;
};

enum class LoadState { IDLE, LOADING, READY };


// Build a peak waveform by streaming from an open AudioStream — no full-file buffer needed.
// Single sequential pass: large reads amortize decoder overhead; bucket boundaries are tracked
// with a running counter so there is no division or seek in the inner loop.
inline bool buildWaveformCache(int64_t timestamp, AudioStream& stream, int pixelWidth, WaveformCache& out) {
	int channels = stream.channels();
	int64_t total = stream.totalFrames();
	if (pixelWidth <= 0 || total <= 0 || channels <= 0) return false;

	int sampleRes = std::min(pixelWidth * 8, 8192);
	out.sampleCount   = sampleRes;
	out.fileTimestamp = timestamp;
	out.samples.assign(channels, std::vector<float>(sampleRes, 0.f));

	const int64_t BUF_FRAMES = 65536;
	std::vector<float> buf((size_t)(BUF_FRAMES * channels));
	double framesPerSample = (double)total / (double)sampleRes;
	int64_t framePos = 0;
	int curSample    = 0;
	bool sampleTaken = false;
	int64_t nextSampleBoundary = (sampleRes > 1) ? (int64_t)(framesPerSample) : total;

	while (framePos < total) {
		int64_t toRead = std::min(BUF_FRAMES, total - framePos);
		int64_t got = stream.readF32(buf.data(), toRead);
		if (got <= 0) break;

		for (int64_t f = 0; f < got; f++) {
			while (framePos + f >= nextSampleBoundary && curSample < sampleRes - 1) {
				curSample++;
				sampleTaken = false;
				nextSampleBoundary = (curSample + 1 < sampleRes)
				                   ? (int64_t)((curSample + 1) * framesPerSample) : total;
			}
			if (!sampleTaken) {
				for (int ch = 0; ch < channels; ch++)
					out.samples[ch][curSample] = buf[(size_t)(f * channels + ch)];
				sampleTaken = true;
			}
		}
		framePos += got;
	}
	return true;
}

// ─── DataSource ──────────────────────────────────────────────────────────────

struct DataSource {
	virtual ~DataSource() = default;

	virtual std::string rootPath() const = 0;
	virtual std::string getRootDisplayName() const { return rootPath(); }
	virtual std::string rootId() const { return ""; }
	virtual bool isSupportedFile(const std::string& path) const = 0;

	// Load the top-level children of an item id asynchronously via TaskWorker.
	// Use rootId() to load the root level. Calls onDone on the main thread.
	virtual void loadChildrenAsync(const std::string& id, TaskWorker& worker,
		std::function<void(std::vector<DataSourceNode>)> onDone) = 0;

	// Sync version for testing
	virtual std::vector<DataSourceNode> loadChildrenSync(const std::string& id) = 0;

	// Per-file metadata (tags, favorites). Returns nullptr if unsupported.
	virtual RootMetadata* getMetadata() { return nullptr; }

	// Returns true if a node matches the search query.
	// - Own name always checked.
	// - Containers: also checks all known descendant filenames via metadata.
	// - Files: also checks every ancestor directory name encoded in relativePath.
	virtual bool matchesSearch(const std::string& relativePath, bool isContainer, const std::string& lowerQuery) {
		if (lowerQuery.empty()) return true;

		// Own name
		size_t lastSlash = relativePath.rfind('/');
		std::string ownName = (lastSlash != std::string::npos) ? relativePath.substr(lastSlash + 1) : relativePath;
		if (rack::string::lowercase(ownName).find(lowerQuery) != std::string::npos)
			return true;

		if (isContainer) {
			// Known descendant filenames via metadata
			RootMetadata* meta = getMetadata();
			if (!meta) return false;
			const std::string dirPrefix = relativePath + "/";
			for (const auto& pair : meta->samples) {
				if (pair.first.compare(0, dirPrefix.size(), dirPrefix) != 0) continue;
				size_t s = pair.first.rfind('/');
				std::string name = (s != std::string::npos) ? pair.first.substr(s + 1) : pair.first;
				if (rack::string::lowercase(name).find(lowerQuery) != std::string::npos)
					return true;
			}
		}
		else {
			// Ancestor directory names are encoded in the relative path
			std::string p = relativePath;
			size_t pos;
			while ((pos = p.find('/')) != std::string::npos) {
				std::string component = p.substr(0, pos);
				if (rack::string::lowercase(component).find(lowerQuery) != std::string::npos)
					return true;
				p = p.substr(pos + 1);
			}
		}
		return false;
	}

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
	//   resampleQuality  — speex resampler quality (0..10) used when targetSampleRate > 0
	virtual std::function<std::string()> prepareForDrop(const std::string& id, bool convertToWav,
			int targetSampleRate = 0, float trimIn = 0.f, float trimOut = 1.f, int resampleQuality = 6) {
		return [id]() { return id; };
	}

	// ── audio provision (abstracts format / transport) ────────────────────────

	// Reconstruct a DataSourceNode from a stored relative path (e.g. for patch restore).
	virtual DataSourceNode resolveNode(const std::string& relativePath) const { return DataSourceNode{}; }

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
