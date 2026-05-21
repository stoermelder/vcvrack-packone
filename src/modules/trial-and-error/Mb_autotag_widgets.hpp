#pragma once
#include "../../plugin.hpp"
#include "Mb_autotag.hpp"
#include <ui/ScrollWidget.hpp>
#include <osdialog.h>
#include <memory>

namespace StoermelderPackOne {
namespace Mb {

struct AutoTagConfirmWidget : widget::OpaqueWidget {
	struct TagButton : ui::Button {
		std::string tag;
		AutoTagConfirmWidget* w;
		bool selected = true;

		TagButton(std::string t, AutoTagConfirmWidget* w) : tag(t), w(w) {}

		void onAction(const ActionEvent& e) override {
			selected ^= true;
			w->selectedTags[tag] = selected;
			w->updateSummary();
		}

		void draw(const DrawArgs& args) override {
			text = string::f("%s %s", tag, selected ? CHECKMARK(true) : "");
			BNDwidgetState state = BND_DEFAULT;
			if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
			if (APP->event->getDraggedWidget() == this) state = BND_ACTIVE;
			bndToolButton(args.vg, 0.f, 0.f, box.size.x, box.size.y, BND_CORNER_NONE, state, -1, text.c_str());
		}
	};

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
				system::openBrowser(url);
				e.consume(this);
			}
		}

		void step() override {
			MenuItem::step();
			box.size.x -= 8.f;
		}
	};

	struct OkButton : ui::Button {
		AutoTagConfirmWidget* w;
		void onAction(const ActionEvent& e) override {
			w->applySelected();
			w->getParent()->requestDelete();
		}
	};

	struct CancelButton : ui::Button {
		AutoTagConfirmWidget* w;
		void onAction(const ActionEvent& e) override {
			w->getParent()->requestDelete();
		}
	};

	CancelButton* cancelButton = nullptr;
	OkButton* okButton = nullptr;
	ui::Label* summaryLabel = nullptr;
    ui::SequentialLayout* tagListLayout = nullptr;
	ui::ScrollWidget* scroll = nullptr;

	bool confirmed = false;
	std::shared_ptr<AutoTagResult> result;
	std::map<std::string, bool> selectedTags;

	AutoTagConfirmWidget(std::shared_ptr<AutoTagResult> result) {
		this->result = result;
		box.size = math::Vec(800.f, 500.f);

		// Initialize all tags as selected
		for (auto& pair : result->perTag) {
			selectedTags[pair.first] = true;
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
		header->text = "Auto-generate tags";
		layout->addChild(header);

		// Scroll container for tag list
		scroll = new ui::ScrollWidget;
        scroll->horizontalScrollbar->hide();
		layout->addChild(scroll);

		tagListLayout = new ui::SequentialLayout;
		tagListLayout->box.pos = math::Vec(0.f, 0.f);
		tagListLayout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
		tagListLayout->spacing = math::Vec(0.f, 4.f);
		scroll->container->addChild(tagListLayout);

		for (auto& pair : result->perTag) {
			widget::Widget* row = new widget::Widget;
			row->box.size.y = 20.f;
			row->box.size.x = box.size.x - 2.f * margin - 40.f;

			TagButton* cb = new TagButton(pair.first, this);
			cb->box.pos = math::Vec(6.f, 0.f);
			cb->box.size.x = 180.f;
			row->addChild(cb);

			// SequentialLayout for individual model labels
			ui::SequentialLayout* modelLayout = new ui::SequentialLayout;
			modelLayout->box.pos = math::Vec(cb->box.pos.x + cb->box.size.x + 10.f, 0.f);
			modelLayout->box.size.x = box.size.x - cb->box.size.x - 30.f;
			modelLayout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
			modelLayout->spacing = math::Vec(-.5f, -.5f);
			row->addChild(modelLayout);

			// Add individual model labels sorted alphabetically
			auto it = result->assignments.find(pair.first);
			if (it != result->assignments.end()) {
				std::vector<plugin::Model*> models(it->second.begin(), it->second.end());
				std::sort(models.begin(), models.end(), [](plugin::Model* a, plugin::Model* b) {
					return string::lowercase(a->plugin->brand + " " + a->name) < string::lowercase(b->plugin->brand + " " + b->name);
				});
				for (auto* m : models) {
					ModelLabel* modelLabel = new ModelLabel(m);
					modelLayout->addChild(modelLabel);
				}
			}

			// Update row height based on model layout
			modelLayout->step();
			row->box.size.y = std::max(row->box.size.y, modelLayout->box.size.y);
			tagListLayout->addChild(row);
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

	void updateSummary() {
		int selected = 0;
		for (auto& s : selectedTags) {
			if (s.second) {
				selected++;
			}
		}

		// Count unique modules across all selected tags
		int selectedModules = 0;
		std::set<plugin::Model*> uniqueModels;
		for (auto& s : selectedTags) {
			if (s.second) {
				auto it = result->assignments.find(s.first);
				if (it != result->assignments.end()) {
					for (auto* m : it->second) {
						uniqueModels.insert(m);
					}
				}
			}
		}
		selectedModules = (int)uniqueModels.size();

		summaryLabel->text = string::f("%d tag%s selected across %d module%s",
			selected, selected == 1 ? "" : "s",
			selectedModules, selectedModules == 1 ? "" : "s");
	}

	void applySelected() {
		// Build filtered assignments based on selected tags
		std::map<std::string, std::set<plugin::Model*>> filtered;
		for (auto& pair : result->assignments) {
			if (selectedTags[pair.first]) {
				filtered[pair.first] = pair.second;
			}
		}
		result->assignments = filtered;
		result->total = (int)filtered.size();
		result->apply();
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

// Helper widget that waits for async result and shows confirmation
struct AsyncTagResultWidget : widget::OpaqueWidget {
	std::shared_ptr<AutoTagResult> result;
	ui::MenuOverlay* loadingOverlay;
	bool ready = false;

	AsyncTagResultWidget(ui::MenuOverlay* lo) : loadingOverlay(lo) {}

	void step() override {
		// Check if we received a result from the background thread
		if (result && !ready) {
			ready = true;
			loadingOverlay->requestDelete();

			if (result->total == 0) {
				osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "No new tag assignments found.");
				requestDelete();
				return;
			}

			ui::MenuOverlay* overlay = new ui::MenuOverlay;
			overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
			AutoTagConfirmWidget* w = new AutoTagConfirmWidget(result);
			overlay->addChild(w);
			APP->scene->addChild(overlay);
			requestDelete();
		}
		OpaqueWidget::step();
	}
};

} // namespace Mb
} // namespace StoermelderPackOne
