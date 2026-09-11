#pragma once
#include "../../plugin.hpp"
#include <app/ModuleWidget.hpp>
#include <widget/FramebufferWidget.hpp>
#include <widget/TransparentWidget.hpp>
#include <widget/ZoomWidget.hpp>
#include <settings.hpp>
#include "../../pluginsettings.hpp"

namespace StoermelderPackOne {
namespace Mb {

/** Draws the module widget plus its light layer, so previews show lit LEDs. */
struct ModuleWidgetContainer : widget::Widget {
	void draw(const DrawArgs& args) override {
		Widget::draw(args);
		Widget::drawLayer(args, 1);
	}
};


/** The lazily-created preview of a single model, shared by the v1 and v2 browsers.

Owns the widget subtree that renders a module panel into a FramebufferWidget:

    previewWidget (TransparentWidget)
      zoomWidget
        fb (FramebufferWidget)
          mwc (ModuleWidgetContainer)
            moduleWidget

`create()` is idempotent and independent of drawing, which is what lets the browsers
warm previews during idle frames instead of during a scroll. It performs the expensive
work (SVG parse, widget tree construction, framebuffer allocation); the rendering
itself still happens in FramebufferWidget::draw() as before.
*/
struct ModelPreview {
	widget::Widget* previewWidget = NULL;
	widget::ZoomWidget* zoomWidget = NULL;
	widget::FramebufferWidget* fb = NULL;
	ModuleWidgetContainer* mwc = NULL;
	ModuleWidget* moduleWidget = NULL;

	/** Panel width in px, valid only once created. */
	float width = -1.f;

	bool created() const {
		return fb != NULL;
	}

	/** Attaches the (empty) container to the owning widget. Call once from setModel(). */
	void attach(widget::Widget* parent) {
		previewWidget = new widget::TransparentWidget;
		parent->addChild(previewWidget);
	}

	/** Builds the preview subtree. Safe to call repeatedly; only the first call does work.
	Returns true if the preview was created by this call.
	*/
	bool create(plugin::Model* model) {
		if (fb) return false;

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

	/** True once the framebuffer holds a rendered image (not merely constructed). */
	bool rendered() const {
		return fb && fb->getFramebuffer() != NULL && !fb->dirty;
	}

	/** Rasterizes the framebuffer now instead of waiting for the box to be drawn.

	Construction (create()) is only half the cost of a preview: FramebufferWidget does
	the actual rasterization lazily inside its own draw(), and defers it when the frame
	budget is exhausted. Until that happens it has no framebuffer and draw() paints
	nothing, so a preview scrolled into view for the first time shows as a black box
	until some later frame renders it.

	The scale passed here must match the world transform the widget will actually be
	drawn with, or FramebufferWidget::draw() will compare it against nvgCurrentTransform,
	see a mismatch, and immediately mark the framebuffer dirty again — discarding this
	render and falling back to the lazy path. That world scale is the widget's absolute
	zoom times the window's pixel ratio.
	*/
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

	/** Applies a zoom factor and marks the framebuffer for re-render. */
	void setZoom(float zoom) {
		if (!fb) return;
		zoomWidget->setZoom(zoom);
		fb->setDirty();
	}

	int hp() const {
		return (int)std::round(width / RACK_GRID_WIDTH);
	}
};


/** Paces preview preparation across frames with spare time.

Preparing a preview has two halves, and the expensive one is easy to miss:

  1. construction  — widget tree + SVG parse (ModelPreview::create())
  2. rasterization — FramebufferWidget renders the panel to a GPU texture

FramebufferWidget does (2) lazily inside its own draw(), and skips it when the frame
budget is spent — allowing only one such render per frame. Until it happens the widget
has no framebuffer and paints nothing, which is why a preview scrolled into view for
the first time shows as a black box that fills in a frame or two later. Doing (1) early
therefore does not help on its own; this runs both halves ahead of time.

It only changes *when* previews are prepared, never what they render. Opt-in via
pluginSettings.mbPrewarmEnabled.
*/
struct PreviewPrewarmer {
	/** Scroll offset seen last frame, to detect active scrolling. */
	math::Vec lastOffset;
	float lastZoom = -1.f;
	/** Frames since the scroll offset last changed. */
	int stillFrames = 0;

	/** Warming is suppressed while the user is actively scrolling, but only briefly:
	the point is to be ready *before* the next scroll, so waiting long defeats it. */
	static const int STILL_FRAMES_REQUIRED = 2;

	void reset() {
		stillFrames = 0;
	}

	/** Remaining frame budget, or 0 when no budget can be determined.

	frameRateLimit may be 0 (unlimited), which makes getFrameDurationRemaining() return
	infinity — warming against that would prepare the entire library in one frame. Treat
	that as "no budget" and leave preparation to draw(), as before.
	*/
	static double budgetRemaining() {
		if (settings::frameRateLimit <= 0.f) return 0.0;
		return APP->window->getFrameDurationRemaining();
	}

	/** Prepares previews using whatever time is left in the frame.

	`warm` is called per candidate and returns true if it did any work, so boxes that
	are already prepared or filtered out don't consume the budget.

	The sweep walks the container from the front each time rather than resuming from a
	saved position: refresh() reorders the list, so a saved index would point at an
	unrelated model. Skipping a prepared box is a pointer test, cheap enough to rescan.
	*/
	template <typename F>
	void run(const std::list<widget::Widget*>& children, math::Vec offset, float zoom, F warm) {
		if (!pluginSettings.mbPrewarmEnabled) return;

		// Track scrolling, but don't gate the first frames on it: at browser open there
		// is nothing prepared yet and draw() would otherwise win the race every time.
		if (!offset.equals(lastOffset) || zoom != lastZoom) {
			lastOffset = offset;
			lastZoom = zoom;
			stillFrames = 0;
			return;
		}
		if (++stillFrames < STILL_FRAMES_REQUIRED) return;

		// Leave headroom for the browser itself; only spend what's clearly spare.
		const double reserve = 0.004;
		const int maxPerFrame = 2;
		int prepared = 0;

		for (widget::Widget* w : children) {
			if (prepared >= maxPerFrame) return;
			if (budgetRemaining() <= reserve) return;
			if (warm(w)) prepared++;
		}
	}
};

} // namespace Mb
} // namespace StoermelderPackOne
