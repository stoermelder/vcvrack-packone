#pragma once
#include "../../plugin.hpp"
#include "../../ui/AutoTagDialog.hpp"
#include "../../vcv/api.hpp"
#include "Mb_autotag.hpp"
#include <memory>

namespace StoermelderPackOne {
namespace Mb {

// ─── ModelLabel: a clickable label widget showing a VCV Rack module ────────
//
// One row in the auto-tag confirm dialog. The label shows the module's brand
// + name; left-click opens the VCV library page for it, right-click opens
// the module's context menu. This widget is Mb-specific; the Siren analog
// (in `SirenAutoTagDialog`) shows a sample filename instead.

struct ModelLabel : MenuItem {
	plugin::Model* model;
	NVGcolor lineColor = bndGetTheme()->regularTheme.textColor;

	ModelLabel(plugin::Model* m) : model(m) {
		text = model->plugin->brand + " " + model->name;
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			ui::Menu* menu = createMenu();
			model->appendContextMenu(menu, true);
			e.consume(this);
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			std::string fullSlug = model->plugin->slug + "/" + model->slug;
			std::string url = "https://library.vcvrack.com/?modules=" + fullSlug;
			vcv::ui::openBrowser(url);
			e.consume(this);
		}
	}

	void step() override {
		MenuItem::step();
		box.size.x -= 8.f;
	}
};


// ─── Adapter: turn an `AutoTagResult` into the dialog's input shape ─────────
//
// AutoTagResult uses `std::set<plugin::Model*>` as the per-tag payload. The
// generic dialog wants `std::vector<ui::TagGroup<plugin::Model*>>`. This
// adapter is the 1-to-1 mapping; the apply callback re-implements the
// `customTagAdd(model, tag)` writeback the old `AutoTagResult::apply()` did.

inline std::vector<ui::TagGroup<plugin::Model*>> autoTagResultToGroups(const AutoTagResult& result) {
	std::vector<ui::TagGroup<plugin::Model*>> groups;
	groups.reserve(result.assignments.size());
	for (const auto& pair : result.assignments) {
		groups.push_back({pair.first, pair.second});
	}
	return groups;
}

inline ui::TagConfirmDialog<plugin::Model*>::ApplyCallback autoTagApplyCallback() {
	return [](const std::map<std::string, std::set<plugin::Model*>>& filtered) {
		for (const auto& pair : filtered) {
			for (plugin::Model* model : pair.second) {
				customTagAdd(model, pair.first);
			}
		}
	};
}

inline ui::TagConfirmDialog<plugin::Model*>::BuildLabelCallback
autoTagBuildLabelCallback() {
	return [](const std::string& /*tag*/, plugin::Model* m) -> widget::Widget* {
		return new ModelLabel(m);
	};
}


// ─── Backward-compat aliases ────────────────────────────────────────────────
//
// The original Mb code used these names. We keep them as type aliases so
// existing call sites in Mb.cpp don't need to change.

using AutoTagConfirmWidget = ui::TagConfirmDialog<plugin::Model*>;
using AsyncTagResultWidget = ui::AsyncTagConfirmDialog<plugin::Model*>;


// ─── Openers used by Mb.cpp's menu items ────────────────────────────────────

inline void openAutoTagConfirmDialog(std::shared_ptr<AutoTagResult> result) {
	if (!result || result->assignments.empty()) {
		vcv::ui::message(vcv::MessageType::INFO, vcv::MessageButtons::OK, "No new tag assignments found.");
		return;
	}
	ui::openTagConfirmDialog<plugin::Model*>(
		"Auto-generate tags",
		autoTagResultToGroups(*result),
		autoTagBuildLabelCallback(),
		autoTagApplyCallback()
	);
}

inline AsyncTagResultWidget* makeAsyncAutoTagWidget(ui::MenuOverlay* loadingOverlay) {
	return new AsyncTagResultWidget(
		loadingOverlay,
		autoTagBuildLabelCallback(),
		autoTagApplyCallback(),
		"Auto-generate tags",                                  // header
		nullptr                                                // summary (use default)
	);
}

// Bridge a worker-thread-produced `std::shared_ptr<AutoTagResult>` into the
// `std::shared_ptr<GroupVector>` the async dialog expects. Used by Mb.cpp's
// background-thread callback: `asyncWidget->setAutoTagResult(result)`.
inline void setAutoTagResult(AsyncTagResultWidget* w, std::shared_ptr<AutoTagResult> r) {
	if (!w) return;
	if (!r || r->assignments.empty()) {
		// Hand the dialog an empty GroupVector so its "no assignments" path
		// fires (it shows an info dialog and closes itself).
		w->result = std::make_shared<std::vector<ui::TagGroup<plugin::Model*>>>();
		return;
	}
	w->result = std::make_shared<std::vector<ui::TagGroup<plugin::Model*>>>(
		autoTagResultToGroups(*r)
	);
}

} // namespace Mb
} // namespace StoermelderPackOne
