#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "SirenFileSystem.hpp"
#include <fstream>
#include <algorithm>

using namespace StoermelderPackOne::Siren;

Test::TestContext<> testContext;

// ─── TempDir RAII helper ──────────────────────────────────────────────────────
// Creates a unique temporary directory; removes it on destruction.

struct TempDir {
	ghc::filesystem::path path;

	TempDir() {
		static int seq = 0;
		path = ghc::filesystem::temp_directory_path()
		       / ("sirenfs_test_" + std::to_string(++seq));
		ghc::filesystem::create_directories(path);
	}

	~TempDir() { ghc::filesystem::remove_all(path); }

	// Create an empty file with the given name inside this directory.
	void touch(const std::string& name) const {
		std::ofstream f((path / name).string());
	}

	std::string filePath(const std::string& name) const {
		return (path / name).string();
	}

	std::string str() const { return path.string(); }
};

// ─── prepareForDrop helper ────────────────────────────────────────────────────

// prepareForDrop() returns a task lambda; calling it executes the (possibly heavy)
// work synchronously. In tests this is fine — no real conversion is attempted since
// the source files don't exist or are already converted.
static std::string callPrepareForDrop(FileSystemDataSource& src, const std::string& id) {
	return src.prepareForDrop(id)();
}

// ─── CONVERTED_WAV_SUFFIX sanity ─────────────────────────────────────────────

TEST_CASE("CONVERTED_WAV_SUFFIX_LEN matches suffix length", "[Siren][FileSystem]") {
	REQUIRE(std::string(CONVERTED_WAV_SUFFIX).size() == CONVERTED_WAV_SUFFIX_LEN);
}

// ─── isConvertedWavFile ───────────────────────────────────────────────────────

TEST_CASE("isConvertedWavFile: recognises converted-artifact suffix", "[Siren][FileSystem]") {
	SECTION("Files ending with .converted.wav are artifacts") {
		REQUIRE(isConvertedWavFile("kick.converted.wav")        == true);
		REQUIRE(isConvertedWavFile("my sample.converted.wav")   == true);
		REQUIRE(isConvertedWavFile("pad.with.dots.converted.wav") == true);
	}

	SECTION("Regular audio files are not artifacts") {
		REQUIRE(isConvertedWavFile("kick.wav")   == false);
		REQUIRE(isConvertedWavFile("pad.flac")   == false);
		REQUIRE(isConvertedWavFile("bass.mp3")   == false);
		REQUIRE(isConvertedWavFile("bass.WAV")   == false);
	}

	SECTION("Edge cases") {
		REQUIRE(isConvertedWavFile("")             == false);
		REQUIRE(isConvertedWavFile(".converted.wav") == true);   // stem-less but valid
		REQUIRE(isConvertedWavFile("converted.wav")  == false);  // missing dot before "converted"
		REQUIRE(isConvertedWavFile(".wav")           == false);
	}
}

// ─── RootMetadata: convertToWavOnDrop ────────────────────────────────────────

TEST_CASE("RootMetadata: convertToWavOnDrop defaults to false", "[Siren][FileSystem]") {
	RootMetadata meta;
	REQUIRE(meta.convertToWavOnDrop == false);
}

TEST_CASE("RootMetadata: convertToWavOnDrop round-trips through JSON", "[Siren][FileSystem]") {
	RootMetadata meta;
	meta.rootPath = "/test/root";

	SECTION("Persists true") {
		meta.convertToWavOnDrop = true;
		json_t* j = meta.toJson();
		RootMetadata restored;
		restored.fromJson(j);
		json_decref(j);
		REQUIRE(restored.convertToWavOnDrop == true);
	}

	SECTION("Persists false") {
		meta.convertToWavOnDrop = false;
		json_t* j = meta.toJson();
		RootMetadata restored;
		restored.fromJson(j);
		json_decref(j);
		REQUIRE(restored.convertToWavOnDrop == false);
	}

	SECTION("Survives round-trip alongside favorites and tags") {
		meta.convertToWavOnDrop = true;
		meta.setFavorite("/kick.wav", true);
		meta.addTag("/kick.wav", "percussion");

		json_t* j = meta.toJson();
		RootMetadata restored;
		restored.fromJson(j);
		json_decref(j);

		REQUIRE(restored.convertToWavOnDrop == true);
		REQUIRE(restored.isFavorite("/kick.wav") == true);
		REQUIRE(restored.getTags("/kick.wav").size() == 1);
	}
}

// ─── FileSystemDataSource: convertToWavOnDrop flag access ────────────────────

TEST_CASE("FileSystemDataSource: convertToWavOnDrop is false by default", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(src.getMetadata()->convertToWavOnDrop == false);
}

TEST_CASE("FileSystemDataSource: convertToWavOnDrop can be set via metadata pointer", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	src.getMetadata()->convertToWavOnDrop = true;
	REQUIRE(src.getMetadata()->convertToWavOnDrop == true);
}

// ─── loadChildrenSync: .converted.wav filtering ──────────────────────────────

TEST_CASE("loadChildrenSync: excludes .converted.wav files from results", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	tmp.touch("kick.converted.wav");  // conversion artifact — must not appear in browser
	tmp.touch("pad.flac");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync(tmp.str());

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "kick.wav")           != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "pad.flac")           != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "kick.converted.wav") == names.end());
}

TEST_CASE("loadChildrenSync: correct count when multiple .converted.wav files are present", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("a.wav");
	tmp.touch("a.converted.wav");
	tmp.touch("b.mp3");
	tmp.touch("b.converted.wav");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync(tmp.str());

	REQUIRE(nodes.size() == 2);
	for (const auto& n : nodes)
		REQUIRE(isConvertedWavFile(n.name) == false);
}

TEST_CASE("loadChildrenSync: container (directory) alongside .converted.wav is unaffected", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("sample.flac");
	tmp.touch("sample.converted.wav");
	// Create a sub-directory
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "Drums");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync(tmp.str());

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "Drums")               != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample.flac")         != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample.converted.wav") == names.end());
}

TEST_CASE("loadChildrenSync: node isContainer flag is correct", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "Loops");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync(tmp.str());

	// Containers first, then files — find each by name
	const DataSourceNode* fileNode = nullptr;
	const DataSourceNode* containerNode = nullptr;
	for (const auto& n : nodes) {
		if (n.name == "kick.wav")  fileNode      = &n;
		if (n.name == "Loops")     containerNode = &n;
	}

	REQUIRE(fileNode      != nullptr);
	REQUIRE(containerNode != nullptr);
	REQUIRE(fileNode->isContainer      == false);
	REQUIRE(containerNode->isContainer == true);
}

// ─── prepareForDrop ──────────────────────────────────────────────────────────

TEST_CASE("prepareForDrop: returns id unchanged when convertToWavOnDrop is false", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	std::string id = tmp.filePath("drone.mp3");
	REQUIRE(callPrepareForDrop(src, id) == id);
}

TEST_CASE("prepareForDrop: returns id unchanged for .wav files even when flag is true", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	src.getMetadata()->convertToWavOnDrop = true;

	std::string id = tmp.filePath("kick.wav");
	REQUIRE(callPrepareForDrop(src, id) == id);
}

TEST_CASE("prepareForDrop: .WAV extension (uppercase) also treated as wav — no conversion", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	src.getMetadata()->convertToWavOnDrop = true;

	std::string id = tmp.filePath("sample.WAV");
	REQUIRE(callPrepareForDrop(src, id) == id);
}

TEST_CASE("prepareForDrop: returns existing .converted.wav without decoding (idempotent)", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("pad.converted.wav");

	FileSystemDataSource src(tmp.str());
	src.getMetadata()->convertToWavOnDrop = true;

	std::string sourceId = tmp.filePath("pad.flac");
	std::string expected = tmp.filePath("pad.converted.wav");
	REQUIRE(callPrepareForDrop(src, sourceId) == expected);
}

TEST_CASE("prepareForDrop: falls back to id when source cannot be decoded", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	src.getMetadata()->convertToWavOnDrop = true;

	// Non-existent file → decode fails inside the returned lambda → original id returned
	std::string id = tmp.filePath("ghost.flac");
	REQUIRE(callPrepareForDrop(src, id) == id);
}

TEST_CASE("prepareForDrop: non-existent .mp3 with flag true also falls back", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	src.getMetadata()->convertToWavOnDrop = true;

	std::string id = tmp.filePath("missing.mp3");
	REQUIRE(callPrepareForDrop(src, id) == id);
}

TEST_CASE("prepareForDrop: output path uses stem + .converted.wav suffix", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("my loop.converted.wav");

	FileSystemDataSource src(tmp.str());
	src.getMetadata()->convertToWavOnDrop = true;

	REQUIRE(callPrepareForDrop(src, tmp.filePath("my loop.flac"))
	        == tmp.filePath("my loop.converted.wav"));
}
