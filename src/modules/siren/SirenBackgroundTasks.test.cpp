#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "SirenFileSystem.hpp"
#include "SirenBackgroundTasks.hpp"
#include "SirenTest.hpp"

using namespace StoermelderPackOne::Siren;
using namespace StoermelderPackOne::Siren::filesystem;

Test::TestContext<> testContext;

// ─── TempDir RAII helper ──────────────────────────────────────────────────────
// Creates a unique temporary directory; removes it on destruction.

struct TempDir {
	std::string path;

	TempDir() {
		static int seq = 0;
		path = rack::system::join(rack::system::getTempDirectory(),
			"siren_index_test_" + std::to_string(++seq));
		rack::system::createDirectories(path);
	}

	~TempDir() { rack::system::removeRecursively(path); }

	std::string filePath(const std::string& name) const { return rack::system::join(path, name); }
	std::string str() const { return path; }
};

// ─── SirenIndexTask ───────────────────────────────────────────────────────────

namespace {

// Writes a short decodable silent WAV file so loadAudioInfo() can read its header.
void writeIndexTestWav(const std::string& path, int frames = 4410, int sampleRate = 44100, int channels = 2) {
	drwav_data_format fmt = {};
	fmt.container = drwav_container_riff;
	fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
	fmt.channels = (drwav_uint32)channels;
	fmt.sampleRate = (drwav_uint32)sampleRate;
	fmt.bitsPerSample = 32;
	drwav wav;
	drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr);
	std::vector<float> samples((size_t)frames * channels, 0.f);
	drwav_write_pcm_frames(&wav, (drwav_uint64)frames, samples.data());
	drwav_uninit(&wav);
}

} // namespace

// start() scans every file below the root, fills in audio info (duration,
// sample rate, bit depth, channels) and detects BPM from filenames — without
// overwriting BPM values that were already set.
TEST_CASE("SirenIndexTask: fills audio info and filename-based BPM, preserves existing BPM", "[Siren][Indexing]") {
	TempDir tmp;
	writeIndexTestWav(tmp.filePath("loop_120bpm.wav"), 4410, 44100, 2);
	writeIndexTestWav(tmp.filePath("other.wav"), 4410, 48000, 1);

	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());
	// Pre-existing BPM must survive indexing unchanged.
	src->getMetadata()->setBpm("/other.wav", 99.f, 1.f);

	StoermelderPackOne::TaskWorker worker;
	auto cancel = std::make_shared<std::atomic<bool>>(false);

	SirenIndexTask task;
	task.start(&worker, src, cancel);
	REQUIRE(task.progress != nullptr);

	while (!task.progress->done.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	MetadataStore* meta = src->getMetadata();

	REQUIRE(meta->samples.count("/loop_120bpm.wav") == 1);
	const SampleMetadata& m1 = meta->samples.at("/loop_120bpm.wav");
	REQUIRE(m1.sampleRate == 44100);
	REQUIRE(m1.channels == 2);
	REQUIRE(m1.bitDepth == 32);
	REQUIRE(m1.durationSeconds == Catch::Approx(0.1f));
	REQUIRE(m1.bpm == Catch::Approx(120.f));

	REQUIRE(meta->samples.count("/other.wav") == 1);
	const SampleMetadata& m2 = meta->samples.at("/other.wav");
	REQUIRE(m2.sampleRate == 48000);
	REQUIRE(m2.channels == 1);
	// BPM detected from the filename must not overwrite the pre-existing value.
	REQUIRE(m2.bpm == Catch::Approx(99.f));
	REQUIRE(m2.bpmConfidence == Catch::Approx(1.f));
}

// start() is a no-op when called again while a scan is still running.
TEST_CASE("SirenIndexTask: re-entrant call while running is ignored", "[Siren][Indexing]") {
	TempDir tmp;
	writeIndexTestWav(tmp.filePath("a.wav"));

	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());

	StoermelderPackOne::TaskWorker worker;
	auto cancel = std::make_shared<std::atomic<bool>>(false);

	SirenIndexTask task;
	task.start(&worker, src, cancel);
	auto firstProgress = task.progress;
	REQUIRE(firstProgress != nullptr);

	// A second call before the first finishes must not replace progress.
	task.start(&worker, src, cancel);
	REQUIRE(task.progress == firstProgress);

	while (!task.progress->done.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

// ─── SirenClassifyTask ────────────────────────────────────────────────────────

namespace {

// Writes a short stereo sine-wave WAV — needed because classify() is skipped
// when ds->openAudioStream() fails to decode the file's header.
void writeClassifyTestWav(const std::string& path, float freqHz = 440.f,
		int frames = 4410, int sampleRate = 44100, int channels = 2) {
	drwav_data_format fmt = {};
	fmt.container = drwav_container_riff;
	fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
	fmt.channels = (drwav_uint32)channels;
	fmt.sampleRate = (drwav_uint32)sampleRate;
	fmt.bitsPerSample = 32;
	drwav wav;
	drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr);
	std::vector<float> samples((size_t)frames * channels);
	for (int i = 0; i < frames; i++) {
		float v = 0.5f * std::sin(2.f * float(M_PI) * freqHz * float(i) / float(sampleRate));
		for (int c = 0; c < channels; c++) samples[i * channels + c] = v;
	}
	drwav_write_pcm_frames(&wav, (drwav_uint64)frames, samples.data());
	drwav_uninit(&wav);
}

// In-memory DataSource for tests where exercising classify()'s worker pipeline
// (file collection, progress, tag filtering, recursion) is the goal — the
// streams themselves don't need to be decodable, so openAudioStream() returns
// nullptr and each file simply contributes zero suggestions. Lets us test the
// task's bookkeeping without coupling to FileSystemDataSource's filesystem I/O.
struct SyntheticDataSource : DataSource {
	struct Node {
		std::string name;
		bool isContainer;
	};
	std::string root;
	std::map<std::string, std::vector<Node>> children;  // id -> children
	MetadataStore meta;

	explicit SyntheticDataSource(const std::string& rootPath) : root(rootPath) {
		meta.rootPath = root;
	}

	std::string rootId() const override { return root; }
	bool isSupportedFile(const std::string&) const override { return true; }

	std::vector<DataSourceNode> loadChildrenSync(const std::string& id, bool = true) override {
		std::vector<DataSourceNode> result;
		auto it = children.find(id);
		if (it == children.end()) return result;
		for (const Node& n : it->second) {
			DataSourceNode node;
			node.name = n.name;
			node.isContainer = n.isContainer;
			node.relativePath = (id == root ? "/" : id) + "/" + n.name;
			result.push_back(std::move(node));
		}
		return result;
	}

	void loadChildrenAsync(const std::string& id, StoermelderPackOne::TaskWorker&,
			std::function<void(std::vector<DataSourceNode>)> onDone) override {
		onDone(loadChildrenSync(id));
	}

	std::unique_ptr<AudioStream> openAudioStream(const std::string&) const override { return nullptr; }

	MetadataStore* getMetadata() override { return &meta; }
};

} // namespace

// start() walks every file below the root, fills in progress totals, and
// marks itself done. running() flips false after the worker completes.
// We do NOT call step() here because step() would open the tag-confirmation
// dialog, which is interactive — the test only asserts the progress state.
TEST_CASE("SirenClassifyTask: scans every file and reports progress totals", "[Siren][Classify]") {
	TempDir tmp;
	writeClassifyTestWav(tmp.filePath("a.wav"));
	writeClassifyTestWav(tmp.filePath("b.wav"), 220.f);

	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());

	StoermelderPackOne::TaskWorker worker;
	SirenClassifyTask task;
	REQUIRE_FALSE(task.running());

	task.start(&worker, src, src->getMetadata(), "/a.wav", false, "a.wav", nullptr);
	REQUIRE(task.running());
	REQUIRE(task.progress != nullptr);
	REQUIRE_FALSE(task.progress->done.load(std::memory_order_relaxed));

	while (!task.progress->done.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	REQUIRE(task.progress->total.load() == 1);
	REQUIRE(task.progress->processed.load() == 1);
	// progress is still held until step() is called — that would open the
	// dialog, which is deliberately NOT done in this test.
	REQUIRE(task.running());
	REQUIRE(task.progress->done.load());
}

// start() on a directory argument recursively collects every file under it.
// /sub contains a deeper container /sub/deeper which contains two files — the
// worker must recurse, count all 2 files, and process each one.
TEST_CASE("SirenClassifyTask: directory mode recurses into subdirectories", "[Siren][Classify]") {
	auto src = std::make_shared<SyntheticDataSource>("/");
	src->children["/sub"] = {SyntheticDataSource::Node{"deeper", true}};
	src->children["/sub/deeper"] = {
		SyntheticDataSource::Node{"deep1.wav", false},
		SyntheticDataSource::Node{"deep2.wav", false},
	};

	StoermelderPackOne::TaskWorker worker;
	SirenClassifyTask task;
	task.start(&worker, src, src->getMetadata(), "/sub", true, "sub", nullptr);

	REQUIRE(task.running());
	while (!task.progress->done.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	REQUIRE(task.progress->total.load() == 2);
	REQUIRE(task.progress->processed.load() == 2);
}

// start() is a no-op while a scan is already in progress.
TEST_CASE("SirenClassifyTask: re-entrant call while running is ignored", "[Siren][Classify]") {
	TempDir tmp;
	writeClassifyTestWav(tmp.filePath("a.wav"));

	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());

	StoermelderPackOne::TaskWorker worker;
	SirenClassifyTask task;
	task.start(&worker, src, src->getMetadata(), "/a.wav", false, "a.wav", nullptr);
	auto firstProgress = task.progress;
	REQUIRE(firstProgress != nullptr);

	task.start(&worker, src, src->getMetadata(), "/a.wav", false, "a.wav", nullptr);
	REQUIRE(task.progress == firstProgress);

	while (!task.progress->done.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

// start() drops a tag the file already has — the filter is case-insensitive
// and trims whitespace, matching MetadataStore::addTag()'s normalisation.
// We don't know which tag the model will emit, so we run the scan first to
// discover it, then re-run with that tag pre-applied and assert it's gone.
TEST_CASE("SirenClassifyTask: existing tags are filtered out of suggestions", "[Siren][Classify]") {
	TempDir tmp;
	writeClassifyTestWav(tmp.filePath("a.wav"));

	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());
	MetadataStore* meta = src->getMetadata();

	// First pass: discover whatever tag the classifier emits for /a.wav.
	std::string observedTag;
	{
		StoermelderPackOne::TaskWorker worker;
		SirenClassifyTask task;
		task.start(&worker, src, meta, "/a.wav", false, "a.wav", nullptr);
		while (!task.progress->done.load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		for (const auto& pair : task.progress->tagToRels) {
			if (pair.second.count("/a.wav") > 0) {
				observedTag = pair.first;
				break;
			}
		}
	}
	if (observedTag.empty()) {
		// The ML classifier emitted no tag for this synthetic WAV — the filter
		// logic is untestable without a suggestion to filter. Skip rather than
		// silently passing: a WARN + return would show as a green test, hiding
		// the fact that the invariant was never exercised.
		SKIP("Classifier emitted no tag for /a.wav — filter path is untested for this signal.");
	}

	// Pre-apply the observed tag (using the SAME normalisation addTag uses)
	// and re-run. The worker must skip emitting this tag for /a.wav.
	meta->addTag("/a.wav", observedTag);

	{
		StoermelderPackOne::TaskWorker worker;
		SirenClassifyTask task;
		task.start(&worker, src, meta, "/a.wav", false, "a.wav", nullptr);
		while (!task.progress->done.load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		REQUIRE(task.progress->tagToRels.count(observedTag) == 0);
	}
}

// statusMessage() is empty when no scan is active and reports "Analysing…"
// (with or without a total) while a scan is queued/running. We do NOT call
// step() — that would open a dialog and release the progress pointer.
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

TEST_CASE("SirenClassifyTask: statusMessage is empty when idle, non-empty while running", "[Siren][Classify]") {
	TempDir tmp;
	writeClassifyTestWav(tmp.filePath("a.wav"));

	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());

	StoermelderPackOne::TaskWorker worker;
	SirenClassifyTask task;

	REQUIRE(task.statusMessage() == "");

	task.start(&worker, src, src->getMetadata(), "/a.wav", false, "a.wav", nullptr);
	REQUIRE(task.statusMessage().find("Analysing") != std::string::npos);

	while (!task.progress->done.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(task.statusMessage().find("Analysing") != std::string::npos);
}
