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


/** Paces preview creation across idle frames.

Creating a preview is expensive (SVG parse + widget tree + framebuffer allocation) and
normally happens inside draw(), so scrolling into not-yet-created previews stalls exactly
the frames the user is interacting with. This spreads that work over frames where
nothing is happening instead, and yields the moment the user does anything.

It only changes *when* previews are created, never what they render. Opt-in via
pluginSettings.mbPrewarmEnabled.
*/
struct PreviewPrewarmer {
	/** Index into the browser's model container of the next candidate to warm. */
	size_t cursor = 0;
	/** Frames of no interaction required before warming resumes. */
	int idleFrames = 0;
	/** Interaction state watched to detect idleness. */
	math::Vec lastOffset;
	float lastZoom = -1.f;

	static const int IDLE_FRAMES_REQUIRED = 6;

	/** Restart the sweep, e.g. after the visible set or its order changed. */
	void reset() {
		cursor = 0;
		idleFrames = 0;
	}

	/** Remaining frame budget, or 0 when no budget can be determined.

	frameRateLimit may be 0 (unlimited), which makes getFrameDurationRemaining() return
	infinity — warming against that would build the entire library in one frame. Treat
	that as "no budget" and leave warming to draw(), as before.
	*/
	static double budgetRemaining() {
		if (settings::frameRateLimit <= 0.f) return 0.0;
		return APP->window->getFrameDurationRemaining();
	}

	/** Creates previews during idle frames so that scrolling doesn't have to.

	`offset` is the browser's current scroll offset and `zoom` its current zoom; any
	change to either means the user is busy, so warming backs off and lets draw() do
	the work as it always has.

	`warm` is called per candidate and returns true if it actually created a preview,
	so already-created or filtered-out boxes don't consume the budget.
	*/
	template <typename F>
	void run(const std::list<widget::Widget*>& children, math::Vec offset, float zoom, F warm) {
		if (!pluginSettings.mbPrewarmEnabled) return;

		if (!offset.equals(lastOffset) || zoom != lastZoom) {
			lastOffset = offset;
			lastZoom = zoom;
			idleFrames = 0;
			return;
		}
		if (++idleFrames < IDLE_FRAMES_REQUIRED) return;
		if (cursor >= children.size()) return;

		// Leave most of the frame to the browser itself; only spend clear headroom.
		const double reserve = 0.004;
		const int maxPerFrame = 2;
		int created = 0;

		auto it = children.begin();
		std::advance(it, cursor);
		for (; it != children.end(); ++it, ++cursor) {
			if (created >= maxPerFrame) return;
			if (budgetRemaining() <= reserve) return;
			if (warm(*it)) created++;
		}
	}
};

} // namespace Mb
} // namespace StoermelderPackOne
