#pragma once
#include "../../plugin.hpp"
#include "../strip/vcvs_helpers.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {


struct ModelBox : widget::OpaqueWidget {
	plugin::Model* model;
	widget::Widget* previewWidget;
	/** Lazily created */
	widget::FramebufferWidget* previewFb = NULL;
	widget::ZoomWidget* zoomWidget = NULL;
	float modelBoxZoom = -1.f;
	float modelBoxZoomApplied = -1.f;
	float modelBoxWidth = -1.f;
	float modelOpacity = 1.f;
	math::Vec originalPos; // Original position in RACK_GRID_SIZE units
	int64_t moduleId = -1;

	/** Get the position of a port center in the parent (SelectionPreview) coordinate space */
	math::Vec getPortPos(bool isOutput, int portIndex) {
		if (!previewFb || previewFb->children.empty()) return math::Vec(0, 0);
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(*previewFb->children.begin());
		if (!mw) return math::Vec(0, 0);

		auto ports = mw->getPorts();
		int outputCount = 0;
		int inputCount = 0;
		for (PortWidget* port : ports) {
			bool match = false;
			if (port->type == engine::Port::OUTPUT) {
				match = isOutput && outputCount == portIndex;
				outputCount++;
			} else {
				match = !isOutput && inputCount == portIndex;
				inputCount++;
			}
			if (match) {
				// Port-local position is unscaled; multiply by zoom before adding the scaled box origin
				math::Vec portCenter = (port->box.pos + port->box.size.div(2)).mult(modelBoxZoom);
				return portCenter.plus(this->box.pos);
			}
		}
		return math::Vec(0, 0);
	}

	void setModel(plugin::Model* model) {
		this->model = model;
		previewWidget = new widget::TransparentWidget;
		addChild(previewWidget);
	}

	void step() override {
		if (modelBoxZoom != modelBoxZoomApplied) {
			modelBoxZoomApplied = modelBoxZoom;
			box.size.x = (modelBoxWidth < 0 ? 10 * RACK_GRID_WIDTH : modelBoxWidth) * modelBoxZoom;
			box.size.y = RACK_GRID_HEIGHT * modelBoxZoom;
			box.size = box.size.ceil();

			previewWidget->box.size.y = std::ceil(RACK_GRID_HEIGHT * modelBoxZoom);
			// Use original position scaled by zoom
			box.pos = originalPos.mult(modelBoxZoom);
			if (previewFb) sizePreview();
		}
		widget::OpaqueWidget::step();
	}

	void createPreview() {
		zoomWidget = new widget::ZoomWidget;
		previewWidget->addChild(zoomWidget);

		previewFb = new widget::FramebufferWidget;
		if (math::isNear(APP->window->pixelRatio, 1.0)) {
			previewFb->oversample = 2.0;
		}
		zoomWidget->addChild(previewFb);

		ModuleWidget* moduleWidget = model->createModuleWidget(NULL);
		previewFb->addChild(moduleWidget);
		modelBoxWidth = moduleWidget->box.size.x;
		modelBoxZoom = 1.f;
		modelBoxZoomApplied = -1.f; // Reset so step() will apply the zoom

		sizePreview();
	}

	void sizePreview() {
		if (!zoomWidget) return;
		zoomWidget->setZoom(modelBoxZoom);
		previewFb->setDirty();
		box.size.x = modelBoxWidth * modelBoxZoom;
		box.size.y = RACK_GRID_HEIGHT * modelBoxZoom;
		box.pos = originalPos.mult(modelBoxZoom);
	}

	void deletePreview() {
		if (!previewFb) return;
		previewWidget->removeChild(previewFb);
		delete previewFb;
		previewFb = NULL;
	}

	void draw(const DrawArgs& args) override {
		if (!previewFb) {
			createPreview();
		}

		float b = math::clamp(settings::rackBrightness + 0.2f, 0.f, 1.f);
		nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, modelOpacity));

		widget::OpaqueWidget::draw(args);
	}
};


struct PreviewCableWidget : widget::Widget {
	ModelBox* outputBox = nullptr;
	ModelBox* inputBox = nullptr;
	int outputId = 0;
	int inputId = 0;
	NVGcolor cableColor = nvgRGB(200, 200, 200);

	void draw(const DrawArgs& args) override {
		if (!outputBox || !inputBox) return;
		if (cableColor.a <= 0.0f) return;

		// Recompute each frame so positions follow zoom changes
		math::Vec outputPos = outputBox->getPortPos(true, outputId);
		math::Vec inputPos = inputBox->getPortPos(false, inputId);

		float thickness = 2.0f;
		math::Vec slump = getSlumpPos(outputPos, inputPos);

		// Draw cable
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, VEC_ARGS(outputPos));
		nvgQuadTo(args.vg, VEC_ARGS(slump), VEC_ARGS(inputPos));
		nvgStrokeColor(args.vg, cableColor);
		nvgStrokeWidth(args.vg, thickness);
		nvgStroke(args.vg);

		// Draw output plug centered at cable endpoint
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(outputPos), 6.0f);
		nvgFillColor(args.vg, cableColor);
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(outputPos), 4.0f);
		nvgFillColor(args.vg, nvgRGB(40, 40, 40));
		nvgFill(args.vg);

		// Draw input plug centered at cable endpoint
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(inputPos), 6.0f);
		nvgFillColor(args.vg, cableColor);
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(inputPos), 4.0f);
		nvgFillColor(args.vg, nvgRGB(40, 40, 40));
		nvgFill(args.vg);
	}

	static math::Vec getSlumpPos(math::Vec pos1, math::Vec pos2) {
		float dist = pos1.minus(pos2).norm();
		math::Vec avg = pos1.plus(pos2).div(2);
		avg.y += 0.5f * (150.0f + dist);
		return avg;
	}
};


struct SelectionPreview : OpaqueWidget {
	std::string filePath;
	float modelOpacity = 1.f;
	math::Vec lastBoxSize;
	std::string lastFilePath;
	
	// Cached content dimensions for resize handling
	float contentWidth = 0.f;
	float contentHeight = 0.f;
	bool contentCached = false;

	// Center offset for scaled content
	float scaledContentOffsetX = 0.f;
	float scaledContentOffsetY = 0.f;

	SppPreview::SelectionPreviewContainer* selectionContainer;

	SelectionPreview(SppPreview::SelectionPreviewContainer* c) {
		selectionContainer = c;
	}

	void fitPreviewToBox() {
		if (children.empty()) return;
		if (box.size.x <= 0 || box.size.y <= 0) return;

		// Eagerly create previews so we know actual sizes
		for (widget::Widget* child : children) {
			ModelBox* modelBox = dynamic_cast<ModelBox*>(child);
			if (modelBox && !modelBox->previewFb) {
				modelBox->createPreview();
			}
		}

		// Calculate content bounds from children (ModelBoxes)
		float contentMinX = std::numeric_limits<float>::infinity();
		float contentMinY = std::numeric_limits<float>::infinity();
		float contentMaxX = -std::numeric_limits<float>::infinity();
		float contentMaxY = -std::numeric_limits<float>::infinity();

		for (widget::Widget* child : children) {
			ModelBox* modelBox = dynamic_cast<ModelBox*>(child);
			if (modelBox) {
				// Use original positions to calculate content bounds
				contentMinX = std::min(contentMinX, modelBox->originalPos.x);
				contentMinY = std::min(contentMinY, modelBox->originalPos.y);
				contentMaxX = std::max(contentMaxX, modelBox->originalPos.x + modelBox->modelBoxWidth);
				contentMaxY = std::max(contentMaxY, modelBox->originalPos.y + RACK_GRID_HEIGHT);
			}
		}

		contentWidth = contentMaxX - contentMinX;
		contentHeight = contentMaxY - contentMinY;
		contentCached = true;

		float scaleX = box.size.x / contentWidth;
		float scaleY = box.size.y / contentHeight;
		float scale = std::min(scaleX, scaleY);
		scale = std::min(scale, 1.f); // Only scale down, never up

		// Center offset for content
		scaledContentOffsetX = (box.size.x - contentWidth * scale) / 2.f;
		scaledContentOffsetY = (box.size.y - contentHeight * scale) / 2.f;

		// Apply zoom to all ModelBox children
		for (widget::Widget* child : children) {
			ModelBox* modelBox = dynamic_cast<ModelBox*>(child);
			if (modelBox) {
				modelBox->modelBoxZoom = scale;
				modelBox->modelBoxZoomApplied = scale; // Prevent step() from overwriting
				modelBox->sizePreview();
				// Apply centering offset in screen coordinates
				modelBox->box.pos = modelBox->box.pos.plus(math::Vec(scaledContentOffsetX, scaledContentOffsetY));
			}
		}
	}

	void refreshPreview() {
		// Reset lastBoxSize so next step() will fit with current box size
		lastBoxSize = math::Vec(-1, -1);
	}

	void step() override {
		OpaqueWidget::step();
		// Recalculate when our box size changes OR when file path changes
		if (box.size != lastBoxSize || lastFilePath != filePath) {
			lastBoxSize = box.size;
			lastFilePath = filePath;
			fitPreviewToBox();
		}
	}

	bool loadSelectionFile(std::string path) {
		FILE* file = std::fopen(path.c_str(), "r");
		if (!file)
			return false;
		DEFER({ std::fclose(file); });

		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		if (!rootJ)
			return false;
		DEFER({ json_decref(rootJ); });
		filePath = path;
		createPreview(rootJ);
		refreshPreview();
		// Create cables after preview is stepped/positioned
		createCables(rootJ);
		return true;
	}

	void createPreview(json_t* rootJ) {
		json_t* modulesJ = json_object_get(rootJ, "modules");
		if (!modulesJ) return;

		json_t* moduleJ;
		size_t moduleIndex;

		double minX = std::numeric_limits<double>::infinity();
		double minY = std::numeric_limits<double>::infinity();
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

			json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
			if (!pluginSlugJ) continue;
			json_t* modelSlugJ = json_object_get(moduleJ, "model");
			if (!modelSlugJ) continue;
			std::string pluginSlug = json_string_value(pluginSlugJ);
			std::string modelSlug = json_string_value(modelSlugJ);

			plugin::Model* model = plugin::getModel(pluginSlug, modelSlug);
			if (!model) {
				WARN("Model not found: %s/%s", pluginSlug.c_str(), modelSlug.c_str());
				continue;
			}

			// Get module ID from JSON
			json_t* idJ = json_object_get(moduleJ, "id");
			int64_t moduleId = idJ ? json_integer_value(idJ) : -1;

			INFO("Creating ModelBox for %s/%s", pluginSlug.c_str(), modelSlug.c_str());

			ModelBox* modelBox = new ModelBox;
			modelBox->setModel(model);
			modelBox->modelOpacity = modelOpacity;
			modelBox->originalPos = Vec(x - minX, y - minY).mult(RACK_GRID_SIZE);
			modelBox->box.pos = modelBox->originalPos;
			modelBox->moduleId = moduleId;
			addChild(modelBox);
		}
	}

	// Create cable previews
	void createCables(json_t* rootJ) {
		json_t* cablesJ = json_object_get(rootJ, "cables");
		if (!cablesJ) return;

		json_t* cableJ;
		size_t cableIndex;
		json_array_foreach(cablesJ, cableIndex, cableJ) {
			int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
			int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
			int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
			int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
			const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

			// Find the ModelBoxes for output and input modules
			ModelBox* outputBox = NULL;
			ModelBox* inputBox = NULL;
			for (widget::Widget* child : children) {
				ModelBox* mb = dynamic_cast<ModelBox*>(child);
				if (mb) {
					if (mb->moduleId == outputModuleId) outputBox = mb;
					if (mb->moduleId == inputModuleId) inputBox = mb;
				}
			}
			if (!outputBox || !inputBox) continue;

			PreviewCableWidget* cableWidget = new PreviewCableWidget;
			cableWidget->outputBox = outputBox;
			cableWidget->inputBox = inputBox;
			cableWidget->outputId = outputId;
			cableWidget->inputId = inputId;
			if (colorStr) {
				cableWidget->cableColor = color::fromHexString(colorStr);
			}
			// Cover the full preview area so draw() can use absolute coords
			cableWidget->box.pos = math::Vec(0, 0);
			cableWidget->box.size = box.size;
			addChild(cableWidget);
		}
	}

	void onHide(const HideEvent& e) override {
		OpaqueWidget::onHide(e);
		clearChildren();
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			APP->scene->browser->hide();
			selectionContainer->showSelectionPreview(filePath, [&]() {
				vcvsLoadFile(filePath);
			});
			e.consume(this);
		}
	}
};


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne
