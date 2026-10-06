// Mb.test.keymap.hpp — MB's keyboard shortcuts, driven through the shared "Mb" Keymap.
// Included by Mb.test.cpp inside namespace __keymap.
//
// v2 only: v1::ModuleBrowser can't be driven headless (its sidebar MenuItems call
// bndLabelWidth(APP->window->vg, ...) in step(), and APP->window is null in every test
// binary), the same limit noted in Mb.test.modelbox.hpp.
//
// Keymaps is a process-wide registry, so the fixture resets it on entry and exit: a rebind in
// one TEST_CASE must not leak into the next.

static void settleKeymapLayout(rack::widget::Widget* w) {
	w->step();
	w->step();
}

// Declared before the harness so it is destroyed after it: ~BrowserOverlay writes the side view
// state back into pluginSettings while the harness tears down, and that must not leak into the
// next TEST_CASE (where a click on a module box no longer hides the browser).
struct SideViewStateGuard {
	bool savedSetting = pluginSettings.mbSideView;
	bool savedGlobal = sideView;
	~SideViewStateGuard() {
		pluginSettings.mbSideView = savedSetting;
		sideView = savedGlobal;
	}
};

struct KeymapFixture {
	SideViewStateGuard sideViewGuard;
	Test::Harness h;
	MbModule* m;
	MbWidget* mw;
	BrowserOverlay* overlay;
	v2::ModuleBrowser* browser;
	std::shared_ptr<Keymap> km;

	explicit KeymapFixture(bool sideViewOn = false) {
		Keymaps::resetForTest();
		// BrowserOverlay reads side view from the settings when it is constructed.
		pluginSettings.mbSideView = sideViewOn;
		APP->scene->box.size = rack::math::Vec(1024, 300);

		m = h.addModule<MbModule>("Mb");
		m->mode = MODE::V2;
		mw = h.addWidget<MbWidget>(m);
		REQUIRE(mw->active);
		overlay = mw->browserOverlay;
		REQUIRE(overlay != nullptr);

		overlay->show();
		settleKeymapLayout(overlay);
		browser = dynamic_cast<v2::ModuleBrowser*>(overlay->mbV2);
		REQUIRE(browser != nullptr);
		REQUIRE(browser->visible);

		h.events().select(browser->searchField);
		km = Keymaps::open("Mb");
	}

	~KeymapFixture() {
		Keymaps::resetForTest();
	}

	// Straight to the search field's onSelectKey, not through EventDriver::key(): a key nothing
	// consumes (an unbound or rebound-away Escape, say) would otherwise fall through to Rack's
	// Scene::onHoverKey, which dereferences APP->window and segfaults headless.
	bool press(int key, int mods = 0) {
		rack::widget::EventContext context;
		rack::widget::Widget::SelectKeyEvent e;
		e.context = &context;
		e.key = key;
		e.action = GLFW_PRESS;
		e.mods = mods;
		browser->searchField->onSelectKey(e);
		return context.target != nullptr;
	}

	void typeText(const std::string& s) {
		h.events().type(s);
		settleKeymapLayout(overlay);
	}
};

TEST_CASE("MB keymap: default bindings in the v2 search field", "[Mb][Widget][Keymap]") {
	KeymapFixture fx;

	SECTION("Escape closes the browser") {
		REQUIRE(fx.press(GLFW_KEY_ESCAPE));
		REQUIRE_FALSE(fx.overlay->visible);
	}

	SECTION("Space on an empty search toggles favorites") {
		REQUIRE_FALSE(fx.browser->favorite);
		REQUIRE(fx.press(GLFW_KEY_SPACE));
		REQUIRE(fx.browser->favorite);
		REQUIRE(fx.press(GLFW_KEY_SPACE));
		REQUIRE_FALSE(fx.browser->favorite);
	}

	SECTION("Space with text in the search leaves favorites alone") {
		fx.typeText("abc");
		fx.press(GLFW_KEY_SPACE);
		REQUIRE_FALSE(fx.browser->favorite);
	}

	SECTION("Shift+Space and Ctrl+Space both toggle hidden") {
		REQUIRE_FALSE(fx.browser->hidden);
		REQUIRE(fx.press(GLFW_KEY_SPACE, GLFW_MOD_SHIFT));
		REQUIRE(fx.browser->hidden);
		REQUIRE(fx.press(GLFW_KEY_SPACE, RACK_MOD_CTRL));
		REQUIRE_FALSE(fx.browser->hidden);
	}

	SECTION("Backspace on an empty search clears the filters") {
		fx.browser->brand = "TestBrand";
		REQUIRE(fx.press(GLFW_KEY_BACKSPACE));
		REQUIRE(fx.browser->brand.empty());
	}

	SECTION("Backspace with text edits the text and keeps the filters") {
		fx.browser->brand = "TestBrand";
		fx.typeText("abc");
		fx.press(GLFW_KEY_BACKSPACE);
		settleKeymapLayout(fx.overlay);
		REQUIRE(fx.browser->searchField->text == "ab");
		REQUIRE(fx.browser->brand == "TestBrand");
	}

	SECTION("Up and Down are consumed as module navigation") {
		REQUIRE(fx.press(GLFW_KEY_DOWN));
		REQUIRE(fx.press(GLFW_KEY_UP));
	}
}

TEST_CASE("MB keymap: rebinding replaces the default", "[Mb][Widget][Keymap]") {
	KeymapFixture fx;

	SECTION("A rebound action fires on the new combo only") {
		fx.km->bind("browser.close", KeyCombo("Ctrl+Q"));

		fx.press(GLFW_KEY_ESCAPE);
		REQUIRE(fx.overlay->visible);

		REQUIRE(fx.press(GLFW_KEY_Q, RACK_MOD_CTRL));
		REQUIRE_FALSE(fx.overlay->visible);
	}

	SECTION("An unbound action does nothing") {
		fx.km->unbind("browser.close");
		fx.press(GLFW_KEY_ESCAPE);
		REQUIRE(fx.overlay->visible);
	}

	SECTION("Rebinding the hidden toggle drops its Ctrl+Space alias too") {
		fx.km->bind("browser.hidden.toggle", KeyCombo("Ctrl+G"));
		fx.press(GLFW_KEY_SPACE, GLFW_MOD_SHIFT);
		fx.press(GLFW_KEY_SPACE, RACK_MOD_CTRL);
		REQUIRE_FALSE(fx.browser->hidden);
		REQUIRE(fx.press(GLFW_KEY_G, RACK_MOD_CTRL));
		REQUIRE(fx.browser->hidden);
	}
}

// Unbinding Left/Right in the keymap hands those keys to the search field's text cursor, which
// is what the "Arrow keys select modules" option does by other means.
TEST_CASE("MB keymap: unbinding Left/Right moves the text cursor instead of the selection", "[Mb][Widget][Keymap]") {
	KeymapFixture fx;
	rack::ui::TextField* field = fx.browser->searchField;
	fx.typeText("abc");
	REQUIRE(field->cursor == 3);

	SECTION("Bound (default): Left/Right navigate the results, cursor stays put") {
		REQUIRE(fx.press(GLFW_KEY_LEFT));
		REQUIRE(field->cursor == 3);
		REQUIRE(fx.press(GLFW_KEY_RIGHT));
		REQUIRE(field->cursor == 3);
	}

	SECTION("Unbound: Left/Right move the text cursor") {
		fx.km->unbind("browser.v2.nav.left");
		fx.km->unbind("browser.v2.nav.right");

		fx.press(GLFW_KEY_LEFT);
		fx.press(GLFW_KEY_LEFT);
		REQUIRE(field->cursor == 1);
		fx.press(GLFW_KEY_RIGHT);
		REQUIRE(field->cursor == 2);
	}

	SECTION("Unbinding only Left leaves Right navigating the results") {
		fx.km->unbind("browser.v2.nav.left");

		fx.press(GLFW_KEY_LEFT);
		REQUIRE(field->cursor == 2);
		REQUIRE(fx.press(GLFW_KEY_RIGHT));
		REQUIRE(field->cursor == 2);
	}

	SECTION("Up/Down keep navigating when Left/Right are unbound") {
		fx.km->unbind("browser.v2.nav.left");
		fx.km->unbind("browser.v2.nav.right");

		REQUIRE(fx.press(GLFW_KEY_DOWN));
		REQUIRE(fx.press(GLFW_KEY_UP));
		REQUIRE(field->cursor == 3);
	}
}

// The "Arrow keys select modules (v2)" menu option is a shortcut to the same two bindings.
TEST_CASE("MB keymap: the arrow-key menu option binds and unbinds Left/Right", "[Mb][Widget][Keymap]") {
	KeymapFixture fx;
	rack::ui::TextField* field = fx.browser->searchField;
	fx.typeText("abc");

	REQUIRE(arrowKeyNavigationEnabled(fx.km));

	SECTION("Disabling unbinds both actions and frees the text cursor") {
		setArrowKeyNavigation(fx.km, false);
		REQUIRE_FALSE(arrowKeyNavigationEnabled(fx.km));
		fx.press(GLFW_KEY_LEFT);
		REQUIRE(field->cursor == 2);
	}

	SECTION("Enabling restores the default keys") {
		setArrowKeyNavigation(fx.km, false);
		setArrowKeyNavigation(fx.km, true);
		REQUIRE(arrowKeyNavigationEnabled(fx.km));
		REQUIRE(fx.press(GLFW_KEY_LEFT));
		REQUIRE(field->cursor == 3);
	}

	SECTION("A custom binding on one action counts as enabled") {
		fx.km->unbind("browser.v2.nav.right");
		fx.km->bind("browser.v2.nav.left", KeyCombo("Ctrl+Left"));
		REQUIRE(arrowKeyNavigationEnabled(fx.km));
		fx.press(GLFW_KEY_LEFT);
		REQUIRE(field->cursor == 2);
		REQUIRE(fx.press(GLFW_KEY_LEFT, RACK_MOD_CTRL));
		REQUIRE(field->cursor == 2);
	}
}

TEST_CASE("MB keymap: the *.always actions are unbound and ignore the search text", "[Mb][Widget][Keymap]") {
	KeymapFixture fx;

	SECTION("Unbound by default") {
		REQUIRE(fx.km->combosFor("browser.clear.always").empty());
		REQUIRE(fx.km->combosFor("browser.favorite.toggle.always").empty());
	}

	SECTION("Clear works with text in the search, unlike browser.clear") {
		fx.km->bind("browser.clear.always", KeyCombo("Ctrl+K"));
		fx.browser->brand = "TestBrand";
		fx.typeText("abc");

		REQUIRE(fx.press(GLFW_KEY_K, RACK_MOD_CTRL));
		REQUIRE(fx.browser->brand.empty());
		REQUIRE(fx.browser->searchField->text.empty());
	}

	SECTION("Favorite toggle works with text in the search and keeps the text") {
		fx.km->bind("browser.favorite.toggle.always", KeyCombo("Ctrl+D"));
		fx.typeText("abc");

		REQUIRE(fx.press(GLFW_KEY_D, RACK_MOD_CTRL));
		REQUIRE(fx.browser->favorite);
		REQUIRE(fx.browser->searchField->text == "abc");
	}
}

// The real Brand/Tag/Custom Tag buttons build DropdownChoiceItems, whose setRawText() measures
// text through APP->window (null headless). The shortcut handlers only call the browser's
// button pointers' virtual onAction() and compare them with a dropdown's `opener`, so stubs
// that record the call exercise exactly that logic.
struct StubChoiceButton : rack::ui::ChoiceButton {
	int opened = 0;
	void onAction(const ActionEvent& e) override { opened++; }
};

struct LayoutFixture : KeymapFixture {
	StubChoiceButton brandStub, tagStub, customTagStub, widthStub;
	rack::ui::ChoiceButton* savedBrand;
	rack::ui::ChoiceButton* savedTag;
	rack::ui::ChoiceButton* savedCustomTag;
	rack::ui::ChoiceButton* savedWidth;
	std::vector<rack::ui::MenuOverlay*> overlays;

	LayoutFixture() {
		savedBrand = browser->brandButton;
		savedTag = browser->tagButton;
		savedCustomTag = browser->customTagButton;
		savedWidth = browser->widthButton;
		browser->brandButton = &brandStub;
		browser->tagButton = &tagStub;
		browser->customTagButton = &customTagStub;
		browser->widthButton = &widthStub;
	}

	~LayoutFixture() {
		// The scene outlives the fixture: a dropdown left in it would put every later
		// TEST_CASE's search field into "dropdown open" mode.
		for (auto* o : overlays) {
			APP->scene->removeChild(o);
			delete o;
		}
		browser->brandButton = savedBrand;
		browser->tagButton = savedTag;
		browser->customTagButton = savedCustomTag;
		browser->widthButton = savedWidth;
	}

	// A dropdown already open, as a shortcut press would have left it.
	DropdownChoiceContainer* openDropdownFor(rack::widget::Widget* opener) {
		auto* overlay = new rack::ui::MenuOverlay;
		APP->scene->addChild(overlay);
		overlays.push_back(overlay);
		auto* container = new DropdownChoiceContainer;
		container->opener = opener;
		overlay->addChild(container);
		return container;
	}
};

TEST_CASE("MB keymap: layout dropdown shortcuts", "[Mb][Widget][Keymap]") {
	LayoutFixture fx;

	SECTION("Ctrl+1/2/3/4 open the Brand/Tag/Custom Tag/Width dropdown") {
		REQUIRE(fx.press(GLFW_KEY_1, RACK_MOD_CTRL));
		REQUIRE(fx.brandStub.opened == 1);
		REQUIRE(fx.press(GLFW_KEY_2, RACK_MOD_CTRL));
		REQUIRE(fx.tagStub.opened == 1);
		REQUIRE(fx.press(GLFW_KEY_3, RACK_MOD_CTRL));
		REQUIRE(fx.customTagStub.opened == 1);
		REQUIRE(fx.press(GLFW_KEY_4, RACK_MOD_CTRL));
		REQUIRE(fx.widthStub.opened == 1);
		REQUIRE(fx.brandStub.opened == 1);
	}

	SECTION("The open dropdown's own shortcut closes it without reopening") {
		auto* open = fx.openDropdownFor(&fx.brandStub);

		REQUIRE(fx.press(GLFW_KEY_1, RACK_MOD_CTRL));
		REQUIRE(open->parent->requestedDelete);
		REQUIRE(fx.brandStub.opened == 0);
	}

	SECTION("Another dropdown's shortcut closes the open one and opens that one") {
		auto* open = fx.openDropdownFor(&fx.brandStub);

		REQUIRE(fx.press(GLFW_KEY_2, RACK_MOD_CTRL));
		REQUIRE(open->parent->requestedDelete);
		REQUIRE(fx.tagStub.opened == 1);
		REQUIRE(fx.brandStub.opened == 0);
	}

	SECTION("The Width shortcut closes an open Width dropdown without reopening it") {
		auto* open = fx.openDropdownFor(&fx.widthStub);
		REQUIRE(fx.press(GLFW_KEY_4, RACK_MOD_CTRL));
		REQUIRE(open->parent->requestedDelete);
		REQUIRE(fx.widthStub.opened == 0);
	}

	SECTION("The Width shortcut switches from another open dropdown") {
		auto* open = fx.openDropdownFor(&fx.brandStub);
		REQUIRE(fx.press(GLFW_KEY_4, RACK_MOD_CTRL));
		REQUIRE(open->parent->requestedDelete);
		REQUIRE(fx.widthStub.opened == 1);
	}

	SECTION("Browse shortcuts are suspended while a dropdown is open") {
		fx.openDropdownFor(&fx.brandStub);

		fx.press(GLFW_KEY_SPACE, GLFW_MOD_SHIFT);
		REQUIRE_FALSE(fx.browser->hidden);
		fx.press(GLFW_KEY_ESCAPE);
		REQUIRE(fx.overlay->visible);
	}

	SECTION("A rebound shortcut opens the dropdown, the old one no longer does") {
		fx.km->bind("browser.v2.layout.brand", KeyCombo("Ctrl+B"));

		REQUIRE_FALSE(fx.press(GLFW_KEY_1, RACK_MOD_CTRL));
		REQUIRE(fx.brandStub.opened == 0);

		REQUIRE(fx.press(GLFW_KEY_B, RACK_MOD_CTRL));
		REQUIRE(fx.brandStub.opened == 1);
	}

	SECTION("A rebound shortcut also closes/switches an open dropdown") {
		fx.km->bind("browser.v2.layout.brand", KeyCombo("Ctrl+B"));
		auto* open = fx.openDropdownFor(&fx.brandStub);

		REQUIRE(fx.press(GLFW_KEY_B, RACK_MOD_CTRL));
		REQUIRE(open->parent->requestedDelete);
		REQUIRE(fx.brandStub.opened == 0);
	}
}

TEST_CASE("MB keymap: hovered module shortcuts follow the keymap", "[Mb][Widget][Keymap][ModelBox]") {
	cleanupMockModels();
	KeymapFixture fx;
	fx.h.events().deselect();

	auto* box = fx.h.events().find<v2::ModelBox>(fx.browser);
	REQUIRE(box != nullptr);
	fx.h.events().hover(box);
	rack::math::Vec pos = Test::EventDriver::centerOf(box);

	SECTION("Rebound favorite toggle fires on the new combo only") {
		fx.km->bind("modelbox.favorite.toggle", KeyCombo("Ctrl+G"));

		fx.h.events().keyAt(pos, GLFW_KEY_F, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE_FALSE(isModelFavorite(box->model));

		fx.h.events().keyAt(pos, GLFW_KEY_G, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE(isModelFavorite(box->model));
	}

	SECTION("Rebound hidden toggle fires on the new combo only") {
		fx.km->bind("modelbox.hidden.toggle", KeyCombo("Ctrl+J"));

		fx.h.events().keyAt(pos, GLFW_KEY_H, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE_FALSE(isModelHidden(box->model));

		fx.h.events().keyAt(pos, GLFW_KEY_J, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE(isModelHidden(box->model));
	}

	cleanupMockModels();
}

TEST_CASE("MB keymap: side view focus and release shortcuts", "[Mb][Widget][Keymap]") {
	KeymapFixture fx(true);
	REQUIRE(sideView);
	auto* catcher = static_cast<SideViewGlobalKeyCatcher*>(fx.overlay->globalKeyCatcher);
	REQUIRE(catcher != nullptr);
	fx.h.events().deselect();

	// The catcher is driven directly: a key nothing consumes would otherwise fall through to
	// Rack's Scene::onHoverKey, which dereferences APP->window (null headless).
	auto hoverKey = [&](int key, int mods = 0) {
		rack::widget::EventContext context;
		rack::widget::Widget::HoverKeyEvent e;
		e.context = &context;
		e.key = key;
		e.action = GLFW_PRESS;
		e.mods = mods;
		catcher->onHoverKey(e);
		return context.target != nullptr;
	};

	SECTION("Ctrl+F focuses the search field, Escape releases it") {
		REQUIRE(hoverKey(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE(catcher->active);
		REQUIRE(APP->event->getSelectedWidget() == fx.browser->searchField);

		REQUIRE(hoverKey(GLFW_KEY_ESCAPE));
		REQUIRE_FALSE(catcher->active);
		REQUIRE(APP->event->getSelectedWidget() == nullptr);
	}

	SECTION("Escape does nothing while the search field is not focused") {
		REQUIRE_FALSE(hoverKey(GLFW_KEY_ESCAPE));
	}

	SECTION("Rebound focus shortcut replaces Ctrl+F") {
		fx.km->bind("browser.v2.sideview.focus", KeyCombo("Ctrl+G"));

		REQUIRE_FALSE(hoverKey(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE_FALSE(catcher->active);

		REQUIRE(hoverKey(GLFW_KEY_G, RACK_MOD_CTRL));
		REQUIRE(catcher->active);
	}

	SECTION("Rebound release shortcut replaces Escape") {
		fx.km->bind("browser.v2.sideview.release", KeyCombo("Ctrl+R"));
		hoverKey(GLFW_KEY_F, RACK_MOD_CTRL);
		REQUIRE(catcher->active);

		REQUIRE_FALSE(hoverKey(GLFW_KEY_ESCAPE));
		REQUIRE(catcher->active);

		REQUIRE(hoverKey(GLFW_KEY_R, RACK_MOD_CTRL));
		REQUIRE_FALSE(catcher->active);
	}

	SECTION("With nothing hovered, Ctrl+F still focuses even though the module toggle shares it") {
		// lookup() reports one action per key: the module toggle, registered first.
		REQUIRE(fx.km->lookup(GLFW_KEY_F, RACK_MOD_CTRL, GLFW_PRESS) == "modelbox.favorite.toggle");
		REQUIRE(hoverKey(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE(catcher->active);
	}
}

// The side view's key catcher sits on top of the whole scene and would otherwise see every key
// before the module box under the cursor, swallowing the shared Ctrl+F.
TEST_CASE("MB keymap: in side view a hovered module's shortcut wins over the focus shortcut", "[Mb][Widget][Keymap][ModelBox]") {
	cleanupMockModels();
	KeymapFixture fx(true);
	fx.h.events().deselect();
	auto* catcher = static_cast<SideViewGlobalKeyCatcher*>(fx.overlay->globalKeyCatcher);
	REQUIRE(catcher != nullptr);

	auto* box = fx.h.events().find<v2::ModelBox>(fx.browser);
	REQUIRE(box != nullptr);
	fx.h.events().hover(box);
	rack::math::Vec pos = Test::EventDriver::centerOf(box);

	SECTION("Ctrl+F over a module toggles its favorite and does not focus the search") {
		REQUIRE_FALSE(isModelFavorite(box->model));
		fx.h.events().keyAt(pos, GLFW_KEY_F, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE(isModelFavorite(box->model));
		REQUIRE_FALSE(catcher->active);
	}

	SECTION("The focus shortcut is unavailable over a module, even with the module toggle rebound") {
		fx.km->bind("modelbox.favorite.toggle", KeyCombo("Ctrl+G"));
		fx.h.events().keyAt(pos, GLFW_KEY_F, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE_FALSE(isModelFavorite(box->model));
		REQUIRE_FALSE(catcher->active);
	}

	SECTION("Away from any module Ctrl+F focuses the search") {
		APP->event->setHoveredWidget(nullptr);
		rack::widget::EventContext context;
		rack::widget::Widget::HoverKeyEvent e;
		e.context = &context;
		e.key = GLFW_KEY_F;
		e.action = GLFW_PRESS;
		e.mods = RACK_MOD_CTRL;
		catcher->onHoverKey(e);
		REQUIRE(catcher->active);
	}

	cleanupMockModels();
}

TEST_CASE("MB keymap: favorite and hidden hotkeys target the keyboard-selected module", "[Mb][Widget][Keymap][ModelBox]") {
	cleanupMockModels();
	KeymapFixture fx;
	// Nothing hovered: a toggle can only have come from the selection.
	APP->event->setHoveredWidget(nullptr);

	auto* box = fx.h.events().find<v2::ModelBox>(fx.browser);
	REQUIRE(box != nullptr);

	SECTION("Without a keyboard selection the search field leaves the key alone") {
		REQUIRE(fx.browser->selectedModel == nullptr);
		REQUIRE_FALSE(fx.press(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE_FALSE(isModelFavorite(box->model));
	}

	SECTION("Ctrl+F toggles the selected module's favorite") {
		fx.press(GLFW_KEY_DOWN);
		REQUIRE(fx.browser->selectedModel == box->model);

		REQUIRE(fx.press(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE(isModelFavorite(box->model));
		REQUIRE(fx.press(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE_FALSE(isModelFavorite(box->model));
	}

	SECTION("Ctrl+H hides the selected module and drops the selection") {
		fx.press(GLFW_KEY_DOWN);
		REQUIRE(fx.browser->selectedModel == box->model);

		REQUIRE(fx.press(GLFW_KEY_H, RACK_MOD_CTRL));
		REQUIRE(isModelHidden(box->model));
		REQUIRE_FALSE(box->visible);
		REQUIRE(fx.browser->selectedModel == nullptr);
		hiddenModelsReset();
	}

	SECTION("Unfavoriting the selection under the Favorites filter drops the selection") {
		toggleModelFavorite(box->model);
		fx.press(GLFW_KEY_SPACE);   // Favorites filter on
		REQUIRE(fx.browser->favorite);
		fx.press(GLFW_KEY_DOWN);
		REQUIRE(fx.browser->selectedModel == box->model);

		REQUIRE(fx.press(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE_FALSE(isModelFavorite(box->model));
		REQUIRE(fx.browser->selectedModel == nullptr);
	}

	SECTION("Rebinding follows the keymap") {
		fx.km->bind("modelbox.favorite.toggle", KeyCombo("Ctrl+G"));
		fx.press(GLFW_KEY_DOWN);

		REQUIRE_FALSE(fx.press(GLFW_KEY_F, RACK_MOD_CTRL));
		REQUIRE_FALSE(isModelFavorite(box->model));
		REQUIRE(fx.press(GLFW_KEY_G, RACK_MOD_CTRL));
		REQUIRE(isModelFavorite(box->model));
	}

	SECTION("Moving the mouse clears the keyboard selection") {
		fx.press(GLFW_KEY_DOWN);
		REQUIRE(fx.browser->selectedModel == box->model);

		fx.h.events().hover(rack::math::Vec(30.f, 40.f));
		fx.h.events().hover(rack::math::Vec(60.f, 80.f));
		REQUIRE(fx.browser->selectedModel == nullptr);
	}

	cleanupMockModels();
}
