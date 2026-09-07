// Stability & performance mode dispatch and threading regression tests, mirroring
// EightFaceMk2.test.threading.hpp for mk1's PresetDispatch (shared with mk2 via PresetDispatch.hpp).

// A minimal target module whose fromJson()/toJson() calls are observable without any real
// modulation -- EightFaceModule itself works fine as the "bound" module for this purpose, its
// panelTheme field just must round-trip through dataToJson()/dataFromJson(), which it does.
struct DispatchFixture {
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceModule<8>* m;
	EightFaceWidget* mw;
	EightFaceModule<8>* boundM;
	EightFaceWidget* boundMw;

	explicit DispatchFixture(std::function<EightFaceModule<8>*()> factory) {
		m = h.addModule<EightFaceModule<8>>(factory);
		// h.uiFrame() only steps widgets added through the harness -- dispatch.step() (m's
		// production drain path under UiPresent) is reached from m's own widget's step(), so the
		// fixture needs one, not just a widget for the bound target below.
		mw = h.addWidget<EightFaceWidget>(m);
		boundM = h.addModule<EightFaceModule<8>>(createEightFaceModule);
		boundMw = Test::createWidget<EightFaceWidget>(boundM);
		connectForTest(m, boundM, boundMw);
	}

	// boundM/boundMw were registered directly (registerModule(), via connectForTest()) so that
	// APP->scene->rack->getModule() can resolve them -- the harness's own addWidget() only parents
	// a widget under APP->scene, not APP->scene->rack. That means the harness's own teardown does
	// not know about boundMw, so it must be unregistered here, before h's destructor removes
	// boundM from the engine -- otherwise boundMw survives (still parented in APP->scene->rack)
	// until the global RackWidget is torn down at process exit, when it tries to remove a module
	// the engine no longer has.
	~DispatchFixture() {
		Test::unregisterModule(boundM, boundMw);
	}

	void savePreset(int p, int themeMarker) {
		saveForTest(m, p, boundM, boundMw, themeMarker);
	}

	int appliedLabel() {
		return appliedTheme(boundM);
	}
};

TEST_CASE("Safe mode applies from the UI thread (uiFrame), not immediately", "[EightFace][dispatch]") {
	DispatchFixture f(createEightFaceModule);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	f.savePreset(0, 11);

	f.m->presetLoad(f.boundM, 0, false, true);

	// Queued, not yet applied: nothing has drained dispatch.guiTasks yet.
	f.h.dspStep();
	REQUIRE(f.appliedLabel() != 11);

	// The widget's step() drains guiTasks under UiPresent -- the production path for a plugin
	// with the editor open.
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 11);
}

TEST_CASE("Unsafe mode also applies from the UI thread (uiFrame)", "[EightFace][dispatch]") {
	DispatchFixture f(createEightFaceModule);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI;
	f.savePreset(0, 22);

	f.m->presetLoad(f.boundM, 0, false, true);
	f.h.dspStep();
	REQUIRE(f.appliedLabel() != 22);

	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 22);
}

TEST_CASE("Unsafe fast mode applies through the injected worker, not the UI queue", "[EightFace][dispatch]") {
	DispatchFixture f(createEightFaceModuleWithSyncWorker);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.savePreset(0, 33);

	// SyncTaskWorker::work() runs the task inline, on the calling thread -- standing in for a real
	// worker thread landing before the next line runs.
	f.m->presetLoad(f.boundM, 0, false, true);
	REQUIRE(f.appliedLabel() == 33);

	// Confirms the UI queue was never touched -- draining it changes nothing further.
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 33);
}

TEST_CASE("needsGuiThread overrides Unsafe fast: allowlisted bound module still applies via the UI queue", "[EightFace][dispatch]") {
	DispatchFixture f(createEightFaceModuleWithSyncWorker);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.m->needsGuiThread = true;
	f.savePreset(0, 44);

	f.m->presetLoad(f.boundM, 0, false, true);
	// The allowlist steers the hop in presetLoad(), so the worker is never given the task at all.
	REQUIRE(f.appliedLabel() != 44);

	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 44);
}

TEST_CASE("needsGuiThread override takes the UI hop directly, without going through the worker", "[EightFace][dispatch]") {
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceModuleWithWorker(worker); });
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.m->needsGuiThread = true;
	f.savePreset(0, 66);

	f.m->presetLoad(f.boundM, 0, false, true);
	// mk1 binds exactly one module, and it is allowlisted here -- there is no worker task at all.
	REQUIRE(worker->count == 0);

	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 66);
	REQUIRE(worker->count == 0);
}

TEST_CASE("Unsafe fast without the allowlist still takes the worker hop", "[EightFace][dispatch]") {
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceModuleWithWorker(worker); });
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.m->needsGuiThread = false;
	f.savePreset(0, 77);

	f.m->presetLoad(f.boundM, 0, false, true);
	REQUIRE(worker->count == 1);
	REQUIRE(f.appliedLabel() == 77);
}

TEST_CASE("GUI vs GUI_WITH_LOCK apply through different objects", "[EightFace][dispatch]") {
	// Distinguishable by which object receives fromJson: mw->module (GUI / Unsafe) vs mw
	// (GUI_WITH_LOCK / Safe) -- both end up updating the same module state in the end (mk1's
	// applyPreset() ultimately reaches Module::fromJson() either way), so the two branches are
	// asserted by exercising each distinctly rather than by a differing effect, matching mk2's
	// equivalent test.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");
	EightFaceModule<8>* boundM = mods.create("EightFace");
	EightFaceWidget* boundMw = Test::createWidget<EightFaceWidget>(boundM);
	connectForTest(m, boundM, boundMw);

	saveForTest(m, 0, boundM, boundMw, 55);
	m->preset = -1;
	m->presetPrev = -1;

	SECTION("GUI_WITH_LOCK calls mw->fromJson (widget path)") {
		m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	}
	SECTION("GUI calls mw->module->fromJson (module-only path)") {
		m->dispatch.guiSafeMode = GUISAFEMODE::GUI;
	}

	m->applyPreset(m->dispatch.allLoader(), boundM->id, -1, 0);
	m->dispatch.drain();

	REQUIRE(appliedTheme(boundM) == 55);

	Test::unregisterModule(boundM, boundMw);
}

TEST_CASE("No window: presets still load in every mode", "[EightFace][dispatch]") {
	SECTION("Safe mode") {
		DispatchFixture f(createEightFaceModule);
		f.h.setUiMode(Test::UiMode::UiAbsent);
		f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
		f.savePreset(0, 88);

		f.m->presetLoad(f.boundM, 0, false, true);
		// guiTasks.process() (called from m->process() every guiTaskDivider tick) starts its own
		// private worker once hasWindow() is false; step the engine long enough for it to run.
		for (int i = 0; i < 10 && f.appliedLabel() != 88; i++) {
			f.h.dspSteps(1024);
		}
		REQUIRE(f.appliedLabel() == 88);
	}

	SECTION("Unsafe fast mode") {
		DispatchFixture f(createEightFaceModuleWithSyncWorker);
		f.h.setUiMode(Test::UiMode::UiAbsent);
		f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
		f.savePreset(0, 99);

		f.m->presetLoad(f.boundM, 0, false, true);
		REQUIRE(f.appliedLabel() == 99);
	}
}

TEST_CASE("No window: an allowlisted bound module is skipped in every mode", "[EightFace][dispatch]") {
	// mk1 has exactly one bound module, so unlike mk2's split-preset case there is no ordinary
	// half to mask a mistake: with the sole bound module allowlisted and no window, nothing may be
	// applied, in any mode -- the crash EightFace::guiModuleSlugs exists to prevent (loading an
	// allowlisted module off the UI thread when there is no UI thread).
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceModuleWithWorker(worker); });
	f.h.setUiMode(Test::UiMode::UiAbsent);
	f.m->needsGuiThread = true;
	f.savePreset(0, 111);

	SECTION("Unsafe fast") {
		f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	}
	SECTION("Safe -- the path that crashed before the skip existed") {
		f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	}
	SECTION("Unsafe") {
		f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI;
	}

	f.m->presetLoad(f.boundM, 0, false, true);
	f.m->dispatch.drain();
	REQUIRE(f.appliedLabel() != 111);
}


// ---- Auto mode ----------------------------------------------------------------------------------

TEST_CASE("Auto mode saves the outgoing slot's live state before applying the incoming preset", "[EightFace][auto]") {
	// applyPreset() (EightFace.cpp:418-428): "if (ctrlMode == CTRLMODE::AUTO && pPrev >= 0 &&
	// presetSlotUsed[pPrev]) { json_decref(presetSlot[pPrev]); presetSlot[pPrev] =
	// mw->toJson(); }" -- captures the bound module's CURRENT state into the slot being left,
	// before fromJson() overwrites that state with the incoming preset. This is the whole point
	// of auto mode, untested before this.
	DispatchFixture f(createEightFaceModule);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	f.m->ctrlMode = CTRLMODE::AUTO;

	// Slot 0: the outgoing slot. Saved with one marker, then the bound module is moved to a
	// second, different marker -- the live state a save-before-advance must pick up, distinct from
	// both what slot 0 was saved with and what slot 1 will apply.
	f.savePreset(0, 10);
	f.boundM->panelTheme = 20;

	// Slot 1: the incoming preset, prepared independently (savePreset() always targets slot 0).
	f.savePreset(1, 30);

	f.m->presetLoad(f.boundM, 0, false, true);
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 10);
	// Back to the live value the save-before-advance must still capture correctly on the next hop.
	f.boundM->panelTheme = 20;

	f.m->presetLoad(f.boundM, 1);
	f.h.uiFrame();

	// The incoming preset (slot 1) applied to the bound module.
	REQUIRE(f.appliedLabel() == 30);

	// The outgoing slot (0) now holds the live state from just before this load -- not its
	// original 10, and not the incoming 30. Slots store the widget-level toJson() (id/plugin/
	// model/params/data), not the module-level dataToJson() appliedLabel() reads -- panelTheme
	// lives under "data".
	json_t* savedJ = f.m->presetSlot[0];
	json_t* themeJ = json_object_get(json_object_get(savedJ, "data"), "panelTheme");
	REQUIRE(themeJ != nullptr);
	REQUIRE((int)json_integer_value(themeJ) == 20);
}


// ---- Threading -----------------------------------------------------------------------------

TEST_CASE("With a window present, no worker thread is started", "[EightFace][dispatch][threading]") {
	DispatchFixture f(createEightFaceModule);
	using WorkerState = StoermelderPackOne::GuiTaskProcessor<32>::WorkerState;

	f.savePreset(0, 12);
	f.m->presetLoad(f.boundM, 0, false, true);
	f.h.dspSteps(1000);
	REQUIRE(f.m->dispatch.guiTasks.workerState.load() == WorkerState::Absent);

	// Tasks must still drain, via uiFrame().
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 12);
}

TEST_CASE("With no window, the worker branch is taken and tasks still run", "[EightFace][dispatch][threading]") {
	DispatchFixture f(createEightFaceModule);
	f.h.setUiMode(Test::UiMode::UiAbsent);
	using WorkerState = StoermelderPackOne::GuiTaskProcessor<32>::WorkerState;

	f.savePreset(0, 13);
	f.m->presetLoad(f.boundM, 0, false, true);
	for (int i = 0; i < 10 && f.appliedLabel() != 13; i++) {
		f.h.dspSteps(1024);
	}
	REQUIRE(f.appliedLabel() == 13);
	REQUIRE(f.m->dispatch.guiTasks.workerState.load() == WorkerState::Running);
}

TEST_CASE("Closing then reopening the editor retires and restarts the dispatch worker", "[EightFace][dispatch][threading]") {
	DispatchFixture f(createEightFaceModule);
	using WorkerState = StoermelderPackOne::GuiTaskProcessor<32>::WorkerState;
	REQUIRE(f.m->dispatch.guiTasks.workerState.load() == WorkerState::Absent);

	// Editor closed: process() ticks guiTaskDivider -> dispatch.process() -> guiTasks.process(),
	// which starts the private worker once hasWindow() is false.
	f.h.setUiMode(Test::UiMode::UiAbsent);
	f.savePreset(0, 21);
	f.m->presetLoad(f.boundM, 0, false, true);
	for (int i = 0; i < 10 && f.appliedLabel() != 21; i++) {
		f.h.dspSteps(1024);
	}
	REQUIRE(f.appliedLabel() == 21);
	REQUIRE(f.m->dispatch.guiTasks.workerState.load() == WorkerState::Running);

	// Editor reopened: the next process() tick sees hasWindow() true again and retires the worker
	// rather than blocking the engine thread on a join.
	f.h.setUiMode(Test::UiMode::UiPresent);
	for (int i = 0; i < 10 && f.m->dispatch.guiTasks.workerState.load() == WorkerState::Running; i++) {
		f.h.dspSteps(1024);
	}
	REQUIRE(f.m->dispatch.guiTasks.workerState.load() != WorkerState::Running);

	// A load issued after reopening must still apply -- via uiFrame() this time, not the worker
	// that just retired.
	f.savePreset(1, 22);
	f.m->presetLoad(f.boundM, 1, false, true);
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 22);
}

TEST_CASE("Two presetLoad() calls between UI frames both apply, in order", "[EightFace][dispatch]") {
	// The historical mk1 defect this guards against is the old
	// single-slot workerGuiModuleWidget silently dropping the first of two loads issued in the
	// same UI frame -- PresetDispatch's guiTasks (GuiTaskProcessor<32>) enqueues per call and
	// captures `p` by value (PresetDispatch.hpp), so this must not happen post-migration.
	DispatchFixture f(createEightFaceModule);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

	f.savePreset(0, 31);
	f.savePreset(1, 32);

	// Both loads issued back to back, on the engine thread, with no uiFrame() between them.
	f.m->presetLoad(f.boundM, 0, false, true);
	f.m->presetLoad(f.boundM, 1, false, true);
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 2);

	// Draining applies both, in order; only the second's effect is observable in the end state,
	// but pendingGuiTasks() above already proved neither was silently coalesced or dropped.
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 32);
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 0);
}

TEST_CASE("Auto-mode with the default GUI_WITH_LOCK safe mode does not dereference an uninitialized widget pointer", "[EightFace][dispatch]") {
	// The historical mk1 defect this pins is auto-mode reading a
	// never-initialized workerModuleWidget under the *default* safe mode (GUI_WITH_LOCK), which
	// crashed on the very first preset change. Post-migration, applyPreset() resolves its target
	// via APP->scene->rack->getModule(moduleId) captured fresh per dispatch() call, so there is no
	// stale/uninitialized pointer to read; this would crash before the migration and must not
	// crash now.
	DispatchFixture f(createEightFaceModule);
	REQUIRE(f.m->dispatch.guiSafeMode == GUISAFEMODE::GUI_WITH_LOCK);
	f.m->ctrlMode = CTRLMODE::AUTO;

	f.savePreset(0, 41);
	f.savePreset(1, 42);

	f.m->presetLoad(f.boundM, 0, false, true);
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 41);

	f.m->presetLoad(f.boundM, 1);
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == 42);
}

TEST_CASE("Destroying a module with a real worker task in flight neither leaks nor crashes", "[EightFace][dispatch]") {
	// dispatch is a member of EightFaceModule (EightFace.cpp), so its destructor -- which drops the
	// shared_ptr<ITaskWorker> and, for the last reference, joins the worker thread -- runs as part
	// of EightFaceModule's own destruction. This uses a REAL MpmcTaskWorker (not SyncTaskWorker/
	// NullTaskWorker) specifically so there is an actual second thread to race against destruction;
	// run under ASan/TSan for the assertion to mean anything.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModuleWith<StoermelderPackOne::MpmcTaskWorker>};
	EightFaceModule<8>* m = mods.create("EightFace");
	m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;

	EightFaceModule<8>* boundM = mods.create("EightFace");
	EightFaceWidget* boundMw = Test::createWidget<EightFaceWidget>(boundM);
	connectForTest(m, boundM, boundMw);
	saveForTest(m, 0, boundM, boundMw, 51);
	m->presetPrev = -1;

	// Dispatches onto the real worker thread. No synchronization with it at all -- mods' teardown
	// (via ModuleScaffold's destructor, invoked when this TEST_CASE returns) deletes m immediately
	// after, so the task is racing the module's destruction exactly as advertised. A clean run
	// (join completes, no leak, no UAF) is the assertion.
	m->presetLoad(boundM, 0, false, true);

	Test::unregisterModule(boundM, boundMw);
	// m is destroyed by mods' own destructor at end of scope, immediately after the dispatch above.
}

TEST_CASE("A load enqueued while guiTasks is full is dropped, not applied, and does not corrupt state", "[EightFace][dispatch]") {
	// GuiTaskProcessor<32>::enqueue() drops silently when the ring
	// buffer is full, and PresetDispatch::dispatch() does not check that return value. Filling the
	// queue directly and confirming the drop leaves everything already queued intact.
	DispatchFixture f(createEightFaceModule);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

	int applied = 0;
	for (size_t i = 0; i < 32; i++) {
		REQUIRE(f.m->dispatch.guiTasks.enqueue([&applied]() { applied++; }) == true);
	}
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 32);

	// The 33rd task is dropped -- enqueue() returns false, and nothing already queued is disturbed.
	bool ok = f.m->dispatch.guiTasks.enqueue([&applied]() { applied++; });
	REQUIRE(ok == false);
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 32);

	f.h.uiFrame();
	// Exactly the 32 that fit ran; the dropped 33rd never did.
	REQUIRE(applied == 32);
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 0);
}

TEST_CASE("A legacy guiSafeMode == 0 (WORKER) patch still loads and dispatches through the worker", "[EightFace][JSON][dispatch]") {
	// guiSafeMode moved from a plain module field to
	// dispatch.guiSafeMode (PresetDispatch.hpp) during the dispatch migration, but the stored
	// integer values and their meaning did not change. A patch saved before the refactor (or one
	// missing the key entirely, WORKER being dataFromJson()'s absent-key fallback, see the
	// "guiSafeMode defaults to WORKER" test above) must still deserialize to the same mode and
	// actually dispatch through the worker.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");

	SECTION("Explicit guiSafeMode: 0") {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "guiSafeMode", json_integer(0));
		m->dataFromJson(rootJ);
		json_decref(rootJ);
		REQUIRE(m->dispatch.guiSafeMode == GUISAFEMODE::WORKER);
	}

	SECTION("Key absent entirely (pre-guiSafeMode legacy patch)") {
		json_t* rootJ = json_object();
		m->dataFromJson(rootJ);
		json_decref(rootJ);
		REQUIRE(m->dispatch.guiSafeMode == GUISAFEMODE::WORKER);
	}

	// Also prove it actually dispatches through the worker, not merely that the enum compares
	// equal -- guards against a future change that renumbers GUISAFEMODE without updating storage.
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	EightFaceModule<8>* m2 = mods.adopt(new EightFaceModule<8>(worker));
	m2->model = modelEightFace;
	m2->id = Test::getModuleId();
	Module::SampleRateChangeEvent e;
	e.sampleRate = Test::sampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m2->onSampleRateChange(e);

	json_t* rootJ2 = json_object();
	json_object_set_new(rootJ2, "guiSafeMode", json_integer(0));
	m2->dataFromJson(rootJ2);
	json_decref(rootJ2);

	EightFaceModule<8>* boundM = mods.create("EightFace");
	EightFaceWidget* boundMw = Test::createWidget<EightFaceWidget>(boundM);
	connectForTest(m2, boundM, boundMw);
	saveForTest(m2, 0, boundM, boundMw, 61);
	m2->presetPrev = -1;

	m2->presetLoad(boundM, 0, false, true);
	REQUIRE(worker->count == 1);

	Test::unregisterModule(boundM, boundMw);
}
