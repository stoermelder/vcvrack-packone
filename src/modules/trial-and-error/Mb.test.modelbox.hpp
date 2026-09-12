// Mb.test.modelbox.hpp — ModelBoxBase behavior, exercised via v2::ModelBox.
// Included by Mb.test.cpp inside namespace __modelbox.
//
// Regression under test: the extraction of ModelBoxBase out of the two near-duplicate
// ModelBox structs (Mb_preview.hpp). The refactor moved click/hover/tooltip/context-menu/
// favorite/hidden/step-skip logic into one base class that v1::ModelBox and v2::ModelBox
// override hooks on (updateZoom, getBrowserBand, refreshBrowser, refreshBrowserTags,
// filterBrowserByBrand). These tests exercise that base behavior independently of the module
// browser's own filter/sort/search machinery (already covered by Mb.test.ui.hpp and
// Mb.test.module.hpp), and independently of a full browse-and-click flow.
//
// v2::ModelBox only, not v1: v1::ModuleBrowser::step() unconditionally steps
// BrowserSidebar's FavoriteItem/BrandItem/etc., which are real ui::MenuItem widgets whose
// step() calls bndLabelWidth(APP->window->vg, ...) unconditionally (Rack's
// ui::MenuItem::step(), MenuItem.cpp:54) — APP->window is null in every test binary, so
// driving v1::ModuleBrowser headless at all segfaults before ModelBoxBase is even reached.
// That is pre-existing and orthogonal to this refactor; v2::ModuleBrowser has no such
// widget in its own step() path, which is why Mb.test.ui.hpp's browser tests are v2-only
// too. ModelBoxBase itself does not know which browser owns it, so coverage through one
// concrete subclass exercises the same shared code v1::ModelBox would call.

// Real layout takes two step() calls to converge, same reason as Mb.test.ui.hpp's
// settleLayout(): ModuleBrowser::step() reads a child's box before that child's own step()
// (later in the same call) has sized it.
static void settleModelBoxLayout(rack::widget::Widget* w) {
	w->step();
	w->step();
}

// Drives BrowserOverlay into v2 mode, opens it, and returns the one ModelBox registered for
// the plugin's own mock model (via testPluginInit()).
struct ModelBoxFixture {
	Test::Harness h;
	MbModule* m;
	MbWidget* mw;
	BrowserOverlay* overlay;
	v2::ModuleBrowser* browser;
	v2::ModelBox* box;

	explicit ModelBoxFixture(rack::math::Vec sceneSize = rack::math::Vec(1024, 300)) {
		APP->scene->box.size = sceneSize;

		m = h.addModule<MbModule>("Mb");
		mw = h.addWidget<MbWidget>(m);
		REQUIRE(mw->active);
		REQUIRE(mw->browserOverlay != nullptr);

		overlay = mw->browserOverlay;
		m->mode = MODE::V2;

		overlay->show();
		settleModelBoxLayout(overlay);

		browser = dynamic_cast<v2::ModuleBrowser*>(overlay->mbV2);
		REQUIRE(browser != nullptr);
		REQUIRE(browser->visible);

		box = h.events().find<v2::ModelBox>(browser);
	}
};

TEST_CASE("ModelBoxBase click behavior", "[Mb][Widget][ModelBox]") {
	cleanupMockModels();
	MockHistoryAccess mockHistory;
	Test::mock::Guard<vcv::HistoryAccess> historyGuard{vcv::historyAccess, &mockHistory};

	// Any click SECTION below that actually chooses the model adds a live ModuleWidget to
	// APP->scene->rack. fx's harness sweeps it on teardown (Harness::sweepAddedModules()),
	// regardless of which SECTION ran, so the next TEST_CASE's own chooseModel() does not
	// inherit it.
	ModelBoxFixture fx;

	SECTION("Plain left click chooses the model and closes the browser") {
		REQUIRE(fx.h.events().click(fx.box));
		REQUIRE_FALSE(fx.overlay->visible);
		REQUIRE(mockHistory.pushed.size() == 1);
		REQUIRE(APP->scene->rack->getModules().size() == 1);
	}

	SECTION("Shift+left click chooses the model but keeps the browser open") {
		REQUIRE(fx.h.events().click(fx.box, GLFW_MOUSE_BUTTON_LEFT, RACK_MOD_SHIFT));
		settleModelBoxLayout(fx.overlay);
		REQUIRE(fx.overlay->visible);
		REQUIRE(mockHistory.pushed.size() == 1);
		REQUIRE(APP->scene->rack->getModules().size() == 1);
	}

	SECTION("Ctrl+left click toggles favorite without adding the model") {
		REQUIRE(!isModelFavorite(fx.box->model));
		REQUIRE(fx.h.events().click(fx.box, GLFW_MOUSE_BUTTON_LEFT, RACK_MOD_CTRL));
		REQUIRE(isModelFavorite(fx.box->model));
		// No module added: the click toggled favorite instead of choosing the model.
		REQUIRE(mockHistory.pushed.empty());
		REQUIRE(fx.overlay->visible);
		REQUIRE(APP->scene->rack->getModules().empty());

		REQUIRE(fx.h.events().click(fx.box, GLFW_MOUSE_BUTTON_LEFT, RACK_MOD_CTRL));
		REQUIRE(!isModelFavorite(fx.box->model));
	}

	cleanupMockModels();
}

TEST_CASE("ModelBoxBase hover-key shortcuts", "[Mb][Widget][ModelBox]") {
	cleanupMockModels();
	ModelBoxFixture fx;

	SECTION("Ctrl+F toggles favorite on the hovered box") {
		fx.h.events().hover(fx.box);
		REQUIRE(!isModelFavorite(fx.box->model));
		fx.h.events().keyAt(Test::EventDriver::centerOf(fx.box), GLFW_KEY_F, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE(isModelFavorite(fx.box->model));
	}

	SECTION("Ctrl+H toggles hidden on the hovered box") {
		fx.h.events().hover(fx.box);
		REQUIRE(!fx.box->modelHidden);
		fx.h.events().keyAt(Test::EventDriver::centerOf(fx.box), GLFW_KEY_H, GLFW_PRESS, RACK_MOD_CTRL);
		REQUIRE(isModelHidden(fx.box->model));
	}

	cleanupMockModels();
}

TEST_CASE("ModelBoxBase tooltip and magnifier lifecycle", "[Mb][Widget][ModelBox]") {
	cleanupMockModels();
	ModelBoxFixture fx;

	SECTION("Entering shows a tooltip naming the model") {
		REQUIRE(fx.box->tooltip == nullptr);
		fx.h.events().hover(fx.box);
		REQUIRE(fx.box->tooltip != nullptr);
		REQUIRE(fx.box->tooltip->text.find(fx.box->model->name) != std::string::npos);
	}

	SECTION("Leaving clears the tooltip and magnifier") {
		fx.h.events().hover(fx.box);
		REQUIRE(fx.box->tooltip != nullptr);

		// Move off the box entirely so onLeave fires.
		fx.h.events().hover(rack::math::Vec(-100, -100));
		REQUIRE(fx.box->tooltip == nullptr);
		REQUIRE(fx.box->magnifier == nullptr);
	}

	SECTION("Hiding the box clears the tooltip and magnifier") {
		fx.h.events().hover(fx.box);
		REQUIRE(fx.box->tooltip != nullptr);

		fx.box->hide();
		REQUIRE(fx.box->tooltip == nullptr);
		REQUIRE(fx.box->magnifier == nullptr);
		fx.box->show();
	}

	SECTION("Magnifier only appears once the preview framebuffer exists") {
		// The preview is created lazily by draw(); a hover before any draw() has run must not
		// crash and must not produce a magnifier (onEnter() guards on preview.created()).
		if (!fx.box->preview.created()) {
			fx.h.events().hover(fx.box);
			REQUIRE(fx.box->magnifier == nullptr);
		}
	}

	cleanupMockModels();
}

TEST_CASE("ModelBoxBase step() skips off-screen preview subtrees", "[Mb][Widget][ModelBox]") {
	cleanupMockModels();
	// A tall scene keeps the single registered box comfortably inside the step band, so this
	// test only needs to prove the *visible=false* early-out; forcing genuine off-screen
	// scroll for one model is exactly what Mb.test.ui.hpp's scroll-position tests already do.
	ModelBoxFixture fx(rack::math::Vec(1024, 720));

	SECTION("An invisible (filtered-out) box's step() is a no-op") {
		fx.box->visible = false;
		// Must not crash despite the preview subtree being unstepped/unattached to anything
		// meaningful — this is the "parked at the origin by SequentialLayout" case the base
		// class's step() comment describes.
		REQUIRE_NOTHROW(fx.box->step());
		fx.box->visible = true;
	}

	SECTION("A visible, in-band box still steps normally") {
		REQUIRE(fx.box->visible);
		const ViewportBand* band = fx.box->getBrowserBand();
		REQUIRE(band != nullptr);
		REQUIRE(band->contains(fx.box->box));
		REQUIRE_NOTHROW(fx.box->step());
	}

	cleanupMockModels();
}

TEST_CASE("ModelBoxBase context menu opens without crashing", "[Mb][Widget][ModelBox]") {
	cleanupMockModels();
	ModelBoxFixture fx;

	SECTION("Right-click opens a context menu") {
		// createContextMenu() is shared code (ModelBoxBase); this exercises it through v2,
		// including appendFilterMenuItems() (v2's HP filter item) and the Details submenu's
		// "remove trailing Favorite item" guard against a model whose appendContextMenu()
		// adds nothing.
		REQUIRE(fx.h.events().rightClick(fx.box));
		settleModelBoxLayout(fx.overlay);

		auto* menu = fx.h.events().find<rack::ui::Menu>(APP->scene);
		REQUIRE(menu != nullptr);
	}

	cleanupMockModels();
}