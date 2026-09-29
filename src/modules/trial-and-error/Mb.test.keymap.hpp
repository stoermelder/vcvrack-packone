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

struct KeymapFixture {
	Test::Harness h;
	MbModule* m;
	MbWidget* mw;
	BrowserOverlay* overlay;
	v2::ModuleBrowser* browser;
	std::shared_ptr<Keymap> km;

	KeymapFixture() {
		Keymaps::resetForTest();
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
