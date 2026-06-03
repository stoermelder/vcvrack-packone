#pragma once
#include <rack.hpp>
#include <ui/ScrollWidget.hpp>
#include <osdialog.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace ui {

using namespace rack;

template <typename TPayload>
struct TagGroup {
	std::string        tag;
	std::set<TPayload> payloads;
};

// ─── TagConfirmDialog ───────────────────────────────────────────────────────
//
// A generic tag-confirmation dialog: a centered MenuOverlay that lists one
// row per tag, each row containing a checkbox-style button for the tag name
// and a horizontal list of "label widgets" (e.g. module names in Mb, sample
// filenames in Siren). The user can toggle which tags to apply and presses
// "Apply" to invoke the apply callback with the filtered per-tag payload map.
//
// Rows can be added after construction via addGroup() — used by
// StreamingTagDialog to append results as the worker thread produces them.

template <typename TPayload>
struct TagConfirmDialog : widget::OpaqueWidget {

	using GroupVector = std::vector<TagGroup<TPayload>>;
	using BuildLabelCallback = std::function<widget::Widget*(const std::string& /*tag*/, TPayload)>;
	using ApplyCallback = std::function<void(const std::map<std::string, std::set<TPayload>>&)>;

	static constexpr float kMargin = 10.f;

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

	ui::Label*             headerLabel    = nullptr;
	CancelButton*          cancelButton   = nullptr;
	OkButton*              okButton       = nullptr;
	ui::Label*             summaryLabel   = nullptr;
	ui::SequentialLayout*  tagListLayout  = nullptr;
	ui::ScrollWidget*      scroll         = nullptr;

	std::map<std::string, bool>                  selectedTags;
	std::map<std::string, ui::SequentialLayout*> labelLayoutByTag;
	std::map<widget::Widget*, std::string>       tagByRow;  // inverse of rowByTag
	GroupVector                                  groups;
	BuildLabelCallback                           buildLabelCallback;
	ApplyCallback                                applyCallback;

	// ── ctor ──────────────────────────────────────────────────────────────

	TagConfirmDialog(std::string headerText, GroupVector groups,
	                 BuildLabelCallback buildLabel, ApplyCallback apply)
		: groups(std::move(groups))
		, buildLabelCallback(std::move(buildLabel))
		, applyCallback(std::move(apply)) {

		box.size = math::Vec(800.f, 500.f);

		for (const auto& g : this->groups)
			selectedTags[g.tag] = true;

		ui::SequentialLayout* layout = new ui::SequentialLayout;
		layout->box.pos  = math::Vec(0.f, 10.f);
		layout->box.size = box.size;
		layout->orientation = ui::SequentialLayout::VERTICAL_ORIENTATION;
		layout->margin  = math::Vec(kMargin, kMargin);
		layout->spacing = math::Vec(kMargin, 8.f);
		layout->wrap    = false;
		addChild(layout);

		headerLabel = new ui::Label;
		headerLabel->box.size.y = 24.f;
		headerLabel->fontSize   = 18.f;
		headerLabel->text       = headerText;
		layout->addChild(headerLabel);

		scroll = new ui::ScrollWidget;
		scroll->horizontalScrollbar->hide();
		layout->addChild(scroll);

		tagListLayout = new ui::SequentialLayout;
		tagListLayout->box.pos  = math::Vec(0.f, 0.f);
		tagListLayout->box.size.y = 0.f;
		tagListLayout->orientation = ui::SequentialLayout::VERTICAL_ORIENTATION;
		tagListLayout->spacing  = math::Vec(0.f, 4.f);
		scroll->container->addChild(tagListLayout);

		for (const auto& g : this->groups) {
			widget::Widget* row = buildRow(g);
			tagListLayout->addChild(row);
			tagListLayout->box.size.y += row->box.size.y + tagListLayout->spacing.y;
		}

		// Button row
		ui::SequentialLayout* buttonLayout = new ui::SequentialLayout;
		buttonLayout->box.size.x = box.size.x - 2.f * kMargin;
		buttonLayout->spacing    = math::Vec(kMargin, kMargin);
		buttonLayout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
		layout->addChild(buttonLayout);

		widget::Widget* spacer = new widget::Widget;
		spacer->box.size.x = 190.f;
		spacer->box.size.y = 0.f;
		buttonLayout->addChild(spacer);

		cancelButton = new CancelButton;
		cancelButton->box.size.x = 100.f;
		cancelButton->text = "Cancel";
		cancelButton->w   = this;
		buttonLayout->addChild(cancelButton);

		okButton = new OkButton;
		okButton->box.size.x = 100.f;
		okButton->text = "Apply";
		okButton->w   = this;
		buttonLayout->addChild(okButton);

		summaryLabel = new ui::Label;
		summaryLabel->box.size.x = 240.f;
		summaryLabel->box.size.y = 20.f;
		updateSummary();
		buttonLayout->addChild(summaryLabel);
	}

	// ── public mutators ───────────────────────────────────────────────────

	void setHeaderText(const std::string& text) {
		if (headerLabel) headerLabel->text = text;
	}

	// Append a new tag group and its row widget. Pre-selects the tag.
	void addGroup(const TagGroup<TPayload>& g) {
		groups.push_back(g);
		selectedTags[g.tag] = true;
		widget::Widget* row = buildRow(g);
		tagListLayout->addChild(row);
		tagListLayout->box.size.y += row->box.size.y + tagListLayout->spacing.y;
		updateSummary();
	}

	// Add a single new payload to an existing group: updates both the data
	// model and appends a label widget to the existing row.
	void addPayloadToGroup(const std::string& tag, const TPayload& payload) {
		for (auto& g : groups)
			if (g.tag == tag) { g.payloads.insert(payload); break; }
		auto it = labelLayoutByTag.find(tag);
		if (it != labelLayoutByTag.end() && buildLabelCallback) {
			widget::Widget* lw = buildLabelCallback(tag, payload);
			if (lw) it->second->addChild(lw);
		}
		updateSummary();
	}

	// ── overridable summary ───────────────────────────────────────────────

	virtual std::string summaryText(int selectedTagCount, int totalItemCount) const {
		return string::f("%d tag%s selected across %d module%s",
			selectedTagCount, selectedTagCount == 1 ? "" : "s",
			totalItemCount,   totalItemCount   == 1 ? "" : "s");
	}

	void updateSummary() {
		int selected = 0;
		std::set<TPayload> uniqueItems;
		for (const auto& g : groups) {
			if (selectedTags[g.tag]) {
				selected++;
				for (const TPayload& p : g.payloads)
					uniqueItems.insert(p);
			}
		}
		summaryLabel->text = summaryText(selected, (int)uniqueItems.size());
	}

	void applySelected() {
		if (!applyCallback) return;
		std::map<std::string, std::set<TPayload>> filtered;
		for (const auto& g : groups)
			if (selectedTags[g.tag])
				filtered[g.tag] = g.payloads;
		applyCallback(filtered);
	}

	void step() override {
		box.pos = parent->box.size.minus(box.size).div(2).round();
		scroll->box.size = box.size - Vec(20.f, 104.f);
		tagListLayout->box.size.x = scroll->box.size.x;
		OpaqueWidget::step();

		// After all children have stepped (label sizes are now valid), sync
		// each row's height from its label layout and recompute the total height.
		// Rows are then repositioned in-place so the layout is correct this frame.
		float y = 0.f;
		bool first = true;
		for (widget::Widget* child : tagListLayout->children) {
			if (!first) y += tagListLayout->spacing.y;
			first = false;
			auto tagIt = tagByRow.find(child);
			if (tagIt != tagByRow.end()) {
				auto llIt = labelLayoutByTag.find(tagIt->second);
				if (llIt != labelLayoutByTag.end())
					child->box.size.y = std::max(20.f, llIt->second->box.size.y);
			}
			child->box.pos.y = y;
			y += child->box.size.y;
		}
		tagListLayout->box.size.y = y;
	}

	void draw(const DrawArgs& args) override {
		bndMenuBackground(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0);
		Widget::draw(args);
	}

private:
	// Build a single tag row widget — shared by the constructor loop and addGroup().
	widget::Widget* buildRow(const TagGroup<TPayload>& g) {
		widget::Widget* row = new widget::Widget;
		row->box.size.y = 20.f;
		row->box.size.x = box.size.x - 2.f * kMargin - 40.f;

		TagButton* cb  = new TagButton(g.tag, this);
		cb->box.pos    = math::Vec(6.f, 0.f);
		cb->box.size.x = 180.f;
		row->addChild(cb);

		ui::SequentialLayout* labelLayout = new ui::SequentialLayout;
		labelLayout->box.pos   = math::Vec(cb->box.pos.x + cb->box.size.x + 10.f, 0.f);
		labelLayout->box.size.x = box.size.x - cb->box.size.x - 20.f;
		labelLayout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
		labelLayout->spacing   = math::Vec(-.5f, -.5f);
		row->addChild(labelLayout);

		// Save layout reference so addPayloadToGroup() can append to it later.
		labelLayoutByTag[g.tag] = labelLayout;

		if (buildLabelCallback) {
			for (const TPayload& p : g.payloads) {
				widget::Widget* lw = buildLabelCallback(g.tag, p);
				if (lw) labelLayout->addChild(lw);
			}
		}

		labelLayout->step();
		row->box.size.y = std::max(row->box.size.y, labelLayout->box.size.y);
		tagByRow[row] = g.tag;
		return row;
	}
};


// ─── StreamingTagDialog ─────────────────────────────────────────────────────
//
// A TagConfirmDialog that shows immediately (empty) and fills in as a worker
// thread produces results. Uses a dsp::RingBuffer (SPSC, no mutex) to pass
// tag events from the worker to step() on the UI thread.
//
// Usage:
//   auto* dlg = new StreamingTagDialog<T>(buildLabel, apply, header, summaryFn);
//   overlay->addChild(dlg);
//   APP->scene->addChild(overlay);
//   auto prog = dlg->progress;           // share with worker
//   worker->work([prog, ...]() {
//       prog->total = N;
//       for each file:
//           /* classify */
//           prog->events.push({tag, fileId});
//           prog->processed++;
//       prog->done = true;
//   });

template <typename TPayload>
struct StreamingTagDialog : TagConfirmDialog<TPayload> {
	using Base = TagConfirmDialog<TPayload>;
	using BuildLabelCallback = typename Base::BuildLabelCallback;
	using ApplyCallback      = typename Base::ApplyCallback;

	struct Progress {
		// SPSC ring buffer: worker pushes, step() shifts — no mutex needed.
		// 4096 slots handles any realistic sample library; at ~5 events/file
		// and 60 fps drain rate the buffer is virtually never more than a few
		// slots deep.
		struct TagEvent { std::string tag; TPayload payload; };
		dsp::RingBuffer<TagEvent, 4096> events;
		std::atomic<int>  processed{0};
		std::atomic<int>  total{0};
		std::atomic<bool> done{false};
	};
	std::shared_ptr<Progress> progress = std::make_shared<Progress>();

	std::string                               baseHeader;
	std::function<std::string(int,int)>       summaryCallback;
	std::map<std::string, std::set<TPayload>> accumulated;   // UI-thread: all events ever drained
	std::map<std::string, std::set<TPayload>> shownPayloads; // UI-thread: what widget rows show
	bool                                      finished = false;

	StreamingTagDialog(BuildLabelCallback buildLabel, ApplyCallback apply,
	                   std::string header,
	                   std::function<std::string(int,int)> summaryFn = {})
		: Base(header, {}, std::move(buildLabel), std::move(apply))
		, baseHeader(header)
		, summaryCallback(std::move(summaryFn)) {
		this->setHeaderText(baseHeader + " — Analysing...");
	}

	std::string summaryText(int sel, int items) const override {
		return summaryCallback ? summaryCallback(sel, items)
		                       : Base::summaryText(sel, items);
	}

	void step() override {
		if (!finished) {
			// Load done with acquire FIRST so the ring buffer's non-atomic
			// end index is guaranteed visible before we drain below.
			const bool done = progress->done.load(std::memory_order_acquire);
			const int  proc = progress->processed.load(std::memory_order_relaxed);
			const int  tot  = progress->total.load(std::memory_order_relaxed);

			// Drain all events now visible after the acquire load.
			bool newData = false;
			while (!progress->events.empty()) {
				auto ev = progress->events.shift();
				accumulated[ev.tag].insert(ev.payload);
				newData = true;
			}

			if (newData) {
				for (const auto& kv : accumulated) {
					if (!shownPayloads.count(kv.first)) {
						// New tag: add full group with all payloads accumulated so far.
						shownPayloads[kv.first] = kv.second;
						this->addGroup({kv.first, kv.second});
					} else {
						// Existing tag: append only payloads not yet shown.
						for (const auto& p : kv.second) {
							if (!shownPayloads[kv.first].count(p)) {
								shownPayloads[kv.first].insert(p);
								this->addPayloadToGroup(kv.first, p);
							}
						}
					}
				}
			}

			if (done) {
				finished = true;
				if (this->groups.empty()) {
					osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "No new tag assignments found.");
					if (this->getParent()) this->getParent()->requestDelete();
					return;
				}
				this->setHeaderText(baseHeader);
				this->updateSummary();
			} else {
				const std::string suffix = (tot > 0)
					? " — Analysing " + std::to_string(proc) + "/" + std::to_string(tot) + "..."
					: " — Analysing...";
				this->setHeaderText(baseHeader + suffix);
			}
		}
		Base::step();
	}
};


// ─── AsyncTagConfirmDialog ──────────────────────────────────────────────────
//
// Original batch async pattern: a background thread sets `result` once when
// all work is done; step() then tears down the loading overlay and constructs
// the real TagConfirmDialog. Kept for backward compatibility (used by Mb).
// New code should prefer StreamingTagDialog for a responsive UI.

template <typename TPayload>
struct AsyncTagConfirmDialog : widget::OpaqueWidget {
	using GroupVector        = std::vector<TagGroup<TPayload>>;
	using BuildLabelCallback = typename TagConfirmDialog<TPayload>::BuildLabelCallback;
	using ApplyCallback      = typename TagConfirmDialog<TPayload>::ApplyCallback;

	std::shared_ptr<GroupVector> result;
	ui::MenuOverlay*             loadingOverlay;
	BuildLabelCallback           buildLabelCallback;
	ApplyCallback                applyCallback;
	std::string                  headerText;
	std::function<std::string(int, int)> summaryCallback;
	bool ready = false;

	AsyncTagConfirmDialog(ui::MenuOverlay* lo, BuildLabelCallback buildLabel, ApplyCallback apply,
	        std::string header = "", std::function<std::string(int, int)> summary = {})
		: loadingOverlay(lo)
		, buildLabelCallback(std::move(buildLabel))
		, applyCallback(std::move(apply))
		, headerText(std::move(header))
		, summaryCallback(std::move(summary)) {}

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

			struct Dialog : TagConfirmDialog<TPayload> {
				std::function<std::string(int, int)> sumFn;
				Dialog(GroupVector g, BuildLabelCallback b, ApplyCallback a,
				        std::string h, std::function<std::string(int, int)> s)
					: TagConfirmDialog<TPayload>(h, std::move(g), std::move(b), std::move(a))
					, sumFn(std::move(s)) {}
				std::string summaryText(int sel, int items) const override {
					return sumFn ? sumFn(sel, items) : TagConfirmDialog<TPayload>::summaryText(sel, items);
				}
			};

			TagConfirmDialog<TPayload>* dlg = new Dialog(
				*result, buildLabelCallback, applyCallback, headerText, summaryCallback);
			dlg->updateSummary();
			overlay->addChild(dlg);
			APP->scene->addChild(overlay);
			requestDelete();
		}
		OpaqueWidget::step();
	}
};


// ─── Convenience opener ─────────────────────────────────────────────────────

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
