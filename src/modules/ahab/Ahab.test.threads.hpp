#pragma once
#include "Ahab.test.widget.hpp"
#include <atomic>
#include <chrono>
#include <thread>

// Two-thread stress test: a real DSP thread runs process() while the test thread plays the UI.
// Every other test in this binary drives both roles from one thread, so none of them can see a
// data race between the two. In the default (ASan) run it only has to not crash and not
// deadlock; the real assertion is the thread sanitizer's report, which must show nothing but
// tolerated dirty reads (see docs/ahab/Ahab_threading.md):
//
//   rm -rf build/test && make -j8 DEBUGPLUGIN=1 SANITIZER=thread testrun-one NAME=Ahab FILTER='[threads]'
//
// What the UI side exercises:
//  - the widget's display copy (field and mark buffer) while the DSP reallocates them
//  - undo/redo history: the UI pushes on every edit, the DSP pops in undo()/redo()
//  - field resize and whole-field replacement (reallocation)
//  - dataToJson(), which reads the live field
//  - creating and destroying a second module while the first plays

namespace {
template <typename F>
int ahabRunForMs(int ms, F fn) {
	auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	int n = 0;
	while (std::chrono::steady_clock::now() < end) {
		fn(n);
		n++;
	}
	return n;
}
}

TEST_CASE("Ahab Threads: DSP process() against UI display copy, edits, undo, resize and save", "[threads][ahab]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	AhabSimWidget* simWidget = mw->simWidget;
	Test::ModuleScaffold<AhabModule> extra;

	// Content that moves every tick: wandering E operators and a bang-driven field.
	Usz outH, outW;
	REQUIRE(m->sim->loadRectFromOrcaRequest("...........\n.E....E....\n...........\n.<.....>...\n...........", 0, 0, outH, outW, true));
	m->params[AhabModule::BPM_PARAM].setValue(300.f);
	m->simRunning = true;

	const float sampleRate = Test::sampleRate();
	int64_t frame = 0;
	m->process(Test::makeProcessArgs(frame++, sampleRate));

	constexpr int DURATION_MS = 2000;
	rack::Context* ctx = rack::contextGet();
	std::atomic<bool> stop{false};
	std::atomic<int64_t> dspBlocks{0};

	// DSP role. Rack's APP is thread_local, so the context has to be set on this thread too.
	std::thread dsp([&]() {
		rack::contextSet(ctx);
		int64_t f = frame;
		int64_t n = 0;
		while (!stop.load(std::memory_order_relaxed)) {
			m->process(Test::makeProcessArgs(f++, sampleRate));
			n++;
			dspBlocks.store(n, std::memory_order_relaxed);
		}
		rack::contextSet(nullptr);
	});

	// UI role, on the test thread.
	int iterations = ahabRunForMs(DURATION_MS, [&](int n) {
		simWidget->takeDisplaySnapshot();

		// Burst of undoable edits, then alternate undo/redo.
		for (int i = 0; i < 3; i++) {
			m->sim->setGlyphRequest((Usz)((n + i) % 5), (Usz)((n * 3 + i) % 11), (Glyph)('A' + (n + i) % 26), Mark_flag_input, true);
		}
		if (n % 2 == 0) m->sim->undoRequest();
		else m->sim->redoRequest();

		if (n % 20 == 0) {
			// Reallocation: resize between two sizes, and replace the whole field with another.
			if ((n / 20) % 2 == 0) m->sim->setFieldSizeRequest(60, 80, true);
			else m->sim->setFieldSizeRequest(5, 11, true);
		}
		if (n % 20 == 10) {
			Usz oh, ow;
			if ((n / 20) % 2 == 0) m->sim->loadRectFromOrcaRequest("E.......\n.<.....>\n........", 0, 0, oh, ow, true);
			else m->sim->loadRectFromOrcaRequest(".E.\n...\n.<.\n...\n...\n...\n...\n...\n...\n...\n...\n...", 0, 0, oh, ow, true);
		}
		if (n % 50 == 0) {
			json_t* rootJ = m->dataToJson();
			json_decref(rootJ);
		}
		if (n % 50 == 25) {
			// A second module appears and disappears while the first one plays.
			Test::ModuleScaffold<AhabModule> scratch;
			scratch.create("Ahab");
		}
	});

	stop = true;
	dsp.join();

	// A silent test proves nothing: both sides must actually have run a lot.
	CATCH_INFO("ui iterations " << iterations << ", dsp blocks " << dspBlocks.load());
	REQUIRE(iterations > 100);
	REQUIRE(dspBlocks.load() > 10000);
}
