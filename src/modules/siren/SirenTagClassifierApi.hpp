#pragma once
// Feature extraction API and model registration interface.
//
// This header is self-contained — it does NOT include SirenTagClassifier.cpp
// (the auto-generated model). The model is compiled into the plugin via the
// root Makefile's `wildcard src/**/**/*.cpp` glob and registers itself at
// static-init time by calling TagClassifier::_setLoader(). The actual
// registerModel() fires on the first score()/classify() call — feature
// extraction alone never touches the model.
//
// Consumers that only need feature extraction (e.g. the CLI tool
// siren_extract_features) include this header alone. Consumers that need
// classification must link the generated SirenTagClassifier.cpp into the
// binary; in the plugin this happens automatically.
//
// SIREN_TAG_NUM_FEATURES is owned here (not by the generated model).
// The generated source contains a static_assert that verifies the counts
// match (53 at the time of writing).

#include "SirenAudioStream.hpp"     // AudioStream interface — no Rack dependency
#include <pffft.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>


namespace StoermelderPackOne {
namespace Siren {

// Number of features extractFeatures() computes. Owned by this header;
// the generated model asserts that it was trained with the same count.
static const int SIREN_TAG_NUM_FEATURES = 53;

struct SuggestedTag {
	std::string name;
	float score;
};

namespace TagClassifierDetail {

// STFT constants — see SirenBpmDetector.hpp:108-115 for rationale
static constexpr int FFT_SIZE = 512;
static constexpr int HOP = 128;
static constexpr int HALF_N = FFT_SIZE / 2;
static constexpr int TARGET_SR = 8820;  // doubled for better high-freq coverage

// Normalisation constants — must match feature_config.FEATURE_NAMES
static constexpr float ONSET_DENSITY_NORM = 30.f;     // peaks/sec → [0, 1]
static constexpr float SUB_BASS_HZ = 80.f;     // sub_bass_ratio cutoff (Hz)
static constexpr float LOW_BAND_HZ = 250.f;    // low_mid_ratio cutoff (Hz)
static constexpr float HIGH_BAND_HZ = 2000.f;   // high_band_ratio cutoff (Hz)
static constexpr float MEAN_FLUX_NORM = 50.f;     // log-flux units/hop → [0, 1]
static constexpr float RMS_FULL_SCALE = 0.7071067811865475f; // 1/√2

// MFCC parameters
static constexpr int N_MELS = 26;    // mel filterbank bands
static constexpr int N_MFCC = 13;    // cepstral coefficients kept
static constexpr float MFCC_NORM_0 = 200.f; // normalization for C[0] (energy-like, wide range)
static constexpr float MFCC_NORM_N = 30.f;  // normalization for C[1..12] (shape)

// Helpers (RAII, window, magnitudes, flux, smoothing, peak-pick)

struct PffftSetupGuard {
	PFFFT_Setup* p;
	explicit PffftSetupGuard(int n, pffft_transform_t t) : p(pffft_new_setup(n, t)) {}
	~PffftSetupGuard() { if (p) pffft_destroy_setup(p); }
	PffftSetupGuard(const PffftSetupGuard&) = delete;
	PffftSetupGuard& operator=(const PffftSetupGuard&) = delete;
};

inline void generateHannWindow(float* out, int n) {
	const float denom = (n > 1) ? float(n - 1) : 1.f;
	for (int i = 0; i < n; ++i) {
		out[i] = 0.5f * (1.f - std::cos(2.f * float(M_PI) * i / denom));
	}
}

inline void computeMagnitudes(const float* fftOut, float* mag, int halfSize) {
	for (int i = 0; i < halfSize; ++i) {
		float re = fftOut[i * 2];
		float im = fftOut[i * 2 + 1];
		mag[i] = std::sqrt(re * re + im * im);
	}
}

inline float computeSpectralFlux(const float* magCur, const float* magPrev, int numBins) {
	float total = 0.f;
	for (int i = 0; i < numBins; ++i) {
		float cur = std::max(magCur[i], 1e-6f);
		float prev = std::max(magPrev[i], 1e-6f);
		float diff = std::log(cur) - std::log(prev);
		if (diff > 0.f) total += diff;
	}
	return total;
}

inline void medianFilter3(float* out, const float* in, int n) {
	if (n < 3) {
		if (out != in) std::memcpy(out, in, size_t(n) * sizeof(float));
		return;
	}
	out[0] = in[0];
	for (int i = 1; i < n - 1; ++i) {
		float a = in[i - 1], b = in[i], c = in[i + 1];
		out[i] = std::max(std::min(a, b), std::min(std::max(a, b), c));
	}
	out[n - 1] = in[n - 1];
}

inline void meanSubtract(float* x, int n) {
	if (n <= 0) return;
	double sum = 0.0;
	for (int i = 0; i < n; ++i) sum += x[i];
	float mean = float(sum / double(n));
	for (int i = 0; i < n; ++i) x[i] -= mean;
}

inline int countPeaks(const float* x, int n, float threshold) {
	if (n < 3) return 0;
	int peaks = 0;
	for (int i = 1; i < n - 1; ++i) {
		if (x[i] > threshold && x[i] >= x[i - 1] && x[i] >= x[i + 1]) ++peaks;
	}
	return peaks;
}

// Per-hop spectral accumulator
// Carries all intermediate STFT-phase state between runSTFT() and
// finalizeSpectralFeatures(). All doubles — accumulated over many hops,
// normalised to [0,1] only when written to the output feature vector.
struct SpectralAccum {
	double centroidBinSum = 0.0;
	double powerSum = 0.0;
	double powerSubBass = 0.0;
	double powerLow = 0.0;
	double powerHigh = 0.0;
	double rolloff85Acc = 0.0;
	double bwAcc = 0.0;
	double flatnessAcc = 0.0;
	double crestAcc = 0.0;
	double entropyAcc = 0.0;
	double slopeAcc = 0.0;
	double decreaseAcc = 0.0;
	double skewnessAcc = 0.0;
	double kurtosisAcc = 0.0;
	std::vector<double> melAcc;
	std::vector<float> onsetSignal;
	int numHops = 0;

	// MFCC delta: mean absolute frame-to-frame difference per coefficient
	std::vector<double> mfccDeltaAcc;
	float mfccPrev[N_MFCC] = {};
	bool mfccPrevValid = false;

	explicit SpectralAccum(int nMels)
		: melAcc(size_t(nMels), 0.0), mfccDeltaAcc(size_t(N_MFCC), 0.0) {}
};

} // namespace TagClassifierDetail


// Public API
struct TagClassifier {
	// Model registration
	// SirenTagClassifier.cpp calls registerModel() from its anonymous
	// namespace so the model is available as soon as its header is included
	// in a TU. Feature extraction works without registration; classification
	// requires it (returns empty if no model is loaded).

	struct ModelInfo {
		void (*scoreFn)(const float*, float*) = nullptr;
		int numClasses = 0;
		const char* const* classNames = nullptr;
		// called once on first scoring use
		void (*lazyInit)() = nullptr;
		// tag name → filename keywords
		std::map<std::string, std::vector<std::string>> keywords;
		// JSON blob describing the training run that produced this model
		// (dataset path, hyper-parameters, scikit-learn version, …).
		// Set by the generated model TU via registerTrainingInfo(). May be
		// nullptr when the model was built without metadata (e.g. legacy
		// generated files), or a pointer to a static "" when no params
		// were supplied at training time.
		const char* trainingInfoJson = nullptr;
	};

	// Access the singleton. On first call after setLoader(), calls the loader
	// and clears it so it never fires again.
	static ModelInfo& _model() {
		static ModelInfo info;
		if (info.lazyInit) {
			void (*fn)() = info.lazyInit;
			info.lazyInit = nullptr;
			fn();
		}
		return info;
	}

	// Called by SirenTagClassifier.cpp at static-init time to register a
	// deferred loader. The loader itself runs on the first scoring call.
	static void _setLoader(void (*fn)()) {
		_model().lazyInit = fn;
	}

	static bool registerModel(void (*fn)(const float*, float*),
			int numClasses, const char* const* classNames) {
		ModelInfo& m = _model();
		m.scoreFn = fn;
		m.numClasses = numClasses;
		m.classNames = classNames;
		return true;
	}

	// Register a JSON blob describing the training parameters used to
	// build the model (dataset path, n_estimators, max_depth, …).
	// `json` must be a pointer with static storage duration (a string
	// literal in the generated model TU is ideal). It is NOT copied —
	// the pointer is stored as-is. Safe to call before or after
	// registerModel(); the values are independent fields on ModelInfo.
	// A null pointer is treated as "no metadata available".
	static void registerTrainingInfo(const char* json) {
		_model().trainingInfoJson = json;
	}

	// Returns the training-parameters JSON registered by the loaded
	// model, or an empty string when none was provided. The pointer is
	// owned by the model TU; callers must not free it.
	static const char* trainingInfo() {
		const char* p = _model().trainingInfoJson;
		return p ? p : "";
	}

	// Register filename keywords loaded from SirenTags.json.
	// Call this on the main thread before first use (e.g. from SirenBrowserPane).
	static void registerKeywords(const std::map<std::string, std::vector<std::string>>& kw) {
		_model().keywords = kw;
	}

	// Number of classes in the loaded model (0 if no model registered yet).
	static int numClasses() {
		return _model().numClasses;
	}

	// Scoring
	// Score a feature vector. `out` must be numClasses() floats.
	// Clamps features and scores to [0, 1]. No-op if no model is loaded.
	static void score(const float features[SIREN_TAG_NUM_FEATURES], float* out) {
		if (!_model().scoreFn) return;
		float clamped[SIREN_TAG_NUM_FEATURES];
		for (int i = 0; i < SIREN_TAG_NUM_FEATURES; ++i) {
			float v = features[i];
			clamped[i] = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
		}
		_model().scoreFn(clamped, out);
		for (int i = 0; i < _model().numClasses; ++i) {
			float v = out[i];
			out[i] = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
		}
	}

	// Pick the top-k from a score vector (numClasses() elements).
	static std::vector<SuggestedTag> topK(const float* scores, int k = 3) {
		int n = _model().numClasses;
		if (n <= 0) return {};
		if (k < 0) k = 0;
		if (k > n) k = n;
		std::vector<int> idx(n);
		for (int i = 0; i < n; ++i) idx[i] = i;
		std::sort(idx.begin(), idx.end(), [&](int a, int b) {
			return scores[a] > scores[b];
		});
		std::vector<SuggestedTag> result;
		result.reserve(size_t(k));
		for (int j = 0; j < k; ++j) {
			int c = idx[j];
			result.push_back({ _model().classNames[c], scores[c] });
		}
		return result;
	}

	// Top-k SuggestedTags from a feature vector.
	static std::vector<SuggestedTag> classify(const float features[SIREN_TAG_NUM_FEATURES], int k = 3) {
		int n = _model().numClasses;
		if (n <= 0) return {};
		std::vector<float> scores(n, 0.f);
		score(features, scores.data());
		return topK(scores.data(), k);
	}

	// Top-k SuggestedTags straight from an audio stream.
	static std::vector<SuggestedTag> classify(AudioStream& stream, int k = 3,
			float maxDurationSeconds = 30.f) {
		float features[SIREN_TAG_NUM_FEATURES] = {};
		extractFeatures(stream, features, maxDurationSeconds);
		return classify(features, k);
	}

	// Top-k SuggestedTags from an audio stream, boosted by filename hints.
	// `filePath` may be a full path or just the filename — only the stem is used.
	static std::vector<SuggestedTag> classify(AudioStream& stream, const std::string& filePath,
			int k = 3, float maxDurationSeconds = 30.f) {
		int n = _model().numClasses;
		if (n <= 0) return {};
		float features[SIREN_TAG_NUM_FEATURES] = {};
		extractFeatures(stream, features, maxDurationSeconds);
		std::vector<float> scores(n, 0.f);
		score(features, scores.data());
		applyFilenameBoosts(filenameStem(filePath), scores.data(), n, _model().classNames);
		return topK(scores.data(), k);
	}

	// Extract the lowercase stem (no directory, no extension) from a path.
	static std::string filenameStem(const std::string& path) {
		size_t sep = path.find_last_of("/\\");
		std::string name = (sep == std::string::npos) ? path : path.substr(sep + 1);
		size_t dot = name.rfind('.');
		if (dot != std::string::npos) name = name.substr(0, dot);
		for (char& c : name) c = (char)std::tolower((unsigned char)c);
		return name;
	}

	// True if `kw` appears in `stem` surrounded by word boundaries
	// (non-alphanumeric character or start/end of string).
	static bool wordContains(const std::string& stem, const char* kw) {
		size_t kwLen = std::strlen(kw);
		if (kwLen == 0) return false;
		size_t pos = 0;
		while ((pos = stem.find(kw, pos)) != std::string::npos) {
			bool leftOk = (pos == 0) || !std::isalnum((unsigned char)stem[pos - 1]);
			bool rightOk = (pos + kwLen >= stem.size()) || !std::isalnum((unsigned char)stem[pos + kwLen]);
			if (leftOk && rightOk) return true;
			++pos;
		}
		return false;
	}

	// Boost scores for classes whose keywords appear in the filename stem.
	// Uses max(score, boost) so audio evidence is never reduced.
	// Keywords are read from the registered map (loaded from SirenTags.json).
	static void applyFilenameBoosts(const std::string& stem, float* scores, int n,
			const char* const* classNames, float boost = 0.9f) {
		const auto& kw = _model().keywords;
		if (kw.empty()) return;
		for (int c = 0; c < n; ++c) {
			auto it = kw.find(classNames[c]);
			if (it == kw.end()) continue;
			for (const std::string& word : it->second) {
				if (wordContains(stem, word.c_str())) {
					if (scores[c] < boost) scores[c] = boost;
					break;
				}
			}
		}
	}

	// Compute the 53 normalized features from an audio stream. Feature order
	// MUST match `feature_config.FEATURE_NAMES` in the training pipeline —
	// it is the contract between the C++ runtime and Python.
	// Returns all-zero if the stream has no frames or too few STFT hops (<4).
	// Output is always clamped to [0, 1].
	static void extractFeatures(AudioStream& stream, float out[SIREN_TAG_NUM_FEATURES],
			float maxDurationSeconds = 30.f) {
		using namespace TagClassifierDetail;

		for (int i = 0; i < SIREN_TAG_NUM_FEATURES; ++i) out[i] = 0.f;

		std::vector<float> mono;
		int outSR;
		if (!prepareMono(stream, maxDurationSeconds, mono, outSR)) return;

		extractTimeDomainFeatures(mono, outSR, out);

		SpectralAccum acc(N_MELS);
		if (!runSTFT(mono, outSR, acc)) return;

		finalizeSpectralFeatures(acc, outSR, out);

		for (int i = 0; i < SIREN_TAG_NUM_FEATURES; ++i) {
			if (out[i] < 0.f) out[i] = 0.f;
			else if (out[i] > 1.f) out[i] = 1.f;
		}
	}

	// Phase 1: Audio ingestion
	// Read the stream, decimate to ~TARGET_SR, mix to mono.
	// Returns false if the stream is invalid or yields no samples.
	static bool prepareMono(AudioStream& stream, float maxDurationSeconds,
			std::vector<float>& mono, int& outSR) {
		using namespace TagClassifierDetail;

		int sampleRate = stream.sampleRate();
		int channels = stream.channels();
		int64_t totalFrames = stream.totalFrames();
		if (totalFrames <= 0 || sampleRate <= 0 || channels <= 0) return false;

		int64_t maxFrames = int64_t(sampleRate * maxDurationSeconds);
		if (totalFrames > maxFrames) totalFrames = maxFrames;

		// Decimate to ~TARGET_SR via box-filter averaging: each output sample
		// is the mean of decimRate consecutive input frames (mixed to mono).
		// Averaging over decimRate frames acts as a low-pass filter with its
		// first null at outSR Hz, attenuating content above outSR/2 before
		// subsampling and preventing it from aliasing into the analysis band.
		int decimRate = std::max(1, sampleRate / TARGET_SR);
		outSR = sampleRate / decimRate;
		if (outSR <= 0) outSR = TARGET_SR;

		const int64_t BUFSIZE = 65536;
		mono.reserve(size_t(totalFrames / decimRate) + 1024);
		std::vector<float> buf(size_t(BUFSIZE) * size_t(std::max(channels, 1)));

		int64_t framesRead = 0;
		float groupSum = 0.f;
		int groupCount = 0;
		while (framesRead < totalFrames) {
			int64_t toRead = std::min<int64_t>(BUFSIZE, totalFrames - framesRead);
			int64_t got = stream.readF32(buf.data(), toRead);
			if (got <= 0) break;
			for (int64_t f = 0; f < got; f++) {
				float frameMono = 0.f;
				for (int ch = 0; ch < channels; ++ch) {
					frameMono += buf[size_t(f * channels + ch)];
				}
				groupSum += frameMono / float(channels);
				if (++groupCount == decimRate) {
					mono.push_back(groupSum / float(decimRate));
					groupSum = 0.f;
					groupCount = 0;
				}
			}
			framesRead += got;
		}
		// Flush any partial group at EOF
		if (groupCount > 0) {
			mono.push_back(groupSum / float(groupCount));
		}
		return !mono.empty();
	}

	// Phase 2: Time-domain features
	// Operates on the decimated mono buffer. No FFT required.
	// Writes: out[2]  ZCR mean
	//         out[3]  RMS
	//         out[10] crest factor
	//         out[11] harmonic ratio
	//         out[31] temporal centroid
	//         out[32] tail/head ratio
	//         out[33] envelope autocorrelation
	//         out[34] attack time
	//         out[35] envelope RMS variance
	//         out[36] temporal entropy
	static void extractTimeDomainFeatures(const std::vector<float>& mono, int outSR,
			float out[SIREN_TAG_NUM_FEATURES]) {
		using namespace TagClassifierDetail;

		// ZCR — per-frame average zero-crossing rate
		{
			const int zcrFrame = std::max(64, outSR / 33);
			const int zcrNumFrames = int(mono.size()) / zcrFrame;
			float zcrSum = 0.f;
			int zcrCount = 0;
			for (int f = 0; f < zcrNumFrames; ++f) {
				int startIdx = f * zcrFrame;
				int endIdx = startIdx + zcrFrame;
				int crossings = 0;
				for (int i = startIdx + 1; i < endIdx; ++i) {
					float a = mono[i - 1], b = mono[i];
					if ((a >= 0.f && b < 0.f) || (a < 0.f && b >= 0.f)) ++crossings;
				}
				zcrSum += float(crossings) / float(zcrFrame);
				++zcrCount;
			}
			out[2] = (zcrCount > 0) ? (zcrSum / float(zcrCount)) : 0.f;
		}

		// RMS, peak amplitude, crest factor
		{
			double sumSq = 0.0;
			float peakAbs = 0.f;
			for (float s : mono) {
				sumSq += double(s) * double(s);
				float a = s < 0.f ? -s : s;
				if (a > peakAbs) peakAbs = a;
			}
			float rms = float(std::sqrt(sumSq / double(mono.size())));
			float crest = (rms > 1e-6f) ? peakAbs / rms : 0.f;
			out[3] = rms / RMS_FULL_SCALE;
			// log(1 + crest) / log(31): sustained sine ~0.2, percussion ~0.7, silence 0
			out[10] = std::min(1.f, std::log(1.f + crest) / std::log(31.f));
		}

		// Temporal envelope — 32-block RMS, shared by five features
		{
			const int N_BLOCKS = 32;
			const int n = int(mono.size());
			float blockRms[N_BLOCKS] = {};
			for (int b = 0; b < N_BLOCKS; ++b) {
				int lo = (int64_t(b) * n) / N_BLOCKS;
				int hi = (int64_t(b + 1) * n) / N_BLOCKS;
				double ss = 0.0;
				for (int i = lo; i < hi; ++i) ss += double(mono[i]) * double(mono[i]);
				blockRms[b] = (hi > lo) ? float(std::sqrt(ss / double(hi - lo))) : 0.f;
			}

			// Temporal centroid: time-weighted centre of mass — one-shot→0, loop/pad→0.5
			{
				double wSum = 0.0, eSum = 0.0;
				for (int b = 0; b < N_BLOCKS; ++b) {
					double e = double(blockRms[b]);
					wSum += double(b) * e;
					eSum += e;
				}
				out[31] = (eSum > 1e-12) ? float(wSum / (eSum * (N_BLOCKS - 1))) : 0.5f;
			}

			// Tail/head ratio: rms(last 20%) / (rms(first+last 20%)) — one-shot→0, loop→0.5
			{
				const int EDGE = std::max(1, N_BLOCKS / 5);
				double headE = 0.0, tailE = 0.0;
				for (int b = 0; b < EDGE; ++b) headE += double(blockRms[b]);
				for (int b = N_BLOCKS - EDGE; b < N_BLOCKS; ++b) tailE += double(blockRms[b]);
				out[32] = float(tailE / (headE + tailE + 1e-12));
			}

			// Attack time: position of peak-RMS block, normalised — percussive→0, pad→high
			{
				int peakBlock = 0;
				for (int b = 1; b < N_BLOCKS; ++b) {
					if (blockRms[b] > blockRms[peakBlock]) peakBlock = b;
				}
				out[34] = float(peakBlock) / float(N_BLOCKS - 1);
			}

			// Envelope RMS variance — sustained→0, rhythmic→high
			{
				double mean = 0.0;
				for (int b = 0; b < N_BLOCKS; ++b) mean += double(blockRms[b]);
				mean /= N_BLOCKS;
				double var = 0.0;
				for (int b = 0; b < N_BLOCKS; ++b) {
					double d = double(blockRms[b]) - mean;
					var += d * d;
				}
				out[35] = std::min(1.f, float(std::sqrt(var / N_BLOCKS)) * 2.f);
			}

			// Temporal entropy: H = -Σ p·log(p) / log(N) — one-shot→0, drone→1
			{
				double eSum = 0.0;
				for (int b = 0; b < N_BLOCKS; ++b) eSum += double(blockRms[b]);
				double H = 0.0;
				if (eSum > 1e-12) {
					for (int b = 0; b < N_BLOCKS; ++b) {
						double p = double(blockRms[b]) / eSum;
						if (p > 1e-12) H -= p * std::log(p);
					}
					H /= std::log(double(N_BLOCKS));
				}
				out[36] = float(H);
			}
		}

		// Envelope autocorrelation (~100 ms blocks, lags 0.25–4 s)
		// Peak normalised AC → 1 for rhythmic loops, ~0 for one-shots / drones.
		{
			const int ENV_BLOCK = std::max(1, outSR / 10);
			const int nBlocks = int(mono.size()) / ENV_BLOCK;
			if (nBlocks >= 6) {
				std::vector<float> env(size_t(nBlocks), 0.f);
				for (int b = 0; b < nBlocks; ++b) {
					double ss = 0.0;
					const int lo = b * ENV_BLOCK, hi = lo + ENV_BLOCK;
					for (int i = lo; i < hi; ++i) ss += double(mono[i]) * double(mono[i]);
					env[b] = float(std::sqrt(ss / double(ENV_BLOCK)));
				}
				double r0 = 0.0;
				for (int b = 0; b < nBlocks; ++b) r0 += double(env[b]) * double(env[b]);
				float maxAC = 0.f;
				if (r0 > 1e-12) {
					const int lagMin = std::max(1, int(0.25f * float(outSR) / float(ENV_BLOCK)));
					const int lagMax = std::min(nBlocks - 1, int(4.0f * float(outSR) / float(ENV_BLOCK)));
					for (int lag = lagMin; lag <= lagMax; ++lag) {
						double r = 0.0;
						const int cnt = nBlocks - lag;
						for (int b = 0; b < cnt; ++b) r += double(env[b]) * double(env[b + lag]);
						float rn = float(r / r0);
						if (rn > maxAC) maxAC = rn;
					}
				}
				out[33] = maxAC;
			}
		}

		// Harmonic ratio — autocorrelation over pitch range 49–1100 Hz (lags 8–180 at outSR)
		// Normalised peak → periodic=1 (tonal), noise=0.
		{
			int n = std::min((int)mono.size(), 4096);
			float r0 = 0.f;
			for (int i = 0; i < n; ++i) r0 += mono[i] * mono[i];
			float maxAC = 0.f;
			if (r0 > 1e-12f) {
				for (int lag = 8; lag <= std::min(180, n - 1); ++lag) {
					float r = 0.f;
					for (int i = 0; i < n - lag; ++i) r += mono[i] * mono[i + lag];
					float rn = r / r0;
					if (rn > maxAC) maxAC = rn;
				}
			}
			out[11] = maxAC;
		}
	}

	// Phase 3: STFT + per-hop spectral accumulation
	// Fills acc with summed statistics over all hops.
	// Returns false if there are too few hops (<4).
	static bool runSTFT(const std::vector<float>& mono, int outSR,
			TagClassifierDetail::SpectralAccum& acc) {
		using namespace TagClassifierDetail;

		const int numFrames = int(mono.size());
		const int numHops = (numFrames - FFT_SIZE) / HOP + 1;
		if (numHops < 4) return false;

		acc.numHops = numHops;
		acc.onsetSignal.assign(size_t(numHops), 0.f);

		std::vector<float> window(FFT_SIZE);
		generateHannWindow(window.data(), FFT_SIZE);

		std::vector<float> magPrev(HALF_N, 0.f), magCur(HALF_N, 0.f);
		std::vector<float> win(FFT_SIZE, 0.f), spec(FFT_SIZE, 0.f), work(FFT_SIZE, 0.f);

		// Precomputed slope constants (function of HALF_N only)
		const double sum_k = double(HALF_N) * (HALF_N - 1) / 2.0;
		const double sum_k2 = double(HALF_N) * (HALF_N - 1) * (2 * HALF_N - 1) / 6.0;
		const double var_k = double(HALF_N) * sum_k2 - sum_k * sum_k;

		PffftSetupGuard fft(FFT_SIZE, PFFFT_REAL);
		if (!fft.p) return false;

		// Frequency-band cutoff bins at the decimated sample rate
		const float binHz = float(outSR) / float(FFT_SIZE);
		const int subBassBinMax = std::max(1, int(std::floor(SUB_BASS_HZ / binHz)));
		const int lowBinMax = std::max(1, int(std::floor(LOW_BAND_HZ / binHz)));
		const int highBinMin = std::min(HALF_N - 1, int(std::ceil(HIGH_BAND_HZ / binHz)));

		// Mel filterbank: N_MELS triangular filters from 0 Hz to Nyquist
		// mel(f) = 2595·log10(1 + f/700);  bin = round(mel_hz * FFT_SIZE / outSR)
		int mel_bins[N_MELS + 2];
		{
			auto hz_to_mel = [](double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); };
			auto mel_to_hz = [](double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); };
			double mel_lo = hz_to_mel(0.0);
			double mel_hi = hz_to_mel(double(outSR) / 2.0);
			for (int m = 0; m < N_MELS + 2; ++m) {
				double mel = mel_lo + (mel_hi - mel_lo) * m / (N_MELS + 1);
				int bin = int(mel_to_hz(mel) * FFT_SIZE / outSR);
				mel_bins[m] = std::max(0, std::min(HALF_N - 1, bin));
			}
		}

		// Prime magPrev from the very first window so hop 0 has a valid predecessor
		for (int i = 0; i < FFT_SIZE; ++i) win[i] = mono[i] * window[i];
		pffft_transform_ordered(fft.p, win.data(), spec.data(), work.data(), PFFFT_FORWARD);
		computeMagnitudes(spec.data(), magPrev.data(), HALF_N);

		for (int h = 0; h < numHops; ++h) {
			const int startIdx = h * HOP;
			const int lastValid = std::min(FFT_SIZE, numFrames - startIdx);
			for (int i = 0; i < lastValid; ++i) win[i] = mono[startIdx + i] * window[i];
			for (int i = lastValid; i < FFT_SIZE; ++i) win[i] = 0.f;

			pffft_transform_ordered(fft.p, win.data(), spec.data(), work.data(), PFFFT_FORWARD);
			computeMagnitudes(spec.data(), magCur.data(), HALF_N);

			acc.onsetSignal[h] = computeSpectralFlux(magCur.data(), magPrev.data(), HALF_N);

			// First pass: power sums, centroid, band energy, slope/crest inputs
			double framePower = 0.0;
			double centroid = 0.0;
			double frameMagSum = 0.0;  // Σ mag[b]
			double frameKXSum = 0.0;  // Σ b·mag[b]  (spectral slope)
			double frameMaxMag = 0.0;  // max(mag[b]) (spectral crest)
			for (int b = 0; b < HALF_N; ++b) {
				double m = double(magCur[b]);
				double p = m * m;
				framePower += p;
				centroid += double(b) * p;
				if (b < subBassBinMax) acc.powerSubBass += p;
				else if (b < lowBinMax) acc.powerLow += p;  // [80, 250) Hz
				if (b >= highBinMin) acc.powerHigh += p;
				frameMagSum += m;
				frameKXSum += double(b) * m;
				if (m > frameMaxMag) frameMaxMag = m;
			}
			acc.powerSum += framePower;
			acc.centroidBinSum += centroid;

			// Spectral crest: max(mag) / mean(mag), normalised by HALF_N
			if (frameMagSum > 1e-12) {
				double meanMag = frameMagSum / double(HALF_N);
				acc.crestAcc += (frameMaxMag / meanMag) / double(HALF_N);
			}

			// Spectral slope: Pearson r between bin index and magnitude
			{
				double cov = double(HALF_N) * frameKXSum - sum_k * frameMagSum;
				double var_x = double(HALF_N) * framePower - frameMagSum * frameMagSum;
				double denom = std::sqrt(var_k * std::max(0.0, var_x));
				acc.slopeAcc += (denom > 1e-12) ? cov / denom : 0.0;
			}

			// Per-frame centroid bin — needed for bandwidth/skewness/kurtosis
			const double centroidBinFrame = (framePower > 1e-12) ? centroid / framePower : 0.0;

			// Second pass: bandwidth, flatness, skewness, kurtosis, decrease, entropy
			double bwSq = 0.0, logMagSum = 0.0, linMagSum = 0.0;
			double sk3 = 0.0, sk4 = 0.0;
			double decNum = 0.0, decDen = 0.0;
			double entropy = 0.0;
			const double x0 = double(magCur[0]);
			for (int b = 0; b < HALF_N; ++b) {
				double m = double(magCur[b]);
				double p = m * m;
				double df = double(b) - centroidBinFrame;
				double df2 = df * df;
				bwSq += df2 * p;
				logMagSum += std::log(m + 1e-12);
				linMagSum += m;
				sk3 += df * df2 * p;
				sk4 += df2 * df2 * p;
				if (b > 0) { decNum += (m - x0) / double(b); decDen += m; }
				if (framePower > 1e-12) {
					double pn = p / framePower;
					if (pn > 1e-12) entropy -= pn * std::log(pn);
				}
			}
			acc.bwAcc += (framePower > 1e-12)
				? std::sqrt(bwSq / framePower) / double(HALF_N - 1)
				: 0.0;
			const double arithMean = linMagSum / double(HALF_N);
			acc.flatnessAcc += (arithMean > 1e-12)
				? std::exp(logMagSum / double(HALF_N)) / arithMean
				: 0.0;
			if (framePower > 1e-12) {
				const double bw = std::sqrt(bwSq / framePower);
				if (bw > 1e-12) {
					acc.skewnessAcc += (sk3 / framePower) / (bw * bw * bw);
					acc.kurtosisAcc += (sk4 / framePower) / (bw * bw * bw * bw);
				}
			}
			acc.decreaseAcc += (decDen > 1e-12) ? decNum / decDen : 0.0;
			acc.entropyAcc += entropy / std::log(double(HALF_N));

			// Mel filterbank: triangular filters, power energy per band
			double mel_hop[N_MELS] = {};
			for (int mf = 0; mf < N_MELS; ++mf) {
				const int lo = mel_bins[mf];
				const int ctr = mel_bins[mf + 1];
				const int hi = mel_bins[mf + 2];
				double e = 0.0;
				if (ctr > lo) {
					for (int b = lo; b < ctr; ++b) {
						e += double(magCur[b]) * double(magCur[b]) * double(b - lo) / double(ctr - lo);
					}
				}
				if (hi > ctr) {
					for (int b = ctr; b <= hi; ++b) {
						e += double(magCur[b]) * double(magCur[b]) * double(hi - b) / double(hi - ctr);
					}
				}
				acc.melAcc[mf] += e;
				mel_hop[mf] = e;
			}

			// Per-hop MFCC → delta accumulation
			// Tracks mean absolute frame-to-frame change per coefficient.
			// Static sounds (drone, pad) → near 0; melodic/rhythmic → higher.
			{
				float log_mel_hop[N_MELS];
				for (int m = 0; m < N_MELS; ++m) {
					log_mel_hop[m] = std::log(float(std::max(1e-10, mel_hop[m])));
				}

				float mfcc_cur[N_MFCC];
				for (int n = 0; n < N_MFCC; ++n) {
					float c = 0.f;
					for (int m = 0; m < N_MELS; ++m) {
						c += log_mel_hop[m] * std::cos(float(M_PI) * n * (m + 0.5f) / float(N_MELS));
					}
					mfcc_cur[n] = c;
				}

				if (acc.mfccPrevValid) {
					for (int n = 0; n < N_MFCC; ++n) {
						acc.mfccDeltaAcc[n] += std::abs(double(mfcc_cur[n]) - double(acc.mfccPrev[n]));
					}
				}
				std::copy(mfcc_cur, mfcc_cur + N_MFCC, acc.mfccPrev);
				acc.mfccPrevValid = true;
			}

			// 85% rolloff: smallest bin where cumulative power ≥ 0.85·framePower
			{
				const double rolloffTarget = 0.85 * framePower;
				double cum = 0.0;
				int rolloffBin = HALF_N - 1;
				if (framePower > 0.0) {
					for (int b = 0; b < HALF_N; ++b) {
						cum += double(magCur[b]) * double(magCur[b]);
						if (cum >= rolloffTarget) { rolloffBin = b; break; }
					}
				}
				acc.rolloff85Acc += double(rolloffBin) / double(HALF_N - 1);
			}

			std::swap(magCur, magPrev);
		}

		return true;
	}

	// Phase 4: Normalize accumulators → output feature vector
	// Writes the 30 spectral / MFCC / band-ratio / flux features and the
	// 13 MFCC deltas:
	//   out[0,1,4-9,12-17,18-30,37-39]  spectral + MFCCs + extra band ratios
	//   out[40-52]                       MFCC deltas
	static void finalizeSpectralFeatures(const TagClassifierDetail::SpectralAccum& acc,
			int outSR, float out[SIREN_TAG_NUM_FEATURES]) {
		using namespace TagClassifierDetail;
		const int numHops = acc.numHops;

		// Spectral centroid: (Σ b·p_b) / (Σ p_b), normalised by (HALF_N - 1)
		if (acc.powerSum > 0.0) {
			out[0] = float(acc.centroidBinSum / acc.powerSum) / float(HALF_N - 1);
		}

		// 85% rolloff: averaged over hops
		out[1] = float(acc.rolloff85Acc / double(numHops));

		// Onset density: peaks in median-filtered, mean-subtracted onset signal
		{
			std::vector<float> onsetFiltered(numHops);
			medianFilter3(onsetFiltered.data(), acc.onsetSignal.data(), numHops);
			meanSubtract(onsetFiltered.data(), numHops);
			float maxOnset = 0.f;
			for (float v : onsetFiltered) if (v > maxOnset) maxOnset = v;
			const float threshold = 0.5f * maxOnset;
			const int peaks = countPeaks(onsetFiltered.data(), numHops, threshold);
			const float peaksPerSec = float(peaks) * (float(outSR) / float(HOP)) / float(numHops);
			out[4] = peaksPerSec / ONSET_DENSITY_NORM;
		}

		// Band ratios: four non-overlapping frequency regions sum to 1.
		// sub_bass [0, 80) Hz · low_mid [80, 250) Hz · mid [250, 2000) Hz · high [2000+) Hz
		if (acc.powerSum > 0.0) {
			out[5] = float(acc.powerLow / acc.powerSum);  // low_mid  [80, 250) Hz
			out[8] = float(acc.powerHigh / acc.powerSum);  // high     [2000+)   Hz
			out[37] = float(acc.powerSubBass / acc.powerSum);  // sub_bass [0, 80)   Hz
			out[38] = float((acc.powerSum - acc.powerSubBass - acc.powerLow - acc.powerHigh)
				/ acc.powerSum);                   // mid      [250, 2000) Hz
		}

		// Spectral shape features — averaged over hops
		out[6] = float(acc.flatnessAcc / double(numHops));
		out[7] = float(acc.bwAcc / double(numHops));
		out[12] = float(acc.crestAcc / double(numHops));
		out[13] = float(acc.entropyAcc / double(numHops));
		out[14] = float(acc.slopeAcc / double(numHops)) * 0.5f + 0.5f;
		out[15] = std::max(0.f, std::min(1.f, float(acc.decreaseAcc / numHops) + 0.5f));
		out[16] = std::max(0.f, std::min(1.f, float(acc.skewnessAcc / numHops) / 6.f + 0.5f));
		out[17] = std::max(0.f, std::min(1.f, (float(acc.kurtosisAcc / numHops) - 3.f + 3.f) / 10.f));

		// Mean spectral flux and flux variance
		{
			float fluxSum = 0.f;
			for (int h = 0; h < numHops; ++h) fluxSum += acc.onsetSignal[h];
			out[9] = (fluxSum / float(numHops)) / MEAN_FLUX_NORM;

			const float fluxMean = fluxSum / float(numHops);
			double fluxVar = 0.0;
			for (int h = 0; h < numHops; ++h) {
				double d = double(acc.onsetSignal[h]) - double(fluxMean);
				fluxVar += d * d;
			}
			out[39] = std::min(1.f, float(std::sqrt(fluxVar / double(numHops))) / MEAN_FLUX_NORM);
		}

		// MFCCs (out[18..30]) — average mel energies, log, then DCT-II
		{
			float log_mel[N_MELS];
			for (int m = 0; m < N_MELS; ++m) {
				log_mel[m] = std::log(float(std::max(1e-10, acc.melAcc[m] / double(numHops))));
			}
			for (int n = 0; n < N_MFCC; ++n) {
				float c = 0.f;
				for (int m = 0; m < N_MELS; ++m) {
					c += log_mel[m] * std::cos(float(M_PI) * n * (m + 0.5f) / float(N_MELS));
				}
				const float norm = (n == 0) ? MFCC_NORM_0 : MFCC_NORM_N;
				out[18 + n] = std::max(0.f, std::min(1.f, c / norm + 0.5f));
			}
		}

		// MFCC deltas (out[40..52]) — mean absolute frame-to-frame difference.
		// Normalised with the same constants as the base MFCCs (no +0.5 offset
		// since deltas are non-negative). Static sounds → near 0; melodic/
		// rhythmic material → higher.
		{
			const int deltaHops = numHops - 1;
			if (deltaHops > 0) {
				for (int n = 0; n < N_MFCC; ++n) {
					const float meanDelta = float(acc.mfccDeltaAcc[n] / double(deltaHops));
					const float norm = (n == 0) ? MFCC_NORM_0 : MFCC_NORM_N;
					out[40 + n] = std::min(1.f, meanDelta / norm);
				}
			}
		}
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
