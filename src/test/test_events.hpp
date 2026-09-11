#pragma once
#include "test_plugin.hpp"
#include "test_traversal.hpp"
#include "../vcv/fs.hpp"
#include "../vcv/ui.hpp"
#include <rack.hpp>
#include <widget/Widget.hpp>
#include <widget/event.hpp>
#include <app/Scene.hpp>
#include <app/ModuleWidget.hpp>
#include <app/RackWidget.hpp>
#include <vector>
#include <string>

// Test::EventDriver — synthetic input through Rack's own dispatch.
//
// Until now every widget test in this suite built event structs by hand and called the handler
// directly (Stroke.test.cpp is the largest example). That tests the handler, and nothing else:
// hit-testing, coordinate translation, z-order, consumption ordering and EventState's own
// bookkeeping are all skipped, and those are where widget bugs actually live. The reason given
// was that "Rack's EventState is not usable headless for synthetic dispatch" — which the Phase 2
// Step 1 spike (src/test/EventDispatchSpike.test.cpp) measured and found to be too strong. It is
// usable, subject to two preconditions:
//
//   1. The scene must be laid out. Widget's default box is infinite, so without a finite scene
//      box and with the scene's own full-size overlays visible, every position matches
//      everything and hit-testing is meaningless in both directions. Test::Harness owns this
//      (see SceneLayout) — which is why this driver is reached through a Harness.
//
//   2. handleButton() and handleHover() cannot be called: their first line dereferences
//      APP->window, which is null in every test binary and cannot be made non-null (Window's
//      constructor calls glfwCreateWindow(), its Internal is opaque, it has no virtuals). Both
//      are re-implemented below.
//
// Everything else routes through Rack's real code. The re-implementations reproduce
// EventState::handleButton/handleHover (Rack/src/widget/event.cpp) line for line apart from the
// window reads, which are:
//
//   - isCursorLocked() — always false in a test, and its only effect is to suppress dispatch
//     entirely, so a test that wanted it would be testing nothing.
//   - getMods() — polled to synthesise RACK_HELD repeats for held keys. Routed through the
//     vcv::ui::getWindowMods() seam, which is the same value the module under test reads, so a
//     test sets mods once (see setMods) and the widget and the key repeats cannot disagree.
//
// So the recursion, the geometry, the ordering, the consumption rules and the EventState state
// machine are all genuinely under test; only one thing a headless test cannot have is absent.
//
// Usage:
//   Test::Harness h;
//   auto* mw = h.addWidget<MyWidget>(m);
//   h.events().click(mw->box.pos.plus(Vec(10, 20)));
//   REQUIRE(h.events().consumedBy() == expectedChild);
//
// Note on positions: every coordinate here is in *scene* space, the same space widget boxes are
// in, because that is what Rack's dispatch entry points take. Read a widget's own box for it
// (`mw->box.pos.plus(...)`, or the centerOf/pointIn helpers below) rather than hardcoding — the
// harness packs widgets left to right, so their positions are deterministic but implicit.
//
// ONE THING THIS CANNOT DO: left-drag a rack::app::ModuleWidget. Not a gap in the driver — a
// hard constraint from Rack. ModuleWidget::onDragStart/onDragEnd (ModuleWidget.cpp:444-470)
// dereference APP->window directly, for the "disable subpixel redraw while dragging" hack, and
// APP->window is null in every test binary and cannot be made non-null. So a left press on a
// ModuleWidget segfaults inside Rack's own handler, before any test assertion runs. The driver
// detects this and FAILs with a diagnostic instead (see requireDraggable), because a segfault
// with no message is the worst possible way to learn it. Everything else about a ModuleWidget —
// left click, right click, hover, scroll, keys, and dragging its *children* (knobs, ports,
// which have their own handlers) — works normally. This was missed by the Step 1 spike, which
// dispatched into a ModuleWidget through the raw recursion only and so never reached
// setDraggedWidget().

namespace Test {

// How long after a click a second click at the same widget counts as a double-click. Rack's own
// constant, inlined here because it is a local in handleButton().
static constexpr double DOUBLE_CLICK_DURATION = 0.3;

struct EventDriver {
	// The widget dispatch starts from. APP->scene in normal use — EventState::rootWidget is the
	// scene, and the scene is what SceneLayout laid out.
	rack::widget::Widget* rootWidget = nullptr;

	// The target of the most recent position event, i.e. the widget that consumed it.
	//
	// IMPORTANT: compare this against the widget you expect, never against nullptr to mean
	// "missed". rack::app::Scene is itself an OpaqueWidget, so a left-click matching nothing is
	// consumed *by the scene* and reported as the target. Use missed() for that question.
	rack::widget::Widget* lastTarget = nullptr;

	// The last position hover() was called at, so drag() can compute mouseDelta the way a real
	// mouse would rather than making the caller track it.
	rack::math::Vec lastMousePos;

	explicit EventDriver(rack::widget::Widget* root) : rootWidget(root) {}

	// ---- Modifiers -------------------------------------------------------------------------

	// The held modifiers every part of a test agrees on: what a widget reading
	// vcv::ui::getWindowMods() sees, and what the synthesised RACK_HELD key repeats in hover()
	// carry. Not a field on the driver — the value lives in the installed vcv::UiAccess, which
	// is the only place both the driver and the module under test can read it from, so the two
	// cannot be set to different things.
	//
	// A widget that gates on modifiers is the reason this exists: Rack's HoverScrollEvent has
	// no mods field, so Spin's and Mb's onHoverScroll() poll the window instead, and until the
	// seam carried mods those branches were unreachable under test.
	//
	//   h.events().setMods(RACK_MOD_CTRL);
	//   h.events().scroll(widget, Vec(0, 1));   // ctrl+scroll, as the widget sees it
	//
	// Takes a GLFW_MOD_* bitmask. Persists until changed, like a real held key; clearMods()
	// releases them, and reset() does not touch them (a suite that sets mods once for a whole
	// TEST_CASE should not have them silently dropped by an unrelated reset).
	void setMods(int mods) {
		StoermelderPackOne::vcv::uiAccessFor().testMods = mods;
	}

	void clearMods() { setMods(0); }

	int mods() const { return StoermelderPackOne::vcv::ui::getWindowMods(); }

	// ---- Queries ---------------------------------------------------------------------------

	rack::widget::Widget* consumedBy() const { return lastTarget; }

	// Whether the last position event reached nothing of interest — it fell through to the
	// scene, or to no widget at all. This is the check "did my click miss?" actually needs.
	bool missed() const {
		return lastTarget == nullptr || lastTarget == APP->scene;
	}

	rack::widget::Widget* hoveredWidget() const { return APP->event->hoveredWidget; }
	rack::widget::Widget* draggedWidget() const { return APP->event->draggedWidget; }
	rack::widget::Widget* dragHoveredWidget() const { return APP->event->dragHoveredWidget; }
	rack::widget::Widget* selectedWidget() const { return APP->event->selectedWidget; }

	// Whether a left press on this widget can be dispatched headless at all.
	//
	// False exactly for a rack::app::ModuleWidget, whose own onDragStart dereferences the
	// null APP->window (see the header comment). Exposed so a test that legitimately wants to
	// click around a panel can check rather than guess — and so the reason is discoverable
	// from the driver rather than from a stack trace.
	static bool canLeftDrag(rack::widget::Widget* w) {
		return dynamic_cast<rack::app::ModuleWidget*>(w) == nullptr;
	}

	// The geometry answer — what lies under a scene-space point, ignoring consumption. Shares
	// its traversal with a future DrawDriver; see test_traversal.hpp.
	rack::widget::Widget* hitTest(rack::math::Vec pos) const {
		return traversal::hitTest(rootWidget, pos);
	}
	std::vector<traversal::Visit> hitPath(rack::math::Vec pos) const {
		return traversal::hitPath(rootWidget, pos);
	}

	// The inner widget to click, found by type rather than by reconstructing where the panel put
	// it. See traversal::findDescendant — this is the form most widget tests want, because a
	// ModuleWidget's interesting children are constructor locals with no accessor.
	//
	// REQUIREs a match: "the widget I meant to click does not exist" is a test bug worth failing
	// at the lookup, not a nullptr that turns into a segfault or a silently-skipped assertion
	// three lines later.
	template <typename T>
	T* find(rack::widget::Widget* root) const {
		T* found = traversal::findDescendant<T>(root);
		REQUIRE(found != nullptr);
		return found;
	}

	template <typename T>
	std::vector<T*> findAll(rack::widget::Widget* root) const {
		return traversal::findDescendants<T>(root);
	}

	// ---- Position helpers ------------------------------------------------------------------

	// The centre of a widget in scene space. Almost every "click this widget" wants this, and
	// computing it by hand at each call site is how tests end up with hardcoded coordinates that
	// break when the harness packs widgets differently.
	static rack::math::Vec centerOf(rack::widget::Widget* w) {
		return sceneOrigin(w).plus(w->box.size.div(2));
	}

	// A widget-local point in scene space, for clicking a specific spot on a panel.
	static rack::math::Vec pointIn(rack::widget::Widget* w, rack::math::Vec local) {
		return sceneOrigin(w).plus(local);
	}

	// A widget's origin in scene space, accumulated up through its parents — so this works for a
	// nested child (a knob on a panel), not just a top-level ModuleWidget.
	static rack::math::Vec sceneOrigin(rack::widget::Widget* w) {
		rack::math::Vec origin;
		for (rack::widget::Widget* p = w; p != nullptr && p != APP->scene; p = p->parent) {
			origin = origin.plus(p->box.pos);
		}
		return origin;
	}

	// ---- Rack mouse position -----------------------------------------------------------------

	// Keeps APP->scene->rack->getMousePos() tracking synthetic input, which it otherwise cannot.
	//
	// This is not a convenience — without it a whole class of widget is untestable. 16 widgets
	// across 11 modules in this plugin (Tilt's grid and edge lanes, Maze, Hive, Glue's label,
	// Siren's waveform canvas, XySeq/XyScreen, Strip, MidiCat, Mb) read the *rack's* tracked
	// mouse position inside onDragStart/onDragMove rather than e.mouseDelta, because that is the
	// only way to get a position in a stable coordinate space across a drag. Rack maintains that
	// field in RackWidget::onHover()/onDragHover() as an event descends through the rack — but
	// APP->scene->rack is a descendant of rackScroll, which SceneLayout deliberately neutralises
	// (its onHoverScroll() dereferences the null APP->window; see SceneLayout's comment). So a
	// real drag() over such a widget dispatches perfectly and moves nothing: onDragMove fires
	// every step and reads the same pre-harness value every time, and the drag silently does
	// nothing while every assertion about dispatch still passes.
	//
	// Both handlers are public and do nothing but record e.pos into the field getMousePos()
	// reads before recursing (RackWidget.cpp:188-192, 226-230), so calling one directly
	// reproduces exactly what the recursion would have done — without making rackScroll live.
	//
	// The position is converted into the rack's own coordinate space, so a widget that compares
	// getMousePos() against a ModuleWidget's box (SirenDropHandler, Glue's ModuleLabelWidget,
	// Maze and Hive's drag handlers all do) sees the two in the same space rather than a scene
	// coordinate that happens to work only while the rack sits at the origin.
	void syncRackMousePos(rack::math::Vec scenePos) {
		if (!trackRackMousePos) return;
		rack::app::RackWidget* rackWidget = APP->scene->rack;
		if (!rackWidget) return;

		rack::math::Vec rackPos = scenePos.minus(sceneOrigin(rackWidget));
		if (APP->event->draggedWidget) {
			rack::widget::Widget::DragHoverEvent e;
			e.pos = rackPos;
			e.button = APP->event->dragButton;
			e.origin = APP->event->draggedWidget;
			rackWidget->onDragHover(e);
		}
		else {
			rack::widget::Widget::HoverEvent e;
			e.pos = rackPos;
			rackWidget->onHover(e);
		}
	}

	// Set false to leave getMousePos() alone — for a test whose subject is what a widget does with
	// a stale rack position, or one asserting that the rack subtree is unreachable by dispatch.
	bool trackRackMousePos = true;

	// The rack's currently tracked mouse position, in rack coordinates. Exposed so a test can
	// assert the sync happened rather than infer it from the widget's reaction.
	rack::math::Vec rackMousePos() const {
		return APP->scene->rack ? APP->scene->rack->getMousePos() : rack::math::Vec();
	}

	// ---- Buttons ---------------------------------------------------------------------------

	// Fails the test with an explanation rather than letting Rack segfault on a null
	// APP->window. Only a *left* press is affected: ModuleWidget::onDragStart's window
	// dereference is inside an `if (e.button == GLFW_MOUSE_BUTTON_LEFT)`, so a right-press
	// drag (which is how a context menu opens) is fine.
	static void requireDraggable(rack::widget::Widget* w, int button) {
		if (button != GLFW_MOUSE_BUTTON_LEFT) return;
		if (w == nullptr || canLeftDrag(w)) return;
		FAIL("EventDriver: left-pressing a ModuleWidget cannot be dispatched headless — "
		     "rack::app::ModuleWidget::onDragStart() dereferences APP->window, which is null "
		     "in a test binary and cannot be constructed. Target a child widget (knob, port, "
		     "button) instead, use rightClick() for the context menu, or check "
		     "EventDriver::canLeftDrag() first.");
	}

	// One button press or release, dispatched exactly as EventState::handleButton would.
	//
	// Returns whether a widget consumed it. Reproduces handleButton()'s full state machine:
	// drag start/end, DragDrop on release, selection on left-press, and double-click detection.
	bool button(rack::math::Vec pos, int button, int action, int mods = 0) {
		// Before dispatch, not after: a press handler that captures a drag origin reads
		// getMousePos() *during* onButton (TiltEdgeWidget's dragStartMouse, Maze's and Hive's
		// dragPos, XySeqWidget's), so syncing afterwards would leave the origin at the previous
		// position and offset the whole drag by one step.
		lastMousePos = pos;
		syncRackMousePos(pos);

		rack::widget::EventContext c;
		rack::widget::Widget::ButtonEvent e;
		e.context = &c;
		e.pos = pos;
		e.button = button;
		e.action = action;
		e.mods = mods;
		rootWidget->onButton(e);
		rack::widget::Widget* clickedWidget = c.target;
		lastTarget = clickedWidget;

		rack::widget::EventState* ev = APP->event;

		if (action == GLFW_PRESS) {
			requireDraggable(clickedWidget, button);
			ev->setDraggedWidget(clickedWidget, button);
		}

		if (action == GLFW_RELEASE) {
			ev->setDragHoveredWidget(nullptr);

			if (clickedWidget && ev->draggedWidget) {
				rack::widget::Widget::DragDropEvent eDragDrop;
				eDragDrop.button = ev->dragButton;
				eDragDrop.origin = ev->draggedWidget;
				clickedWidget->onDragDrop(eDragDrop);
			}

			ev->setDraggedWidget(nullptr, 0);
		}

		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
			ev->setSelectedWidget(clickedWidget);

			// Double-click timing goes through the vcv::fs seam rather than
			// rack::system::getTime() directly, so a test can script it with a FileAccess mock
			// and get a deterministic double-click instead of one that depends on how fast the
			// machine ran the two calls. (Phase 2 Step 6 will split this into its own
			// ClockAccess; this call site moves with it.)
			double clickTime = StoermelderPackOne::vcv::fs::getTime();
			if (clickedWidget
			    && clickTime - ev->lastClickTime <= DOUBLE_CLICK_DURATION
			    && ev->lastClickedWidget == clickedWidget) {
				rack::widget::Widget::DoubleClickEvent eDoubleClick;
				clickedWidget->onDoubleClick(eDoubleClick);
				ev->lastClickTime = -INFINITY;
				ev->lastClickedWidget = nullptr;
			}
			else {
				ev->lastClickTime = clickTime;
				ev->lastClickedWidget = clickedWidget;
			}
		}

		return !!clickedWidget;
	}

	// A full press-and-release at a scene-space point. consumedBy() afterwards reports the
	// release's target, which for a stationary click is the same widget as the press's.
	bool click(rack::math::Vec pos, int btn = GLFW_MOUSE_BUTTON_LEFT, int mods = 0) {
		bool pressed = button(pos, btn, GLFW_PRESS, mods);
		button(pos, btn, GLFW_RELEASE, mods);
		return pressed;
	}

	// A press-and-release on a widget's centre — the common case, without the caller doing
	// coordinate arithmetic.
	bool click(rack::widget::Widget* w, int btn = GLFW_MOUSE_BUTTON_LEFT, int mods = 0) {
		return click(centerOf(w), btn, mods);
	}

	bool rightClick(rack::math::Vec pos, int mods = 0) {
		return click(pos, GLFW_MOUSE_BUTTON_RIGHT, mods);
	}
	bool rightClick(rack::widget::Widget* w, int mods = 0) {
		return click(centerOf(w), GLFW_MOUSE_BUTTON_RIGHT, mods);
	}

	// Two clicks close enough together to register as a double-click.
	//
	// "Close enough" is decided by vcv::getTime(), so with the real clock this depends on the
	// two calls landing inside 300ms — true in practice but not a guarantee. A test that cares
	// should install a FileAccess mock with a scripted clock; see the driver's own tests.
	bool doubleClick(rack::math::Vec pos, int mods = 0) {
		click(pos, GLFW_MOUSE_BUTTON_LEFT, mods);
		return click(pos, GLFW_MOUSE_BUTTON_LEFT, mods);
	}
	bool doubleClick(rack::widget::Widget* w, int mods = 0) {
		return doubleClick(centerOf(w), mods);
	}

	// ---- Hover and drag ----------------------------------------------------------------------

	// Mouse motion to a scene-space point, dispatched as EventState::handleHover would.
	//
	// Drives DragHover/DragMove while a widget is dragged, and Hover (and so Enter/Leave, via
	// setHoveredWidget) otherwise. mouseDelta defaults to the movement since the previous
	// hover(), so a test states where the mouse is rather than how far it moved.
	bool hover(rack::math::Vec pos) {
		return hover(pos, pos.minus(lastMousePos));
	}

	bool hover(rack::math::Vec pos, rack::math::Vec mouseDelta) {
		rack::widget::EventState* ev = APP->event;
		lastMousePos = pos;
		syncRackMousePos(pos);

		// Synthesised RACK_HELD repeats, one per held key — handleHover()'s first block. Mods
		// come from the vcv::ui seam, which is where Rack's getMods() now routes.
		for (int key : ev->heldKeys) {
			int scancode = glfwGetKeyScancode(key);
			ev->handleKey(pos, key, scancode, RACK_HELD, mods());
		}

		if (ev->draggedWidget) {
			bool dragHovered = false;
			{
				rack::widget::EventContext cDragHover;
				rack::widget::Widget::DragHoverEvent eDragHover;
				eDragHover.context = &cDragHover;
				eDragHover.button = ev->dragButton;
				eDragHover.pos = pos;
				eDragHover.mouseDelta = mouseDelta;
				eDragHover.origin = ev->draggedWidget;
				rootWidget->onDragHover(eDragHover);

				ev->setDragHoveredWidget(cDragHover.target);
				if (cDragHover.target) {
					lastTarget = cDragHover.target;
					dragHovered = true;
				}
			}

			rack::widget::Widget::DragMoveEvent eDragMove;
			eDragMove.button = ev->dragButton;
			eDragMove.mouseDelta = mouseDelta;
			ev->draggedWidget->onDragMove(eDragMove);
			if (dragHovered) return true;
		}

		rack::widget::EventContext cHover;
		rack::widget::Widget::HoverEvent eHover;
		eHover.context = &cHover;
		eHover.pos = pos;
		eHover.mouseDelta = mouseDelta;
		rootWidget->onHover(eHover);

		ev->setHoveredWidget(cHover.target);
		if (cHover.target) {
			lastTarget = cHover.target;
			return true;
		}
		return false;
	}

	bool hover(rack::widget::Widget* w) { return hover(centerOf(w)); }

	// A complete drag along an arbitrary path: press at points.front(), hover through every
	// remaining point in order, release at points.back(). All coordinates scene-space.
	//
	// This is the general form `drag()` is built on: the caller owns the path's shape (a straight
	// line, an arc, anything), and EventDriver owns the button/hover/delta protocol. Before this,
	// any test wanting a curved gesture (a knob's rotary drag, sweeping around a circular widget)
	// had to hand-roll the press/hover/release loop itself, including the `hover(next,
	// next.minus(pos))` explicit-delta convention below — easy to get wrong, since omitting the
	// delta measures mouseDelta from the wrong origin.
	void dragPath(const std::vector<rack::math::Vec>& points,
	              int btn = GLFW_MOUSE_BUTTON_LEFT, int mods = 0) {
		REQUIRE(points.size() >= 2);
		// button() sets lastMousePos itself, so the first hover()'s implicit delta is measured
		// from points.front() rather than from wherever a previous event left the mouse.
		button(points.front(), btn, GLFW_PRESS, mods);

		rack::math::Vec pos = points.front();
		for (size_t i = 1; i < points.size(); i++) {
			rack::math::Vec next = points[i];
			hover(next, next.minus(pos));
			pos = next;
		}

		button(points.back(), btn, GLFW_RELEASE, mods);
	}

	// A complete drag: press at `from`, move through the given number of intermediate steps, and
	// release at `to`. All coordinates scene-space.
	//
	// Stepping matters — a widget accumulating mouseDelta behaves differently under one big jump
	// than under the many small ones a real mouse produces, and that difference is a real bug
	// class (a drag handler that clamps per-move rather than in total). Default 1 step keeps the
	// simple case simple. A thin wrapper over dragPath() with a linearly-sampled straight-line
	// path — for anything curved, build a point list and call dragPath() directly.
	void drag(rack::math::Vec from, rack::math::Vec to, int steps = 1,
	          int btn = GLFW_MOUSE_BUTTON_LEFT, int mods = 0) {
		REQUIRE(steps >= 1);
		std::vector<rack::math::Vec> points;
		points.reserve(steps + 1);
		points.push_back(from);
		rack::math::Vec delta = to.minus(from).div(float(steps));
		for (int i = 0; i < steps; i++) {
			// The last step lands exactly on `to`, so accumulated float division cannot leave
			// the drag short of where the test said it ended.
			points.push_back((i == steps - 1) ? to : from.plus(delta.mult(float(i + 1))));
		}
		dragPath(points, btn, mods);
	}

	// A drag starting at a widget's centre and moving by a delta — the form most drag tests
	// want ("grab this knob and pull it up 50px").
	void dragBy(rack::widget::Widget* w, rack::math::Vec delta, int steps = 1,
	            int btn = GLFW_MOUSE_BUTTON_LEFT, int mods = 0) {
		rack::math::Vec from = centerOf(w);
		drag(from, from.plus(delta), steps, btn, mods);
	}

	// ---- Keyboard ----------------------------------------------------------------------------

	// A key event, dispatched through EventState::handleKey — which needs no re-implementation:
	// it touches no Window, and glfwGetKeyName()/glfwGetKeyScancode() are safe on an
	// uninitialised GLFW (they return NULL/0).
	//
	// Rack routes this to the *selected* widget first (SelectKey) and only then, if unconsumed,
	// to whatever is under `pos` (HoverKey). So a test that wants HoverKey must either leave
	// nothing selected or ensure the selection does not consume.
	bool key(int key, int action = GLFW_PRESS, int mods = 0) {
		return keyAt(lastMousePos, key, action, mods);
	}

	bool keyAt(rack::math::Vec pos, int key, int action = GLFW_PRESS, int mods = 0) {
		return APP->event->handleKey(pos, key, glfwGetKeyScancode(key), action, mods);
	}

	// Press and release, the pair a "keystroke" usually means. Returns whether the press was
	// consumed — the release is bookkeeping (it clears heldKeys) and rarely what a test asserts.
	bool keyPress(int k, int mods = 0) {
		bool consumed = key(k, GLFW_PRESS, mods);
		key(k, GLFW_RELEASE, mods);
		return consumed;
	}

	// Text input (a codepoint, not a key) — what a TextField actually receives when typing.
	bool text(rack::math::Vec pos, uint32_t codepoint) {
		return APP->event->handleText(pos, codepoint);
	}
	bool text(uint32_t codepoint) { return text(lastMousePos, codepoint); }

	// Types a whole string as a sequence of HoverText events.
	void type(const std::string& s) {
		for (char ch : s) text(uint32_t((unsigned char) ch));
	}

	// ---- Scroll and drop -----------------------------------------------------------------------

	bool scroll(rack::math::Vec pos, rack::math::Vec scrollDelta) {
		return APP->event->handleScroll(pos, scrollDelta);
	}
	bool scroll(rack::widget::Widget* w, rack::math::Vec scrollDelta) {
		return scroll(centerOf(w), scrollDelta);
	}

	bool drop(rack::math::Vec pos, const std::vector<std::string>& paths) {
		return APP->event->handleDrop(pos, paths);
	}

	// ---- Selection ---------------------------------------------------------------------------

	// Selects a widget directly, without a click. For setting up a test whose subject is what
	// happens *after* selection (a SelectKey handler, say) rather than the selecting itself.
	void select(rack::widget::Widget* w) { APP->event->setSelectedWidget(w); }
	void deselect() { APP->event->setSelectedWidget(nullptr); }

	// Clears every EventState reference and this driver's own memory of what happened.
	//
	// Worth calling between phases of a long test: EventState is shared process-wide, so a
	// widget left hovered or selected by one part of a test changes how the next part dispatches
	// (a selected widget sees SelectKey before anything sees HoverKey).
	void reset() {
		rack::widget::EventState* ev = APP->event;
		ev->setDragHoveredWidget(nullptr);
		ev->setDraggedWidget(nullptr, 0);
		ev->setHoveredWidget(nullptr);
		ev->setSelectedWidget(nullptr);
		ev->heldKeys.clear();
		ev->lastClickTime = -INFINITY;
		ev->lastClickedWidget = nullptr;
		lastTarget = nullptr;
		lastMousePos = rack::math::Vec();
		// The rack's tracked position is process-wide state like EventState's, so a drag left
		// mid-flight by one phase would otherwise offset the next phase's first drag by the
		// distance between them. Cleared after setDraggedWidget(nullptr) above, so the sync
		// takes the Hover path rather than dispatching a DragHover with no drag in progress.
		syncRackMousePos(rack::math::Vec());
	}
};

} // namespace Test
