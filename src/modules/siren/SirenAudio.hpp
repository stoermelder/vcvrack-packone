#pragma once
#include "../../pluginsettings.hpp"
#include <rack.hpp>
#include <ghc/filesystem.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace StoermelderPackOne {
namespace Siren {

// ─── data structures ──────────────────────────────────────────────────────────

struct AudioInfo {
	int sampleRate = 0;
	int channels = 0;
	int bitDepth = 0;
	int64_t frameCount = 0;
	float durationSeconds = 0.f;
};

struct WaveformCache {
	// peaks[channel][bucket] = {min, max} normalized to [-1, 1]
	std::vector<std::vector<std::pair<float, float>>> peaks;
	int bucketCount = 0;
	int64_t fileTimestamp = 0;

	bool empty() const { return peaks.empty() || bucketCount == 0; }
};

// ─── file timestamp ───────────────────────────────────────────────────────────

inline int64_t getFileTimestamp(const std::string& path) {
	try {
		auto ft = ghc::filesystem::last_write_time(path);
		return ft.time_since_epoch().count();
	}
	catch (...) { return 0; }
}

// ─── waveform cache building (format-agnostic) ───────────────────────────────

// Build a peak waveform from pre-decoded interleaved float samples.
// timestamp is supplied by the caller (from the data source) for cache validation.
inline bool buildWaveformCache(int64_t timestamp,
                               const std::vector<float>& samples,
                               int64_t frameCount, int channels,
                               int pixelWidth, WaveformCache& out) {
	if (pixelWidth <= 0 || frameCount == 0 || channels == 0) return false;

	out.bucketCount = pixelWidth;
	out.peaks.assign(channels, std::vector<std::pair<float, float>>(pixelWidth, {0.f, 0.f}));
	out.fileTimestamp = timestamp;

	double framesPerBucket = (double)frameCount / (double)pixelWidth;
	for (int ch = 0; ch < channels; ch++) {
		for (int b = 0; b < pixelWidth; b++) {
			int64_t start = (int64_t)(b * framesPerBucket);
			int64_t end   = (int64_t)((b + 1) * framesPerBucket);
			if (end > frameCount) end = frameCount;
			float mn = 0.f, mx = 0.f;
			for (int64_t f = start; f < end; f++) {
				float s = samples[(size_t)(f * channels + ch)];
				if (s < mn) mn = s;
				if (s > mx) mx = s;
			}
			out.peaks[ch][b] = {mn, mx};
		}
	}
	return true;
}

// ─── waveform cache file I/O ─────────────────────────────────────────────────

// expectedTimestamp == 0 disables cache validation (always load if file exists).
inline bool loadWaveformCacheFile(const std::string& cacheJsonPath, int64_t expectedTimestamp, WaveformCache& out) {
	if (isTesting()) return false;
	FILE* file = fopen(cacheJsonPath.c_str(), "r");
	if (!file) return false;
	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	fclose(file);
	if (!rootJ) return false;
	DEFER({ json_decref(rootJ); });

	json_t* tsJ = json_object_get(rootJ, "timestamp");
	if (!tsJ) return false;
	int64_t storedTs = (int64_t)json_integer_value(tsJ);
	if (expectedTimestamp != 0 && storedTs != expectedTimestamp) return false;

	json_t* bucketsJ = json_object_get(rootJ, "bucketCount");
	if (!bucketsJ) return false;
	out.bucketCount = (int)json_integer_value(bucketsJ);
	out.fileTimestamp = storedTs;
	out.peaks.clear();

	json_t* channelsJ = json_object_get(rootJ, "channels");
	if (!channelsJ || !json_is_array(channelsJ)) return false;
	size_t ch; json_t* chJ;
	json_array_foreach(channelsJ, ch, chJ) {
		std::vector<std::pair<float, float>> chPeaks;
		if (!json_is_array(chJ)) continue;
		size_t b; json_t* bucketJ;
		json_array_foreach(chJ, b, bucketJ) {
			if (!json_is_array(bucketJ) || json_array_size(bucketJ) < 2) {
				chPeaks.push_back({0.f, 0.f}); continue;
			}
			float mn = (float)json_real_value(json_array_get(bucketJ, 0));
			float mx = (float)json_real_value(json_array_get(bucketJ, 1));
			chPeaks.push_back({mn, mx});
		}
		out.peaks.push_back(std::move(chPeaks));
	}
	return !out.peaks.empty();
}

inline void saveWaveformCacheFile(const std::string& cacheJsonPath, const WaveformCache& cache) {
	if (isTesting()) return;
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "timestamp",   json_integer(cache.fileTimestamp));
	json_object_set_new(rootJ, "bucketCount", json_integer(cache.bucketCount));

	json_t* channelsJ = json_array();
	for (const auto& chPeaks : cache.peaks) {
		json_t* chJ = json_array();
		for (const auto& p : chPeaks) {
			json_t* bucketJ = json_array();
			json_array_append_new(bucketJ, json_real(p.first));
			json_array_append_new(bucketJ, json_real(p.second));
			json_array_append_new(chJ, bucketJ);
		}
		json_array_append_new(channelsJ, chJ);
	}
	json_object_set_new(rootJ, "channels", channelsJ);

	FILE* file = fopen(cacheJsonPath.c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}
	json_decref(rootJ);
}

} // namespace Siren
} // namespace StoermelderPackOne
