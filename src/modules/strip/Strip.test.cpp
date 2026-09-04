#include "../../test/framework.hpp"
#include "Strip.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Strip;

SYNC_MODEL(modelStrip, "Strip");
Test::TestContext<> testContext;


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
struct MockFileAccess : Test::mock::MockFileAccess {
	std::map<std::string, std::string> files;
	std::vector<std::string> reads;
	std::map<std::string, std::string> writes;

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
	Test::ModuleScaffold<StripModule> mods;
	StripModule* module;
	StripWidget* widget;

	StripFixture(MODE mode = MODE::LEFTRIGHT) {
		module = mods.create("Strip");
		module->mode = mode;
		widget = Test::createWidget<StripWidget>(module);
	}
	~StripFixture() { Test::destroyWidget(widget); }
};

TEST_CASE("Construction and initialization", "[Strip]") {
	Test::ModuleScaffold<StripModule> mods;
	StripModule* m = mods.create("Strip");
	StripWidget* mw = Test::createWidget<StripWidget>("Strip");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Strip][JSON]") {
	Test::ModuleScaffold<StripModule> mods;
	auto module = mods.create("Strip");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

}


// ---- selection load (.vcvs) -------------------------------------------------

TEST_CASE("groupSelectionFromJson creates modules, cables and one undo entry", "[Strip][selection]") {
	Mock mock;
	StripFixture f;

	json_t* rootJ = json_loads(SELECTION_JSON, 0, nullptr);
	REQUIRE(rootJ != nullptr);
	DEFER({ json_decref(rootJ); });

	f.widget->groupSelectionFromJson(rootJ);

	// Both modules created, in file order, through ModuleAccess.
	REQUIRE(mock.modules.added.size() == 2);
	CHECK(mock.modules.added[0].ref.pluginSlug == "NoSuchPlugin");
	CHECK(mock.modules.added[0].ref.modelSlug == "M1");
	CHECK(mock.modules.added[1].ref.modelSlug == "M2");
	// Positions are normalized to the selection's top-left, then scaled to pixels: the second
	// module sits 3 grid columns right of the first.
	CHECK(mock.modules.added[1].pos.x - mock.modules.added[0].pos.x == Catch::Approx(3 * RACK_GRID_WIDTH));
	CHECK(mock.modules.added[1].pos.y == Catch::Approx(mock.modules.added[0].pos.y));

	// Both are selected, and their presets applied.
	CHECK(mock.scene.selected == std::vector<int64_t>{1000, 1001});
	CHECK(mock.modules.appliedPresets == std::vector<int64_t>{1000, 1001});

	// The cable is re-pointed through the old→new id map (1→1000, 2→1001).
	REQUIRE(mock.cables.added.size() == 1);
	CHECK(mock.cables.added[0].outModuleId == 1000);
	CHECK(mock.cables.added[0].outPortId == 0);
	CHECK(mock.cables.added[0].inModuleId == 1001);
	CHECK(mock.cables.added[0].inPortId == 1);

	// Exactly one undo entry for the whole load, not one per module/cable.
	REQUIRE(mock.history.pushed.size() == 1);
	auto* ca = dynamic_cast<::rack::history::ComplexAction*>(mock.history.pushed[0]);
	REQUIRE(ca != nullptr);
	CHECK(ca->name == "stoermelder STRIP selection load");
}

TEST_CASE("groupSelectionFromJson skips cables whose modules failed to load", "[Strip][selection]") {
	Mock mock;
	StripFixture f;

	json_t* rootJ = json_loads(R"({
		"modules": [{"plugin":"NoSuchPlugin","model":"M1","id":1,"pos":[0,0]}],
		"cables": [
			{"outputModuleId":1,"outputId":0,"inputModuleId":999,"inputId":0},
			{"outputModuleId":998,"outputId":0,"inputModuleId":1,"inputId":0}
		]
	})", 0, nullptr);
	REQUIRE(rootJ != nullptr);
	DEFER({ json_decref(rootJ); });

	f.widget->groupSelectionFromJson(rootJ);

	REQUIRE(mock.modules.added.size() == 1);
	CHECK(mock.cables.added.empty());
}

TEST_CASE("groupSelectionFromJson warns about modules that could not be created", "[Strip][selection]") {
	// addModule always returning -1 is what the real access does for an uninstalled model.
	struct FailingModuleAccess : MockModuleAccess {
		int64_t addModule(const vcv::ModuleRef& ref, Vec pos) override {
			added.push_back({ref, pos});
			return -1;
		}
	};
	struct FailMock {
		TEST_MOCK_MODULES(FailingModuleAccess);
		TEST_MOCK_SCENE(MockSceneAccess);
		TEST_MOCK_CABLES(MockCableAccess);
		TEST_MOCK_UI(MockUiAccess);
		TEST_MOCK_FS(MockFileAccess);
		TEST_MOCK_HISTORY(MockHistoryAccess);
	} mock;
	StripFixture f;

	json_t* rootJ = json_loads(SELECTION_JSON, 0, nullptr);
	REQUIRE(rootJ != nullptr);
	DEFER({ json_decref(rootJ); });

	f.widget->groupSelectionFromJson(rootJ);

	// No module made it into the rack, so nothing is selected and no cable is created...
	CHECK(mock.scene.selected.empty());
	CHECK(mock.cables.added.empty());
	// ...and the user is told which modules were missing, once, at the end of the load.
	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].type == vcv::MessageType::WARNING);
	CHECK(mock.ui.messages[0].buttons == vcv::MessageButtons::OK);
	CHECK(mock.ui.messages[0].msg.find("M1") != std::string::npos);
	CHECK(mock.ui.messages[0].msg.find("M2") != std::string::npos);
}

TEST_CASE("groupSelectionPasteClipboard deselects, reads the clipboard and loads", "[Strip][selection]") {
	Mock mock;
	StripFixture f;

	SECTION("An empty clipboard warns and loads nothing") {
		mock.ui.clipboard = "";
		f.widget->groupSelectionPasteClipboard();

		CHECK(mock.scene.deselectAllCalled);
		REQUIRE(mock.ui.messages.size() == 1);
		CHECK(mock.ui.messages[0].msg.find("clipboard") != std::string::npos);
		CHECK(mock.modules.added.empty());
		CHECK(mock.history.pushed.empty());
	}

	SECTION("Malformed JSON warns and loads nothing") {
		mock.ui.clipboard = "{ not json";
		f.widget->groupSelectionPasteClipboard();

		REQUIRE(mock.ui.messages.size() == 1);
		CHECK(mock.ui.messages[0].msg.find("JSON parsing error") != std::string::npos);
		CHECK(mock.modules.added.empty());
		CHECK(mock.history.pushed.empty());
	}

	SECTION("A valid selection is loaded") {
		mock.ui.clipboard = SELECTION_JSON;
		f.widget->groupSelectionPasteClipboard();

		CHECK(mock.scene.deselectAllCalled);
		REQUIRE(mock.modules.added.size() == 2);
		REQUIRE(mock.history.pushed.size() == 1);
	}
}

TEST_CASE("groupSelectionLoadFileDialog records the directory only when a file is chosen", "[Strip][selection]") {
	Mock mock;
	StripFixture f;
	const std::string before = pluginSettings.stripDirVcvs;
	DEFER({ pluginSettings.stripDirVcvs = before; });

	SECTION("A cancelled dialog loads nothing and leaves the directory untouched") {
		// openResults is empty → the dialog reports cancellation.
		std::string path = f.widget->groupSelectionLoadFileDialog(true);

		CHECK(path == "");
		REQUIRE(mock.ui.openCalls.size() == 1);
		CHECK(mock.ui.openCalls[0].filters == vcv::SELECTION_FILTERS);
		CHECK(mock.ui.openCalls[0].dir == before);
		CHECK(pluginSettings.stripDirVcvs == before);
		CHECK(mock.fs.reads.empty());
		CHECK(mock.modules.added.empty());
	}

	SECTION("A chosen file is read, loaded, and its directory remembered") {
		mock.ui.openResults = {"/a/b/sel.vcvs"};
		mock.fs.files["/a/b/sel.vcvs"] = SELECTION_JSON;

		std::string path = f.widget->groupSelectionLoadFileDialog(true);

		CHECK(path == "/a/b/sel.vcvs");
		CHECK(pluginSettings.stripDirVcvs == "/a/b");
		REQUIRE(mock.fs.reads.size() == 1);
		CHECK(mock.fs.reads[0] == "/a/b/sel.vcvs");
		REQUIRE(mock.modules.added.size() == 2);
		REQUIRE(mock.history.pushed.size() == 1);
	}

	SECTION("load=false selects a path without reading it") {
		mock.ui.openResults = {"/a/b/sel.vcvs"};
		mock.fs.files["/a/b/sel.vcvs"] = SELECTION_JSON;

		std::string path = f.widget->groupSelectionLoadFileDialog(false);

		CHECK(path == "/a/b/sel.vcvs");
		CHECK(pluginSettings.stripDirVcvs == "/a/b");
		CHECK(mock.fs.reads.empty());
		CHECK(mock.modules.added.empty());
	}
}

TEST_CASE("groupSelectionLoadFile prompts once for modules that are not installed", "[Strip][selection]") {
	Mock mock;
	StripFixture f;
	mock.fs.files["/sel.vcvs"] = SELECTION_JSON;

	SECTION("Answering no does not open the library") {
		mock.ui.messageResult = false;
		f.widget->groupSelectionLoadFile("/sel.vcvs");

		REQUIRE(mock.ui.messages.size() == 1);
		CHECK(mock.ui.messages[0].buttons == vcv::MessageButtons::YES_NO);
		CHECK(mock.ui.messages[0].msg.find("not installed") != std::string::npos);
		CHECK(mock.ui.openedBrowsers.empty());
	}

	SECTION("Answering yes opens the library with both missing slugs") {
		mock.ui.messageResult = true;
		f.widget->groupSelectionLoadFile("/sel.vcvs");

		REQUIRE(mock.ui.openedBrowsers.size() == 1);
		CHECK(mock.ui.openedBrowsers[0] ==
		      "https://library.vcvrack.com/?modules=NoSuchPlugin/M1,NoSuchPlugin/M2");
	}
}


// ---- strip load (.vcvss) ----------------------------------------------------

TEST_CASE("groupFromJson lays out left and right modules around the strip", "[Strip][group]") {
	Mock mock;
	StripFixture f;
	f.widget->box.pos = Vec(100.f, 50.f);
	f.widget->box.size = Vec(RACK_GRID_WIDTH * 3, RACK_GRID_HEIGHT);

	json_t* rootJ = json_loads(STRIP_JSON, 0, nullptr);
	REQUIRE(rootJ != nullptr);
	DEFER({ json_decref(rootJ); });

	f.widget->groupFromJson(rootJ);

	// Right side is placed first, then left — one module each.
	REQUIRE(mock.modules.added.size() == 2);
	CHECK(mock.modules.added[0].ref.modelSlug == "R1");
	CHECK(mock.modules.added[1].ref.modelSlug == "L1");
	// The right module starts at the strip's right edge; the left one at its left edge
	// (the mock models are unregistered, so the width lookup yields 0).
	CHECK(mock.modules.added[0].pos.x == Catch::Approx(100.f + RACK_GRID_WIDTH * 3));
	CHECK(mock.modules.added[1].pos.x == Catch::Approx(100.f));
	CHECK(mock.modules.added[0].pos.y == Catch::Approx(50.f));

	// The cable across the strip is re-pointed: 11→1000 (right), 21→1001 (left).
	REQUIRE(mock.cables.added.size() == 1);
	CHECK(mock.cables.added[0].outModuleId == 1001);
	CHECK(mock.cables.added[0].inModuleId == 1000);

	// Selection load selects; group load does not.
	CHECK(mock.scene.selected.empty());

	REQUIRE(mock.history.pushed.size() == 1);
	auto* ca = dynamic_cast<::rack::history::ComplexAction*>(mock.history.pushed[0]);
	REQUIRE(ca != nullptr);
	CHECK(ca->name == "stoermelder STRIP load");
}

TEST_CASE("groupFromJson honours the strip mode", "[Strip][group]") {
	SECTION("RIGHT loads only the right modules") {
		Mock mock;
		StripFixture f(MODE::RIGHT);
		json_t* rootJ = json_loads(STRIP_JSON, 0, nullptr);
		REQUIRE(rootJ != nullptr);
		DEFER({ json_decref(rootJ); });

		f.widget->groupFromJson(rootJ);

		REQUIRE(mock.modules.added.size() == 1);
		CHECK(mock.modules.added[0].ref.modelSlug == "R1");
		// The left module never loaded, so its cable is skipped.
		CHECK(mock.cables.added.empty());
	}

	SECTION("LEFT loads only the left modules") {
		Mock mock;
		StripFixture f(MODE::LEFT);
		json_t* rootJ = json_loads(STRIP_JSON, 0, nullptr);
		REQUIRE(rootJ != nullptr);
		DEFER({ json_decref(rootJ); });

		f.widget->groupFromJson(rootJ);

		REQUIRE(mock.modules.added.size() == 1);
		CHECK(mock.modules.added[0].ref.modelSlug == "L1");
		CHECK(mock.cables.added.empty());
	}
}

TEST_CASE("groupLoadFile reports unreadable and malformed files", "[Strip][group]") {
	Mock mock;
	StripFixture f;

	SECTION("A missing file warns and loads nothing") {
		f.widget->groupLoadFile("/missing.vcvss", false);

		REQUIRE(mock.ui.messages.size() == 1);
		CHECK(mock.ui.messages[0].type == vcv::MessageType::WARNING);
		CHECK(mock.ui.messages[0].buttons == vcv::MessageButtons::OK);
		CHECK(mock.ui.messages[0].msg.find("Could not load file") != std::string::npos);
		CHECK(mock.modules.added.empty());
		CHECK(mock.history.pushed.empty());
	}

	SECTION("Malformed JSON warns and loads nothing") {
		mock.fs.files["/bad.vcvss"] = "{ not json";
		f.widget->groupLoadFile("/bad.vcvss", false);

		REQUIRE(mock.ui.messages.size() == 1);
		CHECK(mock.ui.messages[0].msg.find("JSON parsing error") != std::string::npos);
		CHECK(mock.modules.added.empty());
		CHECK(mock.history.pushed.empty());
	}
}

TEST_CASE("groupLoadFile prompts for strip modules that are not installed", "[Strip][group]") {
	Mock mock;
	StripFixture f;
	mock.fs.files["/g.vcvss"] = STRIP_JSON;
	mock.ui.messageResult = true;

	f.widget->groupLoadFile("/g.vcvss", false);

	// One YES_NO prompt naming both sides' missing modules, then the library opens.
	REQUIRE(mock.ui.messages.size() >= 1);
	CHECK(mock.ui.messages[0].buttons == vcv::MessageButtons::YES_NO);
	REQUIRE(mock.ui.openedBrowsers.size() == 1);
	CHECK(mock.ui.openedBrowsers[0] ==
	      "https://library.vcvrack.com/?modules=NoSuchPlugin/L1,NoSuchPlugin/R1");
}

TEST_CASE("groupLoadFileDialog records the directory only when a file is chosen", "[Strip][group]") {
	Mock mock;
	StripFixture f;
	const std::string before = pluginSettings.stripDirVcvss;
	DEFER({ pluginSettings.stripDirVcvss = before; });

	SECTION("A cancelled dialog loads nothing") {
		f.widget->groupLoadFileDialog(false);

		REQUIRE(mock.ui.openCalls.size() == 1);
		CHECK(mock.ui.openCalls[0].filters == PRESET_FILTERS);
		CHECK(mock.ui.openCalls[0].dir == before);
		CHECK(pluginSettings.stripDirVcvss == before);
		CHECK(mock.fs.reads.empty());
		CHECK(mock.modules.added.empty());
	}

	SECTION("A chosen file is read and its directory remembered") {
		mock.ui.openResults = {"/x/y/g.vcvss"};
		mock.fs.files["/x/y/g.vcvss"] = STRIP_JSON;

		f.widget->groupLoadFileDialog(false);

		CHECK(pluginSettings.stripDirVcvss == "/x/y");
		REQUIRE(mock.fs.reads.size() == 1);
		CHECK(mock.fs.reads[0] == "/x/y/g.vcvss");
		REQUIRE(mock.modules.added.size() == 2);
	}
}


// ---- save / clipboard -------------------------------------------------------

TEST_CASE("groupSaveFile writes through the filesystem access", "[Strip][save]") {
	Mock mock;
	StripFixture f;

	f.widget->groupSaveFile("/out.vcvss");

	REQUIRE(mock.fs.writes.count("/out.vcvss") == 1);
	// The payload is the group's JSON: an empty strip still carries its schema keys.
	const std::string& data = mock.fs.writes["/out.vcvss"];
	CHECK(data.find("\"stripVersion\"") != std::string::npos);
	CHECK(data.find("\"leftModules\"") != std::string::npos);
	CHECK(data.find("\"rightModules\"") != std::string::npos);
	CHECK(mock.ui.messages.empty());
}

TEST_CASE("groupSaveFile warns when the file cannot be written", "[Strip][save]") {
	// A FileAccess whose write always fails, over the recording mock's other behaviour.
	struct FailingFileAccess : MockFileAccess {
		bool write(const std::string& path, const std::string& data) override { return false; }
	};
	struct FailMock {
		TEST_MOCK_MODULES(MockModuleAccess);
		TEST_MOCK_UI(MockUiAccess);
		TEST_MOCK_FS(FailingFileAccess);
	} mock;
	StripFixture f;

	f.widget->groupSaveFile("/out.vcvss");

	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].type == vcv::MessageType::WARNING);
	CHECK(mock.ui.messages[0].msg.find("Could not write") != std::string::npos);
}

TEST_CASE("groupCopyClipboard puts the group JSON on the clipboard", "[Strip][save]") {
	Mock mock;
	StripFixture f;

	f.widget->groupCopyClipboard();

	CHECK(mock.ui.clipboard.find("\"stripVersion\"") != std::string::npos);
	// Copy alone removes nothing.
	CHECK(mock.modules.removed.empty());
	CHECK(mock.history.pushed.empty());
}

TEST_CASE("groupPasteClipboard reports an empty clipboard", "[Strip][save]") {
	Mock mock;
	StripFixture f;
	mock.ui.clipboard = "";

	f.widget->groupPasteClipboard();

	REQUIRE(mock.ui.messages.size() == 1);
	CHECK(mock.ui.messages[0].msg.find("clipboard") != std::string::npos);
	CHECK(mock.modules.added.empty());
	CHECK(mock.history.pushed.empty());
}