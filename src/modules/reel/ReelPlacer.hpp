#pragma once
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Reel {


struct ReelModulePreview : widget::OpaqueWidget {
	plugin::Model* model = nullptr;
	widget::Widget* previewWidget = nullptr;
	widget::FramebufferWidget* previewFb = nullptr;
	widget::ZoomWidget* zoomWidget = nullptr;

	void setModel(plugin::Model* m) {
		model = m;
		previewWidget = new widget::TransparentWidget;
		addChild(previewWidget);
		box.size.x = 10 * RACK_GRID_WIDTH;
		box.size.y = RACK_GRID_HEIGHT;
	}

	void createPreview() {
		zoomWidget = new widget::ZoomWidget;
		zoomWidget->setZoom(1.f);
		previewWidget->addChild(zoomWidget);

		previewFb = new widget::FramebufferWidget;
		if (math::isNear(APP->window->pixelRatio, 1.0)) {
			previewFb->oversample = 2.0;
		}
		zoomWidget->addChild(previewFb);

		ModuleWidget* mw = model->createModuleWidget(NULL);
		previewFb->addChild(mw);

		box.size.x = mw->box.size.x;
		box.size.y = RACK_GRID_HEIGHT;
		previewFb->setDirty();
	}

	void draw(const DrawArgs& args) override {
		if (!previewFb) createPreview();
		float b = math::clamp(settings::rackBrightness + 0.2f, 0.f, 1.f);
		nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 0.4f));
		OpaqueWidget::draw(args);
	}
};


struct ReelPlacerPreview : OpaqueWidget {
	/** Each pair: model (may be null if plugin not installed) + relative position within the group */
	void setModels(const std::vector<std::pair<plugin::Model*, Vec>>& modelPositions) {
		clearChildren();
		for (auto& mp : modelPositions) {
			if (!mp.first) continue;
			ReelModulePreview* mb = new ReelModulePreview;
			mb->setModel(mp.first);
			mb->box.pos = mp.second;
			addChild(mb);
		}
	}

	void onHide(const HideEvent& e) override {
		OpaqueWidget::onHide(e);
		clearChildren();
	}
};


struct ReelPlacerContainer : widget::Widget {
	ReelPlacerPreview* preview = nullptr;
	std::function<void(Vec)> callback;

	ReelPlacerContainer() {
		preview = new ReelPlacerPreview;
		preview->hide();
		addChild(preview);
	}

	void draw(const DrawArgs& args) override {
		preview->box.pos = APP->scene->rack->getMousePos();
		Widget::draw(args);
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE && preview->isVisible()) {
			preview->hide();
			e.consume(this);
			return;
		}
		Widget::onHoverKey(e);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && preview->isVisible()) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
				Vec pos = APP->scene->rack->getMousePos();
				preview->hide();
				callback(pos);
				e.consume(this);
			}
			else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				preview->hide();
				e.consume(this);
			}
		}
		Widget::onButton(e);
	}

	void showPreview(const std::vector<std::pair<plugin::Model*, Vec>>& modelPositions, std::function<void(Vec)> action) {
		preview->setModels(modelPositions);
		callback = action;
		preview->show();
	}
};


} // namespace Reel
} // namespace StoermelderPackOne
