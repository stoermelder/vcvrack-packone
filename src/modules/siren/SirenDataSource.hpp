#pragma once
#include <rack.hpp>
#include <sstream>
#include "../../utils/TaskWorker.hpp"
#include "../../vcv/api.hpp"
#include "SirenPaths.hpp"
#include "SirenMetadata.hpp"
#include "SirenAudioStream.hpp"
#include "SirenAudio.hpp"
#include <SoundTouch.h>

namespace StoermelderPackOne {
namespace Siren {

// A configured root location to browse, plus the type of DataSource that
// should be created for it. "fs" (FileSystemDataSource) is the default and,
// currently, the only type — kept explicit so future source types (e.g.
// cloud or hardware-backed libraries) can be distinguished from plain
// filesystem roots. See createDataSource() in SirenFileSystem.hpp.
struct RootContainer {
	std::string path;
	std::string type = "fs";
	std::string name;

	RootContainer() {}
	RootContainer(const std::string& path, const std::string& type, const std::string& name)
		: path(path), type(type), name(name) {}

	bool operator==(const RootContainer& other) const {
		return path == other.path && type == other.type;
	}
};

namespace filesystem {
	// Defined in SirenFileSystem.hpp. Declared here so createRootContainer()
	// below can dispatch to it without a circular include.
	RootContainer createRootContainer(const std::string& path);
}

// Builds the RootContainer for `path` with `type`, dispatching to the
// type-specific factory (mirroring createDataSource() in SirenFileSystem.hpp)
// so `name` is always derived consistently with the DataSource that will
// browse it. "fs" (the default, and currently the only type) maps to
// filesystem::createRootContainer.
inline RootContainer createRootContainer(const std::string& path, const std::string& type) {
	if (type == "fs") return filesystem::createRootContainer(path);
	return RootContainer();
}


struct SearchFilter {
	// Search query
	// The search field accepts plain text plus optional numeric filter terms of
	// the form "key:[op]value[unit]", e.g. "bpm:140", "bpm:>120", "length:<1s",
	// "length:>=2.5m". Recognised keys: "bpm", and "length"/"duration"/"len" for
	// the file's duration (unit "s"/"sec" or "m"/"min", default seconds).
	// Operators: "<", "<=", ">", ">=", "=" (default "=", matched with a small
	// tolerance since these are detected/measured values).
	enum class Field { Bpm, Length };
	enum class Op { Eq, Lt, Le, Gt, Ge };

	Field field;
	Op op;
	float value;
};

// Attempts to parse a single lowercase, whitespace-free token as a numeric
// filter. Returns false (leaving `out` untouched) if the token isn't one.
inline bool parseSearchFilter(const std::string& lowerToken, SearchFilter& out) {
	size_t colon = lowerToken.find(':');
	if (colon == std::string::npos) return false;
	std::string key = lowerToken.substr(0, colon);
	std::string rest = lowerToken.substr(colon + 1);

	SearchFilter::Field field;
	if (key == "bpm") field = SearchFilter::Field::Bpm;
	else if (key == "length" || key == "duration" || key == "len") field = SearchFilter::Field::Length;
	else return false;

	SearchFilter::Op op = SearchFilter::Op::Eq;
	if (!rest.empty() && (rest[0] == '<' || rest[0] == '>')) {
		if (rest.size() >= 2 && rest[1] == '=') {
			op = (rest[0] == '<') ? SearchFilter::Op::Le : SearchFilter::Op::Ge;
			rest = rest.substr(2);
		}
		else {
			op = (rest[0] == '<') ? SearchFilter::Op::Lt : SearchFilter::Op::Gt;
			rest = rest.substr(1);
		}
	}
	else if (!rest.empty() && rest[0] == '=') {
		rest = rest.substr(1);
	}

	if (rest.empty()) return false;
	size_t numEnd = 0;
	while (numEnd < rest.size() && (std::isdigit((unsigned char)rest[numEnd]) || rest[numEnd] == '.')) numEnd++;
	if (numEnd == 0) return false;

	float value;
	try { value = std::stof(rest.substr(0, numEnd)); }
	catch (...) { return false; }

	std::string unit = rest.substr(numEnd);
	if (field == SearchFilter::Field::Length) {
		if (unit == "m" || unit == "min") value *= 60.f;
		else if (!unit.empty() && unit != "s" && unit != "sec") return false;
	}
	else if (!unit.empty()) return false;

	out.field = field;
	out.op = op;
	out.value = value;
	return true;
}

struct SearchQuery {
	std::string text;  // lowercase plain-text portion, possibly empty
	std::vector<SearchFilter> filters;

	bool empty() const {
		return text.empty() && filters.empty();
	}

	// Returns true if meta satisfies every numeric filter. Files with an
	// unknown (zero) value for a filtered field never match that filter.
	bool matchesMetadata(const SampleMetadata& meta) const {
		for (const SearchFilter& f : filters) {
			float v = (f.field == SearchFilter::Field::Bpm) ? meta.bpm : meta.durationSeconds;
			if (v <= 0.f) return false;
			float eps = (f.field == SearchFilter::Field::Bpm) ? 0.5f : 0.005f;
			switch (f.op) {
				case SearchFilter::Op::Eq: if (std::abs(v - f.value) > eps) return false; break;
				case SearchFilter::Op::Lt: if (!(v < f.value)) return false; break;
				case SearchFilter::Op::Le: if (!(v <= f.value)) return false; break;
				case SearchFilter::Op::Gt: if (!(v > f.value)) return false; break;
				case SearchFilter::Op::Ge: if (!(v >= f.value)) return false; break;
			}
		}
		return true;
	}
};

inline SearchQuery parseSearchQuery(const std::string& query) {
	SearchQuery q;
	std::istringstream iss(query);
	std::string token;
	while (iss >> token) {
		std::string lower = rack::string::lowercase(token);
		SearchFilter f;
		if (parseSearchFilter(lower, f)) {
			q.filters.push_back(f);
		}
		else {
			if (!q.text.empty()) q.text += " ";
			q.text += lower;
		}
	}
	return q;
}


struct DataSourceNode {
	std::string name;
	std::string relativePath;  // relative to root, '/' separated; used as id
	bool isContainer = false;
	float durationSeconds = 0.f;
	std::vector<DataSourceNode> children;
};

struct DataSource {
	virtual ~DataSource() = default;

	virtual std::string rootId() const = 0;
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
	// - Own name always checked against the text portion of the query.
	// - Containers: matches if their own name matches, or any descendant file
	//   (via metadata) matches both the text portion and any numeric filters.
	// - Files: own name or any ancestor directory name must match the text
	//   portion, and the file's metadata must satisfy any numeric filters
	//   (bpm:/length: etc.) — see SearchQuery.
	virtual bool matchesSearch(const std::string& relativePath, bool isContainer, const SearchQuery& query) {
		if (query.empty()) return true;

		size_t lastSlash = relativePath.rfind('/');
		std::string ownName = (lastSlash != std::string::npos) ? relativePath.substr(lastSlash + 1) : relativePath;

		if (!isContainer) {
			bool textMatch = query.text.empty()
				|| rack::string::lowercase(ownName).find(query.text) != std::string::npos;
			if (!textMatch) {
				// Ancestor directory names are encoded in the relative path
				std::string p = relativePath;
				size_t pos;
				while ((pos = p.find('/')) != std::string::npos) {
					std::string component = p.substr(0, pos);
					if (rack::string::lowercase(component).find(query.text) != std::string::npos) {
						textMatch = true;
						break;
					}
					p = p.substr(pos + 1);
				}
			}
			if (!textMatch) return false;
			if (query.filters.empty()) return true;

			MetadataStore* meta = getMetadata();
			if (!meta) return false;
			auto it = meta->samples.find(relativePath);
			if (it == meta->samples.end()) return false;
			return query.matchesMetadata(it->second);
		}

		// Container: own name match is sufficient only when there are no
		// numeric filters to satisfy against a descendant.
		if (query.filters.empty() && (query.text.empty() || rack::string::lowercase(ownName).find(query.text) != std::string::npos)) {
			return true;
		}

		// Known descendant files via metadata
		MetadataStore* meta = getMetadata();
		if (!meta) return false;
		const std::string dirPrefix = relativePath + "/";
		for (const auto& pair : meta->samples) {
			if (pair.first.compare(0, dirPrefix.size(), dirPrefix) != 0) continue;
			size_t s = pair.first.rfind('/');
			std::string name = (s != std::string::npos) ? pair.first.substr(s + 1) : pair.first;
			if (!query.text.empty() && rack::string::lowercase(name).find(query.text) == std::string::npos) {
				continue;
			}
			if (!query.matchesMetadata(pair.second)) continue;
			return true;
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
	//   repitchSemitones — shift pitch by this many semitones without changing duration (0 = off)
	//   alwaysCopy       — force a copy of the source into outputDir even when no
	//                      transformation is needed. Implementations should treat this
	//                      as a no-op when outputDir is empty (copying onto itself is
	//                      pointless) and may return the source path unchanged in that
	//                      case.
	virtual std::function<std::string()> prepareForDrop(const std::string& id, bool convertToWav,
			int targetSampleRate = 0, float trimIn = 0.f, float trimOut = 1.f, int resampleQuality = 6,
			const std::string& outputDir = "", bool loopOnDrop = false, float loopCrossfadeDuration = 8.f,
			float repitchSemitones = 0.f, bool alwaysCopy = false) {
		// Base default: always returns the source id. Concrete sources are expected
		// to override this method to honour alwaysCopy by producing a copy in
		// outputDir when requested.
		return [id]() { return id; };
	}

	// Reconstruct a DataSourceNode from a stored relative path (e.g. for patch restore).
	virtual DataSourceNode resolveNode(const std::string& relativePath) const {
		return DataSourceNode{};
	}

	// Human-readable display name for an item id (e.g. filename without directory).
	virtual std::string getDisplayName(const std::string& id) const { return id; }

	// Relative path within this source (used for metadata key lookup).
	virtual std::string getRelativePath(const std::string& id) const { return id; }

	// Modification timestamp for cache invalidation. 0 = caching disabled.
	virtual int64_t getTimestamp(const std::string& id) const { return 0; }

	// Path to the waveform cache file for `id`. Single source of truth for
	// where this item's cache lives — callers must not derive or guess this
	// path themselves. Virtual so subclasses (e.g. tests) can redirect.
	virtual std::string cacheFilePathFor(const std::string& id) const {
		return sirenCacheDirPath() + "/" + hashPath(id) + ".json";
	}

	// Load a previously-built waveform cache for `id`, if one exists and is
	// still valid for `timestamp` (0 disables timestamp validation).
	virtual bool loadWaveformCache(const std::string& id, int64_t timestamp, AudioWaveformCache& out) const {
		return loadWaveformCacheFile(cacheFilePathFor(id), timestamp, out);
	}

	// Persist a built waveform cache for `id` to disk, creating the cache
	// directory if necessary.
	virtual void saveWaveformCache(const std::string& id, const AudioWaveformCache& cache) const {
		std::string path = cacheFilePathFor(id);
		vcv::fs::createDirectories(vcv::fs::getDirectory(path));
		saveWaveformCacheFile(path, cache);
	}

	// Load audio header metadata only (fast, no PCM decode).
	virtual bool loadAudioInfo(const std::string& id, AudioInfo& out) const { return false; }

	// Open a streaming decoder for the given item id.
	// Returns nullptr if streaming is unsupported or the item cannot be opened.
	virtual std::unique_ptr<AudioStream> openAudioStream(const std::string& id) const {
		return nullptr;
	}

	// Build a waveform cache by streaming audio frames — avoids full decode into memory.
	virtual bool buildWaveformCache(const std::string& id, int64_t timestamp,
			int pixelWidth, AudioWaveformCache& out) const {
		auto stream = openAudioStream(id);
		if (!stream) return false;
		return ::StoermelderPackOne::Siren::buildWaveformCache(timestamp, *stream, pixelWidth, out);
	}
};

namespace filesystem {
	// Defined in SirenFileSystem.hpp. Declared here so createDataSource()
	// below can dispatch to it without a circular include.
	std::shared_ptr<DataSource> createDataSource(const RootContainer& root);
}

// Creates the DataSource implementation for `root`, dispatching by
// root.type (mirroring createRootContainer() above). "fs" (the default,
// and currently the only type) maps to filesystem::createDataSource.
inline std::shared_ptr<DataSource> createDataSource(const RootContainer& root) {
	if (root.type == "fs") return filesystem::createDataSource(root);
	return nullptr;
}



// Shift the pitch of `samples` by `semitones` while preserving duration, using
// the SoundTouch library. `samples` is interleaved float PCM with `channels`
// channels at `sampleRate`. A no-op (0 semitones) returns immediately.
inline void applyRepitch(std::vector<float>& samples, int channels, int sampleRate, float semitones) {
	if (channels <= 0 || sampleRate <= 0 || samples.empty()) return;
	if (semitones == 0.f) return;

	soundtouch::SoundTouch st;
	st.setSampleRate((uint)sampleRate);
	st.setChannels((uint)channels);
	st.setPitchSemiTones(semitones);
	st.setTempo(1.0);

	int64_t totalFrames = (int64_t)(samples.size() / (size_t)channels);
	std::vector<float> out;
	out.reserve(samples.size());

	const uint blockFrames = 4096;
	std::vector<float> block(blockFrames * (size_t)channels);
	for (int64_t pos = 0; pos < totalFrames; pos += blockFrames) {
		uint n = (uint)std::min((int64_t)blockFrames, totalFrames - pos);
		std::copy(samples.begin() + (size_t)(pos * channels),
			samples.begin() + (size_t)((pos + n) * channels), block.begin());
		st.putSamples(block.data(), n);

		uint received;
		while ((received = st.receiveSamples(block.data(), blockFrames)) > 0) {
			out.insert(out.end(), block.begin(), block.begin() + (size_t)(received * channels));
		}
	}
	st.flush();
	uint received;
	while ((received = st.receiveSamples(block.data(), blockFrames)) > 0) {
		out.insert(out.end(), block.begin(), block.begin() + (size_t)(received * channels));
	}
	samples = std::move(out);
}

// Decode the trimmed region from src/id and shift its pitch by `semitones`
// without changing its duration. Runs on the worker thread.
// trimIn/trimOut are normalised [0, 1] over the full file. Preserves the
// source's full channel count (e.g. 5.1 surround stays 6-channel) — playback
// downmixes to stereo at the fill-thread stage, not here, and applyRepitch()
// is channel-count agnostic.
inline AudioPreviewResult buildRepitchPreview(DataSource& src, const std::string& id,
		float trimIn, float trimOut, float semitones) {
	AudioPreviewResult result;

	AudioInfo info;
	if (!src.loadAudioInfo(id, info)) return result;
	if (info.channels <= 0 || info.sampleRate <= 0 || info.frameCount <= 0) return result;

	int64_t startFrame = (int64_t)(trimIn * (float)info.frameCount);
	int64_t endFrame = (int64_t)(trimOut * (float)info.frameCount);
	startFrame = std::max((int64_t)0, std::min(startFrame, info.frameCount));
	endFrame = std::max(startFrame, std::min(endFrame, info.frameCount));
	int64_t trimFrames = endFrame - startFrame;
	if (trimFrames <= 0) return result;

	int ch = info.channels;
	result.samples.resize((size_t)(trimFrames * ch));

	auto stream = src.openAudioStream(id);
	if (!stream) return result;
	stream->seekTo(startFrame);
	int64_t framesRead = stream->readF32(result.samples.data(), trimFrames);
	if (framesRead <= 0) return result;
	result.samples.resize((size_t)(framesRead * ch));

	applyRepitch(result.samples, ch, info.sampleRate, semitones);
	if (result.samples.empty()) return result;

	result.channels = ch;
	result.sampleRate = info.sampleRate;
	result.durationSeconds = (float)(result.samples.size() / (size_t)ch) / (float)info.sampleRate;
	result.ok = true;
	return result;
}


// Generic declick: zero out the first and last frame of every channel.
// NOTE: this is NOT used for loop export. Loop files are left pristine — the
// applyLoopCrossfade seam is already continuous, and fading the ends to zero would
// only trade the boundary click for an audible low-frequency "bump" (an amplitude
// hole) on steep material with no quiet zero crossing. Forcing a single endpoint
// sample to zero only makes sense when the file is played once (a hard start/end).
// Kept as a small utility / fallback building block.
inline void applyDeclick(std::vector<float>& samples, int channels) {
	if (channels <= 0 || samples.empty()) return;
	int64_t N = (int64_t)(samples.size() / (size_t)channels);
	if (N < 2) return;

	for (int ch = 0; ch < channels; ch++) {
		samples[(size_t)ch] = 0.f;
		samples[(size_t)((N - 1) * channels + ch)] = 0.f;
	}
}

// Declick for trimmed (non-loop) files: search for zero crossings within a 5 ms
// window at the start and end and trim the buffer to those crossing points.
// This avoids waveform modification — the audio simply starts/ends where the
// signal naturally crosses zero. Falls back to applyDeclick (short cosine fade)
// for any boundary where no zero crossing exists within the search window.
inline void applyDeclickZeroCross(std::vector<float>& samples, int channels, int sampleRate) {
	if (channels <= 0 || sampleRate <= 0 || samples.empty()) return;
	int64_t N = (int64_t)(samples.size() / (size_t)channels);
	if (N < 4) return;

	const int64_t window = std::min((int64_t)(sampleRate / 200), N / 4);  // 5 ms

	// Trim start: advance to the first zero crossing (where signal crosses zero on ch 0).
	bool foundStart = false;
	for (int64_t f = 1; f < window; f++) {
		float prev = samples[(size_t)((f - 1) * channels)];
		float cur  = samples[(size_t)(f * channels)];
		if (prev * cur <= 0.f) {
			samples.erase(samples.begin(), samples.begin() + (size_t)(f * channels));
			N -= f;
			foundStart = true;
			break;
		}
	}

	// Trim end: retreat to the last zero crossing searching backward.
	bool foundEnd = false;
	for (int64_t f = 1; f < window; f++) {
		int64_t pos = N - f;
		if (pos <= 0) break;
		float prev = samples[(size_t)((pos - 1) * channels)];
		float cur  = samples[(size_t)(pos * channels)];
		if (prev * cur <= 0.f) {
			samples.resize((size_t)(pos * channels));
			N = pos;
			foundEnd = true;
			break;
		}
	}

	// For any boundary with no crossing in the window, fall back to a 5 ms
	// cosine fade. applyDeclick uses only 16 samples (correct for loop files
	// where the boundary is already near-zero); re-implement the longer fade here.
	if (!foundStart || !foundEnd) {
		int64_t Nfb = (int64_t)(samples.size() / (size_t)channels);
		int64_t fadeFrames = std::min((int64_t)(sampleRate / 200), Nfb / 4);  // 5 ms
		if (fadeFrames >= 2) {
			for (int64_t f = 0; f < fadeFrames; f++) {
				float gain = std::sin((float)f / (float)(fadeFrames - 1) * float(M_PI) * 0.5f);
				for (int ch = 0; ch < channels; ch++) {
					if (!foundStart) samples[(size_t)(f * channels + ch)] *= gain;
					if (!foundEnd)   samples[(size_t)((Nfb - 1 - f) * channels + ch)] *= gain;
				}
			}
		}
	}
}


// Loop crossfade post-processing
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
		bool foundCrossing = false;
		// Fallback for material with no zero crossing in the window (e.g. a DC
		// offset): remember the sample closest to zero so the seam still lands at
		// the smallest possible discontinuity instead of an arbitrary jump at the
		// exact midpoint.
		float bestAbs = std::numeric_limits<float>::max();
		int64_t bestAbsPos = mid;
		for (int64_t d = 0; d <= searchWindow; d++) {
			for (int sign : {-1, 1}) {
				int64_t pos = mid + sign * d;
				if (pos <= 0 || pos >= N - 1) continue;
				float cur = samples[(size_t)(pos * channels)];
				float prev = samples[(size_t)((pos - 1) * channels)];
				float a = std::abs(cur);
				if (a < bestAbs) { bestAbs = a; bestAbsPos = pos; }
				if (cur * prev <= 0.f) {
					float score = a + std::abs(prev);
					if (score < bestScore) {
						bestScore = score;
						M = pos;
						foundCrossing = true;
					}
				}
			}
			if (foundCrossing && bestScore < 1e-5f) break;  // essentially silent — can't do better
		}
		if (!foundCrossing) M = bestAbsPos;
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
	int64_t outN = part1Len + C + part2Len; // == N - C

	std::vector<float> out((size_t)(outN * channels));

	// Part 1: original[M .. N-C-1]
	for (int64_t f = 0; f < part1Len; f++) {
		for (int ch = 0; ch < channels; ch++) {
			out[(size_t)(f * channels + ch)] = samples[(size_t)((M + f) * channels + ch)];
		}
	}

	// Crossfade: blend tail of second half (fade-out) with head of first half (fade-in)
	for (int64_t f = 0; f < C; f++) {
		float alpha = (C > 1) ? (float)f / (float)(C - 1) : 1.f;
		float angle = alpha * float(M_PI) * 0.5f;
		float fadeOut = std::cos(angle);
		float fadeIn = std::sin(angle);
		for (int ch = 0; ch < channels; ch++) {
			float s1 = samples[(size_t)((N - C + f) * channels + ch)];
			float s2 = samples[(size_t)(f * channels + ch)];
			out[(size_t)((part1Len + f) * channels + ch)] = s1 * fadeOut + s2 * fadeIn;
		}
	}

	// Part 2: original[C .. M-1]
	for (int64_t f = 0; f < part2Len; f++) {
		for (int ch = 0; ch < channels; ch++) {
			out[(size_t)((part1Len + C + f) * channels + ch)] = samples[(size_t)((C + f) * channels + ch)];
		}
	}

	samples = std::move(out);
}

// Decode the trimmed region from src/id, apply rotation+crossfade, and return
// the result as an in-memory buffer. Runs on the worker thread.
// trimIn/trimOut are normalised [0, 1] over the full file. Preserves the
// source's full channel count (e.g. 5.1 surround stays 6-channel) — playback
// downmixes to stereo at the fill-thread stage, not here, and
// applyLoopCrossfade() is channel-count agnostic.
inline AudioPreviewResult buildLoopPreview(DataSource& src, const std::string& id,
		float trimIn, float trimOut, float crossfadeDuration) {
	AudioPreviewResult result;

	AudioInfo info;
	if (!src.loadAudioInfo(id, info)) return result;
	if (info.channels <= 0 || info.sampleRate <= 0 || info.frameCount <= 0) return result;

	int64_t startFrame = (int64_t)(trimIn * (float)info.frameCount);
	int64_t endFrame = (int64_t)(trimOut * (float)info.frameCount);
	startFrame = std::max((int64_t)0, std::min(startFrame, info.frameCount));
	endFrame = std::max(startFrame, std::min(endFrame, info.frameCount));
	int64_t trimFrames = endFrame - startFrame;
	if (trimFrames <= 0) return result;

	int ch = info.channels;
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
