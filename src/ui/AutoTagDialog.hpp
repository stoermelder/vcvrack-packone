#pragma once
#include <rack.hpp>
#include <ui/ScrollWidget.hpp>
#include <osdialog.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace ui {

using namespace rack;

// ─── TagConfirmDialog ───────────────────────────────────────────────────────
//
// A generic tag-confirmation dialog: a centered MenuOverlay that lists one
// row per tag, each row containing a checkbox-style button for the tag name
// and a horizontal list of "label widgets" (e.g. module names in Mb, sample
// filenames in Siren). The user can toggle which tags to apply and presses
// "Apply" to invoke the apply callback with the filtered per-tag payload map.
//
// Two entry points:
//   - `TagConfirmDialog<TPayload>` for an immediate-use dialog that already
//     has its per-tag data when the user opens the menu.
//   - `AsyncTagConfirmDialog<TPayload>` for a dialog whose per-tag data is
//     produced by a background thread; the dialog itself is just a tiny
//     widget that swaps itself out for the real confirm dialog once the
//     worker thread writes to its `result` field.
//
// TPayload is the value type of the per-tag set: `plugin::Model*` in Mb,
// `std::string` (sample relative path) in Siren. The dialog itself is
// otherwise type-agnostic.

template <typename TPayload>
struct TagGroup {
	std::string tag;
	std::set<TPayload> payloads;
};

template <typename TPayload>
struct TagConfirmDialog : widget::OpaqueWidget {

	using GroupVector = std::vector<TagGroup<TPayload>>;
	// The label callback receives both the tag this payload belongs to and the
	// payload itself, so labels can offer a "remove from this group" action.
	using BuildLabelCallback = std::function<widget::Widget*(const std::string& /*tag*/, TPayload)>;
	using ApplyCallback = std::function<void(const std::map<std::string, std::set<TPayload>>&)>;

	// ── inner widgets ─────────────────────────────────────────────────────

	struct TagButton : ui::Button {
		std::string tag;
		TagConfirmDialog<TPayload>* w;
		bool selected = true;

		TagButton(std::string t, TagConfirmDialog<TPayload>* w) : tag(t), w(w) {}

		void onHover(const event::Hover& e) override {
			Widget::onHover(e);
			e.consume(this);
		}
		void onButton(const event::Button& e) override {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				selected ^= true;
				w->selectedTags[tag] = selected;
				w->updateSummary();
				e.consume(this);
			}
		}
		void draw(const DrawArgs& args) override {
			float x = 0.f, y = 0.f, w_ = box.size.x, h = box.size.y;
			BNDwidgetState state = BND_DEFAULT;
			if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
			if (APP->event->getDraggedWidget() == this) state = BND_ACTIVE;
			std::string text = tag + "  " + (selected ? CHECKMARK(true) : "");
			bndToolButton(args.vg, x, y, w_, h, BND_CORNER_NONE, state, -1, text.c_str());
		}
	};

	struct OkButton : ui::Button {
		TagConfirmDialog<TPayload>* w;
		void onAction(const ActionEvent& e) override {
			w->applySelected();
			w->getParent()->requestDelete();
		}
	};

	struct CancelButton : ui::Button {
		TagConfirmDialog<TPayload>* w;
		void onAction(const ActionEvent& e) override {
			w->getParent()->requestDelete();
		}
	};

	// ── state ─────────────────────────────────────────────────────────────

	CancelButton* cancelButton = nullptr;
	OkButton* okButton = nullptr;
	ui::Label* summaryLabel = nullptr;
	ui::SequentialLayout* tagListLayout = nullptr;
	ui::ScrollWidget* scroll = nullptr;

	std::map<std::string, bool> selectedTags;
	GroupVector groups;
	BuildLabelCallback buildLabelCallback;
	ApplyCallback applyCallback;

	// ── ctor ──────────────────────────────────────────────────────────────

	TagConfirmDialog(std::string headerText, GroupVector groups, BuildLabelCallback buildLabel, ApplyCallback apply)
		: groups(std::move(groups)), buildLabelCallback(std::move(buildLabel)), applyCallback(std::move(apply)) {

		box.size = math::Vec(800.f, 500.f);

		// Pre-select all tags by default
		for (const auto& g : this->groups) {
			selectedTags[g.tag] = true;
		}

		const float margin = 10.f;

		ui::SequentialLayout* layout = new ui::SequentialLayout;
		layout->box.pos = math::Vec(0.f, 10.f);
		layout->box.size = box.size;
		layout->orientation = ui::SequentialLayout::VERTICAL_ORIENTATION;
		layout->margin = math::Vec(margin, margin);
		layout->spacing = math::Vec(margin, 8.f);
		layout->wrap = false;
		addChild(layout);

		ui::Label* header = new ui::Label;
		header->box.size.y = 24.f;
		header->fontSize = 18.f;
		header->text = headerText;
		layout->addChild(header);

		scroll = new ui::ScrollWidget;
		scroll->horizontalScrollbar->hide();
		layout->addChild(scroll);

		tagListLayout = new ui::SequentialLayout;
		tagListLayout->box.pos = math::Vec(0.f, 0.f);
        tagListLayout->box.size.y = 0.f;
		tagListLayout->orientation = ui::SequentialLayout::VERTICAL_ORIENTATION;
		tagListLayout->spacing = math::Vec(0.f, 4.f);
		scroll->container->addChild(tagListLayout);

		for (const auto& g : this->groups) {
			widget::Widget* row = new widget::Widget;
			row->box.size.y = 20.f;
			row->box.size.x = box.size.x - 2.f * margin - 40.f;

			TagButton* cb = new TagButton(g.tag, this);
			cb->box.pos = math::Vec(6.f, 0.f);
			cb->box.size.x = 180.f;
			row->addChild(cb);

			// Caller-built per-row label widgets in a horizontal layout
			ui::SequentialLayout* labelLayout = new ui::SequentialLayout;
			labelLayout->box.pos = math::Vec(cb->box.pos.x + cb->box.size.x + 10.f, 0.f);
			labelLayout->box.size.x = box.size.x - cb->box.size.x - 20.f;
			labelLayout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
			labelLayout->spacing = math::Vec(-.5f, -.5f);
			row->addChild(labelLayout);

			if (buildLabelCallback) {
				for (const TPayload& p : g.payloads) {
					widget::Widget* labelWidget = buildLabelCallback(g.tag, p);
					if (labelWidget) {
						labelLayout->addChild(labelWidget);
					}
				}
			}

			labelLayout->step();
			row->box.size.y = std::max(row->box.size.y, labelLayout->box.size.y);
			tagListLayout->addChild(row);
            tagListLayout->box.size.y += row->box.size.y + tagListLayout->spacing.y;
		}

		// Button row
		ui::SequentialLayout* buttonLayout = new ui::SequentialLayout;
		buttonLayout->box.size.x = box.size.x - 2.f * margin;
		buttonLayout->spacing = math::Vec(margin, margin);
		buttonLayout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
		layout->addChild(buttonLayout);

		widget::Widget* spacer = new widget::Widget;
		spacer->box.size.x = 190.f;
		spacer->box.size.y = 0.f;
		buttonLayout->addChild(spacer);

		cancelButton = new CancelButton;
		cancelButton->box.size.x = 100.f;
		cancelButton->text = "Cancel";
		cancelButton->w = this;
		buttonLayout->addChild(cancelButton);

		okButton = new OkButton;
		okButton->box.size.x = 100.f;
		okButton->text = "Apply";
		okButton->w = this;
		buttonLayout->addChild(okButton);

		summaryLabel = new ui::Label;
		summaryLabel->box.size.x = 240.f;
		summaryLabel->box.size.y = 20.f;
		updateSummary();
		buttonLayout->addChild(summaryLabel);
	}

	// Subclass-overridable: footer summary text. The default talks about
	// "tags across modules" because that's the original Mb use case; Siren
	// and other consumers can override.
	virtual std::string summaryText(int selectedTagCount, int totalItemCount) const {
		return string::f("%d tag%s selected across %d module%s",
			selectedTagCount, selectedTagCount == 1 ? "" : "s",
			totalItemCount, totalItemCount == 1 ? "" : "s");
	}

	void updateSummary() {
		int selected = 0;
		std::set<TPayload> uniqueItems;
		for (const auto& g : groups) {
			if (selectedTags[g.tag]) {
				selected++;
				for (const TPayload& p : g.payloads) {
					uniqueItems.insert(p);
				}
			}
		}
		summaryLabel->text = summaryText(selected, (int)uniqueItems.size());
	}

	void applySelected() {
		if (!applyCallback) return;
		std::map<std::string, std::set<TPayload>> filtered;
		for (const auto& g : groups) {
			if (selectedTags[g.tag]) {
				filtered[g.tag] = g.payloads;
			}
		}
		applyCallback(filtered);
	}

	void step() override {
		box.pos = parent->box.size.minus(box.size).div(2).round();
		scroll->box.size = box.size - Vec(20.f, 104.f);
        tagListLayout->box.size.x = scroll->box.size.x;
		OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		bndMenuBackground(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0);
		Widget::draw(args);
	}
};


// ─── AsyncTagConfirmDialog ──────────────────────────────────────────────────
//
// Drop-in replacement for the old `AsyncTagResultWidget`: a tiny placeholder
// widget that lives on the scene while a worker thread fills in
// `result`. Once non-null, the placeholder removes itself and constructs a
// real `TagConfirmDialog` with the same per-row label builder and apply
// callback. Mirrors the existing pattern but is templated on TPayload.

template <typename TPayload>
struct AsyncTagConfirmDialog : widget::OpaqueWidget {
	using GroupVector = std::vector<TagGroup<TPayload>>;
	using BuildLabelCallback = typename TagConfirmDialog<TPayload>::BuildLabelCallback;
	using ApplyCallback = typename TagConfirmDialog<TPayload>::ApplyCallback;

	std::shared_ptr<GroupVector> result;
	ui::MenuOverlay* loadingOverlay;
	BuildLabelCallback buildLabelCallback;
	ApplyCallback applyCallback;
	std::string headerText;
	std::function<std::string(int, int)> summaryCallback;
	bool ready = false;

	AsyncTagConfirmDialog(ui::MenuOverlay* lo, BuildLabelCallback buildLabel, ApplyCallback apply,
	        std::string header = "", std::function<std::string(int, int)> summary = {})
		: loadingOverlay(lo), buildLabelCallback(std::move(buildLabel)), applyCallback(std::move(apply)), 
            headerText(std::move(header)), summaryCallback(std::move(summary)) {}


	void step() override {
		if (result && !ready) {
			ready = true;
			loadingOverlay->requestDelete();

			if (result->empty()) {
				osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "No new tag assignments found.");
				requestDelete();
				return;
			}

			ui::MenuOverlay* overlay = new ui::MenuOverlay;
			overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);

			// Pick the concrete dialog subclass based on which overrides
			// the caller supplied. Single subclass handles both header and
			// summary (defaults kick in for the empty case).
            struct Dialog : TagConfirmDialog<TPayload> {
                std::function<std::string(int, int)> sumFn;
                Dialog(GroupVector g, BuildLabelCallback b, ApplyCallback a,
                        std::string headerText, std::function<std::string(int, int)> s)
                    : TagConfirmDialog<TPayload>(headerText, std::move(g), std::move(b), std::move(a)),
                        sumFn(std::move(s)) {}
                std::string summaryText(int sel, int items) const override {
                    return sumFn ? sumFn(sel, items) : TagConfirmDialog<TPayload>::summaryText(sel, items);
                }
            };

			TagConfirmDialog<TPayload>* dlg = new Dialog(*result, buildLabelCallback, applyCallback, headerText, summaryCallback);
			dlg->updateSummary();  // re-run after vtable is set so the override fires
			overlay->addChild(dlg);
			APP->scene->addChild(overlay);
			requestDelete();
		}
		OpaqueWidget::step();
	}
};


// ─── Convenience opener ─────────────────────────────────────────────────────

// Show a `TagConfirmDialog` centered on the scene. Equivalent to the three
// lines of MenuOverlay construction that appear at every call site.
template <typename TPayload>
inline void openTagConfirmDialog(
        std::string headerText,
		typename TagConfirmDialog<TPayload>::GroupVector groups,
		typename TagConfirmDialog<TPayload>::BuildLabelCallback buildLabel,
		typename TagConfirmDialog<TPayload>::ApplyCallback apply) {
	ui::MenuOverlay* overlay = new ui::MenuOverlay;
	overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
	overlay->addChild(new TagConfirmDialog<TPayload>(headerText, std::move(groups), std::move(buildLabel), std::move(apply)));
	APP->scene->addChild(overlay);
}

} // namespace ui
} // namespace StoermelderPackOne
