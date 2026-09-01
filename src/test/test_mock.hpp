#pragma once
#include "test_mock.hpp"
#include "../vcv/api.hpp"
#include <rack.hpp>
#include <type_traits>
#include <utility>

namespace Test {

using namespace StoermelderPackOne;

namespace MockVcv {

// Installs the mock accesses into the swappable vcv access globals for the duration of
// the scope, restoring all seven to nullptr on destruction. Template on the concrete mock
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
	typename HistoryT = StoermelderPackOne::vcv::HistoryAccess,
	typename NwT = StoermelderPackOne::vcv::NwAccess
>
struct Mock {
	ModuleT modules;
	SceneT scene;
	CableT cables;
	UiT ui;
	FsT fs;
	HistoryT history;
	NwT nw;

	Mock() {
		installIfMock(&modules, StoermelderPackOne::vcv::moduleAccess, prevModules);
		installIfMock(&scene, StoermelderPackOne::vcv::sceneAccess, prevScene);
		installIfMock(&cables, StoermelderPackOne::vcv::cableAccess, prevCables);
		installIfMock(&ui, StoermelderPackOne::vcv::uiAccess, prevUi);
		installIfMock(&fs, StoermelderPackOne::vcv::fileAccess, prevFs);
		installIfMock(&history, StoermelderPackOne::vcv::historyAccess, prevHistory);
		installIfMock(&nw, StoermelderPackOne::vcv::nwAccess, prevNw);
	}
	~Mock() {
		if (!armed) return;
		restoreIfMock<ModuleT>(StoermelderPackOne::vcv::moduleAccess, prevModules);
		restoreIfMock<SceneT>(StoermelderPackOne::vcv::sceneAccess, prevScene);
		restoreIfMock<CableT>(StoermelderPackOne::vcv::cableAccess, prevCables);
		restoreIfMock<UiT>(StoermelderPackOne::vcv::uiAccess, prevUi);
		restoreIfMock<FsT>(StoermelderPackOne::vcv::fileAccess, prevFs);
		restoreIfMock<HistoryT>(StoermelderPackOne::vcv::historyAccess, prevHistory);
		restoreIfMock<NwT>(StoermelderPackOne::vcv::nwAccess, prevNw);
	}

	// Movable so factories like makeMockVcv() can return one by value: ownership of the
	// installed slots (and the saved previous values) transfers to the new instance, and
	// the moved-from source is disarmed so its destructor is a no-op instead of restoring
	// (or double-restoring) slots it no longer owns.
	//
	// Non-copyable: a copy would restore a slot to a stale saved pointer, or restore it
	// twice.
	Mock(const Mock&) = delete;
	Mock& operator=(const Mock&) = delete;

	Mock(Mock&& other) noexcept
		: modules(std::move(other.modules)), scene(std::move(other.scene)),
		  cables(std::move(other.cables)), ui(std::move(other.ui)), fs(std::move(other.fs)),
		  history(std::move(other.history)), nw(std::move(other.nw)),
		  armed(other.armed),
		  prevModules(other.prevModules), prevScene(other.prevScene), prevCables(other.prevCables),
		  prevUi(other.prevUi), prevFs(other.prevFs), prevHistory(other.prevHistory), prevNw(other.prevNw) {
		other.armed = false;
		rebindSlotIfMock<ModuleT>(StoermelderPackOne::vcv::moduleAccess, &modules);
		rebindSlotIfMock<SceneT>(StoermelderPackOne::vcv::sceneAccess, &scene);
		rebindSlotIfMock<CableT>(StoermelderPackOne::vcv::cableAccess, &cables);
		rebindSlotIfMock<UiT>(StoermelderPackOne::vcv::uiAccess, &ui);
		rebindSlotIfMock<FsT>(StoermelderPackOne::vcv::fileAccess, &fs);
		rebindSlotIfMock<HistoryT>(StoermelderPackOne::vcv::historyAccess, &history);
		rebindSlotIfMock<NwT>(StoermelderPackOne::vcv::nwAccess, &nw);
	}
	Mock& operator=(Mock&&) = delete;

private:
	bool armed = true;

	// After a move, the global slot still points at the moved-from sub-object's address
	// (copied verbatim by installIfMock at construction time). Repoints it at the new
	// sub-object so the mock keeps working and restoreIfMock() on the new instance is
	// the one that runs at destruction.
	template <typename T, typename Base>
	static void rebindSlotIfMock(Base*& slot, T* m) {
		rebindSlotIfMockImpl(slot, m, std::is_same<T, Base>());
	}
	template <typename T, typename Base>
	static void rebindSlotIfMockImpl(Base*& slot, T* m, std::true_type) {
		// T is the base interface — this Mock never installed anything here.
	}
	template <typename T, typename Base>
	static void rebindSlotIfMockImpl(Base*& slot, T* m, std::false_type) {
		slot = m;
	}

	StoermelderPackOne::vcv::ModuleAccess* prevModules = nullptr;
	StoermelderPackOne::vcv::SceneAccess* prevScene = nullptr;
	StoermelderPackOne::vcv::CableAccess* prevCables = nullptr;
	StoermelderPackOne::vcv::UiAccess* prevUi = nullptr;
	StoermelderPackOne::vcv::FileAccess* prevFs = nullptr;
	StoermelderPackOne::vcv::HistoryAccess* prevHistory = nullptr;
	StoermelderPackOne::vcv::NwAccess* prevNw = nullptr;

	// Installs `m` into `slot` only if `T` is a concrete mock (not the base interface),
	// saving the slot's previous value into `prev` first so it can be restored later.
	// A base-interface slot stays untouched, so the real Rack API remains active.
	template <typename T, typename Base>
	static void installIfMock(T* m, Base*& slot, Base*& prev) {
		installIfMockImpl(m, slot, prev, std::is_same<T, Base>());
	}
	template <typename T, typename Base>
	static void installIfMockImpl(T* m, Base*& slot, Base*& prev, std::true_type) {
		// T is the base interface — leave the slot untouched (real Rack API active).
	}
	template <typename T, typename Base>
	static void installIfMockImpl(T* m, Base*& slot, Base*& prev, std::false_type) {
		prev = slot;
		slot = m;
	}

	// Restores `slot` to its saved previous value, but only if this Mock actually
	// installed it (matching installIfMock's condition) — otherwise the slot was never
	// touched and must be left alone.
	template <typename T, typename Base>
	static void restoreIfMock(Base*& slot, Base* prev) {
		restoreIfMockImpl<T>(slot, prev, std::is_same<T, Base>());
	}
	template <typename T, typename Base>
	static void restoreIfMockImpl(Base*& slot, Base* prev, std::true_type) {
		// T is the base interface — this Mock never installed anything here.
	}
	template <typename T, typename Base>
	static void restoreIfMockImpl(Base*& slot, Base* prev, std::false_type) {
		slot = prev;
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
		typename MockVcv::SlotOf<vcv::HistoryAccess, Ts...>::type,
		typename MockVcv::SlotOf<vcv::NwAccess, Ts...>::type
	>();
}

} // namespace Test