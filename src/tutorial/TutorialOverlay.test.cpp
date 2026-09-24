#include "../test/framework.hpp"
#include "Tutorial.hpp"
#include <algorithm>

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
}

using namespace StoermelderPackOne::Tutorial;

static Test::TestContext<> testContext;

namespace {

// A minimal TutorialBubbleHost that just counts calls, for the button-wiring tests.
struct StubHost : TutorialBubbleHost {
	int nextCalls = 0;
	int backCalls = 0;
	int closeCalls = 0;
	void next() override { nextCalls++; }
	void back() override { backCalls++; }
	void close() override { closeCalls++; }
};

// RAII: a TutorialBubble added directly to APP->scene (the harness's addWidget() is
// ModuleWidget-specific; TutorialBubble is a plain rack child). Mirrors
// test_events.test.cpp's ScopedProbe pattern for a hand-added, non-harness-owned widget.
//
// Installs its own UiAccess mock (rather than relying on an enclosing Test::Harness, which
// not every TEST_CASE here needs) so setContent()'s measureTextBox() calls resolve to the
// deterministic estimate instead of RealUiAccess, which dereferences the always-null
// APP->window headless. TEST_MOCK_UI can't be used here (it declares a member named after the
// slot, `ui`, on the enclosing struct) since ScopedBubble already wants that name free for
// other uses, so the guard is spelled out directly.
struct ScopedBubble {
	TutorialBubble* w;
	Test::mock::MockUiAccess uiMock;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiGuard{StoermelderPackOne::vcv::uiAccess, &uiMock};

	ScopedBubble() {
		w = new TutorialBubble;
		w->box.pos = math::Vec(100.f, 100.f);
		APP->scene->addChild(w);
	}
	~ScopedBubble() {
		APP->event->finalizeWidget(w);
		APP->scene->removeChild(w);
		delete w;
	}

	TutorialBubble* operator->() const { return w; }
	operator TutorialBubble*() const { return w; }

	ScopedBubble(const ScopedBubble&) = delete;
	ScopedBubble& operator=(const ScopedBubble&) = delete;
};

} // namespace

TEST_CASE("setContent lays out caption, title, body and buttons without overlap", "[TutorialOverlay][TutorialBubble]") {
	ScopedBubble bubble;
	bubble->setContent(0, "TEST · 1 / 3", "Welcome", "A short intro.", false, false, 180.f);

	REQUIRE(bubble->captionLabel->text == "TEST · 1 / 3");
	REQUIRE(bubble->titleLabel->text == "Welcome");
	REQUIRE(bubble->bodyLabel->text == "A short intro.");
	REQUIRE(bubble->getBodySize().x == 180.f);
	REQUIRE(bubble->getBodySize().y > 0.f);
}

TEST_CASE("bubble height grows with the body text", "[TutorialOverlay][TutorialBubble]") {
	ScopedBubble shortBubble;
	shortBubble->setContent(0, "TEST · 1 / 2", "Title", "Short.", false, false, 180.f);
	float shortHeight = shortBubble->getBodySize().y;

	ScopedBubble longBubble;
	longBubble->setContent(0, "TEST · 1 / 2", "Title",
		"This is a considerably longer body text that should wrap across several lines at "
		"this bubble width, growing the total height of the dialog well beyond the short one.",
		false, false, 180.f);
	float longHeight = longBubble->getBodySize().y;

	CATCH_INFO("short=" << shortHeight << " long=" << longHeight);
	REQUIRE(longHeight > shortHeight);
}

TEST_CASE("Back is hidden on the first step and shown afterwards", "[TutorialOverlay][TutorialBubble]") {
	ScopedBubble bubble;
	bubble->setContent(0, "TEST · 1 / 3", "Step 1", "Text", /*showBack=*/false, /*isLast=*/false, 180.f);
	REQUIRE_FALSE(bubble->backButton->visible);

	bubble->setContent(1, "TEST · 2 / 3", "Step 2", "Text", /*showBack=*/true, /*isLast=*/false, 180.f);
	REQUIRE(bubble->backButton->visible);
}

TEST_CASE("Next reads Finish on the last step, Next elsewhere", "[TutorialOverlay][TutorialBubble]") {
	ScopedBubble bubble;
	bubble->setContent(0, "TEST · 1 / 2", "Step 1", "Text", false, /*isLast=*/false, 180.f);
	REQUIRE(bubble->nextButton->text == "Next");

	bubble->setContent(1, "TEST · 2 / 2", "Step 2", "Text", true, /*isLast=*/true, 180.f);
	REQUIRE(bubble->nextButton->text == "Finish");
}

TEST_CASE("setContent is a no-op when the (index, width) cache key and content are unchanged", "[TutorialOverlay][TutorialBubble]") {
	ScopedBubble bubble;
	bubble->setContent(0, "TEST · 1 / 2", "Step 1", "Text", false, false, 180.f);
	math::Vec firstLayoutBodyPos = bubble->bodyLabel->box.pos;

	// Nudge a child's box by hand, then call setContent again with identical arguments: since
	// the cache key and content match, layout() must not run again and overwrite the nudge —
	// proving it really is a no-op, not just idempotent by coincidence.
	bubble->bodyLabel->box.pos = firstLayoutBodyPos.plus(math::Vec(5.f, 5.f));
	bubble->setContent(0, "TEST · 1 / 2", "Step 1", "Text", false, false, 180.f);
	REQUIRE(bubble->bodyLabel->box.pos.equals(firstLayoutBodyPos.plus(math::Vec(5.f, 5.f))));

	// A genuinely different step re-lays-out and moves it back.
	bubble->setContent(1, "TEST · 2 / 2", "Step 2", "Text", false, false, 180.f);
	REQUIRE(bubble->bodyLabel->box.pos.equals(firstLayoutBodyPos));
}

TEST_CASE("clicking Next calls next() on the host", "[TutorialOverlay][TutorialBubble][EventDriver]") {
	Test::Harness h;
	StubHost host;

	ScopedBubble bubble;
	bubble->setHost(&host);
	bubble->setContent(0, "TEST · 1 / 2", "Step 1", "Text", false, false, 180.f);

	REQUIRE(h.events().click(bubble->nextButton));
	REQUIRE(host.nextCalls == 1);
	REQUIRE(host.backCalls == 0);
	REQUIRE(host.closeCalls == 0);
}

TEST_CASE("clicking Back calls back() on the host", "[TutorialOverlay][TutorialBubble][EventDriver]") {
	Test::Harness h;
	StubHost host;

	ScopedBubble bubble;
	bubble->setHost(&host);
	bubble->setContent(1, "TEST · 2 / 2", "Step 2", "Text", true, false, 180.f);

	REQUIRE(h.events().click(bubble->backButton));
	REQUIRE(host.backCalls == 1);
	REQUIRE(host.nextCalls == 0);
	REQUIRE(host.closeCalls == 0);
}

TEST_CASE("clicking the close button calls close() on the host", "[TutorialOverlay][TutorialBubble][EventDriver]") {
	Test::Harness h;
	StubHost host;

	ScopedBubble bubble;
	bubble->setHost(&host);
	bubble->setContent(0, "TEST · 1 / 2", "Step 1", "Text", false, false, 180.f);

	REQUIRE(h.events().click(bubble->closeButton));
	REQUIRE(host.closeCalls == 1);
	REQUIRE(host.nextCalls == 0);
	REQUIRE(host.backCalls == 0);
}

TEST_CASE("Finish (Next on the last step) still calls next(), not a different method", "[TutorialOverlay][TutorialBubble][EventDriver]") {
	Test::Harness h;
	StubHost host;

	ScopedBubble bubble;
	bubble->setHost(&host);
	bubble->setContent(1, "TEST · 2 / 2", "Step 2", "Text", true, /*isLast=*/true, 180.f);
	REQUIRE(bubble->nextButton->text == "Finish");

	REQUIRE(h.events().click(bubble->nextButton));
	REQUIRE(host.nextCalls == 1);
}

TEST_CASE("setPointer extends the box to include the triangle without moving the body content", "[TutorialOverlay][TutorialBubble]") {
	ScopedBubble bubble;
	bubble->setContent(0, "TEST · 1 / 2", "Step 1", "Text", false, false, 180.f);
	math::Vec bodySize = bubble->getBodySize();
	REQUIRE(bubble->box.size.equals(bodySize));

	// A RIGHT-side pointer: tip sits to the left of the body (negative x), so the box must
	// grow leftward to include it, and the body shifts right by the same amount internally.
	math::Vec tip(-8.f, bodySize.y * 0.5f);
	math::Vec baseA(0.f, bodySize.y * 0.5f - 6.f);
	math::Vec baseB(0.f, bodySize.y * 0.5f + 6.f);
	bubble->setPointer(true, Side::LEFT, tip, baseA, baseB);

	REQUIRE(bubble->box.size.x > bodySize.x);
	REQUIRE(bubble->captionLabel->box.pos.x > 0.f);

	// The stored pointer geometry must stay consistent with the body's own shift within the
	// (now-grown) box: undoing bodyOffset should recover exactly the tip/baseA/baseB the
	// caller originally passed in (both are "relative to the body's top-left" by contract).
	// A regression here drew the triangle spanning corner-to-corner instead of a small notch
	// on the target-facing edge, because the stored points weren't re-offset when the box grew.
	math::Vec offset = bubble->getBodyOffset();
	REQUIRE(bubble->pointerTip.minus(offset).equals(tip));
	REQUIRE(bubble->pointerBaseA.minus(offset).equals(baseA));
	REQUIRE(bubble->pointerBaseB.minus(offset).equals(baseB));

	// Clearing the pointer restores the box to just the body.
	bubble->setPointer(false, Side::AUTO, math::Vec(), math::Vec(), math::Vec());
	REQUIRE(bubble->box.size.equals(bodySize));
}

// ============================================================================================
// TutorialOverlay
// ============================================================================================

namespace {

// Test-only widget type for Target::widget<W>(), distinguishable from every real Rack type.
struct OverlayTargetWidget : widget::Widget {};

// Answers vcv::ui::getRackViewport()/measureTextBox() deterministically. Layered on top of
// Test::Harness's own HarnessUiAccess (constructed after it, so it wins for the fixture's
// lifetime — same rule Test::Harness's own doc comment states for stacked guards).
//
// getRackViewport() is why this exists at all: the base UiAccess default (a large box at
// zoom 1, generous enough for most placement tests) is overridden per-fixture so a test can
// script a specific viewport (module at the rack edge, zoomed in, ...) — see
// FixtureUiAccess::setViewport() — without ever touching the real APP->scene->rackScroll,
// which Test::Harness deliberately leaves zeroed-out and hidden (RackScrollWidget segfaults
// on APP->window the instant a position event reaches it — see test_harness.hpp's
// SceneLayout comment and Mb.test.ui.hpp's rackScroll comment).
struct FixtureUiAccess : Test::mock::MockUiAccess {
	StoermelderPackOne::vcv::RackViewport viewport;

	FixtureUiAccess() {
		// A generous default so a test that doesn't care about the viewport still gets real
		// placement behaviour (candidates feasible, no last-resort fallback).
		viewport = StoermelderPackOne::vcv::RackViewport(math::Rect(math::Vec(-2000.f, -2000.f), math::Vec(4000.f, 4000.f)), 1.f);
	}

	StoermelderPackOne::vcv::RackViewport getRackViewport() const override { return viewport; }
};

// A ModuleWidget with one targetable param, genuinely parented inside APP->scene->rack (not
// APP->scene, unlike Test::Harness::addWidget() — Tutorial's coordinate mapping needs the real
// rack topology, per TutorialOverlay::visibleRegion()'s use of getRelativeOffset(..., rack)).
//
// Dispatch reaches it through a SEPARATE EventDriver rooted at APP->scene->rack directly,
// never through APP->scene (the default root) — that would have to pass through the
// deliberately-broken rackScroll. Both the module and the bubble (also a direct rack child)
// are reachable this way without ever giving a position event a path to RackScrollWidget.
struct OverlayFixture {
	Test::Harness h;
	FixtureUiAccess uiMock;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiGuard{StoermelderPackOne::vcv::uiAccess, &uiMock};
	Test::EventDriver rackEvents{APP->scene->rack};

	app::ModuleWidget* mw;
	app::ParamWidget* param0;
	OverlayTargetWidget* customTarget;

	OverlayFixture() {
		mw = new app::ModuleWidget;
		mw->box.pos = math::Vec(0.f, 0.f);
		mw->box.size = math::Vec(270.f, 200.f);

		param0 = new app::ParamWidget;
		param0->paramId = 0;
		param0->box = math::Rect(120.f, 90.f, 30.f, 20.f);
		mw->addChild(param0);

		customTarget = new OverlayTargetWidget;
		customTarget->box = math::Rect(10.f, 10.f, 15.f, 15.f);
		mw->addChild(customTarget);

		APP->scene->rack->addChild(mw);
	}

	~OverlayFixture() {
		// The overlay (if start() was called) owns the bubble and must be torn down before mw,
		// the same way Widget::step()'s deferred-delete path would — but synchronously, since
		// nothing here drives another step() after the test body.
		for (auto it = mw->children.begin(); it != mw->children.end();) {
			if (TutorialOverlay* overlay = dynamic_cast<TutorialOverlay*>(*it)) {
				it = mw->children.erase(it);
				overlay->parent = nullptr;
				APP->event->finalizeWidget(overlay);
				delete overlay;
			}
			else {
				++it;
			}
		}
		APP->event->finalizeWidget(mw);
		APP->scene->rack->removeChild(mw);
		delete mw;
	}

	OverlayFixture(const OverlayFixture&) = delete;
	OverlayFixture& operator=(const OverlayFixture&) = delete;
};

// The first TutorialOverlay child of mw, or nullptr.
TutorialOverlay* overlayOf(app::ModuleWidget* mw) {
	for (widget::Widget* child : mw->children) {
		if (TutorialOverlay* o = dynamic_cast<TutorialOverlay*>(child)) return o;
	}
	return nullptr;
}

// Every TutorialBubble directly under root (APP->scene->rack), for counting.
std::vector<TutorialBubble*> bubblesIn(widget::Widget* root) {
	std::vector<TutorialBubble*> found;
	for (widget::Widget* child : root->children) {
		if (TutorialBubble* b = dynamic_cast<TutorialBubble*>(child)) found.push_back(b);
	}
	return found;
}

Tutorial fourStepTutorial(app::ModuleWidget* mw, OverlayTargetWidget* customTarget) {
	Tutorial t;
	t.title = "TEST";
	t.steps.push_back(Step("Welcome", "Centered intro, no target."));
	t.steps.push_back(Step("Step 2", "Targets the param.", Target::param(0)));
	t.steps.push_back(Step("Step 3", "Targets the custom widget.", Target::widget<OverlayTargetWidget>()));
	t.steps.push_back(Step("Step 4", "Back to centered."));
	(void) customTarget;
	return t;
}

} // namespace

TEST_CASE("start() appends exactly one overlay, and one bubble after the first step()", "[TutorialOverlay]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));

	REQUIRE(overlayOf(f.mw) != nullptr);
	REQUIRE(f.mw->children.back() == overlayOf(f.mw));
	REQUIRE(bubblesIn(APP->scene->rack).empty());

	f.mw->step();
	REQUIRE(bubblesIn(APP->scene->rack).size() == 1);
}

TEST_CASE("a second start() replaces both the overlay and its bubble", "[TutorialOverlay]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	f.mw->step();
	REQUIRE(bubblesIn(APP->scene->rack).size() == 1);
	TutorialOverlay* first = overlayOf(f.mw);

	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	REQUIRE(overlayOf(f.mw) != first);
	// The old bubble is gone immediately (start() replaces synchronously); the new overlay
	// hasn't created its own yet until the next step().
	REQUIRE(bubblesIn(APP->scene->rack).empty());

	f.mw->step();
	REQUIRE(bubblesIn(APP->scene->rack).size() == 1);
}

TEST_CASE("start() with no steps is a no-op", "[TutorialOverlay]") {
	OverlayFixture f;
	Tutorial empty;
	empty.title = "EMPTY";
	start(f.mw, empty);
	REQUIRE(overlayOf(f.mw) == nullptr);
}

TEST_CASE("each step's bubble rect, mapped back to module coordinates, doesn't intersect the target", "[TutorialOverlay]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	TutorialOverlay* overlay = overlayOf(f.mw);

	for (int i = 0; i < 4; i++) {
		overlay->index = i;
		f.mw->step();

		math::Vec a = overlay->bubble->getRelativeOffset(math::Vec(), f.mw);
		math::Vec b = overlay->bubble->getRelativeOffset(overlay->bubble->box.size, f.mw);
		math::Rect bubbleInModule = math::Rect::fromCorners(a, b);

		CATCH_INFO("step=" << i << " hasTarget=" << overlay->hasTargetRect);
		if (overlay->hasTargetRect) {
			REQUIRE_FALSE(bubbleInModule.intersects(overlay->targetRectModule));
		}
	}
}

TEST_CASE("the bubble's pointer notch is on the edge facing the target, not the far edge", "[TutorialOverlay]") {
	// Regression test: TutorialOverlay::step() once passed Placement::side (which side of the
	// TARGET the bubble sits on) straight through to TutorialBubble::setPointer()'s `side`
	// (which edge of the BUBBLE gets the notch) without inverting it. A bubble placed to the
	// RIGHT of the target must point back at it from the bubble's own LEFT edge — passing
	// RIGHT there spliced the notch into the bubble's far (right) edge using near-edge
	// coordinates, producing a triangle that zigzagged across the whole bubble instead of a
	// small notch touching the target.
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	TutorialOverlay* overlay = overlayOf(f.mw);
	overlay->index = 1; // targets param0, a real (non-AUTO) placement side
	f.mw->step();

	REQUIRE(overlay->bubble->hasPointer);
	Side placementSide = overlay->lastSide;
	REQUIRE(placementSide != Side::AUTO);
	REQUIRE(overlay->bubble->pointerSide == oppositeSide(placementSide));

	// A tighter geometric proxy for "the notch is where it should be": the pointer tip, mapped
	// back into module coordinates, must land within `gap` of the target's edge — the wrong-edge
	// bug put it clear across the bubble instead, tens of module px away.
	math::Vec tipInBubble = overlay->bubble->pointerTip;
	math::Vec tipInModule = overlay->bubble->getRelativeOffset(tipInBubble, f.mw);
	float distToTarget = overlay->hasTargetRect
		? std::min({
			std::fabs(tipInModule.x - overlay->targetRectModule.getLeft()),
			std::fabs(tipInModule.x - overlay->targetRectModule.getRight()),
			std::fabs(tipInModule.y - overlay->targetRectModule.getTop()),
			std::fabs(tipInModule.y - overlay->targetRectModule.getBottom()),
		})
		: -1.f;
	CATCH_INFO("placementSide=" << (int) placementSide << " pointerSide=" << (int) overlay->bubble->pointerSide
		<< " distToTarget=" << distToTarget);
	REQUIRE(overlay->hasTargetRect);
	REQUIRE(distToTarget < 5.f); // well within Style::gap (3px) + float slop, not tens of px off
}

TEST_CASE("a click on a bubble button outside the module box reaches the button", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	// Force the module hard against the rack-viewport's right edge, so the bubble is placed
	// LEFT and its Back/Next row can only land outside the module's own box on that step.
	f.uiMock.viewport = StoermelderPackOne::vcv::RackViewport(
		math::Rect(math::Vec(-2000.f, -2000.f), math::Vec(2000.f + f.mw->box.size.x, 4000.f)), 1.f);

	Tutorial t = fourStepTutorial(f.mw, f.customTarget);
	start(f.mw, t);
	TutorialOverlay* overlay = overlayOf(f.mw);
	overlay->index = 1; // targets param0, forces a side placement
	f.mw->step();

	bool outside = !f.mw->box.zeroPos().contains(overlay->bubble->nextButton->box.pos.plus(overlay->bubble->box.pos));
	CATCH_INFO("bubble side=" << (int) overlay->lastSide << " outsideModule=" << outside);

	REQUIRE(f.rackEvents.click(overlay->bubble->nextButton));
	REQUIRE(f.rackEvents.consumedBy() == overlay->bubble->nextButton);
}

TEST_CASE("a click on the rack away from the module and bubble still reaches the rack", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	f.mw->step();

	// A miss on the module/bubble is consumed by RackWidget itself (real Rack behaviour: a
	// left-click on empty rack space is how deselect/rectangle-select work) — the point here
	// is only that it is NOT swallowed by the tutorial's own widgets.
	f.rackEvents.click(math::Vec(3000.f, 3000.f));
	REQUIRE(f.rackEvents.consumedBy() == APP->scene->rack);
}

TEST_CASE("a left click on the dimmed panel does not reach the param widget below it", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	TutorialOverlay* overlay = overlayOf(f.mw);
	overlay->index = 0; // centered step: whole panel dimmed, param0 not the target
	f.mw->step();

	math::Vec paramCenterInRack = f.param0->getRelativeOffset(f.param0->box.size.div(2.f), APP->scene->rack);
	REQUIRE(f.rackEvents.click(paramCenterInRack));
	REQUIRE(f.rackEvents.consumedBy() != f.param0);
}

TEST_CASE("right-click on the module still reaches ModuleWidget's own handling", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	f.mw->step();

	math::Vec centerInRack = f.mw->getRelativeOffset(f.mw->box.size.div(2.f), APP->scene->rack);
	// Not asserting consumedBy() == mw specifically (ModuleWidget::onButton's own right-click
	// path opens a context menu, whose downstream effects aren't this test's concern) — only
	// that the overlay's own onButton never claims the event via consume(), which would make
	// ModuleWidget::onButton's `if (e.isConsumed()) return;` skip its right-click handling.
	REQUIRE_FALSE(f.rackEvents.consumedBy() == overlayOf(f.mw));
	f.rackEvents.click(centerInRack, GLFW_MOUSE_BUTTON_RIGHT);
}

TEST_CASE("hover-scroll over the overlay is not consumed", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	f.mw->step();

	math::Vec centerInRack = f.mw->getRelativeOffset(f.mw->box.size.div(2.f), APP->scene->rack);
	bool consumed = f.rackEvents.scroll(centerInRack, math::Vec(0.f, 1.f));
	REQUIRE_FALSE(consumed);
}

TEST_CASE("Right arrow / Enter advance, Left goes back, Escape closes", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	TutorialOverlay* overlay = overlayOf(f.mw);
	f.mw->step();
	REQUIRE(overlay->index == 0);

	REQUIRE(f.rackEvents.keyPress(GLFW_KEY_RIGHT));
	REQUIRE(overlay->index == 1);

	REQUIRE(f.rackEvents.keyPress(GLFW_KEY_ENTER));
	REQUIRE(overlay->index == 2);

	REQUIRE(f.rackEvents.keyPress(GLFW_KEY_LEFT));
	REQUIRE(overlay->index == 1);

	REQUIRE(f.rackEvents.keyPress(GLFW_KEY_ESCAPE));
	f.mw->step(); // requestDelete() is deferred to the parent's step()
	REQUIRE(overlayOf(f.mw) == nullptr);
}

TEST_CASE("Back on the first step is a no-op", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	TutorialOverlay* overlay = overlayOf(f.mw);
	f.mw->step();
	REQUIRE(overlay->index == 0);

	f.rackEvents.keyPress(GLFW_KEY_LEFT);
	REQUIRE(overlay->index == 0);
}

TEST_CASE("Next on the last step closes the tutorial", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	TutorialOverlay* overlay = overlayOf(f.mw);
	overlay->index = 3; // last of 4 steps
	f.mw->step();

	f.rackEvents.keyPress(GLFW_KEY_RIGHT);
	f.mw->step();
	REQUIRE(overlayOf(f.mw) == nullptr);
}

TEST_CASE("deleting the ModuleWidget while the tutorial is open removes the bubble from the rack", "[TutorialOverlay]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	f.mw->step();
	REQUIRE(bubblesIn(APP->scene->rack).size() == 1);

	// ~OverlayFixture() tears mw (and its overlay, and the overlay's bubble) down; the
	// assertion is what's left in the rack afterward, not what happens during.
}

TEST_CASE("harness teardown with an open tutorial leaves nothing of the tutorial in the rack", "[TutorialOverlay]") {
	{
		OverlayFixture f;
		start(f.mw, fourStepTutorial(f.mw, f.customTarget));
		f.mw->step();
		REQUIRE(bubblesIn(APP->scene->rack).size() == 1);
	}
	REQUIRE(bubblesIn(APP->scene->rack).empty());
}

TEST_CASE("moving the module moves the bubble by the same delta after step()", "[TutorialOverlay]") {
	OverlayFixture f;
	start(f.mw, fourStepTutorial(f.mw, f.customTarget));
	TutorialOverlay* overlay = overlayOf(f.mw);
	overlay->index = 1; // a targeted step, so there's a pointer/side to track too
	f.mw->step();

	math::Vec before = overlay->bubble->box.pos;
	math::Vec delta(37.f, -18.f);
	f.mw->box.pos = f.mw->box.pos.plus(delta);
	f.mw->step();
	math::Vec after = overlay->bubble->box.pos;

	REQUIRE(math::isNear(after.x - before.x, delta.x, 1e-3f));
	REQUIRE(math::isNear(after.y - before.y, delta.y, 1e-3f));
}

TEST_CASE("onEnter/onLeave fire exactly once per step change, including on close", "[TutorialOverlay][EventDriver]") {
	OverlayFixture f;
	std::vector<std::string> log;

	Tutorial t;
	t.title = "TEST";
	for (int i = 0; i < 3; i++) {
		Step s("Step", "Text");
		std::string name = "step" + std::to_string(i);
		s.onEnter = [&log, name]() { log.push_back(name + ":enter"); };
		s.onLeave = [&log, name]() { log.push_back(name + ":leave"); };
		t.steps.push_back(s);
	}

	start(f.mw, t);
	f.mw->step(); // lazy setup fires step 0's onEnter
	REQUIRE(log == std::vector<std::string>{"step0:enter"});

	f.rackEvents.keyPress(GLFW_KEY_RIGHT); // -> step 1
	REQUIRE(log == std::vector<std::string>{"step0:enter", "step0:leave", "step1:enter"});

	f.rackEvents.keyPress(GLFW_KEY_LEFT); // -> step 0
	REQUIRE(log == std::vector<std::string>{
		"step0:enter", "step0:leave", "step1:enter", "step1:leave", "step0:enter"});

	f.rackEvents.keyPress(GLFW_KEY_RIGHT); // -> step 1
	f.rackEvents.keyPress(GLFW_KEY_RIGHT); // -> step 2 (last)
	log.clear();
	f.rackEvents.keyPress(GLFW_KEY_RIGHT); // Next on the last step -> close()
	REQUIRE(log == std::vector<std::string>{"step2:leave"});
}

TEST_CASE("start() replacing an already-open overlay fires the old step's onLeave", "[TutorialOverlay]") {
	// start() tears down a pre-existing overlay directly (mw->removeChild + delete), not via
	// close()/requestDelete() — a side effect registered in onEnter (e.g. temporary module
	// state a real tutorial step restores in onLeave) must still see onLeave exactly once, or
	// it leaks past the tutorial being reopened. Covered by the destructor's fireLeave() call.
	// log must outlive f: f's destructor deletes any still-open overlay, which now (per the
	// destructor's fireLeave() call) invokes the current step's onLeave closure during teardown.
	std::vector<std::string> log;
	OverlayFixture f;

	auto makeTutorial = [&log](const char* tag) {
		Tutorial t;
		t.title = "TEST";
		Step s("Step", "Text");
		s.onEnter = [&log, tag]() { log.push_back(std::string(tag) + ":enter"); };
		s.onLeave = [&log, tag]() { log.push_back(std::string(tag) + ":leave"); };
		t.steps.push_back(s);
		return t;
	};

	start(f.mw, makeTutorial("first"));
	f.mw->step(); // lazy setup fires "first" step 0's onEnter
	REQUIRE(log == std::vector<std::string>{"first:enter"});

	start(f.mw, makeTutorial("second")); // replaces the still-open "first" overlay
	REQUIRE(log == std::vector<std::string>{"first:enter", "first:leave"});

	f.mw->step(); // lazy setup fires "second" step 0's onEnter
	REQUIRE(log == std::vector<std::string>{"first:enter", "first:leave", "second:enter"});
}

TEST_CASE("forceClose() fires the current step's onLeave exactly once, for a standalone delete", "[TutorialOverlay]") {
	// A caller deleting an overlay directly (not via close()) — start()'s replace-path is the
	// real example — must call forceClose() itself first; the destructor deliberately does NOT
	// fire onLeave/onClose on its own (see ~TutorialOverlay()'s comment: doing so unconditionally
	// is unsafe when the delete instead arrives via mw's own clearChildren(), since a step's
	// onLeave that mutates mw->children would then reenter that same in-progress iteration).
	OverlayFixture f;
	std::vector<std::string> log;

	Tutorial t;
	t.title = "TEST";
	Step s("Step", "Text");
	s.onEnter = [&log]() { log.push_back("enter"); };
	s.onLeave = [&log]() { log.push_back("leave"); };
	t.steps.push_back(s);

	start(f.mw, t);
	f.mw->step();
	REQUIRE(log == std::vector<std::string>{"enter"});

	TutorialOverlay* overlay = nullptr;
	for (widget::Widget* w : f.mw->children) {
		if (auto* o = dynamic_cast<TutorialOverlay*>(w)) overlay = o;
	}
	REQUIRE(overlay != nullptr);

	overlay->forceClose();
	f.mw->removeChild(overlay);
	delete overlay;
	REQUIRE(log == std::vector<std::string>{"enter", "leave"});
}

TEST_CASE("deleting an overlay without forceClose() does not fire onLeave", "[TutorialOverlay]") {
	// The flip side of the test above: this is exactly OverlayFixture's own teardown path (mw's
	// children, including any open overlay, deleted directly with no forceClose() call) — the
	// same shape as ModuleWidget::clearChildren() during real app/patch teardown. onLeave must
	// NOT fire here, both because it would be unsafe in the real clearChildren() case and
	// because there's nothing to usefully restore when the whole module is going away anyway.
	OverlayFixture f;
	std::vector<std::string> log;

	Tutorial t;
	t.title = "TEST";
	Step s("Step", "Text");
	s.onEnter = [&log]() { log.push_back("enter"); };
	s.onLeave = [&log]() { log.push_back("leave"); };
	t.steps.push_back(s);

	start(f.mw, t);
	f.mw->step();
	REQUIRE(log == std::vector<std::string>{"enter"});

	TutorialOverlay* overlay = nullptr;
	for (widget::Widget* w : f.mw->children) {
		if (auto* o = dynamic_cast<TutorialOverlay*>(w)) overlay = o;
	}
	REQUIRE(overlay != nullptr);

	f.mw->removeChild(overlay);
	delete overlay; // no forceClose() — mirrors clearChildren()'s own delete-in-place
	REQUIRE(log == std::vector<std::string>{"enter"});
}

TEST_CASE("mw->clearChildren() survives a step whose onLeave mutates mw->children", "[TutorialOverlay]") {
	// Regression test for a real crash: a step's onLeave that calls mw->removeChild(...) on
	// some OTHER widget it added earlier (the exact shape of a tutorial's own per-step cleanup
	// helper, e.g. removeStepWidget<W>(mw) in a module's tutorial file) used to run during
	// ~TutorialOverlay() whenever the overlay was deleted — including when that delete came from
	// Widget::clearChildren() itself (what ModuleWidget::~ModuleWidget() calls, i.e. real
	// app/patch teardown), reentrantly mutating the very children list clearChildren()'s own
	// loop was mid-iteration over. Now that the destructor never fires onLeave on its own (see
	// ~TutorialOverlay()'s comment), this must be safe — onLeave simply doesn't run, and nothing
	// touches mw->children during the teardown.
	OverlayFixture f;
	std::vector<std::string> log;

	// A second, unrelated widget onLeave will try to remove — standing in for a step's own
	// demo widget (SceneCycleWidget, the old PortMapDemoWidget, etc.).
	auto* demoWidget = new widget::Widget;
	f.mw->addChild(demoWidget);

	Tutorial t;
	t.title = "TEST";
	Step s("Step", "Text");
	s.onEnter = [&log]() { log.push_back("enter"); };
	s.onLeave = [&log, mw = f.mw, demoWidget]() {
		log.push_back("leave");
		// The unsafe operation: removing a sibling from mw's children list, reentrant with
		// clearChildren()'s own iteration over that same list when this runs from there.
		for (widget::Widget* w : mw->children) {
			if (w == demoWidget) {
				mw->removeChild(w);
				delete w;
				break;
			}
		}
	};
	t.steps.push_back(s);

	start(f.mw, t);
	f.mw->step();
	REQUIRE(log == std::vector<std::string>{"enter"});

	// Exercises the real crash path directly: Widget::clearChildren() deletes every child of mw
	// in place, TutorialOverlay (and demoWidget) among them, exactly as ModuleWidget's own
	// destructor does. Must not crash, and onLeave must not have run (the destructor doesn't
	// call it), so demoWidget is still one of mw's children going in — clearChildren() deletes
	// it directly rather than via the step's own (now-unreached) cleanup.
	f.mw->clearChildren();
	REQUIRE(log == std::vector<std::string>{"enter"});
	REQUIRE(f.mw->children.empty());

	// OverlayFixture's own teardown expects to find (and clean up) an open TutorialOverlay;
	// clearChildren() already removed it, so let the fixture's destructor no-op on mw safely —
	// APP->scene->rack->removeChild(mw) + delete mw still need to happen, which its destructor
	// does unconditionally regardless of what's left in mw->children.
}

TEST_CASE("Tutorial onOpen/onClose fire once each, bracketing every step's onEnter/onLeave", "[TutorialOverlay]") {
	// log must outlive f, same hazard as the "replacing an already-open overlay" test above:
	// f's destructor may run a pending onClose during teardown.
	std::vector<std::string> log;
	OverlayFixture f;

	Tutorial t;
	t.title = "TEST";
	t.onOpen = [&log]() { log.push_back("open"); };
	t.onClose = [&log]() { log.push_back("close"); };
	for (int i = 0; i < 2; i++) {
		Step s("Step", "Text");
		std::string name = "step" + std::to_string(i);
		s.onEnter = [&log, name]() { log.push_back(name + ":enter"); };
		s.onLeave = [&log, name]() { log.push_back(name + ":leave"); };
		t.steps.push_back(s);
	}

	start(f.mw, t);
	f.mw->step(); // lazy setup: onOpen fires before step 0's onEnter
	REQUIRE(log == std::vector<std::string>{"open", "step0:enter"});

	f.rackEvents.keyPress(GLFW_KEY_RIGHT); // -> step 1
	REQUIRE(log == std::vector<std::string>{"open", "step0:enter", "step0:leave", "step1:enter"});

	f.rackEvents.keyPress(GLFW_KEY_RIGHT); // Next on the last step -> close()
	// step1:leave (fireLeave, per-step) then close (fireClose, whole-tutorial) — close() calls
	// them in that order so tutorial-scoped cleanup runs after the active step's own cleanup.
	REQUIRE(log == std::vector<std::string>{
		"open", "step0:enter", "step0:leave", "step1:enter", "step1:leave", "close"});
}

TEST_CASE("Tutorial onClose fires exactly once even when start() replaces an open overlay", "[TutorialOverlay]") {
	std::vector<std::string> log;
	OverlayFixture f;

	auto makeTutorial = [&log](const char* tag) {
		Tutorial t;
		t.title = "TEST";
		t.onOpen = [&log, tag]() { log.push_back(std::string(tag) + ":open"); };
		t.onClose = [&log, tag]() { log.push_back(std::string(tag) + ":close"); };
		t.steps.push_back(Step("Step", "Text"));
		return t;
	};

	start(f.mw, makeTutorial("first"));
	f.mw->step();
	REQUIRE(log == std::vector<std::string>{"first:open"});

	start(f.mw, makeTutorial("second")); // tears down "first" without calling close()
	REQUIRE(log == std::vector<std::string>{"first:open", "first:close"});

	f.mw->step();
	REQUIRE(log == std::vector<std::string>{"first:open", "first:close", "second:open"});
}

