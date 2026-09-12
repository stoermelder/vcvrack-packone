#include "Mb_patch_preview.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace patch {


// Unscaled (zoom 1, box-relative) layout for one model, computed once by building a throwaway
// ModuleWidget purely for its geometry — never rendered, never added to any widget tree.
// Outputs and inputs are kept in separate vectors, indexed directly by the same portIndex
// getPortPos() is called with, so a lookup is just an index instead of a re-walk of getPorts()
// every time. panelWidth doubles as the box-sizing input everywhere ModelPreviewWidget needs a
// model's width (step(), sizePreview(), fitPreviewToBox()'s content bounds).
struct PortLayout {
	std::vector<math::Vec> outputs;
	std::vector<math::Vec> inputs;
	float panelWidth = -1.f;

	static const PortLayout& forModel(plugin::Model* model) {
		static std::unordered_map<plugin::Model*, PortLayout> cache;
		auto it = cache.find(model);
		if (it != cache.end()) return it->second;

		PortLayout layout;
		ModuleWidget* mw = model->createModuleWidget(NULL);
		// Some panels only finalize port positions during their first step() (dynamically
		// laid-out panels, SVG-driven port placement) — reading box.pos before that can cache
		// a stale (often zero) position forever.
		mw->step();
		layout.panelWidth = mw->box.size.x;
		for (PortWidget* port : mw->getPorts()) {
			math::Vec center = port->box.pos + port->box.size.div(2);
			if (port->type == engine::Port::OUTPUT) layout.outputs.push_back(center);
			else layout.inputs.push_back(center);
		}
		delete mw;

		return cache.emplace(model, std::move(layout)).first->second;
	}
};


struct ModelPreviewWidget : widget::OpaqueWidget {
	plugin::Model* model;
	widget::Widget* previewWidget;
	/** Lazily created */
	widget::FramebufferWidget* previewFb = NULL;
	widget::ZoomWidget* zoomWidget = NULL;
	float modelBoxZoom = -1.f;
	float modelBoxZoomApplied = -1.f;
	float modelOpacity = 1.f;
	math::Vec originalPos; // Original position in RACK_GRID_SIZE units
	// Centering offset from fitPreviewToBox(), in screen (post-scale) pixels. Must be re-applied
	// by every later recomputation of box.pos from originalPos (step(), sizePreview()) — otherwise
	// a box whose preview is lazily created after the fit (resetting modelBoxZoomApplied) snaps
	// back to its unscaled, uncentered position on the very next step(), which can push it outside
	// the preview bounds entirely.
	math::Vec contentOffset;
	int64_t moduleId = -1;

	/** Get the position of a port center in the parent (PatchPreview) coordinate space.
	Returns false (and leaves `out` untouched) if the model has no such port — e.g. the patch
	references a port index the resolved model doesn't have, such as after a plugin update
	changed its port count. Callers must skip drawing rather than fall back to (0, 0). */
	bool getPortPos(bool isOutput, int portIndex, math::Vec& out) {
		const PortLayout& layout = PortLayout::forModel(model);
		const std::vector<math::Vec>& ports = isOutput ? layout.outputs : layout.inputs;
		if (portIndex < 0 || (size_t)portIndex >= ports.size()) return false;

		// Port-local position is unscaled; multiply by zoom before adding the scaled box origin
		out = ports[portIndex].mult(modelBoxZoom).plus(this->box.pos);
		return true;
	}

	// The model's panel width, from PortLayout: a cheap, cached, unrendered ModuleWidget's
	// geometry, available immediately regardless of whether this box's own preview (a real
	// FramebufferWidget render) has been built yet.
	float panelWidth() const {
		return PortLayout::forModel(model).panelWidth;
	}

	void setModel(plugin::Model* model) {
		this->model = model;
		previewWidget = new widget::TransparentWidget;
		addChild(previewWidget);
	}

	void step() override {
		if (modelBoxZoom != modelBoxZoomApplied) {
			modelBoxZoomApplied = modelBoxZoom;
			// Mirrors sizePreview() exactly (no separate ceil()'d size here): a ceil() used to
			// round this box's size up independently of the unrounded size fitPreviewToBox()
			// accounted for when computing contentWidth/contentHeight, letting the rightmost or
			// bottommost box's rounded-up edge exceed the fitted bound by up to ~1px.
			previewWidget->box.size.y = RACK_GRID_HEIGHT * modelBoxZoom;
			sizePreview();
#ifdef MB_PATCH_PREVIEW_DEBUG
			fprintf(stderr, "[MbPatchPreview] step() re-sized moduleId=%lld pos=(%.2f,%.2f) size=(%.2f,%.2f) offset=(%.2f,%.2f)\n",
				(long long)moduleId, box.pos.x, box.pos.y, box.size.x, box.size.y,
				contentOffset.x, contentOffset.y);
#endif
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
		// modelBoxZoom is deliberately left untouched: fitPreviewToBox() may already have set
		// it to the real fitted scale before this preview existed (box sizing/layout no longer
		// needs the preview built — see panelWidth()), so resetting it here would discard that
		// the moment each box's preview is lazily built on first draw(). Only
		// modelBoxZoomApplied resets, so step()/sizePreview() (below) re-applies it to the
		// newly-built subtree.
		modelBoxZoomApplied = -1.f;

		sizePreview();
	}

	// Sizes and positions this box for the current modelBoxZoom. The box's own geometry
	// (box.size, box.pos) is set unconditionally, since panelWidth() (via PortLayout) no
	// longer needs a live preview to know the model's width — only the live subtree itself
	// (zoomWidget's actual zoom, the framebuffer's dirty flag) is conditional on having been
	// created. A version of this that skipped box.pos whenever zoomWidget was still null used
	// to leave every not-yet-drawn box at its unscaled original position, which
	// fitPreviewToBox()'s later centering-offset step then added to instead of replacing —
	// placing correctly-*sized* but wrongly-*positioned* boxes far outside the preview.
	void sizePreview() {
		box.size.x = panelWidth() * modelBoxZoom;
		box.size.y = RACK_GRID_HEIGHT * modelBoxZoom;
		box.pos = originalPos.mult(modelBoxZoom).plus(contentOffset);
		if (!zoomWidget) return;
		zoomWidget->setZoom(modelBoxZoom);
		previewFb->setDirty();
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

		// Recompute each frame so positions follow zoom changes. Either side can fail to
		// resolve (e.g. the patch references a port index the resolved model no longer has,
		// after a plugin update) — skip the cable rather than draw a stub to (0, 0).
		math::Vec outputPos, inputPos;
		if (!outputBox->getPortPos(true, outputId, outputPos)) return;
		if (!inputBox->getPortPos(false, inputId, inputPos)) return;

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


bool PreviewWidget::setPatch(std::string fileId, json_t* rootJ) {
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
	missingModels.clear();
	createPreview();
	refreshPreview();
	return true;
}

void PreviewWidget::clearPatch() {
	if (this->rootJ) {
		json_decref(this->rootJ);
		this->rootJ = nullptr;
	}
	fileId = "";
	missingModels.clear();
	clearChildren();
}

void PreviewWidget::fitPreviewToBox() {
	if (children.empty()) return;
	if (box.size.x <= 0 || box.size.y <= 0) return;

	// Calculate content bounds from children (ModelBoxes). PortLayout (via panelWidth())
	// supplies each model's width without needing its actual preview built — box sizing/layout
	// no longer forces every model's preview into existence up front; each box still creates
	// its own lazily, the first time it's actually drawn (sizePreview() below is a no-op until
	// then, guarded on zoomWidget being non-null).
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
			contentMaxX = std::max(contentMaxX, modelBox->originalPos.x + modelBox->panelWidth());
			contentMaxY = std::max(contentMaxY, modelBox->originalPos.y + RACK_GRID_HEIGHT);
		}
	}

	contentWidth = contentMaxX - contentMinX;
	contentHeight = contentMaxY - contentMinY;
	contentCached = true;
	fitted = true;

#ifdef MB_PATCH_PREVIEW_DEBUG
	fprintf(stderr, "[MbPatchPreview] fitPreviewToBox: box.size=(%.2f,%.2f) content=(%.2f,%.2f) min=(%.2f,%.2f) max=(%.2f,%.2f)\n",
		box.size.x, box.size.y, contentWidth, contentHeight, contentMinX, contentMinY, contentMaxX, contentMaxY);
#endif

	float scaleX = box.size.x / contentWidth;
	float scaleY = box.size.y / contentHeight;
	float scale = std::min(scaleX, scaleY);
	scale = std::min(scale, 1.f); // Only scale down, never up

	// Center offset for content
	scaledContentOffsetX = (box.size.x - contentWidth * scale) / 2.f;
	scaledContentOffsetY = (box.size.y - contentHeight * scale) / 2.f;

	// contentMinX/Y is the origin of the surviving children's bounding box, which is only ever
	// (0,0) when every module in the patch resolved to a known model. createPreview() normalizes
	// originalPos against the *full patch's* leftmost/topmost module (including ones later
	// skipped as missing), so a patch with any missing module leaves a corresponding gap:
	// originalPos.mult(scale) alone lands each surviving box at its scaled position *relative to
	// the whole patch*, not relative to what's actually being fit into the box — every box ends
	// up shifted by exactly that gap, scaled. Folding -contentMin*scale into the same per-box
	// offset as the centering term corrects this without changing originalPos itself (still
	// needed unscaled, as-is, for content-bounds math above).
	math::Vec contentMin(contentMinX, contentMinY);

	// Apply zoom to all ModelBox children
	for (widget::Widget* child : children) {
		ModelPreviewWidget* modelBox = dynamic_cast<ModelPreviewWidget*>(child);
		if (modelBox) {
			modelBox->modelBoxZoom = scale;
			modelBox->modelBoxZoomApplied = scale; // Prevent step() from overwriting
			// Stored on the box so later recomputations of box.pos from originalPos (step(),
			// a lazily-created preview's sizePreview()) stay centered instead of snapping back
			// to the unscaled, uncentered position.
			modelBox->contentOffset = math::Vec(scaledContentOffsetX, scaledContentOffsetY)
				.minus(contentMin.mult(scale));
			modelBox->sizePreview();
		}
#ifdef MB_PATCH_PREVIEW_DEBUG
		if (modelBox) {
			float right = modelBox->box.pos.x + modelBox->box.size.x;
			float bottom = modelBox->box.pos.y + modelBox->box.size.y;
			bool outOfBounds = right > box.size.x + 0.5f || bottom > box.size.y + 0.5f
				|| modelBox->box.pos.x < -0.5f || modelBox->box.pos.y < -0.5f;
			fprintf(stderr, "[MbPatchPreview] box moduleId=%lld pos=(%.2f,%.2f) size=(%.2f,%.2f) right=%.2f bottom=%.2f%s\n",
				(long long)modelBox->moduleId, modelBox->box.pos.x, modelBox->box.pos.y,
				modelBox->box.size.x, modelBox->box.size.y, right, bottom,
				outOfBounds ? "  <-- OUT OF BOUNDS" : "");
		}
#endif
		CablesPreviewWidget* cableBox = dynamic_cast<CablesPreviewWidget*>(child);
		if (cableBox) {
			cableBox->box.size = box.size;
			cableBox->modelBoxZoom = scale;
		}
	}
}

void PreviewWidget::refreshPreview() {
	lastBoxSize = math::Vec(-1, -1);
	fitted = false;
}

void PreviewWidget::createPreview() {
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ) return;

	json_t* moduleJ;
	size_t moduleIndex;

	double minX = std::numeric_limits<double>::infinity();
	double minY = std::numeric_limits<double>::infinity();
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		Vec pos;
		vcv::readModulePos(moduleJ, pos);
		minX = std::min(minX, (double)pos.x);
		minY = std::min(minY, (double)pos.y);
	}

	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		Vec pos;
		vcv::readModulePos(moduleJ, pos);
		double x = pos.x, y = pos.y;

		vcv::ModuleRef ref;
		if (!vcv::readModuleRef(moduleJ, ref)) continue;
		const std::string& pluginSlug = ref.pluginSlug;
		const std::string& modelSlug = ref.modelSlug;

		plugin::Model* model = plugin::getModel(pluginSlug, modelSlug);
		if (!model) {
			WARN("Model not found: %s/%s", pluginSlug.c_str(), modelSlug.c_str());
			// Construct display name: show as "PluginName ModelName"
			// If modelSlug starts with pluginSlug + separator, strip the prefix and replace separator with space
			// e.g., "Fundamental-SEQ3" from plugin "Fundamental" -> "Fundamental SEQ3"
			// Otherwise just use "PluginName ModelName"
			std::string displayName;
			if (modelSlug.find(pluginSlug) == 0 && modelSlug.size() > pluginSlug.size()) {
				char sep = modelSlug[pluginSlug.size()];
				if (sep == '-' || sep == '_') {
					std::string remainder = modelSlug.substr(pluginSlug.size() + 1);
					displayName = pluginSlug + " " + remainder;
				} 
				else {
					displayName = pluginSlug + " " + modelSlug;
				}
			} 
			else {
				displayName = pluginSlug + " " + modelSlug;
			}
			// Store with displayName as key (dedup) and full slug as value (for URL)
			missingModels[displayName] = pluginSlug + "/" + modelSlug;
			continue;
		}

		// Get module ID from JSON
		int64_t moduleId = vcv::readModuleId(moduleJ);

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

void PreviewWidget::step() {
	OpaqueWidget::step();
	// Recalculate when our box size changes OR when file path changes
	if (box.size != lastBoxSize || lastFileId != fileId) {
		lastBoxSize = box.size;
		lastFileId = fileId;
		fitPreviewToBox();
	}
}

void PreviewWidget::draw(const DrawArgs& args) {
	Rect s = box.zeroPos().grow(10.f);
	nvgBeginPath(args.vg);
	nvgRect(args.vg, RECT_ARGS(s));
	nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 50));
	nvgFill(args.vg);
	if (!fitted) return;
	nvgScissor(args.vg, RECT_ARGS(s));
	OpaqueWidget::draw(args);
	nvgResetScissor(args.vg);
}

void PreviewWidget::onButton(const ButtonEvent& e) {
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
		createContextMenu(fileId);
		e.consume(this);
		return;
	}
	if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
		invokePatchAdding();
		e.consume(this);
	}
}

void PreviewWidget::invokePatchAdding() {
	APP->scene->browser->hide();
	auto c = APP->scene->rack->getFirstDescendantOfType<SppPreview::PatchPreviewContainer<Mb::BrowserOverlay>>();
	if (!c) return;
	c->showPatchPreview(rootJ, [&]() {
		vcv::vcvsFromJson(rootJ, "stoermelder MB patch load");
		json_decref(rootJ);
		fileId = "";
	});
}

void PreviewWidget::createContextMenu(std::string fileId) {
	if (!browser) return;
	PatchSource* src = browser->getSource();
	if (!src) return;
	PatchSourceIndex* index = src->getIndex();
	if (!index) return;

	ui::Menu* menu = createMenu();

	src->appendPreviewMenuItems(menu, fileId);

	if (src->isPatchSource()) {
		if (menu->children.size() > 0) {
			menu->addChild(new MenuSeparator);
		}
		menu->addChild(createMenuItem("Replace current patch", "", [fileId, src]() {
			const std::string path = src->getTempFilePath(fileId);
			auto helper = PatchHelperWidget::getInstance();
			if (helper) {
				helper->setPendingPatchPath(path);
			}
		}));
	}

	if (index->isReadOnly()) return;

	menu->addChild(new MenuSeparator);
	struct FavoriteItem : MenuItem {
		PatchSourceIndex* index;
		std::string fileId;
		bool isFavorite = false;
		void onAction(const event::Action& e) override {
			index->setFavorite(fileId, !isFavorite);
			isFavorite = !isFavorite;
			Browser* browser = APP->scene->getFirstDescendantOfType<Browser>();
			browser->sidebar->refreshFileList();
			browser->sidebar->refreshDescriptionAndTags();
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
		PatchSourceIndex* index;
		std::string fileId;
		void onSelectKey(const event::SelectKey& e) override {
			if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
				std::string tag = string::trim(text);
				if (!tag.empty()) {
					index->addCustomTag(fileId, tag);
					Browser* browser = APP->scene->getFirstDescendantOfType<Browser>();
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
		PatchSourceIndex* index;
		std::string fileId;
		std::string tagName;
		bool hasTag = false;
		void onAction(const event::Action& e) override {
			if (hasTag)
				index->removeCustomTag(fileId, tagName);
			else
				index->addCustomTag(fileId, tagName);
			hasTag = !hasTag;
			Browser* browser = APP->scene->getFirstDescendantOfType<Browser>();
			browser->sidebar->refreshFileList();
			browser->sidebar->refreshDescriptionAndTags();
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
		[fileId, index](const std::string& tagName) {
			CustomTagItem* t = new CustomTagItem;
			t->index = index;
			t->text = tagName;
			t->fileId = fileId;
			t->tagName = tagName;
			t->hasTag = index->hasCustomTag(fileId, tagName);
			return t;
		}, 20
	);

	struct TagItem : ui::MenuItem {
		PatchSourceIndex* index;
		std::string fileId;
		bool hasTag = false;
		void onAction(const event::Action& e) override {
			if (hasTag)
				index->removeTag(fileId, tagName);
			else
				index->addTag(fileId, tagName);
			hasTag = !hasTag;
			Browser* browser = APP->scene->getFirstDescendantOfType<Browser>();
			browser->sidebar->refreshFileList();
			browser->sidebar->refreshDescriptionAndTags();
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
		[fileId, index](const std::string& tag) {
			TagItem* t = new TagItem;
			t->text = t->tagName = tag;
			t->index = index;
			t->fileId = fileId;
			t->hasTag = index->hasTag(fileId, tag);
			return t;
		}
	);
}


const std::map<std::string, std::string>& PreviewWidget::getMissingModels() {
	return missingModels;
}


} // namespace patch
} // namespace Mb
} // namespace StoermelderPackOne