#pragma once
#include <rack.hpp>
#include <ghc/filesystem.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

// dr_libs are implemented once in Siren.cpp via #define DR_*_IMPLEMENTATION
#include "../../../dep/drlibs/dr_wav.h"
#include "../../../dep/drlibs/dr_flac.h"
#include "../../../dep/drlibs/dr_mp3.h"

namespace StoermelderPackOne {
namespace Siren {

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
	int64_t fileTimestamp = 0;  // ghc::filesystem::last_write_time epoch count

	bool empty() const { return peaks.empty() || bucketCount == 0; }
};

// Returns the mtime epoch count for a file path, 0 on error
inline int64_t getFileTimestamp(const std::string& path) {
	try {
		auto ft = ghc::filesystem::last_write_time(path);
		return ft.time_since_epoch().count();
	}
	catch (...) { return 0; }
}

// Decode audio header only (fast metadata)
inline bool loadAudioInfo(const std::string& path, AudioInfo& out) {
	std::string ext = rack::system::getExtension(rack::system::getFilename(path));
	for (char& c : ext) c = (char)tolower(c);

	if (ext == ".wav") {
		drwav wav;
		if (!drwav_init_file(&wav, path.c_str(), nullptr)) return false;
		out.sampleRate = (int)wav.sampleRate;
		out.channels = (int)wav.channels;
		out.bitDepth = (int)wav.bitsPerSample;
		out.frameCount = (int64_t)wav.totalPCMFrameCount;
		out.durationSeconds = wav.sampleRate > 0 ? (float)wav.totalPCMFrameCount / (float)wav.sampleRate : 0.f;
		drwav_uninit(&wav);
		return true;
	}
	if (ext == ".flac") {
		drflac* flac = drflac_open_file(path.c_str(), nullptr);
		if (!flac) return false;
		out.sampleRate = (int)flac->sampleRate;
		out.channels = (int)flac->channels;
		out.bitDepth = (int)flac->bitsPerSample;
		out.frameCount = (int64_t)flac->totalPCMFrameCount;
		out.durationSeconds = flac->sampleRate > 0 ? (float)flac->totalPCMFrameCount / (float)flac->sampleRate : 0.f;
		drflac_close(flac);
		return true;
	}
	if (ext == ".mp3") {
		drmp3 mp3;
		if (!drmp3_init_file(&mp3, path.c_str(), nullptr)) return false;
		out.sampleRate = (int)mp3.sampleRate;
		out.channels = (int)mp3.channels;
		out.bitDepth = 16;
		out.frameCount = (int64_t)drmp3_get_pcm_frame_count(&mp3);
		out.durationSeconds = mp3.sampleRate > 0 ? (float)out.frameCount / (float)mp3.sampleRate : 0.f;
		drmp3_uninit(&mp3);
		return true;
	}
	return false;
}

// Decode full file and compute peak waveform for pixelWidth display buckets
inline bool buildWaveformCache(const std::string& path, int pixelWidth, WaveformCache& out) {
	if (pixelWidth <= 0) return false;

	std::string ext = rack::system::getExtension(rack::system::getFilename(path));
	for (char& c : ext) c = (char)tolower(c);

	int channels = 0;
	int64_t frameCount = 0;
	std::vector<float> samples;  // interleaved, normalized [-1,1]

	if (ext == ".wav") {
		drwav wav;
		if (!drwav_init_file(&wav, path.c_str(), nullptr)) return false;
		channels = (int)wav.channels;
		frameCount = (int64_t)wav.totalPCMFrameCount;
		samples.resize((size_t)(frameCount * channels));
		drwav_read_pcm_frames_f32(&wav, (drwav_uint64)frameCount, samples.data());
		drwav_uninit(&wav);
	}
	else if (ext == ".flac") {
		drflac* flac = drflac_open_file(path.c_str(), nullptr);
		if (!flac) return false;
		channels = (int)flac->channels;
		frameCount = (int64_t)flac->totalPCMFrameCount;
		samples.resize((size_t)(frameCount * channels));
		drflac_read_pcm_frames_f32(flac, (drflac_uint64)frameCount, samples.data());
		drflac_close(flac);
	}
	else if (ext == ".mp3") {
		drmp3 mp3;
		if (!drmp3_init_file(&mp3, path.c_str(), nullptr)) return false;
		channels = (int)mp3.channels;
		frameCount = (int64_t)drmp3_get_pcm_frame_count(&mp3);
		samples.resize((size_t)(frameCount * channels));
		drmp3_read_pcm_frames_f32(&mp3, (drmp3_uint64)frameCount, samples.data());
		drmp3_uninit(&mp3);
	}
	else {
		return false;
	}

	if (frameCount == 0 || channels == 0) return false;

	out.bucketCount = pixelWidth;
	out.peaks.assign(channels, std::vector<std::pair<float, float>>(pixelWidth, {0.f, 0.f}));
	out.fileTimestamp = getFileTimestamp(path);

	double framesPerBucket = (double)frameCount / (double)pixelWidth;
	for (int ch = 0; ch < channels; ch++) {
		for (int b = 0; b < pixelWidth; b++) {
			int64_t start = (int64_t)(b * framesPerBucket);
			int64_t end = (int64_t)((b + 1) * framesPerBucket);
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

// Load waveform cache from JSON file; returns false if missing or timestamp mismatch
inline bool loadWaveformCacheFile(const std::string& cacheJsonPath, const std::string& audioPath, WaveformCache& out) {
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
	int64_t currentTs = getFileTimestamp(audioPath);
	if (storedTs != currentTs || currentTs == 0) return false;

	json_t* bucketsJ = json_object_get(rootJ, "bucketCount");
	if (!bucketsJ) return false;
	out.bucketCount = (int)json_integer_value(bucketsJ);
	out.fileTimestamp = storedTs;
	out.peaks.clear();

	json_t* channelsJ = json_object_get(rootJ, "channels");
	if (!channelsJ || !json_is_array(channelsJ)) return false;
	size_t ch;
	json_t* chJ;
	json_array_foreach(channelsJ, ch, chJ) {
		std::vector<std::pair<float, float>> chPeaks;
		if (!json_is_array(chJ)) continue;
		size_t b;
		json_t* bucketJ;
		json_array_foreach(chJ, b, bucketJ) {
			if (!json_is_array(bucketJ) || json_array_size(bucketJ) < 2) {
				chPeaks.push_back({0.f, 0.f});
				continue;
			}
			float mn = (float)json_real_value(json_array_get(bucketJ, 0));
			float mx = (float)json_real_value(json_array_get(bucketJ, 1));
			chPeaks.push_back({mn, mx});
		}
		out.peaks.push_back(std::move(chPeaks));
	}
	return !out.peaks.empty();
}

// Save waveform cache to JSON file
inline void saveWaveformCacheFile(const std::string& cacheJsonPath, const WaveformCache& cache) {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "timestamp", json_integer(cache.fileTimestamp));
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

	// Ensure parent directory exists
	FILE* file = fopen(cacheJsonPath.c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}
	json_decref(rootJ);
}

} // namespace Siren
} // namespace StoermelderPackOne
