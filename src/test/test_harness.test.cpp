// The harness is framework code that every later UI test will rest on, so its own scheduling
// arithmetic — the DSP:UI ratio, the fractional carry, the interleaving order — needs to be
// pinned down here rather than debugged through a failing module test later.
//
// Uses Stroke as the module under test wherever a real module is needed: it is a plain
// ThemedModuleWidget with no expanders, no background threads and no process-wide registration,
// so a failure here is a harness failure and not a module's.

#include "../test/framework.hpp"
#include "../utils/GuiTaskProcessor.hpp"
#include "../modules/stroke/Stroke.cpp"

using namespace rack;
using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Stroke;

// Stroke is registered with PORTS=10 (see Stroke.cpp's createModel call).
static constexpr int STROKE_PORTS = 10;

SYNC_MODEL(modelStroke, "Stroke");
Test::TestContext<> testContext;


// A module that records the schedule it was driven with, so the interleaving is observable
// without depending on any real module's behaviour.
struct ScheduleProbe : rack::Module {
	int processCalls = 0;
	int64_t lastFrame = -1;
	float lastSampleRate = 0.f;

	// The DSP frame at which each UI frame was observed — filled by a uiFrameHook, so the gaps
	// between entries are exactly the DSP:UI ratio under test.
	std::vector<int64_t> uiFrameAtDspFrame;

	ScheduleProbe() { config(0, 0, 0, 0); }

	void process(const ProcessArgs& args) override {
		processCalls++;
		lastFrame = args.frame;
		lastSampleRate = args.sampleRate;
	}
};


TEST_CASE("DSP stepping") {
	Test::Harness h;
	auto* probe = h.adoptModule(new ScheduleProbe);

	SECTION("dspStep runs process() once and advances the frame counter") {
		REQUIRE(h.frame == 0);
		h.dspStep();
		REQUIRE(probe->processCalls == 1);
		REQUIRE(probe->lastFrame == 0);
		REQUIRE(h.frame == 1);
	}

	SECTION("dspSteps runs process() exactly N times with consecutive frames") {
		h.dspSteps(100);
		REQUIRE(probe->processCalls == 100);
		REQUIRE(probe->lastFrame == 99);
		REQUIRE(h.frame == 100);
	}

	SECTION("process() is stepped at the engine's own sample rate") {
		// A4's single source of truth: the rate a module is stepped at must be the rate it was
		// configured for, even when a test changes it.
		h.dspStep();
		REQUIRE(probe->lastSampleRate == Catch::Approx(APP->engine->getSampleRate()));
	}

	SECTION("modules are stepped in registration order") {
		std::vector<int> order;
		struct OrderProbe : rack::Module {
			std::vector<int>* order;
			int id;
			OrderProbe(std::vector<int>* o, int id) : order(o), id(id) { config(0, 0, 0, 0); }
			void process(const ProcessArgs&) override { order->push_back(id); }
		};
		Test::Harness h2;
		h2.adoptModule(new OrderProbe(&order, 1));
		h2.adoptModule(new OrderProbe(&order, 2));
		h2.adoptModule(new OrderProbe(&order, 3));

		h2.dspStep();
		REQUIRE(order == std::vector<int>{1, 2, 3});
	}
}


TEST_CASE("Expander message flipping matches the real engine") {
	// Carried over from SimpleEngine (Phase 1's A2 fix), and re-asserted here because Harness
	// is meant to replace it: a module that forgets requestMessageFlip() must stay broken under
	// the harness exactly as it is in Rack. Flipping unconditionally is the one failure mode
	// that makes a test *more* permissive than production.
	struct ExpanderProbe : rack::Module {
		bool requestFlip = false;
		ExpanderProbe() {
			config(0, 0, 0, 0);
			leftExpander.producerMessage = &a;
			leftExpander.consumerMessage = &b;
		}
		int a = 1, b = 2;
		void process(const ProcessArgs&) override {
			if (requestFlip) leftExpander.requestMessageFlip();
		}
	};

	Test::Harness h;
	auto* m = h.adoptModule(new ExpanderProbe);
	void* originalProducer = m->leftExpander.producerMessage;

	SECTION("no flip is performed when the module never requests one") {
		h.dspSteps(10);
		REQUIRE(m->leftExpander.producerMessage == originalProducer);
	}

	SECTION("a requested flip happens once and clears the request") {
		m->requestFlip = true;
		h.dspStep();
		REQUIRE(m->leftExpander.producerMessage != originalProducer);
		REQUIRE_FALSE(m->leftExpander.messageFlipRequested);

		// Second step flips back — one flip per request, not a latched state.
		h.dspStep();
		REQUIRE(m->leftExpander.producerMessage == originalProducer);
	}
}


TEST_CASE("UI frames") {
	Test::Harness h;

	SECTION("uiFrame runs registered hooks and advances the UI counter") {
		int hookCalls = 0;
		h.onUiFrame([&]() { hookCalls++; });

		REQUIRE(h.uiFrameCount == 0);
		h.uiFrame();
		REQUIRE(hookCalls == 1);
		REQUIRE(h.uiFrameCount == 1);

		h.uiFrames(5);
		REQUIRE(hookCalls == 6);
		REQUIRE(h.uiFrameCount == 6);
	}

	SECTION("a real widget's step() runs on every UI frame") {
		auto* m = h.addModule<StrokeModule<STROKE_PORTS>>("Stroke");
		auto* mw = h.addWidget<StrokeWidget>(m);
		REQUIRE(mw != nullptr);

		// StrokeWidget::step() must survive being called headless — it is ThemedModuleWidget's
		// step(), which is what every module widget in the plugin inherits.
		REQUIRE_NOTHROW(h.uiFrames(10));
		REQUIRE(h.uiFrameCount == 10);
	}

	SECTION("dspSteps does not run UI frames") {
		// The property that makes queue-overflow testable: N DSP steps with no drain.
		int hookCalls = 0;
		h.onUiFrame([&]() { hookCalls++; });
		h.dspSteps(10000);
		REQUIRE(hookCalls == 0);
	}
}


TEST_CASE("The DSP:UI rate ratio") {
	Test::Harness h;
	auto* probe = h.adoptModule(new ScheduleProbe);
	h.onUiFrame([&]() { probe->uiFrameAtDspFrame.push_back(h.frame); });

	SECTION("stepsPerFrame reports the real ratio, not a rounded one") {
		h.setSampleRate(44100.f);
		h.setFrameRate(60.0);
		REQUIRE(h.stepsPerFrame() == Catch::Approx(735.0));

		h.setFrameRate(30.0);
		REQUIRE(h.stepsPerFrame() == Catch::Approx(1470.0));
	}

	SECTION("run() interleaves at the true ratio") {
		h.setSampleRate(44100.f);
		h.setFrameRate(60.0);
		h.run(Test::seconds(1.0));

		REQUIRE(probe->processCalls == 44100);
		REQUIRE(h.uiFrameCount == 60);

		// Consecutive UI frames are ~735 DSP steps apart — the gap this class exists to make
		// visible. Anything that assumes the UI observes every DSP-side change is broken across
		// a gap this size.
		REQUIRE(probe->uiFrameAtDspFrame.size() == 60);
		for (size_t i = 1; i < probe->uiFrameAtDspFrame.size(); i++) {
			int64_t gap = probe->uiFrameAtDspFrame[i] - probe->uiFrameAtDspFrame[i - 1];
			REQUIRE(gap == 735);
		}
	}

	SECTION("a non-integer ratio neither drifts nor rounds away over repeated runs") {
		// 44100/59.94 = 735.735..., so a naive int truncation loses ~0.7 steps per frame and
		// drifts by hundreds of samples over a second.
		h.setSampleRate(44100.f);
		h.setFrameRate(59.94);

		for (int i = 0; i < 10; i++) h.run(Test::seconds(0.1));

		REQUIRE(probe->processCalls == 44100);
		// 59.94 frames in one second: 59 complete, with the remainder carried.
		REQUIRE(h.uiFrameCount == 59);
	}

	SECTION("run() accumulates a fractional DSP step count without losing samples") {
		h.setSampleRate(44100.f);
		// 0.00001s = 0.441 DSP steps — less than one step per call.
		for (int i = 0; i < 1000; i++) h.run(Test::milliseconds(0.01));
		// 1000 * 0.441 = 441 steps, all of them accounted for.
		REQUIRE(probe->processCalls == 441);
	}

	SECTION("elapsed() tracks the DSP clock") {
		h.setSampleRate(44100.f);
		h.run(Test::seconds(0.5));
		REQUIRE(h.elapsed() == Catch::Approx(0.5).margin(0.001));
	}

	SECTION("run(0) does nothing") {
		h.run(Test::seconds(0.0));
		REQUIRE(probe->processCalls == 0);
		REQUIRE(h.uiFrameCount == 0);
	}
}


TEST_CASE("Sample rate changes keep construction and stepping in agreement") {
	// A4 again, now at harness level: setSampleRate() sets it on the engine, so a module added
	// afterwards is *configured* for that rate and stepped at it.
	Test::Harness h;
	h.setSampleRate(48000.f);

	auto* probe = h.adoptModule(new ScheduleProbe);
	h.dspStep();

	REQUIRE(probe->lastSampleRate == Catch::Approx(48000.f));
	REQUIRE(h.sampleRate() == Catch::Approx(48000.f));

	// Restore, so this TEST_CASE does not leak a rate change into the rest of the binary.
	h.setSampleRate(44100.f);
}


TEST_CASE("UiPresent mode answers the window-present question") {
	SECTION("UiPresent reports a window; UiAbsent does not") {
		Test::Harness present(Test::UiMode::UiPresent);
		REQUIRE(present.hasWindowForMode() == true);

		Test::Harness absent(Test::UiMode::UiAbsent);
		REQUIRE(absent.hasWindowForMode() == false);
	}

	SECTION("production code sees the mode through the vcv::ui seam") {
		// The point of routing this through UiAccess rather than a test-only field: code
		// under test asks vcv::ui::hasWindow() and gets the harness's answer, with no knowledge
		// that it is being tested. APP->window stays null throughout — that is the whole trick.
		REQUIRE(vcv::ui::hasWindow() == false);   // no harness installed yet
		{
			Test::Harness h(Test::UiMode::UiPresent);
			REQUIRE(APP->window == nullptr);
			REQUIRE(vcv::ui::hasWindow() == true);
			REQUIRE(vcv::ui::hasWindow() == h.hasWindowForMode());
		}
		// Restored on destruction, so one TEST_CASE cannot leak a window into the next.
		REQUIRE(vcv::ui::hasWindow() == false);
	}

	SECTION("setUiMode switches the answer mid-test") {
		// The editor-closed/reopened transition, which GuiTaskProcessor handles by retiring or
		// restarting its worker — and which nothing could exercise before this seam existed.
		Test::Harness h(Test::UiMode::UiPresent);
		REQUIRE(vcv::ui::hasWindow() == true);

		h.setUiMode(Test::UiMode::UiAbsent);
		REQUIRE(vcv::ui::hasWindow() == false);

		h.setUiMode(Test::UiMode::UiPresent);
		REQUIRE(vcv::ui::hasWindow() == true);
	}

	SECTION("UiPresent is the default") {
		Test::Harness h;
		REQUIRE(h.uiMode == Test::UiMode::UiPresent);
		REQUIRE(h.hasWindowForMode() == true);
	}

	SECTION("nested harnesses restore the outer answer") {
		// Each harness owns its own mock and its own Guard, so an inner one installed over an
		// outer one hands the seam back on destruction rather than clearing it.
		Test::Harness outer(Test::UiMode::UiPresent);
		{
			Test::Harness inner(Test::UiMode::UiAbsent);
			REQUIRE(vcv::ui::hasWindow() == false);
		}
		REQUIRE(vcv::ui::hasWindow() == true);
	}
}


TEST_CASE("UiPresent exercises GuiTaskProcessor's step() drain path") {
	// The payoff Step 2 was sequenced for. GuiTaskProcessor::process() asks
	// vcv::ui::hasWindow(), which resolves to APP->window != nullptr — false in every test
	// binary — unless something installs a UiAccess mock. So a module always took the worker
	// branch, and the suite's response was to set syncMode, suppressing both. The production
	// path (window present, tasks drained by the widget's step()) had never been exercised by
	// any test in this plugin.
	//
	// Driven here through a bare GuiTaskProcessor rather than through SpliceKit, so this
	// asserts the harness's contribution — that constructing one is what makes the branch
	// selectable — without dragging in a module whose own behaviour could mask the result.
	GuiTaskProcessor<8> gtp;

	SECTION("with a window present, no worker is started and step() drains") {
		Test::Harness h(Test::UiMode::UiPresent);

		int ran = 0;
		gtp.enqueue([&]() { ran++; });

		// The engine-thread tick, called with NO argument — exactly as a module's process()
		// calls it in production. It asks vcv::ui::hasWindow(), which the harness's mock
		// answers true, so the window-present branch is taken without the call site knowing
		// anything about the test.
		h.onUiFrame([&]() { gtp.step(); });
		for (int i = 0; i < 3; i++) {
			gtp.process();
			h.dspStep();
		}

		// No worker: the task is still queued, waiting for a UI frame.
		REQUIRE(gtp.workerState.load() == GuiTaskProcessor<8>::WorkerState::Absent);
		REQUIRE(ran == 0);

		// The UI frame drains it — the production path.
		h.uiFrame();
		REQUIRE(ran == 1);
	}

	SECTION("tasks queued across the rate gap all drain on the next UI frame") {
		// The realistic shape: many DSP steps, then one UI frame. With a queue of 8 and a
		// 735:1 ratio, anything enqueued per-step overflows long before the UI runs — which is
		// exactly bug class 4, and is now expressible.
		Test::Harness h(Test::UiMode::UiPresent);
		h.setSampleRate(44100.f);
		h.setFrameRate(60.0);

		int ran = 0;
		int accepted = 0;
		h.onUiFrame([&]() { gtp.step(); });

		for (int i = 0; i < 20; i++) {
			if (gtp.enqueue([&]() { ran++; })) accepted++;
		}

		// A ring buffer of 8 accepts 8 and drops the rest, reporting the drop.
		REQUIRE(accepted == 8);
		REQUIRE(ran == 0);

		h.uiFrame();
		REQUIRE(ran == 8);
	}
}


TEST_CASE("Scene layout is installed and restored") {
	math::Rect sceneBoxBefore = APP->scene->box;

	{
		Test::Harness h;
		// Finite, so box.contains() is meaningful — the Step 1 precondition.
		REQUIRE_FALSE(std::isinf(APP->scene->box.size.x));
		REQUIRE(APP->scene->box.size.x == Catch::Approx(1024.f));

		// The scene's own full-size children are out of the way.
		for (widget::Widget* child : APP->scene->children) {
			if (std::isinf(child->box.size.x)) {
				REQUIRE_FALSE(child->visible);
			}
		}
	}

	// Restored, so TEST_CASEs stay independent.
	REQUIRE(APP->scene->box.size.x == Catch::Approx(sceneBoxBefore.size.x));
	REQUIRE(std::isinf(APP->scene->box.size.x));
}


TEST_CASE("Widgets are positioned so hit-testing can tell them apart") {
	Test::Harness h;

	auto* m1 = h.addModule<StrokeModule<STROKE_PORTS>>("Stroke");
	auto* mw1 = h.addWidget<StrokeWidget>(m1);
	auto* m2 = h.addModule<StrokeModule<STROKE_PORTS>>("Stroke");
	auto* mw2 = h.addWidget<StrokeWidget>(m2);

	SECTION("widgets do not overlap") {
		REQUIRE(mw1->box.size.x > 0.f);
		REQUIRE_FALSE(mw1->box.isIntersecting(mw2->box));
		REQUIRE(mw2->box.pos.x >= mw1->box.pos.x + mw1->box.size.x);
	}

	SECTION("each widget is reachable by dispatch at its own centre") {
		auto hitAt = [](math::Vec pos) -> widget::Widget* {
			widget::EventContext c;
			widget::Widget::ButtonEvent e;
			e.context = &c;
			e.pos = pos;
			e.button = GLFW_MOUSE_BUTTON_LEFT;
			e.action = GLFW_PRESS;
			e.mods = 0;
			APP->event->rootWidget->onButton(e);
			return c.target;
		};

		widget::Widget* t1 = hitAt(mw1->box.getCenter());
		REQUIRE((t1 == mw1 || t1->isDescendantOf(mw1)));

		widget::Widget* t2 = hitAt(mw2->box.getCenter());
		REQUIRE((t2 == mw2 || t2->isDescendantOf(mw2)));

		// And a point clear of both reaches neither. rack::app::Scene is itself an
		// OpaqueWidget, so an unmatched left-click targets the scene rather than nothing.
		REQUIRE(hitAt(math::Vec(10, 10)) == APP->scene);
	}
}


TEST_CASE("Lifetime") {
	SECTION("modules and widgets are destroyed with the harness") {
		// The ModuleScaffold guarantee, now at harness level: no explicit teardown, and the
		// destructor still runs when Catch2 unwinds through a failed REQUIRE.
		{
			Test::Harness h;
			auto* m = h.addModule<StrokeModule<STROKE_PORTS>>("Stroke");
			h.addWidget<StrokeWidget>(m);
			REQUIRE(h.modules.size() == 1);
			REQUIRE(h.widgets.size() == 1);
		}
		// Nothing to assert directly — ASan is the oracle here. A leak or double-free in the
		// harness destructor fails the binary.
		REQUIRE(true);
	}

	SECTION("widgets are removed from the scene on teardown") {
		size_t childrenBefore = APP->scene->children.size();
		{
			Test::Harness h;
			auto* m = h.addModule<StrokeModule<STROKE_PORTS>>("Stroke");
			h.addWidget<StrokeWidget>(m);
			REQUIRE(APP->scene->children.size() == childrenBefore + 1);
		}
		REQUIRE(APP->scene->children.size() == childrenBefore);
	}

	SECTION("a browser widget (module == nullptr) can be built and stepped") {
		// The module-browser case, and a classic crash source.
		Test::Harness h;
		auto* mw = h.addBrowserWidget<StrokeWidget>("Stroke");
		REQUIRE(mw != nullptr);
		REQUIRE(mw->module == nullptr);
		REQUIRE_NOTHROW(h.uiFrames(3));
	}
}
