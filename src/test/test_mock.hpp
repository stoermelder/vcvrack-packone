#pragma once
#include "test_mock.hpp"
#include "../vcv/api.hpp"
#include <rack.hpp>
#include <type_traits>

namespace Test {

using namespace StoermelderPackOne;

namespace MockVcv {

// Installs the mock accesses into the swappable vcv access globals for the duration of
// the scope, restoring all six to nullptr on destruction. Template on the concrete mock
// types so each test binary supplies its own recording mocks (see src/vcv/files.test.cpp
// for a full example).
//
// A template parameter left at its default (the base interface) means "not mocked": that
// slot is NOT installed, so *AccessFor() falls back to the real Rack implementation. Only
// the accesses you actually mock need concrete mock classes — and any access your code
// path touches must be one of them (a real UiAccess would pop dialogs, a real HistoryAccess
// would crash: APP->history is null in TestContext).
template <
	typename ModuleT = StoermelderPackOne::vcv::ModuleAccess,
	typename SceneT = StoermelderPackOne::vcv::SceneAccess,
	typename CableT = StoermelderPackOne::vcv::CableAccess,
	typename UiT = StoermelderPackOne::vcv::UiAccess,
	typename FsT = StoermelderPackOne::vcv::FileAccess,
	typename HistoryT = StoermelderPackOne::vcv::HistoryAccess
>
struct Mock {
	ModuleT modules;
	SceneT scene;
	CableT cables;
	UiT ui;
	FsT fs;
	HistoryT history;

	Mock() {
		installIfMock(&modules, StoermelderPackOne::vcv::moduleAccess);
		installIfMock(&scene, StoermelderPackOne::vcv::sceneAccess);
		installIfMock(&cables, StoermelderPackOne::vcv::cableAccess);
		installIfMock(&ui, StoermelderPackOne::vcv::uiAccess);
		installIfMock(&fs, StoermelderPackOne::vcv::fileAccess);
		installIfMock(&history, StoermelderPackOne::vcv::historyAccess);
	}
	~Mock() {
		StoermelderPackOne::vcv::moduleAccess = nullptr;
		StoermelderPackOne::vcv::sceneAccess = nullptr;
		StoermelderPackOne::vcv::cableAccess = nullptr;
		StoermelderPackOne::vcv::uiAccess = nullptr;
		StoermelderPackOne::vcv::fileAccess = nullptr;
		StoermelderPackOne::vcv::historyAccess = nullptr;
	}

private:
	// Installs `m` into `slot` only if `T` is a concrete mock (not the base interface).
	// A base-interface slot stays at nullptr, so the real Rack API remains active.
	template <typename T, typename Base>
	static void installIfMock(T* m, Base*& slot) {
		installIfMockImpl(m, slot, std::is_same<T, Base>());
	}
	template <typename T, typename Base>
	static void installIfMockImpl(T* m, Base*& slot, std::true_type) {
		// T is the base interface — leave the slot untouched (real Rack API active).
	}
	template <typename T, typename Base>
	static void installIfMockImpl(T* m, Base*& slot, std::false_type) {
		slot = m;
	}
};

// Maps each mock type in Ts... to its slot by the base interface it derives from.
// A slot with no matching mock keeps the base interface (and is therefore NOT
// installed — the real Rack API stays active for it).
template <typename Base, typename... Ts>
struct SlotOf {
	using type = Base;
};
template <typename Base, typename T, typename... Ts>
struct SlotOf<Base, T, Ts...> {
	using type = typename std::conditional<
		std::is_base_of<Base, T>::value,
		T,
		typename SlotOf<Base, Ts...>::type
	>::type;
};


// Default SystemAccess mock: forwards every call to the real rack::system, so a test
// that only cares about a few filesystem calls can inherit from this and override just
// those methods (e.g. to record or veto them) instead of re-implementing the whole
// interface. Install it directly with makeMockVcv<MockSystemAccess>() for a pure
// pass-through system layer, or derive a recording mock from it.
struct MockFileAccess : StoermelderPackOne::vcv::FileAccess {
	std::string join(const std::string& path1, const std::string& path2) override { return rack::system::join(path1, path2); }
	std::string getDirectory(const std::string& path) override { return rack::system::getDirectory(path); }
	std::string getFilename(const std::string& path) override { return rack::system::getFilename(path); }
	std::string getStem(const std::string& path) override { return rack::system::getStem(path); }
	std::string getExtension(const std::string& path) override { return rack::system::getExtension(path); }
	std::vector<std::string> getEntries(const std::string& dirPath, int depth) override { return rack::system::getEntries(dirPath, depth); }
	bool exists(const std::string& path) override { return rack::system::exists(path); }
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
	double getTime() override { return rack::system::getTime(); }
	void openDirectory(const std::string& path) override { rack::system::openDirectory(path); }
};

} // namespace MockVcv


// Factory: name only the mocks you want (in any order); the rest default to the
// base interfaces. Avoids spelling out the leading default template parameters of
// MockAccess, e.g. makeMockVcv<MockUiAccess>() instead of
// MockAccess<ModuleAccess, SceneAccess, CableAccess, MockUiAccess>().
template <typename... Ts>
auto makeMockVcv() {
	return MockVcv::Mock<
		typename MockVcv::SlotOf<vcv::ModuleAccess, Ts...>::type,
		typename MockVcv::SlotOf<vcv::SceneAccess, Ts...>::type,
		typename MockVcv::SlotOf<vcv::CableAccess, Ts...>::type,
		typename MockVcv::SlotOf<vcv::UiAccess, Ts...>::type,
		typename MockVcv::SlotOf<vcv::FileAccess, Ts...>::type,
		typename MockVcv::SlotOf<vcv::HistoryAccess, Ts...>::type
	>();
}

} // namespace Test