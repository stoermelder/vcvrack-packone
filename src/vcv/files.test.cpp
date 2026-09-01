#include "../test/framework.hpp"
#include "files.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::vcv;
using Catch::Approx;

// Shared test context: registers the plugin's real models and a real Scene (so
// vcvsFromJson's direct getMousePos() call works). One context, reused by all tests.
static Test::TestContext<> testContext;

// ---- mock accesses ----------------------------------------------------------

// A ModuleAccess that records what it is asked to do and hands out fake ids.
// getModuleWidget stays at the base default (nullptr), so no history children are built
// in these tests — the orchestration is what we assert on, not undo internals.
struct MockModuleAccess : ModuleAccess {
	struct Added { ModuleRef ref; Vec pos; };
	std::vector<Added> added;
	std::vector<int64_t> appliedPresets;
	int64_t nextId = 1000;

	int64_t addModule(const ModuleRef& ref, Vec pos) override {
		added.push_back({ref, pos});
		return nextId++;
	}
	void applyPreset(int64_t moduleId, json_t* moduleJ) override {
		appliedPresets.push_back(moduleId);
	}
};

struct MockSceneAccess : SceneAccess {
	std::vector<int64_t> selected;
	bool deselectAllCalled = false;

	void select(int64_t moduleId) override { selected.push_back(moduleId); }
	void deselectAll() override { deselectAllCalled = true; }
};

struct MockCableAccess : CableAccess {
	struct Added { int64_t outModuleId, inModuleId; int outPortId, inPortId; NVGcolor color; };
	std::vector<Added> added;

	void addCableToPort(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool addToHistory, NVGcolor color) override {
		added.push_back({outModuleId, inModuleId, outPortId, inPortId, color});
	}
};

struct MockUiAccess : UiAccess {
	struct Message { MessageType type; MessageButtons buttons; std::string msg; };
	std::vector<Message> messages;
	bool messageResult = false;           // default: "No" / dismiss
	std::vector<std::string> openedBrowsers;
	std::string clipboard;
	std::vector<std::string> openDialogResults;  // queue consumed in order
	int dialogIndex = 0;

	bool message(MessageType type, MessageButtons buttons, const std::string& msg) override {
		messages.push_back({type, buttons, msg});
		return messageResult;
	}
	void openBrowser(const std::string& url) override { openedBrowsers.push_back(url); }
	std::string getClipboard() const override { return clipboard; }
	std::string openDialog(const std::string& filters, const std::string& dir) override {
		if (dialogIndex < (int) openDialogResults.size()) return openDialogResults[dialogIndex++];
		return "";
	}
};

struct MockFileAccess : FileAccess {
	// path → contents; a missing key means "cannot open".
	std::map<std::string, std::string> files;
	mutable std::map<std::string, std::string> lastDirs;

	bool read(const std::string& path, std::string& data) const override {
		auto it = files.find(path);
		if (it == files.end()) return false;
		data = it->second;
		return true;
	}
};

// A recording HistoryAccess. Owns the actions it records (push takes ownership).
struct MockHistoryAccess : HistoryAccess {
	std::vector<::rack::history::Action*> pushed;
	void push(::rack::history::Action* a) override { pushed.push_back(a); }
	~MockHistoryAccess() { for (auto* a : pushed) delete a; }
};


// Installs all six recording mocks via the shared Test::MockAccess helper.

auto createMock = []() { 
    return Test::makeMockVcv<
        MockModuleAccess, MockSceneAccess, MockCableAccess,
        MockUiAccess, MockFileAccess, MockHistoryAccess
    >();
};


static json_t* loadJson(const char* s) {
	return json_loads(s, 0, nullptr);
}


// promptUnavailableModules

TEST_CASE("promptUnavailableModules asks once and opens the browser on yes", "[files]") {
	auto mock = createMock();
	mock.ui.messageResult = true;

	promptUnavailableModules({"A/Missing", "B/Gone"});

	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].type == MessageType::WARNING);
	CHECK(mock.ui.messages[0].buttons == MessageButtons::YES_NO);
	CHECK(mock.ui.messages[0].msg.find("not installed") != std::string::npos);
	REQUIRE(mock.ui.openedBrowsers.size() == 1);
	CHECK(mock.ui.openedBrowsers[0] == "https://library.vcvrack.com/?modules=A/Missing,B/Gone");
}

TEST_CASE("promptUnavailableModules is silent for no missing modules", "[files]") {
	auto mock = createMock();
	promptUnavailableModules({});
	CHECK(mock.ui.messages.empty());
	CHECK(mock.ui.openedBrowsers.empty());
}

TEST_CASE("promptUnavailableModules does not open the browser on no", "[files]") {
	auto mock = createMock();
	mock.ui.messageResult = false;
	promptUnavailableModules({"A/Missing"});
	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.openedBrowsers.empty());
}

// vcvsLoadFile

TEST_CASE("vcvsLoadFile warns when the file cannot be opened", "[files]") {
	auto mock = createMock();
	vcvsLoadFile("missing.vcvs");

	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].type == MessageType::WARNING);
	CHECK(mock.ui.messages[0].buttons == MessageButtons::OK);
	CHECK(mock.ui.messages[0].msg.find("Could not open") != std::string::npos);
	CHECK(mock.modules.added.empty());
	CHECK(mock.history.pushed.empty());
}

TEST_CASE("vcvsLoadFile warns when the JSON is malformed", "[files]") {
	auto mock = createMock();
	mock.fs.files["bad.vcvs"] = "{ not json";
	vcvsLoadFile("bad.vcvs");

	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].type == MessageType::WARNING);
	CHECK(mock.ui.messages[0].msg.find("JSON parsing error") != std::string::npos);
	CHECK(mock.modules.added.empty());
	CHECK(mock.history.pushed.empty());
}

TEST_CASE("vcvsLoadFile loads a selection and pushes one undo entry", "[files]") {
	auto mock = createMock();
	// Fake plugin/model slugs are guaranteed unregistered, so the unavailable-modules
	// prompt is deterministic: exactly one YES_NO question, "No" → no browser.
	mock.fs.files["sel.vcvs"] = R"({
		"modules": [
			{"plugin":"NoSuchPlugin","model":"M1","id":1,"pos":[0,0]},
			{"plugin":"NoSuchPlugin","model":"M2","id":2,"pos":[1,0]}
		],
		"cables": [
			{"outputModuleId":1,"outputId":0,"inputModuleId":2,"inputId":0}
		]
	})";
	vcvsLoadFile("sel.vcvs");

	REQUIRE(mock.modules.added.size() == 2);
	CHECK(mock.modules.added[0].ref.pluginSlug == "NoSuchPlugin");
	CHECK(mock.modules.added[0].ref.modelSlug == "M1");
	CHECK(mock.modules.added[1].ref.modelSlug == "M2");
	REQUIRE(mock.scene.selected.size() == 2);

	// Cables are re-pointed through the old→new id map (new ids are 1000, 1001).
	REQUIRE(mock.cables.added.size() == 1);
	CHECK(mock.cables.added[0].outModuleId == 1000);
	CHECK(mock.cables.added[0].outPortId == 0);
	CHECK(mock.cables.added[0].inModuleId == 1001);
	CHECK(mock.cables.added[0].inPortId == 0);

	// The missing-module prompt: exactly one YES_NO, answered "No" → no browser.
	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].buttons == MessageButtons::YES_NO);
	CHECK(mock.ui.openedBrowsers.empty());

	// One undo entry for the whole load.
	REQUIRE(mock.history.pushed.size() == 1);
	auto* ca = dynamic_cast<::rack::history::ComplexAction*>(mock.history.pushed[0]);
	REQUIRE(ca != nullptr);
	CHECK(ca->name == "Load selection");
}

// vcvsPasteClipboard

TEST_CASE("vcvsPasteClipboard warns when the clipboard is empty", "[files]") {
	auto mock = createMock();
	mock.ui.clipboard = "";
	vcvsPasteClipboard();

	REQUIRE(mock.scene.deselectAllCalled);
	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].msg.find("clipboard") != std::string::npos);
	CHECK(mock.modules.added.empty());
}

TEST_CASE("vcvsPasteClipboard loads a selection from the clipboard", "[files]") {
	auto mock = createMock();
	mock.ui.clipboard = R"({"modules":[{"plugin":"NoSuchPlugin","model":"M1","id":1,"pos":[0,0]}]})";
	vcvsPasteClipboard();

	REQUIRE(mock.scene.deselectAllCalled);
	REQUIRE(mock.modules.added.size() == 1);
	REQUIRE(mock.history.pushed.size() == 1);
}

// vcvsLoadFileDialog

/*
TEST_CASE("vcvsLoadFileDialog returns empty when cancelled", "[files]") {
	auto mock = createMock();
	CHECK(vcvsLoadFileDialog(true) == "");
	CHECK(mock.modules.added.empty());
	CHECK(mock.fs.lastDirs.empty());
}

TEST_CASE("vcvsLoadFileDialog records the last directory and loads", "[files]") {
	auto mocks = createMock();
	mocks.ui.openDialogResults.push_back("/a/b/sel.vcvs");
	mocks.fs.files["/a/b/sel.vcvs"] = R"({"modules":[{"plugin":"NoSuchPlugin","model":"M1","id":1,"pos":[0,0]}]})";

	std::string path = vcvsLoadFileDialog(true);

	CHECK(path == "/a/b/sel.vcvs");
	CHECK(mocks.fs.lastDirs["stripDirVcvs"] == "/a/b");
	REQUIRE(mocks.modules.added.size() == 1);
}
*/

// vcvsFromJson

TEST_CASE("vcvsFromJson orchestrates modules, presets, cables and one undo entry", "[files]") {
	auto mock = createMock();
	json_t* rootJ = loadJson(R"({
		"modules": [
			{"plugin":"A","model":"M1","id":1,"pos":[0,0]},
			{"plugin":"A","model":"M2","id":2,"pos":[1,0]}
		],
		"cables": [
			{"outputModuleId":1,"outputId":0,"inputModuleId":2,"inputId":0}
		]
	})");
	REQUIRE(rootJ);

	vcvsFromJson(rootJ);

	REQUIRE(mock.modules.added.size() == 2);
	REQUIRE(mock.modules.appliedPresets.size() == 2);
	REQUIRE(mock.scene.selected.size() == 2);
	REQUIRE(mock.cables.added.size() == 1);
	CHECK(mock.cables.added[0].outModuleId == 1000);
	CHECK(mock.cables.added[0].inModuleId == 1001);

	REQUIRE(mock.history.pushed.size() == 1);
	auto* ca = dynamic_cast<::rack::history::ComplexAction*>(mock.history.pushed[0]);
	REQUIRE(ca != nullptr);
	CHECK(ca->name == "Load selection");

	json_decref(rootJ);
}

TEST_CASE("vcvsFromJson skips cables whose modules failed to load", "[files]") {
	auto mock = createMock();
	json_t* rootJ = loadJson(R"({
		"modules": [
			{"plugin":"A","model":"M1","id":1,"pos":[0,0]}
		],
		"cables": [
			{"outputModuleId":1,"outputId":0,"inputModuleId":999,"inputId":0},
			{"outputModuleId":998,"outputId":0,"inputModuleId":1,"inputId":0}
		]
	})");
	REQUIRE(rootJ);

	vcvsFromJson(rootJ);

	REQUIRE(mock.modules.added.size() == 1);
	CHECK(mock.cables.added.empty());

	json_decref(rootJ);
}

// MockAccess

TEST_CASE("MockAccess leaves un-mocked accesses on the real Rack API", "[files]") {
	// Only ui is mocked; the other slots default to the base interfaces, which are
	// NOT installed — so *AccessFor() keeps using the real Rack implementations.
	// The scope owns the mock instances (its `ui` member is what gets installed).
	auto scope = Test::makeMockVcv<MockUiAccess>();

	CHECK(vcv::moduleAccess == nullptr);
	CHECK(vcv::sceneAccess == nullptr);
	CHECK(vcv::cableAccess == nullptr);
	CHECK(vcv::uiAccess == &scope.ui);
	CHECK(vcv::fileAccess == nullptr);
	CHECK(vcv::historyAccess == nullptr);
}
