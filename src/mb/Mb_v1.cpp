#include "Mb_v1.hpp"
#include <tag.hpp>
#include <thread>

namespace StoermelderPackOne {
namespace Mb {
namespace v1 {

enum class ModuleBrowserSort {
	DEFAULT = 0,
	NAME = 1,
	LAST_USED = 2,
	MOST_USED = 3,
	RANDOM = 4
};

float modelBoxZoom = 0.9f;
int modelBoxSort = (int)ModuleBrowserSort::DEFAULT;
bool hideBrands = false;


// Static functions

static float modelScore(plugin::Model* model, const std::string& search) {
	if (search.empty())
		return 1.f;
	std::string s;
	s += model->plugin->brand;
	s += " ";
	s += model->plugin->name;
	s += " ";
	s += model->name;
	s += " ";
	s += model->slug;
	for (int tagId : model->tags) {
		// Add all aliases of a tag
		for (const std::string& alias : rack::tag::tagAliases[tagId]) {
			s += " ";
			s += alias;
		}
	}
	float score = string::fuzzyScore(string::lowercase(s), string::lowercase(search));
	return score;
}

static bool isModelVisible(plugin::Model* model, const std::string& search, const bool& favourite, const std::string& brand, const std::set<int>& tagId, const bool& hidden) {
	// Filter search query
	if (search != "") {
		float score = modelScore(model, search);
		if (score <= 0.f)
			return false;
	}

	// Filter favorite
	if (favourite) {
		auto it = favoriteModels.find(model);
		if (it == favoriteModels.end())
			return false;
	}

	// Filter brand
	if (brand != "") {
		if (model->plugin->brand != brand)
			return false;
	}

	// Filter tag
	if (tagId.size() > 0) {
			for (auto t : tagId) {
			auto it = std::find(model->tags.begin(), model->tags.end(), t);
			if (it == model->tags.end())
				return false;
		}
	}

	// Filter hidden
	if (!hidden) {
		auto it = hiddenModels.find(model);
		if (it != hiddenModels.end())
			return false;
	}

	return true;
}

static void toggleModelFavorite(Model* model) {
	auto it = favoriteModels.find(model);
	if (it != favoriteModels.end()) 
		favoriteModels.erase(model);
	else 
		favoriteModels.insert(model);
	hiddenModels.erase(model);

	ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
	if (browser->favorites) {
		browser->refresh(false);
	} 
}

static void toggleModelHidden(Model* model) {
	auto it = hiddenModels.find(model);
	if (it != hiddenModels.end()) 
		hiddenModels.erase(model);
	else 
		hiddenModels.insert(model);

	ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
	browser->refresh(false);
}

static bool isModelHidden(plugin::Model* model) {
	return hiddenModels.find(model) != hiddenModels.end();
}

static ModuleWidget* chooseModel(plugin::Model* model) {
	// Create module
	ModuleWidget* moduleWidget = model->createModuleWidget();
	assert(moduleWidget);
	APP->scene->rack->addModuleAtMouse(moduleWidget);

	// Push ModuleAdd history action
	history::ModuleAdd* h = new history::ModuleAdd;
	h->name = "create module";
	h->setModule(moduleWidget);
	APP->history->push(h);

	// Hide Module Browser
	APP->scene->moduleBrowser->hide();

	// Update usage data
	modelUsageTouch(model);

	return moduleWidget;
}


// Widgets

ModelZoomSlider::ModelZoomSlider() {
	struct ModelZoomQuantity : Quantity {
		void setValue(float value) override {
			v1::modelBoxZoom = math::clamp(value, PREVIEW_MIN, PREVIEW_MAX);
		}
		float getValue() override {
			return v1::modelBoxZoom;
		}
		float getDefaultValue() override {
			return 0.9f;
		}
		float getDisplayValue() override {
			return getValue() * 100;
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue / 100);
		}
		std::string getLabel() override {
			return "Preview";
		}
		std::string getUnit() override {
			return "";
		}
		int getDisplayPrecision() override {
			return 3;
		}
		float getMaxValue() override {
			return PREVIEW_MAX;
		}
		float getMinValue() override {
			return PREVIEW_MIN;
		}
	};

	box.size.x = 180.0f;
	quantity = new ModelZoomQuantity();
}

ModelZoomSlider::~ModelZoomSlider() {
	delete quantity;
}


struct ModelBox : widget::OpaqueWidget {
	plugin::Model* model;
	widget::Widget* previewWidget;
	ui::Tooltip* tooltip = NULL;
	/** Lazily created */
	widget::FramebufferWidget* previewFb = NULL;
	widget::ZoomWidget* zoomWidget = NULL;
	float modelBoxZoom = -1.f;
	float modelBoxWidth = -1.f;
	bool modelHidden = false;

	void setModel(plugin::Model* model) {
		this->model = model;
		previewWidget = new widget::TransparentWidget;
		addChild(previewWidget);
	}

	void step() override {
		if (modelBoxZoom != v1::modelBoxZoom) {
			//deletePreview();
			modelBoxZoom = v1::modelBoxZoom;
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
		previewFb = new widget::FramebufferWidget;
		if (math::isNear(APP->window->pixelRatio, 1.0)) {
			// Small details draw poorly at low DPI, so oversample when drawing to the framebuffer
			previewFb->oversample = 2.0;
		}
		previewWidget->addChild(previewFb);

		zoomWidget = new widget::ZoomWidget;
		previewFb->addChild(zoomWidget);

		ModuleWidget* moduleWidget = model->createModuleWidgetNull();
		zoomWidget->addChild(moduleWidget);
		// Save the width, used for correct width of blank before rendered
		modelBoxWidth = moduleWidget->box.size.x;

		sizePreview();
	}

	void sizePreview() {
		zoomWidget->setZoom(modelBoxZoom);

		zoomWidget->box.size.x = modelBoxWidth * modelBoxZoom;
		zoomWidget->box.size.y = RACK_GRID_HEIGHT * modelBoxZoom;
		previewWidget->box.size.x = std::ceil(zoomWidget->box.size.x);
		previewWidget->box.size.y = std::ceil(zoomWidget->box.size.y);
		box.size = previewWidget->box.size;

		// Not sure how to do this correctly but works for now
		previewFb->fbBox.size = previewWidget->box.size;
		previewFb->dirty = true;
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

		// Draw shadow
		nvgBeginPath(args.vg);
		float r = 10; // Blur radius
		float c = 10; // Corner radius
		nvgRect(args.vg, -r, -r, box.size.x + 2 * r, box.size.y + 2 * r);
		NVGcolor shadowColor = nvgRGBAf(0, 0, 0, 0.5);
		NVGcolor transparentColor = nvgRGBAf(0, 0, 0, 0);
		nvgFillPaint(args.vg, nvgBoxGradient(args.vg, 0, 0, box.size.x, box.size.y, c, r, shadowColor, transparentColor));
		nvgFill(args.vg);

		if (modelHidden) {
			nvgGlobalAlpha(args.vg, 0.33);
		}
		OpaqueWidget::draw(args);
	}

	void setTooltip(ui::Tooltip* tooltip) {
		if (this->tooltip) {
			this->tooltip->requestDelete();
			this->tooltip = NULL;
		}

		if (tooltip) {
			APP->scene->addChild(tooltip);
			this->tooltip = tooltip;
		}
	}

	void onButton(const event::Button& e) override {
		OpaqueWidget::onButton(e);
		//if (e.getTarget() != this)
		//	return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			ModuleWidget* mw = chooseModel(model);
			// Pretend the moduleWidget was clicked so it can be dragged in the RackWidget
			e.consume(mw);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
	}

	void createContextMenu() {
		Menu* menu = createMenu();

		struct FilterBrandItem : MenuItem {
			std::string brand;
			void onAction(const event::Action& e) override {
				ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
				browser->brand = brand;
				browser->refresh(true);
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, model->plugin->name.c_str()));
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, model->name.c_str()));
		menu->addChild(construct<FilterBrandItem>(&MenuItem::text, string::f("Filter by \"%s\"", model->plugin->brand.c_str()), &FilterBrandItem::brand, model->plugin->brand));
		menu->addChild(new MenuSeparator);
		bool m = false;

		struct ModuleUrlItem : ui::MenuItem {
			std::string url;
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, url);
				t.detach();
			}
		};

		if (!model->plugin->pluginUrl.empty()) {
			ModuleUrlItem* websiteItem = new ModuleUrlItem;
			websiteItem->text = "Website";
			websiteItem->url = model->plugin->pluginUrl;
			menu->addChild(websiteItem);
			m = true;
		}

		if (!model->plugin->manualUrl.empty()) {
			ModuleUrlItem* manualItem = new ModuleUrlItem;
			manualItem->text = "Manual";
			manualItem->url = model->plugin->manualUrl;
			menu->addChild(manualItem);
			m = true;
		}

		struct FavoriteModelItem : MenuItem {
			plugin::Model* model;
			bool isFavorite = false;

			FavoriteModelItem(plugin::Model* model) {
				text = "Favorite";
				this->model = model;
				auto it = favoriteModels.find(model);
				isFavorite = it != favoriteModels.end();
			}
			void onAction(const event::Action& e) override {
				toggleModelFavorite(model);
			}
			void step() override {
				rightText = string::f("%s %s", CHECKMARK(isFavorite), RACK_MOD_CTRL_NAME "+F");
				MenuItem::step();
			}
		};

		struct HiddenModelItem : MenuItem {
			plugin::Model* model;
			bool isHidden = false;

			HiddenModelItem(plugin::Model* model) {
				text = "Hide";
				this->model = model;
				auto it = hiddenModels.find(model);
				isHidden = it != hiddenModels.end();
			}
			void onAction(const event::Action& e) override {
				toggleModelHidden(model);
			}
			void step() override {
				rightText = string::f("%s %s", CHECKMARK(isHidden), RACK_MOD_CTRL_NAME "+H");
				MenuItem::step();
			}
		};

		if (m) menu->addChild(new MenuSeparator);
		menu->addChild(new FavoriteModelItem(model));
		menu->addChild(new HiddenModelItem(model));
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			switch (e.key) {
				case GLFW_KEY_F:
					toggleModelFavorite(model);
					e.consume(this);
					break;
				case GLFW_KEY_H:
					toggleModelHidden(model);
					e.consume(this);
					break;
			}
		}
		OpaqueWidget::onHoverKey(e);
	}

	void onEnter(const event::Enter& e) override {
		std::string text;
		text = model->plugin->brand;
		text += " " + model->name;
		// Tags
		text += "\nTags: ";
		for (size_t i = 0; i < model->tags.size(); i++) {
			if (i > 0)
				text += ", ";
			int tagId = model->tags[i];
			text += rack::tag::tagAliases[tagId][0];
		}
		// Description
		if (model->description != "") {
			text += "\n" + model->description;
		}
		ui::Tooltip* tooltip = new ui::Tooltip;
		tooltip->text = text;
		setTooltip(tooltip);
	}

	void onLeave(const event::Leave& e) override {
		setTooltip(NULL);
	}

	void onHide(const event::Hide& e) override {
		// Hide tooltip
		setTooltip(NULL);
		OpaqueWidget::onHide(e);
	}
};


struct SortItem : ui::MenuItem {
	ModuleBrowserSort sort;

	void onAction(const event::Action& e) override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		modelBoxSort = (int)sort;
		browser->refresh(true);
	}

	void step() override {
		// Skip the autosizing of MenuItem
		Widget::step();
		active = modelBoxSort == (int)sort;
	}

	void draw(const DrawArgs& args) override {
		BNDwidgetState state = BND_DEFAULT;

		if (APP->event->hoveredWidget == this)
			state = BND_HOVER;

		if (active)
			state = BND_ACTIVE;

		bndMenuItem(args.vg, 0.0, 0.0, box.size.x, box.size.y, state, -1, NULL);
		const float BND_LABEL_FONT_SIZE = 13.f;
		NVGcolor color = bndTextColor(&bndGetTheme()->menuItemTheme, state);
		bndIconLabelValue(args.vg, 0.f, 0.f, box.size.x, box.size.y, -1, color, BND_CENTER, BND_LABEL_FONT_SIZE, text.c_str(), NULL);
	}
};


struct FavoriteItem : ui::MenuItem {
	void onAction(const event::Action& e) override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		browser->favorites ^= true;
		browser->refresh(true);
	}
	void step() override {
		MenuItem::step();
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		active = browser->favorites;
	}
};


struct BrandItem : ui::MenuItem {
	void onAction(const event::Action& e) override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		if (browser->brand == text)
			browser->brand = "";
		else
			browser->brand = text;
		browser->refresh(true);
	}
	void step() override {
		MenuItem::step();
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		active = (browser->brand == text);
	}
};


struct TagItem : ui::MenuItem {
	int tagId;
	void onAction(const event::Action& e) override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		if (browser->tagId.find(tagId) != browser->tagId.end())
			browser->tagId.erase(tagId);
		else
			browser->tagId.insert(tagId);
		browser->refresh(true);
	}
	void step() override {
		MenuItem::step();
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		active = (browser->tagId.find(tagId) != browser->tagId.end());
	}
};


struct BrowserSearchField : ui::TextField {
	void step() override {
		// Steal focus when step is called
		APP->event->setSelected(this);
		TextField::step();
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			switch (e.key) {
				case GLFW_KEY_ESCAPE: {
					Mb::BrowserOverlay* overlay = getAncestorOfType<Mb::BrowserOverlay>();
					overlay->hide();
					e.consume(this);
					break;
				} 
				case GLFW_KEY_BACKSPACE: {
					if (text == "") {
						ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
						browser->clear(false);
						e.consume(this);
					}
					break;
				} 
				case GLFW_KEY_SPACE: {
					if (string::trim(text) == "" && (e.mods & RACK_MOD_MASK) == 0) {
						ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
						browser->favorites ^= true;
						e.consume(this);
					}
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_CONTROL) {
						ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
						browser->hidden ^= true;
						setText(string::trim(text));
						e.consume(this);
					}
					break;
				}
			}
		}

		bool propagate = !e.getTarget();
		propagate = propagate && !((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_F);
		propagate = propagate && !((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_H);

		if (propagate) {
			ui::TextField::onSelectKey(e);
		}
	}

	void onChange(const event::Change& e) override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		browser->search = string::trim(text);
		browser->refresh(true);
	}

	void onAction(const event::Action& e) override {
		// Get first ModelBox
		ModelBox* mb = NULL;
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		for (Widget* w : browser->modelContainer->children) {
			if (w->visible) {
				mb = dynamic_cast<ModelBox*>(w);
				break;
			}
		}

		if (mb) {
			chooseModel(mb->model);
		}
	}

	void onHide(const event::Hide& e) override {
		APP->event->setSelected(NULL);
		ui::TextField::onHide(e);
	}

	void onShow(const event::Show& e) override {
		text = string::trim(text);
		selectAll();
		TextField::onShow(e);
	}
};


struct ClearButton : ui::Button {
	void onAction(const event::Action& e) override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		browser->clear((APP->window->getMods() & RACK_MOD_MASK) == RACK_MOD_CTRL);
	}
};


BrowserSidebar::BrowserSidebar() {
	// Search
	searchField = new BrowserSearchField;
	addChild(searchField);

	// Clear filters
	clearButton = new ClearButton;
	clearButton->text = "Reset filters";
	addChild(clearButton);

	// Favorites
	favoriteList = new ui::List;
	addChild(favoriteList);

	FavoriteItem* favoriteItem = new FavoriteItem;
	favoriteItem->text = "Favorites";
	favoriteList->addChild(favoriteItem);

	// Tag label
	tagLabel = new ui::Label;
	// tagLabel->fontSize = 16;
	tagLabel->color = nvgRGB(0x80, 0x80, 0x80);
	tagLabel->text = "Tags";
	addChild(tagLabel);

	// Tag list
	tagScroll = new ui::ScrollWidget;
	addChild(tagScroll);

	tagList = new ui::List;
	tagScroll->container->addChild(tagList);

	for (int tagId = 0; tagId < (int) tag::tagAliases.size(); tagId++) {
		TagItem* item = new TagItem;
		item->text = tag::tagAliases[tagId][0];
		item->tagId = tagId;
		tagList->addChild(item);
	}

	// Brand label
	brandLabel = new ui::Label;
	// brandLabel->fontSize = 16;
	brandLabel->color = nvgRGB(0x80, 0x80, 0x80);
	brandLabel->text = "Brands";
	addChild(brandLabel);

	// Brand list
	brandScroll = new ui::ScrollWidget;
	addChild(brandScroll);

	brandList = new ui::List;
	brandScroll->container->addChild(brandList);

	// Collect brands from all plugins
	std::set<std::string, string::CaseInsensitiveCompare> brands;
	for (plugin::Plugin* plugin : rack::plugin::plugins) {
		brands.insert(plugin->brand);
	}

	for (const std::string& brand : brands) {
		BrandItem* item = new BrandItem;
		item->text = brand;
		brandList->addChild(item);
	}
}

void BrowserSidebar::step() {
	searchField->box.size.x = box.size.x;
	clearButton->box.pos = searchField->box.getBottomLeft();
	clearButton->box.size.x = box.size.x;

	favoriteList->box.pos = clearButton->box.getBottomLeft();
	favoriteList->box.size.x = box.size.x;

	float listHeight = hideBrands ? box.size.y : (box.size.y - favoriteList->box.getBottom()) / 2;
	listHeight = std::floor(listHeight);

	tagLabel->box.pos = favoriteList->box.getBottomLeft();
	tagLabel->box.size.x = box.size.x;
	tagScroll->box.pos = tagLabel->box.getBottomLeft();
	tagScroll->box.size.x = box.size.x;
	tagList->box.size.x = tagScroll->box.size.x;
	tagScroll->box.size.y = listHeight - tagLabel->box.size.y;

	if (!hideBrands) {
		brandLabel->box.pos = tagScroll->box.getBottomLeft();
		brandLabel->box.size.x = box.size.x;
		brandScroll->box.pos = brandLabel->box.getBottomLeft();
		brandScroll->box.size.y = listHeight - brandLabel->box.size.y;
		brandScroll->box.size.x = box.size.x;
		brandList->box.size.x = brandScroll->box.size.x;
	}

	brandLabel->visible = !hideBrands;
	brandScroll->visible = !hideBrands;
	brandList->visible = !hideBrands;

	Widget::step();
}


ModuleBrowser::ModuleBrowser() {
	sidebar = new BrowserSidebar;
	sidebar->box.size.x = 200;
	addChild(sidebar);

	modelLabel = new ui::Label;
	// modelLabel->fontSize = 16;
	// modelLabel->box.size.x = 400;
	addChild(modelLabel);

	SortItem* modelSortDefaultItem = new SortItem;
	modelSortDefaultItem->box.size.x = 125.f;
	modelSortDefaultItem->sort = ModuleBrowserSort::DEFAULT;
	modelSortDefaultItem->text = "Recently updated";
	addChild(modelSortDefaultItem);
	this->modelSortDefaultItem = modelSortDefaultItem;

	SortItem* modelSortLastUsedItem = new SortItem;
	modelSortLastUsedItem->box.size.x = 125.f;
	modelSortLastUsedItem->sort = ModuleBrowserSort::LAST_USED;
	modelSortLastUsedItem->text = "Last used";
	addChild(modelSortLastUsedItem);
	this->modelSortLastUsedItem = modelSortLastUsedItem;

	SortItem* modelSortMostUsedItem = new SortItem;
	modelSortMostUsedItem->box.size.x = 125.f;
	modelSortMostUsedItem->sort = ModuleBrowserSort::MOST_USED;
	modelSortMostUsedItem->text = "Most used";
	addChild(modelSortMostUsedItem);
	this->modelSortMostUsedItem = modelSortMostUsedItem;

	SortItem* modelSortRandom = new SortItem;
	modelSortRandom->box.size.x = 125.f;
	modelSortRandom->sort = ModuleBrowserSort::RANDOM;
	modelSortRandom->text = "Random";
	addChild(modelSortRandom);
	this->modelSortRandom = modelSortRandom;

	SortItem* modelSortNameItem = new SortItem;
	modelSortNameItem->box.size.x = 125.f;
	modelSortNameItem->sort = ModuleBrowserSort::NAME;
	modelSortNameItem->text = "Module name";
	addChild(modelSortNameItem);
	this->modelSortNameItem = modelSortNameItem;

	modelZoomSlider = new ModelZoomSlider;
	addChild(modelZoomSlider);

	modelScroll = new ui::ScrollWidget;
	addChild(modelScroll);

	modelMargin = new rack::ui::MarginLayout;
	modelMargin->margin = math::Vec(10, 10);
	modelScroll->container->addChild(modelMargin);

	modelContainer = new ui::SequentialLayout;
	modelContainer->spacing = math::Vec(10, 10);
	modelMargin->addChild(modelContainer);

	// Add ModelBoxes for each Model
	for (plugin::Plugin* plugin : rack::plugin::plugins) {
		for (plugin::Model* model : plugin->models) {
			ModelBox* moduleBox = new ModelBox;
			moduleBox->setModel(model);
			modelContainer->addChild(moduleBox);
		}
	}

	clear(false);
}

void ModuleBrowser::step() {
	if (!visible) return;
	box = parent->box.zeroPos().grow(math::Vec(-70, -70));

	sidebar->box.size.y = box.size.y;

	modelLabel->box.pos = sidebar->box.getTopRight().plus(math::Vec(5, 5));

	modelZoomSlider->box.pos = Vec(box.size.x - modelZoomSlider->box.size.x - 5, 5);

	modelSortDefaultItem->box.pos = Vec(modelZoomSlider->box.pos.x - modelSortDefaultItem->box.size.x - 30, 5);
	modelSortLastUsedItem->box.pos = Vec(modelSortDefaultItem->box.pos.x - modelSortLastUsedItem->box.size.x - 5, 5);
	modelSortMostUsedItem->box.pos = Vec(modelSortLastUsedItem->box.pos.x - modelSortMostUsedItem->box.size.x - 5, 5);
	modelSortRandom->box.pos = Vec(modelSortMostUsedItem->box.pos.x - modelSortRandom->box.size.x - 5, 5);
	modelSortNameItem->box.pos = Vec(modelSortRandom->box.pos.x - modelSortNameItem->box.size.x - 5, 5);

	modelScroll->box.pos = sidebar->box.getTopRight().plus(math::Vec(0, 30));
	modelScroll->box.size = box.size.minus(modelScroll->box.pos);
	modelMargin->box.size.x = modelScroll->box.size.x;
	modelMargin->box.size.y = modelContainer->getChildrenBoundingBox().size.y + 2 * modelMargin->margin.y;

	OpaqueWidget::step();
}

void ModuleBrowser::draw(const DrawArgs& args) {
	bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, 0);
	Widget::draw(args);
}

void ModuleBrowser::refresh(bool resetScroll) {
	if (resetScroll) {
		// Reset scroll position
		modelScroll->offset = math::Vec();
	}

	// Filter ModelBoxes
	for (Widget* w : modelContainer->children) {
		ModelBox* m = dynamic_cast<ModelBox*>(w);
		assert(m);
		m->visible = isModelVisible(m->model, search, favorites, brand, tagId, hidden);
		if (hidden && m->visible) m->modelHidden = isModelHidden(m->model);
	}

	// Sort ModelBoxes
	auto sortDefault = [&](Widget* w1, Widget* w2) {
		ModelBox* m1 = dynamic_cast<ModelBox*>(w1);
		ModelBox* m2 = dynamic_cast<ModelBox*>(w2);
		// Sort by (modifiedTimestamp descending, plugin brand)
		auto t1 = std::make_tuple(-m1->model->plugin->modifiedTimestamp, m1->model->plugin->brand);
		auto t2 = std::make_tuple(-m2->model->plugin->modifiedTimestamp, m2->model->plugin->brand);
		return t1 < t2;
	};

	auto sortByName = [&](Widget* w1, Widget* w2) {
		ModelBox* m1 = dynamic_cast<ModelBox*>(w1);
		ModelBox* m2 = dynamic_cast<ModelBox*>(w2);
		return m1->model->name < m2->model->name;
	};

	auto sortByLastUsed = [&](Widget* w1, Widget* w2) {
		ModelBox* m1 = dynamic_cast<ModelBox*>(w1);
		ModelBox* m2 = dynamic_cast<ModelBox*>(w2);
		auto u1 = modelUsage.find(m1->model);
		auto u2 = modelUsage.find(m2->model);
		// Sort by usedTimestamp descending
		if (u1 == modelUsage.end()) return false;
		if (u2 == modelUsage.end()) return true;
		return -u1->second->usedTimestamp < -u2->second->usedTimestamp;
	};

	auto sortByMostUsed = [&](Widget* w1, Widget* w2) {
		ModelBox* m1 = dynamic_cast<ModelBox*>(w1);
		ModelBox* m2 = dynamic_cast<ModelBox*>(w2);
		auto u1 = modelUsage.find(m1->model);
		auto u2 = modelUsage.find(m2->model);
		if (u1 == modelUsage.end()) return false;
		if (u2 == modelUsage.end()) return true;
		// Sort by (usedCount descending, modifiedTimestamp descending)
		auto t1 = std::make_tuple(-u1->second->usedCount, -m1->model->plugin->modifiedTimestamp);
		auto t2 = std::make_tuple(-u2->second->usedCount, -m2->model->plugin->modifiedTimestamp);
		return t1 < t2;
	};

	switch ((ModuleBrowserSort)modelBoxSort) {
		case ModuleBrowserSort::DEFAULT:
			modelContainer->children.sort(sortDefault);
			break;
		case ModuleBrowserSort::NAME:
			modelContainer->children.sort(sortByName);
			break;
		case ModuleBrowserSort::LAST_USED:
			modelContainer->children.sort(sortByLastUsed);
			break;
		case ModuleBrowserSort::MOST_USED:
			modelContainer->children.sort(sortByMostUsed);
			break;
		case ModuleBrowserSort::RANDOM:
			std::vector<std::reference_wrapper<Widget*>> vec(modelContainer->children.begin(), modelContainer->children.end());
			std::random_shuffle(vec.begin(), vec.end());
			std::list<Widget*> s(vec.begin(), vec.end());
			modelContainer->children.swap(s);
			break;
	}
	

	if (!search.empty()) {
		std::map<Widget*, float> scores;
		// Compute scores
		for (Widget* w : modelContainer->children) {
			ModelBox* m = dynamic_cast<ModelBox*>(w);
			assert(m);
			if (!m->visible)
				continue;
			scores[m] = modelScore(m->model, search);
		}
	}

	// Filter the brand and tag lists

	// Get modules that would be filtered by just the search query
	std::vector<plugin::Model*> filteredModels;
	for (Widget* w : modelContainer->children) {
		ModelBox* m = dynamic_cast<ModelBox*>(w);
		assert(m);
		if (isModelVisible(m->model, search, favorites, "", emptyTagId, hidden))
			filteredModels.push_back(m->model);
	}

	auto hasModel = [&](const std::string& brand, int itemTagId = -1) -> bool {
		std::set<int> tagIdp1 = tagId;
		if (itemTagId >= 0) tagIdp1.insert(itemTagId);
		for (plugin::Model* model : filteredModels) {
			if (isModelVisible(model, "", favorites, brand, tagIdp1, hidden))
				return true;
		}
		return false;
	};

	// Enable brand and tag items that are available in visible ModelBoxes
	int brandsLen = 0;
	for (Widget* w : sidebar->brandList->children) {
		BrandItem* item = dynamic_cast<BrandItem*>(w);
		assert(item);
		item->disabled = !hasModel(item->text);
		if (!item->disabled)
			brandsLen++;
	}
	sidebar->brandLabel->text = string::f("Brands (%d)", brandsLen);

	int tagsLen = 0;
	for (Widget* w : sidebar->tagList->children) {
		TagItem* item = dynamic_cast<TagItem*>(w);
		assert(item);
		item->disabled = !hasModel(brand, item->tagId);
		if (!item->disabled)
			tagsLen++;
	}
	sidebar->tagLabel->text = string::f("Tags (%d)", tagsLen);

	// Count models
	int modelsLen = 0;
	for (Widget* w : modelContainer->children) {
		if (w->visible)
			modelsLen++;
	}
	modelLabel->text = string::f("Modules (%d)", modelsLen);
}

void ModuleBrowser::clear(bool keepSearch) {
	if (!keepSearch) {
		search = "";
		sidebar->searchField->setText("");
	}
	favorites = false;
	brand = "";
	tagId.clear();
	hidden = false;
	refresh(true);
}

void ModuleBrowser::onShow(const event::Show& e) {
	refresh(false);
	OpaqueWidget::onShow(e);
}

void ModuleBrowser::onHoverScroll(const event::HoverScroll& e) {
	if ((APP->window->getMods() & RACK_MOD_MASK) == RACK_MOD_CTRL) {
		// Increase zoom
		float zoomDelta = e.scrollDelta.y / 50.f / 12.f;
		v1::modelBoxZoom = math::clamp(v1::modelBoxZoom + zoomDelta, PREVIEW_MIN, PREVIEW_MAX);
		e.consume(this);
		return;
	}
	OpaqueWidget::onHoverScroll(e);
}

} // namespace v1
} // namespace Mb
} // namespace StoermelderPackOne