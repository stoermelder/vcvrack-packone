#pragma once

// Shared preamble and helpers for the EightFace (mk1) test suite.
// Included by EightFace.test.cpp, which stitches in each EightFace.test.<area>.hpp inside its own
// namespace -- one compiled binary (see plugin-test.mk, which only discovers *.test.cpp), split
// across files so no single one grows unbounded. Mirrors EightFaceMk2's split (EightFaceMk2.test.hpp
// + EightFaceMk2.test.<area>.hpp, stitched by EightFaceMk2.test.cpp).

#include "../../test/framework.hpp"
#include "EightFace.cpp"

using namespace StoermelderPackOne::EightFace;
using StoermelderPackOne::ITaskWorker;
using StoermelderPackOne::SyncTaskWorker;

SYNC_MODEL(modelEightFace, "EightFace");
SYNC_MODEL(modelEightFaceX2, "EightFaceX2");
Test::TestContext<> testContext;

// Test::createModule<EightFaceModule<8>>() would go through modelEightFace's factory, i.e. the
// default constructor, which reaches EightFaceModule::defaultWorker() and spins up a real
// MpmcTaskWorker and its thread -- wasted for tests that never route through Unsafe fast mode.
// This shadow constructs with an injected worker instead, so a test controls exactly when (or
// whether) the worker path runs. Mirrors Test::createModule<T>()'s post-construction setup (id,
// sample rate) since it bypasses the model factory that normally does this.
static EightFaceModule<8>* createEightFaceModuleWithWorker(std::shared_ptr<ITaskWorker> worker) {
	auto* m = new EightFaceModule<8>(std::move(worker));
	m->model = modelEightFace;
	m->id = Test::getModuleId();

	Module::SampleRateChangeEvent e;
	e.sampleRate = Test::sampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);

	return m;
}

template <typename W>
static EightFaceModule<8>* createEightFaceModuleWith() {
	return createEightFaceModuleWithWorker(std::make_shared<W>());
}

// NullTaskWorker for every test that never exercises Unsafe fast mode: Safe and Unsafe apply
// through dispatch.guiTasks (GuiTaskProcessor), never touching the worker at all.
static EightFaceModule<8>* createEightFaceModule() {
	return createEightFaceModuleWith<StoermelderPackOne::NullTaskWorker>();
}
// SyncTaskWorker for the Unsafe fast tests: work() runs inline on the calling thread, which
// stands in for the real worker thread here -- see FRAMEWORK.md §5, "Testing a module written
// this way". Must never be reached through a dspStep(); only ever called from the test thread,
// which is standing in for the caller a real worker would otherwise be.
static EightFaceModule<8>* createEightFaceModuleWithSyncWorker() {
	return createEightFaceModuleWith<SyncTaskWorker>();
}

// SyncTaskWorker that also records how many tasks it was handed. Distinguishes "reached the
// worker" from "landed on the UI thread", which a result-only assertion cannot.
struct CountingSyncTaskWorker : SyncTaskWorker {
	int count = 0;
	bool work(std::function<void()> task) override {
		count++;
		return SyncTaskWorker::work(std::move(task));
	}
};

// Wires `boundM`/`boundMw` as m's expander on the side `m->side` reads from -- the "physically
// plugged in" case presetLoad()'s connected-module lookups need -- and fills in the slug/name
// fields presetSave() would normally derive from the bound module's Model, without going through
// the GUI's connection scan. boundMw is registered directly (Test::registerModule, not through a
// Harness) so applyPreset()'s APP->scene->rack->getModule(moduleId) resolves it; callers must
// Test::unregisterModule(boundM, boundMw) before boundM is destroyed.
static void connectForTest(EightFaceModule<8>* m, EightFaceModule<8>* boundM, EightFaceWidget* boundMw) {
	Test::registerModule(boundM, boundMw);
	Module::Expander& exp = m->side == SIDE::LEFT ? m->leftExpander : m->rightExpander;
	exp.moduleId = boundM->id;
	exp.module = boundM;
	m->pluginSlug = boundM->model->plugin->name;
	m->modelSlug = boundM->model->name;
	m->moduleName = boundM->model->plugin->brand + " " + boundM->model->name;
	m->realPluginSlug = boundM->model->plugin->slug;
	m->realModelSlug = boundM->model->slug;
	auto it = StoermelderPackOne::EightFace::guiModuleSlugs.find(std::make_tuple(m->realPluginSlug, m->realModelSlug));
	m->needsGuiThread = it != StoermelderPackOne::EightFace::guiModuleSlugs.end();
}

// Builds slot `p`'s payload from `boundM`'s current state (widget-level toJson(), same shape
// presetSave() itself captures via APP->scene->rack->getModule(m->id)->toJson()), marked with a
// distinctive panelTheme -- a real field EightFaceModule's own dataToJson()/dataFromJson() round
// trip, so applying the preset is observable afterwards through boundM's own dataToJson().
static void saveForTest(EightFaceModule<8>* m, int p, EightFaceModule<8>* boundM, EightFaceWidget* boundMw, int themeMarker) {
	if (m->presetSlotUsed[p]) json_decref(m->presetSlot[p]);
	int prevTheme = boundM->panelTheme;
	boundM->panelTheme = themeMarker;
	json_t* vJ = boundMw->toJson();
	boundM->panelTheme = prevTheme;
	m->presetSlot[p] = vJ;
	m->presetSlotUsed[p] = true;
}

static int appliedTheme(EightFaceModule<8>* boundM) {
	return boundM->panelTheme;
}
