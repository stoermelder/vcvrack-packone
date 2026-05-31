#pragma once
#include "SirenAudio.hpp"
#include "SirenDataSource.hpp"
#include <pffft.h>
#include <regex>


namespace StoermelderPackOne {
namespace Siren {


// ─── Path extraction ─────────────────────────────────────────────────────────

// Regex-based BPM extraction from filenames and folder names.
// Returns the first match in the range [60, 220] BPM, scanning filename first,
// then parent folders from nearest to root. Returns 0 if nothing found.
inline float extractBpmFromPath(const std::string& fullPath) {
    // Collect all path components: filename first, then each parent dir
    std::vector<std::string> components;
    ghc::filesystem::path p(fullPath);
    for (auto it = p.begin(); it != p.end(); ++it)
        components.push_back(it->string());
    // Reverse so filename (last element) is checked first
    std::reverse(components.begin(), components.end());

    for (const std::string& comp : components) {
        std::string name = comp;
        // Strip extension
        size_t dotPos = name.rfind('.');
        if (dotPos != std::string::npos) {
            std::string ext = name.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg" ||
                ext == "aiff" || ext == "aif" || ext == "m4a" || ext == "wma")
                name = name.substr(0, dotPos);
        }

        // Try patterns — wrap in try/catch in case of regex issues
        std::vector<std::pair<std::string, std::regex>> patterns = {
            {R"((\d+(?:\.\d+)?)\s*bpm)",  std::regex(R"((\d+(?:\.\d+)?)\s*bpm)",  std::regex::icase)},
            {R"(\[(\d+(?:\.\d+)?)\])",     std::regex(R"(\[(\d+(?:\.\d+)?)\])")},
            {R"((\d+(?:\.\d+)?)\s*BPM)",   std::regex(R"((\d+(?:\.\d+)?)\s*BPM)", std::regex::icase)},
            {R"([_\-](\d+(?:\.\d+)?)[_\-])", std::regex(R"([_\-](\d+(?:\.\d+)?)[_\-])")},
            {R"(^(\d+(?:\.\d+)?))",        std::regex(R"(^(\d+(?:\.\d+)?))")},
        };

        for (const auto& p : patterns) {
            (void)p.first; // silence unused warning
            std::smatch m;
            try {
                if (std::regex_search(name, m, p.second)) {
                    float bpm = std::stof(m[1].str());
                    if (bpm >= 60.f && bpm <= 220.f)
                        return bpm;
                }
            } catch (const std::regex_error&) {
                continue;
            }
        }
    }
    return 0.f;
}


// ─── Spectral analysis (spectral flux onset + autocorrelation) ─────────────────

// Constants for BPM detection
static constexpr int    BPM_FFT_SIZE       = 512;   // STFT window size (power of 2, multiple of 16)
static constexpr int    BPM_FFT_HOP        = 128;   // Hop size (window advance)
static constexpr int    BPM_DECIM_RATE     = 10;    // Downsample to 4410 Hz from 44100 Hz
static constexpr float  BPM_MIN_BPM         = 60.f;
static constexpr float  BPM_MAX_BPM         = 200.f;
static constexpr float  BPM_CONFIDENCE_THR  = 0.15f; // Minimum confidence to accept result

// Window function (Hann) generation — fills output[0..N-1]
inline void generateHannWindow(float* output, int N) {
    for (int i = 0; i < N; ++i)
        output[i] = 0.5f * (1.f - std::cos(2.f * M_PI * i / (N - 1)));
}

// Forward FFT using pffft — out must have size >= N
inline void fftForward(const float* input, float* output, float* work, PFFFT_Setup* setup) {
    pffft_transform_ordered(setup, input, output, work, PFFFT_FORWARD);
}

// Backward FFT using pffft
inline void fftBackward(const float* input, float* output, float* work, PFFFT_Setup* setup) {
	pffft_transform_ordered(setup, input, output, work, PFFFT_BACKWARD);
	// Scale by 1/N (pffft is not normalized)
	int n = BPM_FFT_SIZE;
	for (int i = 0; i < n; ++i)
		output[i] *= (1.f / n);
}

// Compute magnitude spectrum from FFT output (interleaved real: [r0, r1, r2, ...])
inline void computeMagnitudes(const float* fftOut, float* mag, int halfSize) {
    for (int i = 0; i < halfSize; ++i) {
        float re = fftOut[i * 2];
        float im = (i * 2 + 1 < BPM_FFT_SIZE) ? fftOut[i * 2 + 1] : 0.f;
        mag[i] = std::sqrt(re * re + im * im);
    }
}

// Spectral flux onset function: half-wave rectified positive difference
inline void computeSpectralFlux(const float* magCur, const float* magPrev, float* flux, int numBins) {
    float totalFlux = 0.f;
    for (int i = 0; i < numBins; ++i) {
        float diff = magCur[i] - magPrev[i];
        if (diff > 0.f) {
            // Sub-band weighting: boost low frequencies (bass/kick priority)
            float weight = (i < 8) ? 2.f : 1.f; // First ~340 Hz at 4410 Hz
            totalFlux += diff * weight;
        }
    }
    *flux = totalFlux;
}

// Autocorrelation via FFT: zero-pad signal to power-of-2 length >= 2*len
// Returns the best lag (in frames) and confidence score
struct AutocorrResult {
    int    bestLag;
    float  confidence;
};

inline AutocorrResult autocorrelate(const float* onsetSignal, int numFrames, float frameRate) {
    AutocorrResult result{0, 0.f};
    if (numFrames < 8) return result;

    // Pad to next power of 2 >= 2*numFrames
    int padLen = 1;
    while (padLen < 2 * numFrames) padLen <<= 1;

    std::vector<float> padded(padLen, 0.f);
    std::vector<float> spectrum(padLen, 0.f);
    std::vector<float> work(padLen, 0.f);

    // Window the onset signal
    for (int i = 0; i < numFrames; ++i)
        padded[i] = onsetSignal[i];

    // Forward FFT
    PFFFT_Setup* fft = pffft_new_setup(padLen, PFFFT_REAL);
    if (!fft) return result;

    pffft_transform_ordered(fft, padded.data(), spectrum.data(), work.data(), PFFFT_FORWARD);

    // Multiply by its complex conjugate (magnitude-squared in freq domain)
    for (int i = 0; i < padLen / 2; ++i) {
        float re = spectrum[i * 2];
        float im = spectrum[i * 2 + 1];
        spectrum[i * 2]     = re * re + im * im;
        spectrum[i * 2 + 1] = 0.f; // Zero imaginary for real output
    }
    // Mirror for real signal
    for (int i = padLen / 2 + 1; i < padLen; ++i) {
        spectrum[i * 2]     = spectrum[(padLen - i) * 2];
        spectrum[i * 2 + 1] = -spectrum[(padLen - i) * 2 + 1];
    }

    // Inverse FFT
    pffft_transform_ordered(fft, spectrum.data(), padded.data(), work.data(), PFFFT_BACKWARD);

    // Normalize by squared magnitude to get proper autocorrelation
    // The value at index 0 represents total energy; use it to normalize
    float totalEnergy = padded[0];
    if (totalEnergy <= 0.f) {
        pffft_destroy_setup(fft);
        return result;
    }
    float invEnergy = 1.f / totalEnergy;

    pffft_destroy_setup(fft);

    // Lag range: 60-200 BPM
    int lagMin = (int)std::max(1, (int)std::round(frameRate * 60.f / BPM_MAX_BPM));
    int lagMax = (int)std::round(frameRate * 60.f / BPM_MIN_BPM);
    if (lagMax >= numFrames) lagMax = numFrames - 1;
    if (lagMin >= lagMax) return result;

    // Find peak in normalized autocorrelation (values should be 0-1 now)
    float peakVal = -1.f;
    int peakIdx = lagMin;

    for (int i = lagMin; i <= lagMax; ++i) {
        float normVal = padded[i] * invEnergy;
        if (normVal > peakVal) {
            peakVal = normVal;
            peakIdx = i;
        }
    }

    // Normalized confidence (peak value is now 0-1)
    float confidence = peakVal;
    if (confidence < BPM_CONFIDENCE_THR) return result;

    // Check 2x and 0.5x harmonics for half-time/double-time ambiguity
    int candidateLags[3] = { peakIdx, peakIdx / 2, peakIdx * 2 };
    float bestPeak = -1.f;
    int bestLag = peakIdx;

    for (int cand : candidateLags) {
        if (cand < lagMin || cand > lagMax) continue;
        float normVal = padded[cand] * invEnergy;
        if (normVal > bestPeak) {
            bestPeak = normVal;
            bestLag = cand;
        }
    }

    result.bestLag    = bestLag;
    result.confidence = bestPeak; // Normalized to 0-1 range
    return result;
}

// ─── Main detect function ────────────────────────────────────────────────────

// Offline BPM detection via spectral flux onset + autocorrelation.
// Must be called on a worker thread, never the DSP thread.
// Returns detected BPM, or 0 if detection failed or confidence is below threshold.
// confidenceOut receives the raw confidence value (0–1) regardless of threshold.
inline float detectBpm(AudioStream& stream, float& confidenceOut,
                       float maxDurationSeconds = 60.f) {
    confidenceOut = 0.f;

    int sampleRate = stream.sampleRate();
    int channels   = stream.channels();
    int64_t totalFrames = stream.totalFrames();
    if (totalFrames <= 0 || sampleRate <= 0 || channels <= 0)
        return 0.f;

    // Limit to maxDurationSeconds
    int64_t maxFrames = (int64_t)(sampleRate * maxDurationSeconds);
    if (totalFrames > maxFrames)
        totalFrames = maxFrames;

    // Decimate to DECIM_RATE (e.g., 44100/10 = 4410 Hz)
    int decimRate = std::max(1, sampleRate / 4410);
    int outSR = sampleRate / decimRate;
    if (outSR <= 0) outSR = 4410;

    // Read all frames into memory (mono mix, decimated)
    std::vector<float> monoFrames;
    monoFrames.reserve(totalFrames / decimRate + 1024);

    const int64_t BUFSIZE = 65536;
    std::vector<float> buf((size_t)BUFSIZE * channels);
    int64_t framesRead = 0;

    while (framesRead < totalFrames) {
        int64_t toRead = std::min<int64_t>(BUFSIZE, totalFrames - framesRead);
        int64_t got = stream.readF32(buf.data(), toRead);
        if (got <= 0) break;

        for (int64_t f = 0; f < got; f += decimRate) {
            float sum = 0.f;
            int chCount = 0;
            for (int ch = 0; ch < channels && (f * channels + ch) < (int64_t)buf.size(); ++ch) {
                sum += buf[(size_t)(f * channels + ch)];
                ++chCount;
            }
            if (chCount > 0) monoFrames.push_back(sum / chCount);
        }
        framesRead += got;
    }

    if ((int)monoFrames.size() < 512) return 0.f; // Not enough data

    // STFT parameters
    const int FFT_SIZE = BPM_FFT_SIZE;
    const int HOP     = BPM_FFT_HOP;
    int numFrames = (int)monoFrames.size();
    int numHops   = (numFrames - FFT_SIZE) / HOP + 1;
    if (numHops < 4) return 0.f;

    // Precompute Hann window
    std::vector<float> window(FFT_SIZE);
    generateHannWindow(window.data(), FFT_SIZE);

    // Spectral flux onset signal
    std::vector<float> onsetSignal(numHops, 0.f);
    std::vector<float> magCur(FFT_SIZE / 2, 0.f);
    std::vector<float> magPrev(FFT_SIZE / 2, 0.f);
    std::vector<float> windowed(FFT_SIZE, 0.f);
    std::vector<float> spectrum(FFT_SIZE, 0.f);
    std::vector<float> work(FFT_SIZE, 0.f);

    PFFFT_Setup* fft = pffft_new_setup(FFT_SIZE, PFFFT_REAL);
    if (!fft) return 0.f;

    // First frame — compute initial spectrum
    for (int i = 0; i < FFT_SIZE; ++i)
        windowed[i] = monoFrames[i] * window[i];
    pffft_transform_ordered(fft, windowed.data(), spectrum.data(), work.data(), PFFFT_FORWARD);
    computeMagnitudes(spectrum.data(), magPrev.data(), FFT_SIZE / 2);

    // Process each hop
    for (int h = 0; h < numHops; ++h) {
        int startIdx = h * HOP;
        for (int i = 0; i < FFT_SIZE && startIdx + i < numFrames; ++i)
            windowed[i] = monoFrames[startIdx + i] * window[i];
        for (int i = numFrames - startIdx; i < FFT_SIZE; ++i)
            windowed[i] = 0.f; // Zero-pad

        pffft_transform_ordered(fft, windowed.data(), spectrum.data(), work.data(), PFFFT_FORWARD);
        computeMagnitudes(spectrum.data(), magCur.data(), FFT_SIZE / 2);

        float flux = 0.f;
        computeSpectralFlux(magCur.data(), magPrev.data(), &flux, FFT_SIZE / 2);
        onsetSignal[h] = flux;

        // Swap for next iteration
        std::swap(magCur, magPrev);
    }

    pffft_destroy_setup(fft);

    // Compute frame rate of onset signal
    float hopDuration = (float)HOP / (float)outSR;
    float onsetFrameRate = 1.f / hopDuration;

    // Autocorrelation
    auto acResult = autocorrelate(onsetSignal.data(), numHops, onsetFrameRate);
    if (acResult.bestLag <= 0) return 0.f;

    confidenceOut = acResult.confidence;
    if (acResult.confidence < BPM_CONFIDENCE_THR) return 0.f;

    // Convert lag to BPM
    float bpm = 60.f * onsetFrameRate / (float)acResult.bestLag;

    // Clamp to reasonable range
    if (bpm < 30.f || bpm > 250.f) return 0.f;

    return bpm;
}


// ─── BpmDetector struct (API) ───────────────────────────────────────────────

struct BpmDetector {
    // Extract BPM from filename/folder — fast path, UI thread safe
    static float extractFromPath(const std::string& path) {
        return StoermelderPackOne::Siren::extractBpmFromPath(path);
    }

    // Full spectral analysis — must be called on a worker thread
    // Returns BPM or 0 on failure / low confidence
    static float detect(AudioStream& stream, float& confidenceOut,
                        float maxDurationSeconds = 60.f) {
        return StoermelderPackOne::Siren::detectBpm(stream, confidenceOut, maxDurationSeconds);
    }
};


} // namespace Siren
} // namespace StoermelderPackOne