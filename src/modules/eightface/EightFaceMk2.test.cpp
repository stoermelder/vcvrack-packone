#include "../../test/framework.hpp"
#include "EightFaceMk2.cpp"
#include "EightFaceMk2Ex.cpp"

using namespace StoermelderPackOne::EightFaceMk2;
using StoermelderPackOne::ITaskWorker;
using StoermelderPackOne::SyncTaskWorker;

using StoermelderPackOne::EightFace::GUISAFEMODE;

SYNC_MODEL(modelEightFaceMk2, "EightFaceMk2");
SYNC_MODEL(modelEightFaceMk2Ex, "EightFaceMk2Ex");
Test::TestContext<> testContext;

// Test::createModule<EightFaceMk2Module<8>>() would go through modelEightFaceMk2's factory, i.e.
// the default constructor, which reaches EightFaceMk2Module::defaultWorker() and spins up a real
// MpmcTaskWorker and its thread -- wasted for tests that never route through Unsafe fast mode.
// This shadow constructs with an injected worker instead, so a test controls exactly when (or
// whether) the worker path runs. Mirrors Test::createModule<T>()'s post-construction setup (id,
// sample rate) since it bypasses the model factory that normally does this.
static EightFaceMk2Module<8>* createEightFaceMk2ModuleWithWorker(std::shared_ptr<ITaskWorker> worker) {
	auto* m = new EightFaceMk2Module<8>(std::move(worker));
	m->model = modelEightFaceMk2;
	m->id = Test::getModuleId();

	Module::SampleRateChangeEvent e;
	e.sampleRate = Test::sampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);

	return m;
}

template <typename W>
static EightFaceMk2Module<8>* createEightFaceMk2ModuleWith() {
	return createEightFaceMk2ModuleWithWorker(std::make_shared<W>());
}

// NullTaskWorker for every test that never exercises Unsafe fast mode: Safe and Unsafe apply
// through guiTasks (GuiTaskProcessor), never touching taskWorker at all.
static EightFaceMk2Module<8>* createEightFaceMk2Module() {
	return createEightFaceMk2ModuleWith<StoermelderPackOne::NullTaskWorker>();
}
// SyncTaskWorker for the Unsafe fast tests: work() runs inline on the calling thread, which
// stands in for the real worker thread here -- see FRAMEWORK.md §5, "Testing a module written
// this way". Must never be reached through a dspStep(); only ever called from the test thread,
// which is standing in for the caller a real worker would otherwise be.
static EightFaceMk2Module<8>* createEightFaceMk2ModuleWithSyncWorker() {
	return createEightFaceMk2ModuleWith<SyncTaskWorker>();
}

// SyncTaskWorker that also records how many tasks it was handed. Distinguishes "reached the
// worker" from "landed on the UI thread", which a result-only assertion cannot: the chained
// two-hop shape and the single-hop one produce the same final state, differing only in whether
// the worker ran at all.
struct CountingSyncTaskWorker : SyncTaskWorker {
	int count = 0;
	bool work(std::function<void()> task) override {
		count++;
		return SyncTaskWorker::work(std::move(task));
	}
};

// Binds `boundM`'s widget to `m` the way bindModule() would, without going through the module
// browser: registers boundMw in APP->scene->rack (so BoundModule::getModuleWidget() resolves it)
// and appends a BoundModule entry directly, optionally overriding the slug pair to land on or off
// EightFace::guiModuleSlugs.
static void bindForTest(EightFaceMk2Module<8>* m, EightFaceMk2Module<8>* boundM, EightFaceMk2Widget<8>* boundMw,
	std::string pluginSlug = "", std::string modelSlug = "") {
	Test::registerModule(boundM, boundMw);

	auto* b = new EightFaceMk2Module<8>::BoundModule;
	b->moduleId = boundM->id;
	b->pluginSlug = !pluginSlug.empty() ? pluginSlug : boundM->model->plugin->slug;
	b->modelSlug = !modelSlug.empty() ? modelSlug : boundM->model->slug;
	b->moduleName = "Test";
	auto it = StoermelderPackOne::EightFace::guiModuleSlugs.find(std::make_tuple(b->pluginSlug, b->modelSlug));
	b->needsGuiThread = it != StoermelderPackOne::EightFace::guiModuleSlugs.end();
	m->boundModules.push_back(b);
}


TEST_CASE("Construction and initialization", "[EightFaceMk2]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* mw = Test::createWidget<EightFaceMk2Widget<8>>("EightFaceMk2");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("EightFaceMk2Ex Construction and initialization", "[EightFaceMk2]") {
	Test::ModuleScaffold<EightFaceMk2ExModule<8>> mods;
	EightFaceMk2ExModule<8>* m = mods.create("EightFaceMk2Ex");
	EightFaceMk2ExWidget<8>* mw = Test::createWidget<EightFaceMk2ExWidget<8>>("EightFaceMk2Ex");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[EightFaceMk2][JSON]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	auto module = mods.create("EightFaceMk2");

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

TEST_CASE("JSON round-trip preserves presets", "[EightFaceMk2][JSON]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	// Distinctive label on EVERY slot
	for (int i = 0; i < 8; i++) {
		m->textLabel[i] = "Slot" + std::to_string(i);
	}
	m->presetSlotUsed[0] = true;
	m->presetSlotUsed[5] = true;
	// The vector owns the json_t; both modules free their own copies in their dtors.
	// Qualified: EightFaceMk2Module declares `int preset` (active slot), shadowing the base array.
	m->EightFaceMk2Base<8>::preset[0].push_back(json_pack("{s:i}", "id", 123));
	m->EightFaceMk2Base<8>::preset[5].push_back(json_pack("{s:i}", "id", 456));
	m->EightFaceMk2Base<8>::preset[5].push_back(json_pack("{s:i}", "id", 789));

	json_t* j = m->dataToJson();

	EightFaceMk2Module<8>* m2 = mods.create("EightFaceMk2");
	m2->dataFromJson(j);
	json_decref(j);

	SECTION("All slot labels") {
		for (int i = 0; i < 8; i++) {
			REQUIRE(m2->textLabel[i] == "Slot" + std::to_string(i));
		}
	}

	SECTION("Slot 0: single preset object") {
		REQUIRE(m2->presetSlotUsed[0] == true);
		REQUIRE(m2->EightFaceMk2Base<8>::preset[0].size() == 1);
		REQUIRE(json_integer_value(json_object_get(m2->EightFaceMk2Base<8>::preset[0][0], "id")) == 123);
	}

	SECTION("Slot 5: two preset objects survive in order") {
		REQUIRE(m2->presetSlotUsed[5] == true);
		REQUIRE(m2->EightFaceMk2Base<8>::preset[5].size() == 2);
		REQUIRE(json_integer_value(json_object_get(m2->EightFaceMk2Base<8>::preset[5][0], "id")) == 456);
		REQUIRE(json_integer_value(json_object_get(m2->EightFaceMk2Base<8>::preset[5][1], "id")) == 789);
	}

}


TEST_CASE("applyPreset does not decrement refcount of slot-owned json objects", "[EightFaceMk2]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	// Regression test: commit 84866bc incorrectly added json_decref(vJ) inside the GUI-thread
	// apply path. vJ pointers queued for the UI thread are owned by slot->preset -- applying a
	// preset must not touch the refcount or the preset slot's json_t* becomes a dangling pointer.

	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	// A second module instance acts as the "bound" module whose preset is being loaded.
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	bindForTest(m, boundM, boundMw);
	// process() is what establishes presetTotal/N[], which expSlot() (and so applyPreset())
	// needs; ModuleScaffold itself never steps the module.
	m->process(Test::makeProcessArgs(0));

	// Use Unsafe mode: applyPreset() calls boundMw->module->fromJson(vJ), and the dispatch runs
	// inline here because taskWorker is a NullTaskWorker and guiSafeMode != WORKER routes through
	// guiTasks.enqueue(), notcl the worker -- draining it directly with drain() keeps this test
	// about the refcount, not about which thread drains the queue.
	m->dispatch.guiSafeMode = GUISAFEMODE::GUI;

	json_t* vJ = m->toJson();
	size_t refcount = vJ->refcount;

	EightFaceMk2Slot* slot = m->faceSlot(0);
	slot->preset->push_back(vJ);
	*(slot->presetSlotUsed) = true;
	m->boundModules[0]->moduleId = boundM->id;

	m->presetPrev = -1;
	m->applyPreset(m->dispatch.allLoader(), 0);
	m->dispatch.drain();

	REQUIRE(json_typeof(vJ) == JSON_OBJECT);
	REQUIRE(vJ->refcount == refcount);

	slot->preset->clear();
	json_decref(vJ);

	// bindForTest() parented boundMw under APP->scene->rack via registerModule(); a bare
	// destroyWidget() would trip Widget's "!parent" destructor assert, so unregister first.
	// ModuleScaffold still owns deleting boundM itself (unregisterModule only un-parents it).
	Test::unregisterModule(boundM, boundMw);
}


// ---- Stability & performance mode dispatch ------------------------------------------------
// var/EightFaceMk2_threading_fix.md: the mode must select a single hop --
//   Safe / Unsafe  -> UI-thread queue (guiTasks) -> apply
//   Unsafe fast    -> worker thread (taskWorker)  -> apply
// and the execution context is the thing to assert, not just the eventual result -- a
// result-only assertion passes under the old two-hop bug too.

// A minimal target module whose fromJson()/toJson() calls are observable without any real
// modulation -- EightFaceMk2Module itself works fine as the "bound" module for this purpose,
// its slug just must not appear in EightFace::guiModuleSlugs (it doesn't).
struct DispatchFixture {
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m;
	EightFaceMk2Widget<8>* mw;
	EightFaceMk2Module<8>* boundM;
	EightFaceMk2Widget<8>* boundMw;

	explicit DispatchFixture(std::function<EightFaceMk2Module<8>*()> factory) {
		m = h.addModule<EightFaceMk2Module<8>>(factory);
		// h.uiFrame() only steps widgets added through the harness -- guiTasks.step() (m's
		// production drain path under UiPresent) is reached from m's own widget's step(), so
		// the fixture needs one, not just a widget for the bound target below.
		mw = h.addWidget<EightFaceMk2Widget<8>>(m);
		boundM = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
		boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
		Test::registerModule(boundM, boundMw);

		auto* b = new EightFaceMk2Module<8>::BoundModule;
		b->moduleId = boundM->id;
		b->pluginSlug = boundM->model->plugin->slug;
		b->modelSlug = boundM->model->slug;
		b->moduleName = "Test";
		b->needsGuiThread = false;
		m->boundModules.push_back(b);

		// presetTotal (and N[0..]) is only established inside process(), which expSlot() needs
		// for every presetLoad() call below -- one step establishes it before any test body runs.
		h.dspStep();
	}

	// boundM/boundMw were registered directly (registerModule(), not h.addWidget()) so that
	// BoundModule::getModuleWidget() can resolve them via APP->scene->rack->getModule() -- the
	// harness's own addWidget() only parents a widget under APP->scene, not APP->scene->rack.
	// That means the harness's own teardown does not know about boundMw, so it must be
	// unregistered here, before h's destructor removes boundM from the engine -- otherwise
	// boundMw survives (still parented in APP->scene->rack) until the global RackWidget is torn
	// down at process exit, when it tries to remove a module the engine no longer has.
	~DispatchFixture() {
		Test::unregisterModule(boundM, boundMw);
	}

	// Builds a one-module preset in slot 0 for `m`, addressed at boundM, marked with a
	// distinctive boxColor -- a real field EightFaceMk2Module's own dataToJson()/dataFromJson()
	// round-trip (BASE::dataToJson() has no fields of its own to piggyback on), so applying the
	// preset is observable through boundM's own JSON afterwards.
	void savePreset(const std::string& label) {
		EightFaceMk2Slot* slot = m->faceSlot(0);
		if (*(slot->presetSlotUsed)) {
			for (json_t* vJ : *slot->preset) json_decref(vJ);
			slot->preset->clear();
		}
		NVGcolor prevColor = boundM->boxColor;
		boundM->boxColor = color::fromHexString(label);
		json_t* vJ = boundM->toJson();
		boundM->boxColor = prevColor;
		slot->preset->push_back(vJ);
		*(slot->presetSlotUsed) = true;
	}

	std::string appliedLabel() {
		json_t* rootJ = boundM->dataToJson();
		json_t* colorJ = json_object_get(rootJ, "boxColor");
		std::string s = colorJ ? json_string_value(colorJ) : "";
		json_decref(rootJ);
		return s;
	}
};

TEST_CASE("Safe mode applies from the UI thread (uiFrame), not immediately", "[EightFaceMk2][dispatch]") {
	DispatchFixture f(createEightFaceMk2Module);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	f.savePreset("#010101");

	f.m->presetLoad(0, false, true);

	// Queued, not yet applied: nothing has drained guiTasks yet.
	f.h.dspStep();
	REQUIRE(f.appliedLabel() != "#010101");

	// The widget's step() drains guiTasks under UiPresent -- the production path for a plugin
	// with the editor open.
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == "#010101");
}

TEST_CASE("Unsafe mode also applies from the UI thread (uiFrame)", "[EightFaceMk2][dispatch]") {
	DispatchFixture f(createEightFaceMk2Module);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI;
	f.savePreset("#020202");

	f.m->presetLoad(0, false, true);
	f.h.dspStep();
	REQUIRE(f.appliedLabel() != "#020202");

	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == "#020202");
}

TEST_CASE("Unsafe fast mode applies through the injected worker, not the UI queue", "[EightFaceMk2][dispatch]") {
	DispatchFixture f(createEightFaceMk2ModuleWithSyncWorker);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.savePreset("#030303");

	// SyncTaskWorker::work() runs the task inline, on the calling thread -- standing in for a
	// real worker thread landing before the next line runs. This is the one place inline
	// execution is correct: presetLoad() below is the "caller" a worker would otherwise wake for,
	// not process() reached through a dspStep(), where inline execution would wrongly make the
	// task run on the engine thread it exists to escape.
	f.m->presetLoad(0, false, true);
	REQUIRE(f.appliedLabel() == "#030303");

	// Confirms the UI queue was never touched -- draining it changes nothing further.
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == "#030303");
}

TEST_CASE("needsGuiThread overrides Unsafe fast: allowlisted modules still apply via the UI queue", "[EightFaceMk2][dispatch]") {
	DispatchFixture f(createEightFaceMk2ModuleWithSyncWorker);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.m->boundModules[0]->needsGuiThread = true;
	f.savePreset("#040404");

	f.m->presetLoad(0, false, true);
	// The allowlist steers the hop in presetLoad(), so the worker is never given the task at all
	// -- a SyncTaskWorker landing synchronously must NOT have run it. (Under the earlier chained
	// shape the worker *did* run applyPreset(), which then enqueued onto guiTasks from the worker
	// thread; this assertion passed then too, so the one below is what distinguishes them.)
	REQUIRE(f.appliedLabel() != "#040404");

	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == "#040404");
}

TEST_CASE("needsGuiThread override takes the UI hop directly, without going through the worker", "[EightFaceMk2][dispatch]") {
	// The distinguishing assertion for the single-hop rule: it is not enough that an allowlisted
	// module ends up on the UI thread -- it must get there without the worker running first.
	// Chaining worker -> guiTasks would also land it on the UI thread, but it re-creates the two
	// hops var/EightFaceMk2_threading_fix.md exists to remove, and it enqueues onto guiTasks from
	// the worker thread, violating GuiTaskProcessor's single-producer contract (GuiTaskProcessor
	// .hpp: "Only the engine thread may call enqueue()").
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceMk2ModuleWithWorker(worker); });
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.m->boundModules[0]->needsGuiThread = true;
	f.savePreset("#060606");

	f.m->presetLoad(0, false, true);
	// The whole preset is the GUI part, so there is no worker task to dispatch at all.
	REQUIRE(worker->count == 0);

	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == "#060606");
	REQUIRE(worker->count == 0);
}

TEST_CASE("GUI-thread entry points apply directly instead of enqueueing", "[EightFaceMk2][dispatch][!shouldfail]") {
	// KNOWN FAILING -- pinned, not yet fixed. See var/EightFace_test_plan.md T5.
	//
	// GuiTaskProcessor is single-producer (GuiTaskProcessor.hpp: "Only the engine thread may call
	// enqueue()") -- but faceSlotCmd(SLOT_CMD::LOAD, ...) is exactly the call
	// EightFaceMk2LedButton::onButton (Shift+click) and the slot's context-menu "Load" item make,
	// both from the GUI thread, and it goes straight to presetLoad(), which enqueues onto guiTasks
	// with no thread check. If a GUI-thread caller and the engine thread (process()'s CV/auto-mode
	// advances) ever raced a push, the ring buffer's non-atomic assignment would interleave --
	// "lost tasks at best, UB at worst" per the class comment. This test does not (and cannot,
	// single-threaded) reproduce that race; it pins the weaker, checkable half: that a GUI-thread
	// call does not go through enqueue() at all, so there is nothing left to race. It currently
	// fails: faceSlotCmd(SLOT_CMD::LOAD, ...) calls presetLoad(), which enqueues like any other
	// caller. Fix: route this call site through applyPreset() directly instead.
	DispatchFixture f(createEightFaceMk2Module);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	f.savePreset("#050505");

	// The production call site (EightFaceMk2LedButton::onButton / the slot's "Load" menu item)
	// calls this exact method, on the GUI thread, standing in for the widget/menu callback.
	f.m->faceSlotCmd(SLOT_CMD::LOAD, 0);

	// Applied synchronously, with no uiFrame()/dspStep() in between -- the enqueue-then-drain
	// contract the other dispatch tests pin (see "Safe mode applies from the UI thread (uiFrame),
	// not immediately") would leave this unapplied at this point.
	REQUIRE(f.appliedLabel() == "#050505");
	// And the queue itself never received anything to drain.
	REQUIRE(f.m->dispatch.pendingGuiTasks() == 0);
}

TEST_CASE("Unsafe fast without the allowlist still takes the worker hop", "[EightFaceMk2][dispatch]") {
	// The counterpart to the case above: the allowlist check must not route *every* load to the
	// UI thread -- with no allowlisted module bound, WORKER mode still reaches the worker.
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceMk2ModuleWithWorker(worker); });
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.m->boundModules[0]->needsGuiThread = false;
	f.savePreset("#070707");

	f.m->presetLoad(0, false, true);
	REQUIRE(worker->count == 1);
	REQUIRE(f.appliedLabel() == "#070707");
}

TEST_CASE("Unsafe fast splits a mixed preset: allowlisted module to the UI, the rest to the worker", "[EightFaceMk2][dispatch]") {
	// The case that rules out both wrong answers. One bound module is allowlisted, one is not.
	//   - Routing the whole preset to the UI (one task, allowlist wins) would cost the
	//     non-allowlisted module its Unsafe fast speed -- worker->count would be 0.
	//   - Chaining (worker task that enqueues the allowlisted module onward) would apply the
	//     allowlisted module only after the worker had already run it through applyPreset().
	// The correct shape is two leaf tasks dispatched side by side from presetLoad(), each
	// covering a disjoint half of the preset. This is the only case that dispatches two --
	// see the two tests above for the single-task paths, which are the common ones.
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceMk2ModuleWithWorker(worker); });
	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;

	// A second bound target, allowlisted; f.boundM (bound by the fixture) stays off the list.
	EightFaceMk2Module<8>* guiM = f.h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	EightFaceMk2Widget<8>* guiMw = Test::createWidget<EightFaceMk2Widget<8>>(guiM);
	Test::registerModule(guiM, guiMw);
	auto* gb = new EightFaceMk2Module<8>::BoundModule;
	gb->moduleId = guiM->id;
	gb->pluginSlug = guiM->model->plugin->slug;
	gb->modelSlug = guiM->model->slug;
	gb->moduleName = "GuiTarget";
	gb->needsGuiThread = true;
	f.m->boundModules.push_back(gb);

	// A preset covering both bound modules, each with its own distinctive colour.
	EightFaceMk2Slot* slot = f.m->faceSlot(0);
	for (json_t* vJ : *slot->preset) json_decref(vJ);
	slot->preset->clear();
	NVGcolor prevWorker = f.boundM->boxColor;
	NVGcolor prevGui = guiM->boxColor;
	f.boundM->boxColor = color::fromHexString("#080808");
	guiM->boxColor = color::fromHexString("#090909");
	slot->preset->push_back(f.boundM->toJson());
	slot->preset->push_back(guiM->toJson());
	f.boundM->boxColor = prevWorker;
	guiM->boxColor = prevGui;
	*(slot->presetSlotUsed) = true;

	auto labelOf = [](EightFaceMk2Module<8>* mod) {
		json_t* rootJ = mod->dataToJson();
		json_t* colorJ = json_object_get(rootJ, "boxColor");
		std::string s = colorJ ? json_string_value(colorJ) : "";
		json_decref(rootJ);
		return s;
	};

	f.m->presetLoad(0, false, true);

	// The worker part ran inline (SyncTaskWorker) and covered ONLY the non-allowlisted module.
	REQUIRE(worker->count == 1);
	REQUIRE(labelOf(f.boundM) == "#080808");
	REQUIRE(labelOf(guiM) != "#090909");

	// The GUI part is a separate task, still waiting on guiTasks.
	f.h.uiFrame();
	REQUIRE(labelOf(guiM) == "#090909");
	// ...and draining it did not re-run the worker.
	REQUIRE(worker->count == 1);

	Test::unregisterModule(guiM, guiMw);
}

TEST_CASE("GUI vs GUI_WITH_LOCK apply through different objects", "[EightFaceMk2][dispatch]") {
	// Distinguishable by which object receives fromJson: mw->module (GUI / Unsafe) vs mw
	// (GUI_WITH_LOCK / Safe) -- observable without threading, per the verification note in
	// var/EightFaceMk2_threading_fix.md. For this module both objects end up updating the same
	// module state (ModuleWidget::fromJson itself just calls APP->engine->moduleFromJson(module,
	// ...), which calls module->fromJson()), so the two branches are asserted by exercising each
	// one distinctly rather than by a differing effect -- see the comment on each SECTION.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	bindForTest(m, boundM, boundMw);
	m->process(Test::makeProcessArgs(0));

	EightFaceMk2Slot* slot = m->faceSlot(0);
	NVGcolor prevColor = boundM->boxColor;
	boundM->boxColor = color::fromHexString("#050505");
	json_t* vJ = boundM->toJson();
	boundM->boxColor = prevColor;
	slot->preset->push_back(vJ);
	*(slot->presetSlotUsed) = true;
	m->presetPrev = -1;

	SECTION("GUI_WITH_LOCK calls mw->fromJson (widget path)") {
		m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
		m->applyPreset(m->dispatch.allLoader(), 0);
	}

	SECTION("GUI calls mw->module->fromJson (module-only path)") {
		m->dispatch.guiSafeMode = GUISAFEMODE::GUI;
		m->applyPreset(m->dispatch.allLoader(), 0);
	}

	// Both paths reach Module::fromJson() in the end, so the applied effect is the same either
	// way -- what differs (mw->fromJson() additionally taking the engine's lock) is not
	// observable without a real engine thread contending for it.
	json_t* rootJ = boundM->dataToJson();
	REQUIRE(json_string_value(json_object_get(rootJ, "boxColor")) == std::string("#050505"));
	json_decref(rootJ);

	slot->preset->clear();
	json_decref(vJ);
	// registerModule() (in bindForTest) parented boundMw under APP->scene->rack; a bare
	// destroyWidget() would trip Widget's "!parent" destructor assert, so unregister first.
	Test::unregisterModule(boundM, boundMw);
}


TEST_CASE("No window: presets still load in every mode", "[EightFaceMk2][dispatch]") {
	// The branch that used to read settings::isPlugin && !APP->window directly (deleted per
	// var/EightFaceMk2_threading_fix.md) -- GuiTaskProcessor now handles "nobody will call
	// step()" itself, starting a private worker the instant vcv::ui::hasWindow() is false. This
	// is the branch most likely to regress: presets must still load with the editor closed.

	SECTION("Safe mode") {
		DispatchFixture f(createEightFaceMk2Module);
		f.h.setUiMode(Test::UiMode::UiAbsent);
		f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
		f.savePreset("#060606");

		f.m->presetLoad(0, false, true);
		// guiTasks.process() (called from m->process() every guiTaskDivider tick) starts its own
		// private worker once hasWindow() is false; step the engine long enough for it to run.
		for (int i = 0; i < 10 && f.appliedLabel() != "#060606"; i++) {
			f.h.dspSteps(256);
		}
		REQUIRE(f.appliedLabel() == "#060606");
	}

	SECTION("Unsafe fast mode") {
		DispatchFixture f(createEightFaceMk2ModuleWithSyncWorker);
		f.h.setUiMode(Test::UiMode::UiAbsent);
		f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
		f.savePreset("#070707");

		f.m->presetLoad(0, false, true);
		REQUIRE(f.appliedLabel() == "#070707");
	}
}

TEST_CASE("No window, Unsafe fast: the GUI half is not dispatched at all", "[EightFaceMk2][dispatch]") {
	// The counterpart to the case above, and the reason the split needs a window check. guiTasks
	// guarantees only "off the engine thread": once hasWindow() is false it drains from its own
	// private worker (GuiTaskProcessor.hpp), so enqueueing the GUI half there would load those
	// modules off the UI thread anyway -- the crash EightFace::guiModuleSlugs exists to prevent.
	// So the half is not queued, and its modules stay unloaded; the worker half still applies
	// the rest of the preset.
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceMk2ModuleWithWorker(worker); });
	f.h.setUiMode(Test::UiMode::UiAbsent);

	// A second bound target, allowlisted; f.boundM (bound by the fixture) stays off the list.
	EightFaceMk2Module<8>* guiM = f.h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	EightFaceMk2Widget<8>* guiMw = Test::createWidget<EightFaceMk2Widget<8>>(guiM);
	Test::registerModule(guiM, guiMw);
	auto* gb = new EightFaceMk2Module<8>::BoundModule;
	gb->moduleId = guiM->id;
	gb->pluginSlug = guiM->model->plugin->slug;
	gb->modelSlug = guiM->model->slug;
	gb->moduleName = "GuiTarget";
	gb->needsGuiThread = true;
	f.m->boundModules.push_back(gb);

	// A preset covering both bound modules, each with its own distinctive colour.
	EightFaceMk2Slot* slot = f.m->faceSlot(0);
	for (json_t* vJ : *slot->preset) json_decref(vJ);
	slot->preset->clear();
	NVGcolor prevOk = f.boundM->boxColor;
	NVGcolor prevGui = guiM->boxColor;
	f.boundM->boxColor = color::fromHexString("#0a0a0a");
	guiM->boxColor = color::fromHexString("#0b0b0b");
	slot->preset->push_back(f.boundM->toJson());
	slot->preset->push_back(guiM->toJson());
	f.boundM->boxColor = prevOk;
	guiM->boxColor = prevGui;
	*(slot->presetSlotUsed) = true;

	auto labelOf = [](EightFaceMk2Module<8>* mod) {
		json_t* rootJ = mod->dataToJson();
		json_t* colorJ = json_object_get(rootJ, "boxColor");
		std::string s = colorJ ? json_string_value(colorJ) : "";
		json_decref(rootJ);
		return s;
	};

	f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	f.m->presetLoad(0, false, true);

	// The split still goes out as usual -- the window check is not in the dispatch.
	REQUIRE(worker->count == 1);

	// The ordinary module loaded; the allowlisted one was skipped by applyPreset() rather than
	// applied with no UI thread to apply it on. Draining guiTasks is what runs the GUI half, so
	// this asserts the skip happened inside it, not that it was never queued.
	f.m->dispatch.drain();
	REQUIRE(labelOf(f.boundM) == "#0a0a0a");
	REQUIRE(labelOf(guiM) != "#0b0b0b");

	Test::unregisterModule(guiM, guiMw);
}

TEST_CASE("No window: an allowlisted module is skipped in every mode", "[EightFaceMk2][dispatch]") {
	// The skip lives in applyPreset(), so it holds for every mode and every dispatch shape --
	// including Safe/Unsafe, which route the whole preset through guiTasks with no split to hang
	// a check on. That was the crashing path: with no window guiTasks drains from its own
	// private worker (GuiTaskProcessor.hpp), so an allowlisted module was being loaded off the
	// UI thread, exactly what EightFace::guiModuleSlugs exists to prevent.
	//
	// Here the module under test has ONE bound module and it is allowlisted, so there is no
	// ordinary half to mask a mistake: nothing may be applied, in any mode. The preset not
	// loading is the deliberate trade -- it contradicts "presets must still load in every mode"
	// from var/EightFaceMk2_threading_fix.md's verification list for this configuration, and is
	// preferred over applying the module with no UI thread to apply it on.
	auto worker = std::make_shared<CountingSyncTaskWorker>();
	DispatchFixture f([&]() { return createEightFaceMk2ModuleWithWorker(worker); });
	f.h.setUiMode(Test::UiMode::UiAbsent);
	f.m->boundModules[0]->needsGuiThread = true;
	f.savePreset("#0c0c0c");

	SECTION("Unsafe fast") {
		f.m->dispatch.guiSafeMode = GUISAFEMODE::WORKER;
	}
	SECTION("Safe -- the path that crashed before the skip existed") {
		f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	}
	SECTION("Unsafe") {
		f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI;
	}

	f.m->presetLoad(0, false, true);
	f.m->dispatch.drain();
	REQUIRE(f.appliedLabel() != "#0c0c0c");
}


TEST_CASE("Queue capacity: more than 8 bound modules load without drops", "[EightFaceMk2][dispatch]") {
	// GuiTaskProcessor<SIZE> defaults to 8; the module uses GuiTaskProcessor<32>, matching the
	// capacity of the queue it replaces. A preset touching more than 8 bound modules must not
	// silently lose any of them -- the regression GuiTaskProcessor<8> would introduce (enqueue()
	// drops when full).
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	// h.uiFrame() only steps widgets added through the harness -- guiTasks.step() is reached
	// from m's own widget's step(), so a widget for m is needed, not just for the bound targets.
	h.addWidget<EightFaceMk2Widget<8>>(m);
	m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

	const int N = 12;
	std::vector<EightFaceMk2Module<8>*> boundMs;
	std::vector<EightFaceMk2Widget<8>*> boundMws;

	EightFaceMk2Slot* slot = m->faceSlot(0);
	for (int i = 0; i < N; i++) {
		EightFaceMk2Module<8>* boundM = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
		EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
		Test::registerModule(boundM, boundMw);
		boundMs.push_back(boundM);
		boundMws.push_back(boundMw);

		auto* b = new EightFaceMk2Module<8>::BoundModule;
		b->moduleId = boundM->id;
		b->pluginSlug = boundM->model->plugin->slug;
		b->modelSlug = boundM->model->slug;
		b->moduleName = "Test";
		b->needsGuiThread = false;
		m->boundModules.push_back(b);

		// A distinct boxColor per module is the marker: a real field EightFaceMk2Module's own
		// dataToJson()/dataFromJson() round-trip, so applying the preset is observable afterwards.
		NVGcolor prevColor = boundM->boxColor;
		boundM->boxColor = nvgRGB(i, i, i);
		json_t* vJ = boundM->toJson();
		boundM->boxColor = prevColor;
		slot->preset->push_back(vJ);
	}
	*(slot->presetSlotUsed) = true;
	m->presetPrev = -1;

	// process() establishes presetTotal/N[], which expSlot() (and so presetLoad()) needs.
	m->process(Test::makeProcessArgs(0));
	m->presetLoad(0, false, true);
	h.dspStep();
	h.uiFrame();

	for (int i = 0; i < N; i++) {
		json_t* rootJ = boundMs[i]->dataToJson();
		json_t* colorJ = json_object_get(rootJ, "boxColor");
		REQUIRE(colorJ != nullptr);
		CHECK(json_string_value(colorJ) == color::toHexString(nvgRGB(i, i, i)));
		json_decref(rootJ);
	}

	// Each boundMw was registered directly (registerModule(), not h.addWidget()) so
	// getModuleWidget() could resolve it via APP->scene->rack->getModule() -- the harness's own
	// teardown does not know about them, so unregister here, before h's destructor removes each
	// boundM from the engine (see DispatchFixture's destructor for the same reasoning).
	for (int i = 0; i < N; i++) {
		Test::unregisterModule(boundMs[i], boundMws[i]);
	}
}


// ---- Auto mode --------------------------------------------------------------------------------

TEST_CASE("Auto mode saves the outgoing slot's live state before applying the incoming preset", "[EightFaceMk2][auto]") {
	// applyPreset() (EightFaceMk2.cpp:613): "if (BASE::ctrlMode == CTRLMODE::AUTO && slotPrev &&
	// *slotPrev->presetSlotUsed) { ... (*slotPrev->preset)[i] = mw->toJson(); }" -- captures the
	// bound module's CURRENT state into the slot being left, before fromJson() overwrites that
	// state with the incoming preset. This is the whole point of auto mode (manual: presets
	// update automatically as you tweak the bound modules) and had no test before this one.
	DispatchFixture f(createEightFaceMk2Module);
	f.m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
	// ctrlMode is not a free-standing field: process() re-derives it from PARAM_RW every call
	// (EightFaceMk2.cpp:256, "CTRLMODE ctrlMode = (CTRLMODE)Module::params[PARAM_RW].getValue()")
	// and overwrites BASE::ctrlMode with it. Setting the member directly is silently clobbered
	// back to Read on the next process(); the switch position is the actual source of truth.
	f.m->params[EightFaceMk2Module<8>::PARAM_RW].setValue((float)CTRLMODE::AUTO);

	// Slot 0: the outgoing slot. Saved with one color, then the bound module is moved to a second,
	// different color -- the live state a save-before-advance must pick up, distinct from both what
	// slot 0 was saved with and what slot 1 will apply.
	f.savePreset("#101010");
	f.boundM->boxColor = color::fromHexString("#202020");

	// Slot 1: the incoming preset, prepared independently (savePreset() always targets slot 0).
	EightFaceMk2Slot* slot1 = f.m->faceSlot(1);
	NVGcolor prevColor = f.boundM->boxColor;
	f.boundM->boxColor = color::fromHexString("#303030");
	json_t* vJ = f.boundM->toJson();
	f.boundM->boxColor = prevColor;
	slot1->preset->push_back(vJ);
	*(slot1->presetSlotUsed) = true;

	// process() establishes presetTotal/N[] (and applies the PARAM_RW switch above to
	// BASE::ctrlMode), and presetLoad(0, ..., force) is what sets preset/presetPrev the way the
	// constructor leaves them (-1) would not otherwise reach slot 0 -> 1.
	f.m->process(Test::makeProcessArgs(0));
	f.m->presetLoad(0, false, true);
	f.h.uiFrame();
	REQUIRE(f.appliedLabel() == "#101010");
	// Back to the live value the save-before-advance must still capture correctly on the next hop.
	f.boundM->boxColor = color::fromHexString("#202020");

	f.m->presetLoad(1);
	f.h.uiFrame();

	// The incoming preset (slot 1) applied to the bound module.
	REQUIRE(f.appliedLabel() == "#303030");

	// The outgoing slot (0) now holds the live state from just before this load -- not its
	// original "#101010", and not the incoming "#303030". Slots store the widget-level toJson()
	// (id/plugin/model/params/data, same shape as savePreset()/appliedLabel() unwrap via "data"),
	// not the module-level dataToJson() appliedLabel() reads -- boxColor lives under "data".
	json_t* savedJ = f.m->faceSlot(0)->preset->at(0);
	json_t* colorJ = json_object_get(json_object_get(savedJ, "data"), "boxColor");
	REQUIRE(colorJ != nullptr);
	REQUIRE(json_string_value(colorJ) == std::string("#202020"));
}
