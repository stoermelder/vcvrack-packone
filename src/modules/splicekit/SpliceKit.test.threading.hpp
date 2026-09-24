// SpliceKit under Test::Harness in UiPresent mode.
//
// SpliceKit is the plugin's only GuiTaskProcessor user, so it's the only place the
// engine->GUI handoff gets tested. Normally APP->window is null in tests, forcing the
// worker-thread branch (a real background thread racing the test thread). The rest of the
// suite avoids this via SpliceKit.test.hpp's createModule() shadow, which sets
// taskProcessorUi.syncMode = true and skips both branches entirely.
//
// This file instead mocks vcv::ui::hasWindow() to return true, so process() takes the
// window-present branch: tasks queue on the engine thread and drain via the widget's
// step() (as Harness::uiFrame() runs it) — the real production path, no syncMode, no
// worker thread. It deliberately skips SpliceKit.test.hpp since that's the exact
// shortcut being tested around here.

#include "SpliceKit.test.hpp"


// Creates a SpliceKit module WITHOUT syncMode, so its real GuiTaskProcessor branching runs.
static SpliceKitModule* createLiveModule() {
	return Test::createModule<SpliceKitModule>("SpliceKit");
}


TEST_CASE("No worker thread is started when a window is present", "[SpliceKit]") {
	// The central claim. In UiPresent mode the module must take the same branch it takes in a
	// real Rack with the editor open: no background thread at all.
	Test::Harness h(Test::UiMode::UiPresent);
	auto* m = h.addModule<SpliceKitModule>(createLiveModule);

	// Drive the module's own process(), which is what calls taskProcessorUi.process() (behind a
	// 256-sample divider) — the real call site, not a stand-in.
	h.dspSteps(1000);

	REQUIRE(m->taskProcessorUi.workerState.load() == GuiTaskProcessor<16>::WorkerState::Absent);
	REQUIRE_FALSE(m->taskProcessorUi.isWorkerThread());
}


TEST_CASE("With no window, the worker branch is taken instead", "[SpliceKit]") {
	// The contrast case, confirming the mode actually selects the branch rather than the module
	// simply never starting a worker. This is the branch every existing test would have taken
	// had syncMode not been suppressing it.
	Test::Harness h(Test::UiMode::UiAbsent);
	auto* m = h.addModule<SpliceKitModule>(createLiveModule);

	h.dspSteps(1000);

	// Started (or starting) — not Absent.
	auto state = m->taskProcessorUi.workerState.load();
	REQUIRE(state != GuiTaskProcessor<16>::WorkerState::Absent);

	// Shut it down before the module is destroyed, so this case does not leave a thread racing
	// the harness teardown. (The destructor does this anyway; doing it explicitly keeps the
	// test's intent clear and the failure mode local if it ever stops working.)
	m->taskProcessorUi.stopWorker();
	REQUIRE(m->taskProcessorUi.workerState.load() == GuiTaskProcessor<16>::WorkerState::Absent);
}


TEST_CASE("Closing and reopening the editor retires and restarts the worker", "[SpliceKit]") {
	// The transition GuiTaskProcessor has explicit machinery for (retireWorker(), and
	// startWorker()'s Retiring -> Starting reclaim path) and which nothing could exercise
	// before: it needs the window to *change* between present and absent while a module runs,
	// which is impossible while the answer comes from an APP->window that is permanently null.
	Test::Harness h(Test::UiMode::UiPresent);
	auto* m = h.addModule<SpliceKitModule>(createLiveModule);

	h.dspSteps(1000);
	REQUIRE(m->taskProcessorUi.workerState.load() == GuiTaskProcessor<16>::WorkerState::Absent);

	// Editor closed: the next engine tick starts a worker.
	h.setUiMode(Test::UiMode::UiAbsent);
	h.dspSteps(1000);
	REQUIRE(m->taskProcessorUi.workerState.load() != GuiTaskProcessor<16>::WorkerState::Absent);

	// Editor reopened: the worker is asked to retire (not joined on the engine thread).
	h.setUiMode(Test::UiMode::UiPresent);
	h.dspSteps(1000);
	auto afterReopen = m->taskProcessorUi.workerState.load();
	REQUIRE((afterReopen == GuiTaskProcessor<16>::WorkerState::Retiring
	      || afterReopen == GuiTaskProcessor<16>::WorkerState::Absent));

	// Closed again: the retired-but-unjoined worker is reclaimed rather than leaked.
	h.setUiMode(Test::UiMode::UiAbsent);
	h.dspSteps(1000);
	REQUIRE(m->taskProcessorUi.workerState.load() != GuiTaskProcessor<16>::WorkerState::Absent);

	m->taskProcessorUi.stopWorker();
	REQUIRE(m->taskProcessorUi.workerState.load() == GuiTaskProcessor<16>::WorkerState::Absent);
}


TEST_CASE("A task queued by the engine thread drains on the UI frame", "[SpliceKit]") {
	Test::Harness h(Test::UiMode::UiPresent);
	auto* m = h.addModule<SpliceKitModule>(createLiveModule);
	auto* mw = h.addWidget<SpliceKitWidget>(m);
	REQUIRE(mw != nullptr);

	int ran = 0;
	REQUIRE(m->taskProcessorUi.enqueue([&]() { ran++; }));

	// Many DSP steps, no UI frame: the task must still be pending. This is the rate gap made
	// concrete — in production ~735 process() calls happen before the UI gets a turn, and
	// nothing may run the task in the meantime.
	h.dspSteps(500);
	REQUIRE(ran == 0);

	// The UI frame runs SpliceKitWidget::step(), which drains taskProcessorUi.
	h.uiFrame();
	REQUIRE(ran == 1);
}


TEST_CASE("The widget step() drain path keeps portHasCable fresh", "[SpliceKit]") {
	// The asymmetry this file was written to check. When a worker is draining, SpliceKit keeps
	// portHasCable[]/peerConnected[] current via taskProcessorUi.onWorkerDrained. On the
	// window-present path there is no worker, so the *widget's* step() must do that work
	// instead (SpliceKit.cpp:2435-2437). Under syncMode neither path runs, so this pairing has
	// never been verified — a regression in either half would have been invisible.
	Test::Harness h(Test::UiMode::UiPresent);
	auto* m = h.addModule<SpliceKitModule>(createLiveModule);
	auto* mw = h.addWidget<SpliceKitWidget>(m);
	REQUIRE(mw != nullptr);

	// A UI frame must run the refresh without crashing. refreshPortHasCable() walks
	// APP->scene->rack's widget tree, so this also confirms the harness's scene is well-formed
	// enough for the real production scan to run headless.
	REQUIRE_NOTHROW(h.uiFrames(5));

	// With no cables anywhere, every cell reports no cable — a definite state, not a stale one.
	for (int i = 0; i < MATRIX_COUNT; i++) {
		REQUIRE_FALSE(m->portHasCable[i]);
	}
}


TEST_CASE("The queue overflows across the rate gap", "[SpliceKit]") {
	// Bug class 4 from the Phase 2 design, now expressible. taskProcessorUi is a
	// GuiTaskProcessor<16>: a fixed-size single-producer ring. At 44.1kHz/60Hz the engine gets
	// ~735 process() calls between UI frames, so anything that enqueues at even a modest rate
	// overflows long before the UI drains — and enqueue() reports the drop rather than growing.
	Test::Harness h(Test::UiMode::UiPresent);
	h.setSampleRate(44100.f);
	h.setFrameRate(60.0);
	auto* m = h.addModule<SpliceKitModule>(createLiveModule);
	auto* mw = h.addWidget<SpliceKitWidget>(m);
	REQUIRE(mw != nullptr);

	int ran = 0;
	int accepted = 0;
	int dropped = 0;
	for (int i = 0; i < 40; i++) {
		if (m->taskProcessorUi.enqueue([&]() { ran++; })) accepted++;
		else dropped++;
	}

	// 16 slots, so 16 accepted and the remaining 24 dropped — reported, not silently lost.
	REQUIRE(accepted == 16);
	REQUIRE(dropped == 24);
	REQUIRE(ran == 0);

	h.uiFrame();
	REQUIRE(ran == 16);

	// And the queue is usable again once drained.
	REQUIRE(m->taskProcessorUi.enqueue([&]() { ran++; }));
	h.uiFrame();
	REQUIRE(ran == 17);
}


TEST_CASE("A full interleaved run is stable", "[SpliceKit]") {
	// The end-to-end shape a module actually sees: both clocks running at their true relative
	// rates, with the module's own process() driving the engine-side tick. This is the closest
	// the suite gets to "SpliceKit running in Rack for a second", and it is fully deterministic.
	Test::Harness h(Test::UiMode::UiPresent);
	h.setSampleRate(44100.f);
	h.setFrameRate(60.0);

	auto* m = h.addModule<SpliceKitModule>(createLiveModule);
	auto* mw = h.addWidget<SpliceKitWidget>(m);
	REQUIRE(mw != nullptr);

	REQUIRE_NOTHROW(h.run(Test::seconds(1.0)));

	REQUIRE(h.uiFrameCount == 60);
	REQUIRE(h.frame == 44100);
	// Still no worker after a full simulated second on the window-present path.
	REQUIRE(m->taskProcessorUi.workerState.load() == GuiTaskProcessor<16>::WorkerState::Absent);
}
