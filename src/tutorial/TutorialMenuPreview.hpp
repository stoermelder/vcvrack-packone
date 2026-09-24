#pragma once
// Requires Tutorial.hpp's HighlightStyle to already be defined — always #include "Tutorial.hpp"
// (never this file directly); it includes this one at the point where that's guaranteed.
#include "../plugin.hpp"
#include <algorithm>
#include <vector>


namespace StoermelderPackOne {
namespace Tutorial {

// ---- Menu previews ---------------------------------------------------------------------------
//
// Shows a module's real context/submenu as a read-only illustration during a step, positioned
// next to a ModuleWidget and highlighted, rather than opening it as a live, clickable menu.
//
// createMenu() wraps the Menu in a modal ui::MenuOverlay that captures every click/key on screen
// and self-deletes on the first one — correct for a real menu, but wrong here, since it would
// make the tutorial's own Back/Next/Esc unreachable while open.
//
// openMenuPreview() instead lets `build` create the menu normally, then detaches the resulting
// Menu and deletes the MenuOverlay, re-hosting the Menu under a plain, non-modal container
// (MenuPreviewHost) added directly to APP->scene, so clicks/keys that miss the menu pass through
// to whatever's under it. The menu is then pinned next to mw, highlighted, and covered by a
// small blocker that only swallows events landing on the menu's own area — see
// MenuHighlightWidget/MenuBlockerWidget below.
namespace menu_detail {

// A do-nothing container: gives the re-hosted Menu a same-size-as-the-scene parent (Menu::step()'s
// nudge() clamps against its parent's box) without MenuOverlay's modal event capture.
struct MenuPreviewHost : widget::Widget {
	void step() override {
		if (parent) box = parent->box.zeroPos();
		Widget::step();
	}
};

// Keeps a Menu pinned next to mw (right edge GAP before mw's left edge, Y_OFFSET below mw's top)
// every frame, and draws a pulsing highlight — either around the whole menu, or (if `items` is
// non-empty) a single ring around the union of every listed entry's row. Repositioning happens
// every frame since a Menu doesn't know its own box.size until its first step() has run.
//
// Draws on layer 0, not layer 1: nothing walks APP->scene's subtree at layer 1 (the render loop
// only calls APP->scene->draw()), so a drawLayer(1) override here would never be invoked.
struct MenuHighlightWidget : widget::TransparentWidget {
	static constexpr float GAP = 8.f;
	static constexpr float Y_OFFSET = 24.f;

	ui::Menu* menu = nullptr;
	app::ModuleWidget* mw = nullptr;
	std::vector<widget::Widget*> items; // optional: highlight just these entries instead of the whole menu

	void step() override {
		if (parent) { box.pos = Vec(0.f, 0.f); box.size = parent->box.size; }
		if (menu && mw && menu->parent) {
			Vec mwTopLeft = mw->getRelativeOffset(Vec(0.f, 0.f), APP->scene);
			menu->box.pos = Vec(mwTopLeft.x - menu->box.size.x - GAP, mwTopLeft.y + Y_OFFSET);
			// Redo the clamp Menu::step()'s nudge() already did this frame, now that the position
			// has been overwritten, so the pin can't push the menu off-screen.
			menu->box = menu->box.nudge(menu->parent->box.zeroPos());
		}
		Widget::step();
	}

	void drawRing(const DrawArgs& args, math::Rect r, float ringWidth, float cornerRadius) {
		float alpha = HighlightStyle::pulseAlpha((float) vcv::fs::getTime());
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(r), cornerRadius);
		NVGcolor c = HighlightStyle::accentColor();
		nvgStrokeColor(args.vg, nvgRGBAf(c.r, c.g, c.b, 0.5f + 0.5f * alpha));
		nvgStrokeWidth(args.vg, ringWidth);
		nvgStroke(args.vg);
	}

	void draw(const DrawArgs& args) override {
		if (!menu) return;
		// HighlightStyle's numbers are module px (rack zoom space); this widget draws in true
		// screen px under APP->scene, so scale by mw's current zoom to match visually.
		float zoom = mw ? mw->getAbsoluteZoom() : 1.f;
		float ringWidth = HighlightStyle::ringWidth * zoom;
		float cornerRadius = HighlightStyle::cornerRadius * zoom;
		float padding = 2.f * zoom;
		if (items.empty()) {
			drawRing(args, menu->box.grow(Vec(ringWidth + padding, ringWidth + padding)), ringWidth, cornerRadius);
		}
		else {
			// One ring around the union of all listed items, not one ring per item.
			bool any = false;
			math::Rect r;
			for (widget::Widget* item : items) {
				if (!item) continue;
				// item's box is menu-local; offset by menu->box.pos into this widget's space.
				math::Rect itemR(item->box.pos.plus(menu->box.pos), item->box.size);
				r = any ? r.expand(itemR) : itemR;
				any = true;
			}
			if (any) drawRing(args, r.grow(Vec(padding, padding)), ringWidth, cornerRadius);
		}
		Widget::draw(args);
	}
};

// Sits over the WHOLE menu (regardless of which items are highlighted) and swallows hover/click
// there, so nothing in the preview is triggerable. Clicks outside the menu still pass through.
struct MenuBlockerWidget : widget::OpaqueWidget {
	ui::Menu* menu = nullptr;

	void step() override {
		if (menu) box = menu->box;
		Widget::step();
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		e.consume(this); // also covers right-click; OpaqueWidget's own default only eats left-clicks
	}
};

} // namespace menu_detail

struct MenuPreview {
	widget::Widget* host = nullptr; // owns the menu + highlight + blocker; child of APP->scene
	ui::Menu* menu = nullptr;
};

// Finds the first direct child of `menu` whose MenuItem::text equals `label` exactly.
inline widget::Widget* findMenuItemByLabel(ui::Menu* menu, const std::string& label) {
	if (!menu) return nullptr;
	for (widget::Widget* w : menu->children) {
		if (auto* item = dynamic_cast<ui::MenuItem*>(w)) {
			if (item->text == label) return item;
		}
		if (auto* item = dynamic_cast<ui::MenuLabel*>(w)) {
			if (item->text == label) return item;
		}
	}
	return nullptr;
}

// Same as findMenuItemByLabel(), for highlighting several entries at once — e.g.
// findMenuItemsByLabel(menu, {"MIDI", "Learn", "Start sequential learn..."}). Walks menu's
// children once: arms on the first match of labels.front() (included, as the disambiguating
// anchor), collects every match up through labels.back(), then unarms. Labels with no match
// while armed are silently skipped.
inline std::vector<widget::Widget*> findMenuItemsByLabel(ui::Menu* menu, const std::vector<std::string>& labels) {
	std::vector<widget::Widget*> result;
	if (!menu || labels.empty()) return result;

	auto textOf = [](widget::Widget* w) -> const std::string* {
		if (auto* item = dynamic_cast<ui::MenuItem*>(w)) return &item->text;
		if (auto* item = dynamic_cast<ui::MenuLabel*>(w)) return &item->text;
		return nullptr;
	};

	bool armed = false;
	size_t next = 0; // index into labels of the next label still to be matched
	for (widget::Widget* w : menu->children) {
		const std::string* text = textOf(w);
		if (!text) continue;

		if (!armed) {
			if (*text == labels[next]) {
				armed = true;
				result.push_back(w);
				next++;
			}
			continue;
		}
		if (next >= labels.size()) break; // already matched labels.back()
		if (*text == labels[next]) {
			result.push_back(w);
			next++;
			if (next >= labels.size()) break; // just matched labels.back(): unarm
		}
	}
	return result;
}

// Runs `build` (anything that ends by opening a real menu via createMenu()) and re-hosts
// the menu it creates as a read-only, non-modal preview next to mw.
//
// If `pickItems` is given, it's called with the freshly-opened menu and whatever it returns
// is highlighted instead of the whole menu; an empty vector falls back to the whole menu.
//
// Returns an empty MenuPreview if `build` didn't actually open a menu.
inline MenuPreview openMenuPreview(app::ModuleWidget* mw, std::function<void()> build,
	std::function<std::vector<widget::Widget*>(ui::Menu*)> pickItems = nullptr) {
	MenuPreview preview;
	if (!APP->scene) return preview;

	// Rack's module browser (BrowserOverlay) is itself a permanent, hidden-when-unused
	// MenuOverlay child of APP->scene, so a naive "cast the last child" check could grab it
	// instead of the one build() just opened, and delete it. Identify the new menu by diffing
	// the child list before/after build() instead.
	std::vector<widget::Widget*> before(APP->scene->children.begin(), APP->scene->children.end());
	build();

	widget::Widget* added = nullptr;
	for (widget::Widget* w : APP->scene->children) {
		if (std::find(before.begin(), before.end(), w) == before.end()) {
			added = w; // last such match wins: the most recently appended new child
		}
	}
	if (!added) return preview; // build() didn't open anything new

	auto* overlay = dynamic_cast<ui::MenuOverlay*>(added);
	if (!overlay || overlay->children.empty()) {
		if (overlay) overlay->requestDelete();
		return preview;
	}
	ui::Menu* menu = dynamic_cast<ui::Menu*>(overlay->children.front());
	if (!menu) {
		overlay->requestDelete();
		return preview;
	}

	// Detach Menu from its modal MenuOverlay and delete the overlay directly (not via
	// requestDelete(), which would leave a dangling live-but-empty frame).
	overlay->removeChild(menu);
	APP->scene->removeChild(overlay);
	delete overlay;

	auto* host = new menu_detail::MenuPreviewHost;
	APP->scene->addChild(host);
	host->addChild(menu);

	// pickItems runs against the already-repositioned menu, before its first step()/layout.
	std::vector<widget::Widget*> items = pickItems ? pickItems(menu) : std::vector<widget::Widget*>();

	auto* highlight = new menu_detail::MenuHighlightWidget;
	highlight->menu = menu;
	highlight->mw = mw;
	highlight->items = items;
	host->addChild(highlight);

	// Added last so it's hit-tested first (children walked in reverse insertion order).
	auto* blocker = new menu_detail::MenuBlockerWidget;
	blocker->menu = menu;
	host->addChild(blocker);

	preview.host = host;
	preview.menu = menu;
	return preview;
}

// Tears down a preview openMenuPreview() returned. Safe on an empty MenuPreview, or after the
// widget tree was already torn down — rechecks preview.host is still a live child of APP->scene.
inline void closeMenuPreview(MenuPreview& preview) {
	if (preview.host && APP->scene) {
		for (widget::Widget* w : APP->scene->children) {
			if (w == preview.host) {
				preview.host->requestDelete();
				break;
			}
		}
	}
	preview.host = nullptr;
	preview.menu = nullptr;
}

} // namespace Tutorial
} // namespace StoermelderPackOne
