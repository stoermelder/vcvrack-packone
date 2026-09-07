#include "../../test/framework.hpp"
#include "SirenDataSource.hpp"
#include <cmath>
#include <vector>

using namespace StoermelderPackOne::Siren;

Test::TestContext<> testContext;

// ─── applyDeclick ─────────────────────────────────────────────────────────────

TEST_CASE("applyDeclick: first and last frames are zeroed", "[Siren][Audio][Declick]") {
	const int channels = 2;
	const int64_t N = 44100;
	std::vector<float> samples((size_t)(N * channels), 0.8f);

	applyDeclick(samples, channels);

	// Both channels of the very first frame must be exactly zero.
	REQUIRE(samples[0] == 0.f);
	REQUIRE(samples[1] == 0.f);

	// Both channels of the very last frame must be exactly zero.
	REQUIRE(samples[(size_t)((N - 1) * channels + 0)] == 0.f);
	REQUIRE(samples[(size_t)((N - 1) * channels + 1)] == 0.f);
}

TEST_CASE("applyDeclick: interior samples are unchanged", "[Siren][Audio][Declick]") {
	const int channels = 1;
	const int64_t N = 44100;
	std::vector<float> samples((size_t)(N * channels), 1.f);

	applyDeclick(samples, channels);

	REQUIRE(samples[(size_t)(N / 2)] == 1.f);
}

TEST_CASE("applyDeclick: mono and stereo both zeroed at boundaries", "[Siren][Audio][Declick]") {
	const int64_t N = 44100;
	for (int ch : {1, 2}) {
		std::vector<float> samples((size_t)(N * ch), 1.f);
		applyDeclick(samples, ch);
		for (int c = 0; c < ch; c++) {
			REQUIRE(samples[(size_t)c] == 0.f);
			REQUIRE(samples[(size_t)((N - 1) * ch + c)] == 0.f);
		}
	}
}

// ─── applyDeclickZeroCross ────────────────────────────────────────────────────

TEST_CASE("applyDeclickZeroCross: trims start to first zero crossing", "[Siren][Audio][Declick]") {
	// Build a buffer that crosses zero at frame 5 (channel 0 goes from negative to positive).
	const int sampleRate = 44100;
	const int channels = 1;
	std::vector<float> samples(sampleRate, 1.f);  // 1 second, all positive
	// Force a sign change at frame 5: frames 0-4 negative, frame 5+ positive.
	for (int i = 0; i < 5; i++) samples[(size_t)i] = -0.3f;

	size_t originalSize = samples.size();
	applyDeclickZeroCross(samples, channels, sampleRate);

	// The first 5 frames (negative side) should have been removed.
	REQUIRE(samples.size() < originalSize);
	// New frame 0 is the first positive sample; it must be non-negative.
	REQUIRE(samples[0] >= 0.f);
}

TEST_CASE("applyDeclickZeroCross: trims end to last zero crossing", "[Siren][Audio][Declick]") {
	const int sampleRate = 44100;
	const int channels = 1;
	const int N = sampleRate;
	std::vector<float> samples((size_t)N, 1.f);
	// Force a sign change near the end: last 5 frames positive, frame N-6 negative.
	for (int i = N - 5; i < N; i++) samples[(size_t)i] = 0.2f;
	samples[(size_t)(N - 6)] = -0.1f;  // crossing between N-6 and N-5

	size_t originalSize = samples.size();
	applyDeclickZeroCross(samples, channels, sampleRate);

	// Tail frames beyond the crossing should have been removed.
	REQUIRE(samples.size() < originalSize);
}

TEST_CASE("applyDeclickZeroCross: falls back to fade when no zero crossing found in window", "[Siren][Audio][Declick]") {
	// A buffer with constant positive value — no zero crossing anywhere.
	const int sampleRate = 44100;
	const int channels = 1;
	const int N = sampleRate;
	std::vector<float> samples((size_t)N, 0.8f);

	applyDeclickZeroCross(samples, channels, sampleRate);

	// No ZC found → applyDeclick fallback → first and last samples must be zero.
	REQUIRE(samples[0] == Catch::Approx(0.f).margin(1e-6f));
	REQUIRE(samples.back() == Catch::Approx(0.f).margin(1e-6f));
}

TEST_CASE("applyDeclickZeroCross: no-op on degenerate inputs", "[Siren][Audio][Declick]") {
	SECTION("empty buffer") {
		std::vector<float> empty;
		applyDeclickZeroCross(empty, 1, 44100);
		REQUIRE(empty.empty());
	}
	SECTION("zero channels") {
		std::vector<float> buf(100, 1.f);
		applyDeclickZeroCross(buf, 0, 44100);
		REQUIRE(buf[0] == Catch::Approx(1.f));
	}
}

TEST_CASE("applyDeclick: no-op on degenerate inputs", "[Siren][Audio][Declick]") {
	SECTION("empty buffer") {
		std::vector<float> empty;
		applyDeclick(empty, 1);
		REQUIRE(empty.empty());
	}
	SECTION("zero channels") {
		std::vector<float> buf(100, 1.f);
		applyDeclick(buf, 0);
		REQUIRE(buf[0] == 1.f);
	}
}

// ─── applyLoopCrossfade ───────────────────────────────────────────────────────

// Regression: loop export must NOT force the buffer endpoints to zero.
// applyLoopCrossfade places the loop seam at two consecutive source samples
// straddling a channel-0 zero crossing, so the wrap (out[last] → out[0]) is
// continuous in value and slope on EVERY channel — even channels whose value at
// the seam is far from zero. The old code ran applyDeclick on loop files, which
// zeroed both endpoints on every channel and punched a notch into the
// non-reference channel(s), producing the audible click it meant to remove.
TEST_CASE("applyLoopCrossfade: stereo loop seam is continuous on all channels", "[Siren][Audio][Loop]") {
	const int sampleRate = 44100;
	const int channels = 2;
	const int64_t N = sampleRate;  // 1 second
	const float freq = 441.f;      // plenty of zero crossings within the search window

	// Channel 0 = sine, channel 1 = cosine — so where channel 0 crosses zero
	// (where the seam is placed) channel 1 is near its peak (|value| ≈ 1).
	std::vector<float> samples((size_t)(N * channels));
	for (int64_t f = 0; f < N; f++) {
		float t = (float)f / (float)sampleRate;
		samples[(size_t)(f * channels + 0)] = std::sin(2.f * float(M_PI) * freq * t);
		samples[(size_t)(f * channels + 1)] = std::cos(2.f * float(M_PI) * freq * t);
	}

	applyLoopCrossfade(samples, channels, sampleRate, /*crossfadeSecs=*/0.01f);
	REQUIRE(!samples.empty());

	const int64_t outN = (int64_t)(samples.size() / (size_t)channels);
	REQUIRE(outN > 2);

	// The wrap reproduces two consecutive source samples, so the per-channel jump
	// must stay within a few samples' worth of this sine's maximum slope.
	const float perSampleDelta = 2.f * float(M_PI) * freq / (float)sampleRate;
	const float tol = perSampleDelta * 4.f;

	bool anyChannelFarFromZero = false;
	for (int ch = 0; ch < channels; ch++) {
		float first = samples[(size_t)(0 * channels + ch)];
		float last  = samples[(size_t)((outN - 1) * channels + ch)];
		REQUIRE(std::abs(first - last) <= tol);  // continuous wrap on this channel
		if (std::abs(first) > 0.3f) anyChannelFarFromZero = true;
	}
	// At least one channel sits well away from zero at the seam — proving the old
	// "zero both endpoints" behaviour would have created an audible notch there.
	REQUIRE(anyChannelFarFromZero);
}

// Off-by-one guard: the loop seam must reproduce two *consecutive* source frames
// (out[last] = src[M-1], out[0] = src[M]). A linear ramp makes any 1-sample gap
// visible — its sample-to-sample delta is constant, so the wrap step must equal
// the interior step exactly. A sine would hide a one-sample error; a ramp can't.
TEST_CASE("applyLoopCrossfade: loop wrap is exactly one source step (no off-by-one)", "[Siren][Audio][Loop]") {
	const int sampleRate = 48000;
	const int channels = 1;
	const int64_t N = 20000;
	const float step = 0.00005f;  // ramp slope per frame

	// Ramp centred on zero so a zero crossing exists at the midpoint (where the
	// seam is placed): src[i] = (i - N/2) * step.
	std::vector<float> samples((size_t)N);
	for (int64_t i = 0; i < N; i++) samples[(size_t)i] = (float)(i - N / 2) * step;

	applyLoopCrossfade(samples, channels, sampleRate, /*crossfadeSecs=*/0.02f);
	const int64_t outN = (int64_t)samples.size();
	REQUIRE(outN > 2);

	// The wrap delta (out[0] - out[last]) must equal a single ramp step — proving
	// out[0] and out[last] are adjacent source frames, i.e. no sample is dropped
	// or duplicated at the loop point.
	float wrapDelta = samples[0] - samples[(size_t)(outN - 1)];
	REQUIRE(wrapDelta == Catch::Approx(step).margin(1e-7f));
}
