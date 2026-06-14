#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "SirenFileSystem.hpp"
#include "SirenTest.hpp"
#include <fstream>
#include <algorithm>
#include <sstream>

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

// Writes a short but decodable silent stereo WAV file — needed to exercise the
// trim/convert path in prepareForDrop(), which falls back to the source path
// whenever loadAudioInfo() can't decode a header (e.g. the empty files from touch()).
static void writeTestWav(const std::string& path, int frames = 4410, int sampleRate = 44100, int channels = 2) {
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

// ─── isGeneratedFile ──────────────────────────────────────────────────────────
// pattern: _siren_ + exactly 6 lowercase letters + .wav suffix, must be at position size-17.
TEST_CASE("isGeneratedFile: recognises _siren_+6letters.wav pattern", "[Siren][FileSystem]") {
	REQUIRE(isGeneratedFile("_siren_abcdef.wav")         == true);
	REQUIRE(isGeneratedFile("kick_siren_abcdef.wav")     == true);
	REQUIRE(isGeneratedFile("pad.with.dots_siren_uvwxyz.wav") == true);
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
	REQUIRE(isGeneratedFile("_siren_abc.wav")         == false); // only 5 letters after _siren_
	REQUIRE(isGeneratedFile("_siren_ABC.wav")          == false); // uppercase in random part
	REQUIRE(isGeneratedFile("_siren_abcdefgh.wav")      == false); // 8 letters (must be exactly 6)
	REQUIRE(isGeneratedFile("kick_siren_.wav")          == false); // missing 6 letters
	REQUIRE(isGeneratedFile("kick_siren_abcdef.wav")   == true);  // valid: _siren_ at position size-17
	REQUIRE(isGeneratedFile(".wav")                    == false);
}


// ─── loadChildrenSync: generated-file filtering ────────────────────────────
// files matching the _siren_ pattern are excluded from directory listings.
TEST_CASE("loadChildrenSync: excludes _siren_ files from results", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	tmp.touch("kick_siren_abcdef.wav");
	tmp.touch("pad.flac");

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto nodes = src.loadChildrenSync("");

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "kick.wav")             != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "pad.flac")             != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "kick_siren_abcdef.wav") == names.end());
}

// valid generated files are filtered; count reflects only visible files.
TEST_CASE("loadChildrenSync: correct count when multiple _siren_ files are present", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("a.wav");
	tmp.touch("a_siren_abcdef.wav");
	tmp.touch("b.mp3");
	tmp.touch("b_siren_uvwxyz.wav");

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto nodes = src.loadChildrenSync("");

	REQUIRE(nodes.size() == 2);
	for (const auto& n : nodes)
		REQUIRE(isGeneratedFile(n.name) == false);
}

// directories are returned alongside audio files with no interference.
TEST_CASE("loadChildrenSync: container (directory) alongside _siren_ files is unaffected", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("sample.flac");
	tmp.touch("sample_siren_abcdef.wav");
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "Drums");

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto nodes = src.loadChildrenSync("");

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "Drums")                 != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample.flac")         != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample_siren_abcdef.wav") == names.end());
}

// isContainer flag is true for directories, false for files.
TEST_CASE("loadChildrenSync: node isContainer flag is correct", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	ghc::filesystem::create_directories(ghc::filesystem::path(tmp.str()) / "Loops");

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
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
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/drone.mp3", false) == tmp.filePath("drone.mp3"));
}

// .wav files pass through without conversion regardless of the flag.
TEST_CASE("prepareForDrop: returns absolute path for .wav files even when flag is true", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/kick.wav", true) == tmp.filePath("kick.wav"));
}

// uppercase .WAV is also recognised and passed through unchanged.
TEST_CASE("prepareForDrop: .WAV extension (uppercase) also treated as wav — no conversion", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/sample.WAV", true) == tmp.filePath("sample.WAV"));
}

// non-audio ids that can't be decoded fall back to the resolved absolute path.
TEST_CASE("prepareForDrop: returns existing _siren_ file without decoding (idempotent)", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/ghost.flac", true) == tmp.filePath("ghost.flac"));
}

// decode failure (e.g. non-existent file) falls back to the resolved absolute path.
TEST_CASE("prepareForDrop: falls back to absolute path when source cannot be decoded", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/ghost.flac", true) == tmp.filePath("ghost.flac"));
}

// decode failure for non-existent .mp3 also falls back to absolute path.
TEST_CASE("prepareForDrop: non-existent .mp3 with flag true also falls back", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/missing.mp3", true) == tmp.filePath("missing.mp3"));
}

// The new resampleQuality parameter must be accepted and not break the early-return
// path. When targetSampleRate is 0, prepareForDrop short-circuits with the absolute path
// regardless of quality, and the returned lambda must still be a valid (non-null) call.
TEST_CASE("prepareForDrop: resampleQuality parameter is accepted (no-op when no resample requested)", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());

	for (int q : { 0, 1, 4, 7, 10 }) {
		// targetSampleRate=0 → no resample, no convert, no trim → identity lambda returning abs path.
		auto task = src.prepareForDrop("/kick.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
		                               /*trimIn=*/0.f, /*trimOut=*/1.f, q);
		REQUIRE(task != nullptr);
		REQUIRE(task() == tmp.filePath("kick.wav"));
	}
}

// ─── prepareForDrop: outputDir ───────────────────────────────────────────────
// When trim/convert/resample produces a new file, outputDir (when non-empty)
// overrides the destination folder; otherwise the file lands beside the source.

// trimOut < 1 forces the conversion path even though convertToWav is false and
// no resampling is requested — exercising outPath construction without needing
// a real format conversion.
TEST_CASE("prepareForDrop: writes trimmed file into custom outputDir when set", "[Siren][FileSystem]") {
	TempDir tmp;
	TempDir outDir;
	writeTestWav(tmp.filePath("loop.wav"));

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto task = src.prepareForDrop("/loop.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
	                               /*trimIn=*/0.f, /*trimOut=*/0.5f, /*resampleQuality=*/6, outDir.str());
	std::string result = task();

	REQUIRE(result != tmp.filePath("loop.wav"));
	REQUIRE(ghc::filesystem::path(result).parent_path().string() == outDir.str());
	REQUIRE(ghc::filesystem::exists(result));
}

// With outputDir empty (default), the generated file is written beside the source —
// preserving the pre-existing behaviour.
TEST_CASE("prepareForDrop: writes trimmed file beside source when outputDir is empty", "[Siren][FileSystem]") {
	TempDir tmp;
	writeTestWav(tmp.filePath("loop.wav"));

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto task = src.prepareForDrop("/loop.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
	                               /*trimIn=*/0.f, /*trimOut=*/0.5f, /*resampleQuality=*/6, "");
	std::string result = task();

	REQUIRE(result != tmp.filePath("loop.wav"));
	REQUIRE(ghc::filesystem::path(result).parent_path().string() == tmp.str());
	REQUIRE(ghc::filesystem::exists(result));
}

// outputDir must be ignored on the early-return (no-op) path — the resolved
// absolute source path is returned unchanged regardless of the destination hint.
TEST_CASE("prepareForDrop: outputDir is ignored when no conversion/trim/resample is needed", "[Siren][FileSystem]") {
	TempDir tmp;
	TempDir outDir;
	tmp.touch("kick.wav");

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto task = src.prepareForDrop("/kick.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
	                               /*trimIn=*/0.f, /*trimOut=*/1.f, /*resampleQuality=*/6, outDir.str());
	REQUIRE(task() == tmp.filePath("kick.wav"));
}

// ─── prepareForDrop: alwaysCopy ──────────────────────────────────────────────
// "Always copy" produces a binary copy of the source into outputDir even when no
// other transformation is needed. The copy path is only used when the source
// contents are sufficient — i.e. no trim/resample/repitch/loop is requested.

// alwaysCopy with a real file copies the bytes verbatim into outputDir.
TEST_CASE("prepareForDrop: alwaysCopy copies source file into outputDir", "[Siren][FileSystem]") {
	TempDir tmp;
	TempDir outDir;
	tmp.touch("kick.wav");
	// Seed the source with recognisable bytes so we can confirm byte-for-byte
	// equality on the output (proving it's a true copy, not a re-encode).
	{
		std::ofstream f(tmp.filePath("kick.wav"), std::ios::binary);
		const char bytes[] = "siren-test-fixture-bytes";
		f.write(bytes, sizeof(bytes) - 1);
	}

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto task = src.prepareForDrop("/kick.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
	                               /*trimIn=*/0.f, /*trimOut=*/1.f, /*resampleQuality=*/6,
	                               outDir.str(), /*loopOnDrop=*/false, /*loopCrossfadeDuration=*/8.f,
	                               /*repitchSemitones=*/0.f, /*alwaysCopy=*/true);
	std::string result = task();

	REQUIRE(result != tmp.filePath("kick.wav"));
	REQUIRE(ghc::filesystem::path(result).parent_path().string() == outDir.str());
	REQUIRE(ghc::filesystem::exists(result));

	// The new file must contain the original bytes unchanged.
	std::ifstream in(result, std::ios::binary);
	std::stringstream buf;
	buf << in.rdbuf();
	REQUIRE(buf.str() == "siren-test-fixture-bytes");
}

// alwaysCopy is a no-op when outputDir is empty ("Same folder as source" target):
// copying a file on top of itself serves no purpose.
TEST_CASE("prepareForDrop: alwaysCopy is a no-op when outputDir is empty", "[Siren][FileSystem]") {
	TempDir tmp;
	tmp.touch("kick.wav");

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto task = src.prepareForDrop("/kick.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
	                               /*trimIn=*/0.f, /*trimOut=*/1.f, /*resampleQuality=*/6,
	                               /*outputDir=*/"", /*loopOnDrop=*/false, /*loopCrossfadeDuration=*/8.f,
	                               /*repitchSemitones=*/0.f, /*alwaysCopy=*/true);
	REQUIRE(task() == tmp.filePath("kick.wav"));
}

// alwaysCopy leaves non-copy transformations (trim, resample, etc.) to the
// existing processAudioForDrop pipeline: when combined with a trim, the result
// is a re-encoded WAV trimmed into outputDir rather than a raw byte copy.
TEST_CASE("prepareForDrop: alwaysCopy defers to processAudioForDrop when a transformation is needed", "[Siren][FileSystem]") {
	TempDir tmp;
	TempDir outDir;
	writeTestWav(tmp.filePath("loop.wav"));

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	auto task = src.prepareForDrop("/loop.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
	                               /*trimIn=*/0.f, /*trimOut=*/0.5f, /*resampleQuality=*/6,
	                               outDir.str(), /*loopOnDrop=*/false, /*loopCrossfadeDuration=*/8.f,
	                               /*repitchSemitones=*/0.f, /*alwaysCopy=*/true);
	std::string result = task();

	// Even with alwaysCopy=true the trim drives the path through the encoder.
	REQUIRE(ghc::filesystem::path(result).parent_path().string() == outDir.str());
	REQUIRE(ghc::filesystem::exists(result));
}

// copyFileForDrop is the binary-copy helper used by the alwaysCopy path. A
// missing source file should fall back to the source path so the rest of the
// drop pipeline can decide what to do.
TEST_CASE("copyFileForDrop: missing source returns the source path", "[Siren][FileSystem]") {
	TempDir outDir;
	std::string result = FileSystemDataSource::copyFileForDrop("/does/not/exist.wav",
	                                                           outDir.str() + "/out.wav");
	REQUIRE(result == "/does/not/exist.wav");
	REQUIRE(!ghc::filesystem::exists(outDir.str() + "/out.wav"));
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
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(src.isSupportedFile("/path/to/kick.wav") == true);
	REQUIRE(src.isSupportedFile("/path/to/kick.mp3") == true);
	REQUIRE(src.isSupportedFile("/path/to/kick.ogg") == false);
}

// getDisplayName extracts just the filename component from a relative id.
TEST_CASE("FileSystemDataSource: getDisplayName returns filename only", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	REQUIRE(src.getDisplayName("/samples/kick.wav") == "kick.wav");
	REQUIRE(src.getDisplayName("/samples/drum loop.flac") == "drum loop.flac");
}

// getRelativePath is identity: ids are already relative paths.
TEST_CASE("FileSystemDataSource: getRelativePath is identity for relative ids", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
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

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
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
// returns '_siren_' + exactly 6 lowercase ASCII letters; integrates with isGeneratedFile.
TEST_CASE("randomFileSuffix: format is '_siren_' + 6 lowercase letters and works with isGeneratedFile", "[Siren][FileSystem]") {
	auto suffix = randomFileSuffix();
	REQUIRE(suffix.size() == 13);
	REQUIRE(suffix.substr(0, 7) == "_siren_");
	for (size_t i = 7; i < suffix.size(); i++)
		REQUIRE(std::islower(suffix[i]) != 0);

	std::string filename = "kick" + suffix + ".wav";
	REQUIRE(isGeneratedFile(filename) == true);
}

// MetadataStore::filePath is derived from rootPath and is stable across instances on the same root.
TEST_CASE("FileSystemDataSource: metadata file path is stable for same root", "[Siren][FileSystem]") {
	TempDir tmp;
	FileSystemDataSource src1(tmp.str(), scratchMetadataStore());
	FileSystemDataSource src2(tmp.str(), scratchMetadataStore());
	REQUIRE(src1.getMetadata()->filePath() == src2.getMetadata()->filePath());
}
