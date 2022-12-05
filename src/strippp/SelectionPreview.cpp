#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {
namespace SppPreview {


struct ModelBox : widget::OpaqueWidget {
	plugin::Model* model;
	widget::Widget* previewWidget;
	/** Lazily created */
	widget::FramebufferWidget* previewFb = NULL;
	widget::ZoomWidget* zoomWidget = NULL;
	float modelBoxZoom = -1.f;
	float modelBoxWidth = -1.f;

	void setModel(plugin::Model* model) {
		this->model = model;
		previewWidget = new widget::TransparentWidget;
		addChild(previewWidget);
	}

	void step() override {
		if (modelBoxZoom != 1.f) {
			//deletePreview();
			modelBoxZoom = 1.f;
			// Approximate size as 10HP before we know the actual size.
			// We need a nonzero size, otherwise the parent widget will consider it not in the draw bounds, so its preview will not be lazily created.
			box.size.x = (modelBoxWidth < 0 ? 10 * RACK_GRID_WIDTH : modelBoxWidth) * modelBoxZoom;
			box.size.y = RACK_GRID_HEIGHT * modelBoxZoom;
			box.size = box.size.ceil();

			previewWidget->box.size.y = std::ceil(RACK_GRID_HEIGHT * modelBoxZoom);
			if (previewFb) sizePreview();
		}
		widget::OpaqueWidget::step();
	}

	void createPreview() {
		zoomWidget = new widget::ZoomWidget;
		previewWidget->addChild(zoomWidget);

		previewFb = new widget::FramebufferWidget;
		if (math::isNear(APP->window->pixelRatio, 1.0)) {
			// Small details draw poorly at low DPI, so oversample when drawing to the framebuffer
			previewFb->oversample = 2.0;
		}
		zoomWidget->addChild(previewFb);

		ModuleWidget* moduleWidget = model->createModuleWidget(NULL);
		previewFb->addChild(moduleWidget);
		// Save the width, used for correct width of blank before rendered
		modelBoxWidth = moduleWidget->box.size.x;

		sizePreview();
	}

	void sizePreview() {
		zoomWidget->setZoom(modelBoxZoom);
		previewFb->setDirty();
		box.size.x = modelBoxWidth * modelBoxZoom;
		box.size.y = RACK_GRID_HEIGHT * modelBoxZoom;
	}

	void deletePreview() {
		if (!previewFb) return;
		previewWidget->removeChild(previewFb);
		delete previewFb;
		previewFb = NULL;
	}

	void draw(const DrawArgs& args) override {
		// Lazily create preview when drawn
		if (!previewFb) {
			createPreview();
		}

		// To avoid blinding the user when rack brightness is low, draw framebuffer with the same brightness.
		float b = math::clamp(settings::rackBrightness + 0.2f, 0.f, 1.f);
		nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 0.4f));

		OpaqueWidget::draw(args);
	}
};



struct SelectionPreview : OpaqueWidget {
	void loadSelectionFile(std::string path) {
		FILE* file = std::fopen(path.c_str(), "r");
		if (!file) return;
		DEFER({std::fclose(file);});
		INFO("Loading selection %s", path.c_str());

		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		if (!rootJ)
			throw Exception("File is not a valid selection file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
		DEFER({json_decref(rootJ);});
		createPreview(rootJ);
	}

	void createPreview(json_t* rootJ) {
		json_t* modulesJ = json_object_get(rootJ, "modules");
		if (!modulesJ) return;

		json_t* moduleJ;
		size_t moduleIndex;

		double minX = std::numeric_limits<float>::infinity();
		double minY = std::numeric_limits<float>::infinity();
		json_array_foreach(modulesJ, moduleIndex, moduleJ) {
			json_t* posJ = json_object_get(moduleJ, "pos");
			double x = 0.0, y = 0.0;
			json_unpack(posJ, "[F, F]", &x, &y);
			minX = std::min(minX, x);
			minY = std::min(minY, y);
		}

		json_array_foreach(modulesJ, moduleIndex, moduleJ) {
			json_t* posJ = json_object_get(moduleJ, "pos");
			double x = 0.0, y = 0.0;
			json_unpack(posJ, "[F, F]", &x, &y);

			// Get slugs
			json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
			if (!pluginSlugJ) continue;
			json_t* modelSlugJ = json_object_get(moduleJ, "model");
			if (!modelSlugJ) continue;
			std::string pluginSlug = json_string_value(pluginSlugJ);
			std::string modelSlug = json_string_value(modelSlugJ);

			// Get Model
			plugin::Model* model = plugin::getModel(pluginSlug, modelSlug);
			if (!model) continue;

			ModelBox* modelBox = new ModelBox;
			modelBox->setModel(model);
			modelBox->box.pos = Vec(x - minX, y - minY).mult(RACK_GRID_SIZE);
			addChild(modelBox);
		}
	}

	void onHide(const HideEvent& e) override {
		OpaqueWidget::onHide(e);
		clearChildren();
	}
};


} // namespace SppPreview
} // namespace StoermelderPackOne