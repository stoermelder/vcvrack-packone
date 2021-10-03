#pragma once
#include "plugin.hpp"
#include "settings.hpp"

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
		Vec target = w->box.pos;
		target = target.plus(w->box.size.mult(0.5f));
		trigger(target, zoom, framerate, transitionTime);
	}

	void trigger(Vec target, float zoom, float framerate, float transitionTime = 1.f) {
		// source is at top-left, translate to center of screen
		Vec source = APP->scene->rackScroll->offset;
		source = source.plus(APP->scene->rackScroll->box.size.mult(0.5f));
		source = source.div(APP->scene->rackScroll->zoomWidget->zoom);

		this->source = source;
		this->target = target;
		this->sourceZoom = rack::settings::zoom;
		this->targetZoom = zoom;
		this->framecount = int(transitionTime * framerate);
		this->frame = 0;
	}

	void trigger(Rect rect, float framerate, float transitionTime = 1.f) {
		// NB: unstable API!
		Vec target = rect.getCenter();
		target = target.mult(APP->scene->rackScroll->zoomWidget->zoom);
		target = target.minus(APP->scene->rackScroll->box.size.mult(0.5f));
		float zx = std::log2(APP->scene->rackScroll->box.size.x / rect.size.x * 0.9f);
		float zy = std::log2(APP->scene->rackScroll->box.size.y / rect.size.y * 0.9f);
		float zoom = std::min(zx, zy);
		trigger(rect.getCenter(), zoom, framerate, transitionTime);
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
			rack::settings::zoom = z;
		}

		// Move the view
		// NB: unstable API!
		p = p.mult(APP->scene->rackScroll->zoomWidget->zoom);
		p = p.minus(APP->scene->rackScroll->box.size.mult(0.5f));
		APP->scene->rackScroll->offset = p;

		frame++;
	}
};

struct ViewportCenter {
	ViewportCenter(Widget* w, float zoomToWidget = -1.f) {
		// NB: unstable API!
		Vec target = w->box.pos;
		target = target.plus(w->box.size.mult(0.5f));
		target = target.mult(APP->scene->rackScroll->zoomWidget->zoom);
		target = target.minus(APP->scene->rackScroll->box.size.mult(0.5f));
		APP->scene->rackScroll->offset = target;
		if (zoomToWidget > 0.f) {
			rack::settings::zoom = std::log2(APP->scene->rackScroll->box.size.y / w->box.size.y * zoomToWidget);
		}
	}

	ViewportCenter(Vec target) {
		// NB: unstable API!
		target = target.mult(APP->scene->rackScroll->zoomWidget->zoom);
		target = target.minus(APP->scene->rackScroll->box.size.mult(0.5f));
		APP->scene->rackScroll->offset = target;
	}

	ViewportCenter(Rect rect) {
		// NB: unstable API!
		Vec target = rect.getCenter();
		target = target.mult(APP->scene->rackScroll->zoomWidget->zoom);
		target = target.minus(APP->scene->rackScroll->box.size.mult(0.5f));
		APP->scene->rackScroll->offset = target;
		float zx = std::log2(APP->scene->rackScroll->box.size.x / rect.size.x * 0.9f);
		float zy = std::log2(APP->scene->rackScroll->box.size.y / rect.size.y * 0.9f);
		rack::settings::zoom = std::min(zx, zy);
	}
};


/** Creates a MenuItem that when hovered, opens a submenu with several MenuItems identified by a map.
Example:
	menu->addChild(createMapSubmenuItem<QUALITY>("Mode",
		{
			{ QUALITY::HIFI, "Hi-fi" },
			{ QUALITY::MIDFI, "Mid-fi" },
			{ QUALITY::LOFI, "Lo-fi" }
		},
		[=]() {
			return module->getMode();
		},
		[=](QUALITY mode) {
			module->setMode(mode);
		}
	));
*/
template <typename TEnum, class TMenuItem = ui::MenuItem>
ui::MenuItem* createMapSubmenuItem(std::string text, std::map<TEnum, std::string> labels, std::function<TEnum()> getter, std::function<void(TEnum val)> setter, bool showRightText = true, bool disabled = false, bool alwaysConsume = false) {
	struct IndexItem : ui::MenuItem {
		std::function<TEnum()> getter;
		std::function<void(TEnum)> setter;
		TEnum index;
		bool alwaysConsume;

		void step() override {
			TEnum currIndex = getter();
			this->rightText = CHECKMARK(currIndex == index);
			MenuItem::step();
		}
		void onAction(const event::Action& e) override {
			setter(index);
			if (alwaysConsume)
				e.consume(this);
		}
	};

	struct Item : TMenuItem {
		std::function<TEnum()> getter;
		std::function<void(TEnum)> setter;
		std::map<TEnum, std::string> labels;
		TEnum currIndex;
		bool showRightText;
		bool alwaysConsume;

		void step() override {
			TEnum currIndex = getter();
			if (showRightText) {
				if (this->currIndex != currIndex) {
					std::string label = labels[currIndex];
					this->rightText = label + "  " + RIGHT_ARROW;
					this->currIndex = currIndex;
				}
			}
			else {
				this->rightText = RIGHT_ARROW;
			}
			TMenuItem::step();
		}
		ui::Menu* createChildMenu() override {
			ui::Menu* menu = new ui::Menu;
			for (const auto& i : labels) {
				IndexItem* item = createMenuItem<IndexItem>(i.second);
				item->getter = getter;
				item->setter = setter;
				item->index = i.first;
				item->alwaysConsume = alwaysConsume;
				menu->addChild(item);
			}
			return menu;
		}
	};

	Item* item = createMenuItem<Item>(text);
	item->getter = getter;
	item->setter = setter;
	item->labels = labels;
	item->showRightText = showRightText;
	item->disabled = disabled;
	item->alwaysConsume = alwaysConsume;
	return item;
}

/** Easy wrapper for createMapPtrSubmenuItem() that controls a mapped label at a pointer address.
Example:
	menu->addChild(createMapPtrSubmenuItem<QUALITY>("Mode",
		{
			{ QUALITY::HIFI, "Hi-fi" },
			{ QUALITY::MIDFI, "Mid-fi" },
			{ QUALITY::LOFI, "Lo-fi" }
		},
		&module->mode
	));
*/
template <typename TEnum>
ui::MenuItem* createMapPtrSubmenuItem(std::string text, std::map<TEnum, std::string> labels, TEnum* ptr, bool showRightText = true) {
	return createMapSubmenuItem<TEnum>(text, labels,
		[=]() { return *ptr; },
		[=](TEnum index) { *ptr = TEnum(index); },
		showRightText
	);
}

/** Easy wrapper for createMenuItem() to modify a property with a specific value.
Example:
	menu->addChild(createValuePtrMenuItem("Loop", &module->mode, MODE::LOOP));
*/
template <typename T>
ui::MenuItem* createValuePtrMenuItem(std::string text, T* ptr, T val) {
	return createMenuItem(text, CHECKMARK(*ptr == val), [=]() { *ptr = val; });
}


} // namespace Rack
} // namespace StoermelderPackOne