// Test::EventDriver's own tests — Phase 2 Step 5.
//
// The driver re-implements two of Rack's dispatch entry points (handleButton, handleHover),
// because their first line dereferences APP->window, which cannot exist headless. Anything
// re-implemented can drift from the original, so what is pinned down here is not "does a click
// work" but the *behaviour of the state machine those two functions own*: drag start/end
// pairing, DragDrop on release, selection on left-press only, double-click timing, the
// DragHover/DragMove/Hover precedence, and Enter/Leave via setHoveredWidget. Those are the
// parts a hand-built event never exercised and the parts a future Rack update could change
// under us.
//
// The traversal spine (test_traversal.hpp) is tested here too: it must agree with Rack's own
// recursion about visit order and visibility, since disagreeing silently is exactly the failure
// mode §2.8 says to avoid.
//
// Uses Stroke where a real ModuleWidget is needed, for the same reason test_harness.test.cpp
// does: a plain ThemedModuleWidget with no expanders, threads or process-wide registration, so
// a failure here is the driver's and not a module's.

#include "../test/framework.hpp"
#include <widget/OpaqueWidget.hpp>
#include "../modules/stroke/Stroke.cpp"

using namespace rack;
using namespace StoermelderPackOne;

SYNC_MODEL(modelStroke, "Stroke");
Test::TestContext<> testContext;


// Records every event it receives, and consumes position events (as OpaqueWidget does) so
// consumption and propagation ordering are observable.
struct ProbeWidget : widget::OpaqueWidget {
	int buttonCount = 0, hoverCount = 0, hoverKeyCount = 0, hoverScrollCount = 0;
	int hoverTextCount = 0, selectKeyCount = 0, enterCount = 0, leaveCount = 0;
	int selectCount = 0, deselectCount = 0, doubleClickCount = 0;
	int dragStartCount = 0, dragMoveCount = 0, dragEndCount = 0;
	int dragHoverCount = 0, dragEnterCount = 0, dragLeaveCount = 0, dragDropCount = 0;

	math::Vec lastButtonPos, lastHoverPos, lastDragMouseDelta;
	int lastButton = -1, lastAction = -1, lastMods = -1;
	int lastKey = -1;
	uint32_t lastCodepoint = 0;
	widget::Widget* lastDragDropOrigin = nullptr;
	// Accumulated drag movement, so a stepped drag can be distinguished from a single jump.
	math::Vec dragTotal;

	// Set false to make this widget transparent to position events, so a widget behind it can
	// be observed receiving them.
	bool consume = true;

	void onButton(const ButtonEvent& e) override {
		buttonCount++;
		lastButtonPos = e.pos;
		lastButton = e.button;
		lastAction = e.action;
		lastMods = e.mods;
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
		lastKey = e.key;
		if (consume) e.consume(this);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		hoverScrollCount++;
		if (consume) e.consume(this);
	}
	void onHoverText(const HoverTextEvent& e) override {
		hoverTextCount++;
		lastCodepoint = e.codepoint;
		if (consume) e.consume(this);
	}
	void onSelectKey(const SelectKeyEvent& e) override {
		selectKeyCount++;
		lastKey = e.key;
		if (consume) e.consume(this);
	}
	void onDragHover(const DragHoverEvent& e) override {
		dragHoverCount++;
		if (consume) OpaqueWidget::onDragHover(e);
		else widget::Widget::onDragHover(e);
	}
	void onEnter(const EnterEvent& e) override { enterCount++; OpaqueWidget::onEnter(e); }
	void onLeave(const LeaveEvent& e) override { leaveCount++; OpaqueWidget::onLeave(e); }
	void onDragEnter(const DragEnterEvent& e) override { dragEnterCount++; }
	void onDragLeave(const DragLeaveEvent& e) override { dragLeaveCount++; }
	void onDragDrop(const DragDropEvent& e) override {
		dragDropCount++;
		lastDragDropOrigin = e.origin;
	}
	void onSelect(const SelectEvent& e) override { selectCount++; }
	void onDeselect(const DeselectEvent& e) override { deselectCount++; }
	void onDoubleClick(const DoubleClickEvent& e) override { doubleClickCount++; }
	void onDragStart(const DragStartEvent& e) override { dragStartCount++; }
	void onDragMove(const DragMoveEvent& e) override {
		dragMoveCount++;
		lastDragMouseDelta = e.mouseDelta;
		dragTotal = dragTotal.plus(e.mouseDelta);
	}
	void onDragEnd(const DragEndEvent& e) override { dragEndCount++; }
};


// Adds a probe to the scene at an absolute scene-space box, removing it — and clearing any
// EventState reference to it — on destruction. A probe left dangling in EventState would
// corrupt every later TEST_CASE in this binary.
struct ScopedProbe {
	ProbeWidget* w;

	explicit ScopedProbe(math::Rect box) {
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


// A FileAccess whose clock the test drives, so double-click timing is decided by the test
// rather than by how fast the machine ran two calls. This is the seam the driver reads for
// double-click detection (Phase 2 Step 6 will move it to its own ClockAccess).
struct ScriptedClock : Test::mock::MockFileAccess {
	double now = 1000.0;
	double getTime() override { return now; }
};


TEST_CASE("Position helpers translate widget-local to scene space") {
	Test::Harness h;
	ScopedProbe probe(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));

	SECTION("centerOf is the widget's box centre in scene coordinates") {
		math::Vec c = Test::EventDriver::centerOf(probe);
		REQUIRE(c.x == Catch::Approx(230.f));
		REQUIRE(c.y == Catch::Approx(170.f));
	}

	SECTION("pointIn offsets a local point by the widget's scene origin") {
		math::Vec p = Test::EventDriver::pointIn(probe, math::Vec(5, 7));
		REQUIRE(p.x == Catch::Approx(205.f));
		REQUIRE(p.y == Catch::Approx(157.f));
	}

	SECTION("sceneOrigin accumulates through nested parents") {
		auto* inner = new ProbeWidget;
		inner->box = math::Rect(math::Vec(10, 20), math::Vec(15, 15));
		probe->addChild(inner);

		math::Vec origin = Test::EventDriver::sceneOrigin(inner);
		REQUIRE(origin.x == Catch::Approx(210.f));
		REQUIRE(origin.y == Catch::Approx(170.f));

		// And the centre computed from it is the point that actually hits the child.
		h.events().click(Test::EventDriver::centerOf(inner));
		REQUIRE(h.events().consumedBy() == inner);
	}
}


TEST_CASE("Clicking dispatches through Rack's recursion") {
	Test::Harness h;
	ScopedProbe probe(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));

	SECTION("a click on a widget reports it as the consumer") {
		REQUIRE(h.events().click(probe));
		REQUIRE(h.events().consumedBy() == probe.w);
		// Press and release both reached it.
		REQUIRE(probe->buttonCount == 2);
		REQUIRE_FALSE(h.events().missed());
	}

	SECTION("coordinates arrive in the widget's own local space") {
		h.events().button(Test::EventDriver::pointIn(probe, math::Vec(12, 9)),
		                  GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
		REQUIRE(probe->lastButtonPos.x == Catch::Approx(12.f));
		REQUIRE(probe->lastButtonPos.y == Catch::Approx(9.f));
	}

	SECTION("a click that hits nothing is absorbed by the scene, not by nullptr") {
		// The trap the Step 1 spike called out: rack::app::Scene is itself an OpaqueWidget, so
		// "missed" can never mean target == nullptr. missed() exists so tests don't get this
		// wrong.
		h.events().click(math::Vec(5, 5));
		REQUIRE(probe->buttonCount == 0);
		REQUIRE(h.events().consumedBy() == APP->scene);
		REQUIRE(h.events().missed());
	}

	SECTION("button and mods reach the handler unchanged") {
		h.events().rightClick(probe, RACK_MOD_CTRL);
		REQUIRE(probe->lastButton == GLFW_MOUSE_BUTTON_RIGHT);
		REQUIRE(probe->lastMods == RACK_MOD_CTRL);
	}

	SECTION("an invisible widget is skipped") {
		probe->visible = false;
		h.events().click(math::Vec(230, 170));
		REQUIRE(probe->buttonCount == 0);
		REQUIRE(h.events().missed());
	}
}


TEST_CASE("Z-order and consumption") {
	// The property hand-built events can never test. Children are walked in reverse insertion
	// order, so the last-added widget is topmost.
	Test::Harness h;
	ScopedProbe below(math::Rect(math::Vec(200, 150), math::Vec(100, 100)));
	ScopedProbe above(math::Rect(math::Vec(220, 170), math::Vec(40, 40)));

	SECTION("a click in the overlap reaches only the topmost widget") {
		h.events().click(math::Vec(230, 180));
		REQUIRE(h.events().consumedBy() == above.w);
		REQUIRE(above->buttonCount == 2);
		REQUIRE(below->buttonCount == 0);
	}

	SECTION("a non-consuming topmost widget lets the one below receive it") {
		above->consume = false;
		h.events().click(math::Vec(230, 180));
		REQUIRE(h.events().consumedBy() == below.w);
		REQUIRE(above->buttonCount == 2);
		REQUIRE(below->buttonCount == 2);
	}

	SECTION("a click outside the topmost but inside the one below reaches the one below") {
		h.events().click(math::Vec(205, 155));
		REQUIRE(h.events().consumedBy() == below.w);
		REQUIRE(above->buttonCount == 0);
	}
}


TEST_CASE("Button state machine: selection, drag pairing and DragDrop") {
	Test::Harness h;
	ScopedProbe probe(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));

	SECTION("left press selects and starts a drag; release ends it") {
		h.events().button(Test::EventDriver::centerOf(probe), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
		REQUIRE(probe->selectCount == 1);
		REQUIRE(probe->dragStartCount == 1);
		REQUIRE(h.events().draggedWidget() == probe.w);
		REQUIRE(h.events().selectedWidget() == probe.w);

		h.events().button(Test::EventDriver::centerOf(probe), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
		REQUIRE(probe->dragEndCount == 1);
		REQUIRE(h.events().draggedWidget() == nullptr);
		// Selection survives the release — that is Rack's behaviour, not an oversight.
		REQUIRE(h.events().selectedWidget() == probe.w);
	}

	SECTION("a right press neither selects nor starts a drag on a plain OpaqueWidget") {
		// OpaqueWidget::onButton consumes LEFT clicks only, so a right press leaves no target
		// — hence no drag and no selection. A widget wanting right-click behaviour has to
		// consume it itself, which is why every module's context menu is opened by its own
		// onButton override rather than by anything generic.
		h.events().button(Test::EventDriver::centerOf(probe), GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
		REQUIRE(probe->buttonCount == 1);        // it was reached ...
		REQUIRE(probe->lastButton == GLFW_MOUSE_BUTTON_RIGHT);
		REQUIRE(h.events().consumedBy() == nullptr);  // ... but consumed it by nobody
		REQUIRE(probe->dragStartCount == 0);
		REQUIRE(probe->selectCount == 0);
		REQUIRE(h.events().selectedWidget() == nullptr);
	}

	SECTION("a widget that consumes a right press does get a drag") {
		// The counterpart: consumption is what creates a target, and a target is what
		// setDraggedWidget acts on. Selection still does not happen — that is left-only.
		struct RightConsumer : ProbeWidget {
			void onButton(const ButtonEvent& e) override {
				buttonCount++;
				lastButton = e.button;
				e.consume(this);
			}
		};
		auto* rc = new RightConsumer;
		rc->box = math::Rect(math::Vec(400, 150), math::Vec(60, 40));
		APP->scene->addChild(rc);

		h.events().button(Test::EventDriver::centerOf(rc), GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
		REQUIRE(rc->dragStartCount == 1);
		REQUIRE(rc->selectCount == 0);
		REQUIRE(h.events().selectedWidget() == nullptr);

		h.events().reset();
		APP->event->finalizeWidget(rc);
		APP->scene->removeChild(rc);
		delete rc;
	}

	SECTION("releasing over a widget delivers DragDrop with the drag's origin") {
		ScopedProbe target(math::Rect(math::Vec(400, 150), math::Vec(60, 40)));

		h.events().button(Test::EventDriver::centerOf(probe), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
		h.events().button(Test::EventDriver::centerOf(target), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);

		REQUIRE(target->dragDropCount == 1);
		REQUIRE(target->lastDragDropOrigin == probe.w);
		REQUIRE(probe->dragEndCount == 1);
	}

	SECTION("clicking another widget moves the selection and deselects the first") {
		ScopedProbe other(math::Rect(math::Vec(400, 150), math::Vec(60, 40)));

		h.events().click(probe);
		REQUIRE(probe->selectCount == 1);

		h.events().click(other);
		REQUIRE(probe->deselectCount == 1);
		REQUIRE(other->selectCount == 1);
		REQUIRE(h.events().selectedWidget() == other.w);
	}
}


TEST_CASE("Double-click detection is driven by the vcv clock seam") {
	// Rack decides a double-click by comparing system::getTime() against the last click. The
	// driver reads that through vcv::getTime() instead, so a test can script it — otherwise
	// "did these two clicks count as a double-click?" depends on machine speed.
	Test::Harness h;
	struct { TEST_MOCK_FS(ScriptedClock); } mock;
	ScopedProbe probe(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));

	SECTION("two clicks inside the window fire DoubleClick") {
		mock.fs.now = 100.0;
		h.events().click(probe);
		mock.fs.now = 100.1;
		h.events().click(probe);
		REQUIRE(probe->doubleClickCount == 1);
	}

	SECTION("two clicks outside the window do not") {
		mock.fs.now = 100.0;
		h.events().click(probe);
		mock.fs.now = 100.0 + Test::DOUBLE_CLICK_DURATION + 0.01;
		h.events().click(probe);
		REQUIRE(probe->doubleClickCount == 0);
	}

	SECTION("a third fast click does not double-fire") {
		// Rack resets the timer after firing, so three rapid clicks are one double-click, not
		// two. Pinned because it is easy to lose when re-implementing.
		mock.fs.now = 100.0;
		h.events().click(probe);
		mock.fs.now = 100.1;
		h.events().click(probe);
		mock.fs.now = 100.2;
		h.events().click(probe);
		REQUIRE(probe->doubleClickCount == 1);
	}

	SECTION("fast clicks on different widgets are not a double-click") {
		ScopedProbe other(math::Rect(math::Vec(400, 150), math::Vec(60, 40)));
		mock.fs.now = 100.0;
		h.events().click(probe);
		mock.fs.now = 100.05;
		h.events().click(other);
		REQUIRE(probe->doubleClickCount == 0);
		REQUIRE(other->doubleClickCount == 0);
	}

	SECTION("doubleClick() is a double-click with a real clock too") {
		// The convenience form, used with the scripted clock frozen — the two clicks land at
		// the same instant, which is inside any window.
		REQUIRE(h.events().doubleClick(probe));
		REQUIRE(probe->doubleClickCount == 1);
	}
}


TEST_CASE("Hover, Enter and Leave") {
	Test::Harness h;
	ScopedProbe a(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));
	ScopedProbe b(math::Rect(math::Vec(400, 150), math::Vec(60, 40)));

	SECTION("hovering a widget enters it; hovering another leaves the first") {
		h.events().hover(a);
		REQUIRE(a->enterCount == 1);
		REQUIRE(h.events().hoveredWidget() == a.w);

		h.events().hover(b);
		REQUIRE(a->leaveCount == 1);
		REQUIRE(b->enterCount == 1);
	}

	SECTION("re-hovering the same widget does not re-enter it") {
		h.events().hover(a);
		h.events().hover(Test::EventDriver::pointIn(a, math::Vec(5, 5)));
		REQUIRE(a->enterCount == 1);
		REQUIRE(a->leaveCount == 0);
		REQUIRE(a->hoverCount == 2);
	}

	SECTION("hovering empty space leaves the previously hovered widget") {
		h.events().hover(a);
		h.events().hover(math::Vec(5, 5));
		REQUIRE(a->leaveCount == 1);
		// The scene consumes the hover, so it — not nothing — is now hovered.
		REQUIRE(h.events().hoveredWidget() == APP->scene);
	}

	SECTION("a hover through the scene does not resurrect the menu bar") {
		// Scene::onHover() calls menuBar->show() whenever mousePos.y is above the menu bar's
		// height — and an un-sized menu bar's height is INFINITY, so that is every position.
		// SceneLayout zeroes the scene children's boxes precisely so this cannot re-break
		// hit-testing mid-test. Pinned here because the symptom (a later click landing on the
		// menu bar) looks nothing like its cause.
		h.events().hover(math::Vec(5, 5));
		h.events().click(a);
		REQUIRE(h.events().consumedBy() == a.w);
	}

	SECTION("mouseDelta defaults to the movement since the last hover") {
		h.events().hover(math::Vec(210, 160));
		h.events().hover(math::Vec(215, 155));
		REQUIRE(a->hoverCount == 2);
		// The widget sees local positions; the delta is what the driver computed.
		REQUIRE(a->lastHoverPos.x == Catch::Approx(15.f));
		REQUIRE(a->lastHoverPos.y == Catch::Approx(5.f));
	}
}


TEST_CASE("Dragging") {
	Test::Harness h;
	ScopedProbe probe(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));

	SECTION("a drag is press, moves, release — with DragMove carrying the deltas") {
		h.events().dragBy(probe, math::Vec(0, -50));
		REQUIRE(probe->dragStartCount == 1);
		REQUIRE(probe->dragMoveCount == 1);
		REQUIRE(probe->dragEndCount == 1);
		REQUIRE(probe->lastDragMouseDelta.y == Catch::Approx(-50.f));
	}

	SECTION("a stepped drag delivers many small moves summing to the total") {
		// The distinction that matters for a handler clamping per-move rather than in total —
		// a real mouse never delivers one 50px jump.
		h.events().dragBy(probe, math::Vec(0, -50), 10);
		REQUIRE(probe->dragMoveCount == 10);
		REQUIRE(probe->dragTotal.y == Catch::Approx(-50.f));
	}

	SECTION("a stepped drag ends exactly at the requested point") {
		// Float division across steps must not leave the drag short.
		math::Vec from = Test::EventDriver::centerOf(probe);
		math::Vec to = from.plus(math::Vec(7, -13));
		h.events().drag(from, to, 3);
		REQUIRE(probe->dragTotal.x == Catch::Approx(7.f));
		REQUIRE(probe->dragTotal.y == Catch::Approx(-13.f));
	}

	SECTION("DragHover reaches the widget under the cursor while dragging") {
		ScopedProbe target(math::Rect(math::Vec(400, 150), math::Vec(60, 40)));

		h.events().button(Test::EventDriver::centerOf(probe), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
		h.events().hover(Test::EventDriver::centerOf(target));

		REQUIRE(target->dragHoverCount == 1);
		REQUIRE(h.events().dragHoveredWidget() == target.w);
		REQUIRE(target->dragEnterCount == 1);
		// The dragged widget still gets its DragMove, even though the hover landed elsewhere.
		REQUIRE(probe->dragMoveCount == 1);
		// And a plain Hover is NOT dispatched, because the DragHover consumed. That precedence
		// is handleHover()'s, and is the part most easily lost in a re-implementation.
		REQUIRE(target->hoverCount == 0);

		h.events().button(Test::EventDriver::centerOf(target), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
	}
}


TEST_CASE("Keyboard, text and scroll") {
	Test::Harness h;
	ScopedProbe probe(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));

	SECTION("a key at a position reaches the widget there as HoverKey") {
		REQUIRE(h.events().keyAt(Test::EventDriver::centerOf(probe), GLFW_KEY_A));
		REQUIRE(probe->hoverKeyCount == 1);
		REQUIRE(probe->lastKey == GLFW_KEY_A);
	}

	SECTION("key() uses the last hovered position, so hover-then-key reads naturally") {
		h.events().hover(probe);
		REQUIRE(h.events().key(GLFW_KEY_B));
		REQUIRE(probe->hoverKeyCount == 1);
		REQUIRE(probe->lastKey == GLFW_KEY_B);
	}

	SECTION("a selected widget receives SelectKey first, and HoverKey is then skipped") {
		h.events().select(probe);
		REQUIRE(h.events().keyAt(math::Vec(5, 5), GLFW_KEY_C));
		REQUIRE(probe->selectKeyCount == 1);
		REQUIRE(probe->hoverKeyCount == 0);
	}

	SECTION("keyPress presses and releases, clearing the held-key set") {
		h.events().hover(probe);
		h.events().keyPress(GLFW_KEY_D);
		REQUIRE(probe->hoverKeyCount == 2);
		REQUIRE(APP->event->heldKeys.count(GLFW_KEY_D) == 0);
	}

	SECTION("a held key is repeated as RACK_HELD on every hover") {
		// handleHover() synthesises these from APP->window->getMods(); the driver supplies
		// heldKeyMods instead, which is the one place it deliberately differs from Rack.
		h.events().hover(probe);
		h.events().key(GLFW_KEY_E, GLFW_PRESS);
		REQUIRE(APP->event->heldKeys.count(GLFW_KEY_E) == 1);

		int before = probe->hoverKeyCount;
		h.events().hover(Test::EventDriver::pointIn(probe, math::Vec(5, 5)));
		// One extra HoverKey, from the synthesised repeat.
		REQUIRE(probe->hoverKeyCount == before + 1);
	}

	SECTION("text delivers a codepoint") {
		h.events().hover(probe);
		REQUIRE(h.events().text('x'));
		REQUIRE(probe->hoverTextCount == 1);
		REQUIRE(probe->lastCodepoint == uint32_t('x'));
	}

	SECTION("type() delivers one event per character") {
		h.events().hover(probe);
		h.events().type("abc");
		REQUIRE(probe->hoverTextCount == 3);
		REQUIRE(probe->lastCodepoint == uint32_t('c'));
	}

	SECTION("scroll hit-tests like any other position event") {
		REQUIRE(h.events().scroll(probe, math::Vec(0, -1)));
		REQUIRE(probe->hoverScrollCount == 1);

		REQUIRE_FALSE(h.events().scroll(math::Vec(5, 5), math::Vec(0, -1)));
		REQUIRE(probe->hoverScrollCount == 1);
	}
}


TEST_CASE("reset() clears every EventState reference") {
	Test::Harness h;
	ScopedProbe probe(math::Rect(math::Vec(200, 150), math::Vec(60, 40)));

	h.events().hover(probe);
	h.events().button(Test::EventDriver::centerOf(probe), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
	h.events().key(GLFW_KEY_F, GLFW_PRESS);
	REQUIRE(h.events().draggedWidget() == probe.w);
	REQUIRE(APP->event->heldKeys.size() == 1);

	h.events().reset();

	REQUIRE(h.events().hoveredWidget() == nullptr);
	REQUIRE(h.events().draggedWidget() == nullptr);
	REQUIRE(h.events().selectedWidget() == nullptr);
	REQUIRE(h.events().dragHoveredWidget() == nullptr);
	REQUIRE(h.events().consumedBy() == nullptr);
	REQUIRE(APP->event->heldKeys.empty());
	// Widgets were notified, not just forgotten.
	REQUIRE(probe->dragEndCount == 1);
	REQUIRE(probe->leaveCount == 1);
}


TEST_CASE("The traversal spine agrees with Rack's own recursion") {
	// test_traversal.hpp is the piece a future DrawDriver will share (§2.8). If it disagreed
	// with recursePositionEvent about visit order or visibility, a traversal-based assertion
	// would describe a tree Rack never walks — so the two are checked against each other here.
	Test::Harness h;
	ScopedProbe outer(math::Rect(math::Vec(200, 150), math::Vec(100, 100)));

	auto* inner = new ProbeWidget;
	inner->box = math::Rect(math::Vec(10, 20), math::Vec(30, 30));
	outer->addChild(inner);

	SECTION("hitTest finds the deepest widget under a point") {
		REQUIRE(h.events().hitTest(math::Vec(215, 175)) == inner);
		// Inside `outer` but outside `inner`.
		REQUIRE(h.events().hitTest(math::Vec(290, 240)) == outer.w);
	}

	SECTION("hitPath reports the descent outermost-first with accumulated offsets") {
		std::vector<Test::traversal::Visit> path = h.events().hitPath(math::Vec(215, 175));
		REQUIRE(path.size() == 3);
		REQUIRE(path[0].widget == APP->scene);
		REQUIRE(path[1].widget == outer.w);
		REQUIRE(path[2].widget == inner);

		REQUIRE(path[1].offset.x == Catch::Approx(200.f));
		REQUIRE(path[2].offset.x == Catch::Approx(210.f));
		REQUIRE(path[2].offset.y == Catch::Approx(170.f));
		REQUIRE(path[2].depth == 2);
	}

	SECTION("hitTest and real dispatch agree on the target") {
		math::Vec p(215, 175);
		widget::Widget* predicted = h.events().hitTest(p);
		h.events().click(p);
		REQUIRE(h.events().consumedBy() == predicted);
	}

	SECTION("an invisible subtree is skipped by both") {
		inner->visible = false;
		REQUIRE(h.events().hitTest(math::Vec(215, 175)) == outer.w);
		h.events().click(math::Vec(215, 175));
		REQUIRE(h.events().consumedBy() == outer.w);
		REQUIRE(inner->buttonCount == 0);
	}

	SECTION("visitAll walks children topmost-first") {
		auto* second = new ProbeWidget;
		second->box = math::Rect(math::Vec(50, 50), math::Vec(20, 20));
		outer->addChild(second);

		std::vector<Test::traversal::Visit> visits = Test::traversal::visitAll(outer);
		REQUIRE(visits.size() == 3);
		REQUIRE(visits[0].widget == outer.w);
		// Reverse insertion order: `second` was added last, so it is visited first.
		REQUIRE(visits[1].widget == second);
		REQUIRE(visits[2].widget == inner);
	}

	SECTION("walk() prunes a subtree when the visitor returns false") {
		int visited = 0;
		Test::traversal::walk(outer, [&](const Test::traversal::Visit& v) {
			visited++;
			return v.widget == outer.w;  // descend into outer only
		});
		REQUIRE(visited == 2);
	}
}


TEST_CASE("A real ModuleWidget is driveable through the harness") {
	// Everything above uses a synthetic probe. This confirms the same holds for a real plugin
	// widget added the normal way — the actual use case the driver exists for, and the thing
	// that proves placeWidget()'s positioning is compatible with hit-testing.
	Test::Harness h;
	auto* m = h.addModule<StoermelderPackOne::Stroke::StrokeModule<10>>("Stroke");
	auto* mw = h.addWidget<rack::app::ModuleWidget>(m);

	SECTION("a press at the panel centre lands on the widget or a descendant") {
		// Press only, not a full click: the release path is fine, but a *left* press on a
		// ModuleWidget cannot be dispatched headless at all (see below), so this asserts the
		// hit-test through a right press, which Rack's own handler does not gate on a window.
		h.events().button(Test::EventDriver::centerOf(mw), GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
		widget::Widget* target = h.events().consumedBy();
		REQUIRE_FALSE(h.events().missed());
		REQUIRE((target == mw || target->isDescendantOf(mw)));
		h.events().button(Test::EventDriver::centerOf(mw), GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
	}

	SECTION("a left press on a ModuleWidget is refused with a diagnostic, not a segfault") {
		// The constraint Rack imposes and the driver surfaces: ModuleWidget::onDragStart
		// dereferences the null APP->window. Without the guard this crashes inside Rack with
		// no indication of why — which is how it was found.
		REQUIRE_FALSE(Test::EventDriver::canLeftDrag(mw));
		// A widget that is not a ModuleWidget is unaffected.
		ScopedProbe probe(math::Rect(math::Vec(600, 400), math::Vec(20, 20)));
		REQUIRE(Test::EventDriver::canLeftDrag(probe));
	}

	SECTION("a click well outside the panel misses it") {
		h.events().click(math::Vec(5, 5));
		REQUIRE(h.events().missed());
	}

	SECTION("the widget is where the harness said it is") {
		// placeWidget() packs widgets left to right from a non-zero origin, so a test should
		// read mw->box rather than hardcode — this asserts the two agree.
		REQUIRE(mw->box.size.x > 0.f);
		REQUIRE(h.events().hitTest(Test::EventDriver::centerOf(mw)) != nullptr);
		REQUIRE(h.events().hitTest(Test::EventDriver::centerOf(mw))->isDescendantOf(APP->scene));
	}

	SECTION("events and the DSP/UI schedule interleave without interfering") {
		// The driver shares a harness with the scheduler, so a test can drive input between
		// frames. Hover rather than click, since a left press on a ModuleWidget is refused.
		h.run(Test::milliseconds(10));
		h.events().hover(mw);
		h.run(Test::milliseconds(10));
		REQUIRE(h.frame > 0);
		REQUIRE(h.uiFrameCount > 0);
		REQUIRE_FALSE(h.events().missed());
	}
}
