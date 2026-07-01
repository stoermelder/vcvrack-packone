#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Siren.cpp"
#include "SirenTest.hpp"

using namespace StoermelderPackOne::Siren;

SYNC_MODEL(modelSiren, "Siren");
Test::TestContext<> testContext;

// ─── Construction ─────────────────────────────────────────────────────────────

// Module instantiation leaves outputs at 0 V.
TEST_CASE("Construction and initialization", "[Siren]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	REQUIRE(m != nullptr);
	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == 0.f);
	REQUIRE(m->outputs[SirenModule::OUTPUT_R].getVoltage() == 0.f);
	Test::destroyModule(m);
}

// ─── JSON serialization ───────────────────────────────────────────────────────

TEST_CASE("Preset JSON null-guards", "[Siren][JSON]") {
	auto module = Test::createModule<SirenModule>("Siren");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

// JSON round-trip preserves lastFile, lastPlayheadPos, activeRootIdx and trim.
TEST_CASE("JSON serialization", "[Siren][JSON]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	// Set state and serialise
	m->lastFilePath = "/some/path/sample.wav";
	m->lastPlayheadPos = 0.42f;
	m->activeRootIdx = 2;
	m->trimIn.store(0.15f, std::memory_order_relaxed);
	m->trimOut.store(0.85f, std::memory_order_relaxed);

	json_t* j = m->dataToJson();
	REQUIRE(j != nullptr);

	// Verify JSON content directly (avoids dylib/test-TU global symbol conflict)
	json_t* lfJ = json_object_get(j, "lastFile");
	REQUIRE(lfJ != nullptr);
	REQUIRE(std::string(json_string_value(lfJ)) == "/some/path/sample.wav");

	json_t* ppJ = json_object_get(j, "lastPlayheadPos");
	REQUIRE(ppJ != nullptr);
	REQUIRE(json_real_value(ppJ) == Catch::Approx(0.42).margin(0.001));

	json_t* arJ = json_object_get(j, "activeRootIdx");
	REQUIRE(arJ != nullptr);
	REQUIRE(json_integer_value(arJ) == 2);

	json_t* tiJ = json_object_get(j, "trimIn");
	REQUIRE(tiJ != nullptr);
	REQUIRE(json_real_value(tiJ) == Catch::Approx(0.15).margin(0.001));

	json_t* toJ = json_object_get(j, "trimOut");
	REQUIRE(toJ != nullptr);
	REQUIRE(json_real_value(toJ) == Catch::Approx(0.85).margin(0.001));

	json_decref(j);
	Test::destroyModule(m);
}

// ─── SirenSettings: fromJson sort ──────────────────────────────────────────────

// fromJson sorts rootContainers alphabetically (the canonical on-disk order).
TEST_CASE("SirenSettings::fromJson: sorts rootContainers alphabetically", "[Siren][Settings]") {
	// Build JSON with three roots in reverse-alphabetical insertion order.
	json_t* j = json_object();
	json_t* rootsJ = json_array();
	auto makeRoot = [](const char* path) {
		json_t* r = json_object();
		json_object_set_new(r, "path", json_string(path));
		json_object_set_new(r, "type", json_string("fs"));
		return r;
	};
	json_array_append_new(rootsJ, makeRoot("/Zebra"));
	json_array_append_new(rootsJ, makeRoot("/Mango"));
	json_array_append_new(rootsJ, makeRoot("/Apple"));
	json_object_set_new(j, "rootContainers", rootsJ);

	SirenSettings settings;
	settings.fromJson(j);
	json_decref(j);

	REQUIRE(settings.rootContainers.size() == 3);
	REQUIRE(settings.rootContainers[0].name == "Apple");
	REQUIRE(settings.rootContainers[1].name == "Mango");
	REQUIRE(settings.rootContainers[2].name == "Zebra");
}

// Legacy settings files (pre per-instance refactor) included activeRootIdx,
// lastFile and lastPlayheadPos. They are now stored per SirenModule on the
// patch, so the global SirenSettings ignores them silently — but must not
// choke on them when loading an old file.
TEST_CASE("SirenSettings::fromJson: legacy per-instance fields are silently ignored", "[Siren][Settings]") {
	json_t* j = json_object();
	json_t* rootsJ = json_array();
	auto makeRoot = [](const char* path) {
		json_t* r = json_object();
		json_object_set_new(r, "path", json_string(path));
		json_object_set_new(r, "type", json_string("fs"));
		return r;
	};
	json_array_append_new(rootsJ, makeRoot("/Samples"));
	json_object_set_new(j, "rootContainers", rootsJ);
	json_object_set_new(j, "activeRootIdx", json_integer(0));   // legacy
	json_object_set_new(j, "lastFile", json_string("/x.wav"));  // legacy
	json_object_set_new(j, "lastPlayheadPos", json_real(0.5));  // legacy

	SirenSettings settings;
	settings.fromJson(j);
	json_decref(j);

	// Global settings still load fine.
	REQUIRE(settings.rootContainers.size() == 1);
	REQUIRE(settings.rootContainers[0].name == "Samples");
}

// ─── MetadataStore: favorites ──────────────────────────────────────────────────
// favorite flag can be set, cleared, and persists independently of tags.
TEST_CASE("MetadataStore: favorites", "[Siren][Metadata]") {
	MetadataStore meta;
	meta.rootPath = "/test/root";

	SECTION("Set and get favorite") {
		meta.setFavorite("drums/kick.wav", true);
		REQUIRE(meta.isFavorite("drums/kick.wav") == true);
		REQUIRE(meta.isFavorite("drums/snare.wav") == false);
	}

	SECTION("Clear favorite keeps entry as a seen-marker even with no tags") {
		// The samples map only ever grows (see MetadataStore::markSeen) so that
		// cleanup() can later locate and remove matching waveform cache files.
		meta.setFavorite("drums/kick.wav", true);
		meta.setFavorite("drums/kick.wav", false);
		REQUIRE(meta.isFavorite("drums/kick.wav") == false);
		REQUIRE(meta.samples.find("drums/kick.wav") != meta.samples.end());
	}

	SECTION("Clearing favorite keeps entry when tags remain") {
		meta.addTag("drums/kick.wav", "percussion");
		meta.setFavorite("drums/kick.wav", true);
		meta.setFavorite("drums/kick.wav", false);
		REQUIRE(meta.isFavorite("drums/kick.wav") == false);
		REQUIRE(meta.samples.find("drums/kick.wav") != meta.samples.end());
	}
}

// ─── MetadataStore: tags ───────────────────────────────────────────────────────
// tags can be added, retrieved, removed, and allTags returns the union.
TEST_CASE("MetadataStore: tags", "[Siren][Metadata]") {
	MetadataStore meta;
	meta.rootPath = "/test/root";

	SECTION("Add and retrieve tag") {
		meta.addTag("field/rain.wav", "ambient");
		auto tags = meta.getTags("field/rain.wav");
		REQUIRE(tags.size() == 1);
		REQUIRE(tags[0] == "ambient");
	}

	SECTION("No duplicate tags — exact match") {
		meta.addTag("field/rain.wav", "ambient");
		meta.addTag("field/rain.wav", "ambient");
		REQUIRE(meta.getTags("field/rain.wav").size() == 1);
	}

	SECTION("No duplicate tags — case-insensitive match") {
		meta.addTag("field/rain.wav", "Ambient");
		meta.addTag("field/rain.wav", "ambient");
		meta.addTag("field/rain.wav", "AMBIENT");
		REQUIRE(meta.getTags("field/rain.wav").size() == 1);
	}

	SECTION("Exact spelling of first occurrence is preserved") {
		meta.addTag("field/rain.wav", "Dark Ambient");
		meta.addTag("field/rain.wav", "dark ambient");  // rejected — same tag, different case
		auto tags = meta.getTags("field/rain.wav");
		REQUIRE(tags.size() == 1);
		REQUIRE(tags[0] == "Dark Ambient");
	}

	SECTION("Tags with different text are all stored") {
		meta.addTag("field/rain.wav", "Bass");
		meta.addTag("field/rain.wav", "Drums");
		REQUIRE(meta.getTags("field/rain.wav").size() == 2);
	}

	SECTION("Remove tag") {
		meta.addTag("field/rain.wav", "ambient");
		meta.addTag("field/rain.wav", "loop");
		meta.removeTag("field/rain.wav", "ambient");
		auto tags = meta.getTags("field/rain.wav");
		REQUIRE(tags.size() == 1);
		REQUIRE(tags[0] == "loop");
	}

	SECTION("allTags returns union across samples") {
		meta.addTag("a.wav", "Drone");
		meta.addTag("b.wav", "Loop");
		meta.addTag("b.wav", "Drone");
		auto all = meta.allTags();
		REQUIRE(all.count("Drone") == 1);
		REQUIRE(all.count("Loop") == 1);
	}

	SECTION("allTags returns starter tags when empty") {
		auto all = meta.allTags();
		REQUIRE(!all.empty());
		REQUIRE(all.count("Drone") == 1);
	}
}

// ─── addTag: spelling and case rules ─────────────────────────────────────────
// custom tags preserve their exact spelling when first added.
TEST_CASE("addTag: custom tag spelling is stored verbatim", "[Siren][Metadata]") {
	MetadataStore meta;

	meta.addTag("kick.wav", "Dark Ambient");
	REQUIRE(meta.getTags("kick.wav")[0] == "Dark Ambient");

	meta.addTag("snare.wav", "FX");
	REQUIRE(meta.getTags("snare.wav")[0] == "FX");

	meta.addTag("hat.wav", "one-shot");
	REQUIRE(meta.getTags("hat.wav")[0] == "one-shot");
}

// case-insensitive duplicate prevention keeps only the first spelling.
TEST_CASE("addTag: case-insensitive duplicate prevention", "[Siren][Metadata]") {
	MetadataStore meta;

	// Three case-variants of the same word → only the first is stored
	meta.addTag("a.wav", "Percussion");
	meta.addTag("a.wav", "percussion");
	meta.addTag("a.wav", "PERCUSSION");
	auto tags = meta.getTags("a.wav");
	REQUIRE(tags.size() == 1);
	REQUIRE(tags[0] == "Percussion");   // first spelling wins

	// Adding a genuinely different tag still works
	meta.addTag("a.wav", "Loop");
	REQUIRE(meta.getTags("a.wav").size() == 2);
}

// case-insensitive check preserves the exact stored spelling in allTags.
TEST_CASE("addTag: case-insensitive check does not affect allTags display", "[Siren][Metadata]") {
	MetadataStore meta;
	// Store a capitalised custom tag
	meta.addTag("a.wav", "Heavy Bass");
	auto all = meta.allTags();
	// The exact stored spelling appears in allTags
	REQUIRE(all.count("Heavy Bass") == 1);
	// The lowercase version does not appear separately
	REQUIRE(all.count("heavy bass") == 0);
}

// ─── MetadataStore: JSON I/O ───────────────────────────────────────────────────
// metadata serialises to JSON and deserialises back correctly.
TEST_CASE("MetadataStore: JSON round-trip", "[Siren][Metadata]") {
	MetadataStore meta;
	meta.rootPath = "/test/root";
	meta.setFavorite("a.wav", true);
	meta.addTag("a.wav", "drone");
	meta.addTag("b.wav", "loop");
	meta.setAudioInfo("a.wav", 12.5f, 44100, 16, 2);

	json_t* j = meta.toJson();
	REQUIRE(j != nullptr);

	MetadataStore meta2;
	meta2.fromJson(j);
	json_decref(j);

	REQUIRE(meta2.rootPath == "/test/root");
	REQUIRE(meta2.isFavorite("a.wav") == true);
	REQUIRE(meta2.isFavorite("b.wav") == false);
	auto tags = meta2.getTags("a.wav");
	REQUIRE(std::find(tags.begin(), tags.end(), "drone") != tags.end());
	auto tags2 = meta2.getTags("b.wav");
	REQUIRE(std::find(tags2.begin(), tags2.end(), "loop") != tags2.end());

	const SampleMetadata& a = meta2.samples.at("a.wav");
	REQUIRE(a.durationSeconds == Catch::Approx(12.5f));
	REQUIRE(a.sampleRate == 44100);
	REQUIRE(a.bitDepth == 16);
	REQUIRE(a.channels == 2);
}

// ─── MetadataStore: 3-step save process (rename → write → verify → delete) ────
// save() persists via a real file and is readable back through load(); ScratchMetadataStore
// redirects both to a scratch folder so the rename/verify/cleanup sequence runs for real.

// save() then load() round-trips favorites, tags and BPM through real file I/O.
TEST_CASE("MetadataStore::save: round-trips through real file I/O", "[Siren][Metadata][Persistence]") {
	ScratchMetadataStore store;
	store.rootPath = "/test/save-roundtrip";
	DEFER({ rack::system::remove(store.filePath()); });

	store.setFavorite("a.wav", true);
	store.addTag("a.wav", "drone");
	store.setBpm("b.wav", 120.f, 0.9f);
	store.setAudioInfo("a.wav", 12.5f, 44100, 16, 2);
	store.save();

	REQUIRE(rack::system::exists(store.filePath()));

	ScratchMetadataStore loaded;
	loaded.rootPath = store.rootPath;
	loaded.load();

	REQUIRE(loaded.isFavorite("a.wav") == true);
	REQUIRE(loaded.getTags("a.wav") == std::vector<std::string>{"drone"});
	REQUIRE(loaded.getBpm("b.wav") == Catch::Approx(120.f));
	const SampleMetadata& a = loaded.samples.at("a.wav");
	REQUIRE(a.durationSeconds == Catch::Approx(12.5f));
	REQUIRE(a.sampleRate == 44100);
	REQUIRE(a.bitDepth == 16);
	REQUIRE(a.channels == 2);
}

// A successful save leaves no ".bak" file behind — the backup is removed once
// the freshly written file has been verified to parse as valid JSON.
TEST_CASE("MetadataStore::save: leaves no stray .bak file on success", "[Siren][Metadata][Persistence]") {
	ScratchMetadataStore store;
	store.rootPath = "/test/save-no-bak";
	DEFER({
		rack::system::remove(store.filePath());
		rack::system::remove(store.filePath() + ".bak");
	});

	store.addTag("a.wav", "loop");
	store.save();
	REQUIRE(rack::system::exists(store.filePath()));
	REQUIRE_FALSE(rack::system::exists(store.filePath() + ".bak"));

	// Saving again over an existing file also cleans up its own backup.
	store.addTag("a.wav", "drone");
	store.save();
	REQUIRE_FALSE(rack::system::exists(store.filePath() + ".bak"));
}

// save() overwrites a previously-existing file, even one with stale or corrupted
// content — the new file is what verification checks, not the old one.
TEST_CASE("MetadataStore::save: overwrites a corrupted prior file", "[Siren][Metadata][Persistence]") {
	ScratchMetadataStore store;
	store.rootPath = "/test/save-overwrites-corrupt";
	DEFER({
		rack::system::remove(store.filePath());
		rack::system::remove(store.filePath() + ".bak");
	});

	rack::system::createDirectories(rack::system::getDirectory(store.filePath()));
	FILE* f = fopen(store.filePath().c_str(), "w");
	REQUIRE(f != nullptr);
	fputs("{ this is not valid json", f);
	fclose(f);

	store.addTag("a.wav", "percussion");
	store.save();

	REQUIRE_FALSE(rack::system::exists(store.filePath() + ".bak"));

	ScratchMetadataStore loaded;
	loaded.rootPath = store.rootPath;
	loaded.load();
	REQUIRE(loaded.getTags("a.wav") == std::vector<std::string>{"percussion"});
}

// load() against a corrupt/unparsable file is a no-op: it neither crashes nor
// mutates the store's existing in-memory state.
TEST_CASE("MetadataStore::load: ignores a corrupt file without crashing", "[Siren][Metadata][Persistence]") {
	ScratchMetadataStore store;
	store.rootPath = "/test/load-ignores-corrupt";
	DEFER({ rack::system::remove(store.filePath()); });

	rack::system::createDirectories(rack::system::getDirectory(store.filePath()));
	FILE* f = fopen(store.filePath().c_str(), "w");
	REQUIRE(f != nullptr);
	fputs("not json at all", f);
	fclose(f);

	store.addTag("a.wav", "kept");
	store.load();

	// fromJson() is never reached, so the pre-existing in-memory state survives.
	REQUIRE(store.getTags("a.wav") == std::vector<std::string>{"kept"});
}

// ─── AudioWaveformCache ────────────────────────────────────────────────────────
// empty() state and buildWaveformCache output (loadWaveformCacheFile /
// saveWaveformCacheFile are gated by isTesting() and cannot run in the test
// harness, so we exercise buildWaveformCache directly instead).

struct FakeAudioStream : AudioStream {
	std::vector<float> data;
	int ch_, sr_;
	int64_t pos_ = 0;
	FakeAudioStream(int frames, int ch, int sr, float fill = 0.5f)
			: ch_(ch), sr_(sr) {
		data.assign((size_t)frames * ch, fill);
	}
	int channels() const override { return ch_; }
	int sampleRate() const override { return sr_; }
	int64_t totalFrames() const override { return (int64_t)data.size() / std::max(1, ch_); }
	int64_t readF32(float* buf, int64_t n) override {
		int64_t avail = totalFrames() - pos_;
		int64_t toRead = std::min(n, avail);
		if (buf && toRead > 0) {
			std::memcpy(buf, data.data() + pos_ * ch_, (size_t)toRead * (size_t)ch_ * sizeof(float));
		}
		pos_ += toRead;
		return toRead;
	}
	bool seekTo(int64_t f) override { pos_ = f; return true; }
};

TEST_CASE("AudioWaveformCache: empty() reports cache state correctly", "[Siren][Audio]") {
	SECTION("Default-constructed cache is empty") {
		AudioWaveformCache cache;
		REQUIRE(cache.empty() == true);
	}

	SECTION("Cache with sampleCount but no channel data is empty") {
		AudioWaveformCache cache;
		cache.sampleCount = 100;
		REQUIRE(cache.empty() == true);
	}

	SECTION("Populated cache is not empty") {
		AudioWaveformCache cache;
		cache.sampleCount = 100;
		cache.samples.push_back(std::vector<float>(100, 0.f));
		REQUIRE(cache.empty() == false);
	}
}

TEST_CASE("buildWaveformCache: fills cache from a stream", "[Siren][Audio]") {
	FakeAudioStream stream(4410, 1, 44100, 0.5f);

	AudioWaveformCache cache;
	bool ok = buildWaveformCache(/*timestamp=*/99999, stream, /*pixelWidth=*/100, cache);

	REQUIRE(ok == true);
	REQUIRE(cache.fileTimestamp == 99999);
	REQUIRE(cache.sampleCount == std::min(100 * 8, 8192));  // internal resolution = pixelWidth * 8
	REQUIRE(cache.samples.size() == 1);  // mono
	REQUIRE(!cache.empty());
}

TEST_CASE("buildWaveformCache: stores timestamp for stale-detection", "[Siren][Audio]") {
	// buildWaveformCache stamps the cache with the caller-supplied timestamp.
	// At load time, loadWaveformCacheFile rejects any cache whose stored timestamp
	// does not match the file's current mtime, making this the key staleness signal.
	FakeAudioStream s1(4410, 1, 44100);
	FakeAudioStream s2(4410, 1, 44100);
	AudioWaveformCache c1, c2;
	buildWaveformCache(11111, s1, 50, c1);
	buildWaveformCache(22222, s2, 50, c2);
	REQUIRE(c1.fileTimestamp == 11111);
	REQUIRE(c2.fileTimestamp == 22222);
	REQUIRE(c1.fileTimestamp != c2.fileTimestamp);
}

TEST_CASE("buildWaveformCache: stereo stream produces two channel vectors", "[Siren][Audio]") {
	FakeAudioStream stream(4410, 2, 44100);
	AudioWaveformCache cache;
	REQUIRE(buildWaveformCache(0, stream, 80, cache) == true);
	REQUIRE(cache.samples.size() == 2);
}

TEST_CASE("buildWaveformCache: rejects degenerate inputs", "[Siren][Audio]") {
	SECTION("zero pixel width") {
		FakeAudioStream s(1000, 1, 44100);
		AudioWaveformCache c;
		REQUIRE(buildWaveformCache(0, s, 0, c) == false);
	}
	SECTION("zero-frame stream") {
		FakeAudioStream s(0, 1, 44100);
		AudioWaveformCache c;
		REQUIRE(buildWaveformCache(0, s, 100, c) == false);
	}
}

// ─── hashPath ─────────────────────────────────────────────────────────────────
// hashPath returns a deterministic 8-character hexadecimal string.
TEST_CASE("hashPath produces 8-char hex string", "[Siren][Utility]") {
	std::string h = hashPath("/Users/ben/Samples");
	REQUIRE(h.size() == 8);
	for (char c : h) {
		REQUIRE(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
	}
}

// identical paths produce identical hashes; different paths differ.
TEST_CASE("hashPath is deterministic", "[Siren][Utility]") {
	REQUIRE(hashPath("/foo/bar") == hashPath("/foo/bar"));
	REQUIRE(hashPath("/foo/bar") != hashPath("/foo/baz"));
}

// ─── Audio output: silence without file ──────────────────────────────────────
// process() outputs 0 V when no audio file is loaded.
TEST_CASE("Audio output: silence without loaded file", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == 0.f);
	REQUIRE(m->outputs[SirenModule::OUTPUT_R].getVoltage() == 0.f);

	Test::destroyModule(m);
}

// ─── Playhead clamping ────────────────────────────────────────────────────────
// playhead position is clamped to valid range [0, 1].
TEST_CASE("Playhead clamps to [0, 1]", "[Siren][Preview]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);
	pane.init(nullptr, nullptr);
	pane.canvas->box.size = Vec(600.f, 380.f - SirenPreviewPane::TB_H);

	SECTION("Below 0 clamps to 0") {
		float pos = rack::math::clamp(-0.5f, 0.f, 1.f);
		REQUIRE(pos == 0.f);
	}

	SECTION("Above 1 clamps to 1") {
		float pos = rack::math::clamp(1.5f, 0.f, 1.f);
		REQUIRE(pos == 1.f);
	}

	SECTION("posToPlayhead from inside rect returns value in [0, 1]") {
		// Simulate a click inside the waveform area
		Rect wr = pane.canvas->waveformRect();
		Vec midpoint = wr.pos.plus(wr.size.mult(0.5f));
		float result = pane.canvas->posToNormalized(midpoint);
		REQUIRE(result >= 0.f);
		REQUIRE(result <= 1.f);
		REQUIRE(result == Catch::Approx(0.5f).margin(0.05f));
	}
}

// ─── SirenDropHandler ─────────────────────────────────────────────────────────
// drop handler starts inactive with no drag path.
TEST_CASE("SirenDropHandler initial state", "[Siren][DragDrop]") {
	SirenDropHandler dh;
	REQUIRE(dh.active == false);
	REQUIRE(dh.dragPath.empty());
	REQUIRE(dh.converting.load() == false);
}

// ─── allTags: starter tags always present ─────────────────────────────────────
// starter tags are always included in allTags, even when custom tags exist.
TEST_CASE("allTags: starter tags present even when samples have tags", "[Siren][Metadata]") {
	MetadataStore meta;
	meta.rootPath = "/test/root";

	// Assign a custom tag — starter tags must still appear (regression for the
	// bug where the "if (result.empty())" guard dropped them once any tag existed)
	meta.addTag("kick.wav", "percussion");
	auto all = meta.allTags();

	REQUIRE(all.count("percussion") == 1);
	for (const std::string& starter : starterTags()) {
		REQUIRE(all.count(starter) == 1);
	}
}

// user tags merge with starter tags; duplicates are prevented.
TEST_CASE("allTags: user tags merge with starter tags without duplicates", "[Siren][Metadata]") {
	MetadataStore meta;
	meta.rootPath = "/test/root";

	// "Drone" is already a starter tag; adding it again must not duplicate it
	meta.addTag("a.wav", "Drone");
	auto all = meta.allTags();
	REQUIRE(all.count("Drone") == 1);
}

// starter tags are loaded from SirenTags.json at runtime; in tests we use
// the hard-coded fallback (mirrors the JSON contents).
TEST_CASE("starterTags: returns 15-tag canonical list in tests", "[Siren][Metadata][Manifest]") {
	auto tags = starterTags();
	REQUIRE(tags.size() == 15);
	// Spot-check a few representative tags from the new vocabulary
	REQUIRE(std::find(tags.begin(), tags.end(), "Drone") != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Pad") != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Lead") != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Stab") != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Noise") != tags.end());
	// Removed from the old list
	REQUIRE(std::find(tags.begin(), tags.end(), "Fx") == tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Ambient") == tags.end());
}

// ─── Volume parameter ─────────────────────────────────────────────────────────
// volume parameter defaults to 1.0 and accepts values in range [0, 2].
TEST_CASE("PARAM_VOLUME: default value and range", "[Siren][Module]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	REQUIRE(m != nullptr);

	// Default is unity gain (1.0)
	REQUIRE(m->params[SirenModule::PARAM_VOLUME].getValue() == Catch::Approx(1.f));

	// Range extremes are accepted
	m->params[SirenModule::PARAM_VOLUME].setValue(0.f);
	REQUIRE(m->params[SirenModule::PARAM_VOLUME].getValue() == Catch::Approx(0.f));
	m->params[SirenModule::PARAM_VOLUME].setValue(2.f);
	REQUIRE(m->params[SirenModule::PARAM_VOLUME].getValue() == Catch::Approx(2.f));

	Test::destroyModule(m);
}

// zero volume produces silence even when audio is available.
TEST_CASE("PARAM_VOLUME: zero volume produces silence", "[Siren][Module]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	m->params[SirenModule::PARAM_VOLUME].setValue(0.f);
	// previewPane is null so process() exits early but must not crash
	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == 0.f);
	REQUIRE(m->outputs[SirenModule::OUTPUT_R].getVoltage() == 0.f);
	Test::destroyModule(m);
}

// ─── SirenPreviewPane: inPoint / scrubPos / stream state ─────────────────────

// ─── Playhead visual source during drag ───────────────────────────────────────
// during drag, scrubPos overrides modulePlayheadPos for visual display.
TEST_CASE("posToPlayhead: scrubPos is display source while draggingPlayhead", "[Siren][Preview]") {
	// The draw() function reads scrubPos directly when draggingPlayhead == true,
	// ignoring modulePlayheadPos. This prevents the DSP thread from overwriting
	// the visual position while the fill thread is still processing the seek.
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);
	pane.init(nullptr, nullptr);

	// Simulate a playing module whose playhead is at 0.1
	std::atomic<float> fakePlayhead{0.1f};
	pane.modulePlayheadPos = &fakePlayhead;

	// User starts dragging to 0.7
	pane.canvas->scrubPos = 0.7f;
	pane.canvas->inPoint = 0.7f;
	pane.canvas->draggingPlayhead = true;

	// The visual playhead should track scrubPos, not modulePlayheadPos
	float displayedPh = pane.canvas->draggingPlayhead
		? pane.canvas->scrubPos
		: (pane.modulePlayheadPos ? pane.modulePlayheadPos->load() : 0.f);
	REQUIRE(displayedPh == Catch::Approx(0.7f));

	// After drag ends, display switches back to modulePlayheadPos
	pane.canvas->draggingPlayhead = false;
	displayedPh = pane.canvas->draggingPlayhead
		? pane.canvas->scrubPos
		: (pane.modulePlayheadPos ? pane.modulePlayheadPos->load() : 0.f);
	REQUIRE(displayedPh == Catch::Approx(0.1f));
}

// loadItem resets inPoint and scrubPos to 0 on each call.
// loadItem resets scrubPos/zoom/scroll to 0 (view state is per-file).
// The trim range is PER-INSTANCE — owned by the module's trimIn/trimOut
// atomics — and is deliberately NOT touched by loadItem, so loading a new
// file preserves the user's chosen trim points.
TEST_CASE("loadItem resets view state but preserves trim range", "[Siren][Preview]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);
	pane.init(nullptr, nullptr);

	pane.canvas->inPoint = 0.7f;
	pane.canvas->outPoint = 0.9f;
	pane.canvas->scrubPos = 0.7f;

	pane.loadItem(DataSourceNode{}, nullptr, false, false, /*resetTrim=*/false);

	// View state resets
	REQUIRE(pane.canvas->scrubPos == 0.f);
	REQUIRE(pane.canvas->zoomLevel == 1.0f);
	REQUIRE(pane.canvas->scrollPos == 0.0f);
	// Trim range is preserved (no module wired up, so the canvas's local
	// fields are what getInPoint/getOutPoint return).
	REQUIRE(pane.canvas->getInPoint() == 0.7f);
	REQUIRE(pane.canvas->getOutPoint() == 0.9f);
}

// loadItem with empty id or null source leaves currentNode.relativePath empty.
TEST_CASE("loadItem: no item loaded for empty id or null source", "[Siren][Preview]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);

	pane.loadItem(DataSourceNode{}, nullptr);
	REQUIRE(pane.currentNode.relativePath.empty());
}

// loadItem with startPlay=true does not start playback when no stream can be opened.
TEST_CASE("loadItem: playing stays false when no stream can be opened", "[Siren][Preview]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);

	// Wire a local playing flag so the pane's modulePlaying read has a target.
	std::atomic<bool> playing{false};
	pane.modulePlaying = &playing;

	// startPlay=true with empty id → early return before startPlaybackFrom()
	pane.loadItem(DataSourceNode{}, nullptr, /*startPlay=*/true);
	REQUIRE(playing.load() == false);
}

// ─── DataSource: metadata ownership ──────────────────────────────────────────
// These exercise the DataSource/MetadataStore contract itself — any DataSource
// must return a stable, mutable MetadataStore* — so a minimal in-memory
// TestDataSource is used rather than the filesystem-backed FileSystemDataSource.

// getMetadata returns a valid pointer with correct rootPath.
TEST_CASE("DataSource: getMetadata returns valid pointer", "[Siren][Metadata]") {
	TestDataSource src("/test/root");

	MetadataStore* meta = src.getMetadata();
	REQUIRE(meta != nullptr);
	REQUIRE(meta->rootPath == "/test/root");
}

// metadata pointer remains stable and allows mutation.
TEST_CASE("DataSource: metadata is mutable through pointer", "[Siren][Metadata]") {
	TestDataSource src("/test/root");

	MetadataStore* meta = src.getMetadata();
	meta->addTag("kick.wav", "percussion");
	REQUIRE(meta->getTags("kick.wav").size() == 1);
	// Pointer remains stable: same address on second call
	REQUIRE(src.getMetadata() == meta);
}


// ─── Search query: parsing and matching ──────────────────────────────────────

TEST_CASE("parseSearchQuery: splits plain text from numeric filters", "[Siren][Search]") {
	SearchQuery q = parseSearchQuery("Kick bpm:140 length:<1s");
	REQUIRE(q.text == "kick");
	REQUIRE(q.filters.size() == 2);
	REQUIRE(q.filters[0].field == SearchFilter::Field::Bpm);
	REQUIRE(q.filters[0].op == SearchFilter::Op::Eq);
	REQUIRE(q.filters[0].value == Catch::Approx(140.f));
	REQUIRE(q.filters[1].field == SearchFilter::Field::Length);
	REQUIRE(q.filters[1].op == SearchFilter::Op::Lt);
	REQUIRE(q.filters[1].value == Catch::Approx(1.f));
}

TEST_CASE("parseSearchQuery: length filter accepts minute unit and >= operator", "[Siren][Search]") {
	SearchQuery q = parseSearchQuery("length:>=2.5m");
	REQUIRE(q.text.empty());
	REQUIRE(q.filters.size() == 1);
	REQUIRE(q.filters[0].field == SearchFilter::Field::Length);
	REQUIRE(q.filters[0].op == SearchFilter::Op::Ge);
	REQUIRE(q.filters[0].value == Catch::Approx(150.f)); // 2.5 minutes -> seconds
}

TEST_CASE("parseSearchQuery: unrecognised key:value falls back to plain text", "[Siren][Search]") {
	SearchQuery q = parseSearchQuery("foo:bar bpm:140");
	REQUIRE(q.text == "foo:bar");
	REQUIRE(q.filters.size() == 1);
}

TEST_CASE("matchesSearch: bpm filter matches within tolerance", "[Siren][Search]") {
	TestDataSource src("/test/root");
	MetadataStore* meta = src.getMetadata();
	meta->setAudioInfo("kick.wav", 1.f, 44100, 16, 2);
	meta->setBpm("kick.wav", 140.2f);
	meta->setAudioInfo("snare.wav", 1.f, 44100, 16, 2);
	meta->setBpm("snare.wav", 90.f);

	SearchQuery q = parseSearchQuery("bpm:140");
	REQUIRE(src.matchesSearch("kick.wav", false, q) == true);
	REQUIRE(src.matchesSearch("snare.wav", false, q) == false);
}

TEST_CASE("matchesSearch: length filter with operator and unit", "[Siren][Search]") {
	TestDataSource src("/test/root");
	MetadataStore* meta = src.getMetadata();
	meta->setAudioInfo("short.wav", 0.5f, 44100, 16, 2);
	meta->setAudioInfo("long.wav", 5.f, 44100, 16, 2);

	SearchQuery q = parseSearchQuery("length:<1s");
	REQUIRE(src.matchesSearch("short.wav", false, q) == true);
	REQUIRE(src.matchesSearch("long.wav", false, q) == false);
}

TEST_CASE("matchesSearch: file with no metadata never matches a numeric filter", "[Siren][Search]") {
	TestDataSource src("/test/root");

	SearchQuery q = parseSearchQuery("bpm:140");
	REQUIRE(src.matchesSearch("unknown.wav", false, q) == false);
}

TEST_CASE("matchesSearch: combined text and numeric filter", "[Siren][Search]") {
	TestDataSource src("/test/root");
	MetadataStore* meta = src.getMetadata();
	meta->setAudioInfo("kick.wav", 1.f, 44100, 16, 2);
	meta->setBpm("kick.wav", 140.f);
	meta->setAudioInfo("kick2.wav", 1.f, 44100, 16, 2);
	meta->setBpm("kick2.wav", 90.f);

	SearchQuery q = parseSearchQuery("kick bpm:140");
	REQUIRE(src.matchesSearch("kick.wav", false, q) == true);
	REQUIRE(src.matchesSearch("kick2.wav", false, q) == false);
}

TEST_CASE("matchesSearch: container matches when a descendant satisfies the filter", "[Siren][Search]") {
	TestDataSource src("/test/root");
	MetadataStore* meta = src.getMetadata();
	meta->setAudioInfo("drums/kick.wav", 1.f, 44100, 16, 2);
	meta->setBpm("drums/kick.wav", 140.f);
	meta->setAudioInfo("vocals/take.wav", 1.f, 44100, 16, 2);
	meta->setBpm("vocals/take.wav", 90.f);

	SearchQuery q = parseSearchQuery("bpm:140");
	REQUIRE(src.matchesSearch("drums", true, q) == true);
	REQUIRE(src.matchesSearch("vocals", true, q) == false);
}


// ─── Audio streaming: ring buffer / DSP ──────────────────────────────────────
// process() reads from ring buffers and applies volume scaling.
// Helper: push a stereo frame directly into the module's ring buffers.
static void pushFrame(SirenModule* m, float l, float r) {
	dsp::Frame<2> fr;
	fr.samples[0] = l;
	fr.samples[1] = r;
	m->rb.push(fr);
}

TEST_CASE("process: reads samples from ring buffer and scales by volume", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	// Default volume = 1.0; DSP multiplies by vol * 5.f
	pushFrame(m, 0.5f, -0.5f);
	m->streamTotalFrames = 100;
	m->playing.store(true, std::memory_order_release);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == Catch::Approx(2.5f));
	REQUIRE(m->outputs[SirenModule::OUTPUT_R].getVoltage() == Catch::Approx(-2.5f));

	Test::destroyModule(m);
}

// process() stops playing when ring is empty and EOF is reached.
TEST_CASE("process: stops playing when ring drained after EOF", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	// Fill thread signals EOF; ring is empty → process() must stop
	m->streamTotalFrames = 100;
	m->playing.store(true, std::memory_order_release);
	m->eofReached.store(true, std::memory_order_release);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->playing.load() == false);
	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == 0.f);

	Test::destroyModule(m);
}

// ring samples are consumed before EOF triggers stop.
TEST_CASE("process: ring samples consumed before EOF stop", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	// One frame in ring + eofReached=true: first process() drains the frame;
	// second process() sees empty ring + eofReached and stops.
	pushFrame(m, 1.f, 1.f);
	m->streamTotalFrames = 2;
	m->playing.store(true, std::memory_order_release);
	m->eofReached.store(true, std::memory_order_release);

	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->playing.load() == true);   // still playing — ring had data

	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->playing.load() == false);  // drained now

	Test::destroyModule(m);
}

TEST_CASE("process: volume knob at zero produces silence even with ring data", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	m->params[SirenModule::PARAM_VOLUME].setValue(0.f);

	pushFrame(m, 1.f, 1.f);
	m->streamTotalFrames = 100;
	m->playing.store(true, std::memory_order_release);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == 0.f);
	REQUIRE(m->outputs[SirenModule::OUTPUT_R].getVoltage() == 0.f);

	Test::destroyModule(m);
}

// startPlayback computes seekBaseFrame from position * total frames.
TEST_CASE("startPlayback: seekBaseFrame computed from position and total frames", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	m->streamTotalFrames = 1000;

	m->startPlayback(0.5f);

	REQUIRE(m->seekBaseFrame == 500);

	Test::destroyModule(m);
}

// startPlayback with position 0 seeks to frame 0.
TEST_CASE("startPlayback: position 0 seeks to frame 0", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	m->streamTotalFrames = 1000;

	m->startPlayback(0.f);

	REQUIRE(m->seekBaseFrame == 0);

	Test::destroyModule(m);
}

// rapid successive startPlayback calls — last position wins.
TEST_CASE("startPlayback: rapid successive calls — last position wins", "[Siren][Audio]") {
	// Simulates the full press+scrub sequence: onButton fires startPlayback at the
	// click position, then onDragMove fires it again for each moved position.
	// pendingSeekFrame is a single atomic; rapid overwrites are safe — the fill
	// thread always picks up the latest position.
	auto* m = Test::createModule<SirenModule>("Siren");
	// Stop the fill thread so it can't consume pendingSeekFrame via
	// exchange(-1) before the assertion runs. The destructor will not
	// re-join because joinable() returns false after this.
	m->fillThreadStop.store(true, std::memory_order_release);
	m->fillCv.notify_all();
	if (m->fillThread.joinable()) m->fillThread.join();

	m->streamTotalFrames = 1000;

	m->startPlayback(0.1f);   // press
	m->startPlayback(0.4f);   // drag step 1
	m->startPlayback(0.7f);   // drag step 2

	REQUIRE(m->seekBaseFrame == 700);
	REQUIRE(m->pendingSeekFrame.load() == 700);

	Test::destroyModule(m);
}

// startPlayback resets outputFrameCount on each call for correct playhead tracking.
TEST_CASE("startPlayback: outputFrameCount reset on each call", "[Siren][Audio]") {
	// Each scrub seek resets the output counter so the playhead position
	// is computed relative to the new seek base, not the previous one.
	auto* m = Test::createModule<SirenModule>("Siren");
	m->streamTotalFrames = 1000;

	// Simulate some frames having been output
	m->outputFrameCount.store(200, std::memory_order_relaxed);

	m->startPlayback(0.5f);

	REQUIRE(m->outputFrameCount.load() == 0);

	Test::destroyModule(m);
}

// openStream with null source leaves pendingStream nullptr.
TEST_CASE("openStream: null source leaves pendingStream nullptr", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	m->openStream("", nullptr);

	// pendingStream is consumed by the fill thread; after exchange it should be nullptr
	// (fill thread might race, but since stream is null either way it is safe to check)
	AudioStream* ps = m->pendingStream.exchange(nullptr, std::memory_order_acq_rel);
	REQUIRE(ps == nullptr);

	Test::destroyModule(m);
}

// Fill thread plays back only the first two channels of a multi-channel file.
// Regression test: stream->readF32() always interleaves the decoder's real
// channel count, but the fill thread's scratch buffers (zcBuf, tmp, loopHead/
// loopTail) were sized assuming <=2 channels (the already-clamped fillCh).
// A file with more channels (e.g. 5.1 surround) overflowed those stack
// buffers, corrupting memory and crashing during playback (though the
// preview path, which sizes its buffers to the real channel count, was fine).
TEST_CASE("Fill thread: multi-channel stream plays back first two channels only", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	m->engineSampleRate = 44100;

	// 6-channel stream (5.1 surround). Channels 0/1 carry distinct constant
	// values so the test can verify they pass through correctly; channels
	// 2-5 carry a different value that must never leak into the output.
	const int frames = 8192;
	const int channels = 6;
	auto stream = std::unique_ptr<FakeAudioStream>(new FakeAudioStream(frames, channels, 44100, 0.f));
	for (int f = 0; f < frames; f++) {
		stream->data[(size_t)f * channels + 0] = 0.8f;
		stream->data[(size_t)f * channels + 1] = -0.4f;
		for (int c = 2; c < channels; c++) stream->data[(size_t)f * channels + c] = 1.f;
	}

	m->adoptStream(std::move(stream), frames);
	m->startPlayback(0.f);

	// Drive process() until the fill thread has caught up and the declick
	// ramp (~660 frames at 44.1 kHz) has settled; interleave short sleeps so
	// the background fill thread actually gets scheduled.
	float l = 0.f, r = 0.f;
	for (int i = 0; i < 3000; i++) {
		m->process(Test::makeProcessArgs(1));
		l = m->outputs[SirenModule::OUTPUT_L].getVoltage();
		r = m->outputs[SirenModule::OUTPUT_R].getVoltage();
		if (i % 20 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Output = sample * volume(1.0) * 5.f
	REQUIRE(l == Catch::Approx(0.8f * 5.f).margin(0.05f));
	REQUIRE(r == Catch::Approx(-0.4f * 5.f).margin(0.05f));

	Test::destroyModule(m);
}

// ─── sampleMatchesFilter: tag filtering logic ─────────────────────────────────
// Tests for include/exclude/favorites filtering on SirenBrowserPane.
// SirenBrowserPane is instantiated without init() — sampleMatchesFilter only
// reads tagFilter, tagExcludeFilter, and favoritesOnly from `this`.

TEST_CASE("sampleMatchesFilter: include filter", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.tagFilter.insert("Kick");

	SECTION("passes file that has the included tag") {
		SampleMetadata sm;
		sm.tags = {"Kick"};
		REQUIRE(pane.sampleMatchesFilter(sm) == true);
	}
	SECTION("hides file that lacks the included tag") {
		SampleMetadata sm;
		sm.tags = {"Snare"};
		REQUIRE(pane.sampleMatchesFilter(sm) == false);
	}
	SECTION("hides file with no tags") {
		SampleMetadata empty;
		REQUIRE(pane.sampleMatchesFilter(empty) == false);
	}
}

TEST_CASE("sampleMatchesFilter: exclude filter", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.tagExcludeFilter.insert("Kick");

	SECTION("hides file that has the excluded tag") {
		SampleMetadata sm;
		sm.tags = {"Kick"};
		REQUIRE(pane.sampleMatchesFilter(sm) == false);
	}
	SECTION("passes file without the excluded tag") {
		SampleMetadata sm;
		sm.tags = {"Snare"};
		REQUIRE(pane.sampleMatchesFilter(sm) == true);
	}
	// Regression: before the fix, files not in meta->samples were always hidden
	// when filterActive was true. With an exclude-only filter, an unindexed file
	// (represented here by a default-constructed SampleMetadata with no tags)
	// has no excluded tag by definition and must remain visible.
	SECTION("passes file with no tags — regression for unindexed files") {
		SampleMetadata empty;
		REQUIRE(pane.sampleMatchesFilter(empty) == true);
	}
	SECTION("hides file that has excluded tag alongside other tags") {
		SampleMetadata sm;
		sm.tags = {"Kick", "Loop"};
		REQUIRE(pane.sampleMatchesFilter(sm) == false);
	}
}

TEST_CASE("sampleMatchesFilter: include + exclude combined", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.tagFilter.insert("Loop");
	pane.tagExcludeFilter.insert("Kick");

	SECTION("passes file that has included tag but not excluded tag") {
		SampleMetadata sm;
		sm.tags = {"Loop", "Snare"};
		REQUIRE(pane.sampleMatchesFilter(sm) == true);
	}
	SECTION("hides file that has both included and excluded tag") {
		SampleMetadata sm;
		sm.tags = {"Loop", "Kick"};
		REQUIRE(pane.sampleMatchesFilter(sm) == false);
	}
	SECTION("hides file with no tags — fails include filter") {
		SampleMetadata empty;
		REQUIRE(pane.sampleMatchesFilter(empty) == false);
	}
}

TEST_CASE("sampleMatchesFilter: favorites filter", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.favoritesOnly = true;

	SECTION("passes favorited file") {
		SampleMetadata sm;
		sm.favorite = true;
		REQUIRE(pane.sampleMatchesFilter(sm) == true);
	}
	SECTION("hides non-favorited file") {
		SampleMetadata sm;
		sm.favorite = false;
		REQUIRE(pane.sampleMatchesFilter(sm) == false);
	}
	SECTION("hides file with no metadata — not favorited by default") {
		SampleMetadata empty;
		REQUIRE(pane.sampleMatchesFilter(empty) == false);
	}
}

// ─── rebuildRowWidgets: container visibility with tag filters ─────────────────
// These tests exercise the container-row visibility path in rebuildRowWidgets(),
// which is a separate code path from sampleMatchesFilter (file rows).
//
// With a positive filter (include tag or favorites) a container is hidden unless
// matchingDirs contains it (i.e. at least one indexed descendant passes the filter).
// With an exclude-only filter the matchingDirs check is skipped entirely: we cannot
// know from the index alone whether a collapsed container holds unindexed files that
// would pass, so all containers remain visible.

static SirenBrowserPane::TreeEntry makeContainerEntry(const std::string& path) {
	SirenBrowserPane::TreeEntry e;
	e.node.relativePath = path;
	e.node.name = path;
	e.node.isContainer = true;
	e.indent = 0;
	return e;
}

static SirenBrowserPane::TreeEntry makeFileEntry(const std::string& path) {
	SirenBrowserPane::TreeEntry e;
	e.node.relativePath = path;
	e.node.name = path;
	e.node.isContainer = false;
	e.indent = 1;
	return e;
}

TEST_CASE("rebuildRowWidgets: exclude-only filter keeps containers visible", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.box.size = Vec(200.f, 400.f);
	pane.init(nullptr);
	pane.setSize(pane.box.size);

	auto ds = std::make_shared<TestDataSource>("/test/root");
	pane.activeDs = ds;

	// One container with no indexed files at all — not in meta->samples.
	pane.rows.push_back(makeContainerEntry("drums"));

	pane.tagExcludeFilter.insert("Kick");
	pane.rebuildRowWidgets();

	// The container must be visible: we cannot prove it has no passing children.
	REQUIRE(pane.rowContainer->children.size() == 1);
}

TEST_CASE("rebuildRowWidgets: include filter hides container with no matching indexed files", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.box.size = Vec(200.f, 400.f);
	pane.init(nullptr);
	pane.setSize(pane.box.size);

	auto ds = std::make_shared<TestDataSource>("/test/root");
	pane.activeDs = ds;

	pane.rows.push_back(makeContainerEntry("drums"));

	pane.tagFilter.insert("Kick");
	pane.rebuildRowWidgets();

	// No indexed file under "drums" has the "Kick" tag → container is hidden.
	REQUIRE(pane.rowContainer->children.empty());
}

TEST_CASE("rebuildRowWidgets: include filter shows container that has a matching indexed file", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.box.size = Vec(200.f, 400.f);
	pane.init(nullptr);
	pane.setSize(pane.box.size);

	auto ds = std::make_shared<TestDataSource>("/test/root");
	MetadataStore* meta = ds->getMetadata();
	meta->addTag("drums/kick.wav", "Kick");
	pane.activeDs = ds;

	pane.rows.push_back(makeContainerEntry("drums"));

	pane.tagFilter.insert("Kick");
	pane.rebuildRowWidgets();

	// "drums/kick.wav" has the "Kick" tag → "drums" appears in matchingDirs → visible.
	REQUIRE(pane.rowContainer->children.size() == 1);
}

TEST_CASE("rebuildRowWidgets: exclude filter hides file with excluded tag, keeps unindexed file", "[Siren][Browser][Filter]") {
	SirenBrowserPane pane;
	pane.box.size = Vec(200.f, 400.f);
	pane.init(nullptr);
	pane.setSize(pane.box.size);

	auto ds = std::make_shared<TestDataSource>("/test/root");
	MetadataStore* meta = ds->getMetadata();
	meta->addTag("drums/kick.wav", "Kick");
	pane.activeDs = ds;

	// Three rows: directory, a tagged (excluded) file, an unindexed file.
	pane.rows.push_back(makeContainerEntry("drums"));
	pane.rows.push_back(makeFileEntry("drums/kick.wav"));    // tagged "Kick" → hidden
	pane.rows.push_back(makeFileEntry("drums/snare.wav"));   // not in meta  → visible

	pane.tagExcludeFilter.insert("Kick");
	pane.rebuildRowWidgets();

	// Container + unindexed snare.wav = 2 rows; kick.wav is hidden.
	REQUIRE(pane.rowContainer->children.size() == 2);
}

// ─── Cross-instance settings sync (ModuleChangeListener wiring) ───────────────
//
// Siren stores its settings in a process-global `sirenSettings` singleton. With
// multiple Siren module instances on the patch, mutating the global in one
// instance used to leave the others stale (their browser pane kept showing the
// old root list / active root). The fix wires each SirenWidget up as a
// ModuleChangeListener under the "Siren" topic; every mutating callback calls
// notifyModuleListeners("Siren"), and each instance's step() consumes the flag
// by re-applying sirenSettings to its browser pane.

// Helper: snapshot the global sirenSettings state we mutate in [SettingsSync]
// tests so we can restore it on the way out and not leak state to other tests.
// (Per-instance state — activeRootIdx, lastFilePath, lastPlayheadPos — now
// lives on SirenModule and is restored per test by the widget pair.)
struct SirenSettingsGuard {
	std::vector<RootContainer> savedRoots;

	SirenSettingsGuard() {
		savedRoots = sirenSettings.rootContainers;
		sirenSettings.rootContainers.clear();
	}

	~SirenSettingsGuard() {
		sirenSettings.rootContainers = savedRoots;
	}
};

// Helper: build a (module, widget) pair the way the rack does — the widget is
// what registers itself as a ModuleChangeListener for "Siren", so the pair is
// the smallest faithful unit for testing the cross-instance sync.
struct SirenWidgetPair {
	SirenModule* module;
	SirenWidget* widget;
};

static SirenWidgetPair makeWidgetPair() {
	SirenWidgetPair p;
	p.module = Test::createModule<SirenModule>("Siren");
	p.widget = Test::createWidget<SirenWidget>(p.module);
	return p;
}

static void destroyWidgetPair(SirenWidgetPair& p) {
	Test::destroyWidget(p.widget);
	Test::destroyModule(p.module);
}

TEST_CASE("Cross-instance settings sync: notifyModuleListeners reaches every SirenWidget", "[Siren][SettingsSync]") {
	SirenSettingsGuard guard;
	auto a = makeWidgetPair();
	auto b = makeWidgetPair();
	REQUIRE(a.widget != nullptr);
	REQUIRE(b.widget != nullptr);
	REQUIRE(a.widget->moduleChangedFlag == false);
	REQUIRE(b.widget->moduleChangedFlag == false);

	// Simulate one instance mutating sirenSettings by broadcasting directly.
	// This is the same channel the onAddRoot/onSelectRoot/onRemoveRoot/
	// onFileSelected callbacks use.
	StoermelderPackOne::notifyModuleListeners("Siren");

	REQUIRE(a.widget->moduleChangedFlag == true);
	REQUIRE(b.widget->moduleChangedFlag == true);

	destroyWidgetPair(b);
	destroyWidgetPair(a);
}

TEST_CASE("Cross-instance settings sync: originator clears its own flag after notify", "[Siren][SettingsSync]") {
	// Reproduce the originator-clears-self pattern used by the root/file
	// mutation callbacks: notify, then immediately reset the local flag so
	// step() does not redundantly refresh the UI we just updated inline.
	SirenSettingsGuard guard;
	auto a = makeWidgetPair();
	auto b = makeWidgetPair();

	StoermelderPackOne::notifyModuleListeners("Siren");
	// Originator (a) clears its own flag — mirroring the
	//   notifyModuleListeners("Siren"); moduleChangedFlag = false;
	// sequence in SirenWidget::SirenWidget's onAddRoot / onSelectRoot /
	// onRemoveRoot lambdas and in onFileSelected.
	a.widget->moduleChangedFlag = false;

	REQUIRE(a.widget->moduleChangedFlag == false);
	REQUIRE(b.widget->moduleChangedFlag == true);

	destroyWidgetPair(b);
	destroyWidgetPair(a);
}

TEST_CASE("Cross-instance settings sync: destroyed widget is removed from the listener set", "[Siren][SettingsSync]") {
	// After unregisterModuleListener runs in ~SirenWidget, subsequent
	// broadcasts must not touch the freed pointer. We exercise this by
	// destroying widget a, then broadcasting, then verifying widget b's flag
	// flips — without a crash or use-after-free.
	SirenSettingsGuard guard;
	auto a = makeWidgetPair();
	auto b = makeWidgetPair();

	Test::destroyWidget(a.widget);
	a.widget = nullptr;

	// Broadcasting after a's destruction must not crash.
	StoermelderPackOne::notifyModuleListeners("Siren");
	REQUIRE(b.widget->moduleChangedFlag == true);

	destroyWidgetPair(b);
}

TEST_CASE("ModuleChangeListener protocol: notify reaches all registered listeners and unregister removes them", "[Siren][SettingsSync]") {
	// Lower-level protocol test using bare ModuleChangeListener objects, so
	// the registry's semantics are pinned down independently of the Siren
	// widget tree.
	StoermelderPackOne::ModuleChangeListener x{false};
	StoermelderPackOne::ModuleChangeListener y{false};

	StoermelderPackOne::registerModuleListener("SirenSyncTest", &x);
	StoermelderPackOne::registerModuleListener("SirenSyncTest", &y);

	StoermelderPackOne::notifyModuleListeners("SirenSyncTest");
	REQUIRE(x.moduleChangedFlag == true);
	REQUIRE(y.moduleChangedFlag == true);

	// Unregister one; the other must still be notified next time.
	StoermelderPackOne::unregisterModuleListener("SirenSyncTest", &x);
	x.moduleChangedFlag = false;
	y.moduleChangedFlag = false;

	StoermelderPackOne::notifyModuleListeners("SirenSyncTest");
	REQUIRE(x.moduleChangedFlag == false);  // not in the set anymore
	REQUIRE(y.moduleChangedFlag == true);   // still registered

	StoermelderPackOne::unregisterModuleListener("SirenSyncTest", &y);
}

TEST_CASE("Cross-instance settings sync: SirenWidget step() refreshes browser pane from sirenSettings", "[Siren][SettingsSync]") {
	// End-to-end behaviour: when the flag fires, the widget's step() must
	// rebuild the browser pane from the current global sirenSettings, using
	// the module's own activeRootIdx as the active selection. We seed the
	// global with one root entry, give the module its own active root, set
	// the flag, run step(), and verify the browser pane picked up the change.
	SirenSettingsGuard guard;
	sirenSettings.rootContainers.push_back(createRootContainer("/seeded/path", "fs"));

	auto p = makeWidgetPair();
	REQUIRE(p.widget != nullptr);
	REQUIRE(p.widget->browserPane != nullptr);
	p.module->activeRootIdx = 0;

	// Simulate the flag arriving from another instance.
	p.widget->moduleChangedFlag = true;
	p.widget->step();
	REQUIRE(p.widget->moduleChangedFlag == false);
	REQUIRE(p.widget->browserPane->rootContainers.size() == 1);
	REQUIRE(p.widget->browserPane->rootContainers[0].path == "/seeded/path");
	REQUIRE(p.widget->browserPane->activeRootIdx == 0);

	destroyWidgetPair(p);
}

// ─── Per-instance state (refactor: moved off the process-global SirenSettings) ─
//
// activeRootIdx, lastFilePath and lastPlayheadPos used to live on the
// process-global `sirenSettings` singleton, so changing them in one Siren
// instance silently moved every other instance to the same selection. They
// now live on SirenModule (persisted with the patch) — each instance owns
// its own and changes are local.

// Two instances with different active roots must each keep their own.
// Constructing a widget reads module->activeRootIdx; changing it on one
// instance must NOT change the other.
TEST_CASE("Per-instance activeRootIdx: two Siren instances keep separate selections", "[Siren][PerInstance]") {
	SirenSettingsGuard guard;
	sirenSettings.rootContainers.push_back(createRootContainer("/root-a", "fs"));
	sirenSettings.rootContainers.push_back(createRootContainer("/root-b", "fs"));

	auto a = makeWidgetPair();
	auto b = makeWidgetPair();
	a.module->activeRootIdx = 0;
	b.module->activeRootIdx = 1;
	a.widget->browserPane->setRoots(sirenSettings.rootContainers, a.module->activeRootIdx);
	b.widget->browserPane->setRoots(sirenSettings.rootContainers, b.module->activeRootIdx);

	REQUIRE(a.widget->browserPane->activeRootIdx == 0);
	REQUIRE(b.widget->browserPane->activeRootIdx == 1);

	// Mutate a's selection locally — b must NOT follow.
	a.module->activeRootIdx = 1;
	a.widget->browserPane->setRoots(sirenSettings.rootContainers, a.module->activeRootIdx);

	REQUIRE(a.widget->browserPane->activeRootIdx == 1);
	REQUIRE(b.widget->browserPane->activeRootIdx == 1);  // unchanged

	// And vice versa: changing b does not move a.
	b.module->activeRootIdx = 0;
	b.widget->browserPane->setRoots(sirenSettings.rootContainers, b.module->activeRootIdx);

	REQUIRE(a.widget->browserPane->activeRootIdx == 1);  // unchanged
	REQUIRE(b.widget->browserPane->activeRootIdx == 0);

	destroyWidgetPair(b);
	destroyWidgetPair(a);
}

// Removing a root from instance A shifts the indices on instance B if the
// removed root was at or before B's own active index. The shared root list
// changes; each instance's activeRootIdx is per-instance and must be
// clamped by B's step() refresh.
TEST_CASE("Per-instance activeRootIdx: removing a root before another instance's idx shifts it", "[Siren][PerInstance]") {
	SirenSettingsGuard guard;
	sirenSettings.rootContainers.push_back(createRootContainer("/r0", "fs"));
	sirenSettings.rootContainers.push_back(createRootContainer("/r1", "fs"));
	sirenSettings.rootContainers.push_back(createRootContainer("/r2", "fs"));

	auto a = makeWidgetPair();
	auto b = makeWidgetPair();

	// Both pick their own root. b's idx = 2 (third root).
	a.module->activeRootIdx = 0;
	b.module->activeRootIdx = 2;
	a.widget->browserPane->setRoots(sirenSettings.rootContainers, a.module->activeRootIdx);
	b.widget->browserPane->setRoots(sirenSettings.rootContainers, b.module->activeRootIdx);

	REQUIRE(b.widget->browserPane->rootContainers.size() == 3);

	// Instance A removes its active root (/r0) — wipes the global root at idx 0.
	// B's module still says activeRootIdx = 2, but the list is now only 2 long.
	sirenSettings.removeRootAt(a.module->activeRootIdx, nullptr);

	// B hasn't been told yet — its browserPane still reflects the pre-removal state.
	REQUIRE(b.widget->browserPane->rootContainers.size() == 3);

	// The cross-instance broadcast arrives; B's step() refreshes and clamps.
	b.widget->moduleChangedFlag = true;
	b.widget->step();

	REQUIRE(b.widget->browserPane->rootContainers.size() == 2);
	// B's idx 2 was out of range after the removal; step() clamps it to a
	// valid index in the shrunken list (0 here, since the new size is 2).
	REQUIRE(b.widget->browserPane->activeRootIdx == 0);

	destroyWidgetPair(b);
	destroyWidgetPair(a);
}

// lastFilePath is per-instance: setting it on one module must not bleed into
// another.
TEST_CASE("Per-instance lastFilePath: two Siren instances keep separate files", "[Siren][PerInstance]") {
	SirenSettingsGuard guard;
	auto a = makeWidgetPair();
	auto b = makeWidgetPair();

	a.module->lastFilePath = "/samples/a.wav";
	b.module->lastFilePath = "/samples/b.wav";

	a.module->lastFilePath = "/samples/a2.wav";  // mutate a only

	REQUIRE(a.module->lastFilePath == "/samples/a2.wav");
	REQUIRE(b.module->lastFilePath == "/samples/b.wav");

	destroyWidgetPair(b);
	destroyWidgetPair(a);
}

// SirenModule persists activeRootIdx, lastFilePath, lastPlayheadPos and trim
// range into the patch JSON — they must round-trip even though SirenSettings
// no longer touches them.
TEST_CASE("Per-instance state: JSON round-trips activeRootIdx, lastFilePath, lastPlayheadPos, trim", "[Siren][PerInstance][JSON]") {
	SirenSettingsGuard guard;
	auto m = Test::createModule<SirenModule>("Siren");

	m->activeRootIdx = 3;
	m->lastFilePath = "/round/trip.wav";
	m->lastPlayheadPos = 0.875f;
	m->trimIn.store(0.21f, std::memory_order_relaxed);
	m->trimOut.store(0.79f, std::memory_order_relaxed);

	json_t* j = m->dataToJson();
	REQUIRE(j != nullptr);

	REQUIRE(json_integer_value(json_object_get(j, "activeRootIdx")) == 3);
	REQUIRE(std::string(json_string_value(json_object_get(j, "lastFile"))) == "/round/trip.wav");
	REQUIRE(json_real_value(json_object_get(j, "lastPlayheadPos")) == Catch::Approx(0.875).margin(0.001));
	REQUIRE(json_real_value(json_object_get(j, "trimIn")) == Catch::Approx(0.21).margin(0.001));
	REQUIRE(json_real_value(json_object_get(j, "trimOut")) == Catch::Approx(0.79).margin(0.001));

	// Reset module fields, then restore from JSON.
	m->activeRootIdx = -1;
	m->lastFilePath = "";
	m->lastPlayheadPos = 0.f;
	m->trimIn.store(0.f, std::memory_order_relaxed);
	m->trimOut.store(1.f, std::memory_order_relaxed);
	m->dataFromJson(j);

	REQUIRE(m->activeRootIdx == 3);
	REQUIRE(m->lastFilePath == "/round/trip.wav");
	REQUIRE(m->lastPlayheadPos == Catch::Approx(0.875).margin(0.001));
	REQUIRE(m->trimIn.load() == Catch::Approx(0.21).margin(0.001));
	REQUIRE(m->trimOut.load() == Catch::Approx(0.79).margin(0.001));

	json_decref(j);
	Test::destroyModule(m);
}

// Out-of-range trim values in patch JSON must be clamped to [0,1] so a
// malformed patch can never produce an out-of-range trim that breaks loop
// wrapping at trimOut → trimIn.
TEST_CASE("Per-instance state: out-of-range trim is clamped on dataFromJson", "[Siren][PerInstance][JSON]") {
	SirenSettingsGuard guard;
	auto m = Test::createModule<SirenModule>("Siren");

	json_t* j = json_object();
	json_object_set_new(j, "trimIn", json_real(-0.5));
	json_object_set_new(j, "trimOut", json_real(1.7));
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->trimIn.load() == 0.f);
	REQUIRE(m->trimOut.load() == 1.f);

	Test::destroyModule(m);
}

// The module's trimIn/trimOut atomics are the single source of truth for
// the canvas's visible trim range: when wired up via moduleInPoint /
// moduleOutPoint, the canvas's getInPoint/getOutPoint/setInPoint/setOutPoint
// all read from / write to the module atomics — not the canvas's local
// inPoint/outPoint fields. Saving and restoring a patch therefore
// automatically restores the visible trim range, without any explicit
// "restore" step in the widget constructor.
TEST_CASE("Canvas trim is driven by the module atomics when wired up", "[Siren][PerInstance]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);
	pane.init(nullptr, nullptr);

	// Simulate a real module by wiring up atomics the way SirenWidget does.
	std::atomic<float> moduleIn{0.f};
	std::atomic<float> moduleOut{1.f};
	pane.canvas->moduleInPoint = &moduleIn;
	pane.canvas->moduleOutPoint = &moduleOut;

	// Reads go through the atomic — local fields are ignored.
	pane.canvas->inPoint = 0.7f;
	pane.canvas->outPoint = 0.9f;
	REQUIRE(pane.canvas->getInPoint() == 0.f);
	REQUIRE(pane.canvas->getOutPoint() == 1.f);

	// Writes go through the atomic.
	pane.canvas->setInPoint(0.25f);
	pane.canvas->setOutPoint(0.75f);
	REQUIRE(moduleIn.load() == 0.25f);
	REQUIRE(moduleOut.load() == 0.75f);
	REQUIRE(pane.canvas->getInPoint() == 0.25f);
	REQUIRE(pane.canvas->getOutPoint() == 0.75f);
	// Local fields are NOT touched — they remain at their previous values.
	REQUIRE(pane.canvas->inPoint == 0.7f);
	REQUIRE(pane.canvas->outPoint == 0.9f);
}

// dataFromJson followed by a draw (or any getInPoint/getOutPoint read) on
// the canvas immediately reflects the saved trim values — no widget
// constructor restore step is needed because the canvas reads from the
// module atomics on every read.
TEST_CASE("Restored trim is visible immediately without an explicit restore step", "[Siren][PerInstance]") {
	SirenSettingsGuard guard;
	auto m = Test::createModule<SirenModule>("Siren");
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);
	pane.init(nullptr, nullptr);
	pane.canvas->moduleInPoint = &m->trimIn;
	pane.canvas->moduleOutPoint = &m->trimOut;

	// Persist trim, simulate preset being loaded, then read the canvas.
	m->trimIn.store(0.12f, std::memory_order_relaxed);
	m->trimOut.store(0.88f, std::memory_order_relaxed);
	json_t* j = m->dataToJson();
	m->trimIn.store(0.f, std::memory_order_relaxed);
	m->trimOut.store(1.f, std::memory_order_relaxed);
	m->dataFromJson(j);
	json_decref(j);

	// The canvas sees the restored values without any explicit restore call.
	REQUIRE(pane.canvas->getInPoint() == Catch::Approx(0.12f).margin(0.001));
	REQUIRE(pane.canvas->getOutPoint() == Catch::Approx(0.88f).margin(0.001));

	Test::destroyModule(m);
}
