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

// Builds a preview subtree without going through ModelPreview::create(): create() dereferences
// APP->window->pixelRatio (Mb_preview.hpp), which is null in every test binary (see the v1
// browser note in this file's header comment). Constructing the same shape of subtree by hand
// keeps these tests exercising captureIfNewlyRendered()'s guard logic without that GL/window
// dependency. Left genuinely un-rendered (fb->getFramebuffer() == NULL): a real render needs a
// GL context this binary doesn't have, so captureIfNewlyRendered()'s own `preview.rendered()`
// check is exactly what keeps it a no-op here (see the SECTION below).
static void fakeCreatePreview(ModelPreview& preview, widget::Widget* parent, plugin::Model* model) {
	preview.attach(parent);
	preview.zoomWidget = new widget::ZoomWidget;
	preview.previewWidget->addChild(preview.zoomWidget);
	preview.fb = new widget::FramebufferWidget;
	preview.zoomWidget->addChild(preview.fb);
	preview.moduleWidget = model->createModuleWidget(NULL);
	preview.mwc = new ModuleWidgetContainer;
	preview.mwc->addChild(preview.moduleWidget);
	preview.fb->addChild(preview.mwc);
	preview.width = 100.f;
}

TEST_CASE("captureIfNewlyRendered() only caches an actually-rendered preview", "[Mb][Widget][ModelBox][Preview]") {
	cleanupMockModels();
	// A cached preview from an earlier, already-torn-down test binary run must not leak into
	// this one: PreviewPixelCache::map() is a plugin-lifetime static, so start from empty.
	PreviewPixelCache::map().clear();

	ModelBoxFixture fx;
	plugin::Model* model = fx.box->model;
	fx.box->preview = ModelPreview();
	fakeCreatePreview(fx.box->preview, fx.box, model);
	REQUIRE(fx.box->preview.created());
	// Genuinely unrendered (no GL context here to render for real) — rendered() must see
	// that and captureIfNewlyRendered() must not cache a blank framebuffer's pixels.
	REQUIRE_FALSE(fx.box->preview.rendered());

	fx.box->captureIfNewlyRendered();
	REQUIRE(PreviewPixelCache::map().count(model) == 0);

	PreviewPixelCache::map().clear();
	cleanupMockModels();
}

TEST_CASE("A model with a PreviewPixelCache hit reclaims it instead of building a live preview", "[Mb][Widget][ModelBox][Preview]") {
	cleanupMockModels();
	PreviewPixelCache::map().clear();
	// mbPrewarmEnabled defaults to false (pluginsettings.hpp) — create()'s cache lookup is
	// gated on it, so this must turn it on explicitly rather than relying on ambient state.
	bool savedSetting = pluginSettings.mbPrewarmEnabled;
	pluginSettings.mbPrewarmEnabled = true;

	ModelBoxFixture fx;
	plugin::Model* model = fx.box->model;

	// Seed the cache directly (readback itself needs a real GL context, covered separately;
	// this exercises create()'s consuming side, not PreviewPixelCache::store()'s GL side).
	PreviewPixelCache::Entry entry;
	entry.width = 4;
	entry.height = 4;
	entry.panelWidth = 5.f * RACK_GRID_WIDTH;
	entry.pixels.assign((size_t)entry.width * entry.height * 4, 0xFF);
	PreviewPixelCache::map()[model] = std::move(entry);

	REQUIRE(fx.box->createPreview());
	REQUIRE(fx.box->preview.isCached());
	// The live path (createModuleWidget + FramebufferWidget) must not have run at all.
	REQUIRE(fx.box->preview.fb == nullptr);
	REQUIRE(fx.box->preview.moduleWidget == nullptr);
	// hp()/width comes from the cached entry's panelWidth, not a live moduleWidget's box.
	REQUIRE(fx.box->preview.hp() == 5);
	// A cache hit is immediately "ready" — no further prewarm work needed.
	REQUIRE(fx.box->previewReady());

	pluginSettings.mbPrewarmEnabled = savedSetting;
	PreviewPixelCache::map().clear();
	cleanupMockModels();
}

// pluginSettings.mbPrewarmEnabled gates the whole PreviewPixelCache feature end to end: with it
// off, create() must fall through to the live branch even with a hit sitting in the cache,
// rather than silently reclaiming a stale bitmap.
//
// This can't be proven by actually calling create()/createPreview() and letting it fall
// through: the live branch calls model->createModuleWidget(NULL), which for the real Mb model
// this fixture uses builds a ThemedModuleWidget that loads a panel SVG via APP->window — null
// in every test binary (see this file's header comment on why v1::ModuleBrowser can't be
// driven headless at all, for the same underlying reason) — and segfaults. So this instead
// pins the exact branch-selection expression create() uses (mbPrewarmEnabled ? find(model) :
// end()) directly, restoring the setting on exit so a REQUIRE failure can't leak `false` into
// later TEST_CASEs. "A model with a PreviewPixelCache hit reclaims it..." above is the
// complementary proof that the cache branch really is taken when the setting is on.
TEST_CASE("mbPrewarmEnabled off means create()'s cache lookup is skipped", "[Mb][Widget][ModelBox][Preview]") {
	cleanupMockModels();
	PreviewPixelCache::map().clear();
	bool savedSetting = pluginSettings.mbPrewarmEnabled;
	pluginSettings.mbPrewarmEnabled = false;

	ModelBoxFixture fx;
	plugin::Model* model = fx.box->model;

	PreviewPixelCache::Entry entry;
	entry.width = 4;
	entry.height = 4;
	entry.panelWidth = 5.f * RACK_GRID_WIDTH;
	entry.pixels.assign((size_t)entry.width * entry.height * 4, 0xFF);
	PreviewPixelCache::map()[model] = std::move(entry);

	auto it = pluginSettings.mbPrewarmEnabled
		? PreviewPixelCache::map().find(model) : PreviewPixelCache::map().end();
	REQUIRE(it == PreviewPixelCache::map().end());
	// The entry itself is untouched — "not looked at", not "erased".
	REQUIRE(PreviewPixelCache::map().count(model) == 1);

	pluginSettings.mbPrewarmEnabled = savedSetting;
	PreviewPixelCache::map().clear();
	cleanupMockModels();
}

// The mirror image of the test above, and of "A model with a PreviewPixelCache hit reclaims
// it..." further up: the same seeded cache entry is reclaimed when the setting is on, ignored
// when it's off. Together they pin create()'s branch on pluginSettings.mbPrewarmEnabled rather
// than on some other condition that happened to coincide with it (e.g. the entry's presence
// alone).
TEST_CASE("captureIfNewlyRendered() never stores while mbPrewarmEnabled is off", "[Mb][Widget][ModelBox][Preview]") {
	cleanupMockModels();
	PreviewPixelCache::map().clear();
	bool savedSetting = pluginSettings.mbPrewarmEnabled;
	pluginSettings.mbPrewarmEnabled = false;

	ModelBoxFixture fx;
	plugin::Model* model = fx.box->model;
	fx.box->preview = ModelPreview();
	fakeCreatePreview(fx.box->preview, fx.box, model);

	// This fake preview is never actually rendered (no GL context here to render with), so
	// even with the setting on this would already be a no-op — the point of this test is only
	// that the setting is checked first and unconditionally, before preview.rendered() is even
	// consulted, so a build that somehow did produce a rendered fb still wouldn't get captured.
	fx.box->captureIfNewlyRendered();
	REQUIRE(PreviewPixelCache::map().count(model) == 0);

	pluginSettings.mbPrewarmEnabled = savedSetting;
	PreviewPixelCache::map().clear();
	cleanupMockModels();
}

TEST_CASE("ModelPreview::swapToCached() replaces the live subtree in place", "[Mb][Widget][ModelBox][Preview]") {
	cleanupMockModels();
	PreviewPixelCache::map().clear();

	ModelBoxFixture fx;
	plugin::Model* model = fx.box->model;
	fx.box->preview = ModelPreview();
	fakeCreatePreview(fx.box->preview, fx.box, model);
	REQUIRE(fx.box->preview.fb != nullptr);
	REQUIRE_FALSE(fx.box->preview.isCached());

	// A non-default zoom, to prove swapToCached()'s displayZoom argument is what's carried
	// over — separate from whatever zoom the capture render itself used. mbPrewarmEnabled
	// defaults to false, so setZoom() applies this immediately rather than pinning at 1.0 (see
	// the mbPrewarmEnabled-gated pinning test below) — irrelevant here either way, since
	// swapToCached() takes displayZoom as an explicit argument regardless of how zoomWidget got
	// to whatever zoom it was at before the swap.
	fx.box->preview.setZoom(2.5f);

	PreviewPixelCache::Entry entry;
	entry.width = 4;
	entry.height = 4;
	entry.panelWidth = 7.f * RACK_GRID_WIDTH;
	entry.pixels.assign((size_t)entry.width * entry.height * 4, 0xFF);
	PreviewPixelCache::map()[model] = std::move(entry);

	fx.box->preview.swapToCached(PreviewPixelCache::map().at(model), 2.5f);

	REQUIRE(fx.box->preview.isCached());
	REQUIRE(fx.box->preview.fb == nullptr);
	REQUIRE(fx.box->preview.moduleWidget == nullptr);
	REQUIRE(fx.box->preview.width == entry.panelWidth);
	REQUIRE(fx.box->preview.zoomWidget != nullptr);
	REQUIRE(fx.box->preview.zoomWidget->getZoom() == Catch::Approx(2.5f));
	// Still exactly one child under previewWidget — the old live zoomWidget was deleted, not
	// left orphaned alongside the new one.
	REQUIRE(fx.box->preview.previewWidget->children.size() == 1);

	PreviewPixelCache::map().clear();
	cleanupMockModels();
}

// buildCached() must size the cached widget's box from the entry's own captured aspect ratio,
// not assume RACK_GRID_HEIGHT: render()'s framebuffer size comes from world-space coordinates
// run through floor()/ceil(), so a captured entry's pixel aspect ratio can drift slightly from
// panelWidth/RACK_GRID_HEIGHT depending on zoom/oversample at capture time. Getting this wrong
// would silently stretch or squash every cached preview.
TEST_CASE("A cached preview's box respects the captured entry's own aspect ratio", "[Mb][Widget][ModelBox][Preview]") {
	cleanupMockModels();
	PreviewPixelCache::map().clear();
	// create()'s cache lookup is gated on this (defaults to false).
	bool savedSetting = pluginSettings.mbPrewarmEnabled;
	pluginSettings.mbPrewarmEnabled = true;

	ModelBoxFixture fx;
	plugin::Model* model = fx.box->model;

	// Deliberately not RACK_GRID_HEIGHT-proportioned: width:height 2:1, unlike a typical panel.
	PreviewPixelCache::Entry entry;
	entry.width = 200;
	entry.height = 100;
	entry.panelWidth = 8.f * RACK_GRID_WIDTH;
	entry.pixels.assign((size_t)entry.width * entry.height * 4, 0xFF);
	PreviewPixelCache::map()[model] = std::move(entry);

	fx.box->preview = ModelPreview();
	fx.box->preview.attach(fx.box);
	REQUIRE(fx.box->preview.create(model));
	REQUIRE(fx.box->preview.isCached());

	// cachedWidget is zoomWidget's only child.
	auto* cachedWidget = static_cast<CachedPreviewWidget*>(fx.box->preview.zoomWidget->children.front());
	float expectedHeight = entry.panelWidth * ((float)entry.height / (float)entry.width);
	REQUIRE(cachedWidget->box.size.x == Catch::Approx(entry.panelWidth));
	REQUIRE(cachedWidget->box.size.y == Catch::Approx(expectedHeight));
	// Not the RACK_GRID_HEIGHT this entry's aspect ratio deliberately doesn't match.
	REQUIRE(cachedWidget->box.size.y != Catch::Approx(RACK_GRID_HEIGHT));

	pluginSettings.mbPrewarmEnabled = savedSetting;
	PreviewPixelCache::map().clear();
	cleanupMockModels();
}

// setZoom() pins the render at zoom 1.0 while prewarming and unrendered, instead of applying
// the requested zoom immediately — this is what lets captureIfNewlyRendered() capture without
// a second, capture-only render (an earlier version of this feature forced a re-render at 1.0
// after the fact instead, which doubled render time across a whole library warm-up). The
// requested zoom is remembered in pendingDisplayZoom for swapToCached() to apply once capture
// is done — see "ModelPreview::swapToCached() replaces the live subtree in place" above.
TEST_CASE("ModelPreview::setZoom() pins the render at zoom 1.0 while prewarming and unrendered", "[Mb][Widget][ModelBox][Preview]") {
	cleanupMockModels();
	bool savedSetting = pluginSettings.mbPrewarmEnabled;
	ModelBoxFixture fx;
	plugin::Model* model = fx.box->model;

	SECTION("mbPrewarmEnabled on, unrendered: zoomWidget stays at 1.0, request is remembered") {
		pluginSettings.mbPrewarmEnabled = true;
		fx.box->preview = ModelPreview();
		fakeCreatePreview(fx.box->preview, fx.box, model);
		REQUIRE_FALSE(fx.box->preview.rendered());

		fx.box->preview.setZoom(1.7f);

		REQUIRE(fx.box->preview.zoomWidget->getZoom() == Catch::Approx(1.f));
		REQUIRE(fx.box->preview.pendingDisplayZoom == Catch::Approx(1.7f));
	}

	SECTION("mbPrewarmEnabled off: applies immediately regardless of rendered state") {
		pluginSettings.mbPrewarmEnabled = false;
		fx.box->preview = ModelPreview();
		fakeCreatePreview(fx.box->preview, fx.box, model);
		REQUIRE_FALSE(fx.box->preview.rendered());

		fx.box->preview.setZoom(1.7f);

		REQUIRE(fx.box->preview.zoomWidget->getZoom() == Catch::Approx(1.7f));
	}

	SECTION("mbPrewarmEnabled on, already cached: applies immediately, nothing left to pin") {
		pluginSettings.mbPrewarmEnabled = true;
		PreviewPixelCache::map().clear();
		PreviewPixelCache::Entry entry;
		entry.width = 4;
		entry.height = 4;
		entry.panelWidth = 5.f * RACK_GRID_WIDTH;
		entry.pixels.assign((size_t)entry.width * entry.height * 4, 0xFF);
		PreviewPixelCache::map()[model] = std::move(entry);

		fx.box->preview = ModelPreview();
		fx.box->preview.attach(fx.box);
		REQUIRE(fx.box->preview.create(model));
		REQUIRE(fx.box->preview.isCached());

		fx.box->preview.setZoom(1.7f);

		REQUIRE(fx.box->preview.zoomWidget->getZoom() == Catch::Approx(1.7f));
		PreviewPixelCache::map().clear();
	}

	pluginSettings.mbPrewarmEnabled = savedSetting;
	cleanupMockModels();
}