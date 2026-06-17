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
