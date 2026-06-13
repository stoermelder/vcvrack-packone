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
	ghc::filesystem::path path;

	TempDir() {
		static int seq = 0;
		path = ghc::filesystem::temp_directory_path()
		       / ("siren_index_test_" + std::to_string(++seq));
		ghc::filesystem::create_directories(path);
	}

	~TempDir() { ghc::filesystem::remove_all(path); }

	std::string filePath(const std::string& name) const { return (path / name).string(); }
	std::string str() const { return path.string(); }
};

// ─── SirenIndexTask ───────────────────────────────────────────────────────────

namespace {

// Writes a short decodable silent WAV file so loadAudioInfo() can read its header.
void writeIndexTestWav(const std::string& path, int frames = 4410, int sampleRate = 44100, int channels = 2) {
	drwav_data_format fmt = {};
	fmt.container     = drwav_container_riff;
	fmt.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
	fmt.channels      = (drwav_uint32)channels;
	fmt.sampleRate    = (drwav_uint32)sampleRate;
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
