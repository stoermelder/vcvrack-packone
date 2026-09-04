#include "../../plugin.hpp"
#include "../../vcv/fs.hpp"
#include "../../vcv/selection.hpp"

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
	bool loadSelectionFile(std::string path) {
		std::string data;
		if (!vcv::fs::read(path, data)) return false;
		INFO("Loading selection %s", path.c_str());

		std::string error;
		json_t* rootJ = vcv::parseJson(data, error);
		if (!rootJ) {
			throw Exception("File is not a valid selection file. %s", error.c_str());
		}
		DEFER({json_decref(rootJ);});
		return (createPreview(rootJ) > 0);
	}

	int createPreview(json_t* rootJ) {
		// The same layout the loader itself uses, so the preview lands exactly where the
		// modules will (origin (0,0): the container positions itself at the mouse).
		auto placements = vcv::layoutSelection(json_object_get(rootJ, "modules"), Vec(0.f, 0.f));

		int i = 0;
		for (const vcv::ModulePlacement& p : placements) {
			vcv::ModuleRef ref;
			if (!vcv::readModuleRef(p.moduleJ, ref)) continue;

			// Get Model
			plugin::Model* model = plugin::getModel(ref.pluginSlug, ref.modelSlug);
			if (!model) continue;

			ModelBox* modelBox = new ModelBox;
			modelBox->setModel(model);
			modelBox->box.pos = p.pos;
			addChild(modelBox);
			i++;
		}
		return i;
	}

	void onHide(const HideEvent& e) override {
		OpaqueWidget::onHide(e);
		clearChildren();
	}
};


} // namespace SppPreview
} // namespace StoermelderPackOne