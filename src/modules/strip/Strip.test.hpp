#pragma once

// Shared preamble for the STRIP test suite.
// Included by Strip.test.cpp, which pulls the test cases in from Strip.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Strip.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Strip;

Test::TestContext<> testContext;

// Test::createModule<StripModule>() would go through modelStrip's factory, i.e. the default
// constructor, which reaches StripModule::defaultWorker() and spins up a real MpmcTaskWorker
// and its thread -- wasted for every test here, since none of them route through the queued
// worker path (see var/TaskWorker_existing_modules.md). This shadow constructs with an injected
// worker instead, so no thread is ever created. Mirrors Test::createModule<T>()'s
// post-construction setup (id, sample rate) since it bypasses the model factory that normally
// does this.
//
// Worker type is a template parameter rather than a fixed NullTaskWorker because one test
// (groupBypass runs groupBypassWorker through the injected worker) needs a real handoff through
// taskWorker->work() -- SyncTaskWorker there, NullTaskWorker everywhere else. See
// createStripModuleWithSyncWorker below for why SyncTaskWorker is safe only in that one case.
template <typename W>
static StripModule* createStripModuleWith() {
	auto* m = new StripModule(std::make_shared<W>());
	m->model = modelStrip;
	m->id = Test::getModuleId();

	Module::SampleRateChangeEvent e;
	e.sampleRate = Test::sampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);

	return m;
}

static StripModule* createStripModule() {
	return createStripModuleWith<NullTaskWorker>();
}
static StripModule* createStripModuleWithSyncWorker() {
	return createStripModuleWith<SyncTaskWorker>();
}

// ---- mock accesses ----------------------------------------------------------
// Strip's load/save paths run entirely on the swappable vcv accesses, so these mocks make the
// decisions observable without a live Rack GUI: which modules were created and where, which
// cables were re-pointed through the old→new id map, what the user was asked, and how many
// undo entries one load produces.

// Hands out fake ids and records placements. getModuleWidget stays at the base default
// (nullptr), so no history children are built — the orchestration is what is asserted on.
struct MockModuleAccess : vcv::ModuleAccess {
	struct Added { vcv::ModuleRef ref; Vec pos; };
	std::vector<Added> added;
	std::vector<int64_t> appliedPresets;
	std::vector<int64_t> removed;
	int64_t nextId = 1000;

	int64_t addModule(const vcv::ModuleRef& ref, Vec pos) override {
		added.push_back({ref, pos});
		return nextId++;
	}
	void applyPreset(int64_t moduleId, json_t* moduleJ) override {
		appliedPresets.push_back(moduleId);
	}
	void removeModule(int64_t moduleId) override { removed.push_back(moduleId); }
};

struct MockSceneAccess : vcv::SceneAccess {
	std::vector<int64_t> selected;
	bool deselectAllCalled = false;

	void select(int64_t moduleId) override { selected.push_back(moduleId); }
	void deselectAll() override { deselectAllCalled = true; }
};

struct MockCableAccess : vcv::CableAccess {
	struct Added { int64_t outModuleId, inModuleId; int outPortId, inPortId; NVGcolor color; };
	std::vector<Added> added;

	::rack::history::CableAdd* addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory, NVGcolor color) override {
		added.push_back({outModuleId, inModuleId, outPortId, inPortId, color});
		// nullptr = "no cable created", which is also what the real access returns for an
		// unknown module id; the caller then folds nothing into its ComplexAction.
		return nullptr;
	}
};

struct MockUiAccess : vcv::UiAccess {
	struct Message { vcv::MessageType type; vcv::MessageButtons buttons; std::string msg; };
	struct OpenCall { std::string filters, dir; };
	std::vector<Message> messages;
	bool messageResult = false;            // default: "No" / dismiss
	std::vector<std::string> openedBrowsers;
	std::string clipboard;
	std::vector<OpenCall> openCalls;
	std::vector<std::string> openResults;  // queue consumed in order; exhausted = cancelled
	size_t openIndex = 0;

	bool message(vcv::MessageType type, vcv::MessageButtons buttons, const std::string& msg) override {
		messages.push_back({type, buttons, msg});
		return messageResult;
	}
	void openBrowser(const std::string& url) override { openedBrowsers.push_back(url); }
	std::string getClipboard() const override { return clipboard; }
	void setClipboard(const std::string& text) override { clipboard = text; }
	std::string openDialog(const std::string& filters, const std::string& dir) override {
		openCalls.push_back({filters, dir});
		return openIndex < openResults.size() ? openResults[openIndex++] : "";
	}
};

// path → contents; a missing key means "cannot open". Path helpers stay on the real
// rack::system (via Test::mock::MockFileAccess) so getDirectory/getExtension behave.
//
// getUserDirectory/createDirectory are overridden to stay inside the virtual filesystem
// (base class forwards both to the real disk) — Strip's dialog paths call
// pluginSettings.saveToJson(), which resolves its directory through getUserDirectory(), so
// without this override that save silently created a "Stoermelder-P1" folder on real disk
// (relative, since rack::asset::user() is never initialized in tests) every time this suite ran.
struct MockFileAccess : Test::mock::MockFileAccess {
	std::map<std::string, std::string> files;
	std::vector<std::string> reads;
	std::map<std::string, std::string> writes;
	std::set<std::string> dirs;

	bool read(const std::string& path, std::string& data) const override {
		const_cast<MockFileAccess*>(this)->reads.push_back(path);
		auto it = files.find(path);
		if (it == files.end()) return false;
		data = it->second;
		return true;
	}
	bool write(const std::string& path, const std::string& data) override {
		writes[path] = data;
		return true;
	}
	std::string getUserDirectory(const std::string& path) override {
		return "/vfs/user/" + path;
	}
	bool createDirectory(const std::string& path) override {
		dirs.insert(path);
		return true;
	}
};

// Records pushed actions and owns them (push takes ownership).
struct MockHistoryAccess : vcv::HistoryAccess {
	std::vector<::rack::history::Action*> pushed;
	void push(::rack::history::Action* a) override { pushed.push_back(a); }
	~MockHistoryAccess() { for (auto* a : pushed) delete a; }
};

struct Mock {
	TEST_MOCK_MODULES(MockModuleAccess);
	TEST_MOCK_SCENE(MockSceneAccess);
	TEST_MOCK_CABLES(MockCableAccess);
	TEST_MOCK_UI(MockUiAccess);
	TEST_MOCK_FS(MockFileAccess);
	TEST_MOCK_HISTORY(MockHistoryAccess);
};

// A .vcvs selection: two modules of an unregistered plugin, one cable between them.
static const char SELECTION_JSON[] = R"({
	"modules": [
		{"plugin":"NoSuchPlugin","model":"M1","id":1,"pos":[0,0]},
		{"plugin":"NoSuchPlugin","model":"M2","id":2,"pos":[3,0]}
	],
	"cables": [
		{"outputModuleId":1,"outputId":0,"inputModuleId":2,"inputId":1}
	]
})";

// A .vcvss strip: one module on each side, one cable between them.
static const char STRIP_JSON[] = R"({
	"stripVersion": 1,
	"leftWidth": 0.0,
	"rightWidth": 0.0,
	"leftModules": [{"plugin":"NoSuchPlugin","model":"L1","id":11}],
	"rightModules": [{"plugin":"NoSuchPlugin","model":"R1","id":21}],
	"cables": [
		{"outputModuleId":11,"outputId":0,"inputModuleId":21,"inputId":0}
	]
})";

// Creates a Strip widget bound to a module, ready to drive the group/selection entry points.
struct StripFixture {
	Test::ModuleScaffold<StripModule> mods{createStripModule};
	StripModule* module;
	StripWidget* widget;

	StripFixture(MODE mode = MODE::LEFTRIGHT) {
		module = mods.create();
		module->mode = mode;
		widget = Test::createWidget<StripWidget>(module);
	}
	~StripFixture() { Test::destroyWidget(widget); }
};

