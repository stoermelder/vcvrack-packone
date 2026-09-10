#include "../test/framework.hpp"
#include "../modules/stroke/Stroke.cpp"
// Phase 2 Step 1 spike — "does headless event dispatch work?"
//
// Stroke.test.cpp:1338 carries the claim that drove the whole hand-built-event style of the
// existing widget tests:
//
//   "Events are built by hand (Rack's EventState is not usable headless for synthetic dispatch)"
//
// That was an assumption, never a measurement. This binary measures it. It is a *diagnostic*
// test, not a behaviour test: each TEST_CASE probes one part of the dispatch path against a
// real, positioned widget tree under the existing TestContext (no Window, no GL, no GLFW) and
// asserts what actually happens — so the answer is recorded as a passing test rather than as a
// comment, and stays true as Rack is updated.
//
// The full result is written up at the bottom of this file.

#include <widget/OpaqueWidget.hpp>
#include <app/Scene.hpp>

using namespace rack;

Test::TestContext<> testContext;

// ---------------------------------------------------------------------------------------------
// Precondition: make the scene hit-testable.
//
// This is the spike's central discovery, and it is not what the design doc expected the obstacle
// to be. rack::widget::Widget's default box is `Rect(Vec(), Vec(INFINITY, INFINITY))`
// (Widget.hpp:23), and the only code that ever assigns the scene a real size is
// Window::step() (Window.cpp:493), which never runs headless. Scene::step(), which would size
// rackScroll and menuBar, dereferences APP->window on its first line and so cannot be called
// either.
//
// The consequence: straight out of TestContext, *every* widget in the scene has an infinite box,
// so `child->box.contains(e.pos)` is true for all of them at every position. Position events do
// dispatch — they just always land on whichever full-size overlay happens to be topmost
// (a ui::MenuOverlay, in practice) and never reach anything a test added. Both symptoms this
// produces read as "dispatch doesn't work headless": a hit reports the wrong target, and a
// deliberate miss reports a target anyway.
//
// Two lines of setup fix it, and they are exactly what Test::Harness (Phase 2 Step 2) should do
// when it constructs the scene.
inline void layoutSceneForDispatch(math::Vec size = math::Vec(1024, 720)) {
	// 1. Give the scene a finite box, as Window::step() would.
	APP->scene->box.pos = math::Vec(0, 0);
	APP->scene->box.size = size;

	// 2. Neutralise the scene's own full-size children. Each is a genuine part of the running
	//    app (rack viewport, menu bar, module browser, menu overlay, resize handle), but with an
	//    infinite box each swallows every position event before a test's widget is reached.
	//    Hiding them is enough: recursePositionEvent() skips invisible children.
	for (widget::Widget* child : APP->scene->children) {
		child->hide();
	}
}

// Restores the scene to how TestContext left it, so this binary's TEST_CASEs stay independent
// of each other regardless of Catch2's ordering.
inline void unlayoutScene() {
	for (widget::Widget* child : APP->scene->children) {
		child->show();
	}
	APP->scene->box = math::Rect(math::Vec(), math::Vec(INFINITY, INFINITY));
}

struct ScopedSceneLayout {
	ScopedSceneLayout(math::Vec size = math::Vec(1024, 720)) { layoutSceneForDispatch(size); }
	~ScopedSceneLayout() { unlayoutScene(); }

	ScopedSceneLayout(const ScopedSceneLayout&) = delete;
	ScopedSceneLayout& operator=(const ScopedSceneLayout&) = delete;
};

// A widget that records every event it receives, and consumes position events (as OpaqueWidget
// does) so consumption/propagation ordering is observable.
struct ProbeWidget : widget::OpaqueWidget {
	int buttonCount = 0;
	int hoverCount = 0;
	int hoverKeyCount = 0;
	int hoverScrollCount = 0;
	int hoverTextCount = 0;
	int selectKeyCount = 0;
	int enterCount = 0;
	int leaveCount = 0;
	int selectCount = 0;
	int deselectCount = 0;
	int dragStartCount = 0;
	int dragMoveCount = 0;
	int dragEndCount = 0;

	// Position of the last position-event, in this widget's own local coordinates — the value
	// that proves the recursion actually translated coordinates down the tree.
	math::Vec lastButtonPos;
	math::Vec lastHoverPos;
	math::Vec lastDragMouseDelta;

	// Set to false to make this widget transparent to position events (no consumption, no
	// stopPropagating), so a widget behind it can be observed receiving them.
	bool consume = true;

	void onButton(const ButtonEvent& e) override {
		buttonCount++;
		lastButtonPos = e.pos;
		if (consume) OpaqueWidget::onButton(e);
		else widget::Widget::onButton(e);
	}
	void onHover(const HoverEvent& e) override {
		hoverCount++;
		lastHoverPos = e.pos;
		if (consume) OpaqueWidget::onHover(e);
		else widget::Widget::onHover(e);
	}
	void onHoverKey(const HoverKeyEvent& e) override {
		hoverKeyCount++;
		if (consume) e.consume(this);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		hoverScrollCount++;
		if (consume) e.consume(this);
	}
	void onHoverText(const HoverTextEvent& e) override {
		hoverTextCount++;
		if (consume) e.consume(this);
	}
	void onSelectKey(const SelectKeyEvent& e) override {
		selectKeyCount++;
		if (consume) e.consume(this);
	}
	void onEnter(const EnterEvent& e) override { enterCount++; OpaqueWidget::onEnter(e); }
	void onLeave(const LeaveEvent& e) override { leaveCount++; OpaqueWidget::onLeave(e); }
	void onSelect(const SelectEvent& e) override { selectCount++; }
	void onDeselect(const DeselectEvent& e) override { deselectCount++; }
	void onDragStart(const DragStartEvent& e) override { dragStartCount++; }
	void onDragMove(const DragMoveEvent& e) override {
		dragMoveCount++;
		lastDragMouseDelta = e.mouseDelta;
	}
	void onDragEnd(const DragEndEvent& e) override { dragEndCount++; }
};

// Adds a probe to the scene at an absolute scene-coordinate box, and removes it again on
// destruction — also clearing any EventState references to it, as Test::destroyWidget() does for
// ModuleWidgets. A probe left dangling in EventState would corrupt every later TEST_CASE here.
struct ScopedProbe {
	ProbeWidget* w;

	ScopedProbe(math::Rect box) {
		w = new ProbeWidget;
		w->box = box;
		APP->scene->addChild(w);
	}
	~ScopedProbe() {
		APP->event->finalizeWidget(w);
		APP->scene->removeChild(w);
		delete w;
	}

	ProbeWidget* operator->() const { return w; }
	operator ProbeWidget*() const { return w; }

	ScopedProbe(const ScopedProbe&) = delete;
	ScopedProbe& operator=(const ScopedProbe&) = delete;
};

// Dispatches a ButtonEvent the way handleButton() does, minus its APP->window call, and returns
// the resulting target. This is the shape EventDriver (Step 5) would use.
inline widget::Widget* dispatchButton(math::Vec pos, int action,
                                      int button = GLFW_MOUSE_BUTTON_LEFT, int mods = 0) {
	widget::EventContext c;
	widget::Widget::ButtonEvent e;
	e.context = &c;
	e.pos = pos;
	e.button = button;
	e.action = action;
	e.mods = mods;
	APP->event->rootWidget->onButton(e);
	return c.target;
}

TEST_CASE("The headless preconditions") {
	SECTION("APP->event exists and its rootWidget is the scene") {
		REQUIRE(APP->event != nullptr);
		REQUIRE(APP->event->rootWidget == APP->scene);
	}

	SECTION("APP->window is null — this is the one hard constraint") {
		// TestContext never constructs a rack::window::Window: its constructor calls
		// glfwCreateWindow() and throws if that fails, its Internal is opaque, and it has no
		// virtuals — so it can be neither constructed nor subclassed headless. Every EventState
		// entry point that dereferences APP->window is therefore unusable as-is; every one that
		// does not is usable directly.
		REQUIRE(APP->window == nullptr);
	}

	SECTION("out of the box, every scene widget has an infinite box") {
		// The reason naive headless dispatch appears not to work. See layoutSceneForDispatch().
		REQUIRE(std::isinf(APP->scene->box.size.x));
		REQUIRE(std::isinf(APP->scene->box.size.y));

		int infiniteChildren = 0;
		for (widget::Widget* child : APP->scene->children) {
			if (std::isinf(child->box.size.x)) infiniteChildren++;
		}
		REQUIRE(infiniteChildren > 0);
	}

	SECTION("without layout, hit-testing is meaningless: every position hits everything") {
		// The concrete failure mode, asserted rather than described. A probe added last is
		// topmost, so a click inside its box does reach it — but so does a click at a position
		// nowhere near it, because its box is not what decided the match: the *parents'* boxes
		// are all infinite, and the probe's own box is only consulted at the last level.
		ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

		REQUIRE(dispatchButton(math::Vec(120, 120), GLFW_PRESS) == probe.w);
		REQUIRE(probe->buttonCount == 1);

		// A hit test at a wildly out-of-bounds position must miss the probe. It does — but the
		// event still lands on a full-size scene overlay rather than falling through, so a test
		// cannot distinguish "missed" from "hit something else" ...
		widget::Widget* strayTarget = dispatchButton(math::Vec(-5000, -5000), GLFW_PRESS);
		REQUIRE(probe->buttonCount == 1);
		REQUIRE(strayTarget != nullptr);
		REQUIRE(strayTarget != probe.w);

		// ... and any widget *not* added last is unreachable entirely, because the overlays in
		// front of it match every position and consume first.
		ScopedProbe behind(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));
		REQUIRE(dispatchButton(math::Vec(120, 120), GLFW_PRESS) == behind.w);
		REQUIRE(probe->buttonCount == 1);   // unchanged: `behind` is now topmost and consumed
	}

	SECTION("after layoutSceneForDispatch() the same dispatch reaches the probe") {
		ScopedSceneLayout layout;
		ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

		REQUIRE(dispatchButton(math::Vec(120, 120), GLFW_PRESS) == probe.w);
		REQUIRE(probe->buttonCount == 1);
	}
}

TEST_CASE("handleScroll dispatches and hit-tests headless") {
	// handleScroll touches no Window and no GLFW: it is a straight rootWidget->onHoverScroll().
	ScopedSceneLayout layout;
	ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

	SECTION("a scroll inside the widget's box reaches it and is reported as consumed") {
		bool consumed = APP->event->handleScroll(math::Vec(120, 120), math::Vec(0, -1));
		REQUIRE(probe->hoverScrollCount == 1);
		REQUIRE(consumed);
	}

	SECTION("a scroll outside the widget's box does not reach it") {
		bool consumed = APP->event->handleScroll(math::Vec(10, 10), math::Vec(0, -1));
		REQUIRE(probe->hoverScrollCount == 0);
		REQUIRE_FALSE(consumed);
	}
}

TEST_CASE("handleText dispatches headless") {
	ScopedSceneLayout layout;
	ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

	SECTION("HoverText hit-tests like any other position event") {
		bool consumed = APP->event->handleText(math::Vec(120, 120), 'a');
		REQUIRE(probe->hoverTextCount == 1);
		REQUIRE(consumed);
	}
}

TEST_CASE("handleKey dispatches headless") {
	// handleKey calls glfwGetKeyName(), and this binary never calls glfwInit() — so the spike
	// also establishes that an uninitialised GLFW is survivable on this path.
	ScopedSceneLayout layout;
	ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

	SECTION("HoverKey reaches a widget under the given position") {
		bool consumed = APP->event->handleKey(math::Vec(120, 120), GLFW_KEY_A, 0, GLFW_PRESS, 0);
		REQUIRE(probe->hoverKeyCount == 1);
		REQUIRE(consumed);
	}

	SECTION("SelectKey goes to the selected widget regardless of position") {
		APP->event->setSelectedWidget(probe);
		REQUIRE(probe->selectCount == 1);

		bool consumed = APP->event->handleKey(math::Vec(0, 0), GLFW_KEY_A, 0, GLFW_PRESS, 0);
		REQUIRE(probe->selectKeyCount == 1);
		REQUIRE(consumed);
		// Position-based dispatch was skipped because SelectKey consumed the event.
		REQUIRE(probe->hoverKeyCount == 0);

		APP->event->setSelectedWidget(nullptr);
		REQUIRE(probe->deselectCount == 1);
	}

	SECTION("held-key state is tracked across press and release") {
		APP->event->handleKey(math::Vec(120, 120), GLFW_KEY_A, 0, GLFW_PRESS, 0);
		REQUIRE(APP->event->heldKeys.count(GLFW_KEY_A) == 1);

		APP->event->handleKey(math::Vec(120, 120), GLFW_KEY_A, 0, GLFW_RELEASE, 0);
		REQUIRE(APP->event->heldKeys.count(GLFW_KEY_A) == 0);
	}
}

TEST_CASE("EventState bookkeeping setters work headless") {
	// setHoveredWidget / setDraggedWidget / setSelectedWidget / setDragHoveredWidget are the
	// state machine handleButton() and handleHover() drive. They touch no Window, so an
	// EventDriver can drive them directly even though those two entry points are blocked.
	ScopedSceneLayout layout;
	ScopedProbe a(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));
	ScopedProbe b(math::Rect(math::Vec(200, 100), math::Vec(50, 40)));

	SECTION("hover enter/leave fires on transition, not on repeat") {
		APP->event->setHoveredWidget(a);
		REQUIRE(a->enterCount == 1);

		APP->event->setHoveredWidget(a);
		REQUIRE(a->enterCount == 1);   // no re-entry
		REQUIRE(a->leaveCount == 0);

		APP->event->setHoveredWidget(b);
		REQUIRE(a->leaveCount == 1);
		REQUIRE(b->enterCount == 1);

		APP->event->setHoveredWidget(nullptr);
		REQUIRE(b->leaveCount == 1);
	}

	SECTION("drag start/end pair up") {
		APP->event->setDraggedWidget(a, GLFW_MOUSE_BUTTON_LEFT);
		REQUIRE(a->dragStartCount == 1);
		REQUIRE(APP->event->draggedWidget == a.w);

		APP->event->setDraggedWidget(nullptr, 0);
		REQUIRE(a->dragEndCount == 1);
		REQUIRE(APP->event->draggedWidget == nullptr);
	}

	SECTION("finalizeWidget clears every reference, as A5 relies on") {
		APP->event->setHoveredWidget(a);
		APP->event->setDraggedWidget(a, GLFW_MOUSE_BUTTON_LEFT);
		APP->event->setSelectedWidget(a);

		APP->event->finalizeWidget(a);

		REQUIRE(APP->event->hoveredWidget == nullptr);
		REQUIRE(APP->event->draggedWidget == nullptr);
		REQUIRE(APP->event->selectedWidget == nullptr);
	}
}

TEST_CASE("Raw recursion hit-tests and translates coordinates") {
	// The half of handleButton() that does NOT need a Window: rootWidget->onButton() with a
	// hand-built EventContext. This is what an EventDriver would call in place of
	// handleButton() — so the spike must establish that this path really does hit-test and
	// translate positions, not just that it compiles.
	ScopedSceneLayout layout;
	ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

	SECTION("a hit inside the box consumes and reports local coordinates") {
		widget::EventContext c;
		widget::Widget::ButtonEvent e;
		e.context = &c;
		e.pos = math::Vec(120, 130);
		e.button = GLFW_MOUSE_BUTTON_LEFT;
		e.action = GLFW_PRESS;
		e.mods = 0;
		APP->event->rootWidget->onButton(e);

		REQUIRE(probe->buttonCount == 1);
		REQUIRE(c.target == probe.w);
		// 120-100, 130-100 — the recursion subtracted the widget's box origin.
		REQUIRE(probe->lastButtonPos.x == Catch::Approx(20.f));
		REQUIRE(probe->lastButtonPos.y == Catch::Approx(30.f));
	}

	SECTION("a miss outside the box does not reach the widget; the scene absorbs it") {
		// rack::app::Scene is itself an OpaqueWidget (Scene.hpp:12), so an unmatched left-click
		// is consumed by the scene rather than falling through to a null target. An
		// EventDriver's "did this click hit my widget?" check must therefore compare the target
		// against the widget, not against nullptr.
		REQUIRE(dispatchButton(math::Vec(10, 10), GLFW_PRESS) == APP->scene);
		REQUIRE(probe->buttonCount == 0);
	}

	SECTION("coordinates translate through a nested parent, not just one level") {
		// The property that makes the driver worth more than hand-built events: a ModuleWidget's
		// children are nested several levels deep, and only real recursion gets their local
		// coordinates right.
		auto* inner = new ProbeWidget;
		inner->box = math::Rect(math::Vec(10, 5), math::Vec(20, 20));
		probe->addChild(inner);

		REQUIRE(dispatchButton(math::Vec(115, 110), GLFW_PRESS) == inner);
		// 115-100-10, 110-100-5
		REQUIRE(inner->lastButtonPos.x == Catch::Approx(5.f));
		REQUIRE(inner->lastButtonPos.y == Catch::Approx(5.f));
		// The parent saw it first, in its own coordinates, before recursing.
		REQUIRE(probe->buttonCount == 1);
		REQUIRE(probe->lastButtonPos.x == Catch::Approx(15.f));
	}
}

TEST_CASE("Overlapping widgets — topmost consumes, propagation stops") {
	// The property hand-built events can never test: z-ordering and consumption. Children are
	// walked in reverse insertion order, so the last-added widget is the topmost.
	ScopedSceneLayout layout;
	ScopedProbe below(math::Rect(math::Vec(100, 100), math::Vec(80, 80)));
	ScopedProbe above(math::Rect(math::Vec(120, 120), math::Vec(40, 40)));

	SECTION("a click in the overlap reaches only the topmost widget") {
		REQUIRE(dispatchButton(math::Vec(130, 130), GLFW_PRESS) == above.w);
		REQUIRE(above->buttonCount == 1);
		REQUIRE(below->buttonCount == 0);
	}

	SECTION("a non-consuming topmost widget lets the one below receive it") {
		above->consume = false;

		REQUIRE(dispatchButton(math::Vec(130, 130), GLFW_PRESS) == below.w);
		REQUIRE(above->buttonCount == 1);
		REQUIRE(below->buttonCount == 1);
	}

	SECTION("a click outside the topmost but inside the one below reaches the one below") {
		REQUIRE(dispatchButton(math::Vec(105, 105), GLFW_PRESS) == below.w);
		REQUIRE(above->buttonCount == 0);
		REQUIRE(below->buttonCount == 1);
	}
}

TEST_CASE("An invisible widget is skipped by position events") {
	ScopedSceneLayout layout;
	ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));
	probe->visible = false;

	REQUIRE(dispatchButton(math::Vec(120, 120), GLFW_PRESS) == APP->scene);
	REQUIRE(probe->buttonCount == 0);
}

TEST_CASE("A full click composes from the Window-free primitives") {
	// The composition an EventDriver::click() would perform: the recursion for hit-testing, then
	// EventState's own setters for the bookkeeping — i.e. handleButton() minus its
	// APP->window->isCursorLocked() call and minus the double-click timing (which calls
	// system::getTime(), and so should route through the existing vcv::FileAccess seam).
	ScopedSceneLayout layout;
	ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

	widget::Widget* clicked = dispatchButton(math::Vec(120, 120), GLFW_PRESS);
	REQUIRE(clicked == probe.w);
	APP->event->setDraggedWidget(clicked, GLFW_MOUSE_BUTTON_LEFT);
	APP->event->setSelectedWidget(clicked);
	REQUIRE(probe->dragStartCount == 1);
	REQUIRE(probe->selectCount == 1);

	dispatchButton(math::Vec(120, 120), GLFW_RELEASE);
	APP->event->setDragHoveredWidget(nullptr);
	APP->event->setDraggedWidget(nullptr, 0);
	REQUIRE(probe->dragEndCount == 1);
	REQUIRE(probe->buttonCount == 2);
	REQUIRE(APP->event->selectedWidget == probe.w);

	APP->event->setSelectedWidget(nullptr);
}

TEST_CASE("A full drag composes from the Window-free primitives") {
	// handleHover() minus its APP->window->isCursorLocked()/getMods() calls — which are only
	// used for cursor-lock gating and for synthesising RACK_HELD key repeats, neither of which a
	// deterministic test wants anyway.
	ScopedSceneLayout layout;
	ScopedProbe probe(math::Rect(math::Vec(100, 100), math::Vec(50, 40)));

	APP->event->setDraggedWidget(probe, GLFW_MOUSE_BUTTON_LEFT);
	REQUIRE(probe->dragStartCount == 1);

	// A DragMove, as handleHover() would dispatch it while a widget is dragged.
	widget::Widget::DragMoveEvent eDragMove;
	eDragMove.button = APP->event->dragButton;
	eDragMove.mouseDelta = math::Vec(5, -10);
	APP->event->draggedWidget->onDragMove(eDragMove);

	REQUIRE(probe->dragMoveCount == 1);
	REQUIRE(probe->lastDragMouseDelta.x == Catch::Approx(5.f));
	REQUIRE(probe->lastDragMouseDelta.y == Catch::Approx(-10.f));

	APP->event->setDraggedWidget(nullptr, 0);
	REQUIRE(probe->dragEndCount == 1);
}

TEST_CASE("A real ModuleWidget is hit-testable once positioned") {
	// The preceding cases use a synthetic probe. This one confirms the same holds for a real
	// plugin ModuleWidget added through the normal Test:: helpers — the actual Step 5 use case.
	// Stroke is used because it is a plain ThemedModuleWidget with no exotic construction.
	ScopedSceneLayout layout;

	auto* mw = Test::createWidget<rack::app::ModuleWidget>("Stroke");
	REQUIRE(mw != nullptr);
	// Panel geometry comes from the module's SVG, so it is real, finite and non-zero.
	REQUIRE(mw->box.size.x > 0.f);
	REQUIRE(mw->box.size.y > 0.f);

	APP->scene->addChild(mw);
	mw->box.pos = math::Vec(300, 200);

	// A click at the panel's centre lands on the widget itself or one of its children — not on
	// the scene, and not on nothing.
	math::Vec centre = mw->box.pos.plus(mw->box.size.div(2));
	widget::Widget* target = dispatchButton(centre, GLFW_PRESS);
	REQUIRE(target != nullptr);
	REQUIRE(target != APP->scene);
	REQUIRE((target == mw || target->isDescendantOf(mw)));

	// A click well outside the panel does not reach it; the scene absorbs it.
	REQUIRE(dispatchButton(math::Vec(20, 20), GLFW_PRESS) == APP->scene);

	APP->event->finalizeWidget(mw);
	APP->scene->removeChild(mw);
	Test::destroyWidget(mw);
}

// ---------------------------------------------------------------------------------------------
// SPIKE RESULT — Phase 2, Step 1
//
// Q: Does headless event dispatch through Rack's own EventState work?
// A: **Yes**, once two preconditions are met. The dispatch machinery itself is entirely
//    headless; the obstacles are layout and two Window dereferences, not the recursion.
//
// Works headless, called directly, with no changes to Rack:
//   - handleScroll, handleText, handleKey, handleDrop, handleDirty, handleLeave
//   - setHoveredWidget / setDraggedWidget / setDragHoveredWidget / setSelectedWidget
//   - finalizeWidget
//   - the whole Widget::recursePositionEvent recursion: hit-testing by box, coordinate
//     translation into local space (including through nesting), reverse-z-order traversal, and
//     consumption/propagation — all verified above, including against a real ModuleWidget.
//   - glfwGetKeyName()/glfwGetKeyScancode() on an uninitialised GLFW: safe, they return NULL/0.
//
// Precondition 1 — the scene must be laid out. THIS IS THE REAL FINDING.
//   Widget's default box is Rect(Vec(), Vec(INFINITY, INFINITY)) (Widget.hpp:23). The scene's
//   size is only ever assigned by Window::step() (Window.cpp:493), and Scene::step() — which
//   sizes rackScroll and menuBar — dereferences APP->window on its first line. Neither runs
//   headless, so out of TestContext the scene and each of its five children (RackScrollWidget,
//   MenuBar, BrowserOverlay, MenuOverlay, ResizeHandle) all have an infinite box, and
//   `child->box.contains(pos)` is true at every position. Hit-testing is then meaningless:
//   a widget added last is reached at *any* position, and one added earlier is unreachable at
//   *every* position because a full-size overlay in front of it consumes first. That looks
//   exactly like "dispatch is broken headless", and is almost certainly what produced the claim
//   in Stroke.test.cpp:1338 — the failure is in layout, not in EventState.
//   layoutSceneForDispatch() above fixes it in two steps — assign the scene a finite box, hide
//   the scene's own full-size children — and every dispatch assertion in this file then passes.
//   Test::Harness (Step 2) should do exactly this when it builds the scene.
//
// Precondition 2 — two entry points must be re-implemented, not called:
//   - handleButton() — first line is `APP->window->isCursorLocked()`  (event.cpp:210)
//   - handleHover()  — same, plus `APP->window->getMods()`            (event.cpp:273, 277)
//   APP->window is null under TestContext and cannot be made non-null: Window's constructor
//   calls glfwCreateWindow() and throws on failure, its Internal is opaque, and it has no
//   virtuals — so it can be neither constructed nor subclassed headless.
//
// Consequence for Phase 2 Step 5 (EventDriver):
//   The driver does NOT need a shim replicating the recursion — that was the expensive fallback
//   the design doc feared, and it is not required. It needs only the ~15 lines of
//   handleButton()/handleHover() that sit *around* the recursion, calling
//   rootWidget->onButton()/->onHover() plus EventState's public setters (see the two
//   "composes from the Window-free primitives" cases above, which are that code). The two Window
//   calls it skips are cursor-lock gating (always false in a test) and modifier-key polling (a
//   test supplies mods explicitly), so nothing of test value is lost. Everything else routes
//   through Rack's real code path, so hit-testing, geometry, ordering and EventState bookkeeping
//   are all genuinely under test.
//
//   One coupling for Step 6 (deterministic clock): handleButton()'s double-click detection calls
//   system::getTime(). The re-implementation should route that through the existing
//   vcv::FileAccess::getTime() seam so double-click tests are deterministic.
//
//   One API detail the driver must expose correctly: rack::app::Scene is itself an OpaqueWidget
//   (Scene.hpp:12), so a left-click that matches nothing is consumed by the *scene*, not left
//   with a null target. `consumedBy()` in the design sketch must therefore be compared against
//   the expected widget; comparing against nullptr to mean "missed" would never be true.
//
// Consequence for Stroke.test.cpp:1338:
//   The comment there is too strong and should be corrected in Step 5. handleKey() — the entry
//   point Stroke actually cares about — works headless as-is; what was missing was the scene
//   layout, not a usable EventState.
//
// ---------------------------------------------------------------------------------------------
// ADDENDUM — three things this spike got wrong or missed, found while building Step 5's
// EventDriver (src/test/test_events.hpp). Recorded here so this file is not read as the last
// word; see var/TestFramework_review.md's Step 5 for the full write-up.
//
// 1. "A real ModuleWidget is hit-testable once positioned" is true, but incomplete: a real
//    ModuleWidget cannot be *left-pressed* headless at all. ModuleWidget::onDragStart
//    (ModuleWidget.cpp:444) dereferences APP->window->fbDirtyOnSubpixelChange(), so
//    setDraggedWidget() — which handleButton() calls on every press — segfaults. This spike's
//    ModuleWidget case only calls the raw recursion, which never reaches setDraggedWidget, so
//    it never hit it. EventState's own setters ARE window-free, as claimed above; the
//    dereference is in the widget, one level down.
//
// 2. layoutSceneForDispatch()'s "hide the scene's children" is not sufficient on its own.
//    Scene::onHover() calls menuBar->show() whenever mousePos.y < menuBar->box.size.y — and an
//    un-sized menu bar's height is INFINITY, so that is every position. The first hover
//    dispatched through the scene un-hides a full-size menu bar, which consumes everything
//    after it. Test::SceneLayout also zeroes the children's boxes for this reason. This spike
//    never dispatched a Hover through the scene, so it never saw it.
//
// 3. Neutralising the scene's children makes APP->scene->rack unreachable too (it is a
//    descendant of rackScroll), so anything production code parents to the rack — Stroke's own
//    KeyContainer, for one — is invisible to dispatch. Harness::exposeRackWidgets() is the
//    opt-in escape hatch. Leaving rackScroll live instead does not work:
//    RackScrollWidget::onHoverScroll dereferences APP->window unconditionally.
// ---------------------------------------------------------------------------------------------

// This binary's plugin entry point (see src/test/CONVERTING.md).
void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelStroke);
}

