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

// ─── Loop crossfade post-processing ──────────────────────────────────────────
// Rotation + crossfade: makes a sample loop-ready so that looping at the
// file boundaries produces a smooth transition.
//
// Technique:
//   1. Find the frame M nearest to the midpoint that crosses zero on channel 0
//      (minimises the click at the new loop-point).
//   2. Rearrange: [original[M..N-1], original[0..M-1]]  (swap halves)
//   3. Crossfade the join: blend the last C frames of the second half (fade-out)
//      with the first C frames of the first half (fade-in) into a single C-frame
//      region.  This handles the formerly-glitchy original end→start transition.
//   4. Output length = N − C.
//
// After this the new loop point (output end → output start) lands at the
// original midpoint M, which is typically a quieter/smoother splice location.
//
// `samples` is interleaved float PCM with `channels` channels.
// `sampleRate` is used only to convert `crossfadeSecs` to frame count.
// `crossfadeSecs` is clamped so that C ≤ min(M, N-M) − 1; a too-long
// crossfade is silently shortened rather than failing.
inline void applyLoopCrossfade(std::vector<float>& samples, int channels, int sampleRate, float crossfadeSecs) {
	if (channels <= 0 || sampleRate <= 0 || samples.empty()) return;
	int64_t N = (int64_t)(samples.size() / (size_t)channels);
	if (N < 4) return;

	// Find the zero-crossing of channel 0 within a ¼-second window around the midpoint
	// that has the lowest splice amplitude (|prev| + |cur|). Minimising amplitude at
	// the splice — not just proximity to the midpoint — is what eliminates the click:
	// a zero crossing at -0.8 → +0.6 looks valid but still sounds audible.
	// We scan the whole window rather than stopping at the first crossing found, so
	// we can trade a bit of search time for a much quieter loop point.
	// `mid` is fixed so the symmetric probe positions are always mid ± d.
	const int64_t mid = N / 2;
	int64_t M = mid;
	{
		const int64_t searchWindow = std::min(N / 4, (int64_t)(sampleRate / 4));
		float bestScore = std::numeric_limits<float>::max();
		for (int64_t d = 0; d <= searchWindow; d++) {
			for (int sign : {-1, 1}) {
				int64_t pos = mid + sign * d;
				if (pos <= 0 || pos >= N - 1) continue;
				float cur  = samples[(size_t)(pos       * channels)];
				float prev = samples[(size_t)((pos - 1) * channels)];
				if (cur * prev <= 0.f) {
					float score = std::abs(cur) + std::abs(prev);
					if (score < bestScore) {
						bestScore = score;
						M = pos;
					}
				}
			}
			if (bestScore < 1e-5f) break;  // essentially silent — can't do better
		}
	}

	// Clamp crossfade length so it fits in both halves.
	int64_t C = (int64_t)(crossfadeSecs * (float)sampleRate);
	int64_t maxC = std::min(M, N - M) - 1;
	if (maxC <= 0) return;
	C = std::max((int64_t)1, std::min(C, maxC));

	// Build output of length N−C:
	//   [original[M .. N-C-1], crossfade, original[C .. M-1]]
	// where crossfade blends original[N-C .. N-1] (fade-out) and
	//                         original[0  .. C-1]  (fade-in).
	int64_t part1Len = (N - C) - M;   // frames from rotation point to end minus tail
	int64_t part2Len = M - C;          // frames from crossfade end to rotation point
	int64_t outN     = part1Len + C + part2Len; // == N - C

	std::vector<float> out((size_t)(outN * channels));

	// Part 1: original[M .. N-C-1]
	for (int64_t f = 0; f < part1Len; f++)
		for (int ch = 0; ch < channels; ch++)
			out[(size_t)(f * channels + ch)] = samples[(size_t)((M + f) * channels + ch)];

	// Crossfade: blend tail of second half (fade-out) with head of first half (fade-in)
	for (int64_t f = 0; f < C; f++) {
		float alpha   = (C > 1) ? (float)f / (float)(C - 1) : 1.f;
		float angle   = alpha * float(M_PI) * 0.5f;
		float fadeOut = std::cos(angle);
		float fadeIn  = std::sin(angle);
		for (int ch = 0; ch < channels; ch++) {
			float s1 = samples[(size_t)((N - C + f) * channels + ch)];
			float s2 = samples[(size_t)(f            * channels + ch)];
			out[(size_t)((part1Len + f) * channels + ch)] = s1 * fadeOut + s2 * fadeIn;
		}
	}

	// Part 2: original[C .. M-1]
	for (int64_t f = 0; f < part2Len; f++)
		for (int ch = 0; ch < channels; ch++)
			out[(size_t)((part1Len + C + f) * channels + ch)] = samples[(size_t)((C + f) * channels + ch)];

	samples = std::move(out);
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

	// Sync version for testing. withAudioInfo controls whether each file's audio
	// header is opened to fill in DataSourceNode::durationSeconds — pass false
	// for callers that only need names/relativePath/isContainer (e.g. recursive
	// scans), since opening every file (notably MP3 frame counting) is costly.
	virtual std::vector<DataSourceNode> loadChildrenSync(const std::string& id, bool withAudioInfo = true) = 0;

	// Per-file metadata (tags, favorites). Returns nullptr if unsupported.
	virtual MetadataStore* getMetadata() { return nullptr; }

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
			MetadataStore* meta = getMetadata();
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

	// Remove any cached state owned by this data source that is safe to discard
	// when the source is being detached (e.g. waveform cache files). The data
	// source's persistent metadata MUST NOT be removed here — the caller is
	// expected to preserve it across detach/reattach of the same root.
	virtual void cleanup() {}

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
	//   outputDir        — when non-empty, write the generated file here instead of
	//                      alongside the source file
	//   loopOnDrop       — apply rotation+crossfade to produce a loop-ready WAV
	//   loopCrossfadeDuration — crossfade length in seconds for the loop join
	virtual std::function<std::string()> prepareForDrop(const std::string& id, bool convertToWav,
			int targetSampleRate = 0, float trimIn = 0.f, float trimOut = 1.f, int resampleQuality = 6,
			const std::string& outputDir = "", bool loopOnDrop = false, float loopCrossfadeDuration = 8.f) {
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

// ─── Loop preview: in-memory decode + process ────────────────────────────────

struct LoopPreviewResult {
	std::vector<float> samples;   // interleaved float PCM after rotation+crossfade
	int channels = 0;
	int sampleRate = 0;
	float durationSeconds = 0.f;  // actual duration of the processed buffer
	bool ok = false;
};

// Decode the trimmed region from src/id, apply rotation+crossfade, and return
// the result as an in-memory buffer. Runs on the worker thread.
// trimIn/trimOut are normalised [0, 1] over the full file.
inline LoopPreviewResult buildLoopPreview(DataSource& src, const std::string& id,
		float trimIn, float trimOut, float crossfadeDuration) {
	LoopPreviewResult result;

	AudioInfo info;
	if (!src.loadAudioInfo(id, info)) return result;
	if (info.channels <= 0 || info.sampleRate <= 0 || info.frameCount <= 0) return result;

	int64_t startFrame = (int64_t)(trimIn * (float)info.frameCount);
	int64_t endFrame = (int64_t)(trimOut * (float)info.frameCount);
	startFrame = std::max((int64_t)0, std::min(startFrame, info.frameCount));
	endFrame = std::max(startFrame, std::min(endFrame, info.frameCount));
	int64_t trimFrames = endFrame - startFrame;
	if (trimFrames <= 0) return result;

	// Cap at stereo to match the downstream fill-thread resampler.
	int ch = std::min(info.channels, 2);
	result.samples.resize((size_t)(trimFrames * ch));

	auto stream = src.openAudioStream(id);
	if (!stream) return result;
	stream->seekTo(startFrame);
	int64_t framesRead = stream->readF32(result.samples.data(), trimFrames);
	if (framesRead <= 0) return result;
	result.samples.resize((size_t)(framesRead * ch));

	applyLoopCrossfade(result.samples, ch, info.sampleRate, crossfadeDuration);
	if (result.samples.empty()) return result;

	result.channels = ch;
	result.sampleRate = info.sampleRate;
	result.durationSeconds = (float)(result.samples.size() / (size_t)ch) / (float)info.sampleRate;
	result.ok = true;
	return result;
}

} // namespace Siren
} // namespace StoermelderPackOne
