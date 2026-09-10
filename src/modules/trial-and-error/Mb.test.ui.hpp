// Mb.test.ui.hpp — v2 module browser widget behavior, via Test::Harness + Test::EventDriver.
// Included by Mb.test.cpp inside namespace __ui.
//
// Regression under test: "Mb - fixed v2 scroll position when reopening the browser overlay"
// (ModuleBrowser::onShow(), Mb_v2.cpp). Reverting that fix fails the TEST_CASE below.
//
// Real user path: scroll the model list -> left-click a ModelBox (chooseModel() ->
// BrowserOverlay::hide()) -> reopen (BrowserOverlay::show(), as step() does every frame the
// overlay is visible, Mb.cpp:829-846) -> scroll offset should survive.
//
// BrowserOverlay's `mode` is a MODE* into the owning MbModule (Mb.cpp:978), so it needs a real
// MbModule/MbWidget rather than standalone construction — h.addModule/h.addWidget give that
// wiring for free.
//
// chooseModel() used to call APP->history->push() directly, segfaulting under TestContext
// (ctx->history is never set). Migrated to the vcv::history seam so MockHistoryAccess can
// stand in.

// Real layout takes two step() calls to converge: ModuleBrowser::step() reads
// headerLayout->box.getBottomLeft() before headerLayout's own step() (later in the same call)
// has sized it. Only matters when driving step() by hand instead of a real frame loop.
static void settleLayout(rack::widget::Widget* w) {
	w->step();
	w->step();
}

TEST_CASE("Reopening the v2 browser preserves the model list's scroll position", "[Mb][Widget]") {
	Test::Harness h;

	// A shrunk scene makes the one registered ModelBox taller than the viewport, so the list
	// actually overflows and the scroll below isn't a no-op clamped back to 0.
	APP->scene->box.size = math::Vec(1024, 300);

	MockHistoryAccess mockHistory;
	Test::mock::Guard<vcv::HistoryAccess> historyGuard{vcv::historyAccess, &mockHistory};

	auto* m = h.addModule<MbModule>("Mb");
	auto* mw = h.addWidget<MbWidget>(m);
	REQUIRE(mw->active);
	REQUIRE(mw->browserOverlay != nullptr);

	BrowserOverlay* overlay = mw->browserOverlay;
	auto* browser = dynamic_cast<v2::ModuleBrowser*>(overlay->mbV2);
	REQUIRE(browser != nullptr);

	// A real right-click-to-open goes through RackWidget::onButton(), unreachable here: the
	// harness keeps rackScroll (rack's ancestor) zeroed-out and hidden, since a live
	// RackScrollWidget segfaults on APP->window in onHoverScroll(). show() is the deliberate
	// substitute for that one precondition; everything from here on is event-driven.
	overlay->show();
	settleLayout(overlay);
	REQUIRE(browser->visible);

	// Found by type: Mb's own model, registered by this suite's testPluginInit().
	v2::ModelBox* box = h.events().find<v2::ModelBox>(browser);

	// Real scroll, against real overflow from the scene shrink above.
	h.events().scroll(browser->modelScroll, math::Vec(0.f, -250.f));
	// ScrollWidget::step() clamps offset to content bounds a frame later, so settle before
	// capturing the value the reopened browser is expected to match.
	settleLayout(overlay);
	math::Vec scrolledOffset = browser->modelScroll->offset;
	REQUIRE(scrolledOffset.y > 0.f);

	// Real click: ModelBox::onButton() -> chooseModel() -> ... -> browser->hide().
	REQUIRE(h.events().click(box));
	REQUIRE_FALSE(overlay->visible);
	REQUIRE(mockHistory.pushed.size() == 1);

	// Reopen: same hide()/show() cycle BrowserOverlay::step() drives on a second right-click.
	overlay->show();
	settleLayout(overlay);
	REQUIRE(browser->visible);

	// The fix: onShow() saves/restores modelScroll->offset around refresh()'s scroll-to-top.
	REQUIRE(browser->modelScroll->offset.x == Catch::Approx(scrolledOffset.x));
	REQUIRE(browser->modelScroll->offset.y == Catch::Approx(scrolledOffset.y));
}
