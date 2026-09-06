#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace StoermelderPackOne {
namespace Siren {

// AudioStream
// Streaming decoder kept open by the data source — no full-file buffering needed.
struct AudioStream {
	virtual ~AudioStream() = default;
	virtual int channels() const = 0;
	virtual int sampleRate() const = 0;
	virtual int64_t totalFrames() const = 0;  // 0 if unknown (e.g. live stream)

	// Read up to frameCount interleaved PCM frames normalised to [-1, 1].
	// Returns the number of frames actually read (< frameCount at EOF).
	virtual int64_t readF32(float* buffer, int64_t frameCount) = 0;

	// Seek to an absolute frame index. Returns true on success.
	virtual bool seekTo(int64_t frameIndex) = 0;
};

// MemoryAudioStream
// Holds a fully decoded PCM buffer in memory — used for loop preview so the
// fill thread can stream from processed audio without touching the filesystem.
// Thread-safe for concurrent readF32/seekTo once `samples` is assigned,
// because readPos is only written by the fill thread after stream adoption.
struct MemoryAudioStream : AudioStream {
	std::vector<float> samples;  // interleaved float PCM
	int ch = 0;
	int sr = 0;
	int64_t readPos = 0;

	int channels() const override { return ch; }
	int sampleRate() const override { return sr; }
	int64_t totalFrames() const override {
		return (ch > 0 && !samples.empty()) ? (int64_t)(samples.size() / (size_t)ch) : 0;
	}

	int64_t readF32(float* buf, int64_t n) override {
		int64_t avail = std::min(n, totalFrames() - readPos);
		if (avail <= 0) return 0;
		std::memcpy(buf, samples.data() + (size_t)(readPos * ch), (size_t)(avail * ch) * sizeof(float));
		readPos += avail;
		return avail;
	}

	bool seekTo(int64_t f) override {
		int64_t total = totalFrames();
		readPos = (f < 0) ? 0 : (f > total ? total : f);
		return true;
	}
};

} // namespace Siren
} // namespace StoermelderPackOne