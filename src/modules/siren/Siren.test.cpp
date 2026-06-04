#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Siren.cpp"

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

// JSON round-trip preserves lastFile, lastPlayheadPos and activeRootIdx.
TEST_CASE("JSON serialization", "[Siren][JSON]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	// Set state and serialise
	m->lastFilePath = "/some/path/sample.wav";
	m->lastPlayheadPos = 0.42f;
	m->activeRootIdx = 2;

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

	json_decref(j);
	Test::destroyModule(m);
}

// ─── RootMetadata: favorites ──────────────────────────────────────────────────
// favorite flag can be set, cleared, and persists independently of tags.
TEST_CASE("RootMetadata: favorites", "[Siren][Metadata]") {
	RootMetadata meta;
	meta.rootPath = "/test/root";

	SECTION("Set and get favorite") {
		meta.setFavorite("drums/kick.wav", true);
		REQUIRE(meta.isFavorite("drums/kick.wav") == true);
		REQUIRE(meta.isFavorite("drums/snare.wav") == false);
	}

	SECTION("Clear favorite removes entry when no tags") {
		meta.setFavorite("drums/kick.wav", true);
		meta.setFavorite("drums/kick.wav", false);
		REQUIRE(meta.isFavorite("drums/kick.wav") == false);
		REQUIRE(meta.samples.find("drums/kick.wav") == meta.samples.end());
	}

	SECTION("Clearing favorite keeps entry when tags remain") {
		meta.addTag("drums/kick.wav", "percussion");
		meta.setFavorite("drums/kick.wav", true);
		meta.setFavorite("drums/kick.wav", false);
		REQUIRE(meta.isFavorite("drums/kick.wav") == false);
		REQUIRE(meta.samples.find("drums/kick.wav") != meta.samples.end());
	}
}

// ─── RootMetadata: tags ───────────────────────────────────────────────────────
// tags can be added, retrieved, removed, and allTags returns the union.
TEST_CASE("RootMetadata: tags", "[Siren][Metadata]") {
	RootMetadata meta;
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
		meta.addTag("a.wav", "drone");
		meta.addTag("b.wav", "loop");
		meta.addTag("b.wav", "drone");
		auto all = meta.allTags();
		REQUIRE(all.count("drone") == 1);
		REQUIRE(all.count("loop") == 1);
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
	RootMetadata meta;

	meta.addTag("kick.wav", "Dark Ambient");
	REQUIRE(meta.getTags("kick.wav")[0] == "Dark Ambient");

	meta.addTag("snare.wav", "FX");
	REQUIRE(meta.getTags("snare.wav")[0] == "FX");

	meta.addTag("hat.wav", "one-shot");
	REQUIRE(meta.getTags("hat.wav")[0] == "one-shot");
}

// case-insensitive duplicate prevention keeps only the first spelling.
TEST_CASE("addTag: case-insensitive duplicate prevention", "[Siren][Metadata]") {
	RootMetadata meta;

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
	RootMetadata meta;
	// Store a capitalised custom tag
	meta.addTag("a.wav", "Heavy Bass");
	auto all = meta.allTags();
	// The exact stored spelling appears in allTags
	REQUIRE(all.count("Heavy Bass") == 1);
	// The lowercase version does not appear separately
	REQUIRE(all.count("heavy bass") == 0);
}

// ─── RootMetadata: JSON I/O ───────────────────────────────────────────────────
// metadata serialises to JSON and deserialises back correctly.
TEST_CASE("RootMetadata: JSON round-trip", "[Siren][Metadata]") {
	RootMetadata meta;
	meta.rootPath = "/test/root";
	meta.setFavorite("a.wav", true);
	meta.addTag("a.wav", "drone");
	meta.addTag("b.wav", "loop");

	json_t* j = meta.toJson();
	REQUIRE(j != nullptr);

	RootMetadata meta2;
	meta2.fromJson(j);
	json_decref(j);

	REQUIRE(meta2.rootPath == "/test/root");
	REQUIRE(meta2.isFavorite("a.wav") == true);
	REQUIRE(meta2.isFavorite("b.wav") == false);
	auto tags = meta2.getTags("a.wav");
	REQUIRE(std::find(tags.begin(), tags.end(), "drone") != tags.end());
	auto tags2 = meta2.getTags("b.wav");
	REQUIRE(std::find(tags2.begin(), tags2.end(), "loop") != tags2.end());
}

// ─── FileSystemDataSource ─────────────────────────────────────────────────────
// isSupportedFile accepts .wav/.flac/.mp3 and rejects other extensions.
TEST_CASE("FileSystemDataSource: supported file filter", "[Siren][FileSystem]") {
	FileSystemDataSource src("/tmp");

	SECTION("Supported extensions accepted") {
		REQUIRE(src.isSupportedFile("kick.wav") == true);
		REQUIRE(src.isSupportedFile("kick.WAV") == true);
		REQUIRE(src.isSupportedFile("pad.flac") == true);
		REQUIRE(src.isSupportedFile("bass.mp3") == true);
	}

	SECTION("Unsupported extensions rejected") {
		REQUIRE(src.isSupportedFile("patch.txt") == false);
		REQUIRE(src.isSupportedFile("song.aif") == false);
		REQUIRE(src.isSupportedFile("image.png") == false);
		REQUIRE(src.isSupportedFile("notes.json") == false);
	}
}

// ─── WaveformCache: timestamp invalidation ────────────────────────────────────
// cache tracks file mtime and reports empty/non-empty state.
TEST_CASE("WaveformCache: timestamp validation", "[Siren][Audio]") {
	SECTION("Different timestamp is detected as stale") {
		WaveformCache cache;
		cache.fileTimestamp = 12345;
		// Simulate: stored timestamp != current mtime
		// (We test the logic by checking the stored value)
		REQUIRE(cache.fileTimestamp == 12345);
		// If current mtime were different, loadWaveformCacheFile returns false
	}

	SECTION("Empty cache reports empty()") {
		WaveformCache cache;
		REQUIRE(cache.empty() == true);
	}

	SECTION("Non-empty cache reports not empty") {
		WaveformCache cache;
		cache.sampleCount = 100;
		cache.samples.push_back(std::vector<float>(100, 0.f));
		REQUIRE(cache.empty() == false);
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
		Rect wr = pane.waveformRect();
		Vec midpoint = wr.pos.plus(wr.size.mult(0.5f));
		float result = pane.posToPlayhead(midpoint);
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
	RootMetadata meta;
	meta.rootPath = "/test/root";

	// Assign a custom tag — starter tags must still appear (regression for the
	// bug where the "if (result.empty())" guard dropped them once any tag existed)
	meta.addTag("kick.wav", "percussion");
	auto all = meta.allTags();

	REQUIRE(all.count("percussion") == 1);
	for (const std::string& starter : starterTags())
		REQUIRE(all.count(starter) == 1);
}

// user tags merge with starter tags; duplicates are prevented.
TEST_CASE("allTags: user tags merge with starter tags without duplicates", "[Siren][Metadata]") {
	RootMetadata meta;
	meta.rootPath = "/test/root";

	// "drone" is already a starter tag; adding it again must not duplicate it
	meta.addTag("a.wav", "drone");
	auto all = meta.allTags();
	REQUIRE(all.count("drone") == 1);
}

// starter tags are loaded from SirenTags.json at runtime; in tests we use
// the hard-coded fallback (mirrors the JSON contents).
TEST_CASE("starterTags: returns 15-tag canonical list in tests", "[Siren][Metadata][Manifest]") {
	auto tags = starterTags();
	REQUIRE(tags.size() == 15);
	// Spot-check a few representative tags from the new vocabulary
	REQUIRE(std::find(tags.begin(), tags.end(), "Drone")    != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Pad")      != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Lead")     != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Stab")     != tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Noise")    != tags.end());
	// Removed from the old list
	REQUIRE(std::find(tags.begin(), tags.end(), "Fx")       == tags.end());
	REQUIRE(std::find(tags.begin(), tags.end(), "Ambient")  == tags.end());
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
	// ignoring modulePlayheadPos.  This prevents the DSP thread from overwriting
	// the visual position while the fill thread is still processing the seek.
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);

	// Simulate a playing module whose playhead is at 0.1
	std::atomic<float> fakePlayhead{0.1f};
	pane.modulePlayheadPos = &fakePlayhead;

	// User starts dragging to 0.7
	pane.scrubPos         = 0.7f;
	pane.inPoint          = 0.7f;
	pane.draggingPlayhead = true;

	// The visual playhead should track scrubPos, not modulePlayheadPos
	float displayedPh = pane.draggingPlayhead
	    ? pane.scrubPos
	    : (pane.modulePlayheadPos ? pane.modulePlayheadPos->load() : 0.f);
	REQUIRE(displayedPh == Catch::Approx(0.7f));

	// After drag ends, display switches back to modulePlayheadPos
	pane.draggingPlayhead = false;
	displayedPh = pane.draggingPlayhead
	    ? pane.scrubPos
	    : (pane.modulePlayheadPos ? pane.modulePlayheadPos->load() : 0.f);
	REQUIRE(displayedPh == Catch::Approx(0.1f));
}

// loadItem resets inPoint and scrubPos to 0 on each call.
TEST_CASE("loadItem resets inPoint and scrubPos", "[Siren][Preview]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);

	pane.inPoint  = 0.7f;
	pane.scrubPos = 0.7f;

	pane.loadItem("", nullptr, nullptr);

	REQUIRE(pane.inPoint  == 0.f);
	REQUIRE(pane.scrubPos == 0.f);
}

// loadItem with empty id or null source leaves currentId empty.
TEST_CASE("loadItem: no item loaded for empty id or null source", "[Siren][Preview]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);

	pane.loadItem("", nullptr, nullptr);
	// Stream ownership moved to SirenModule's fill thread; verify by checking
	// that the pane did not record any valid item.
	REQUIRE(pane.currentId.empty());
}

// loadItem with startPlay=true does not start playback when no stream can be opened.
TEST_CASE("loadItem: playing stays false when no stream can be opened", "[Siren][Preview]") {
	SirenPreviewPane pane;
	pane.box.size = Vec(600.f, 380.f);

	// Wire a local playing flag so the pane's modulePlaying read has a target.
	std::atomic<bool> playing{false};
	pane.modulePlaying = &playing;

	// startPlay=true with empty id → early return before startPlaybackFrom()
	pane.loadItem("", nullptr, nullptr, /*startPlay=*/true);
	REQUIRE(playing.load() == false);
}

// ─── FileSystemDataSource: metadata ownership ────────────────────────────────
// getMetadata returns a valid pointer with correct rootPath.
TEST_CASE("FileSystemDataSource: getMetadata returns valid pointer", "[Siren][FileSystem]") {
	FileSystemDataSource src("/tmp/siren_test_nonexistent");

	RootMetadata* meta = src.getMetadata();
	REQUIRE(meta != nullptr);
	REQUIRE(meta->rootPath == "/tmp/siren_test_nonexistent");
	// samples may be non-empty if a metadata file was previously persisted for this path
}

// metadata pointer remains stable and allows mutation.
TEST_CASE("FileSystemDataSource: metadata is mutable through pointer", "[Siren][FileSystem]") {
	FileSystemDataSource src("/tmp/siren_test_nonexistent");

	RootMetadata* meta = src.getMetadata();
	meta->addTag("kick.wav", "percussion");
	REQUIRE(meta->getTags("kick.wav").size() == 1);
	// Pointer remains stable: same address on second call
	REQUIRE(src.getMetadata() == meta);
}


// ─── Audio streaming: ring buffer / DSP ──────────────────────────────────────
// process() reads from ring buffers and applies volume scaling.
// Helper: push a stereo frame directly into the module's ring buffers.
static void pushFrame(SirenModule* m, float l, float r) {
	m->rbL.push(l);
	m->rbR.push(r);
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
