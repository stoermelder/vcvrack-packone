#pragma once
#include "SirenMetadata.hpp"
#include "SirenDataSource.hpp"
#include "../../test/test_mock.hpp"


namespace StoermelderPackOne {
namespace Siren {


// Writes a short but decodable silent stereo WAV file — needed to exercise the
// trim/convert path in prepareForDrop(), which falls back to the source path
// whenever loadAudioInfo() can't decode a header (e.g. the empty files from touch()).
static void writeTestWav(const std::string& path, int frames = 4410, int sampleRate = 44100, int channels = 2) {
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

// Writes a WAV where channel 0 and channel 1 each hold a distinct constant
// value and any further channels hold a third value — lets tests verify that
// only the first two channels survive a downmix.
static void writeMultichannelTestWav(const std::string& path, int frames, int sampleRate, int channels,
		float ch0, float ch1, float chRest) {
	drwav_data_format fmt = {};
	fmt.container = drwav_container_riff;
	fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
	fmt.channels = (drwav_uint32)channels;
	fmt.sampleRate = (drwav_uint32)sampleRate;
	fmt.bitsPerSample = 32;
	drwav wav;
	drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr);
	std::vector<float> samples((size_t)frames * channels);
	for (int f = 0; f < frames; f++) {
		samples[(size_t)f * channels + 0] = ch0;
		if (channels > 1) samples[(size_t)f * channels + 1] = ch1;
		for (int c = 2; c < channels; c++) samples[(size_t)f * channels + c] = chRest;
	}
	drwav_write_pcm_frames(&wav, (drwav_uint64)frames, samples.data());
	drwav_uninit(&wav);
}

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


// Virtual filesystem root
// The mock-based tests script a virtual tree under this root instead of creating
// real temp files. FileSystemDataSource is constructed with this as its root, and
// the mock's getEntries()/isDirectory()/... serve the listing from the in-memory
// map — no disk I/O happens at all.
static const std::string VFS_ROOT = "/vfs/root";


// MetadataStore subclass used by tests to redirect persistence to a scratch
// folder under the system's temp directory, so load()/save() — including the
// rename-write-verify-restore sequence — can be exercised without ever touching
// the user's actual settings folder. With the virtual-fs mocks installed the
// path resolves under /vfs; without them it falls through to the real temp dir.
struct ScratchMetadataStore : MetadataStore {
	std::string filePath() const override {
		return vcv::fs::join(vcv::fs::join(vcv::fs::getTempDirectory(), "siren_test_metadata"),
			"siren-" + hashPath(rootPath) + ".json");
	}
};

inline std::unique_ptr<MetadataStore> scratchMetadataStore() {
	return std::make_unique<ScratchMetadataStore>();
}

// Minimal in-memory DataSource mock for tests that only care about the
// DataSource/MetadataStore contract (e.g. "getMetadata returns a stable,
// mutable pointer") and have no business touching the filesystem at all —
// keeps such tests decoupled from FileSystemDataSource's I/O behavior.
struct TestDataSource : DataSource {
	std::string root;
	MetadataStore meta_;

	explicit TestDataSource(const std::string& rootPath) : root(rootPath) {
		meta_.rootPath = root;
	}

	std::string rootId() const override { return root; }
	bool isSupportedFile(const std::string& path) const override { return true; }

	void loadChildrenAsync(const std::string& id, TaskWorker& worker,
			std::function<void(std::vector<DataSourceNode>)> onDone) override {
		onDone(loadChildrenSync(id));
	}
	std::vector<DataSourceNode> loadChildrenSync(const std::string& id, bool withAudioInfo = true) override { return {}; }

	MetadataStore* getMetadata() override { return &meta_; }
};


// TempDir RAII helper
// Creates a unique temporary directory; removes it on destruction.
struct TempDir {
	std::string path;

	TempDir() {
		static int seq = 0;
		path = rack::system::join(rack::system::getTempDirectory(),
			"sirenfs_test_" + std::to_string(++seq));
		rack::system::createDirectories(path);
	}

	~TempDir() {
		rack::system::removeRecursively(path);
	}

	// Create an empty file with the given name inside this directory.
	void touch(const std::string& name) const {
		std::ofstream f(rack::system::join(path, name));
	}

	std::string filePath(const std::string& name) const {
		return rack::system::join(path, name);
	}

	std::string str() const {
		return path;
	}
};


// Virtual filesystem mocks
// Replaces the real disk with an in-memory file map + directory set, so tests can
// script directory listings and file operations without touching the filesystem.
// MockSystemAccess serves the vcv::fs layer (exists/rename/remove/...);
// MockFileAccess serves the vcv::fs layer (read/write). They SHARE one file map —
// the fs mock is pointed at the installed system mock by its rebind() hook — so
// MetadataStore::save()'s rename-write-verify-restore dance, which checks
// exists()/rename() through the system layer and writes/reads through the fs layer,
// sees a single consistent filesystem.
struct MockFileAccess : Test::MockVcv::MockFileAccess {
	// path → contents; a missing key means "no such file".
	std::map<std::string, std::string> files;
	// directories that exist (even if empty).
	std::set<std::string> dirs;

	// Recording of the operations the Siren code routes through the layer.
	std::vector<std::string> removed;
	std::vector<std::string> getEntriesCalls;

	// Create an empty file at `path`, recording its parent as a directory.
	void touch(const std::string& path) {
		files[path] = "";
		dirs.insert(rack::system::getDirectory(path));
	}

	// Create a directory (and its parents).
	void mkdir(const std::string& path) {
		dirs.insert(path);
	}

	bool read(const std::string& path, std::string& data) const override {
		auto it = files.find(path);
		if (it == files.end()) return false;
		data = it->second;
		return true;
	}
	bool write(const std::string& path, const std::string& data) override {
		files[path] = data;
		return true;
	}
	bool exists(const std::string& path) const override {
		return files.count(path) > 0;
	}

	bool exists(const std::string& path) override {
		return files.count(path) > 0 || dirs.count(path) > 0;
	}
	bool isFile(const std::string& path) override {
		return files.count(path) > 0;
	}
	bool isDirectory(const std::string& path) override {
		return dirs.count(path) > 0;
	}
	uint64_t getFileSize(const std::string& path) override {
		auto it = files.find(path);
		return it != files.end() ? it->second.size() : 0;
	}
	// Non-recursive listing only (the Siren code always passes depth 0).
	std::vector<std::string> getEntries(const std::string& dirPath, int depth) override {
		getEntriesCalls.push_back(dirPath);
		std::vector<std::string> result;
		std::string prefix = dirPath + "/";
		for (const auto& f : files) {
			if (f.first.compare(0, prefix.size(), prefix) == 0
				&& f.first.find('/', prefix.size()) == std::string::npos) {
				result.push_back(f.first);
			}
		}
		for (const auto& d : dirs) {
			if (d.compare(0, prefix.size(), prefix) == 0
				&& d.find('/', prefix.size()) == std::string::npos) {
				result.push_back(d);
			}
		}
		return result;
	}
	bool createDirectory(const std::string& path) override {
		dirs.insert(path);
		return true;
	}
	bool createDirectories(const std::string& path) override {
		dirs.insert(path);
		return true;
	}
	bool rename(const std::string& srcPath, const std::string& destPath) override {
		auto it = files.find(srcPath);
		if (it != files.end()) {
			files[destPath] = it->second;
			files.erase(it);
			return true;
		}
		if (dirs.count(srcPath) > 0) {
			dirs.erase(srcPath);
			dirs.insert(destPath);
			return true;
		}
		return false;
	}
	bool copy(const std::string& srcPath, const std::string& destPath) override {
		auto it = files.find(srcPath);
		if (it == files.end()) return false;
		files[destPath] = it->second;
		return true;
	}
	bool remove(const std::string& path) override {
		removed.push_back(path);
		files.erase(path);
		dirs.erase(path);
		return true;
	}
	int removeRecursively(const std::string& path) override {
		removed.push_back(path);
		std::string prefix = path + "/";
		for (auto it = files.begin(); it != files.end();) {
			if (it->first == path || it->first.compare(0, prefix.size(), prefix) == 0) it = files.erase(it);
			else ++it;
		}
		for (auto it = dirs.begin(); it != dirs.end();) {
			if (*it == path || it->compare(0, prefix.size(), prefix) == 0) it = dirs.erase(it);
			else ++it;
		}
		return 0;
	}
	std::string getTempDirectory() override {
		return "/vfs";
	}
};

// A UiAccess mock that records openDirectoryDialog calls and returns scripted
// answers. Installed by Mock so any UI dialog the Siren code touches is
// scripted instead of popping a real native dialog.
struct MockUiAccess : vcv::UiAccess {
	std::vector<std::string> dirResults;  // queue consumed in order
	int dirIndex = 0;

	std::string openDirDialog() override {
		if (dirIndex < (int) dirResults.size()) return dirResults[dirIndex++];
		return "";
	}
};

// Installs the shared-filesystem pair. MockFileAccess::rebind() re-points the fs mock
// at the installed system mock, so the two layers share one file map no matter where
// the instances were built.
struct Mock {
	MockFileAccess fs;
	MockUiAccess ui;
	Mock() {
		assert(StoermelderPackOne::vcv::fileAccess == nullptr && "Mock nested or leaked");
		StoermelderPackOne::vcv::fileAccess = &fs;
		StoermelderPackOne::vcv::uiAccess = &ui;
	}
	~Mock() {
		assert(StoermelderPackOne::vcv::fileAccess == &fs && "Mock nested or leaked");
		StoermelderPackOne::vcv::fileAccess = nullptr;
		StoermelderPackOne::vcv::uiAccess = nullptr;
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
