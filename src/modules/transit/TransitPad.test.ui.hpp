// TransitPad widget/UI interaction tests: drag-and-drop rebinding, pad-screen
// click handling, the visualize-mode keyboard shortcut, node-menu slider
// defaults, and every context menu (node, screen, module-mirror, set-button).
// Split out of a single TransitPad.test.module.hpp; see TransitPad.test.cpp
// for how this file is wired into the test binary.


// TransitPadSnapshotDragWidget::onDragDrop never
// consulted isLocked(), so a locked pad still accepted drag-and-drop
// rebinding from a TRANSIT snapshot button — contradicting the manual's
// documented "dropping is still allowed to highlight a target, but the
// binding is rejected" behaviour.
// This builds the real widget tree (TRANSIT + TransitPad as expanders) and
// dispatches DragEnter/DragDrop directly at the target node, per the
// headless traps documented in FRAMEWORK.md: VCVButton's onDragStart needs
// settings::allowCursorLock = false, and Knob::onDragMove (VCVButton's
// base) calls the un-early-out'd APP->window->getMods(), so a full
// h.events().drag() from the TransitLedButton can't be driven end-to-end.
// Dispatching DragEnter/DragDrop directly at the pad node is the handler
// under test anyway.

// Helper: find a TransitLedButton param widget on a TransitWidget by absolute slot index.
static TransitSnapshotButton* findSnapshotButton(rack::app::ModuleWidget* transitWidget, int slot) {
	for (rack::widget::Widget* w : transitWidget->getParams()) {
		auto* btn = dynamic_cast<TransitLedButton<12>*>(w);
		if (btn && btn->getSlotIndex() == slot) return btn;
	}
	return nullptr;
}

// Helper: find the TransitPad's node drag widget for a given pad point id.
static rack::widget::Widget* findPadNodeWidget(rack::app::ModuleWidget* padWidget, int id) {
	rack::widget::Widget* found = nullptr;
	Test::traversal::walk(padWidget, [&](const Test::traversal::Visit& v) {
		auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(v.widget);
		if (node && node->id == id) {
			found = v.widget;
			return false;
		}
		return true;
	});
	return found;
}

TEST_CASE("Locked pad rejects drag-and-drop rebinding", "[TransitPad][BLOCKER-2]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitWidget<12>* transitWidget = h.addWidget<TransitWidget<12>>(transit);
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	// Drag from slot 3's button, distinct from the slot-0 baseline below, so an
	// erroneously-accepted rebind is observable as a change.
	TransitSnapshotButton* srcButton = findSnapshotButton(transitWidget, 3);
	REQUIRE(srcButton != nullptr);
	rack::widget::Widget* node = findPadNodeWidget(padWidget, 0);
	REQUIRE(node != nullptr);

	// Bind pad point 0 to slot 0 while unlocked, establishing a known baseline.
	pad->bindSnapshot(0, 0);
	REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);

	pad->locked = true;

	event::DragEnter eEnter;
	eEnter.button = GLFW_MOUSE_BUTTON_LEFT;
	eEnter.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragEnter(eEnter);

	// The manual promises dropping "is still allowed to highlight a target" while
	// locked: hovering a snapshot button over the node still arms the highlight.
	auto* dragNode = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(node);
	REQUIRE(dragNode->dropArmed == true);

	event::DragDrop eDrop;
	eDrop.button = GLFW_MOUSE_BUTTON_LEFT;
	eDrop.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragDrop(eDrop);

	// ...but the binding itself is rejected.
	REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);
}

TEST_CASE("Unlocked pad accepts drag-and-drop rebinding", "[TransitPad][BLOCKER-2]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitWidget<12>* transitWidget = h.addWidget<TransitWidget<12>>(transit);
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	TransitSnapshotButton* srcButton = findSnapshotButton(transitWidget, 3);
	REQUIRE(srcButton != nullptr);
	rack::widget::Widget* node = findPadNodeWidget(padWidget, 0);
	REQUIRE(node != nullptr);

	REQUIRE(pad->isLocked() == false);

	event::DragEnter eEnter;
	eEnter.button = GLFW_MOUSE_BUTTON_LEFT;
	eEnter.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragEnter(eEnter);

	event::DragDrop eDrop;
	eDrop.button = GLFW_MOUSE_BUTTON_LEFT;
	eDrop.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragDrop(eDrop);

	REQUIRE(pad->snapshots[pad->currentSet][0].id == 3);
}

TEST_CASE("Drop onto an inactive node (id >= snapshotsUsed) is rejected", "[TransitPad]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitWidget<12>* transitWidget = h.addWidget<TransitWidget<12>>(transit);
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	// snapshotsUsed defaults to 4, so node id 5 is inactive; isNodeActive()
	// is `id < snapshotsUsed`, checked before dropArmed is ever touched.
	REQUIRE(pad->snapshotsUsed == 4);
	REQUIRE(pad->isNodeActive(5) == false);

	TransitSnapshotButton* srcButton = findSnapshotButton(transitWidget, 3);
	REQUIRE(srcButton != nullptr);
	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 5));
	REQUIRE(node != nullptr);

	REQUIRE(pad->snapshots[pad->currentSet][5].id == -1);

	event::DragDrop eDrop;
	eDrop.button = GLFW_MOUSE_BUTTON_LEFT;
	eDrop.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragDrop(eDrop);

	REQUIRE(pad->snapshots[pad->currentSet][5].id == -1);
}

TEST_CASE("A non-left-button drop does not bind", "[TransitPad]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitWidget<12>* transitWidget = h.addWidget<TransitWidget<12>>(transit);
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	TransitSnapshotButton* srcButton = findSnapshotButton(transitWidget, 3);
	REQUIRE(srcButton != nullptr);
	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);

	event::DragEnter eEnter;
	eEnter.button = GLFW_MOUSE_BUTTON_LEFT;
	eEnter.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragEnter(eEnter);
	REQUIRE(node->dropArmed == true);

	// The handler's `e.button == GLFW_MOUSE_BUTTON_LEFT` check gates the bind;
	// a right- or middle-button drop must leave both the binding and the
	// still-armed highlight untouched (only the id==0 no-op path clears it).
	event::DragDrop eDrop;
	eDrop.button = GLFW_MOUSE_BUTTON_RIGHT;
	eDrop.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragDrop(eDrop);

	REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);
	REQUIRE(node->dropArmed == true);
}

TEST_CASE("onDragLeave clears dropArmed", "[TransitPad]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitWidget<12>* transitWidget = h.addWidget<TransitWidget<12>>(transit);
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	TransitSnapshotButton* srcButton = findSnapshotButton(transitWidget, 3);
	REQUIRE(srcButton != nullptr);
	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	event::DragEnter eEnter;
	eEnter.button = GLFW_MOUSE_BUTTON_LEFT;
	eEnter.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragEnter(eEnter);
	REQUIRE(node->dropArmed == true);

	event::DragLeave eLeave;
	eLeave.origin = dynamic_cast<rack::widget::Widget*>(srcButton);
	node->onDragLeave(eLeave);

	REQUIRE(node->dropArmed == false);
}


// Helper: find the pad's XY screen widget.
static TransitPadXyScreenWidget<TransitPadModule<>>* findPadScreenWidget(rack::app::ModuleWidget* padWidget) {
	TransitPadXyScreenWidget<TransitPadModule<>>* found = nullptr;
	Test::traversal::walk(padWidget, [&](const Test::traversal::Visit& v) {
		auto* screen = dynamic_cast<TransitPadXyScreenWidget<TransitPadModule<>>*>(v.widget);
		if (screen) {
			found = screen;
			return false;
		}
		return true;
	});
	return found;
}

// Helper: find the pad's SEQ-EDIT drag/record widget (XySeqWidget.hpp).
static StoermelderPackOne::XySeqEditDragWidget<TransitPadModule<>>* findSeqEditDragWidget(rack::app::ModuleWidget* padWidget) {
	StoermelderPackOne::XySeqEditDragWidget<TransitPadModule<>>* found = nullptr;
	Test::traversal::walk(padWidget, [&](const Test::traversal::Visit& v) {
		auto* w = dynamic_cast<StoermelderPackOne::XySeqEditDragWidget<TransitPadModule<>>*>(v.widget);
		if (w) {
			found = w;
			return false;
		}
		return true;
	});
	return found;
}

// Helper: count the ui::MenuOverlay children currently on the scene.
// Always compared as a delta — earlier test cases in the same process leave
// overlays behind, so the absolute count is not meaningful.
static int menuOverlayCount() {
	int n = 0;
	for (rack::widget::Widget* c : APP->scene->children) {
		if (dynamic_cast<rack::ui::MenuOverlay*>(c)) n++;
	}
	return n;
}

// Regression for the `e.button == GLFW_PRESS` / `e.action == GLFW_PRESS` mixup:
// because GLFW_PRESS == 1 == GLFW_MOUSE_BUTTON_RIGHT, the condition collapsed to
// `e.button == 1` and fired on press *and* release, so one right-click on the
// empty screen area built the context menu twice. Note this covers XyScreenWidget,
// which ARENA uses as well.
TEST_CASE("One right-click on the pad screen builds exactly one context menu", "[TransitPad]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* screen = findPadScreenWidget(padWidget);
	REQUIRE(screen != nullptr);

	// Top-left corner of the screen: inside the widget but clear of every pad
	// point, so the press reaches the screen's own handler rather than a node's.
	const Vec emptySpot = Vec(5.f, 5.f);
	const int base = menuOverlayCount();

	event::Button ePress;
	rack::widget::EventContext cPress;
	ePress.context = &cPress;
	ePress.button = GLFW_MOUSE_BUTTON_RIGHT;
	ePress.action = GLFW_PRESS;
	ePress.pos = emptySpot;
	screen->onButton(ePress);
	const int afterPress = menuOverlayCount();

	event::Button eRelease;
	rack::widget::EventContext cRelease;
	eRelease.context = &cRelease;
	eRelease.button = GLFW_MOUSE_BUTTON_RIGHT;
	eRelease.action = GLFW_RELEASE;
	eRelease.pos = emptySpot;
	screen->onButton(eRelease);
	const int afterRelease = menuOverlayCount();

	// The press opens the menu...
	REQUIRE(afterPress - base == 1);
	// ...and the release must not open a second one.
	REQUIRE(afterRelease - afterPress == 0);
}


// "Lock pad" is tested for persistence elsewhere; this covers what it is *for*.
TEST_CASE("Locked pad refuses a left press on the screen", "[TransitPad]") {
	settings::allowCursorLock = false;

	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* screen = findPadScreenWidget(padWidget);
	REQUIRE(screen != nullptr);

	const Vec spot = Vec(5.f, 5.f);

	SECTION("Unlocked: the press passes through to XyScreenWidget") {
		REQUIRE(pad->isLocked() == false);
		event::Button e;
		rack::widget::EventContext c;
		e.context = &c;
		e.button = GLFW_MOUSE_BUTTON_LEFT;
		e.action = GLFW_PRESS;
		e.pos = spot;
		screen->onButton(e);
		// XyScreenWidget clears the selection on an empty-area left press; the
		// lock short-circuit returns before that ever runs.
		REQUIRE(c.target != screen);
	}

	SECTION("Locked: the screen consumes the press itself, so no node can be dragged") {
		pad->locked = true;
		event::Button e;
		rack::widget::EventContext c;
		e.context = &c;
		e.button = GLFW_MOUSE_BUTTON_LEFT;
		e.action = GLFW_PRESS;
		e.pos = spot;
		screen->onButton(e);
		REQUIRE(c.target == screen);
	}

	SECTION("Locked: right-click still opens the screen context menu") {
		pad->locked = true;
		const int base = menuOverlayCount();
		event::Button e;
		rack::widget::EventContext c;
		e.context = &c;
		e.button = GLFW_MOUSE_BUTTON_RIGHT;
		e.action = GLFW_PRESS;
		e.pos = spot;
		screen->onButton(e);
		REQUIRE(menuOverlayCount() - base == 1);
	}
}


// Space toggles pad active (ON_PARAM), Shift+Space toggles visualize mode.
// The null-module case is the regression: the module browser builds this
// widget with module == nullptr to render the preview, and onHoverKey
// dereferenced it unconditionally, so a space press while the browser
// preview was hovered segfaulted.
TEST_CASE("Space toggles pad active, modifier+space toggles visualize mode", "[TransitPad]") {
	settings::allowCursorLock = false;

	SECTION("With a module, space flips Pad active and consumes the event") {
		Test::Harness h;
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

		REQUIRE(pad->isPadActive() == true);

		event::HoverKey e;
		rack::widget::EventContext c;
		e.context = &c;
		e.key = GLFW_KEY_SPACE;
		e.action = GLFW_PRESS;
		e.mods = 0;
		padWidget->onHoverKey(e);
		REQUIRE(pad->isPadActive() == false);
		REQUIRE(c.target == padWidget);

		// A second press toggles it back on.
		rack::widget::EventContext c2;
		e.context = &c2;
		padWidget->onHoverKey(e);
		REQUIRE(pad->isPadActive() == true);
	}

	SECTION("Shift+space flips vizMode and consumes the event, leaving Pad active alone") {
		Test::Harness h;
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

		REQUIRE(pad->vizMode == false);

		event::HoverKey e;
		rack::widget::EventContext c;
		e.context = &c;
		e.key = GLFW_KEY_SPACE;
		e.action = GLFW_PRESS;
		e.mods = GLFW_MOD_SHIFT;
		padWidget->onHoverKey(e);
		REQUIRE(pad->vizMode == true);
		REQUIRE(pad->isPadActive() == true);
		REQUIRE(c.target == padWidget);

		// A second press toggles it back off.
		rack::widget::EventContext c2;
		e.context = &c2;
		padWidget->onHoverKey(e);
		REQUIRE(pad->vizMode == false);
	}

	SECTION("Visualize overlay hides while seq-edit is active, even with vizMode on") {
		Test::Harness h;
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
		REQUIRE(padWidget->vizOverlay != nullptr);

		pad->vizMode = true;
		padWidget->step();
		REQUIRE(padWidget->vizOverlay->visible == true);

		// Splines would otherwise clutter the pad while it's showing the
		// recorded motion-sequence path instead.
		pad->seqEdit = 0;
		padWidget->step();
		REQUIRE(padWidget->vizOverlay->visible == false);

		pad->seqEdit = -1;
		padWidget->step();
		REQUIRE(padWidget->vizOverlay->visible == true);
	}

	SECTION("A different modifier held with space is neither shortcut") {
		Test::Harness h;
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

		event::HoverKey e;
		rack::widget::EventContext c;
		e.context = &c;
		e.key = GLFW_KEY_SPACE;
		e.action = GLFW_PRESS;
		e.mods = RACK_MOD_CTRL;
		padWidget->onHoverKey(e);
		REQUIRE(pad->vizMode == false);
		REQUIRE(pad->isPadActive() == true);
	}

	SECTION("The browser preview (module == nullptr) survives a space press") {
		Test::Harness h;
		TransitPadWidget* padWidget = Test::createWidget<TransitPadWidget>("TransitPad");
		REQUIRE(padWidget->module == nullptr);

		event::HoverKey e;
		rack::widget::EventContext c;
		e.context = &c;
		e.key = GLFW_KEY_SPACE;
		e.action = GLFW_PRESS;
		e.mods = 0;
		REQUIRE_NOTHROW(padWidget->onHoverKey(e));
		Test::destroyWidget(padWidget);
	}
}

TEST_CASE("Shift+L toggles Lock pad", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	REQUIRE(pad->locked == false);

	event::HoverKey e;
	rack::widget::EventContext c;
	e.context = &c;
	e.key = GLFW_KEY_L;
	e.action = GLFW_PRESS;
	e.mods = GLFW_MOD_SHIFT;
	padWidget->onHoverKey(e);
	REQUIRE(pad->locked == true);
	REQUIRE(c.target == padWidget);

	// A second press toggles it back off.
	rack::widget::EventContext c2;
	e.context = &c2;
	padWidget->onHoverKey(e);
	REQUIRE(pad->locked == false);
}

TEST_CASE("L without Shift does not toggle Lock pad", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	event::HoverKey e;
	rack::widget::EventContext c;
	e.context = &c;
	e.key = GLFW_KEY_L;
	e.action = GLFW_PRESS;
	e.mods = 0;
	padWidget->onHoverKey(e);
	REQUIRE(pad->locked == false);
}

// Regression: StoermelderLedDisplay derives from LightWidget/TransparentWidget,
// whose onHover() is a no-op that never consumes the event, so onEnter/onLeave
// (and with them, ui::Tooltip) were never dispatched to the Mix motion-sequence
// LED display -- hovering over it silently showed no tooltip.
TEST_CASE("Hovering the Mix motion-sequence display shows a tooltip", "[TransitPad]") {
	settings::allowCursorLock = false;
	settings::tooltips = true;

	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	TransitPadXySeqLedDisplay<TransitPadModule<>>* seqDisplay = nullptr;
	Test::traversal::walk(padWidget, [&](const Test::traversal::Visit& v) {
		auto* d = dynamic_cast<TransitPadXySeqLedDisplay<TransitPadModule<>>*>(v.widget);
		if (d) {
			seqDisplay = d;
			return false;
		}
		return true;
	});
	REQUIRE(seqDisplay != nullptr);

	auto tooltipCount = [&]() {
		int n = 0;
		for (rack::widget::Widget* c : APP->scene->children) {
			if (dynamic_cast<rack::ui::Tooltip*>(c)) n++;
		}
		return n;
	};
	int base = tooltipCount();

	event::Enter enterEvent;
	rack::widget::EventContext ec;
	enterEvent.context = &ec;
	seqDisplay->onEnter(enterEvent);
	REQUIRE(tooltipCount() - base == 1);

	event::Leave leaveEvent;
	rack::widget::EventContext lc;
	leaveEvent.context = &lc;
	seqDisplay->onLeave(leaveEvent);
	REQUIRE(tooltipCount() - base == 0);
}

// Regression: XyScreenDragWidgetBase::onHover() checked only the circular
// hit-radius, not isActive() -- unlike onButton(), which already did. An
// inactive node (drawn nowhere, since drawLayer() early-outs on !isActive())
// still consumed hover at its stacked-but-invisible position, blocking the
// event from reaching anything beneath it and, on TransitPadSnapshotDragWidget,
// setting vizHoveredId / creating a tooltip for a point the user can't see.
TEST_CASE("An inactive node does not consume hover", "[TransitPad]") {
	settings::allowCursorLock = false;
	settings::tooltips = true;

	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	// snapshotsUsed defaults to 4, so node id 5 is inactive.
	REQUIRE(pad->snapshotsUsed == 4);
	REQUIRE(pad->isNodeActive(5) == false);

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 5));
	REQUIRE(node != nullptr);

	event::Hover e;
	rack::widget::EventContext c;
	e.context = &c;
	e.pos = node->box.size.div(2);
	node->onHover(e);

	REQUIRE(c.target == nullptr);
	REQUIRE(pad->vizHoveredId != 5);
}

// Regression: the node-menu sliders hardcoded 0.5 as their reset value, so a
// double-click reset Amount and Radius to 50% although a fresh snapshot point
// (and Initialize) uses 100% for both.
TEST_CASE("Amount/Radius slider reset values match the node defaults", "[TransitPad]") {
	Test::Harness h;
	typedef TransitPadModule<> M;
	M* m = h.addModule<M>("TransitPad");
	for (uint8_t i = 0; i < 8; i++) {
		StoermelderPackOne::XyScreenRadiusSlider<M>::RadiusQuantity radius(m, i);
		StoermelderPackOne::XyScreenAmountSlider<M>::AmountQuantity amount(m, i);
		REQUIRE(radius.getDefaultValue() == 1.f);
		REQUIRE(amount.getDefaultValue() == 1.f);
	}
}

// Context menus: node ("Bind snapshot"/"Unbind snapshot"), screen
// ("Snapshot-set node positions" -> "Off"), its module-level mirror, and
// the set-button menu ("Store positions", the set-label field, "Reset").
// Reuses connectPad() (TransitPad.test.expander.hpp) and
// findPadNodeWidget()/findPadScreenWidget() (above) -- same file/namespace.

// Finds a direct MenuItem child by its label text, matching Ahab's
// findMenuItemByText(): menu items are added as plain widget::Widget
// children, so a linear scan + dynamic_cast is the whole helper needed.
static ui::MenuItem* findMenuItemByText(ui::Menu* menu, const std::string& text) {
	for (rack::widget::Widget* w : menu->children) {
		auto* item = dynamic_cast<ui::MenuItem*>(w);
		if (item && item->text == text) return item;
	}
	return nullptr;
}

// Finds a TransitPadSetButton param widget on the pad by its set index.
static TransitPadSetButton<TransitPadModule<>>* findSetButton(rack::app::ModuleWidget* padWidget, size_t setIndex) {
	for (rack::widget::Widget* w : padWidget->getParams()) {
		auto* btn = dynamic_cast<TransitPadSetButton<TransitPadModule<>>*>(w);
		if (btn && btn->setIndex == setIndex) return btn;
	}
	return nullptr;
}


// Node context menu: "Bind snapshot" / "Unbind snapshot"

TEST_CASE("Node menu: Bind snapshot binds to getSelectedSlot()", "[TransitPad][widget]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	// presetSave() also sets preset = p as a side effect, giving Transit a
	// selected slot distinct from any pad point's default binding.
	transit->presetSave(7);
	REQUIRE(transit->getSelectedSlot() == 7);

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	ui::Menu menu;
	node->prependContextMenu(&menu);

	auto* bind = findMenuItemByText(&menu, "Bind snapshot");
	REQUIRE(bind != nullptr);
	REQUIRE(bind->disabled == false);

	bind->onAction(*(new event::Action));
	REQUIRE(pad->snapshots[pad->currentSet][0].id == 7);
}

TEST_CASE("Node menu: Bind snapshot is disabled with no Transit connected", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	h.dspStep();

	REQUIRE(pad->masterModule == nullptr);

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	ui::Menu menu;
	node->prependContextMenu(&menu);

	auto* bind = findMenuItemByText(&menu, "Bind snapshot");
	REQUIRE(bind != nullptr);
	REQUIRE(bind->disabled == true);

	// "Unbind" only cares about lock state, not about Transit being present.
	auto* unbind = findMenuItemByText(&menu, "Unbind snapshot");
	REQUIRE(unbind != nullptr);
	REQUIRE(unbind->disabled == false);
}

TEST_CASE("Node menu: Bind snapshot is disabled when no slot is selected on Transit", "[TransitPad][widget]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();

	// A freshly connected Transit has no selected slot (preset == -1).
	REQUIRE(transit->getSelectedSlot() == -1);

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	ui::Menu menu;
	node->prependContextMenu(&menu);

	auto* bind = findMenuItemByText(&menu, "Bind snapshot");
	REQUIRE(bind != nullptr);
	REQUIRE(bind->disabled == true);
}

TEST_CASE("Node menu: Bind and Unbind are both disabled while the pad is locked", "[TransitPad][widget]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	connectPad(h, transit, pad);
	h.dspStep();
	transit->presetSave(7);
	pad->locked = true;

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	ui::Menu menu;
	node->prependContextMenu(&menu);

	auto* bind = findMenuItemByText(&menu, "Bind snapshot");
	REQUIRE(bind != nullptr);
	REQUIRE(bind->disabled == true);

	auto* unbind = findMenuItemByText(&menu, "Unbind snapshot");
	REQUIRE(unbind != nullptr);
	REQUIRE(unbind->disabled == true);
}

TEST_CASE("Node menu: Unbind snapshot clears the pad point's binding", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	h.dspStep();

	REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	ui::Menu menu;
	node->prependContextMenu(&menu);

	auto* unbind = findMenuItemByText(&menu, "Unbind snapshot");
	REQUIRE(unbind != nullptr);
	REQUIRE(unbind->disabled == false);

	unbind->onAction(*(new event::Action));
	REQUIRE(pad->snapshots[pad->currentSet][0].id == -1);
}

TEST_CASE("Node menu: Load snapshot is disabled with no Transit, no binding, or an unused slot", "[TransitPad][widget]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	SECTION("No Transit connected") {
		REQUIRE(pad->masterModule == nullptr);
		// Node 0 defaults to bound slot 0, but there is nothing to load from.
		ui::Menu menu;
		node->prependContextMenu(&menu);
		auto* load = findMenuItemByText(&menu, "Load snapshot");
		REQUIRE(load != nullptr);
		REQUIRE(load->disabled == true);
	}

	SECTION("Connected, but the node is unbound") {
		connectPad(h, transit, pad);
		h.dspStep();
		pad->bindSnapshot(0, -1);

		ui::Menu menu;
		node->prependContextMenu(&menu);
		auto* load = findMenuItemByText(&menu, "Load snapshot");
		REQUIRE(load != nullptr);
		REQUIRE(load->disabled == true);
	}

	SECTION("Connected and bound, but the slot was never saved") {
		connectPad(h, transit, pad);
		h.dspStep();
		// Node 0's default binding (slot 0) is in range but nothing was ever
		// saved to it.
		REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);
		REQUIRE(transit->isSlotUsed(0) == false);

		ui::Menu menu;
		node->prependContextMenu(&menu);
		auto* load = findMenuItemByText(&menu, "Load snapshot");
		REQUIRE(load != nullptr);
		REQUIRE(load->disabled == true);
	}
}

TEST_CASE("Node menu: Load snapshot switches Pad active off, then applies the bound slot", "[TransitPad][widget][Transit]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	TestParamModule* target = h.adoptModule(new TestParamModule);

	connectPad(h, transit, pad);
	h.dspStep();
	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Slot 5 holds a value distinct from anything the pad's live blend would
	// produce, and from the sentinel the target is driven to below.
	target->params[TestParamModule::PARAM_A].setValue(0.75f);
	transit->presetSave(5);
	pad->bindSnapshot(0, 5);

	REQUIRE(pad->isPadActive() == true);

	auto* node = dynamic_cast<TransitPadSnapshotDragWidget<TransitPadModule<>>*>(findPadNodeWidget(padWidget, 0));
	REQUIRE(node != nullptr);

	ui::Menu menu;
	node->prependContextMenu(&menu);
	auto* load = findMenuItemByText(&menu, "Load snapshot");
	REQUIRE(load != nullptr);
	REQUIRE(load->disabled == false);

	// Drive the target to a sentinel first: while the pad is active it keeps
	// overwriting this every tick, so if Load snapshot failed to switch the
	// pad off, the sentinel (not slot 5's 0.75) would still be there below.
	target->params[TestParamModule::PARAM_A].setValue(0.1f);
	h.dspSteps(20);

	load->onAction(*(new event::Action));

	REQUIRE(pad->isPadActive() == false);

	// presetLoad()'s crossfade needs several ticks (and the pad genuinely off,
	// not just switched off this instant) to actually reach the target.
	h.dspSteps(200);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.75f).margin(0.01f));
}


// Screen context menu: "Snapshot-set node positions" -> "Off"

// Regression covered elsewhere by calling clearNodePositions() directly; this
// drives the same action through the actual menu item a user clicks.
TEST_CASE("Screen menu: 'Node positions' -> Off calls clearNodePositions() through the menu", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* screen = findPadScreenWidget(padWidget);
	REQUIRE(screen != nullptr);

	// Distinctive stored layout on a couple of sets, so "wiped back to
	// defaults" is unambiguous.
	pad->snapshots[2][0].x = 0.8f;
	pad->snapshots[2][0].y = 0.9f;
	pad->snapshots[5][3].radius = 0.3f;
	pad->nodePosMode = NODEPOSMODE::STORE;

	ui::Menu menu;
	screen->appendContextMenu(&menu);

	auto* nodePositions = findMenuItemByText(&menu, "Snapshot-set node positions");
	REQUIRE(nodePositions != nullptr);
	ui::Menu* submenu = nodePositions->createChildMenu();
	REQUIRE(submenu != nullptr);

	auto* off = findMenuItemByText(submenu, "Off");
	REQUIRE(off != nullptr);

	off->onAction(*(new event::Action));

	REQUIRE(pad->nodePosMode == NODEPOSMODE::OFF);
	REQUIRE(pad->snapshots[2][0].x == pad->getNodePqX(0)->getDefaultValue());
	REQUIRE(pad->snapshots[2][0].y == pad->getNodePqY(0)->getDefaultValue());
	REQUIRE(pad->snapshots[5][3].radius == pad->getNodeRadiusDefault(3));
}

// TransitPadWidget::appendContextMenu mirrors the screen's snapshot-set menu
// so a right-click anywhere on the module reaches the same options -- flagged
// in review as untested. This drives the module-level menu, not the screen's,
// and checks the same "Off" wipe reaches through it.
TEST_CASE("Module menu mirrors the screen's node-positions submenu, including Off's wipe", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	pad->snapshots[1][0].x = 0.77f;
	pad->nodePosMode = NODEPOSMODE::AUTO;

	ui::Menu menu;
	padWidget->appendContextMenu(&menu);

	auto* nodePositions = findMenuItemByText(&menu, "Snapshot-set node positions");
	REQUIRE(nodePositions != nullptr);
	ui::Menu* submenu = nodePositions->createChildMenu();
	REQUIRE(submenu != nullptr);

	auto* off = findMenuItemByText(submenu, "Off");
	REQUIRE(off != nullptr);
	off->onAction(*(new event::Action));

	REQUIRE(pad->nodePosMode == NODEPOSMODE::OFF);
	REQUIRE(pad->snapshots[1][0].x == pad->getNodePqX(0)->getDefaultValue());

	// The rest of the mirrored menu is present too, not just this one item.
	REQUIRE(findMenuItemByText(&menu, "Visualize") != nullptr);
	REQUIRE(findMenuItemByText(&menu, "Lock pad") != nullptr);
	REQUIRE(findMenuItemByText(&menu, "Snapshot-set CV mode") != nullptr);
}

TEST_CASE("Module menu mirror is a no-op without a module (browser preview)", "[TransitPad][widget]") {
	TransitPadWidget* padWidget = Test::createWidget<TransitPadWidget>("TransitPad");
	REQUIRE(padWidget->module == nullptr);

	ui::Menu menu;
	REQUIRE_NOTHROW(padWidget->appendContextMenu(&menu));
	REQUIRE(findMenuItemByText(&menu, "Snapshot-set node positions") == nullptr);

	Test::destroyWidget(padWidget);
}


// Set-button menu: "Store positions", label field, "Reset"

TEST_CASE("Set-button menu: Store positions is enabled only in Store mode", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* button = findSetButton(padWidget, 0);
	REQUIRE(button != nullptr);

	SECTION("Store mode: enabled") {
		pad->nodePosMode = NODEPOSMODE::STORE;
		ui::Menu menu;
		button->appendContextMenu(&menu);
		auto* store = findMenuItemByText(&menu, "Store positions");
		REQUIRE(store != nullptr);
		REQUIRE(store->disabled == false);
	}

	SECTION("Auto mode: present but disabled") {
		pad->nodePosMode = NODEPOSMODE::AUTO;
		ui::Menu menu;
		button->appendContextMenu(&menu);
		auto* store = findMenuItemByText(&menu, "Store positions");
		REQUIRE(store != nullptr);
		REQUIRE(store->disabled == true);
	}

	SECTION("Off: absent entirely") {
		pad->nodePosMode = NODEPOSMODE::OFF;
		ui::Menu menu;
		button->appendContextMenu(&menu);
		auto* store = findMenuItemByText(&menu, "Store positions");
		REQUIRE(store == nullptr);
	}
}

TEST_CASE("Set-button menu: Store positions captures the live layout into that set", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* button = findSetButton(padWidget, 3);
	REQUIRE(button != nullptr);

	pad->nodePosMode = NODEPOSMODE::STORE;
	pad->nodes.setXyImmediate(0, 0.42f, 0.24f);

	ui::Menu menu;
	button->appendContextMenu(&menu);
	auto* store = findMenuItemByText(&menu, "Store positions");
	REQUIRE(store != nullptr);
	REQUIRE(store->disabled == false);

	store->onAction(*(new event::Action));

	REQUIRE(pad->snapshots[3][0].x == 0.42f);
	REQUIRE(pad->snapshots[3][0].y == 0.24f);
}

// The label field commits its text to setLabel on Enter and closes its
// MenuOverlay, mirroring a user typing a name and pressing Enter. Needs a
// real createMenu()-built overlay (not a bare stack Menu), since onSelectKey
// looks one up via getAncestorOfType<ui::MenuOverlay>().
TEST_CASE("Set-button menu: label field commits its text to setLabel on Enter", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* button = findSetButton(padWidget, 2);
	REQUIRE(button != nullptr);

	REQUIRE(pad->setLabel[2] == "");

	ui::Menu* menu = createMenu();
	button->appendContextMenu(menu);
	auto* label = findMenuItemByText(menu, "Label");
	REQUIRE(label != nullptr);
	// setChildMenu(), not a bare createChildMenu(): it parents the submenu
	// under the MenuOverlay the same way opening it for real would, which
	// LabelField::onSelectKey needs to find via getAncestorOfType().
	ui::Menu* submenu = label->createChildMenu();
	REQUIRE(submenu != nullptr);
	menu->setChildMenu(submenu);

	TransitPadSetButton<TransitPadModule<>>::LabelField* field = nullptr;
	for (rack::widget::Widget* w : submenu->children) {
		field = dynamic_cast<TransitPadSetButton<TransitPadModule<>>::LabelField*>(w);
		if (field) break;
	}
	REQUIRE(field != nullptr);
	REQUIRE(field->module == pad);

	field->text = "Chorus";

	event::SelectKey e;
	e.key = GLFW_KEY_ENTER;
	e.action = GLFW_PRESS;
	rack::widget::EventContext c;
	e.context = &c;
	field->onSelectKey(e);

	REQUIRE(pad->setLabel[2] == "Chorus");

	ui::MenuOverlay* overlay = field->getAncestorOfType<ui::MenuOverlay>();
	REQUIRE(overlay != nullptr);
	// requestDelete() marks it for removal on the next scene step rather than
	// removing it synchronously; nothing steps the scene in this harness, so
	// it must be torn down by hand, same as any other hand-added scene child
	// (FRAMEWORK.md: finalizeWidget() before deletion, or it dangles into
	// every later TEST_CASE).
	REQUIRE(overlay->requestedDelete == true);
	APP->scene->removeChild(overlay);
	APP->event->finalizeWidget(overlay);
	delete overlay;
	// padWidget itself is harness-owned (h.addWidget()), torn down when h
	// goes out of scope -- no manual destroyWidget() here.
}

TEST_CASE("Set-button menu: Reset clears the set's label", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* button = findSetButton(padWidget, 4);
	REQUIRE(button != nullptr);

	pad->setLabel[4] = "Bridge";

	ui::Menu menu;
	button->appendContextMenu(&menu);
	auto* label = findMenuItemByText(&menu, "Label");
	REQUIRE(label != nullptr);
	ui::Menu* submenu = label->createChildMenu();
	REQUIRE(submenu != nullptr);

	auto* reset = findMenuItemByText(submenu, "Reset");
	REQUIRE(reset != nullptr);

	reset->onAction(*(new event::Action));

	REQUIRE(pad->setLabel[4] == "");
}

// Set-button menu: "Copy" / "Paste"
// PasteItem's step() override chains into MenuItem::step(), which measures
// text via APP->window->vg -- null in every test binary (see FRAMEWORK.md's
// headless-window traps). So these tests drive the enable/disable + rightText
// logic through onAction()'s effects rather than calling step() directly.

TEST_CASE("Set-button menu: Copy records the set", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);
	auto* button = findSetButton(padWidget, 0);
	REQUIRE(button != nullptr);

	REQUIRE(pad->setCopy == -1);

	ui::Menu menu;
	button->appendContextMenu(&menu);
	auto* copy = findMenuItemByText(&menu, "Copy");
	REQUIRE(copy != nullptr);
	copy->onAction(*(new event::Action));

	REQUIRE(pad->setCopy == 0);
}

TEST_CASE("Set-button menu: Paste copies snapshot bindings but not color or label", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	pad->snapshots[1][0].id = 5;
	pad->setColor[1] = nvgRGBA(0x11, 0x22, 0x33, 0xff);
	pad->setLabel[1] = "Verse";
	pad->setCopy = 1;

	NVGcolor origColor = pad->setColor[6];
	pad->setLabel[6] = "Untouched";

	auto* button = findSetButton(padWidget, 6);
	REQUIRE(button != nullptr);

	ui::Menu menu;
	button->appendContextMenu(&menu);
	auto* paste = findMenuItemByText(&menu, "Paste");
	REQUIRE(paste != nullptr);

	paste->onAction(*(new event::Action));

	REQUIRE(pad->snapshots[6][0].id == 5);
	// Color and label are per-set identity, not "content" -- Paste must leave
	// them alone.
	REQUIRE(pad->setColor[6].r == Catch::Approx(origColor.r));
	REQUIRE(pad->setColor[6].g == Catch::Approx(origColor.g));
	REQUIRE(pad->setColor[6].b == Catch::Approx(origColor.b));
	REQUIRE(pad->setLabel[6] == "Untouched");
}

TEST_CASE("Set-button menu: Paste copies pad-point geometry only in a position-store mode", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	pad->snapshots[1][0].id = 5;
	pad->snapshots[1][0].x = 0.11f;
	pad->snapshots[1][0].y = 0.22f;
	pad->snapshots[1][0].radius = 0.33f;
	pad->snapshots[1][0].amount = 0.44f;
	pad->mixX[1] = 0.55f;
	pad->mixY[1] = 0.66f;
	pad->setCopy = 1;

	auto* button = findSetButton(padWidget, 6);
	REQUIRE(button != nullptr);

	SECTION("nodePosMode Off: bindings copy, geometry is left alone") {
		pad->nodePosMode = NODEPOSMODE::OFF;
		float origX = pad->snapshots[6][0].x;
		float origMixX = pad->mixX[6];

		ui::Menu menu;
		button->appendContextMenu(&menu);
		auto* paste = findMenuItemByText(&menu, "Paste");
		REQUIRE(paste != nullptr);
		paste->onAction(*(new event::Action));

		REQUIRE(pad->snapshots[6][0].id == 5);
		REQUIRE(pad->snapshots[6][0].x == origX);
		REQUIRE(pad->mixX[6] == origMixX);
	}

	SECTION("nodePosMode Store: bindings and geometry both copy") {
		pad->nodePosMode = NODEPOSMODE::STORE;

		ui::Menu menu;
		button->appendContextMenu(&menu);
		auto* paste = findMenuItemByText(&menu, "Paste");
		REQUIRE(paste != nullptr);
		paste->onAction(*(new event::Action));

		REQUIRE(pad->snapshots[6][0].id == 5);
		REQUIRE(pad->snapshots[6][0].x == 0.11f);
		REQUIRE(pad->snapshots[6][0].y == 0.22f);
		REQUIRE(pad->snapshots[6][0].radius == 0.33f);
		REQUIRE(pad->snapshots[6][0].amount == 0.44f);
		REQUIRE(pad->mixX[6] == 0.55f);
		REQUIRE(pad->mixY[6] == 0.66f);
	}
}

// Set-button ParamQuantity: "Active" tooltip

TEST_CASE("Set ParamQuantity reports Active only for the current set", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	h.addWidget<TransitPadWidget>(pad);

	REQUIRE(pad->currentSet == 0);
	REQUIRE(pad->paramQuantities[TransitPadModule<>::SET_PARAM + 0]->getDisplayValueString() == "Active");
	REQUIRE(pad->paramQuantities[TransitPadModule<>::SET_PARAM + 1]->getDisplayValueString() == "");

	pad->changeSet(1);

	REQUIRE(pad->paramQuantities[TransitPadModule<>::SET_PARAM + 0]->getDisplayValueString() == "");
	REQUIRE(pad->paramQuantities[TransitPadModule<>::SET_PARAM + 1]->getDisplayValueString() == "Active");
}

// SEQ-EDIT: "Clear" followed by a fresh drag (XySeqWidget.hpp)
// Bug: the module-level "Clear" menu item only zeroes seqData[...].length; it
// never touches XySeqEditDragWidget::index, the widget's own write cursor
// into seqData[...].x/y[]. A drag recorded before Clear could leave index at,
// say, 40; onDragMove() then starts overwriting x[40]/y[40] onward instead of
// x[0]/y[0], so length grows straight past the old, still-populated tail
// waypoints, and the pre-Clear path reappears once the new drag is long
// enough to reach them. onDragStart() already reset length to 0 for the same
// reason -- it just forgot to reset index alongside it.

TEST_CASE("SEQ-EDIT: a fresh drag after Clear doesn't resurrect the old path's tail", "[TransitPad][widget]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitPadWidget* padWidget = h.addWidget<TransitPadWidget>(pad);

	auto* rec = findSeqEditDragWidget(padWidget);
	REQUIRE(rec != nullptr);
	// ThemedModuleWidget::step() no-ops under settings::headless (true in every
	// test binary), so XySeqEditWidget::step() -- which applies module->seqEdit
	// to recWidget via init() -- never runs through padWidget->step(). Step the
	// parent XySeqEditWidget directly instead.
	pad->seqEdit = 0;
	auto* parentSeqEditWidget = dynamic_cast<StoermelderPackOne::XySeqEditWidget<TransitPadModule<>>*>(rec->parent);
	REQUIRE(parentSeqEditWidget != nullptr);
	parentSeqEditWidget->step();
	REQUIRE(rec->id == 0);

	// Simulate a long recorded drag leaving a stale, non-zero index -- as a
	// real drag of 40+ points (well under XYSEQ_LENGTH) would.
	for (int i = 0; i < 40; i++) {
		pad->seqData[0][0].x[i] = 0.9f;
		pad->seqData[0][0].y[i] = 0.9f;
	}
	pad->seqData[0][0].length = 40;
	rec->index = 40;

	// Clear via the module call the context menu item uses directly
	// (XySeqWidget.hpp's createContextMenu()), bypassing rec->clear().
	pad->seqClear(0);
	REQUIRE(pad->seqLength(0) == 0);

	// A fresh drag: onDragStart() resets the recording state, then
	// onDragMove() writes the first waypoint immediately (timerClear from
	// onDragStart() lets it bypass the ~65ms recording-interval gate that
	// throttles every subsequent move within a real drag).
	Vec scenePos = rec->getAbsoluteOffset(Vec());
	h.events().hover(scenePos);

	event::DragStart eStart;
	eStart.button = GLFW_MOUSE_BUTTON_LEFT;
	rec->onDragStart(eStart);
	REQUIRE(rec->index == 0);

	h.events().hover(scenePos.plus(Vec(10.f, 10.f)));
	event::DragMove eMove;
	eMove.button = GLFW_MOUSE_BUTTON_LEFT;
	rec->onDragMove(eMove);

	// Only the recorded point from this drag -- none of the stale 0.9f/0.9f
	// tail from before Clear.
	REQUIRE(pad->seqLength(0) == 1);
	REQUIRE(pad->seqData[0][0].x[0] != 0.9f);
	REQUIRE(pad->seqData[0][0].y[0] != 0.9f);

	rec->dragChange = nullptr;
}
