#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "../../test/test_mock.hpp"
#include "SirenFileSystem.hpp"
#include "Siren.test.hpp"
#include <fstream>
#include <algorithm>
#include <sstream>

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Siren;
using namespace StoermelderPackOne::Siren::filesystem;

Test::TestContext<> testContext;

// prepareForDrop() returns a task lambda; calling it executes the (possibly heavy)
// work synchronously. In tests this is fine — no real conversion is attempted since
// the source files don't exist or are already converted.
static std::string callPrepareForDrop(FileSystemDataSource& src, const std::string& id, bool convertToWav) {
	return src.prepareForDrop(id, convertToWav)();
}


// ─── isGeneratedFile ──────────────────────────────────────────────────────────
// pattern: _siren_ + exactly 6 lowercase letters + .wav suffix, must be at position size-17.
TEST_CASE("isGeneratedFile: recognises _siren_+6letters.wav pattern", "[Siren][FileSystem]") {
	REQUIRE(isGeneratedFile("_siren_abcdef.wav") == true);
	REQUIRE(isGeneratedFile("kick_siren_abcdef.wav") == true);
	REQUIRE(isGeneratedFile("pad.with.dots_siren_uvwxyz.wav") == true);
}

// regular audio files are not flagged as generated.
TEST_CASE("isGeneratedFile: regular audio files are not generated", "[Siren][FileSystem]") {
	REQUIRE(isGeneratedFile("kick.wav") == false);
	REQUIRE(isGeneratedFile("pad.flac") == false);
	REQUIRE(isGeneratedFile("bass.mp3") == false);
	REQUIRE(isGeneratedFile("bass.WAV") == false);
}

// old .converted.wav naming and various invalid patterns are rejected.
TEST_CASE("isGeneratedFile: rejects old .converted.wav and other edge cases", "[Siren][FileSystem]") {
	REQUIRE(isGeneratedFile("kick.converted.wav") == false);
	REQUIRE(isGeneratedFile("sample.converted.wav") == false);
	REQUIRE(isGeneratedFile("") == false);
	REQUIRE(isGeneratedFile("_siren_abc.wav") == false); // only 5 letters after _siren_
	REQUIRE(isGeneratedFile("_siren_ABC.wav") == false); // uppercase in random part
	REQUIRE(isGeneratedFile("_siren_abcdefgh.wav") == false); // 8 letters (must be exactly 6)
	REQUIRE(isGeneratedFile("kick_siren_.wav") == false); // missing 6 letters
	REQUIRE(isGeneratedFile("kick_siren_abcdef.wav") == true);  // valid: _siren_ at position size-17
	REQUIRE(isGeneratedFile(".wav") == false);
}


// ─── loadChildrenSync: generated-file filtering ────────────────────────────
// files matching the _siren_ pattern are excluded from directory listings.
// The directory tree is scripted in the virtual filesystem mock — no real files.
TEST_CASE("loadChildrenSync: excludes _siren_ files from results", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/kick.wav");
	mock.fs.touch(VFS_ROOT + "/kick_siren_abcdef.wav");
	mock.fs.touch(VFS_ROOT + "/pad.flac");

	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	// withAudioInfo=false: these tests only care about names/isContainer, so the
	// listing never probes the (virtual) files for audio headers.
	auto nodes = src.loadChildrenSync("", /*withAudioInfo=*/false);

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "kick.wav") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "pad.flac") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "kick_siren_abcdef.wav") == names.end());
}

// valid generated files are filtered; count reflects only visible files.
TEST_CASE("loadChildrenSync: correct count when multiple _siren_ files are present", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/a.wav");
	mock.fs.touch(VFS_ROOT + "/a_siren_abcdef.wav");
	mock.fs.touch(VFS_ROOT + "/b.mp3");
	mock.fs.touch(VFS_ROOT + "/b_siren_uvwxyz.wav");

	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	auto nodes = src.loadChildrenSync("", /*withAudioInfo=*/false);

	REQUIRE(nodes.size() == 2);
	for (const auto& n : nodes) {
		REQUIRE(isGeneratedFile(n.name) == false);
	}
}

// directories are returned alongside audio files with no interference.
TEST_CASE("loadChildrenSync: container (directory) alongside _siren_ files is unaffected", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/sample.flac");
	mock.fs.touch(VFS_ROOT + "/sample_siren_abcdef.wav");
	mock.fs.mkdir(VFS_ROOT + "/Drums");

	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	auto nodes = src.loadChildrenSync("", /*withAudioInfo=*/false);

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);

	REQUIRE(std::find(names.begin(), names.end(), "Drums") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample.flac") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "sample_siren_abcdef.wav") == names.end());
}

// isContainer flag is true for directories, false for files.
TEST_CASE("loadChildrenSync: node isContainer flag is correct", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/kick.wav");
	mock.fs.mkdir(VFS_ROOT + "/Loops");

	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	auto nodes = src.loadChildrenSync("", /*withAudioInfo=*/false);

	const DataSourceNode* fileNode = nullptr;
	const DataSourceNode* containerNode = nullptr;
	for (const auto& n : nodes) {
		if (n.name == "kick.wav") fileNode = &n;
		if (n.name == "Loops") containerNode = &n;
	}

	REQUIRE(fileNode != nullptr);
	REQUIRE(containerNode != nullptr);
	REQUIRE(fileNode->isContainer == false);
	REQUIRE(containerNode->isContainer == true);
}

// ─── prepareForDrop ──────────────────────────────────────────────────────────
// IDs are relative paths; the returned absolute path is what the rack uses for drop.

// when convertToWav is false, the returned path is the resolved absolute path.
TEST_CASE("prepareForDrop: returns absolute path when convertToWav is false", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/drone.mp3", false) == VFS_ROOT + "/drone.mp3");
}

// .wav files pass through without conversion regardless of the flag.
TEST_CASE("prepareForDrop: returns absolute path for .wav files even when flag is true", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/kick.wav", true) == VFS_ROOT + "/kick.wav");
}

// uppercase .WAV is also recognised and passed through unchanged.
TEST_CASE("prepareForDrop: .WAV extension (uppercase) also treated as wav — no conversion", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/sample.WAV", true) == VFS_ROOT + "/sample.WAV");
}

// A _siren_-named WAV file passed with convertToWav=true is returned unchanged:
// the .wav extension means needConvert=false, and the early-return path fires
// without any decode attempt — so previously-generated files are never re-encoded.
TEST_CASE("prepareForDrop: generated _siren_ WAV file is returned without re-conversion", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/kick_siren_abcdef.wav", true) == VFS_ROOT + "/kick_siren_abcdef.wav");
}

// decode failure (e.g. non-existent file) falls back to the resolved absolute path.
TEST_CASE("prepareForDrop: falls back to absolute path when source cannot be decoded", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/ghost.flac", true) == VFS_ROOT + "/ghost.flac");
}

// decode failure for non-existent .mp3 also falls back to absolute path.
TEST_CASE("prepareForDrop: non-existent .mp3 with flag true also falls back", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(callPrepareForDrop(src, "/missing.mp3", true) == VFS_ROOT + "/missing.mp3");
}

// The new resampleQuality parameter must be accepted and not break the early-return
// path. When targetSampleRate is 0, prepareForDrop short-circuits with the absolute path
// regardless of quality, and the returned lambda must still be a valid (non-null) call.
TEST_CASE("prepareForDrop: resampleQuality parameter is accepted (no-op when no resample requested)", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/kick.wav");
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());

	for (int q : { 0, 1, 4, 7, 10 }) {
		// targetSampleRate=0 → no resample, no convert, no trim → identity lambda returning abs path.
		auto task = src.prepareForDrop("/kick.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
			/*trimIn=*/0.f, /*trimOut=*/1.f, q);
		REQUIRE(task != nullptr);
		REQUIRE(task() == VFS_ROOT + "/kick.wav");
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
	REQUIRE(rack::system::getDirectory(result) == outDir.str());
	REQUIRE(rack::system::exists(result));
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
	REQUIRE(rack::system::getDirectory(result) == tmp.str());
	REQUIRE(rack::system::exists(result));
}

// outputDir must be ignored on the early-return (no-op) path — the resolved
// absolute source path is returned unchanged regardless of the destination hint.
TEST_CASE("prepareForDrop: outputDir is ignored when no conversion/trim/resample is needed", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/kick.wav");

	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	auto task = src.prepareForDrop("/kick.wav", /*convertToWav=*/false, /*targetSampleRate=*/0,
		/*trimIn=*/0.f, /*trimOut=*/1.f, /*resampleQuality=*/6, "/vfs/out");
	REQUIRE(task() == VFS_ROOT + "/kick.wav");
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
	REQUIRE(rack::system::getDirectory(result) == outDir.str());
	REQUIRE(rack::system::exists(result));

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
	REQUIRE(rack::system::getDirectory(result) == outDir.str());
	REQUIRE(rack::system::exists(result));
}

// copyFileForDrop is the binary-copy helper used by the alwaysCopy path. A
// missing source file should fall back to the source path so the rest of the
// drop pipeline can decide what to do.
TEST_CASE("copyFileForDrop: missing source returns the source path", "[Siren][FileSystem][vcv]") {
	Mock mock;
	std::string result = FileSystemDataSource::copyFileForDrop("/vfs/does/not/exist.wav",
		"/vfs/out.wav");
	REQUIRE(result == "/vfs/does/not/exist.wav");
	REQUIRE(!mock.fs.exists("/vfs/out.wav"));
}

// ─── isSupportedAudioFile ───────────────────────────────────────────────────
// accepts .wav/.flac/.mp3 in any case; rejects everything else.
TEST_CASE("isSupportedAudioFile: recognises wav/flac/mp3 (any case), rejects everything else", "[Siren][FileSystem]") {
	REQUIRE(isSupportedAudioFile("/path/to/kick.wav") == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.WAV") == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.flac") == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.FLAC") == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.mp3") == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.MP3") == true);
	REQUIRE(isSupportedAudioFile("/path/to/kick.ogg") == false);
	REQUIRE(isSupportedAudioFile("/path/to/kick.aac") == false);
	REQUIRE(isSupportedAudioFile("/path/to/kick.m4a") == false);
	REQUIRE(isSupportedAudioFile("/path/to/kick") == false);
	REQUIRE(isSupportedAudioFile("") == false);
	REQUIRE(isSupportedAudioFile("/path/to/.wav") == false);
	REQUIRE(isSupportedAudioFile("/path/to/no-ext") == false);
}

// isSupportedFile delegates to isSupportedAudioFile on the path.
TEST_CASE("FileSystemDataSource: isSupportedFile delegates to isSupportedAudioFile", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(src.isSupportedFile("/path/to/kick.wav") == true);
	REQUIRE(src.isSupportedFile("/path/to/kick.mp3") == true);
	REQUIRE(src.isSupportedFile("/path/to/kick.ogg") == false);
}

// getDisplayName extracts just the filename component from a relative id.
TEST_CASE("FileSystemDataSource: getDisplayName returns filename only", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(src.getDisplayName("/samples/kick.wav") == "kick.wav");
	REQUIRE(src.getDisplayName("/samples/drum loop.flac") == "drum loop.flac");
}

// getRelativePath is identity: ids are already relative paths.
TEST_CASE("FileSystemDataSource: getRelativePath is identity for relative ids", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	REQUIRE(src.getRelativePath("/kick.wav") == "/kick.wav");
	REQUIRE(src.getRelativePath("/sub/loop.flac") == "/sub/loop.flac");
}

// directories sort before files; both groups sort case-insensitively.
TEST_CASE("loadChildrenSync: directories appear before audio files (case-insensitive sort)", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/Zebra.wav");
	mock.fs.touch(VFS_ROOT + "/alpha.mp3");
	mock.fs.mkdir(VFS_ROOT + "/Beta");
	mock.fs.mkdir(VFS_ROOT + "/alpha");

	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	auto nodes = src.loadChildrenSync("", /*withAudioInfo=*/false);

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
	for (size_t i = 7; i < suffix.size(); i++) {
		REQUIRE(std::islower(suffix[i]) != 0);
	}

	std::string filename = "kick" + suffix + ".wav";
	REQUIRE(isGeneratedFile(filename) == true);
}

// MetadataStore::filePath is deterministic: two independent instances with the same root
// produce the same file path, so metadata is shared rather than duplicated.
TEST_CASE("FileSystemDataSource: metadata file path is deterministic for same root", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src1(VFS_ROOT, scratchMetadataStore());
	FileSystemDataSource src2(VFS_ROOT, scratchMetadataStore());
	REQUIRE(src1.getMetadata()->filePath() == src2.getMetadata()->filePath());
}

// ─── buildRepitchPreview / buildLoopPreview: multi-channel support ───────────
// Regression test for the same bug fixed in the fill thread: readF32() always
// interleaves the decoder's real channel count, but these functions allocated
// their read buffer sized for a stereo-clamped channel count, overflowing it
// for files with more than 2 channels (e.g. 5.1 surround). The fix keeps the
// full channel count all the way through — these functions no longer clamp
// to stereo at all; downmixing (if any) happens only at the fill-thread
// playback stage, not here.

// semitones = 0 is a no-op in applyRepitch, so the result reflects the raw
// read exactly — letting this test assert precise sample values per channel.
TEST_CASE("buildRepitchPreview: multi-channel file keeps its full channel count", "[Siren][Audio][Repitch]") {
	TempDir tmp;
	const int frames = 4410;
	writeMultichannelTestWav(tmp.filePath("surround.wav"), frames, 44100, 6, 0.6f, -0.3f, 0.15f);

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	AudioPreviewResult result = buildRepitchPreview(src, "/surround.wav", 0.f, 1.f, 0.f);

	REQUIRE(result.ok == true);
	REQUIRE(result.channels == 6);
	REQUIRE(!result.samples.empty());
	for (size_t f = 0; f < result.samples.size() / 6; f++) {
		REQUIRE(result.samples[f * 6 + 0] == Catch::Approx(0.6f));
		REQUIRE(result.samples[f * 6 + 1] == Catch::Approx(-0.3f));
		for (int c = 2; c < 6; c++) {
			REQUIRE(result.samples[f * 6 + c] == Catch::Approx(0.15f));
		}
	}
}

// An actual pitch shift on a >2-channel file must complete without crashing
// and preserve the full channel count.
TEST_CASE("buildRepitchPreview: multi-channel file with actual pitch shift does not crash", "[Siren][Audio][Repitch]") {
	TempDir tmp;
	writeMultichannelTestWav(tmp.filePath("surround.wav"), 4410, 44100, 8, 0.5f, 0.2f, 0.1f);

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	AudioPreviewResult result = buildRepitchPreview(src, "/surround.wav", 0.f, 1.f, 5.f);

	REQUIRE(result.ok == true);
	REQUIRE(result.channels == 8);
	REQUIRE(!result.samples.empty());
}

// A mono file is unaffected — regression guard against accidentally forcing
// a minimum channel count.
TEST_CASE("buildRepitchPreview: mono file is read correctly", "[Siren][Audio][Repitch]") {
	TempDir tmp;
	writeMultichannelTestWav(tmp.filePath("mono.wav"), 4410, 44100, 1, 0.42f, 0.f, 0.f);

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	AudioPreviewResult result = buildRepitchPreview(src, "/mono.wav", 0.f, 1.f, 0.f);

	REQUIRE(result.ok == true);
	REQUIRE(result.channels == 1);
	REQUIRE(!result.samples.empty());
	for (float s : result.samples) {
		REQUIRE(s == Catch::Approx(0.42f));
	}
}

TEST_CASE("buildLoopPreview: multi-channel file keeps its full channel count without crashing", "[Siren][Audio][Loop]") {
	TempDir tmp;
	writeMultichannelTestWav(tmp.filePath("surround.wav"), 8820, 44100, 6, 0.4f, -0.2f, 0.1f);

	FileSystemDataSource src(tmp.str(), scratchMetadataStore());
	AudioPreviewResult result = buildLoopPreview(src, "/surround.wav", 0.f, 1.f, 0.05f);

	REQUIRE(result.ok == true);
	REQUIRE(result.channels == 6);
	REQUIRE(!result.samples.empty());
	// The loop-crossfade rotation/blend only ever mixes ch0/ch1 (0.4 / -0.2)
	// with the same-index channel across the splice, and channels 2-5 (0.1)
	// with themselves — so with the equal-power crossfade's worst-case
	// sqrt(2) gain, no output sample can exceed ~0.6. A misaligned/overflowing
	// read would instead blend unrelated channels together and break this bound.
	for (float s : result.samples) {
		REQUIRE(std::abs(s) <= 0.6f);
	}
}

// ─── SystemAccess routing ─────────────────────────────────────────────────────
// cleanup() used to be gated by isTesting() (a no-op in the test harness). With
// the SystemAccess mock installed, the real logic runs and the mock records the
// cache-file removals instead of touching disk.

TEST_CASE("FileSystemDataSource: cleanup removes cache files through SystemAccess", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());

	// Populate the metadata so cleanup() has cache files to remove.
	src.getMetadata()->setAudioInfo("/kick.wav", 1.0f, 44100, 32, 2, 12345);
	src.getMetadata()->setAudioInfo("/pad.flac", 2.0f, 44100, 16, 2, 67890);

	src.cleanup();

	REQUIRE(mock.fs.removed.size() == 2);
	REQUIRE(mock.fs.removed[0] == src.cacheFilePathFor("/kick.wav"));
	REQUIRE(mock.fs.removed[1] == src.cacheFilePathFor("/pad.flac"));
}

// cleanup() with no metadata entries is a no-op — nothing is removed.
TEST_CASE("FileSystemDataSource: cleanup with no samples removes nothing", "[Siren][FileSystem][vcv]") {
	Mock mock;
	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());

	src.cleanup();

	REQUIRE(mock.fs.removed.empty());
}

// The directory scan in loadChildrenSync goes through the SystemAccess layer.
TEST_CASE("FileSystemDataSource: loadChildrenSync routes getEntries through SystemAccess", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.fs.touch(VFS_ROOT + "/kick.wav");
	mock.fs.touch(VFS_ROOT + "/pad.flac");
	mock.fs.mkdir(VFS_ROOT + "/Drums");

	FileSystemDataSource src(VFS_ROOT, scratchMetadataStore());
	auto nodes = src.loadChildrenSync("", /*withAudioInfo=*/false);

	REQUIRE(mock.fs.getEntriesCalls.size() == 1);
	REQUIRE(mock.fs.getEntriesCalls[0] == VFS_ROOT);

	std::vector<std::string> names;
	for (const auto& n : nodes) names.push_back(n.name);
	REQUIRE(std::find(names.begin(), names.end(), "kick.wav") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "pad.flac") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "Drums") != names.end());
}

// ─── createNewRootContainer ───────────────────────────────────────────────────
// The folder picker routes through the vcv::ui layer; the mock scripts the answer.
// A cancelled dialog (empty string) returns false and leaves `out` untouched.

TEST_CASE("createNewRootContainer: cancelled dialog returns false", "[Siren][FileSystem][vcv]") {
	Mock mock;
	RootContainer rc;
	REQUIRE(createNewRootContainer(rc) == false);
}

// A picked folder is turned into a RootContainer with the name derived from the path.
TEST_CASE("createNewRootContainer: selected folder builds a RootContainer", "[Siren][FileSystem][vcv]") {
	Mock mock;
	mock.ui.dirResults.push_back("/vfs/root");

	RootContainer rc;
	REQUIRE(createNewRootContainer(rc) == true);
	REQUIRE(rc.path == "/vfs/root");
	REQUIRE(rc.type == "fs");
	REQUIRE(rc.name == "root");
}
