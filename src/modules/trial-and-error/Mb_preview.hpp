#pragma once
#include "Mb.hpp"
#include "../../plugin.hpp"
#include "../../vcv/ui.hpp"
#include "../../pluginsettings.hpp"
#include <tag.hpp>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <nanovg_gl_utils.h>

namespace StoermelderPackOne {
namespace Mb {

// Draws the module widget plus its light layer, so previews show lit LEDs.
struct ModuleWidgetContainer : widget::Widget {
	void draw(const DrawArgs& args) override {
		Widget::draw(args);
		Widget::drawLayer(args, 1);
	}
};


// Rendered previews survive the BrowserOverlay that built them (e.g. removing/re-adding the Mb
// module) as plain CPU-side pixels, keyed by model. Not a live widget subtree: FramebufferWidget's
// GL resources aren't refcounted (its destructor frees them unconditionally), so keeping one
// alive past its widget's lifetime is unsafe. Reads back via the public
// getFramebuffer()/getFramebufferSize() API and NVGLUframebuffer's public GL handles, without
// touching FramebufferWidget's private internals.
struct PreviewPixelCache {
	struct Entry {
		int width = 0;
		int height = 0;
		// Panel width in px at zoom 1, oversample 1 — same quantity as ModelPreview::width,
		// kept alongside the pixels so hp()/HP-filtering keep working without a live preview.
		float panelWidth = 0.f;
		// RGBA8, top-down row order, straight nvgCreateImageRGBA input.
		std::vector<uint8_t> pixels;

		// Uploaded GL image, owned by the entry itself rather than by whichever
		// CachedPreviewWidget happens to draw it — every ModelBox reclaiming this model draws
		// the same already-uploaded image instead of re-uploading, and it's only ever deleted
		// once (when the entry itself goes away), not once per ModelBox instance across
		// remove/re-add cycles. 0 until first drawn.
		mutable int nvgImage = 0;

		Entry() = default;
		Entry(const Entry&) = delete;
		Entry& operator=(const Entry&) = delete;
		Entry(Entry&&) = default;
		Entry& operator=(Entry&&) = default;

		~Entry() {
			if (nvgImage && APP && APP->window) {
				nvgDeleteImage(APP->window->vg, nvgImage);
			}
		}

		bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }

		// Returns the uploaded GL image for `vg`, uploading it once on first call.
		int image(NVGcontext* vg) const {
			if (!nvgImage) {
				// PREMULTIPLIED to match nvgluCreateFramebuffer's own flags.
				nvgImage = nvgCreateImageRGBA(vg, width, height, NVG_IMAGE_PREMULTIPLIED, pixels.data());
			}
			return nvgImage;
		}
	};

	static std::unordered_map<plugin::Model*, Entry>& map() {
		static std::unordered_map<plugin::Model*, Entry> m;
		return m;
	}

	// Already cached for this model.
	static bool has(plugin::Model* model) {
		return map().find(model) != map().end();
	}

	// Reads back `fb`'s current contents (must already be rendered) and stores them under
	// `model`, replacing any existing entry. No-op if the framebuffer isn't actually rendered.
	static void store(plugin::Model* model, widget::FramebufferWidget* fb, float panelWidth) {
		NVGLUframebuffer* nfb = fb->getFramebuffer();
		if (!nfb) return;

		math::Vec sizeF = fb->getFramebufferSize();
		int w = (int)sizeF.x;
		int h = (int)sizeF.y;
		if (w <= 0 || h <= 0) return;

		// Preserve whatever FBO NanoVG currently has bound.
		GLint prevFbo = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

		Entry e;
		e.width = w;
		e.height = h;
		e.panelWidth = panelWidth;
		e.pixels.resize((size_t)w * h * 4);

		glBindFramebuffer(GL_FRAMEBUFFER, nfb->fbo);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, e.pixels.data());
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

		// nvgluCreateFramebuffer uses NVG_IMAGE_FLIPY, so the texture is bottom-up relative to
		// nvgCreateImageRGBA's plain top-down input — flip in place, row pairs swapped from
		// both ends, so this never holds a second full-size copy alongside e.pixels.
		size_t rowBytes = (size_t)w * 4;
		for (int y = 0; y < h / 2; y++) {
			std::swap_ranges(&e.pixels[(size_t)y * rowBytes], &e.pixels[(size_t)(y + 1) * rowBytes],
				&e.pixels[(size_t)(h - 1 - y) * rowBytes]);
		}

		map()[model] = std::move(e);
	}
};


// Displays a PreviewPixelCache::Entry as a static image, stretched to the widget's box. Never
// re-renders; zooming just stretches the baked bitmap. The GL image belongs to the entry, not
// to this widget (see PreviewPixelCache::Entry::image()) — many ModelBoxes across
// remove/re-add cycles can share one upload, and none of them delete it on destruction.
struct CachedPreviewWidget : widget::Widget {
	const PreviewPixelCache::Entry* entry = NULL;

	void draw(const DrawArgs& args) override {
		if (!entry || !entry->valid()) return;
		int nvgImage = entry->image(args.vg);
		if (!nvgImage) return;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		NVGpaint paint = nvgImagePattern(args.vg, 0, 0, box.size.x, box.size.y, 0.f, nvgImage, 1.f);
		nvgFillPaint(args.vg, paint);
		nvgFill(args.vg);
		Widget::draw(args);
	}
};


// The lazily-created preview of a single model, shared by the v1 and v2 browsers:
// previewWidget -> zoomWidget -> fb (FramebufferWidget) -> mwc (ModuleWidgetContainer) ->
// moduleWidget. create() is idempotent and separate from rendering, which is what lets the
// browsers warm previews during idle frames instead of during a scroll.
struct ModelPreview {
	widget::Widget* previewWidget = NULL;
	widget::ZoomWidget* zoomWidget = NULL;
	widget::FramebufferWidget* fb = NULL;
	ModuleWidgetContainer* mwc = NULL;
	ModuleWidget* moduleWidget = NULL;

	// Set instead of the above on a PreviewPixelCache hit: a static bitmap stands in for the
	// live subtree, so createModuleWidget() and the render are both skipped.
	CachedPreviewWidget* cachedWidget = NULL;

	// Panel width in px, valid only once created.
	float width = -1.f;

	bool created() const {
		return fb != NULL || cachedWidget != NULL;
	}

	// True while showing a cache hit rather than a live subtree.
	bool isCached() const {
		return cachedWidget != NULL;
	}

	// Attaches the (empty) container to the owning widget. Call once from setModel().
	void attach(widget::Widget* parent) {
		previewWidget = new widget::TransparentWidget;
		parent->addChild(previewWidget);
	}

	// Builds the cached-image half of the subtree (zoomWidget -> cachedWidget) at `zoom`.
	// Shared by create()'s cache-hit branch and swapToCached().
	void buildCached(const PreviewPixelCache::Entry& entry, float zoom) {
		// Same ZoomWidget wrapper as the live path, so setZoom() works identically either way.
		zoomWidget = new widget::ZoomWidget;
		zoomWidget->setZoom(zoom);
		previewWidget->addChild(zoomWidget);

		// Height derived from the entry's own captured aspect ratio rather than assumed to be
		// exactly RACK_GRID_HEIGHT: render()'s framebuffer size comes from world-space
		// coordinates run through floor()/ceil(), so the captured pixel aspect ratio can drift
		// slightly from panelWidth/RACK_GRID_HEIGHT depending on zoom/oversample at capture
		// time. Using the real ratio avoids a visible stretch/squash.
		float aspect = (entry.width > 0) ? (float)entry.height / (float)entry.width : 1.f;
		cachedWidget = new CachedPreviewWidget;
		cachedWidget->entry = &entry;
		cachedWidget->box.size = math::Vec(entry.panelWidth, entry.panelWidth * aspect);
		zoomWidget->addChild(cachedWidget);
		width = entry.panelWidth;
	}

	// Builds the preview subtree. Safe to call repeatedly; only the first call does work.
	// Returns true if the preview was created by this call.
	//
	// A PreviewPixelCache hit short-circuits into a static image instead — but only while
	// prewarming is enabled. With it off, a stale entry is simply never looked at (not
	// cleared), and a live preview is built as if nothing had ever been cached.
	bool create(plugin::Model* model) {
		if (fb || cachedWidget) return false;

		auto it = pluginSettings.mbPrewarmEnabled
			? PreviewPixelCache::map().find(model) : PreviewPixelCache::map().end();
		if (it != PreviewPixelCache::map().end()) {
			buildCached(it->second, 1.f);
			return true;
		}

		zoomWidget = new widget::ZoomWidget;
		previewWidget->addChild(zoomWidget);

		fb = new widget::FramebufferWidget;
		if (math::isNear(APP->window->pixelRatio, 1.0)) {
			// Small details draw poorly at low DPI, so oversample when drawing to the framebuffer
			fb->oversample = 2.0;
		}
		zoomWidget->addChild(fb);

		moduleWidget = model->createModuleWidget(NULL);
		mwc = new ModuleWidgetContainer;
		mwc->addChild(moduleWidget);
		mwc->box.size = moduleWidget->box.size;
		fb->addChild(mwc);
		// Save the width, used for correct width of blank before rendered
		width = moduleWidget->box.size.x;

		// Widgets such as lights only compute their initial visible state (color, layout, nested dirty
		// framebuffers) inside step(). Without running it once here, the framebuffer bakes its first snapshot
		// from an unstepped, just-constructed tree.
		moduleWidget->step();
		return true;
	}

	// True once the framebuffer holds a rendered image (not merely constructed), or a cached
	// static image is showing — either way, there is nothing left to prepare.
	bool rendered() const {
		if (cachedWidget) return true;
		return fb && fb->getFramebuffer() != NULL && !fb->dirty;
	}

	// The NanoVG image the magnifier should sample, whichever mode this preview is in — a live
	// rendered framebuffer's own image, or the cached bitmap's uploaded one. -1 if neither is
	// ready yet (unwarmed box).
	int magnifierImage(NVGcontext* vg) const {
		if (cachedWidget && cachedWidget->entry && cachedWidget->entry->valid()) {
			return cachedWidget->entry->image(vg);
		}
		if (fb) {
			NVGLUframebuffer* nfb = fb->getFramebuffer();
			if (nfb) return nfb->image;
		}
		return -1;
	}

	// Rasterizes the framebuffer now instead of waiting for FramebufferWidget::draw() to do
	// it lazily, so a preview scrolled into view isn't a black box for a frame or two. The scale
	// passed must match the world transform the widget draws with (absolute zoom * pixel ratio),
	// or draw() sees a mismatch against nvgCurrentTransform and immediately re-dirties it.
	bool render() {
		if (!fb || rendered()) return false;

		// render() sizes the framebuffer from getVisibleChildrenBoundingBox(), and
		// creates nothing at all when that comes out empty — while still clearing
		// `dirty`. A preview left in that state draws as a black box forever, because
		// nothing ever marks it dirty again. Make sure the subtree has a real size
		// before rendering, and report failure so the caller can retry later.
		if (mwc->box.size.isZero()) {
			mwc->box.size = moduleWidget->box.size;
		}
		if (mwc->box.size.isZero() || !fb->isVisible()) return false;

		float s = fb->getAbsoluteZoom() * APP->window->pixelRatio;
		fb->render(math::Vec(s, s));

		// If nothing was actually allocated, the render did not take: leave it dirty so
		// the normal lazy path still has a chance rather than baking a permanent blank.
		if (fb->getFramebuffer() == NULL) {
			fb->setDirty();
			return false;
		}
		return true;
	}

	// Last requested zoom, remembered while setZoom() is deferring it (see below) so
	// swapToCached() can apply it once capture is done.
	float pendingDisplayZoom = 1.f;

	// Applies a zoom factor and, in live mode, marks the framebuffer for re-render. In cached
	// mode there is no framebuffer to re-render — the baked image is simply drawn at the new
	// scale by zoomWidget's transform, same as the live path.
	//
	// While prewarming and not yet rendered, the zoom is deliberately not applied to the render:
	// the first render always happens at zoom 1.0 (see create()), so a rendered-then-captured
	// preview never needs a second render at a different scale, which used to double render
	// time across a whole library warm-up. The requested zoom is only remembered
	// (pendingDisplayZoom); swapToCached() applies it once capture is done. Once actually
	// rendered (including a reclaimed cache hit), zoom changes apply immediately as normal.
	void setZoom(float zoom) {
		if (!zoomWidget) return;
		pendingDisplayZoom = zoom;
		if (pluginSettings.mbPrewarmEnabled && fb && !rendered()) return;
		zoomWidget->setZoom(zoom);
		if (fb) fb->setDirty();
	}

	int hp() const {
		return (int)std::round(width / RACK_GRID_WIDTH);
	}

	// Replaces a live, just-rendered subtree with the cached bitmap for `entry`, right after
	// that render populated the cache (see ModelBoxBase::captureIfNewlyRendered()). `displayZoom`
	// (typically pendingDisplayZoom) is the zoom to display at, since the capture itself always
	// rendered at a fixed 1.0. No-op if already cached or never went live.
	void swapToCached(const PreviewPixelCache::Entry& entry, float displayZoom) {
		if (!fb || cachedWidget) return;
		// zoomWidget owns fb/mwc/moduleWidget, so deleting it tears down the whole live subtree
		// at once; removeChild() first satisfies Widget::~Widget()'s assert(!parent).
		previewWidget->removeChild(zoomWidget);
		delete zoomWidget;
		zoomWidget = NULL;
		fb = NULL;
		mwc = NULL;
		moduleWidget = NULL;

		buildCached(entry, displayZoom);
	}
};


// Outcome of ModelBoxBase::preparePreview(), so PreviewPrewarmer::run() can budget a near-free
// PreviewPixelCache reclaim differently from an expensive live createModuleWidget()+render.
enum class PrepareResult {
	NONE,     // already ready, or nothing to do yet
	CACHED,   // reclaimed a PreviewPixelCache hit — no createModuleWidget(), no render
	LIVE,     // built and/or rendered the real ModuleWidget/FramebufferWidget subtree
};


// Paces preview preparation (construction + rasterization) across frames with spare
// time, so previews are ready before they scroll into view instead of showing as a black
// box for a frame or two. Only changes *when* previews are prepared, never what they
// render. Opt-in via pluginSettings.mbPrewarmEnabled.
struct PreviewPrewarmer {
	// Scroll offset seen last frame, to detect active scrolling.
	math::Vec lastOffset;
	float lastZoom = -1.f;
	// Frames since the scroll offset last changed.
	int stillFrames = 0;

	// Progress of the last completed sweep: how many candidates were already prepared,
	// out of how many were eligible. Both are 0 before the first sweep runs.
	int readyCount = 0;
	int totalCount = 0;

	// True once a full sweep found nothing left to prepare.
	bool complete() const {
		return totalCount > 0 && readyCount >= totalCount;
	}

	// Fraction prepared, 0..1. Returns 1 when there is nothing to do.
	float progress() const {
		if (totalCount <= 0) return 1.f;
		return math::clamp((float)readyCount / (float)totalCount, 0.f, 1.f);
	}

	// Warming is suppressed while the user is actively scrolling, but only briefly:
	// the point is to be ready *before* the next scroll, so waiting long defeats it.
	static const int STILL_FRAMES_REQUIRED = 1;

	void reset() {
		stillFrames = 0;
		readyCount = totalCount = 0;
	}

	// Remaining frame budget, or 0 when frameRateLimit is unlimited (0) — warming against
	// an infinite budget would prepare the whole library in one frame.
	static double budgetRemaining() {
		if (settings::frameRateLimit <= 0.f) return 0.0;
		return APP->window->getFrameDurationRemaining();
	}

	// Prepares previews using whatever time is left in the frame. `ready` reports whether a
	// candidate is already prepared; `warm` does the work and returns a PrepareResult.
	//
	// Every model is warmed, not just ones matching the current filter — filters are transient,
	// and re-warming from scratch after every search/tag change would defeat the point. The
	// sweep always walks the whole list from the front (refresh() reorders it, so a saved
	// position would be meaningless) so the progress tally stays complete even once the frame
	// budget runs out and the remainder only counts instead of preparing.
	//
	// CACHED reclaims are cheap enough (no module widget built, no render) to be paced by the
	// time budget alone, never maxPerFrame — so re-adding the Mb module after warming once can
	// reclaim its whole library in ~1 frame. LIVE work is normally paced by time alone too;
	// maxPerFrame only kicks in for it when frameRateLimit is unlimited, see below.
	template <typename R, typename F>
	void run(const std::list<widget::Widget*>& children, math::Vec offset, float zoom,
		R ready, F warm) {
		if (!pluginSettings.mbPrewarmEnabled) {
			readyCount = totalCount = 0;
			return;
		}

		// Track scrolling, but don't gate the first frames on it: at browser open there
		// is nothing prepared yet and draw() would otherwise win the race every time.
		if (!offset.equals(lastOffset) || zoom != lastZoom) {
			lastOffset = offset;
			lastZoom = zoom;
			stillFrames = 0;
			return;
		}
		if (++stillFrames < STILL_FRAMES_REQUIRED) return;

		// `reserve` is deliberately negative (half a frame): on a slow machine draw() already
		// overruns the frame before warming is reached, so requiring leftover time meant
		// preparing ~1 preview per sweep. Expressed as a fraction of the frame so the
		// overshoot stays proportional across refresh rates.
		const double frameDuration = 1.0 / settings::frameRateLimit;
		const double reserve = -0.5 * frameDuration;

		// budgetRemaining() is the real throttle for LIVE work; maxPerFrame only matters when
		// frameRateLimit is unlimited, where budgetRemaining() always reports 0.0 (the time
		// check above never trips) — without a count cap there, the sweep would build every
		// remaining ModuleWidget in a single frame. With a real frame rate limit, the time
		// budget alone already stops the loop first, so the cap is a no-op then.
		const bool unlimitedFrameRate = settings::frameRateLimit <= 0.f;
		const int maxPerFrame = 8;
		int prepared = 0;
		int nReady = 0, nTotal = 0;
		bool budgetLeft = true;

		for (widget::Widget* w : children) {
			nTotal++;
			if (ready(w)) {
				nReady++;
				continue;
			}
			if (!budgetLeft) continue;
			if (budgetRemaining() <= reserve) {
				budgetLeft = false;
				continue;
			}
			PrepareResult result = warm(w);
			if (result == PrepareResult::LIVE) {
				prepared++;
				nReady++;
				if (unlimitedFrameRate && prepared >= maxPerFrame) budgetLeft = false;
			}
			else if (result == PrepareResult::CACHED) {
				nReady++;
			}
		}

		readyCount = nReady;
		totalCount = nTotal;
	}
};

// Vertical band of the model container currently on screen, in container coordinates,
// grown by `margin` so boxes just outside still step normally. Widget::step() has no clip
// test, so boxes outside this band skip their preview subtree — they aren't drawn and
// nothing in a preview animates.
struct ViewportBand {
	float top = -INFINITY;
	float bottom = INFINITY;

	// `scrollOffsetY` is the ScrollWidget's offset, `viewHeight` its visible height, and
	// `containerOffsetY` the model container's position within the scrolled content.
	static ViewportBand around(float scrollOffsetY, float viewHeight, float containerOffsetY,
		float margin = 0.f) {
		ViewportBand b;
		b.top = scrollOffsetY - containerOffsetY - margin;
		b.bottom = scrollOffsetY - containerOffsetY + viewHeight + margin;
		return b;
	}

	bool contains(const math::Rect& box) const {
		return box.pos.y + box.size.y >= top && box.pos.y <= bottom;
	}
};


// Thin progress bar showing how much of the browser's preview set is prepared. Visible only
// while pre-warming is enabled and still has work to do.
struct PrewarmProgressWidget : widget::Widget {
	PreviewPrewarmer* prewarmer = NULL;

	void step() override {
		// Hide as soon as there is nothing left to report.
		visible = prewarmer && pluginSettings.mbPrewarmEnabled && !prewarmer->complete()
			&& prewarmer->totalCount > 0;
		widget::Widget::step();
	}

	void draw(const DrawArgs& args) override {
		if (!prewarmer) return;
		float p = prewarmer->progress();

		// The widget spans the full row height so SequentialLayout (which top-aligns)
		// places it like its neighbours; the bar itself is centred within that.
		const float h = 5.f;
		float y = std::floor((box.size.y - h) / 2.f);

		// Track
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, y, box.size.x, h, h / 2.f);
		nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0x60));
		nvgFill(args.vg);

		// Filled portion, never narrower than the rounded cap
		if (p > 0.f) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0, y, std::max(h, box.size.x * p), h, h / 2.f);
			nvgFillColor(args.vg, componentlibrary::SCHEME_YELLOW);
			nvgFill(args.vg);
		}

		widget::Widget::draw(args);
	}
};


// Common behaviour of the v1 and v2 ModelBox widgets: preview creation, pre-warm
// preparation, the off-screen step skip, and the tooltip/magnifier lifecycle. The two
// browsers differ only in chrome (zoom source, shadow corner radius, selection outline),
// supplied through the hooks below.
//
// Subclasses must implement updateZoom(), getBrowserBand(), refreshBrowser(), refreshBrowserTags()
// and filterBrowserByBrand(), and may override onPreviewCreated() or tooltipText().
struct ModelBoxBase : widget::OpaqueWidget {
	plugin::Model* model = NULL;
	ui::Tooltip* tooltip = NULL;
	MagnifierOverlay* magnifier = NULL;
	// Lazily created
	ModelPreview preview;
	bool modelHidden = false;

	// Blur/corner radii of the drop shadow; the two browsers use different corners.
	float shadowBlurRadius = 10.f;
	float shadowCornerRadius = 10.f;

	virtual ~ModelBoxBase() { }

	void setModel(plugin::Model* m) {
		model = m;
		preview.attach(this);
		updateZoom();
	}

	// Applies the browser's current zoom to the preview and this box's size.
	virtual void updateZoom() = 0;

	// The owning browser's off-screen band, or NULL when there is no browser.
	virtual const ViewportBand* getBrowserBand() = 0;

	// Re-runs the owning browser's filter and sort, keeping the scroll position. A no-op
	// when there is no browser. `onlyIfFavoriteFilter` limits the refresh to when the
	// favorites filter is on — the one case where toggling favorite changes the list.
	virtual void refreshBrowser(bool onlyIfFavoriteFilter = false) = 0;

	// Refreshes after a custom or predefined tag changed, rebuilding any tag list the
	// browser shows alongside the models.
	virtual void refreshBrowserTags() = 0;

	// Sets the browser's brand filter to this model's brand and refreshes.
	virtual void filterBrowserByBrand() = 0;

	// Adds browser-specific entries to the context menu, just below "Filter by brand".
	// Called with the menu already holding the common header items.
	virtual void appendFilterMenuItems(ui::Menu* menu) { }

	// Hover tooltip contents: brand, name, tags, custom tags and description. Tags come
	// from getEffectiveTagIds() rather than model->tagIds, so custom additions/removals show.
	virtual std::string tooltipText() {
		std::string text = model->plugin->brand + " " + model->name;
		text += "\nTags: ";
		int i = 0;
		for (int tagId : getEffectiveTagIds(model)) {
			if (i++ > 0) text += ", ";
			text += rack::tag::tagAliases[tagId][0];
		}
		std::set<std::string> customTags = customTagsForModel(model);
		if (!customTags.empty()) {
			text += "\nCustom Tags: ";
			i = 0;
			for (const auto& tag : customTags) {
				if (i++ > 0) text += ", ";
				text += tag;
			}
		}
		if (!model->description.empty())
			text += "\n" + model->description;
		return text;
	}

	// Called once, right after the preview subtree first exists.
	virtual void onPreviewCreated() { }

	// Returns true if the preview was created by this call.
	bool createPreview() {
		if (!preview.create(model)) return false;
		onPreviewCreated();
		updateZoom();
		return true;
	}

	// Captures the live framebuffer into PreviewPixelCache the first time it's seen rendered
	// (via the prewarm sweep or a lazy scroll-triggered render), then immediately swaps this
	// box over to the cached bitmap — a captured preview never keeps its live subtree around
	// longer than it takes to capture it. Gated on pluginSettings.mbPrewarmEnabled: with
	// prewarming off there's nothing to capture. PreviewPixelCache::has() makes this run once
	// per model per process lifetime; not done at ModelBox destruction instead because doing it
	// for every model at once (removing the Mb module) once froze the UI for over a second.
	void captureIfNewlyRendered() {
		if (!pluginSettings.mbPrewarmEnabled) return;
		if (preview.fb && !preview.isCached() && preview.rendered() && !PreviewPixelCache::has(model)) {
			// Already rendered at zoom 1.0 (setZoom() pins it there while unrendered), so no
			// second capture-only render is needed. pendingDisplayZoom is the zoom to resume at.
			PreviewPixelCache::store(model, preview.fb, preview.width);
			preview.swapToCached(PreviewPixelCache::map().at(model), preview.pendingDisplayZoom);
		}
	}

	// Creates and rasterizes the preview ahead of it being scrolled into view.
	PrepareResult preparePreview() {
		bool wasCached = preview.isCached();
		bool did = createPreview();
		// render() reports whether it actually produced a framebuffer; a no-op must not
		// consume the frame's warming budget, or boxes it skipped are never retried.
		if (preview.render()) did = true;
		captureIfNewlyRendered();
		if (!did) return PrepareResult::NONE;
		// isCached() only just became true if create() found a hit just now (wasCached catches
		// the reclaim-happened-this-call case; the live path never sets it).
		return (preview.isCached() && !wasCached) ? PrepareResult::CACHED : PrepareResult::LIVE;
	}

	// Reports whether this box needs no further pre-warming.
	bool previewReady() const {
		return preview.rendered();
	}

	void step() override {
		// Filtered-out boxes are parked at the origin by SequentialLayout and never drawn,
		// so there is nothing in their subtree to keep up to date.
		if (!visible) return;

		// Skip the preview subtree while off screen. Widget::step() has no clip test, so
		// otherwise every prepared preview steps its whole ModuleWidget tree every frame.
		// Pre-warming is unaffected: it calls preparePreview() directly, not via step().
		const ViewportBand* band = getBrowserBand();
		if (band && !band->contains(box)) return;

		widget::OpaqueWidget::step();
	}

	// Draws shadow, preview and favorite highlight. Subclasses that add their own
	// decoration call this first, then draw on top.
	void draw(const DrawArgs& args) override {
		// Lazily create preview when drawn
		createPreview();

		// Draw shadow
		nvgBeginPath(args.vg);
		float r = shadowBlurRadius;
		float c = shadowCornerRadius;
		nvgRect(args.vg, -r, -r, box.size.x + 2 * r, box.size.y + 2 * r);
		NVGcolor shadowColor = nvgRGBAf(0, 0, 0, 0.5);
		NVGcolor transparentColor = nvgRGBAf(0, 0, 0, 0);
		nvgFillPaint(args.vg, nvgBoxGradient(args.vg, 0, 0, box.size.x, box.size.y, c, r, shadowColor, transparentColor));
		nvgFill(args.vg);

		// To avoid blinding the user when rack brightness is low, draw framebuffer with the same brightness.
		float b = math::clamp(settings::rackBrightness + 0.2f, 0.f, 1.f);
		if (modelHidden) b *= 0.33f;
		nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 1));

		OpaqueWidget::draw(args);

		// Catches a lazy render (FramebufferWidget's own draw-triggered render, above) that
		// happened before the prewarm sweep got to this box.
		captureIfNewlyRendered();

		if (favoriteHighlight && isModelFavorite(model)) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgStrokeWidth(args.vg, 2);
			nvgStrokeColor(args.vg, componentlibrary::SCHEME_YELLOW);
			nvgStroke(args.vg);
		}
	}

	void setTooltip(ui::Tooltip* tt) {
		if (tooltip) {
			tooltip->requestDelete();
			tooltip = NULL;
		}
		if (tt) {
			APP->scene->addChild(tt);
			tooltip = tt;
		}
	}

	void setMagnifier(MagnifierOverlay* mg) {
		if (magnifier) {
			magnifier->requestDelete();
			magnifier = NULL;
		}
		if (mg) {
			APP->scene->addChild(mg);
			magnifier = mg;
		}
	}

	// Left click adds the model; +Shift keeps the browser open; +Ctrl toggles favorite.
	// Right click opens the context menu.
	void onButton(const event::Button& e) override {
		OpaqueWidget::onButton(e);

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0) {
			chooseModel(model);
			e.consume(this);
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == RACK_MOD_SHIFT) {
			chooseModel(model, false);
			e.consume(this);
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			toggleModelFavorite(model);
			refreshBrowser(true);
			e.consume(this);
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
	}

	// Menu items for the context menu's tag editors. Nested so they can name ModelBoxBase
	// directly: each mutates a model's tags and calls back through `modelBox` to refresh.

	// Toggles one predefined tag on a model.
	struct TogglePredefinedTagItem : ui::MenuItem {
		plugin::Model* model = nullptr;
		ModelBoxBase* modelBox = nullptr;
		int tagId = 0;
		bool hasEffectiveTag = false;
		std::shared_ptr<std::string> filter;

		void onAction(const event::Action& e) override {
			if (hasEffectiveTag) {
				predefinedTagRemove(model, tagId);
			}
			else {
				predefinedTagAdd(model, tagId);
			}
			hasEffectiveTag = !hasEffectiveTag;
			if (modelBox) modelBox->refreshBrowserTags();
			e.unconsume();
		}

		void step() override {
			visible = Rack::menuFilterMatches(filter, text);
			rightText = CHECKMARK(hasEffectiveTag);
			MenuItem::step();
		}
	};

	// Toggles one existing custom tag on a model.
	struct ToggleCustomTagItem : ui::MenuItem {
		plugin::Model* model = nullptr;
		ModelBoxBase* modelBox = nullptr;
		std::string tagName;
		std::shared_ptr<std::string> filter;

		void onAction(const event::Action& e) override {
			if (customTagHas(model, tagName)) {
				customTagRemove(model, tagName);
			}
			else {
				customTagAdd(model, tagName);
			}
			if (modelBox) modelBox->refreshBrowserTags();
			e.unconsume();
		}

		void step() override {
			visible = Rack::menuFilterMatches(filter, text);
			rightText = CHECKMARK(customTagHas(model, tagName));
			MenuItem::step();
		}
	};

	// Creates a new custom tag on enter; the typed text doubles as a live
	// case-insensitive filter over the ToggleCustomTagItems listed below it.
	struct NewCustomTagField : ui::TextField {
		plugin::Model* model = nullptr;
		ModelBoxBase* modelBox = nullptr;
		std::shared_ptr<std::string> filter;

		void onChange(const event::Change& e) override {
			ui::TextField::onChange(e);
			*filter = string::trim(text);
		}

		void onSelectKey(const event::SelectKey& e) override {
			if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
				std::string tag = string::trim(text);
				if (isValidCustomTag(tag)) {
					customTagAdd(model, tag);
					if (modelBox) modelBox->refreshBrowserTags();
				}
				ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
				if (overlay) overlay->requestDelete();
				e.consume(this);
				return;
			}
			if (!e.getTarget()) {
				ui::TextField::onSelectKey(e);
			}
		}
	};

	// Builds the right-click menu: model details, filters, favorite/hidden toggles and
	// the custom/predefined tag editors. Browser-specific filters come from
	// appendFilterMenuItems().
	void createContextMenu() {
		Menu* menu = createMenu();
		menu->addChild(createMenuLabel(model->plugin->name.c_str()));
		menu->addChild(createMenuLabel(model->name.c_str()));
		menu->addChild(createSubmenuItem("Details", "", [this](Menu* menu) {
			model->appendContextMenu(menu, true);
			// Remove the model's own "Favorite" item — this menu has its own below.
			// appendContextMenu() may add nothing at all, so guard the back() call.
			if (!menu->children.empty()) {
				auto f = menu->children.back();
				menu->removeChild(f);
				delete f;
			}
		}));
		menu->addChild(createMenuItem(string::f("Filter by \"%s\"", model->plugin->brand.c_str()), "",
			[this]() { filterBrowserByBrand(); }));

		appendFilterMenuItems(menu);

		menu->addChild(new MenuSeparator);
		menu->addChild(createCheckMenuItem("Favorite", RACK_MOD_CTRL_NAME "+F",
			[this]() { return isModelFavorite(model); },
			[this]() {
				toggleModelFavorite(model);
				refreshBrowser(true);
			}
		));
		menu->addChild(createCheckMenuItem("Hidden", RACK_MOD_CTRL_NAME "+H",
			[this]() { return modelHidden; },
			[this]() {
				toggleModelHidden(model);
				refreshBrowser(false);
			}
		));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Custom Tags"));

		// Shared between the new-tag text field and the tag menu items below:
		// the typed text doubles as a live case-insensitive filter on the
		// existing tags while still creating a new tag on enter.
		auto tagFilter = std::make_shared<std::string>();

		NewCustomTagField* ntf = new NewCustomTagField;
		ntf->box.size.x = 150.f;
		ntf->placeholder = "Filter / new tag...";
		ntf->model = model;
		ntf->modelBox = this;
		ntf->filter = tagFilter;
		menu->addChild(ntf);
		APP->event->setSelectedWidget(ntf);

		auto unsortedTags = customTagsAll();
		std::vector<std::string> tags(unsortedTags.begin(), unsortedTags.end());
		std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
			return string::lowercase(a) < string::lowercase(b);
		});

		plugin::Model* m = model;
		Rack::addGroupedMenuItems<std::string>(menu, tags, [m, this, tagFilter](const std::string& tag) -> ui::MenuItem* {
			ToggleCustomTagItem* item = new ToggleCustomTagItem;
			item->text = tag;
			item->model = m;
			item->modelBox = this;
			item->tagName = tag;
			item->filter = tagFilter;
			return item;
		}, 20, 16, tagFilter);

		// Add section for modifying predefined tags
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Tags"));

		std::set<int> effectiveTagIds = getEffectiveTagIds(model);

		// Build list of all predefined tags with their status
		using MenuItemType = std::pair<std::string, int>;
		std::vector<MenuItemType> allTags;
		for (int id = 0; id < (int)tag::tagAliases.size(); id++) {
			allTags.push_back(std::make_pair(tag::tagAliases[id][0], id));
		}
		std::sort(allTags.begin(), allTags.end(), [](const MenuItemType& a, const MenuItemType& b) {
			return string::lowercase(a.first) < string::lowercase(b.first);
		});

		Rack::addGroupedMenuItems<MenuItemType>(menu, allTags,
			[effectiveTagIds, m, this, tagFilter](const MenuItemType& item) {
				TogglePredefinedTagItem* t = new TogglePredefinedTagItem;
				t->text = item.first;
				t->model = m;
				t->modelBox = this;
				t->tagId = item.second;
				t->hasEffectiveTag = effectiveTagIds.find(item.second) != effectiveTagIds.end();
				t->filter = tagFilter;
				return t;
			},
			24, 16, tagFilter
		);
	}

	// Ctrl+F toggles favorite, Ctrl+H toggles hidden, on the box under the cursor.
	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			switch (e.key) {
				case GLFW_KEY_F: {
					toggleModelFavorite(model);
					refreshBrowser(true);
					e.consume(this);
					break;
				}
				case GLFW_KEY_H: {
					toggleModelHidden(model);
					refreshBrowser();
					e.consume(this);
					break;
				}
			}
		}
		OpaqueWidget::onHoverKey(e);
	}

	void onEnter(const event::Enter& e) override {
		ui::Tooltip* tt = new ui::Tooltip;
		tt->text = tooltipText();
		setTooltip(tt);

		// The magnifier samples a live or cached image alike; an unwarmed box has neither.
		int img = (APP->window) ? preview.magnifierImage(APP->window->vg) : -1;
		if (img >= 0) {
			MagnifierOverlay* mg = new MagnifierOverlay;
			mg->nvgImage = img;
			mg->sourceAbsPos = getAbsoluteOffset(Vec(0, 0));
			mg->sourceSize = box.size;
			mg->mousePos = getAbsoluteOffset(box.size.div(2));
			mg->enabled = pluginSettings.mbMagnifierEnabled;
			setMagnifier(mg);
		}
	}

	void onHover(const event::Hover& e) override {
		if (magnifier) {
			// A mid-hover capture+swap replaces the live framebuffer with a cached image (or
			// vice versa in principle); re-fetch every frame so the magnifier always samples
			// whichever is actually current, rather than a pointer to one that's gone.
			int img = (APP->window) ? preview.magnifierImage(APP->window->vg) : -1;
			if (img < 0) {
				setMagnifier(NULL);
			}
			else {
				magnifier->nvgImage = img;
				magnifier->mousePos = getAbsoluteOffset(e.pos);
				magnifier->initialized = true;
				magnifier->sourceAbsPos = getAbsoluteOffset(Vec(0, 0));
				magnifier->sourceSize = box.size;
				// Keep the on-screen magnification constant regardless of browser zoom: the
				// box is already scaled by the preview's zoom, so divide it back out.
				if (preview.zoomWidget) {
					float z = preview.zoomWidget->getZoom();
					if (z > 0.f) magnifier->magnification = 3.f / z;
				}
			}
		}
		OpaqueWidget::onHover(e);
	}

	void onLeave(const event::Leave& e) override {
		setTooltip(NULL);
		setMagnifier(NULL);
	}

	void onHide(const event::Hide& e) override {
		// Hide tooltip and magnifier
		setTooltip(NULL);
		setMagnifier(NULL);
		OpaqueWidget::onHide(e);
	}
};


// Runs a pre-warm sweep over a container of ModelBoxBase children.
template <typename TModelBox>
static void prewarmModelContainer(PreviewPrewarmer& prewarmer,
	const std::list<widget::Widget*>& children, math::Vec offset, float zoom) {
	static_assert(std::is_base_of<ModelBoxBase, TModelBox>::value,
		"TModelBox must derive from ModelBoxBase");
	prewarmer.run(children, offset, zoom,
		[](widget::Widget* w) { return static_cast<TModelBox*>(w)->previewReady(); },
		[](widget::Widget* w) { return static_cast<TModelBox*>(w)->preparePreview(); });
}

} // namespace Mb
} // namespace StoermelderPackOne
