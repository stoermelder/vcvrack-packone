#pragma once
#include <cstdint>

namespace StoermelderPackOne {
namespace Siren {

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

} // namespace Siren
} // namespace StoermelderPackOne