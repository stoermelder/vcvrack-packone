#pragma once
#include "../../pluginsettings.hpp"
#include <rack.hpp>
#include <ghc/filesystem.hpp>


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
	// samples[channel][i] = sample value [-1, 1], decimated for display
	std::vector<std::vector<float>> samples;
	int sampleCount = 0;
	int64_t fileTimestamp = 0;

	bool empty() const { return samples.empty() || sampleCount == 0; }
};

// ─── file timestamp ───────────────────────────────────────────────────────────

inline int64_t getFileTimestamp(const std::string& path) {
	try {
		auto ft = ghc::filesystem::last_write_time(path);
		return ft.time_since_epoch().count();
	}
	catch (...) { return 0; }
}

// ─── base64 helpers ───────────────────────────────────────────────────────────

static const char B64_ENC[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string b64Encode(const uint8_t* data, size_t len) {
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	for (size_t i = 0; i < len; i += 3) {
		uint32_t n = (uint32_t)data[i] << 16;
		if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
		if (i + 2 < len) n |= (uint32_t)data[i + 2];
		out += B64_ENC[(n >> 18) & 63];
		out += B64_ENC[(n >> 12) & 63];
		out += (i + 1 < len) ? B64_ENC[(n >> 6) & 63] : '=';
		out += (i + 2 < len) ? B64_ENC[n        & 63] : '=';
	}
	return out;
}

inline std::vector<uint8_t> b64Decode(const std::string& s) {
	// Build decode table on first call
	static int8_t lut[256] = {};
	static bool lutReady = false;
	if (!lutReady) {
		memset(lut, -1, sizeof(lut));
		for (int i = 0; i < 64; i++) lut[(uint8_t)B64_ENC[i]] = (int8_t)i;
		lutReady = true;
	}

	std::vector<uint8_t> out;
	out.reserve(s.size() / 4 * 3);
	uint32_t acc = 0;
	int bits = 0;
	for (char c : s) {
		if (c == '=') break;
		int8_t v = lut[(uint8_t)c];
		if (v < 0) continue;
		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((uint8_t)(acc >> bits));
		}
	}
	return out;
}

// ─── waveform cache file I/O ─────────────────────────────────────────────────
//
// JSON file with metadata fields + per-channel base64-encoded int16 sample data.
// Each channel's samples are scaled to [-32767, 32767], packed as little-endian
// int16, then base64-encoded into a JSON string.

// expectedTimestamp == 0 disables cache validation (always load if file exists).
inline bool loadWaveformCacheFile(const std::string& cachePath, int64_t expectedTimestamp, WaveformCache& out) {
	if (isTesting()) return false;
	FILE* f = fopen(cachePath.c_str(), "r");
	if (!f) return false;
	json_error_t error;
	json_t* rootJ = json_loadf(f, 0, &error);
	fclose(f);
	if (!rootJ) return false;
	DEFER({ json_decref(rootJ); });

	json_t* tsJ = json_object_get(rootJ, "timestamp");
	if (!tsJ) return false;
	int64_t storedTs = (int64_t)json_integer_value(tsJ);
	if (expectedTimestamp != 0 && storedTs != expectedTimestamp) return false;

	json_t* scJ = json_object_get(rootJ, "sampleCount");
	if (!scJ) return false;
	int sampleCount = (int)json_integer_value(scJ);
	if (sampleCount <= 0) return false;

	json_t* channelsJ = json_object_get(rootJ, "channels");
	if (!channelsJ || !json_is_array(channelsJ)) return false;

	out.fileTimestamp = storedTs;
	out.sampleCount   = sampleCount;
	out.samples.clear();

	size_t ch; json_t* chJ;
	json_array_foreach(channelsJ, ch, chJ) {
		if (!json_is_string(chJ)) return false;
		auto bytes = b64Decode(json_string_value(chJ));
		if ((int)bytes.size() < sampleCount * 2) return false;

		std::vector<float> chSamples(sampleCount);
		for (int i = 0; i < sampleCount; i++) {
			int16_t v;
			memcpy(&v, bytes.data() + i * 2, 2);
			chSamples[i] = v / 32767.f;
		}
		out.samples.push_back(std::move(chSamples));
	}
	return !out.samples.empty();
}

inline void saveWaveformCacheFile(const std::string& cachePath, const WaveformCache& cache) {
	if (isTesting()) return;
	if (cache.samples.empty() || cache.sampleCount == 0) return;

	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "timestamp",   json_integer(cache.fileTimestamp));
	json_object_set_new(rootJ, "sampleCount", json_integer(cache.sampleCount));

	json_t* channelsJ = json_array();
	std::vector<uint8_t> bytes((size_t)cache.sampleCount * 2);
	for (const auto& chSamples : cache.samples) {
		for (int i = 0; i < cache.sampleCount; i++) {
			int16_t v = (int16_t)(chSamples[i] * 32767.f);
			memcpy(bytes.data() + i * 2, &v, 2);
		}
		std::string encoded = b64Encode(bytes.data(), bytes.size());
		json_array_append_new(channelsJ, json_string(encoded.c_str()));
	}
	json_object_set_new(rootJ, "channels", channelsJ);

	FILE* f = fopen(cachePath.c_str(), "w");
	if (f) {
		json_dumpf(rootJ, f, 0);
		fclose(f);
	}
	json_decref(rootJ);
}

} // namespace Siren
} // namespace StoermelderPackOne
