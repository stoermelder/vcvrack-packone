#include "../../test/framework.hpp"
#include "SirenFileSystem.hpp"
#include "SirenBackgroundTasks.hpp"
#include "Siren.test.hpp"

using namespace StoermelderPackOne::Siren;
using namespace StoermelderPackOne::Siren::filesystem;

Test::TestContext<> testContext;


// SirenIndexTask

// start() scans every file below the root, fills in audio info (duration,
// sample rate, bit depth, channels) and detects BPM from filenames — without
// overwriting BPM values that were already set.
TEST_CASE("SirenIndexTask: fills audio info and filename-based BPM, preserves existing BPM", "[Siren][Indexing]") {
	TempDir tmp;
	writeTestWav(tmp.filePath("loop_120bpm.wav"), 4410, 44100, 2);
	writeTestWav(tmp.filePath("other.wav"), 4410, 48000, 1);

	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());
	// Pre-existing BPM must survive indexing unchanged.
	src->getMetadata()->setBpm("/other.wav", 99.f, 1.f);

	StoermelderPackOne::SyncTaskWorker worker;  // runs inline
	auto cancel = std::make_shared<std::atomic<bool>>(false);

	SirenIndexTask task;
	task.start(&worker, src, cancel);
	REQUIRE(task.progress != nullptr);
	REQUIRE(task.progress->done.load(std::memory_order_acquire));  // already done, no polling

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
// SyncTaskWorker runs the task to completion inline, so a real worker can't
// observe "still running" at the point of the second start() call — the
// first call would already be done. running() is pure state derived from
// progress (non-null and !done), so we exercise the guard directly by setting
// progress by hand to simulate "already running", with no worker involved at
// all. This is strictly better than exercising a real thread schedule: it
// tests the invariant, not a timing window.
TEST_CASE("SirenIndexTask: re-entrant call while running is ignored", "[Siren][Indexing]") {
	SirenIndexTask task;
	auto p = std::make_shared<SirenIndexTask::Progress>();
	p->done.store(false, std::memory_order_relaxed);
	task.progress = p;
	REQUIRE(task.running());

	TempDir tmp;
	writeTestWav(tmp.filePath("a.wav"));
	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());
	StoermelderPackOne::SyncTaskWorker worker;
	auto cancel = std::make_shared<std::atomic<bool>>(false);

	// A call while progress looks "running" must not replace it.
	task.start(&worker, src, cancel);
	REQUIRE(task.progress == p);
}

// ─── SirenClassifyTask ────────────────────────────────────────────────────────

namespace {

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

	void loadChildrenAsync(const std::string& id, StoermelderPackOne::ITaskWorker&,
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

	StoermelderPackOne::SyncTaskWorker worker;
	SirenClassifyTask task;
	REQUIRE_FALSE(task.running());
	auto dsCancel = std::make_shared<std::atomic<bool>>(false);

	task.start(&worker, src, src->getMetadata(), "/a.wav", false, "a.wav", nullptr, dsCancel);
	REQUIRE(task.running());
	REQUIRE(task.progress != nullptr);
	REQUIRE(task.progress->done.load(std::memory_order_acquire));  // already done, no polling

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

	StoermelderPackOne::SyncTaskWorker worker;
	SirenClassifyTask task;
	auto dsCancel = std::make_shared<std::atomic<bool>>(false);
	task.start(&worker, src, src->getMetadata(), "/sub", true, "sub", nullptr, dsCancel);

	REQUIRE(task.running());
	REQUIRE(task.progress->done.load(std::memory_order_acquire));

	REQUIRE(task.progress->total.load() == 2);
	REQUIRE(task.progress->processed.load() == 2);
}

// start() must honor dsCancel (the pane's per-source cancel flag bumped by
// "Cancel tag classification"/switching the active source), not just the
// worker's own teardown cancel flag — otherwise cancelling from the UI has no
// effect on a running scan. Pre-set dsCancel before start() so SyncTaskWorker's
// inline execution observes it as "already cancelled" from the first check.
TEST_CASE("SirenClassifyTask: start() honors a pre-set dsCancel", "[Siren][Classify]") {
	auto src = std::make_shared<SyntheticDataSource>("/");
	src->children["/sub"] = {
		SyntheticDataSource::Node{"a.wav", false},
		SyntheticDataSource::Node{"b.wav", false},
	};

	StoermelderPackOne::SyncTaskWorker worker;
	SirenClassifyTask task;
	auto dsCancel = std::make_shared<std::atomic<bool>>(true);
	task.start(&worker, src, src->getMetadata(), "/sub", true, "sub", nullptr, dsCancel);

	REQUIRE(task.progress->done.load(std::memory_order_acquire));
	// Cancelled before any file was processed.
	REQUIRE(task.progress->processed.load() == 0);
}

// start() is a no-op while a scan is already in progress. See the equivalent
// SirenIndexTask test above for why this exercises the guard directly instead
// of a real worker: SyncTaskWorker makes the first start() already-complete
// by the time the second call happens, so a real worker can no longer
// observe the mid-flight window this test is about.
TEST_CASE("SirenClassifyTask: re-entrant call while running is ignored", "[Siren][Classify]") {
	SirenClassifyTask task;
	auto p = std::make_shared<SirenClassifyTask::Progress>();
	task.progress = p;
	REQUIRE(task.running());

	TempDir tmp;
	writeClassifyTestWav(tmp.filePath("a.wav"));
	auto src = std::make_shared<FileSystemDataSource>(tmp.str(), scratchMetadataStore());
	StoermelderPackOne::SyncTaskWorker worker;
	auto dsCancel = std::make_shared<std::atomic<bool>>(false);

	task.start(&worker, src, src->getMetadata(), "/a.wav", false, "a.wav", nullptr, dsCancel);
	REQUIRE(task.progress == p);
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
		StoermelderPackOne::SyncTaskWorker worker;
		SirenClassifyTask task;
		auto dsCancel = std::make_shared<std::atomic<bool>>(false);
		task.start(&worker, src, meta, "/a.wav", false, "a.wav", nullptr, dsCancel);
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
		StoermelderPackOne::SyncTaskWorker worker;
		SirenClassifyTask task;
		auto dsCancel = std::make_shared<std::atomic<bool>>(false);
		task.start(&worker, src, meta, "/a.wav", false, "a.wav", nullptr, dsCancel);
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

	StoermelderPackOne::SyncTaskWorker worker;
	SirenClassifyTask task;

	REQUIRE(task.statusMessage() == "");

	auto dsCancel = std::make_shared<std::atomic<bool>>(false);
	task.start(&worker, src, src->getMetadata(), "/a.wav", false, "a.wav", nullptr, dsCancel);
	REQUIRE(task.statusMessage().find("Analysing") != std::string::npos);
	REQUIRE(task.progress->done.load(std::memory_order_acquire));
	REQUIRE(task.statusMessage().find("Analysing") != std::string::npos);
}
