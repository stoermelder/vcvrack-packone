#pragma once

// Shared preamble and helpers for the EightFaceMk2 test suite.
// Included by EightFaceMk2.test.cpp, which stitches in each EightFaceMk2.test.<area>.hpp inside
// its own namespace -- one compiled binary (see plugin-test.mk, which only discovers *.test.cpp),
// split across files so no single one grows unbounded. Mirrors SpliceKit's split
// (SpliceKit.test.hpp + SpliceKit.test.<area>.hpp, stitched by SpliceKit.test.cpp).

#include "../../test/framework.hpp"
#include "EightFaceMk2.cpp"
#include "EightFaceMk2Ex.cpp"

using namespace StoermelderPackOne::EightFace::mk2;
using StoermelderPackOne::ITaskWorker;
using StoermelderPackOne::SyncTaskWorker;

using StoermelderPackOne::EightFace::GUISAFEMODE;

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