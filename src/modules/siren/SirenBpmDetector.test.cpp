#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "SirenBpmDetector.hpp"
#include "SirenDataSource.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace StoermelderPackOne::Siren;
namespace BpmDetail = StoermelderPackOne::Siren::detail;

Test::TestContext<> testContext;

// ─── Mock AudioStream ────────────────────────────────────────────────────────
// A trivial in-memory AudioStream used to drive detectBpm in tests. Holds
// interleaved float samples and tracks a read position so successive reads
// return consecutive frames (mirroring a real decoder).
struct MockAudioStream : AudioStream {
	std::vector<float> data;     // interleaved, length = frames * channels
	int sampleRate_   = 44100;
	int channels_     = 1;
	int64_t framesTotal_ = 0;
	int64_t pos_        = 0;    // read position in frames

	MockAudioStream() = default;

	// Build a stream from a pre-filled sample vector
	MockAudioStream(std::vector<float> samples, int sr, int ch)
		: data(std::move(samples)), sampleRate_(sr), channels_(ch) {
		framesTotal_ = int64_t(data.size()) / std::max(1, ch);
	}

	// Helper: generate a click train at a given BPM. Each click is a short
	// pulse with a decaying envelope, convolved with a low-frequency tone so
	// the spectrum has bass content (mirroring a real kick).  A low-amplitude
	// pink-noise bed is mixed in so the discrete autocorrelation has nonzero
	// values at all lags — without it, a click train with non-integer
	// hops/beat (e.g. 17.6) gives an autocorr that's exactly zero at every
	// integer lag, and the detector falls back to a 2× or 0.5× false positive.
	static std::vector<float> clickTrain(int sr, double bpm, double seconds, int channels) {
		int64_t totalFrames = int64_t(sr * seconds);
		std::vector<float> out(size_t(totalFrames) * size_t(std::max(1, channels)), 0.f);
		double interval = 60.0 / bpm;        // seconds between clicks
		int   pulseLen  = std::max(64, sr / 100);  // ~10 ms pulse
		float bassFreq  = 60.f;              // 60 Hz bass tone
		// Seeded noise so the test is deterministic.
		std::mt19937 rng(0xC0FFEE);
		std::uniform_real_distribution<float> noiseDist(-1.f, 1.f);
		for (int64_t idx = 0; idx < totalFrames; ++idx) {
			float noise = 0.005f * noiseDist(rng);
			for (int ch = 0; ch < channels; ++ch)
				out[size_t(idx) * size_t(channels) + size_t(ch)] = noise;
		}
		for (double t = 0.0; t < seconds; t += interval) {
			int64_t c0 = int64_t(t * sr);
			for (int i = 0; i < pulseLen; ++i) {
				int64_t idx = c0 + i;
				if (idx < 0 || idx >= totalFrames) break;
				float pulsePhase = float(i) / float(pulseLen);
				float env = (1.f - pulsePhase) * 0.5f * (1.f - std::cos(2.f * float(M_PI) * pulsePhase));
				float bass = std::cos(2.f * float(M_PI) * bassFreq * float(idx) / float(sr));
				float amp  = 0.9f * env * bass;
				for (int ch = 0; ch < channels; ++ch)
					out[size_t(idx) * size_t(channels) + size_t(ch)] += amp;
			}
		}
		return out;
	}

	int     channels()    const override { return channels_; }
	int     sampleRate()  const override { return sampleRate_; }
	int64_t totalFrames() const override { return framesTotal_; }

	int64_t readF32(float* buffer, int64_t frameCount) override {
		int64_t remaining = framesTotal_ - pos_;
		int64_t toRead    = std::min(frameCount, std::max<int64_t>(0, remaining));
		if (buffer && toRead > 0) {
			int ch = std::max(1, channels_);
			std::memcpy(buffer,
			            data.data() + size_t(pos_) * size_t(ch),
			            size_t(toRead) * size_t(ch) * sizeof(float));
			pos_ += toRead;
		}
		return toRead;
	}

	bool seekTo(int64_t frameIndex) override {
		if (frameIndex < 0 || frameIndex > framesTotal_) return false;
		pos_ = frameIndex;
		return true;
	}
};

// ─── Path extraction ─────────────────────────────────────────────────────────

TEST_CASE("extractBpmFromPath: filename with bpm suffix", "[Siren][BPM][Path]") {
	REQUIRE(extractBpmFromPath("/samples/kick_120bpm.wav")     == 120.f);
	REQUIRE(extractBpmFromPath("/samples/kick_120BPM.wav")     == 120.f);
	REQUIRE(extractBpmFromPath("/samples/kick_120 Bpm.wav")    == 120.f);
	REQUIRE(extractBpmFromPath("/samples/kick_120.5bpm.wav")   == Catch::Approx(120.5f));
	REQUIRE(extractBpmFromPath("/samples/kick_120.5BPM.wav")   == Catch::Approx(120.5f));
}

TEST_CASE("extractBpmFromPath: filename with bracket notation", "[Siren][BPM][Path]") {
	REQUIRE(extractBpmFromPath("/samples/loop[128].wav")     == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop [128].wav")    == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop[140.5].wav")   == Catch::Approx(140.5f));
}

TEST_CASE("extractBpmFromPath: filename with delimiter-padded number", "[Siren][BPM][Path]") {
	// "kick_120_808.wav" → 120; the first delimited match wins.
	REQUIRE(extractBpmFromPath("/samples/kick_120_808.wav")  == 120.f);
	REQUIRE(extractBpmFromPath("/samples/loop-140-pad.wav")  == 140.f);
}

TEST_CASE("extractBpmFromPath: leading number in filename", "[Siren][BPM][Path]") {
	// The leading-number pattern picks up "120_kick.wav"
	REQUIRE(extractBpmFromPath("/samples/120_kick.wav")      == 120.f);
	REQUIRE(extractBpmFromPath("/samples/90_ride.flac")      == 90.f);
}

TEST_CASE("extractBpmFromPath: audio extension is stripped before matching", "[Siren][BPM][Path]") {
	REQUIRE(extractBpmFromPath("/samples/loop_128.wav")  == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.WAV")  == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.mp3")  == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.flac") == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.aif")  == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.aiff") == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.ogg")  == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.m4a")  == 128.f);
	REQUIRE(extractBpmFromPath("/samples/loop_128.opus") == 128.f);
}

TEST_CASE("extractBpmFromPath: BPM extracted from parent folder name", "[Siren][BPM][Path]") {
	// No bpm in filename, but "Drum_Loops_120bpm" folder contains the file.
	REQUIRE(extractBpmFromPath("/samples/Drum_Loops_120bpm/kick.wav")   == 120.f);
	// Closer (more specific) folder wins if it is also in range.
	REQUIRE(extractBpmFromPath("/samples/Drum_Loops_120bpm/140bpm/kick.wav") == 140.f);
}

TEST_CASE("extractBpmFromPath: filename is preferred over parent folder", "[Siren][BPM][Path]") {
	// Filename says 128, parent says 120 — filename wins.
	REQUIRE(extractBpmFromPath("/samples/Drum_Loops_120bpm/loop_128.wav") == 128.f);
}

TEST_CASE("extractBpmFromPath: out-of-range numbers are ignored", "[Siren][BPM][Path]") {
	// 50 is below the 60 floor and 300 is above the 220 ceiling.
	REQUIRE(extractBpmFromPath("/samples/loop_50bpm.wav")    == 0.f);
	REQUIRE(extractBpmFromPath("/samples/loop_300bpm.wav")   == 0.f);
	REQUIRE(extractBpmFromPath("/samples/loop_220bpm.wav")   == 220.f);  // boundary OK
	REQUIRE(extractBpmFromPath("/samples/loop_60bpm.wav")    == 60.f);   // boundary OK
}

TEST_CASE("extractBpmFromPath: no BPM in path returns 0", "[Siren][BPM][Path]") {
	REQUIRE(extractBpmFromPath("/samples/kick.wav")           == 0.f);
	REQUIRE(extractBpmFromPath("/samples/synth pad.flac")     == 0.f);
	REQUIRE(extractBpmFromPath("")                            == 0.f);
}

TEST_CASE("extractBpmFromPath: filenames with spaces", "[Siren][BPM][Path]") {
	REQUIRE(extractBpmFromPath("/samples/Drone 110bpm.wav") == 110.f);
	REQUIRE(extractBpmFromPath("/samples/Drum Kit [120].wav") == 120.f);
}

TEST_CASE("extractBpmFromPath: case-insensitive BPM tag", "[Siren][BPM][Path]") {
	REQUIRE(extractBpmFromPath("/samples/kick_100Bpm.wav") == 100.f);
	REQUIRE(extractBpmFromPath("/samples/kick_100bPm.wav") == 100.f);
	REQUIRE(extractBpmFromPath("/samples/kick_100BPM.wav") == 100.f);
}

TEST_CASE("extractBpmFromPath: filename without extension still matches", "[Siren][BPM][Path]") {
	// Leading-number pattern matches a bare number or a number followed by a
	// delimiter; "120_loop" matches the leading pattern.
	REQUIRE(extractBpmFromPath("/samples/120_loop")     == 120.f);
	REQUIRE(extractBpmFromPath("/samples/Loop [128]")   == 128.f);
}

// ─── BpmDetector (path) ──────────────────────────────────────────────────────

TEST_CASE("BpmDetector::extractFromPath: forwards to free function", "[Siren][BPM][Path]") {
	REQUIRE(BpmDetector::extractFromPath("/samples/kick_120bpm.wav") == 120.f);
	REQUIRE(BpmDetector::extractFromPath("/samples/no_bpm.wav")      == 0.f);
}

// ─── Synthetic spectral detection ────────────────────────────────────────────

// Synthesise a click train at a known BPM and verify detectBpm lands on it.
// The detector decimates to ~4410 Hz and uses 4× overlap STFT, so a 60 BPM
// track gives an onset period of ~57.3 frames at 4410 Hz. We allow generous
// tolerance (±10%) because spectral flux is sensitive to envelope shape.
TEST_CASE("detectBpm: recovers BPM from a synthetic click train (120 BPM)", "[Siren][BPM][Spectral]") {
	const int sr       = 44100;
	const int ch       = 1;
	const double bpm   = 120.0;
	const double secs  = 12.0;
	auto samples = MockAudioStream::clickTrain(sr, bpm, secs, ch);
	MockAudioStream stream(std::move(samples), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, /*maxDurationSeconds=*/30.f);

	REQUIRE(detected > 0.f);
	REQUIRE(confidence > 0.f);
	REQUIRE(confidence <= 1.f);
	// 120 BPM target — allow ±10% (±12 BPM)
	REQUIRE(detected == Catch::Approx(120.f).margin(12.f));
}

TEST_CASE("detectBpm: recovers BPM from a synthetic click train (90 BPM)", "[Siren][BPM][Spectral]") {
	const int sr       = 44100;
	const int ch       = 1;
	const double bpm   = 90.0;
	const double secs  = 15.0;
	auto samples = MockAudioStream::clickTrain(sr, bpm, secs, ch);
	MockAudioStream stream(std::move(samples), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);

	REQUIRE(detected > 0.f);
	REQUIRE(confidence > 0.f);
	REQUIRE(detected == Catch::Approx(90.f).margin(9.f));
}

TEST_CASE("detectBpm: recovers BPM from a synthetic click train (140 BPM)", "[Siren][BPM][Spectral]") {
	const int sr       = 44100;
	const int ch       = 1;
	const double bpm   = 140.0;
	const double secs  = 12.0;
	auto samples = MockAudioStream::clickTrain(sr, bpm, secs, ch);
	MockAudioStream stream(std::move(samples), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);

	REQUIRE(detected > 0.f);
	REQUIRE(confidence > 0.f);
	REQUIRE(detected == Catch::Approx(140.f).margin(14.f));
}

// Stereo stream — both channels should be summed to mono.
TEST_CASE("detectBpm: handles stereo (interleaved) audio", "[Siren][BPM][Spectral]") {
	const int sr       = 44100;
	const int ch       = 2;
	const double bpm   = 120.0;
	const double secs  = 12.0;
	auto samples = MockAudioStream::clickTrain(sr, bpm, secs, ch);
	MockAudioStream stream(std::move(samples), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);
	REQUIRE(detected > 0.f);
	REQUIRE(detected == Catch::Approx(120.f).margin(15.f));
}

// 48 kHz input — exercises a different decimation factor. Use 90 BPM so the
// hops/beat ratio at the 4800 Hz decimated sample rate is an exact integer
// (25); non-integer ratios trigger half/double-time false positives for
// synthetic click-train signals because the discrete autocorr is exactly
// zero at every lag that doesn't align with a click.
TEST_CASE("detectBpm: handles 48 kHz input", "[Siren][BPM][Spectral]") {
	const int sr       = 48000;
	const int ch       = 1;
	const double bpm   = 90.0;
	const double secs  = 12.0;
	auto samples = MockAudioStream::clickTrain(sr, bpm, secs, ch);
	MockAudioStream stream(std::move(samples), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);
	REQUIRE(detected > 0.f);
	REQUIRE(detected == Catch::Approx(90.f).margin(9.f));
}

// Silence yields zero BPM (low confidence, no clear periodic structure).
TEST_CASE("detectBpm: silence yields no result (confidence below threshold)", "[Siren][BPM][Spectral]") {
	const int sr  = 44100;
	const int ch  = 1;
	std::vector<float> silent(sr * 5, 0.f);
	MockAudioStream stream(std::move(silent), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);
	REQUIRE(detected == 0.f);
	REQUIRE(confidence >= 0.f);
	REQUIRE(confidence < 0.20f);
}

// Short input (< 2 s after decimation) returns 0 — not enough data to estimate.
TEST_CASE("detectBpm: very short input returns 0", "[Siren][BPM][Spectral]") {
	const int sr  = 44100;
	const int ch  = 1;
	std::vector<float> shortClip(sr / 2, 0.f);   // 0.5 s of silence
	MockAudioStream stream(std::move(shortClip), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);
	REQUIRE(detected == 0.f);
	REQUIRE(confidence == 0.f);
}

// maxDurationSeconds caps the analysed region — a long file is still fast.
TEST_CASE("detectBpm: maxDurationSeconds caps the analysed region", "[Siren][BPM][Spectral]") {
	const int sr       = 44100;
	const int ch       = 1;
	const double bpm   = 120.0;
	const double secs  = 30.0;       // long input
	auto samples = MockAudioStream::clickTrain(sr, bpm, secs, ch);
	MockAudioStream stream(std::move(samples), sr, ch);

	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, /*maxDurationSeconds=*/6.f);
	REQUIRE(detected > 0.f);
	REQUIRE(detected == Catch::Approx(120.f).margin(12.f));
}

// Zero-channel stream (defensive) returns 0 instead of crashing.
TEST_CASE("detectBpm: zero-channel stream returns 0", "[Siren][BPM][Spectral]") {
	MockAudioStream stream;
	stream.channels_   = 0;
	stream.framesTotal_ = 1000;
	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);
	REQUIRE(detected == 0.f);
	REQUIRE(confidence == 0.f);
}

// totalFrames == 0 returns 0.
TEST_CASE("detectBpm: empty stream returns 0", "[Siren][BPM][Spectral]") {
	MockAudioStream stream;
	stream.channels_   = 1;
	stream.framesTotal_ = 0;
	float confidence = 0.f;
	float detected   = detectBpm(stream, confidence, 30.f);
	REQUIRE(detected == 0.f);
	REQUIRE(confidence == 0.f);
}

// BpmDetector::detect forwards to the free function.
TEST_CASE("BpmDetector::detect: forwards to free function", "[Siren][BPM][Spectral]") {
	const int sr       = 44100;
	const int ch       = 1;
	const double bpm   = 120.0;
	const double secs  = 12.0;
	auto samples = MockAudioStream::clickTrain(sr, bpm, secs, ch);
	MockAudioStream stream(std::move(samples), sr, ch);

	float confidence = 0.f;
	float detected   = BpmDetector::detect(stream, confidence, 30.f);
	REQUIRE(detected > 0.f);
	REQUIRE(detected == Catch::Approx(120.f).margin(12.f));
}

// ─── Diagnostic: autocorrelator on a known impulse train ──────────────────────
// Verifies the autocorrelator in isolation, separately from the STFT path.
// Use a small frame rate so the period lands inside the [MIN_BPM, MAX_BPM] lag
// window.  With frameRate=10 and MAX_BPM=200, lagMin=3, lagMax=10 — periods
// 5/7/10 all fall in range.
TEST_CASE("diagnostic: autocorrelator on impulse train", "[Siren][BPM][Diagnostic]") {
	struct Period { int p; };
	auto periods = GENERATE(Period{5}, Period{7}, Period{10});

	std::vector<float> sig(400, 0.f);
	for (int i = 0; i < (int)sig.size(); i += periods.p) sig[i] = 1.f;
	sig[0] = 1.f;
	auto r = BpmDetail::autocorrelate(sig.data(), (int)sig.size(), /*frameRate=*/10.f);
	DYNAMIC_SECTION("period " << periods.p) {
		REQUIRE(r.bestLag == periods.p);
		REQUIRE(r.confidence > 0.5f);
	}
}
