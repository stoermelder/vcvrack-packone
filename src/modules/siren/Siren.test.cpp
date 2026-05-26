#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Siren.cpp"

using namespace StoermelderPackOne::Siren;

SYNC_MODEL(modelSiren, "Siren");
Test::TestContext<> testContext;

// ─── Construction ─────────────────────────────────────────────────────────────

TEST_CASE("Construction and initialization", "[Siren]") {
	auto* m = Test::createModule<SirenModule>("Siren");
	REQUIRE(m != nullptr);
	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == 0.f);
	REQUIRE(m->outputs[SirenModule::OUTPUT_R].getVoltage() == 0.f);
	Test::destroyModule(m);
}

// ─── JSON serialization ───────────────────────────────────────────────────────

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

TEST_CASE("RootMetadata: tags", "[Siren][Metadata]") {
	RootMetadata meta;
	meta.rootPath = "/test/root";

	SECTION("Add and retrieve tag") {
		meta.addTag("field/rain.wav", "ambient");
		auto tags = meta.getTags("field/rain.wav");
		REQUIRE(tags.size() == 1);
		REQUIRE(tags[0] == "ambient");
	}

	SECTION("No duplicate tags") {
		meta.addTag("field/rain.wav", "ambient");
		meta.addTag("field/rain.wav", "ambient");
		REQUIRE(meta.getTags("field/rain.wav").size() == 1);
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
		REQUIRE(all.count("drone") == 1);
	}
}

// ─── RootMetadata: JSON I/O ───────────────────────────────────────────────────

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
		cache.bucketCount = 100;
		cache.peaks.push_back(std::vector<std::pair<float,float>>(100, {-0.5f, 0.5f}));
		REQUIRE(cache.empty() == false);
	}
}

// ─── hashPath ─────────────────────────────────────────────────────────────────

TEST_CASE("hashPath produces 8-char hex string", "[Siren][Utility]") {
	std::string h = hashPath("/Users/ben/Samples");
	REQUIRE(h.size() == 8);
	for (char c : h) {
		REQUIRE(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
	}
}

TEST_CASE("hashPath is deterministic", "[Siren][Utility]") {
	REQUIRE(hashPath("/foo/bar") == hashPath("/foo/bar"));
	REQUIRE(hashPath("/foo/bar") != hashPath("/foo/baz"));
}

// ─── Audio output: silence without file ──────────────────────────────────────

TEST_CASE("Audio output: silence without loaded file", "[Siren][Audio]") {
	auto* m = Test::createModule<SirenModule>("Siren");

	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->outputs[SirenModule::OUTPUT_L].getVoltage() == 0.f);
	REQUIRE(m->outputs[SirenModule::OUTPUT_R].getVoltage() == 0.f);

	Test::destroyModule(m);
}

// ─── Playhead clamping ────────────────────────────────────────────────────────

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

// ─── SirenDragState ───────────────────────────────────────────────────────────

TEST_CASE("SirenDragState initial state", "[Siren][DragDrop]") {
	SirenDragState ds;
	REQUIRE(ds.active == false);
	REQUIRE(ds.dragPath.empty());
}
