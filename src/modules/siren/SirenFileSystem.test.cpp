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
static std::string callPrepareForDrop(FileSystemDataSource& src, const std::string& id, bool convertToWav) {
	return src.prepareForDrop(id, convertToWav)();
}

// ─── isGeneratedFile ──────────────────────────────────────────────────────────
// pattern: _siren + exactly 6 lowercase letters + .wav suffix, must be at position size-16.
TEST_CASE("isGeneratedFile: recognises _siren+6letters.wav pattern", "[Siren][FileSystem]") {
	REQUIRE(isGeneratedFile("_sirenabcdef.wav")         == true);
	REQUIRE(isGeneratedFile("kick_sirenabcdef.wav")     == true);
	REQUIRE(isGeneratedFile("pad.with.dots_sirenuvwxyz.wav") == true);
}

// regular audio files are not flagged as generated.
TEST_CASE("isGeneratedFile: regular audio files are not generated", "[Siren][FileSystem]") {
	REQUIRE(isGeneratedFile("kick.wav")   == false);
	REQUIRE(isGeneratedFile("pad.flac")   == false);
	REQUIRE(isGeneratedFile("bass.mp3")   == false);
	REQUIRE(isGeneratedFile("bass.WAV")   == false);
}

// old .converted.wav naming and various invalid patterns are rejected.
TEST_CASE("isGeneratedFile: rejects old .converted.wav and other edge cases", "[Siren][FileSystem]") {
	REQUIRE(isGeneratedFile("kick.converted.wav")   == false);
	REQUIRE(isGeneratedFile("sample.converted.wav")  == false);
	REQUIRE(isGeneratedFile("")                      == false);
	REQUIRE(isGeneratedFile("_sirenabc.wav")         == false); // only 5 letters after _siren
	REQUIRE(isGeneratedFile("_sirenABC.wav")          == false); // uppercase in random part
	REQUIRE(isGeneratedFile("_sirenabcdefgh.wav")      == false); // 8 letters (must be exactly 6)
	REQUIRE(isGeneratedFile("kick_siren.wav")          == false); // missing 6 letters
	REQUIRE(isGeneratedFile("kick_sirenabcdef.wav")   == true);  // valid: _siren at position size-16
	REQUIRE(isGeneratedFile(".wav")                    == false);
}

// rootPath() returns the path passed to the constructor.
TEST_CASE("FileSystemDataSource: rootPath returns configured root", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(src.rootPath() == tmp.str());
}

// ─── loadChildrenSync: generated-file filtering ────────────────────────────
// files matching the _siren pattern are excluded from directory listings.
TEST_CASE("loadChildrenSync: excludes _siren files from results", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	tmp.touch("kick_sirenabcdef.wav");
	tmp.touch("pad.flac");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync("");

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "kick.wav")             != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "pad.flac")             != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "kick_sirenabcdef.wav") == names.end());
}

// valid generated files are filtered; count reflects only visible files.
TEST_CASE("loadChildrenSync: correct count when multiple _siren files are present", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("a.wav");
	tmp.touch("a_sirenabcdef.wav");
	tmp.touch("b.mp3");
	tmp.touch("b_sirenuvwxyz.wav");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync("");

	REQUIRE(nodes.size() == 2);
	for (const auto& n : nodes)
		REQUIRE(isGeneratedFile(n.name) == false);
}

// directories are returned alongside audio files with no interference.
TEST_CASE("loadChildrenSync: container (directory) alongside _siren files is unaffected", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("sample.flac");
	tmp.touch("sample_sirenabcdef.wav");
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "Drums");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync("");

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "Drums")                 != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample.flac")         != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample_sirenabcdef.wav") == names.end());
}

// isContainer flag is true for directories, false for files.
TEST_CASE("loadChildrenSync: node isContainer flag is correct", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "Loops");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync("");

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
// IDs are relative paths; the returned absolute path is what the rack uses for drop.

// when convertToWav is false, the returned path is the resolved absolute path.
TEST_CASE("prepareForDrop: returns absolute path when convertToWav is false", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(callPrepareForDrop(src, "/drone.mp3", false) == tmp.filePath("drone.mp3"));
}

// .wav files pass through without conversion regardless of the flag.
TEST_CASE("prepareForDrop: returns absolute path for .wav files even when flag is true", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(callPrepareForDrop(src, "/kick.wav", true) == tmp.filePath("kick.wav"));
}

// uppercase .WAV is also recognised and passed through unchanged.
TEST_CASE("prepareForDrop: .WAV extension (uppercase) also treated as wav — no conversion", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(callPrepareForDrop(src, "/sample.WAV", true) == tmp.filePath("sample.WAV"));
}

// non-audio ids that can't be decoded fall back to the resolved absolute path.
TEST_CASE("prepareForDrop: returns existing _siren file without decoding (idempotent)", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(callPrepareForDrop(src, "/ghost.flac", true) == tmp.filePath("ghost.flac"));
}

// decode failure (e.g. non-existent file) falls back to the resolved absolute path.
TEST_CASE("prepareForDrop: falls back to absolute path when source cannot be decoded", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(callPrepareForDrop(src, "/ghost.flac", true) == tmp.filePath("ghost.flac"));
}

// decode failure for non-existent .mp3 also falls back to absolute path.
TEST_CASE("prepareForDrop: non-existent .mp3 with flag true also falls back", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(callPrepareForDrop(src, "/missing.mp3", true) == tmp.filePath("missing.mp3"));
}

// The new resampleQuality parameter must be accepted and not break the early-return
// path. When targetSampleRate is 0, prepareForDrop short-circuits with the absolute path
// regardless of quality, and the returned lambda must still be a valid (non-null) call.
TEST_CASE("prepareForDrop: resampleQuality parameter is accepted (no-op when no resample requested)", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	FileSystemDataSource src(tmp.str());

	for (int q : { 0, 1, 4, 7, 10 }) {
		// targetSampleRate=0 → no resample, no convert, no trim → identity lambda returning abs path.
		auto task = src.prepareForDrop("/kick.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
		                               /*trimIn=*/0.f, /*trimOut=*/1.f, q);
		REQUIRE(task != nullptr);
		REQUIRE(task() == tmp.filePath("kick.wav"));
	}
}

// ─── isSupportedAudioFile ───────────────────────────────────────────────────
// accepts .wav/.flac/.mp3 in any case; rejects everything else.
TEST_CASE("isSupportedAudioFile: recognises wav/flac/mp3 (any case), rejects everything else", "[Siren][FileSystem]") {
	REQUIRE(isSupportedAudioFile("/path/to/kick.wav")   == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.WAV")   == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.flac")  == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.FLAC")  == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.mp3")   == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.MP3")   == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.ogg")   == false);
	REQUIRE(isSupportedAudioFile("/path/to/kick.aac")   == false);
	REQUIRE(isSupportedAudioFile("/path/to/kick.m4a")   == false);
	REQUIRE(isSupportedAudioFile("/path/to/kick")       == false);
	REQUIRE(isSupportedAudioFile("")                     == false);
	REQUIRE(isSupportedAudioFile("/path/to/.wav")       == false);
	REQUIRE(isSupportedAudioFile("/path/to/no-ext")     == false);
}

// isSupportedFile delegates to isSupportedAudioFile on the path.
TEST_CASE("FileSystemDataSource: isSupportedFile delegates to isSupportedAudioFile", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(src.isSupportedFile("/path/to/kick.wav") == true);
	REQUIRE(src.isSupportedFile("/path/to/kick.mp3") == true);
	REQUIRE(src.isSupportedFile("/path/to/kick.ogg") == false);
}

// getDisplayName extracts just the filename component from a relative id.
TEST_CASE("FileSystemDataSource: getDisplayName returns filename only", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(src.getDisplayName("/samples/kick.wav") == "kick.wav");
	REQUIRE(src.getDisplayName("/samples/drum loop.flac") == "drum loop.flac");
}

// getRelativePath is identity: ids are already relative paths.
TEST_CASE("FileSystemDataSource: getRelativePath is identity for relative ids", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str());
	REQUIRE(src.getRelativePath("/kick.wav") == "/kick.wav");
	REQUIRE(src.getRelativePath("/sub/loop.flac") == "/sub/loop.flac");
}

// directories sort before files; both groups sort case-insensitively.
TEST_CASE("loadChildrenSync: directories appear before audio files (case-insensitive sort)", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("Zebra.wav");
	tmp.touch("alpha.mp3");
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "Beta");
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "alpha");

	FileSystemDataSource src(tmp.str());
	auto nodes = src.loadChildrenSync("");

	REQUIRE(nodes.size() == 4);
	REQUIRE(nodes[0].isContainer == true);
	REQUIRE(nodes[0].name == "alpha");
	REQUIRE(nodes[1].isContainer == true);
	REQUIRE(nodes[1].name == "Beta");
	REQUIRE(nodes[2].isContainer == false);
	REQUIRE(nodes[2].name == "alpha.mp3");
	REQUIRE(nodes[3].isContainer == false);
	REQUIRE(nodes[3].name == "Zebra.wav");
}

// ─── randomFileSuffix ───────────────────────────────────────────────────────
// returns '_siren' + exactly 6 lowercase ASCII letters; integrates with isGeneratedFile.
TEST_CASE("randomFileSuffix: format is '_siren' + 6 lowercase letters and works with isGeneratedFile", "[Siren][FileSystem]") {
	auto suffix = randomFileSuffix();
	REQUIRE(suffix.size() == 12);
	REQUIRE(suffix.substr(0, 6) == "_siren");
	for (size_t i = 6; i < suffix.size(); i++)
		REQUIRE(std::islower(suffix[i]) != 0);

	std::string filename = "kick" + suffix + ".wav";
	REQUIRE(isGeneratedFile(filename) == true);
}

// metadataFilePath is derived from rootPath and is stable across instances on the same root.
TEST_CASE("FileSystemDataSource: metadataFilePath is stable for same root", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src1(tmp.str());
	FileSystemDataSource src2(tmp.str());
	REQUIRE(src1.metadataFilePath() == src2.metadataFilePath());
}
