// SpliceKit.viz.test.hpp — cable-container visibility in viz mode (review #21).
//
// Each SpliceKit hides the shared cable container while its "Visualize" overlay
// is active. Because several instances can be in viz mode at once, one instance
// leaving viz mode (or being deleted) must NOT re-show cables while another
// still wants them hidden. These tests lock in that multi-owner behaviour via
// the VisibilityTracker shared registry.
//
// Ownership model used here:
//   - Widgets are created with their module attached but are NOT added to the
//     rack, so they are orphaned and can be `delete`d directly.
//   - The modules ARE registered with the engine (addModule_NoLock) so that
//     ~ModuleWidget::setModule(NULL) — which runs when the widget is deleted —
//     can remove them without tripping Engine::removeModule's assert.
//   - Deleting a widget with its module intact is deliberate: ~SpliceKitWidget
//     then runs the destructor's release/unregister path (Test::destroyWidget
//     would null the module first and skip it). The widget's base class frees
//     the module, so Test::destroyModule must NOT be called for it afterward.
//
// These tests drive the process-wide VisibilityTracker singleton (through
// SpliceKitWidget::setVizMode). Each test restores the container and releases
// every owner it creates, so the shared state is clean when the test finishes.

#include "SpliceKit.test.hpp"


TEST_CASE("setVizMode - cables stay hidden while another instance is in viz mode", "[SpliceKit][viz]") {
	SpliceKitModule* mA = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* mB = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitWidget* mwA = Test::createWidget<SpliceKitWidget>(mA);
	SpliceKitWidget* mwB = Test::createWidget<SpliceKitWidget>(mB);
	Widget* cableContainer = APP->scene->rack->getCableContainer();
	APP->engine->addModule_NoLock(mA);
	APP->engine->addModule_NoLock(mB);

	// Start from a known-visible state.
	cableContainer->show();
	REQUIRE(cableContainer->visible == true);

	// First instance entering viz mode hides the cables.
	mwA->setVizMode(true);
	REQUIRE(cableContainer->visible == false);

	// Second instance entering viz mode keeps them hidden.
	mwB->setVizMode(true);
	REQUIRE(cableContainer->visible == false);

	// A leaves viz mode — B still holds the cables hidden (the #21 regression).
	mwA->setVizMode(false);
	REQUIRE(cableContainer->visible == false);

	// Last instance leaves — cables restored.
	mwB->setVizMode(false);
	REQUIRE(cableContainer->visible == true);
	REQUIRE(VisibilityTracker::ownerCount(cableContainer) == 0);

	// Widget destructors run the release path (a no-op here) and free mA/mB.
	// These widgets are orphaned, so clear EventState references before delete.
	APP->event->finalizeWidget(mwA);
	delete mwA;
	APP->event->finalizeWidget(mwB);
	delete mwB;
}

TEST_CASE("setVizMode - deleting an instance releases only its own hide request", "[SpliceKit][viz]") {
	SpliceKitModule* mA = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* mB = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitWidget* mwA = Test::createWidget<SpliceKitWidget>(mA);
	SpliceKitWidget* mwB = Test::createWidget<SpliceKitWidget>(mB);
	Widget* cableContainer = APP->scene->rack->getCableContainer();
	APP->engine->addModule_NoLock(mA);
	APP->engine->addModule_NoLock(mB);

	cableContainer->show();
	mwA->setVizMode(true);
	mwB->setVizMode(true);
	REQUIRE(cableContainer->visible == false);

	// Deleting A (still in viz mode) must NOT re-show cables while B is active.
	// ~SpliceKitWidget runs with module intact (releasing the hide request), then
	// ~ModuleWidget removes and deletes mA from the engine. The widget is orphaned,
	// so clear EventState references first.
	APP->event->finalizeWidget(mwA);
	delete mwA;
	REQUIRE(cableContainer->visible == false);

	// Deleting B releases the last request — cables restored. mB is auto-deleted.
	APP->event->finalizeWidget(mwB);
	delete mwB;
	REQUIRE(cableContainer->visible == true);
	REQUIRE(VisibilityTracker::ownerCount(cableContainer) == 0);
}
