#include "Mb_selection_preview.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {


struct ModelPreviewWidget : widget::OpaqueWidget {
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

struct CablesPreviewWidget : widget::Widget {
	ModelPreviewWidget* outputBox = nullptr;
	ModelPreviewWidget* inputBox = nullptr;
	int outputId = 0;
	int inputId = 0;
	float modelBoxZoom = 1.f;
	NVGcolor cableColor = nvgRGB(200, 200, 200);

	void draw(const DrawArgs& args) override {
		if (!outputBox || !inputBox) return;
		if (cableColor.a <= 0.0f) return;

		// Recompute each frame so positions follow zoom changes
		math::Vec outputPos = outputBox->getPortPos(true, outputId);
		math::Vec inputPos = inputBox->getPortPos(false, inputId);

		float thickness = 2.0f * modelBoxZoom;
		math::Vec slump = getSlumpPos(outputPos, inputPos, modelBoxZoom);

		// Draw cable
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, VEC_ARGS(outputPos));
		nvgQuadTo(args.vg, VEC_ARGS(slump), VEC_ARGS(inputPos));
		nvgStrokeColor(args.vg, cableColor);
		nvgStrokeWidth(args.vg, thickness);
		nvgStroke(args.vg);

		float plugOuter = 6.0f * modelBoxZoom;
		float plugInner = 4.0f * modelBoxZoom;

		// Draw output plug centered at cable endpoint
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(outputPos), plugOuter);
		nvgFillColor(args.vg, cableColor);
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(outputPos), plugInner);
		nvgFillColor(args.vg, nvgRGB(40, 40, 40));
		nvgFill(args.vg);

		// Draw input plug centered at cable endpoint
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(inputPos), plugOuter);
		nvgFillColor(args.vg, cableColor);
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, VEC_ARGS(inputPos), plugInner);
		nvgFillColor(args.vg, nvgRGB(40, 40, 40));
		nvgFill(args.vg);
	}

	static math::Vec getSlumpPos(math::Vec pos1, math::Vec pos2, float zoom = 1.f) {
		float dist = pos1.minus(pos2).norm();
		math::Vec avg = pos1.plus(pos2).div(2);
		avg.y += 0.5f * (150.0f + dist) * zoom;
		return avg;
	}
};


bool SelectionPreviewWidget::setSelection(std::string fileId, json_t* rootJ) {
	if (this->fileId == fileId) {
		return true;
	}
	if (this->rootJ) {
		json_decref(this->rootJ);
		this->rootJ = nullptr;
		clearChildren();
	}
	this->fileId = fileId;
	this->rootJ = rootJ;
	createPreview();
	refreshPreview();
	return true;
}

void SelectionPreviewWidget::clearSelection() {
	if (this->rootJ) {
		json_decref(this->rootJ);
		this->rootJ = nullptr;
	}
	fileId = "";
	clearChildren();
}

void SelectionPreviewWidget::fitPreviewToBox() {
	if (children.empty()) return;
	if (box.size.x <= 0 || box.size.y <= 0) return;

	// Eagerly create previews so we know actual sizes
	for (widget::Widget* child : children) {
		ModelPreviewWidget* modelBox = dynamic_cast<ModelPreviewWidget*>(child);
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
		ModelPreviewWidget* modelBox = dynamic_cast<ModelPreviewWidget*>(child);
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
		ModelPreviewWidget* modelBox = dynamic_cast<ModelPreviewWidget*>(child);
		if (modelBox) {
			modelBox->modelBoxZoom = scale;
			modelBox->modelBoxZoomApplied = scale; // Prevent step() from overwriting
			modelBox->sizePreview();
			// Apply centering offset in screen coordinates
			modelBox->box.pos = modelBox->box.pos.plus(math::Vec(scaledContentOffsetX, scaledContentOffsetY));
		}
		CablesPreviewWidget* cableBox = dynamic_cast<CablesPreviewWidget*>(child);
		if (cableBox) {
			cableBox->box.size = box.size;
			cableBox->modelBoxZoom = scale;
		}
	}
}

void SelectionPreviewWidget::refreshPreview() {
	// Reset lastBoxSize so next step() will fit with current box size
	lastBoxSize = math::Vec(-1, -1);
}

void SelectionPreviewWidget::createPreview() {
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

		ModelPreviewWidget* modelBox = new ModelPreviewWidget;
		modelBox->setModel(model);
		modelBox->modelOpacity = modelOpacity;
		modelBox->originalPos = Vec(x - minX, y - minY).mult(RACK_GRID_SIZE);
		modelBox->box.pos = modelBox->originalPos;
		modelBox->moduleId = moduleId;
		addChild(modelBox);
	}

	// Create cable previews
	json_t* cablesJ = json_object_get(rootJ, "cables");
	json_t* cableJ;
	size_t cableIndex;
	json_array_foreach(cablesJ, cableIndex, cableJ) {
		int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
		int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
		int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
		int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
		const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

		// Find the ModelBoxes for output and input modules
		ModelPreviewWidget* outputBox = NULL;
		ModelPreviewWidget* inputBox = NULL;
		for (widget::Widget* child : children) {
			ModelPreviewWidget* mb = dynamic_cast<ModelPreviewWidget*>(child);
			if (mb) {
				if (mb->moduleId == outputModuleId) outputBox = mb;
				if (mb->moduleId == inputModuleId) inputBox = mb;
			}
		}
		if (!outputBox || !inputBox) continue;

		CablesPreviewWidget* cableWidget = new CablesPreviewWidget;
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

void SelectionPreviewWidget::onButton(const ButtonEvent& e) {
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
		createContextMenu();
		e.consume(this);
		return;
	}
	if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
		APP->scene->browser->hide();
		auto selectionContainer =
			APP->scene->rack->getFirstDescendantOfType<SppPreview::SelectionPreviewContainer<Mb::BrowserOverlay>>();
		if (selectionContainer) {
			selectionContainer->showSelectionPreview(rootJ, [&]() {
				vcvsFromJson(rootJ, "stoermelder MB selection load");
				json_decref(rootJ);
				fileId = "";
			});
		}
		e.consume(this);
	}
}

void SelectionPreviewWidget::step() {
	OpaqueWidget::step();
	// Recalculate when our box size changes OR when file path changes
	if (box.size != lastBoxSize || lastFileId != fileId) {
		lastBoxSize = box.size;
		lastFileId = fileId;
		fitPreviewToBox();
	}
}

void SelectionPreviewWidget::draw(const DrawArgs& args) {
	Rect s = box.zeroPos().grow(10.f);
	nvgBeginPath(args.vg);
	nvgRect(args.vg, RECT_ARGS(s));
	nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 50));
	nvgFill(args.vg);
	nvgScissor(args.vg, RECT_ARGS(s));
	OpaqueWidget::draw(args);
	nvgResetScissor(args.vg);
}

void SelectionPreviewWidget::createContextMenu() {
	if (!browser) return;
	SelectionSource* src = browser->getSource();
	if (!src) return;
	SelectionSourceIndex* index = src->getIndex();
	if (!index) return;

	ui::Menu* menu = createMenu();

	if (src->isPatchSource()) {
		menu->addChild(createMenuItem("Replace current patch...", "", [this, src]() {
			const std::string path = src->getAbsoluteFilePath(fileId);
			// This is kind of hacky but interacting directly with rack::patch::Manager
			// is not supported, as we would need to include patch.hpp.
			const std::vector<std::string>& paths = {path};
			const Widget::PathDropEvent e(paths);
			APP->scene->onPathDrop(e);
		}));
		menu->addChild(new MenuSeparator);
	}

	struct FavoriteItem : MenuItem {
		SelectionSourceIndex* index;
		std::string fileId;
		bool isFavorite = false;
		void onAction(const event::Action& e) override {
			index->setFavorite(fileId, !isFavorite);
			isFavorite = !isFavorite;
			SelectionBrowser* browser = APP->scene->getFirstDescendantOfType<SelectionBrowser>();
			if (browser) browser->sidebar->loadContainer();
			e.unconsume();
		}
		void step() override {
			rightText = CHECKMARK(isFavorite);
			MenuItem::step();
		}
	};

	FavoriteItem* favItem = new FavoriteItem;
	favItem->text = "Favorite";
	favItem->index = index;
	favItem->fileId = fileId;
	favItem->isFavorite = index->isFavorite(fileId);
	favItem->disabled = index->isReadOnly();
	menu->addChild(favItem);

	menu->addChild(new MenuSeparator);

	menu->addChild(createMenuLabel("Custom Tags"));

	struct NewCustomTagField : ui::TextField {
		SelectionSourceIndex* index;
		std::string fileId;
		void onSelectKey(const event::SelectKey& e) override {
			if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
				std::string tag = string::trim(text);
				if (!tag.empty()) {
					index->addCustomTag(fileId, tag);
					SelectionBrowser* browser = APP->scene->getFirstDescendantOfType<SelectionBrowser>();
					if (browser) browser->sidebar->loadContainer();
				}
				ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
				if (overlay) overlay->requestDelete();
				e.consume(this);
				return;
			}
			if (!e.getTarget()) {
				ui::TextField::onSelectKey(e);
			}
		}
	};

	struct CustomTagItem : MenuItem {
		SelectionSourceIndex* index;
		std::string fileId;
		std::string tagName;
		bool hasTag = false;
		void onAction(const event::Action& e) override {
			if (hasTag)
				index->removeCustomTag(fileId, tagName);
			else
				index->addCustomTag(fileId, tagName);
			hasTag = !hasTag;
			SelectionBrowser* browser = APP->scene->getFirstDescendantOfType<SelectionBrowser>();
			if (browser) browser->sidebar->loadContainer();
			e.unconsume();
		}
		void step() override {
			rightText = CHECKMARK(hasTag);
			MenuItem::step();
		}
	};

	if (!index->isReadOnly()) {
		NewCustomTagField* ntf = new NewCustomTagField;
		ntf->box.size.x = 150.f;
		ntf->placeholder = "New tag...";
		ntf->index = index;
		ntf->fileId = fileId;
		menu->addChild(ntf);
		APP->event->setSelectedWidget(ntf);
	}

	auto unsortedTags = customTagsAll();
	std::vector<std::string> customTags(unsortedTags.begin(), unsortedTags.end());
	std::sort(customTags.begin(), customTags.end(), [](const std::string& a, const std::string& b) {
		return string::lowercase(a) < string::lowercase(b);
	});

	Rack::addGroupedMenuItems<std::string>(menu, customTags, 
		[index, this](const std::string& tagName) {
			CustomTagItem* t = new CustomTagItem;
			t->index = index;
			t->text = tagName;
			t->fileId = fileId;
			t->tagName = tagName;
			t->hasTag = index->hasCustomTag(fileId, tagName);
			t->disabled = index->isReadOnly();
			return t;
		}, 20
	);

	struct TagItem : ui::MenuItem {
		SelectionSourceIndex* index;
		std::string fileId;
		bool hasTag = false;
		void onAction(const event::Action& e) override {
			if (hasTag)
				index->removeTag(fileId, tagName);
			else
				index->addTag(fileId, tagName);
			hasTag = !hasTag;
			e.unconsume();
		}
		void step() override {
			rightText = CHECKMARK(hasTag);
			MenuItem::step();
		}
		std::string tagName;
	};

	menu->addChild(new MenuSeparator);
	menu->addChild(createMenuLabel("Tags"));

	// Build list of all predefined tags with their status
	std::vector<std::string> tags;
	for (int id = 0; id < (int)tag::tagAliases.size(); id++) {
		tags.push_back(tag::tagAliases[id][0]);
	}
	std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
		return string::lowercase(a) < string::lowercase(b);
	});

	Rack::addGroupedMenuItems<std::string>(menu, tags,
		[this, index](const std::string& tag) {
			TagItem* t = new TagItem;
			t->text = t->tagName = tag;
			t->index = index;
			t->fileId = fileId;
			t->hasTag = index->hasTag(fileId, tag);
			t->disabled = index->isReadOnly();
			return t;
		}
	);
}


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne