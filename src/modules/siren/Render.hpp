#pragma once
#include <rack.hpp>
#include <algorithm>
#include <climits>
#include <vector>
#include "Audio.hpp"
#include "AudioStream.hpp"
#include <SoundTouch.h>

namespace StoermelderPackOne {
namespace Siren {

// Offline rendering of a trimmed region: the one pipeline behind the loop/repitch
// previews and the drag-and-drop export. Everything here is independent of DataSource.

// Resolve normalised [trimIn, trimOut] over `totalFrames` to a frame range.
// Returns false when the range is empty.
inline bool trimFrameRange(int64_t totalFrames, float trimIn, float trimOut, int64_t& start, int64_t& end) {
	start = (int64_t)(trimIn * (float)totalFrames);
	end = (int64_t)(trimOut * (float)totalFrames);
	start = std::max((int64_t)0, std::min(start, totalFrames));
	end = std::max(start, std::min(end, totalFrames));
	return end > start;
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

// Widest file the resampler is configured for; wider files keep their own rate
// rather than losing channels.
constexpr int RENDER_MAX_RESAMPLE_CHANNELS = 16;

struct RenderSpec {
	float trimIn = 0.f;            // normalised [0, 1] over the full file
	float trimOut = 1.f;
	float repitchSemitones = 0.f;  // 0 = off
	bool loop = false;             // rotate + crossfade so the region loops seamlessly
	float loopCrossfadeSecs = 8.f;
	int targetRate = 0;            // 0 = keep the source rate
	int resampleQuality = 6;       // speex 0..10
	bool declick = false;          // non-loop only: trim to zero crossings at both ends
};

// The pipeline stages, in the order renderRegion runs them. A caller may pass a vector
// to renderRegion to record which stages actually ran (used by tests).
enum class RenderStage {
	Decode,
	Repitch,
	Resample,
	LoopCrossfade,
	Declick
};

// Stage: read the trimmed region of `stream` into `out` (interleaved, full channel
// count). Returns false on an empty range or a failed read.
inline bool decodeStage(AudioStream& stream, const RenderSpec& spec, std::vector<float>& out) {
	const int ch = stream.channels();
	const int64_t total = stream.totalFrames();
	int64_t startFrame, endFrame;
	if (!trimFrameRange(total, spec.trimIn, spec.trimOut, startFrame, endFrame)) return false;
	int64_t trimFrames = endFrame - startFrame;

	out.resize((size_t)(trimFrames * ch));
	stream.seekTo(startFrame);
	int64_t framesRead = stream.readF32(out.data(), trimFrames);
	if (framesRead <= 0) { out.clear(); return false; }
	out.resize((size_t)(framesRead * ch));
	return true;
}

// Stage: convert `samples` from srcRate to outRate in place. The caller guarantees
// ch <= RENDER_MAX_RESAMPLE_CHANNELS.
inline void resampleStage(std::vector<float>& samples, int ch, int srcRate, int outRate, int quality) {
	int64_t inFrames = (int64_t)(samples.size() / (size_t)ch);
	int64_t outCapacity = (int64_t)((double)inFrames * outRate / srcRate) + 256;
	std::vector<float> resampled((size_t)(outCapacity * ch));
	dsp::SampleRateConverter<RENDER_MAX_RESAMPLE_CHANNELS> src;
	src.setChannels(ch);
	src.setRates(srcRate, outRate);
	// Clamp to speex's documented range; out-of-range values would assert / destroy
	// the resampler state silently in refreshState().
	src.setQuality(std::max(0, std::min(quality, 10)));
	int inF = (int)std::min(inFrames, (int64_t)INT_MAX);
	int outF = (int)std::min(outCapacity, (int64_t)INT_MAX);
	src.process(samples.data(), ch, &inF, resampled.data(), ch, &outF);
	resampled.resize((size_t)outF * ch);
	samples = std::move(resampled);
}

// Decode the trimmed region of `stream` and apply, in this order: repitch, resample,
// then loop crossfade (loop) or zero-crossing declick (declick). Keeps the source's
// full channel count; playback downmixes at the fill-thread stage. Runs on a worker.
//
// Repitch comes first: SoundTouch's window/hop time constants are tuned for the source
// rate, and pitch-shifting up can extend the spectrum above the source Nyquist —
// running SRC afterwards lets it anti-alias correctly. The loop and declick stages work
// at the output rate.
//
// Loop: the ends are left untouched. applyLoopCrossfade already placed the seam at two
// consecutive source frames, so the wrap is continuous in value and slope on every
// channel; fading the ends to zero would only trade the boundary click for an audible
// low-frequency "bump".
inline AudioPreviewResult renderRegion(AudioStream& stream, const RenderSpec& spec,
		std::vector<RenderStage>* trace = nullptr) {
	AudioPreviewResult result;
	auto ran = [&](RenderStage st) {
		if (trace) trace->push_back(st);
	};

	const int ch = stream.channels();
	const int srcRate = stream.sampleRate();
	if (ch <= 0 || srcRate <= 0 || stream.totalFrames() <= 0) return result;

	if (!decodeStage(stream, spec, result.samples)) return result;
	ran(RenderStage::Decode);

	if (spec.repitchSemitones != 0.f) {
		applyRepitch(result.samples, ch, srcRate, spec.repitchSemitones);
		ran(RenderStage::Repitch);
		if (result.samples.empty()) return result;
	}

	int outRate = srcRate;
	if (spec.targetRate > 0 && spec.targetRate != srcRate && ch <= RENDER_MAX_RESAMPLE_CHANNELS) {
		outRate = spec.targetRate;
		resampleStage(result.samples, ch, srcRate, outRate, spec.resampleQuality);
		ran(RenderStage::Resample);
		if (result.samples.empty()) return result;
	}

	if (spec.loop) {
		applyLoopCrossfade(result.samples, ch, outRate, spec.loopCrossfadeSecs);
		ran(RenderStage::LoopCrossfade);
	}
	else if (spec.declick) {
		applyDeclickZeroCross(result.samples, ch, outRate);
		ran(RenderStage::Declick);
	}
	if (result.samples.empty()) return result;

	result.channels = ch;
	result.sampleRate = outRate;
	result.durationSeconds = (float)(result.samples.size() / (size_t)ch) / (float)outRate;
	result.ok = true;
	return result;
}

} // namespace Siren
} // namespace StoermelderPackOne
