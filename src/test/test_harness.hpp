#pragma once
#include "test_plugin.hpp"
#include "test_context.hpp"
#include "test_mock.hpp"
#include "test_events.hpp"
#include "../vcv/ui.hpp"
#include <rack.hpp>
#include <app/Scene.hpp>
#include <app/ModuleWidget.hpp>
#include <widget/Widget.hpp>
#include <cmath>
#include <vector>
#include <utility>
#include <functional>

// Test::Harness — a deterministic scheduler for both of a module's threads.
//
// The framework has always been able to drive Module::process(). What it could not do is drive
// the *other* side — the widget's step(), the GUI task drain, event dispatch — or, crucially,
// interleave the two at their real relative rates. That gap is where the interesting bugs live:
// the DSP thread runs ~44100 times a second and the UI thread ~60, so roughly 735 process()
// calls happen between consecutive step() calls, and anything that assumes otherwise (an edge
// flag the UI is supposed to observe, a fixed-size queue the UI is supposed to drain) is broken
// in a way no test in this suite could previously express.
//
// Everything here runs on the calling thread. The harness interleaves; it does not thread. That
// is a deliberate choice, not a limitation to be fixed later: a test that fails intermittently
// is worse than no test, and the majority of the bug classes above are *scheduling* bugs, fully
// reproducible from a single thread once the schedule is under the test's control. Real
// concurrency is Phase 2 Step 7's job (ThreadedHarness, under TSan), for the two bug classes
// that genuinely require it.
//
// Usage:
//   Test::Harness h;
//   MyModule* m  = h.addModule<MyModule>("MySlug");
//   MyWidget* mw = h.addWidget<MyWidget>(m);
//
//   h.dspSteps(512);          // 512 process() calls, no UI frame in between
//   h.uiFrame();              // one widget step() pass
//   h.run(Test::seconds(0.1)) // interleave both at their true relative rates
//
// Lifetime is the harness's: modules and widgets added through it are destroyed in reverse
// order when it goes out of scope, including when Catch2 unwinds through a failed REQUIRE (see
// ModuleScaffold for why that matters — one real failure otherwise cascades into several fake
// ones for any module with process-wide registration).

namespace Test {

// A duration in seconds, so run() reads as a time rather than as a bare number whose unit the
// caller has to remember. Test::seconds(0.1) is 100ms of simulated time, not 0.1 of anything.
struct Duration {
	double value;
	explicit Duration(double seconds) : value(seconds) {}
};

inline Duration seconds(double s) {
	return Duration(s);
}
inline Duration milliseconds(double ms) {
	return Duration(ms / 1000.0);
}


// Whether the harness presents a window to the module under test.
//
// This decides which of GuiTaskProcessor's two drain paths runs, and it is the single most
// consequential setting here. In production a plugin normally HAS a window, so queued GUI tasks
// drain from the widget's step(). But GuiTaskProcessor::process() used to read APP->window,
// which is null in every test binary (TestContext never constructs a rack::window::Window — it
// cannot, see the Step 1 spike), so a module under test has always taken the *worker-thread*
// branch instead: it starts a real background thread, which then races the test's own thread
// over module state that is GUI-thread-only in production.
//
// The suite's response to that has been to switch the processor into syncMode, which suppresses
// both paths. So the production path — drain from step(), with a window present — has never
// been exercised by any test in this plugin. UiPresent is what fixes that: the harness installs
// a vcv::UiAccess mock answering hasWindow() == true, so the module takes the window-present
// branch while APP->window stays null.
enum class UiMode {
	// A window is present: GUI tasks drain from uiFrame(), no worker thread is ever started.
	// This is production behaviour for a plugin whose editor is open, and the default.
	UiPresent,
	// No window: the module takes its worker-thread branch, as it does in headless Rack or
	// with the plugin editor closed. Starts real threads, so it is only deterministic for
	// modules that do not share state with the test thread — prefer UiPresent unless the
	// worker branch is itself what is under test.
	UiAbsent,
};


// The scene layout the harness installs so that event dispatch and hit-testing work.
//
// Required, not cosmetic — this is the Step 1 spike's central finding. rack::widget::Widget's
// default box is Rect(Vec(), Vec(INFINITY, INFINITY)), the scene's real size is only ever
// assigned by Window::step(), and Scene::step() dereferences APP->window on its first line.
// Neither runs headless, so straight out of TestContext the scene and all five of its children
// have infinite boxes and `box.contains(pos)` is true at every position. Hit-testing is then
// meaningless in both directions: a widget added last is hit at any position, and one added
// earlier is unreachable at every position because a full-size overlay in front of it consumes
// first. Give the scene a finite box, hide the scene's own full-size children
// (recursePositionEvent skips invisible children), and collapse those children's boxes to zero.
//
// The box collapse is not redundant with hiding them, and the reason is a trap worth stating:
// Rack can un-hide them behind the harness's back. Scene::onHover()'s first act is
//
//     if (mousePos.y < menuBar->box.size.y) menuBar->show();
//
// and with an infinite menuBar box that test is true at every position — so the first hover
// that reaches the scene reveals a full-size menu bar, which then consumes every subsequent
// position event and silently un-does the precondition mid-test. Zeroing the boxes makes a
// re-shown child harmless: it is visible, but contains no point, so hit-testing skips it
// anyway. Found by EventDriver's own hover tests, not by the Step 1 spike, which never
// dispatched a Hover through the scene.
//
// rackScroll gets neutralised along with the rest, and that has a consequence worth stating
// because it is not obvious: APP->scene->rack is rackScroll's *descendant*, so anything
// production code parents to the rack — StrokeWidget's global-hotkey KeyContainer, for one — is
// unreachable by dispatch while the harness is alive.
//
// It is neutralised anyway, deliberately. RackScrollWidget is not an inert container: its
// onHoverScroll() dereferences APP->window unconditionally (RackScrollWidget.cpp:157), so a
// live rack viewport segfaults on the first scroll, and a live one also consumes the position
// events that tests use to assert a *miss*. Making it live costs more than it buys.
//
// Where a test does need the rack subtree reachable, `reparentRackWidgets` moves those widgets
// up to the scene for the harness's lifetime and puts them back afterwards — opt-in, so only
// the tests that need it pay for it. See Harness::exposeRackWidgets().
//
// Restores what it changed on destruction, so a harness leaves the shared scene as it found it
// and TEST_CASEs in one binary stay independent of each other.
struct SceneLayout {
	rack::math::Rect previousBox;
	std::vector<rack::widget::Widget*> hidden;
	// Every child's original box, restored on teardown — including children that were already
	// hidden, since their boxes are collapsed too (the scene may show them at any time).
	std::vector<std::pair<rack::widget::Widget*, rack::math::Rect>> previousChildBoxes;

	explicit SceneLayout(rack::math::Vec size = rack::math::Vec(1024, 720)) {
		previousBox = APP->scene->box;
		APP->scene->box.pos = rack::math::Vec(0, 0);
		APP->scene->box.size = size;

		for (rack::widget::Widget* child : APP->scene->children) {
			previousChildBoxes.push_back(std::make_pair(child, child->box));

			// Zero size, so `box.contains(pos)` is false everywhere even if something shows
			// this child again while the harness is alive.
			child->box.size = rack::math::Vec(0, 0);

			// Only record what was visible, so show() on teardown cannot reveal something
			// (BrowserOverlay, ResizeHandle) that the scene deliberately keeps hidden.
			if (!child->visible) continue;
			child->hide();
			hidden.push_back(child);
		}
	}

	~SceneLayout() {
		for (rack::widget::Widget* child : hidden) {
			child->show();
		}
		for (const auto& entry : previousChildBoxes) {
			entry.first->box = entry.second;
		}
		APP->scene->box = previousBox;
	}

	SceneLayout(const SceneLayout&) = delete;
	SceneLayout& operator=(const SceneLayout&) = delete;
};


struct Harness {
	// ---- Configuration -------------------------------------------------------------------

	UiMode uiMode = UiMode::UiPresent;

	// The UiAccess mock through which the harness answers "is a UI present?".
	//
	// This is the existing vcv::*Access seam doing the job it exists for, rather than a
	// test-only field bolted onto GuiTaskProcessor. Production code asks vcv::ui::hasWindow(),
	// which resolves to APP->window != nullptr normally and to this mock under test — so
	// nothing in the module has to know it is being tested, and the branch selection sticks
	// even at call sites that pass no argument (GuiTaskProcessor::process()'s old `window`
	// default was evaluated at the call site, so a harness-supplied argument alone would not).
	struct HarnessUiAccess : StoermelderPackOne::vcv::UiAccess {
		bool present = false;
		bool hasWindow() const override { return present; }
	};

	// Whether a UI is present in the current mode. This is what the mock answers; nothing
	// passes it anywhere, since production code asks the seam. Exposed only so a test can
	// assert on it.
	bool hasWindowForMode() const {
		return uiMode == UiMode::UiPresent;
	}

	// Switches modes mid-test — the "editor closed" / "editor reopened" transition, which is a
	// real thing GuiTaskProcessor handles (it retires or restarts its worker accordingly) and
	// which nothing could previously exercise.
	void setUiMode(UiMode mode) {
		uiMode = mode;
		installUiAccess();
	}

	// ---- Clocks --------------------------------------------------------------------------

	// DSP frame counter, handed to process() via ProcessArgs::frame. int64_t, matching
	// ProcessArgs::frame — SimpleEngine used an int, which silently differs from production
	// once a test runs long enough to matter.
	int64_t frame = 0;
	// UI frame counter — how many times uiFrame() has run.
	int64_t uiFrameCount = 0;

	// UI refresh rate in Hz. 60 is Rack's default and gives the ~735:1 DSP:UI ratio that makes
	// lost-edge bugs reproducible; lower it to compress a long run, raise it to model a
	// high-refresh display.
	double frameRate = 60.0;

	// Fractional DSP steps carried between run() calls, so a ratio that is not a whole number
	// (44100/60 = 735 exactly, but 44100/59.94 is not) does not drift or round away over
	// repeated calls.
	double dspStepDebt = 0.0;

	float sampleRate() const { return Test::sampleRate(); }

	// DSP steps per UI frame at the current rates — the number this whole class exists to make
	// explicit. Exposed because a test asserting on rate-gap behaviour usually wants to state
	// it ("queue N+1 tasks between frames"), not hardcode 735.
	double stepsPerFrame() const { return sampleRate() / frameRate; }

	// Simulated elapsed time, derived from the DSP clock (the finer of the two).
	double elapsed() const { return double(frame) / double(sampleRate()); }

	void setFrameRate(double hz) {
		REQUIRE(hz > 0.0);
		frameRate = hz;
	}

	void setSampleRate(float hz) {
		REQUIRE(hz > 0.f);
		// Set it on the engine, not on a harness-local field: Test::sampleRate() reads the
		// engine, and createModule()/makeProcessArgs() both read Test::sampleRate(). Keeping
		// one source of truth is A4's fix, and a harness-local override would reintroduce
		// exactly the construction/stepping disagreement A4 removed.
		TEST_SUPPRESS_DEPRECATED_BEGIN
		APP->engine->setSampleRate(hz);
		TEST_SUPPRESS_DEPRECATED_END
	}

	// ---- Construction --------------------------------------------------------------------

	Harness() { installUiAccess(); }
	explicit Harness(UiMode mode) : uiMode(mode) { installUiAccess(); }

	// The UiAccess mock, installed into vcv::uiAccess for the harness's lifetime and restored
	// on destruction. Declared after uiMode so installUiAccess() can read the mode.
	HarnessUiAccess uiAccessMock;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiAccessGuard{
		StoermelderPackOne::vcv::uiAccess, &uiAccessMock};

	// The scene layout that makes hit-testing meaningful, installed for the harness's lifetime
	// and restored on destruction. Owned rather than left to the caller because it is a
	// precondition for event dispatch, not an option: without it every dispatch lands on a
	// full-size scene overlay. See SceneLayout's comment for the mechanism.
	SceneLayout layout;

	// Synthetic input, dispatched through Rack's own event machinery. Declared after `layout`
	// so the scene is laid out before the driver can be used — hit-testing against an
	// un-laid-out scene silently matches everything, which is the Step 1 spike's central
	// finding and the reason this is reached through the harness rather than constructed
	// standalone. See test_events.hpp.
	EventDriver eventDriver{APP->scene};

	EventDriver& events() { return eventDriver; }

	// Makes widgets that production code parented to APP->scene->rack reachable by dispatch,
	// by moving them up to the scene for the rest of the harness's life.
	//
	// Needed because SceneLayout neutralises rackScroll, and the rack is its descendant (see
	// SceneLayout's comment for why it must be neutralised). A module widget that adds a
	// top-level helper to the rack — StrokeWidget's KeyContainer is the plugin's example, and
	// Glue's LabelContainer the other — is otherwise invisible to every event, which would let
	// a dispatch test pass while asserting nothing.
	//
	// Call it AFTER adding the widget whose constructor parents the helper. Only widgets
	// present at the time of the call are moved; the original parent and child order are
	// restored on teardown, so the widget's own destructor still finds what it expects.
	void exposeRackWidgets() {
		rack::widget::Widget* rackWidget = APP->scene->rack;
		if (!rackWidget) return;

		// Copy first: the loop reparents, which mutates rackWidget->children.
		std::vector<rack::widget::Widget*> toMove(rackWidget->children.begin(),
		                                          rackWidget->children.end());
		for (rack::widget::Widget* w : toMove) {
			// Skip anything the harness itself put in the scene, and any ModuleWidget — a
			// ModuleWidget in the rack is a real patch module positioned in rack coordinates,
			// and moving it would change what it overlaps.
			if (dynamic_cast<rack::app::ModuleWidget*>(w)) continue;
			rackWidget->removeChild(w);
			APP->scene->addChild(w);
			exposedFromRack.push_back(w);
		}
	}

	// Non-copyable: two harnesses owning the same modules would double-free them.
	Harness(const Harness&) = delete;
	Harness& operator=(const Harness&) = delete;

	~Harness() {
		// Drop every reference to a widget that is about to be deleted, before deleting it.
		// destroyWidget() runs finalizeWidget() and so clears EventState per widget, but the
		// driver's own lastTarget is not EventState's to clear — and a subsequent TEST_CASE
		// reading consumedBy() would otherwise see a freed pointer.
		eventDriver.reset();

		// Put anything exposeRackWidgets() borrowed back where its owner expects it, BEFORE
		// destroying widgets: a ModuleWidget destructor that created a rack helper removes it
		// from APP->scene->rack (StrokeWidget does exactly this), and would silently fail to
		// find it — leaking the helper and leaving a freed pointer in the scene.
		for (auto it = exposedFromRack.rbegin(); it != exposedFromRack.rend(); ++it) {
			APP->scene->removeChild(*it);
			APP->scene->rack->addChild(*it);
		}
		exposedFromRack.clear();

		// Widgets first: a ModuleWidget points at its module, and destroyWidget() runs
		// APP->event->finalizeWidget() which may still dispatch into the widget. Reverse
		// order within each kind, matching ModuleScaffold.
		for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
			APP->scene->removeChild(*it);
			Test::destroyWidget(*it);
		}
		for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
			Test::destroyModule(*it);
		}
	}

	// ---- Modules and widgets -------------------------------------------------------------

	// Creates a module, registers it for stepping, and takes ownership of it.
	template <typename T>
	T* addModule(const std::string& modelSlug) {
		T* m = Test::createModule<T>(modelSlug);
		return adoptModule(m);
	}

	// Creates a module through a caller-supplied factory. Suites that need setup applied the
	// instant a module is constructed (SpliceKit's syncMode shadow, say) can keep it here
	// rather than dropping out of the harness to hand-roll construction.
	template <typename T>
	T* addModule(std::function<T*()> factory) {
		return adoptModule(factory());
	}

	// Hands an already-constructed module to the harness.
	template <typename T>
	T* adoptModule(T* m) {
		modules.push_back(m);
		return m;
	}

	// Creates the widget for a module, positions it in rack coordinates, and adds it to the
	// scene so it is reachable by event dispatch.
	//
	// Position matters: a widget left at the origin overlaps every other widget added the same
	// way, so hit-testing cannot distinguish them. Widgets are laid out left to right by
	// default, each immediately right of the last, which is also how a user would place a
	// chain of expanders.
	template <typename T>
	T* addWidget(rack::Module* m) {
		T* mw = Test::createWidget<T>(m);
		REQUIRE(mw != nullptr);
		placeWidget(mw);
		return adoptWidget(mw);
	}

	// Creates a module-less widget, the way the module browser does — the `module == nullptr`
	// case that is a classic source of draw()/step() crashes.
	template <typename T>
	T* addBrowserWidget(const std::string& modelSlug) {
		T* mw = Test::createWidget<T>(modelSlug);
		REQUIRE(mw != nullptr);
		placeWidget(mw);
		return adoptWidget(mw);
	}

	template <typename T>
	T* adoptWidget(T* mw) {
		widgets.push_back(mw);
		return mw;
	}

	// Positions a widget immediately right of the previously placed one and adds it to the
	// scene. Call directly only for a widget built outside the harness.
	void placeWidget(rack::app::ModuleWidget* mw) {
		mw->box.pos = rack::math::Vec(nextWidgetX, widgetY);
		nextWidgetX += mw->box.size.x;
		APP->scene->addChild(mw);
	}

	// ---- Expanders -------------------------------------------------------------------------
	//
	// Wiring expanders by hand — `a->rightExpander.module = b; b->leftExpander.module = a;` — is
	// what every expander test in this suite used to do, and it silently skips two things Rack
	// does:
	//
	//   1. **Module::onExpanderChange() is never dispatched.** Rack assigns the neighbour via
	//      Module::setExpanderModule() (Module.cpp), which fires the event when the pointer
	//      actually changes. 11 modules in this plugin override onExpanderChange, and several do
	//      real work in it — MidiCat and Transit call notifyModuleListeners() (which is what sets
	//      moduleChangedFlag), and IntermixBase unpublishes its expander message and resets its
	//      outputs on a left-side change. A hand-wired test runs none of that, so it tests the
	//      steady state a module reaches *after* a change, never the change itself.
	//   2. **Expander::moduleId is left at -1.** Rack maintains it alongside `module`, and
	//      setExpanderModule does NOT touch it — the engine assigns it separately (see
	//      Engine::removeModule_NoLock). Strip walks chains by moduleId, so a test that only sets
	//      `module` presents as a connected-but-unidentifiable neighbour.
	//
	// These methods do both, in Rack's order. What they deliberately do NOT do is touch
	// `moduleChangedFlag`: that is this plugin's own ModuleChangeListener signal, and a module
	// that reaches it does so *through* onExpanderChange -> notifyModuleListeners(). Setting it
	// from the harness would paper over a module that forgot to notify, which is exactly the bug
	// worth catching. A test that needs the flag set without a real notification should set it
	// itself, and say why.

	// Connects `right` as the right-hand neighbour of `left` (and `left` as the left-hand
	// neighbour of `right`), dispatching onExpanderChange on both, as Rack does.
	//
	// Does not step. Modules that react to a neighbour change during process() need a dspStep()
	// afterwards; whether one is required — and how many — is the module's business, not the
	// harness's, so it stays at the call site where it can be asserted.
	void connectExpander(rack::Module* left, rack::Module* right) {
		REQUIRE(left != nullptr);
		REQUIRE(right != nullptr);
		REQUIRE(left != right);
		setExpander(left, SIDE_RIGHT, right);
		setExpander(right, SIDE_LEFT, left);
	}

	// Connects a chain left-to-right: chain[0] -> chain[1] -> ... Each link is made with
	// connectExpander, so events fire per link and in order — the same way Rack dispatches them
	// as a rack is rearranged, rather than as one batched update at the end.
	void connectChain(const std::vector<rack::Module*>& chain) {
		for (size_t i = 0; i + 1 < chain.size(); i++) {
			connectExpander(chain[i], chain[i + 1]);
		}
	}

	// Variadic form: h.connectChain(transit, ex1, ex2).
	template <typename... T>
	void connectChain(rack::Module* first, rack::Module* second, T*... rest) {
		connectChain(std::vector<rack::Module*>{first, second, rest...});
	}

	// Removes the neighbour on one side of `m`, and clears the matching back-reference on that
	// neighbour, dispatching onExpanderChange on both — the "module removed from the rack" case.
	void disconnectExpander(rack::Module* m, uint8_t side) {
		REQUIRE(m != nullptr);
		rack::Module* neighbour = m->getExpander(side).module;
		setExpander(m, side, nullptr);
		if (neighbour) {
			// The neighbour's view of m is on its opposite side.
			setExpander(neighbour, side == SIDE_RIGHT ? SIDE_LEFT : SIDE_RIGHT, nullptr);
		}
	}

	// Rack's side convention (Module::getExpander): 0 = left, 1 = right. Named because a bare
	// 0/1 at a call site reads as a module index.
	static const uint8_t SIDE_LEFT = 0;
	static const uint8_t SIDE_RIGHT = 1;

	// ---- Stepping ------------------------------------------------------------------------

	// Runs one DSP step: process() on every registered module, in registration order, with
	// expander messages flipped exactly as Rack's engine does.
	void dspStep() {
		const rack::Module::ProcessArgs args = Test::makeProcessArgs(frame);
		for (rack::Module* m : modules) {
			m->process(args);
			// Only flip when the module asked for it, matching
			// Rack/src/engine/Engine.cpp — a module that forgets requestMessageFlip() must
			// fail here just as it would in Rack. (This is Phase 1's A2 fix, carried over
			// from SimpleEngine.)
			if (m->leftExpander.messageFlipRequested) {
				std::swap(m->leftExpander.producerMessage, m->leftExpander.consumerMessage);
				m->leftExpander.messageFlipRequested = false;
			}
			if (m->rightExpander.messageFlipRequested) {
				std::swap(m->rightExpander.producerMessage, m->rightExpander.consumerMessage);
				m->rightExpander.messageFlipRequested = false;
			}
		}
		frame++;
	}

	void dspSteps(int64_t count) {
		for (int64_t i = 0; i < count; i++) dspStep();
	}

	// Runs one UI frame: step() on every registered widget, in registration order.
	//
	// This is the production UI thread's work, and the reason UiPresent mode matters: a
	// widget's step() typically drains its module's GuiTaskProcessor, which is the path that
	// runs in a real Rack with the editor open — and the path no test in this suite has ever
	// taken.
	void uiFrame() {
		for (rack::app::ModuleWidget* mw : widgets) {
			mw->step();
		}
		for (const auto& hook : uiFrameHooks) {
			hook();
		}
		uiFrameCount++;
	}

	void uiFrames(int64_t count) {
		for (int64_t i = 0; i < count; i++) uiFrame();
	}

	// Extra work to run at the end of every UI frame. For a module whose real widget cannot be
	// constructed in a test (or whose step() a test wants to stand in for), this keeps the
	// per-frame work inside the harness's schedule rather than sprinkled through the test body.
	std::vector<std::function<void()>> uiFrameHooks;

	void onUiFrame(std::function<void()> hook) {
		uiFrameHooks.push_back(std::move(hook));
	}

	// Interleaves both clocks at their true relative rates for the given duration.
	//
	// A UI frame runs after every stepsPerFrame() DSP steps, so a test written against run()
	// sees the same schedule a module sees in Rack — including the ~735 process() calls that
	// happen between two consecutive step()s at 44.1kHz/60Hz. Any fractional remainder is
	// carried in dspStepDebt, so repeated run() calls neither drift nor accumulate rounding.
	void run(Duration duration) {
		REQUIRE(duration.value >= 0.0);
		double totalSteps = duration.value * double(sampleRate()) + dspStepDebt;
		int64_t wholeSteps = int64_t(std::floor(totalSteps));
		dspStepDebt = totalSteps - double(wholeSteps);

		const double perFrame = stepsPerFrame();
		int64_t remaining = wholeSteps;
		while (remaining > 0) {
			// Steps until the next UI frame is due, never more than what is left.
			int64_t chunk = int64_t(std::floor(perFrame - frameDebt));
			if (chunk < 1) chunk = 1;
			if (chunk > remaining) chunk = remaining;

			dspSteps(chunk);
			remaining -= chunk;
			frameDebt += double(chunk);

			while (frameDebt >= perFrame) {
				frameDebt -= perFrame;
				uiFrame();
			}
		}
	}

	// ---- State -----------------------------------------------------------------------------

	std::vector<rack::Module*> modules;
	std::vector<rack::app::ModuleWidget*> widgets;

	// Widgets moved out of APP->scene->rack by exposeRackWidgets(), put back on teardown.
	std::vector<rack::widget::Widget*> exposedFromRack;

private:
	// Sets one side's neighbour on one module, the way Rack's engine does: assign moduleId
	// directly, then route the pointer through setExpanderModule() so onExpanderChange fires.
	//
	// The split is Rack's, not ours — setExpanderModule() only touches `module` and the event,
	// and the engine assigns `moduleId` around it (Engine::removeModule_NoLock). moduleId is set
	// first so a handler reacting to the event already sees a consistent pair.
	// setExpanderModule is PRIVATE (which expands to a deprecation attribute, rack.hpp:16), like
	// the engine and scene calls elsewhere in this framework: there is no public way to dispatch
	// an ExpanderChangeEvent, and re-implementing the dispatch here would be the hand-wiring this
	// method exists to replace.
	static void setExpander(rack::Module* m, uint8_t side, rack::Module* neighbour) {
		m->getExpander(side).moduleId = neighbour ? neighbour->id : -1;
		TEST_SUPPRESS_DEPRECATED_BEGIN
		m->setExpanderModule(neighbour, side);
		TEST_SUPPRESS_DEPRECATED_END
	}

	// Points the UiAccess mock at whatever the current mode implies.
	void installUiAccess() {
		uiAccessMock.present = hasWindowForMode();
	}

	// DSP steps accumulated toward the next UI frame, carried across run() calls.
	double frameDebt = 0.0;

	// Where the next widget is placed. RACK_GRID_WIDTH-ish spacing is not needed — widgets are
	// packed edge to edge by their own panel width — but they must start clear of the scene
	// origin so a test can click "outside every widget" at a small coordinate.
	float nextWidgetX = 100.f;
	float widgetY = 100.f;
};

} // namespace Test
