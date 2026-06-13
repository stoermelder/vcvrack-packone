#pragma once
#include "SirenTagClassifierApi.hpp"
#include "SirenDataSource.hpp"
#include <pffft.h>
#include <regex>
#include <array>
#include <cmath>
#include <cstring>


namespace StoermelderPackOne {
namespace Siren {


// Path extraction
namespace detail {

// Compiled once and reused. Patterns are static so try/catch around
// std::regex_search would never fire — kept out of the hot path.
//
// The "delimited" pattern is intentionally permissive: a number preceded by
// a delimiter (`_120`, `-120`) is matched even when not followed by another
// delimiter, so filenames like "loop_120" or "120_kick" all match.
struct BpmRegexTable {
	std::regex tagged;      // "120bpm", "120 BPM", "120.5Bpm"
	std::regex bracket;     // "[120]"
	std::regex delimited;   // "loop_120" / "120_kick" / "loop-120"
	std::regex leading;     // "120.5 kick.wav"

	BpmRegexTable()
		: tagged   (R"((\d+(?:\.\d+)?)\s*bpm)", std::regex::icase)
		, bracket  (R"(\[(\d+(?:\.\d+)?)\])")
		, delimited(R"((?:^|[_\-])(\d+(?:\.\d+)?)(?:[_\-]|$))")
		, leading  (R"(^(\d+(?:\.\d+)?))") {}
};

inline const BpmRegexTable& bpmRegexTable() {
	static const BpmRegexTable t;
	return t;
}

// Strip a known audio file extension. Returns the basename without the
// extension if `name` ends in a supported extension, otherwise `name`.
inline std::string stripAudioExtension(const std::string& name) {
	size_t dotPos = name.rfind('.');
	if (dotPos == std::string::npos || dotPos + 1 >= name.size()) return name;
	std::string ext = name.substr(dotPos + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg" ||
		ext == "aiff" || ext == "aif" || ext == "m4a" || ext == "wma" ||
		ext == "opus" || ext == "oga" || ext == "aac")
		return name.substr(0, dotPos);
	return name;
}

// Enumerate every numeric run in `name` that is bounded by a delimiter
// (`_` / `-`) or a string boundary on at least one side. Returns the
// substrings in left-to-right order.
//
// The existing `delimited` regex uses `regex_search`, which only reports the
// first match.  In filenames like "LHD_Loop_04_76.5" the first delimited
// number is the loop index "04" (out of range), and the trailing BPM "76.5"
// is skipped because its leading `_` is consumed by the previous match.  To
// resolve this we walk the string ourselves and collect every bounded
// number, then let the caller pick the first one that lies in the valid
// BPM range.
inline std::vector<std::string> collectBoundedNumbers(const std::string& name) {
	std::vector<std::string> out;
	auto isDelim = [](char c) { return c == '_' || c == '-'; };
	size_t i = 0;
	while (i < name.size()) {
		if (!std::isdigit(static_cast<unsigned char>(name[i]))) { ++i; continue; }
		size_t start = i;
		while (i < name.size() &&
		       (std::isdigit(static_cast<unsigned char>(name[i])) || name[i] == '.')) {
			++i;
		}
		bool leftBounded  = (start == 0) || isDelim(name[start - 1]);
		bool rightBounded = (i == name.size()) || isDelim(name[i]);
		if (leftBounded || rightBounded) {
			out.push_back(name.substr(start, i - start));
		}
	}
	return out;
}

} // namespace detail




// Spectral analysis (spectral flux onset + autocorrelation)
namespace detail {

// Reuse STFT constants from the tag classifier (FFT_SIZE=512, HOP=128,
// TARGET_SR=8820). 8820 Hz captures all kick frequencies (< 200 Hz) and
// gives twice the autocorrelation lag resolution compared to 4410 Hz.
using TagClassifierDetail::FFT_SIZE;
using TagClassifierDetail::HOP;
using TagClassifierDetail::TARGET_SR;
using TagClassifierDetail::PffftSetupGuard;
using TagClassifierDetail::generateHannWindow;
using TagClassifierDetail::computeMagnitudes;
using TagClassifierDetail::medianFilter3;
using TagClassifierDetail::meanSubtract;

static constexpr float MIN_BPM = 60.f;
static constexpr float MAX_BPM = 200.f;
static constexpr float CONFIDENCE_THR = 0.20f;  // Min normalised autocorr peak to accept

// Sub-band weighting. With SR=8820 and FFT_SIZE=512, each bin spans ~17 Hz
// (Nyquist 4410 Hz / 256 bins). The first 8 bins cover 0–~140 Hz — i.e. the
// bass/kick band that drives perceived tempo.
static constexpr int BASS_BIN_COUNT = 8;
static constexpr float BASS_BIN_WEIGHT = 2.0f;

// Log-magnitude spectral flux with bass-bin weighting. Differs from the
// classifier's unweighted version — kept here to emphasise kick energy.
inline float computeSpectralFlux(const float* magCur, const float* magPrev, int numBins) {
	float total = 0.f;
	for (int i = 0; i < numBins; ++i) {
		float cur  = std::max(magCur[i],  1e-6f);
		float prev = std::max(magPrev[i], 1e-6f);
		float diff = std::log(cur) - std::log(prev);
		if (diff > 0.f) {
			float w = (i < BASS_BIN_COUNT) ? BASS_BIN_WEIGHT : 1.f;
			total += diff * w;
		}
	}
	return total;
}

struct AutocorrResult {
	int bestLag = 0;     // integer lag of the strongest peak
	float confidence = 0.f;   // normalised peak value, ~[0, 1]
	float refinedLag = 0.f;   // parabolic-interpolated fractional lag
};

// Autocorrelation via FFT. The signal is real, length numFrames, and we want
// a real autocorrelation of length numFrames. We use PFFFT_COMPLEX on N
// complex samples (real part = signal, imag = 0, zero-padded), so a forward
// + conjugate-multiply + inverse yields a length-N real sequence whose first
// numFrames values are the unbiased autocorrelation.
inline AutocorrResult autocorrelate(const float* onsetSignal, int numFrames, float frameRate) {
	AutocorrResult result;
	if (numFrames < 8) return result;

	// Linear (non-circular) autocorrelation via FFT requires the signal to be
	// zero-padded to at least 2·numFrames. With only N ≥ numFrames the inverse
	// transform yields a *circular* autocorrelation: for any lag k where
	// N − numFrames < k the term x[i]·x[(i+k) mod N] wraps around and corrupts
	// r(k). Since the BPM lag window reaches lagMax ≈ frameRate·60/MIN_BPM, a
	// numFrames landing on (or just above) a power of two would contaminate
	// every lag we search. Padding to the next power of 2 ≥ 2·numFrames keeps
	// N − numFrames ≥ numFrames > lagMax, so no in-range lag ever wraps.
	int n = 1;
	while (n < 2 * numFrames) n <<= 1;
	if (n < 32) n = 32;

	// PFFFT_COMPLEX: input/output/work arrays must hold 2*N floats
	// (interleaved (r, i) complex samples).
	const int fsz = 2 * n;
	std::vector<float> input  (size_t(fsz), 0.f);
	std::vector<float> spec   (size_t(fsz), 0.f);
	std::vector<float> output (size_t(fsz), 0.f);
	std::vector<float> work   (size_t(fsz), 0.f);

	// Pack real signal into interleaved (r, i) complex samples.
	for (int i = 0; i < numFrames; ++i) {
		input[i * 2]     = onsetSignal[i];
		input[i * 2 + 1] = 0.f;
	}

	PffftSetupGuard fft(n, PFFFT_COMPLEX);
	if (!fft.p) return result;

	pffft_transform_ordered(fft.p, input.data(), spec.data(), work.data(), PFFFT_FORWARD);

	// X · conj(X) = |X|² — real, non-negative. pffft stores n complex bins
	// as interleaved (r, i) pairs.
	for (int i = 0; i < n; ++i) {
		float re = spec[i * 2];
		float im = spec[i * 2 + 1];
		spec[i * 2]     = re * re + im * im;
		spec[i * 2 + 1] = 0.f;
	}

	pffft_transform_ordered(fft.p, spec.data(), output.data(), work.data(), PFFFT_BACKWARD);

	// pffft is unnormalised: IFFT(FFT(x)) = N · x.  output[2*k] holds the
	// real part of the k-th complex sample; for a real input the imaginary
	// part is zero.  output[2*0] = N · Σ x² (the signal energy × N).
	const float norm = 1.f / float(n);
	const float energy = output[0] * norm;   // = Σ x²
	if (energy <= 0.f) return result;
	const float invEnergy = 1.f / energy;

	// Normalised autocorr at lag k: output[2*k] * norm / energy ∈ ~[0, 1].
	auto rAt = [&](int k) -> float {
		return output[size_t(k) * 2] * norm * invEnergy;
	};

	// Search the lag window corresponding to [MIN_BPM, MAX_BPM].
	int lagMin = std::max(1, int(std::round(frameRate * 60.f / MAX_BPM)));
	int lagMax = std::min(numFrames - 1, int(std::round(frameRate * 60.f / MIN_BPM)));
	if (lagMin >= lagMax) return result;

	int peakIdx = lagMin;
	float peakVal = rAt(lagMin);
	for (int k = lagMin + 1; k <= lagMax; ++k) {
		float v = rAt(k);
		if (v > peakVal) { peakVal = v; peakIdx = k; }
	}
	if (peakVal <= 0.f) return result;

	// Resolve half-time / double-time ambiguity with a weighted harmonic sum.
	//
	// For a strictly periodic signal the autocorr at the true period P and at
	// any integer multiple of P (2P, 3P, …) are all equal in the limit of
	// long signals.  In practice, finite-length + windowing + a non-integer
	// HOP/SR ratio can make the 2P peak happen to be *stronger* than the 1P
	// peak — e.g. a synthetic 120 BPM click train at 44.1 kSR / HOP=128 has a
	// frame period of 34.45, so lags 34 and 35 straddle the true period and
	// partially cancel, while lag 69 (≈ 2×34.45) happens to align with a
	// near-integer number of samples and dominates the autocorr.
	//
	// We score every candidate period P ∈ [lagMin, lagMax] by
	//   score(P) = rAt(P) + W · Σ rAt(P·h) for h ≥ 2 (clipped to lagMax)
	// with W < 1, so the fundamental dominates the score.  This recovers the
	// true period when:
	//   • The fundamental's autocorr is strong AND has a strong 2× harmonic
	//     in range (e.g. 120 BPM → lag 34 with strong 2× at lag 68).
	//   • A spurious "octave" candidate (e.g. lag 23 for a 90 BPM track)
	//     can only borrow score from its 2× harmonic — since W < 1, the
	//     smaller raw rAt(23) cannot outvote the larger rAt(46).
	int bestIdx = peakIdx;
	float bestScore = -1.f;
	constexpr float HARMONIC_W = 0.5f;
	// For each candidate p, look at the maximum autocorr in a small window
	// [k-1, k+1] around each harmonic k = p·h.  This is robust to sub-frame
	// drift: a true 2× harmonic might land at lag 2p±1 (because the true
	// period is rarely an integer number of hops), and we still want to
	// count it as support for p.
	auto rAtMaxAround = [&](int k) -> float {
		float best = rAt(k);
		if (k - 1 >= 0) best = std::max(best, rAt(k - 1));
		if (k + 1 <= lagMax) best = std::max(best, rAt(k + 1));
		return best;
	};
	for (int p = lagMin; p <= lagMax; ++p) {
		float sum = rAt(p);
		for (int h = 2; p * h <= lagMax; ++h) sum += HARMONIC_W * rAtMaxAround(p * h);
		// Smallest-lag tiebreak: subtract a tiny bias that increases with
		// lag, so that within ~0.001 of score the smaller lag wins.
		float biased = sum - 1e-3f * float(lagMax - p) / float(lagMax);
		if (biased > bestScore) { bestScore = biased; bestIdx = p; }
	}
	(void)peakVal;
	(void)peakIdx;

	result.bestLag = bestIdx;
	result.refinedLag = float(bestIdx);
	// Re-evaluate parabolic interpolation at the chosen bestIdx.
	if (bestIdx > lagMin && bestIdx < lagMax) {
		float yL = rAt(bestIdx - 1);
		float yC = rAt(bestIdx);
		float yR = rAt(bestIdx + 1);
		float denom = (yL - 2.f * yC + yR);
		if (std::fabs(denom) > 1e-9f) {
			float delta = 0.5f * (yL - yR) / denom;
			if (delta > -1.f && delta < 1.f) result.refinedLag = float(bestIdx) + delta;
		}
	}
	// Confidence: the autocorr value at the chosen best lag (in [-1, 1] but
	// clipped to [0, 1] for thresholding).  We don't use the harmonic-summed
	// score because it can exceed 1 when many harmonics line up.
	result.confidence = std::max(0.f, std::min(1.f, rAt(bestIdx)));
	return result;
}

} // namespace detail


// Main detect function

// Offline BPM detection via log-magnitude spectral flux + autocorrelation.
// Must be called on a worker thread, never the DSP thread.
// Returns detected BPM, or 0 if detection failed or confidence is below threshold.
// confidenceOut receives the raw confidence value (0–1) regardless of threshold.
inline float detectBpm(AudioStream& stream, float& confidenceOut, float maxDurationSeconds = 60.f) {
	confidenceOut = 0.f;

	std::vector<float> mono;
	int outSR = 0;
	if (!TagClassifier::prepareMono(stream, maxDurationSeconds, mono, outSR)) return 0.f;

	// Need at least ~2 s of audio to make a tempo estimate.
	if (mono.size() < size_t(outSR * 2)) return 0.f;

	// STFT (Hann-windowed, 4× overlap)
	const int FFT_SIZE = detail::FFT_SIZE;
	const int HOP = detail::HOP;
	const int halfN = FFT_SIZE / 2;

	int numFrames = int(mono.size());
	int numHops   = (numFrames - FFT_SIZE) / HOP + 1;
	if (numHops < 4) return 0.f;

	std::vector<float> window(FFT_SIZE);
	detail::generateHannWindow(window.data(), FFT_SIZE);

	std::vector<float> onsetSignal(numHops, 0.f);
	std::vector<float> magPrev(halfN, 0.f);
	std::vector<float> magCur(halfN, 0.f);
	std::vector<float> win(FFT_SIZE, 0.f);
	std::vector<float> spec(FFT_SIZE, 0.f);
	std::vector<float> work(FFT_SIZE, 0.f);

	detail::PffftSetupGuard fft(FFT_SIZE, PFFFT_REAL);
	if (!fft.p) return 0.f;

	// Prime magPrev from the very first window so hop 0 has a valid predecessor.
	for (int i = 0; i < FFT_SIZE; ++i) win[i] = mono[i] * window[i];
	pffft_transform_ordered(fft.p, win.data(), spec.data(), work.data(), PFFFT_FORWARD);
	detail::computeMagnitudes(spec.data(), magPrev.data(), halfN);

	for (int h = 0; h < numHops; ++h) {
		int startIdx = h * HOP;
		int lastValid = std::min(FFT_SIZE, numFrames - startIdx);
		for (int i = 0; i < lastValid; ++i) win[i] = mono[startIdx + i] * window[i];
		for (int i = lastValid; i < FFT_SIZE; ++i) win[i] = 0.f;   // zero-pad tail

		pffft_transform_ordered(fft.p, win.data(), spec.data(), work.data(), PFFFT_FORWARD);
		detail::computeMagnitudes(spec.data(), magCur.data(), halfN);

		onsetSignal[h] = detail::computeSpectralFlux(magCur.data(), magPrev.data(), halfN);

		// Reuse magCur as the next magPrev via std::swap — avoids a copy.
		std::swap(magCur, magPrev);
	}

	// Spike suppression + DC removal.
	std::vector<float> onsetFiltered(numHops);
	detail::medianFilter3(onsetFiltered.data(), onsetSignal.data(), numHops);
	detail::meanSubtract(onsetFiltered.data(), numHops);

	// Onset signal frame rate: outSR / HOP.
	const float frameRate = float(outSR) / float(HOP);

	auto ac = detail::autocorrelate(onsetFiltered.data(), numHops, frameRate);
	confidenceOut = ac.confidence;
	if (ac.bestLag <= 0 || ac.confidence < detail::CONFIDENCE_THR) return 0.f;

	// Convert lag to BPM using the parabolic-refined (fractional) lag for
	// sub-sample accuracy, then clamp to a sane window.
	float lag = (ac.refinedLag > 0.f) ? ac.refinedLag : float(ac.bestLag);
	float bpm = 60.f * frameRate / lag;
	if (bpm < detail::MIN_BPM * 0.5f || bpm > detail::MAX_BPM * 2.f) return 0.f;
	return bpm;
}


// BpmDetector struct (API)
struct BpmDetector {
	// Scan filename and parent folder path components for an encoded BPM.
	// Fast and synchronous — safe to call on the main thread.
	// Returns BPM or 0 if not found in the name.
	static float detectFromName(const std::string& id, float& confidenceOut) {
		confidenceOut = 0.f;
		const auto& tbl = detail::bpmRegexTable();
		// Strong patterns first: "120bpm", "[120]", and leading numbers are
		// unambiguous and use regex_search.
		const std::array<std::regex, 3> strongPatterns = { tbl.tagged, tbl.bracket, tbl.leading };
		std::vector<std::string> components;
		size_t start = 0;
		size_t end;
		while ((end = id.find('/', start)) != std::string::npos) {
			if (end > start) components.push_back(id.substr(start, end - start));
			start = end + 1;
		}
		if (start < id.size()) components.push_back(id.substr(start));
		std::reverse(components.begin(), components.end());
		for (const std::string& comp : components) {
			std::string name = detail::stripAudioExtension(comp);
			for (const std::regex& re : strongPatterns) {
				std::smatch m;
				if (!std::regex_search(name, m, re)) continue;
				try {
					float bpm = std::stof(m[1].str());
					if (bpm >= 60.f && bpm <= 220.f) { confidenceOut = 1.f; return bpm; }
				}
				catch (const std::exception&) {}
			}
			// Delimited numbers: enumerate every bounded number and accept
			// the first one in the valid BPM range.  This handles filenames
			// like "LHD_Drum_Loop_04_76.5" where an out-of-range index ("04")
			// appears before the actual BPM ("76.5").
			for (const std::string& numStr : detail::collectBoundedNumbers(name)) {
				try {
					float bpm = std::stof(numStr);
					if (bpm >= 60.f && bpm <= 220.f) { confidenceOut = 1.f; return bpm; }
				}
				catch (const std::exception&) {}
			}
		}
		return 0.f;
	}

	// Spectral analysis BPM detection — must be called on a worker thread.
	// Returns BPM or 0 if detection failed or confidence is below threshold.
	static float detectFromDsp(DataSource& source, const std::string& id, float& confidenceOut,
			float maxDurationSeconds = 60.f) {
		confidenceOut = 0.f;
		auto stream = source.openAudioStream(id);
		if (!stream) return 0.f;
		return StoermelderPackOne::Siren::detectBpm(*stream, confidenceOut, maxDurationSeconds);
	}
};


} // namespace Siren
} // namespace StoermelderPackOne
