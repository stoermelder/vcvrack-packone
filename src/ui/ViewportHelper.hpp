#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Rack {

/** Move the view-port smoothly and center a Widget
 */
struct ViewportCenterSmooth {
	Vec source, target;
	float sourceZoom, targetZoom;
	int framecount = 0;
	int frame = 0;

	void trigger(Widget* w, float zoom, float framerate, float transitionTime = 1.f) {
		Vec target = w->getBox().getCenter();
		zoom = std::pow(2.f, zoom);
		trigger(target, zoom, framerate, transitionTime);
	}

	void trigger(Rect rect, float framerate, float transitionTime = 1.f) {
		float zx = APP->scene->rackScroll->box.size.x / rect.size.x * 0.9f;
		float zy = APP->scene->rackScroll->box.size.y / rect.size.y * 0.9f;
		float zoom = std::min(zx, zy);
		trigger(rect.getCenter(), zoom, framerate, transitionTime);
	}

	void trigger(Vec target, float zoom, float framerate, float transitionTime = 1.f) {
		// source is at top-left, translate to center of screen
		Vec source = APP->scene->rackScroll->offset / APP->scene->rackScroll->getZoom();
		Vec center = APP->scene->rackScroll->getSize() * (1.f / APP->scene->rackScroll->getZoom()) * 0.5f;

		this->source = source + center;
		this->target = target;
		this->sourceZoom = APP->scene->rackScroll->getZoom();
		this->targetZoom = zoom;
		this->framecount = int(transitionTime * framerate);
		this->frame = 0;
	}

	void reset() {
		frame = framecount = 0;
	}

	void process() {
		if (framecount == frame) return;

		float t = float(frame) / float(framecount - 1);
		// Sigmoid
		t = t * 8.f - 4.f;
		t = 1.f / (1.f + std::exp(-t));
		t = rescale(t, 0.0179f, 0.98201f, 0.f, 1.f);

		// Calculate interpolated view-point and zoom
		Vec p1 = source.mult(1.f - t);
		Vec p2 = target.mult(t);
		Vec p = p1.plus(p2);
		
		// Ignore tiny changes in zoom as they will cause graphical artifacts
		if (std::abs(sourceZoom - targetZoom) > 0.01f) {
			float z = sourceZoom * (1.f - t) + targetZoom * t;
			APP->scene->rackScroll->setZoom(z);
		}

		// Move the view
		Vec center = APP->scene->rackScroll->getSize() * (1.f / APP->scene->rackScroll->getZoom()) * 0.5f;
		APP->scene->rackScroll->setGridOffset((p - center - RACK_OFFSET) / RACK_GRID_SIZE);

		frame++;
	}
};

struct ViewportCenter {
	ViewportCenter(Widget* w, float zoomToWidget = -1.f, float zoom = -1.f) {
		float z;
		if (zoomToWidget > 0.f)
			z = APP->scene->rackScroll->getSize().y / w->getSize().y * zoomToWidget;
		else if (zoom > 0.f)
			z = std::pow(2.f, zoom);
		else
			z = 2.0f;
		Vec target = w->getBox().getCenter();
		Vec viewport = APP->scene->rackScroll->getSize() * (1.f / z);
		APP->scene->rackScroll->setZoom(z);
		APP->scene->rackScroll->setGridOffset((target - viewport * 0.5f - RACK_OFFSET) / RACK_GRID_SIZE);
	}

	ViewportCenter(Vec target) {
		float z = APP->scene->rackScroll->getZoom();
		Vec viewport = APP->scene->rackScroll->getSize() * (1.f / z);
		APP->scene->rackScroll->setZoom(z);
		APP->scene->rackScroll->setGridOffset((target - viewport * 0.5f - RACK_OFFSET) / RACK_GRID_SIZE);
	}

	ViewportCenter(Rect rect) {
		Vec target = rect.getCenter();
		float zx = APP->scene->rackScroll->getSize().x / rect.size.x * 0.9f;
		float zy = APP->scene->rackScroll->getSize().y / rect.size.y * 0.9f;
		float z = std::min(zx, zy);
		Vec viewport = APP->scene->rackScroll->getSize() * (1.f / z);
		APP->scene->rackScroll->setZoom(z);
		APP->scene->rackScroll->setGridOffset((target - viewport * 0.5f - RACK_OFFSET) / RACK_GRID_SIZE);
	}
};

struct ViewportTopLeft {
	ViewportTopLeft(Widget* w, float zoomToWidget = -1.f, float zoom = -1.f) {
		float z;
		if (zoomToWidget > 0.f)
			z = APP->scene->rackScroll->getSize().y / w->getSize().y * zoomToWidget;
		else if (zoom > 0.f)
			z = std::pow(2.f, zoom);
		else
			z = 2.0f;
		Vec target = w->getBox().getTopLeft();
		APP->scene->rackScroll->setZoom(z);
		APP->scene->rackScroll->setGridOffset((target - RACK_OFFSET) / RACK_GRID_SIZE);
	}
};

} // namespace Rack
} // namespace StoermelderPackOne