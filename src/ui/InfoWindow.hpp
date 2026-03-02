#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {

struct InfoOverlayWidget : widget::OpaqueWidget {
	struct UrlButton : ui::Button {
		std::string url;
		void onAction(const ActionEvent& e) override {
			system::openBrowser(url);
		}
	};

	struct CloseButton : ui::Button {
		InfoOverlayWidget* w;
		void onAction(const ActionEvent& e) override {
			w->getParent()->requestDelete();
		}
	};

	struct SettingQuantity : Quantity {
		InfoOverlayWidget* w;
		void setValue(float value) override {
			*w->setting = (value > 0.f);
			pluginSettings.saveToJson();
		}
		float getValue() override {
			return *w->setting ? 1.f : 0.f;
		}
	};

	bool* setting;
	SettingQuantity* settingQuantity;
	ui::SequentialLayout* layout;
	ui::SequentialLayout* buttonLayout;
	ui::Label* header;
	ui::Label* label;
	UrlButton* linkButton;

	InfoOverlayWidget() {
		box.size = math::Vec(450.f, 250.f);
		const float margin = 10.f;
		const float buttonWidth = 100.f;

		layout = new ui::SequentialLayout;
		layout->box.pos = math::Vec(0.f, 10.f);
		layout->box.size = box.size;
		layout->orientation = ui::SequentialLayout::VERTICAL_ORIENTATION;
		layout->margin = math::Vec(margin, margin);
		layout->spacing = math::Vec(margin, 2.f * margin);
		layout->wrap = false;
		addChild(layout);

		header = new ui::Label;
		// header->box.size.x = box.size.x - 2*margin;
		header->box.size.y = 20.f;
		header->fontSize = 20.f;
		layout->addChild(header);

		label = new ui::Label;
		label->box.size.y = 80.f;
		label->box.size.x = box.size.x - 2.f * margin;
		layout->addChild(label);

		// Container for link button so hiding it won't shift layout
		widget::Widget* linkPlaceholder = new widget::Widget;
		layout->addChild(linkPlaceholder);

		linkButton = new UrlButton;
		linkButton->box.size.x = box.size.x - 2.f * margin;
		linkPlaceholder->box.size = linkButton->box.size;
		linkPlaceholder->addChild(linkButton);

		buttonLayout = new ui::SequentialLayout;
		buttonLayout->box.size.x = box.size.x - 2.f * margin;
		buttonLayout->spacing = math::Vec(margin, margin);
		layout->addChild(buttonLayout);

		settingQuantity = new SettingQuantity;
		settingQuantity->w = this;

		ui::OptionButton* showButton = new ui::OptionButton;
		showButton->box.size.x = 160.f;
		showButton->text = "Show this info";
		showButton->quantity = settingQuantity;
		buttonLayout->addChild(showButton);

		CloseButton* closeButton = new CloseButton;
		closeButton->box.size.x = buttonWidth;
		closeButton->text = "✖ Close";
		closeButton->w = this;
		buttonLayout->addChild(closeButton);

		buttonLayout->box.size.y = closeButton->box.size.y;
	}

	~InfoOverlayWidget() {
		delete settingQuantity;
	}

	void step() override {
		OpaqueWidget::step();
		box.pos = parent->box.size.minus(box.size).div(2).round();
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			bndMenuBackground(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0);
			Widget::draw(args);
		}
	}
};

widget::Widget* infoOverlayCreate(bool* setting, std::string header, std::string text, std::string url) {
	ui::MenuOverlay* overlay = new ui::MenuOverlay;
	overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);

	InfoOverlayWidget* w = new InfoOverlayWidget;
	w->setting = setting;
	w->header->text = header;
	w->label->text = text;
	w->linkButton->url = url;
	w->linkButton->text = url;
	overlay->addChild(w);

	return overlay;
}


} // namespace StoermelderPackOne
