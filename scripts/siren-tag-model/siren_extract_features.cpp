// CLI tool: read audio files, extract features, write CSV to stdout.
//
// Usage:
//   siren_extract_features <file> [file ...]
//
// Output (stdout):
//   path,f0,f1,...,f9
//   /abs/path/to/kick.wav,0.12,0.45,...
//
// Errors (skipped files) go to stderr; exit code is 0 if at least one
// file was processed successfully, 1 if all files failed.
//
// This tool exists so training and plugin inference use the exact same
// C++ extractFeatures() — no Python twin to drift.

// ── drlibs implementations (header-only, define once) ────────────────────────
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunused-function"
#  pragma clang diagnostic ignored "-Wunused-variable"
#endif
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "../../../dep/drlibs/dr_wav.h"
#include "../../../dep/drlibs/dr_flac.h"
#include "../../../dep/drlibs/dr_mp3.h"
#ifdef __clang__
#  pragma clang diagnostic pop
#endif
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

// ── Feature extractor (no Rack dependency via SirenAudioStream.hpp) ──────────
#include "../../../src/modules/siren/SirenTagClassifierApi.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

using namespace StoermelderPackOne::Siren;

// ── Concrete AudioStream backed by dr_wav / dr_flac / dr_mp3 ─────────────────

static std::string file_ext(const std::string& path) {
	auto p = path.rfind('.');
	if (p == std::string::npos) return "";
	std::string e = path.substr(p + 1);
	std::transform(e.begin(), e.end(), e.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return e;
}

struct CliAudioStream : AudioStream {
	enum Fmt { WAV, FLAC, MP3 } fmt = WAV;
	drwav   wav  = {};
	drflac* flac = nullptr;
	drmp3   mp3  = {};
	int     ch_    = 0;
	int     sr_    = 0;
	int64_t total_ = 0;
	bool    open_  = false;

	bool open(const std::string& path) {
		std::string e = file_ext(path);
		if (e == "wav") {
			if (!drwav_init_file(&wav, path.c_str(), nullptr)) return false;
			fmt    = WAV;
			ch_    = (int)wav.channels;
			sr_    = (int)wav.sampleRate;
			total_ = (int64_t)wav.totalPCMFrameCount;
		}
		else if (e == "flac") {
			flac = drflac_open_file(path.c_str(), nullptr);
			if (!flac) return false;
			fmt    = FLAC;
			ch_    = (int)flac->channels;
			sr_    = (int)flac->sampleRate;
			total_ = (int64_t)flac->totalPCMFrameCount;
		}
		else if (e == "mp3") {
			if (!drmp3_init_file(&mp3, path.c_str(), nullptr)) return false;
			fmt    = MP3;
			ch_    = (int)mp3.channels;
			sr_    = (int)mp3.sampleRate;
			total_ = (int64_t)drmp3_get_pcm_frame_count(&mp3);
		}
		else {
			return false;
		}
		open_ = true;
		return true;
	}

	~CliAudioStream() override {
		if (!open_) return;
		if      (fmt == WAV)            drwav_uninit(&wav);
		else if (fmt == FLAC && flac)   drflac_close(flac);
		else if (fmt == MP3)            drmp3_uninit(&mp3);
	}

	int     channels()    const override { return ch_; }
	int     sampleRate()  const override { return sr_; }
	int64_t totalFrames() const override { return total_; }

	int64_t readF32(float* buf, int64_t n) override {
		if (fmt == WAV)  return (int64_t)drwav_read_pcm_frames_f32(&wav,  (drwav_uint64)n, buf);
		if (fmt == FLAC) return (int64_t)drflac_read_pcm_frames_f32(flac, (drflac_uint64)n, buf);
		if (fmt == MP3)  return (int64_t)drmp3_read_pcm_frames_f32(&mp3,  (drmp3_uint64)n, buf);
		return 0;
	}

	bool seekTo(int64_t frame) override {
		if (fmt == WAV)  return !!drwav_seek_to_pcm_frame(&wav,  (drwav_uint64)frame);
		if (fmt == FLAC) return !!drflac_seek_to_pcm_frame(flac, (drflac_uint64)frame);
		if (fmt == MP3)  return !!drmp3_seek_to_pcm_frame(&mp3,  (drmp3_uint64)frame);
		return false;
	}
};

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr,
			"Usage: siren_extract_features <file> [file ...]\n"
			"Output: CSV to stdout — path,f0,f1,...,f%d\n",
			SIREN_TAG_NUM_FEATURES - 1);
		return 1;
	}

	// Header
	std::printf("path");
	for (int i = 0; i < SIREN_TAG_NUM_FEATURES; ++i)
		std::printf(",f%d", i);
	std::printf("\n");

	int ok = 0, failed = 0;
	for (int i = 1; i < argc; ++i) {
		const char* path = argv[i];
		CliAudioStream stream;
		if (!stream.open(path)) {
			std::fprintf(stderr, "skip: cannot open %s\n", path);
			++failed;
			continue;
		}

		float features[SIREN_TAG_NUM_FEATURES] = {};
		TagClassifier::extractFeatures(stream, features);

		std::printf("%s", path);
		for (int j = 0; j < SIREN_TAG_NUM_FEATURES; ++j)
			std::printf(",%.8g", (double)features[j]);
		std::printf("\n");
		++ok;
	}

	return (ok == 0) ? 1 : 0;
}
