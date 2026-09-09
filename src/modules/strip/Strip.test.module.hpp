// STRIP test cases. Included by Strip.test.cpp inside namespace __module.
// Not a standalone header: Strip.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[Strip]") {
	Test::ModuleScaffold<StripModule> mods{createStripModule};
	StripModule* m = mods.create();
	StripWidget* mw = Test::createWidget<StripWidget>("Strip");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Strip][JSON]") {
	Test::ModuleScaffold<StripModule> mods{createStripModule};
	auto module = mods.create();

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


// ---- bypass -----------------------------------------------------------------
// groupBypassRequest() (UI thread) enqueues groupBypass() onto taskProcessor; process() drains
// that queue and hands the actual work to taskWorker->work(groupBypassWorker). Since the test
// worker is a NullTaskWorker (see createStripModule()), work() is never run here -- these tests
// call groupBypassWorker() directly, per var/TaskWorker_existing_modules.md, keeping the queue
// side (did process() decide to enqueue?) and the task side (what does the task do when it runs?)
// separately assertable rather than blended into one sequence.

// A minimal expander neighbour whose isBypassed() state is directly observable -- Strip's bypass
// walk only needs a real Module registered with the engine, nothing module-specific.
struct BypassNeighbour : rack::Module {
	BypassNeighbour() { config(0, 0, 0, 0); }
};

TEST_CASE("groupBypassRequest enqueues a task without running it inline", "[Strip][bypass]") {
	Mock mock;
	Test::Harness h;
	StripModule* m = h.addModule<StripModule>(createStripModule);
	auto* right = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	h.connectExpander(m, right);
	h.dspStep();

	m->groupBypassRequest(true);

	// Queue side: process() has not run yet, so the neighbour is untouched.
	CHECK_FALSE(right->isBypassed());
	REQUIRE(mock.history.pushed.size() == 1);
	auto* ca = dynamic_cast<::rack::history::ComplexAction*>(mock.history.pushed[0]);
	REQUIRE(ca != nullptr);
	CHECK(ca->name == "stoermelder STRIP bypass");

	h.dspStep();

	// process() drained taskProcessor and handed the work to taskWorker->work(), which is a
	// NullTaskWorker here and drops it -- so the neighbour is still untouched. This confirms
	// process() reached groupBypass() (nothing else drains taskProcessor) without depending on
	// the real worker; groupBypassWorker()'s own effect is covered separately below.
	CHECK_FALSE(right->isBypassed());
}

// The one test in this file that goes through taskWorker->work() itself, rather than calling
// groupBypassWorker() directly or confirming NullTaskWorker dropped it. Closes the remaining gap:
// every other test either stops at "process() decided to enqueue" or calls the task body directly,
// so nothing here previously exercised groupBypass()'s handoff to the worker -- e.g. a typo'd
// closure (wrong flag, or capturing nothing) would pass every other test unnoticed. Calling
// groupBypass() directly from the test thread, with SyncTaskWorker injected, is safe specifically
// because the test thread is standing in for the caller -- this must never be done from inside a
// dspStep(), where process() is the real caller and the task would run on the engine thread it was
// queued to escape.
TEST_CASE("groupBypass runs groupBypassWorker through the injected worker", "[Strip][bypass]") {
	Test::Harness h;
	StripModule* m = h.addModule<StripModule>(createStripModuleWithSyncWorker);
	m->mode = MODE::RIGHT;
	auto* right = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	h.connectExpander(m, right);
	h.dspStep();

	m->groupBypass(true);

	// SyncTaskWorker ran the task inline, so the effect is visible immediately -- no dspStep()
	// needed, and none would help since dspStep() only drains taskProcessor, not taskWorker.
	CHECK(right->isBypassed());
	CHECK(m->lastBypassState.load());

	// A dspStep() in between confirms the bypass state isn't reset or reasserted by process()
	// itself -- it should only ever be touched by the handoff, not by ordinary stepping.
	h.dspStep();
	CHECK(right->isBypassed());

	// And back the other way, through the same handoff, to confirm it isn't a one-shot latch.
	m->groupBypass(false);

	CHECK_FALSE(right->isBypassed());
	CHECK_FALSE(m->lastBypassState.load());

	h.dspStep();
	CHECK_FALSE(right->isBypassed());
}

TEST_CASE("groupBypassWorker bypasses the right neighbour chain", "[Strip][bypass]") {
	Test::Harness h;
	StripModule* m = h.addModule<StripModule>(createStripModule);
	m->mode = MODE::RIGHT;
	auto* right1 = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	auto* right2 = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	h.connectChain(m, right1, right2);
	h.dspStep();

	m->groupBypassWorker(true);

	CHECK(right1->isBypassed());
	CHECK(right2->isBypassed());
	CHECK(m->lastBypassState.load());
}

TEST_CASE("groupBypassWorker bypasses the left neighbour chain", "[Strip][bypass]") {
	Test::Harness h;
	StripModule* m = h.addModule<StripModule>(createStripModule);
	m->mode = MODE::LEFT;
	auto* left1 = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	auto* left2 = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	// connectChain wires left-to-right; the strip sits at the right end of the chain here.
	h.connectChain(left2, left1, m);
	h.dspStep();

	m->groupBypassWorker(true);

	CHECK(left1->isBypassed());
	CHECK(left2->isBypassed());
}

TEST_CASE("groupBypassWorker in LEFTRIGHT mode bypasses both sides", "[Strip][bypass]") {
	Test::Harness h;
	StripModule* m = h.addModule<StripModule>(createStripModule);
	m->mode = MODE::LEFTRIGHT;
	auto* left = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	auto* right = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	h.connectChain(left, m, right);
	h.dspStep();

	m->groupBypassWorker(true);

	CHECK(left->isBypassed());
	CHECK(right->isBypassed());
}

TEST_CASE("groupBypassWorker in RIGHT mode leaves the left neighbour untouched", "[Strip][bypass]") {
	Test::Harness h;
	StripModule* m = h.addModule<StripModule>(createStripModule);
	m->mode = MODE::RIGHT;
	auto* left = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	auto* right = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	h.connectChain(left, m, right);
	h.dspStep();

	m->groupBypassWorker(true);

	CHECK(right->isBypassed());
	CHECK_FALSE(left->isBypassed());
}

TEST_CASE("groupBypassWorker un-bypasses on a second call with false", "[Strip][bypass]") {
	Test::Harness h;
	StripModule* m = h.addModule<StripModule>(createStripModule);
	m->mode = MODE::RIGHT;
	auto* right = h.addModule<BypassNeighbour>([]{ return new BypassNeighbour; });
	h.connectExpander(m, right);
	h.dspStep();

	m->groupBypassWorker(true);
	REQUIRE(right->isBypassed());

	m->groupBypassWorker(false);

	CHECK_FALSE(right->isBypassed());
	CHECK_FALSE(m->lastBypassState.load());
}
