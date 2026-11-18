#pragma once
#include <rack.hpp>
#include <sstream>
#include "../../utils/MpmcTaskWorker.hpp"
#include "../../vcv/api.hpp"
#include "SirenPaths.hpp"
#include "SirenMetadata.hpp"
#include "SirenAudioStream.hpp"
#include "SirenAudio.hpp"
#include "SirenRender.hpp"

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

	// Top-level children of an item id (rootId() for the root level). Blocking: callers
	// that must not stall the UI run it on a worker and keep the DataSource alive via a
	// shared_ptr. withAudioInfo controls whether each file's audio
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



// Loop / repitch previews: render the trimmed region at the source rate.
inline AudioPreviewResult buildRepitchPreview(DataSource& src, const std::string& id,
		float trimIn, float trimOut, float semitones) {
	auto stream = src.openAudioStream(id);
	if (!stream) return AudioPreviewResult();
	RenderSpec spec;
	spec.trimIn = trimIn;
	spec.trimOut = trimOut;
	spec.repitchSemitones = semitones;
	return renderRegion(*stream, spec);
}

inline AudioPreviewResult buildLoopPreview(DataSource& src, const std::string& id,
		float trimIn, float trimOut, float crossfadeDuration) {
	auto stream = src.openAudioStream(id);
	if (!stream) return AudioPreviewResult();
	RenderSpec spec;
	spec.trimIn = trimIn;
	spec.trimOut = trimOut;
	spec.loop = true;
	spec.loopCrossfadeSecs = crossfadeDuration;
	return renderRegion(*stream, spec);
}


} // namespace Siren
} // namespace StoermelderPackOne
