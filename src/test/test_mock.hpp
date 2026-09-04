#pragma once
#include "../vcv/api.hpp"
#include <rack.hpp>

namespace Test {

using namespace StoermelderPackOne;

namespace mock {

// Installs one mock into one vcv access slot for the duration of the scope, restoring the
// slot's previous value (nullptr if it wasn't already mocked) on destruction.
//
// Base is the access interface (vcv::UiAccess, vcv::FileAccess, ...); `slot` is the matching
// global pointer (vcv::uiAccess, vcv::fileAccess, ...). Compose one Guard per slot you need to
// mock into a per-suite Mock struct — see src/modules/siren/Siren.test.hpp for the pattern this
// replaces by hand, or the example below.
//
// Usage:
//   struct Mock {
//       MockUiAccess ui;
//       MockFileAccess fs;
//       Test::mock::Guard<vcv::UiAccess> uiGuard{vcv::uiAccess, &ui};
//       Test::mock::Guard<vcv::FileAccess> fsGuard{vcv::fileAccess, &fs};
//   };
template <typename Base>
struct Guard {
	Base*& slot;
	Base* prev;

	Guard(Base*& slot, Base* mock) : slot(slot), prev(slot) {
		slot = mock;
	}
	~Guard() {
		slot = prev;
	}

	Guard(const Guard&) = delete;
	Guard& operator=(const Guard&) = delete;
	Guard(Guard&&) = delete;
	Guard& operator=(Guard&&) = delete;
};

// Default FileAccess mock: forwards every call to the real rack::system, so a test that only
// cares about a few filesystem calls can inherit from this and override just those methods
// (e.g. to record or veto them) instead of re-implementing the whole interface.
struct MockFileAccess : StoermelderPackOne::vcv::FileAccess {
	std::string join(const std::string& path1, const std::string& path2) override { return rack::system::join(path1, path2); }
	std::string getDirectory(const std::string& path) override { return rack::system::getDirectory(path); }
	std::string getFilename(const std::string& path) override { return rack::system::getFilename(path); }
	std::string getStem(const std::string& path) override { return rack::system::getStem(path); }
	std::string getExtension(const std::string& path) override { return rack::system::getExtension(path); }
	std::vector<std::string> getEntries(const std::string& dirPath, int depth) override { return rack::system::getEntries(dirPath, depth); }
	bool exists(const std::string& path) const override { return rack::system::exists(path); }
	bool isFile(const std::string& path) override { return rack::system::isFile(path); }
	bool isDirectory(const std::string& path) override { return rack::system::isDirectory(path); }
	uint64_t getFileSize(const std::string& path) override { return rack::system::getFileSize(path); }
	bool rename(const std::string& srcPath, const std::string& destPath) override { return rack::system::rename(srcPath, destPath); }
	bool copy(const std::string& srcPath, const std::string& destPath) override { return rack::system::copy(srcPath, destPath); }
	bool createDirectory(const std::string& path) override { return rack::system::createDirectory(path); }
	bool createDirectories(const std::string& path) override { return rack::system::createDirectories(path); }
	bool remove(const std::string& path) override { return rack::system::remove(path); }
	int removeRecursively(const std::string& path) override { return rack::system::removeRecursively(path); }
	std::string getTempDirectory() override { return rack::system::getTempDirectory(); }
	std::string getUserDirectory(const std::string& path) override { return rack::asset::user(path); }
	double getTime() override { return rack::system::getTime(); }
	void openDirectory(const std::string& path) override { rack::system::openDirectory(path); }
};

} // namespace mock

// One macro per access slot: declares the mock member (named after the slot: modules, scene,
// cables, ui, fs, history, nw) of the given type, plus a Guard that installs it into the
// matching global for the enclosing struct's lifetime. Each macro hardcodes its own member
// name, Base type and global — there is no shared naming convention to infer the latter two
// from (e.g. the FileAccess global is `fileAccess`, not `fsAccess`), so this stays 7 explicit
// one-liners rather than a single macro with a lookup table.
//
// Usage — replaces the 2-line member+Guard pair per slot:
//   struct Mock {
//       TEST_MOCK_UI(MockUiAccess);
//       TEST_MOCK_FS(MockFileAccess);
//       TEST_MOCK_HISTORY(MockHistoryAccess);
//   } mock;   // mock.ui, mock.fs, mock.history
#define TEST_MOCK_MODULES(Type) Type modules; Test::mock::Guard<vcv::ModuleAccess> modulesGuard{vcv::moduleAccess, &modules}
#define TEST_MOCK_SCENE(Type)   Type scene; Test::mock::Guard<vcv::SceneAccess> sceneGuard{vcv::sceneAccess, &scene}
#define TEST_MOCK_CABLES(Type)  Type cables; Test::mock::Guard<vcv::CableAccess> cablesGuard{vcv::cableAccess, &cables}
#define TEST_MOCK_UI(Type)      Type ui; Test::mock::Guard<vcv::UiAccess> uiGuard{vcv::uiAccess, &ui}
#define TEST_MOCK_FS(Type)      Type fs; Test::mock::Guard<vcv::FileAccess> fsGuard{vcv::fileAccess, &fs}
#define TEST_MOCK_HISTORY(Type) Type history; Test::mock::Guard<vcv::HistoryAccess> historyGuard{vcv::historyAccess, &history}
#define TEST_MOCK_NW(Type)      Type nw; Test::mock::Guard<vcv::NwAccess> nwGuard{vcv::nwAccess, &nw}

} // namespace Test
