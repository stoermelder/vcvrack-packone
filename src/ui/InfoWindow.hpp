#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {

struct LayerOneMenuOverlay : ui::MenuOverlay {
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			ui::MenuOverlay::draw(args);
		}
	}
};


struct InfoOverlayWidget : widget::OpaqueWidget {
	struct UrlButton : ui::Button {
		std::string url;
		void onAction(const ActionEvent& e) override {
			vcv::ui::openBrowser(url);
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

inline widget::Widget* infoOverlayCreate(bool* setting, std::string header, std::string text, std::string url) {
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


// Centered modal with a header, a free-form text body, and two buttons
// (Cancel + Confirm).  Caller supplies both actions via callbacks.  Used for
// pre-flight confirmations (e.g. allocating a large in-memory preview buffer).
struct ConfirmOverlayWidget : widget::OpaqueWidget {
	struct CancelButton : ui::Button {
		ConfirmOverlayWidget* w;
		void onAction(const ActionEvent& e) override {
			if (w->onCancel) w->onCancel();
			w->getParent()->requestDelete();
		}
	};

	struct ConfirmButton : ui::Button {
		ConfirmOverlayWidget* w;
		void onAction(const ActionEvent& e) override {
			if (w->onConfirm) w->onConfirm();
			w->getParent()->requestDelete();
		}
	};

	ui::SequentialLayout* layout;
	ui::Label* header;
	ui::Label* label;
	CancelButton* cancelButton = nullptr;
	ConfirmButton* confirmButton = nullptr;

	std::function<void()> onCancel;
	std::function<void()> onConfirm;

	ConfirmOverlayWidget() {
		// Layout the dialog at a comfortable, normal size and then render it
		// through a ZoomWidget that uniformly scales the whole appearance
		// (font, buttons, padding) down.  This keeps the internal layout
		// ratios — including the BND default 21px button height and 13px
		// label font — intact, so the dialog looks like a properly-proportioned
		// modal rather than a tightly-shrunk panel.
		//
		// The inner content is laid out at (innerW × innerH).  The dialog's
		// own box.size equals the *visible* rendered footprint, which is
		// innerSize × zoom because the ZoomWidget scales its children by
		// `zoom` when drawing.
		constexpr float zoom = 0.75f;
		const float innerW = 360.f;
		const float innerH = 150.f;
		box.size = math::Vec(innerW * zoom, innerH * zoom);

		widget::ZoomWidget* zw = new widget::ZoomWidget;
		zw->box.pos = math::Vec(0.f, 0.f);
		zw->box.size = math::Vec(innerW, innerH);
		zw->setZoom(zoom);
		addChild(zw);

		const float margin = 10.f;
		const float buttonWidth = 100.f;

		layout = new ui::SequentialLayout;
		layout->box.size = zw->box.size;
		layout->orientation = ui::SequentialLayout::VERTICAL_ORIENTATION;
		layout->margin = math::Vec(margin, margin);
		layout->spacing = math::Vec(margin, margin);
		layout->wrap = false;
		zw->addChild(layout);

		header = new ui::Label;
		header->box.size.y = 20.f;
		header->fontSize = 14.f;
		layout->addChild(header);

		label = new ui::Label;
		label->box.size.y = 60.f;
		label->box.size.x = innerW - 2.f * margin;
		label->fontSize = 12.f;
		layout->addChild(label);

		ui::SequentialLayout* buttonLayout = new ui::SequentialLayout;
		buttonLayout->box.size.x = innerW - 2.f * margin;
		buttonLayout->spacing = math::Vec(margin, margin);
		layout->addChild(buttonLayout);

		cancelButton = new CancelButton;
		cancelButton->box.size.x = buttonWidth;
		cancelButton->text = "Cancel";
		cancelButton->w = this;
		buttonLayout->addChild(cancelButton);

		confirmButton = new ConfirmButton;
		confirmButton->box.size.x = buttonWidth;
		confirmButton->text = "Confirm";
		confirmButton->w = this;
		buttonLayout->addChild(confirmButton);

		buttonLayout->box.size.y = cancelButton->box.size.y;
	}

	void step() override {
		OpaqueWidget::step();
		// `box.size` is the post-scale visible size; the ZoomWidget child
		// renders the layout at `box.size / zoom` and is positioned at (0, 0).
		box.pos = parent->box.size.minus(box.size).div(2).round();
	}

	void draw(const DrawArgs& args) override {
		bndMenuBackground(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0);
		Widget::draw(args);
	}
};


// Builds a centered confirmation overlay.  Returns the MenuOverlay (already
// containing the dialog widget) so the caller can addChild() it where the
// overlay should be parented (e.g. APP->scene or a module widget).
inline widget::Widget* confirmOverlayCreate(std::string header, std::string text,
		std::string confirmLabel, std::function<void()> onCancel,
		std::function<void()> onConfirm) {
	ui::MenuOverlay* overlay = new LayerOneMenuOverlay;
	overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);

	ConfirmOverlayWidget* w = new ConfirmOverlayWidget;
	w->header->text = header;
	w->label->text = text;
	w->confirmButton->text = confirmLabel;
	w->onCancel = std::move(onCancel);
	w->onConfirm = std::move(onConfirm);
	overlay->addChild(w);

	return overlay;
}


} // namespace StoermelderPackOne
