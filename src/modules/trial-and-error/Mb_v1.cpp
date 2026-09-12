#include "Mb_v1.hpp"
#include "Mb_preview.hpp"
#include "../../vcv/ui.hpp"
#include <tag.hpp>

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

static bool isModelVisible(plugin::Model* model, const bool& favourite, const std::string& brand, const std::set<int>& tagId, const std::set<std::string>& customTagFilter, const bool& hidden) {
	// Filter if not whitelisted by library
	if (pluginSettings.mbApplyLibraryWhitelist) {
		if (!settings::isModuleWhitelisted(model->plugin->slug, model->slug)) {
			return false;
		}
	}

	// Filter deprecated modules
	if (!pluginSettings.mbShowDeprecated) {
		if (model->hidden) {
			return false;
		}
	}

	// Filter favorite
	if (favourite) {
		if (!isModelFavorite(model))
			return false;
	}

	// Filter brand
	if (brand != "") {
		if (model->plugin->brand != brand)
			return false;
	}

	// Filter built-in tags (AND: model must have all selected tags)
	// Use effective tag IDs (with predefined tag modifications applied)
	std::set<int> effectiveTagIds = getEffectiveTagIds(model);
	for (auto t : tagId) {
		if (effectiveTagIds.find(t) == effectiveTagIds.end())
			return false;
	}

	// Filter custom tags (AND: model must have all selected custom tags)
	for (const auto& ct : customTagFilter) {
		if (!customTagHas(model, ct))
			return false;
	}

	// Filter hidden
	if (!hidden) {
		auto it = hiddenModels.find(model);
		if (it != hiddenModels.end())
			return false;
	}

	return true;
}


// Widgets

struct ModelBox : ModelBoxBase {
	// Zoom the box was last sized for, to detect changes to v1::modelBoxZoom.
	float appliedZoom = -1.f;

	// Applies v1's browser-wide zoom, called from step() only when it changed.
	void updateZoom() override {
		// Width is unknown before the preview exists; step() uses a 10HP approximation instead.
		if (!preview.created()) return;
		preview.setZoom(v1::modelBoxZoom);
		box.size.x = preview.width * v1::modelBoxZoom;
		box.size.y = RACK_GRID_HEIGHT * v1::modelBoxZoom;
	}

	const ViewportBand* getBrowserBand() override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		return browser ? &browser->stepBand : NULL;
	}

	// Default argument stays on the base declaration only, so it can't drift from it.
	void refreshBrowser(bool onlyIfFavoriteFilter) override {
		ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
		if (!browser) return;
		if (onlyIfFavoriteFilter && !browser->favorites) return;
		browser->refresh(false);
	}

	void refreshBrowserTags() override {
		ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
		if (!browser) return;
		// v1 lists custom tags in its sidebar, so that has to be rebuilt too.
		browser->sidebar->refreshCustomTagList();
		browser->refresh(false);
	}

	void filterBrowserByBrand() override {
		ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
		if (!browser) return;
		browser->brand = model->plugin->brand;
		browser->refresh(true);
	}

	void step() override {
		// Must run even off screen: the layout positions every box from its size, so a stale
		// size would misplace the grid. Hence this runs before ModelBoxBase::step()'s
		// visibility and off-screen early-outs.
		if (appliedZoom != v1::modelBoxZoom) {
			appliedZoom = v1::modelBoxZoom;
			// 10HP approximation before the real width is known; a zero size would make the
			// parent think it's out of the draw bounds, so its preview would never be created.
			box.size.x = (preview.width < 0 ? 10 * RACK_GRID_WIDTH : preview.width) * appliedZoom;
			box.size.y = RACK_GRID_HEIGHT * appliedZoom;
			box.size = box.size.ceil();

			preview.previewWidget->box.size.y = std::ceil(RACK_GRID_HEIGHT * appliedZoom);

			if (preview.created()) updateZoom();
		}

		ModelBoxBase::step();
	}
};


struct SortChoice : ui::ChoiceButton {
	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			createContextMenu();
			e.consume(this);
		}
	}

	void createContextMenu() {
		Menu* menu = createMenu();
		menu->box.pos = getAbsoluteOffset(Vec(0, box.size.y)).round();

		struct SortItem : ui::MenuItem {
			ModuleBrowserSort sort;
			void onAction(const event::Action& e) override {
				ModuleBrowser* browser = APP->scene->browser->getFirstDescendantOfType<ModuleBrowser>();
				modelBoxSort = (int)sort;
				browser->refresh(true);
			}
		};

		menu->addChild(construct<SortItem>(&MenuItem::text, "Recently updated", &SortItem::sort, ModuleBrowserSort::DEFAULT));
		menu->addChild(construct<SortItem>(&MenuItem::text, "Last used", &SortItem::sort, ModuleBrowserSort::LAST_USED));
		menu->addChild(construct<SortItem>(&MenuItem::text, "Most used", &SortItem::sort, ModuleBrowserSort::MOST_USED));
		menu->addChild(construct<SortItem>(&MenuItem::text, "Random", &SortItem::sort, ModuleBrowserSort::RANDOM));
		menu->addChild(construct<SortItem>(&MenuItem::text, "Module name", &SortItem::sort, ModuleBrowserSort::NAME));
	}

	void step() override {
		switch ((ModuleBrowserSort)modelBoxSort) {
			case ModuleBrowserSort::DEFAULT:
				text = "Recently updated"; break;
			case ModuleBrowserSort::LAST_USED:
				text = "Last used"; break;
			case ModuleBrowserSort::MOST_USED:
				text = "Most used"; break;
			case ModuleBrowserSort::RANDOM:
				text = "Random"; break;
			case ModuleBrowserSort::NAME:
				text = "Module name"; break;
		}
		ChoiceButton::step();
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
		rightText = CHECKMARK(browser->favorites);
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
		rightText = CHECKMARK(browser->brand == text);
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
		rightText = CHECKMARK(browser->tagId.find(tagId) != browser->tagId.end());
	}
};


struct CustomTagItem : ui::MenuItem {
	std::string tagName;
	void onAction(const event::Action& e) override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		if (browser->customTagFilter.find(tagName) != browser->customTagFilter.end())
			browser->customTagFilter.erase(tagName);
		else
			browser->customTagFilter.insert(tagName);
		browser->refresh(true);
	}
	void step() override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		if (browser) {
			rightText = CHECKMARK(browser->customTagFilter.find(tagName) != browser->customTagFilter.end());
		}
		MenuItem::step();
	}
};


struct BrowserSearchField : ui::TextField {
	void step() override {
		// Steal focus, but yield to any other TextField that has it
		widget::Widget* selected = APP->event->getSelectedWidget();
		if (!selected || !dynamic_cast<ui::TextField*>(selected)) {
			APP->event->setSelectedWidget(this);
		}
		TextField::step();
	}

	void onSelectKey(const event::SelectKey& e) override {
		bool propagate = !e.getTarget();

		switch (e.key) {
			case GLFW_KEY_ESCAPE: {
				if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
					Mb::BrowserOverlay* overlay = getAncestorOfType<Mb::BrowserOverlay>();
					overlay->hide();
				}
				e.consume(this);
				break;
			} 
			case GLFW_KEY_BACKSPACE: {
				if (text == "") {
					if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
						ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
						browser->clear(false);
					}
					e.consume(this);
				}
				break;
			} 
			case GLFW_KEY_SPACE: {
				if (string::trim(text) == "" && (e.mods & RACK_MOD_MASK) == 0) {
					if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
						ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
						browser->favorites ^= true;
					}
					setText("");
					propagate = false;
					e.consume(this);
				}
				if ((e.mods & RACK_MOD_MASK) == RACK_MOD_SHIFT) {
					if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
						ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
						browser->hidden ^= true;
					}
					setText(string::trim(text));
					propagate = false;
					e.consume(this);
				}
				break;
			}
		}

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
		APP->event->setSelectedWidget(NULL);
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
		browser->clear((vcv::ui::getWindowMods() & RACK_MOD_MASK) == RACK_MOD_CTRL);
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

	// Custom tag label
	customTagLabel = new ui::Label;
	customTagLabel->color = nvgRGB(0x80, 0x80, 0x80);
	customTagLabel->text = "Custom Tags";
	addChild(customTagLabel);

	// Custom tag list
	customTagScroll = new ui::ScrollWidget;
	addChild(customTagScroll);

	customTagList = new ui::List;
	customTagScroll->container->addChild(customTagList);

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

	bool hasCustomTags = !customTagList->children.empty();
	customTagLabel->visible = hasCustomTags;
	customTagScroll->visible = hasCustomTags;

	// Divide remaining sidebar height equally among visible list sections
	int numSections = (hasCustomTags ? 1 : 0) + 1 + (hideBrands ? 0 : 1);
	float remainingHeight = box.size.y - favoriteList->box.getBottom();
	float listHeight = std::floor(remainingHeight / numSections);

	widget::Widget* anchor = favoriteList;

	if (hasCustomTags) {
		customTagLabel->box.pos = anchor->box.getBottomLeft();
		customTagLabel->box.size.x = box.size.x;
		customTagScroll->box.pos = customTagLabel->box.getBottomLeft();
		customTagScroll->box.size.x = box.size.x;
		customTagList->box.size.x = customTagScroll->box.size.x;
		customTagScroll->box.size.y = listHeight - customTagLabel->box.size.y;
		anchor = customTagScroll;
	}

	tagLabel->box.pos = anchor->box.getBottomLeft();
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

void BrowserSidebar::refreshCustomTagList() {
	customTagList->clearChildren();
	auto unsortedTags = customTagsAll();
	std::vector<std::string> tags(unsortedTags.begin(), unsortedTags.end());
	std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
		return string::lowercase(a) < string::lowercase(b);
	});
	for (const auto& tag : tags) {
		CustomTagItem* item = new CustomTagItem;
		item->text = tag;
		item->tagName = tag;
		customTagList->addChild(item);
	}
}


ModuleBrowser::ModuleBrowser() {
	const float margin = 10;

	sidebar = new BrowserSidebar;
	sidebar->box.size.x = 200;
	addChild(sidebar);

	modelLabel = new ui::Label;
	// modelLabel->fontSize = 16;
	// modelLabel->box.size.x = 400;
	addChild(modelLabel);

	ChoiceButton* modelSortChoice = new SortChoice;
	modelSortChoice->box.size.x = 160.f;
	addChild(modelSortChoice);
	this->modelSortChoice = modelSortChoice;

	modelZoomSlider = Rack::createPtrSlider(&v1::modelBoxZoom, PREVIEW_MIN, PREVIEW_MAX, 0.9f, "Preview", "", 100.f, 180.0f);
	addChild(modelZoomSlider);

	prewarmProgress = new PrewarmProgressWidget;
	prewarmProgress->prewarmer = &prewarmer;
	prewarmProgress->box.size = math::Vec(60, BND_WIDGET_HEIGHT);
	addChild(prewarmProgress);

	modelScroll = new ui::ScrollWidget;
	addChild(modelScroll);

	modelMargin = new widget::Widget;
	modelScroll->container->addChild(modelMargin);

	modelContainer = new ui::SequentialLayout;
	modelContainer->margin = math::Vec(margin, 0);
	modelContainer->spacing = math::Vec(margin, margin);
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
	const float margin = 10;
	if (!visible) return;
	box = parent->box.zeroPos().grow(math::Vec(-70, -70));

	sidebar->box.size.y = box.size.y;
	modelLabel->box.pos = sidebar->box.getTopRight().plus(math::Vec(5, 5));
	// Right of the "Modules (N)" label; hides itself once warming is done.
	prewarmProgress->box.pos = modelLabel->box.pos.plus(math::Vec(110, 0));
	modelZoomSlider->box.pos = Vec(box.size.x - modelZoomSlider->box.size.x - 5, 5);
	modelSortChoice->box.pos =  Vec(modelZoomSlider->box.pos.x - modelSortChoice->box.size.x - 20, 5);

	modelScroll->box.pos = sidebar->box.getTopRight().plus(math::Vec(0, 30));
	modelScroll->box.size = box.size.minus(modelScroll->box.pos);
	modelMargin->box.size.x = modelScroll->box.size.x;
	modelMargin->box.size.y = modelContainer->getChildrenBoundingBox().size.y + 2 * margin;
	modelContainer->box.size.x = modelMargin->box.size.x - margin;

	// One screen of slack either side, so boxes about to scroll in are already stepped.
	stepBand = ViewportBand::around(modelScroll->offset.y, modelScroll->box.size.y,
		modelMargin->box.pos.y + modelContainer->box.pos.y, modelScroll->box.size.y);

	OpaqueWidget::step();
}

void ModuleBrowser::draw(const DrawArgs& args) {
	bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, 0);
	Widget::draw(args);

	// After the visible boxes have drawn (and taken their share of the frame), spend
	// what's left preparing previews that haven't been scrolled to yet. This runs from
	// draw() rather than step() because rasterizing needs a current GL context.
	prewarmModelContainer<ModelBox>(prewarmer, modelContainer->children,
		modelScroll->offset, v1::modelBoxZoom);
}

void ModuleBrowser::refresh(bool resetScroll) {
	// Filtering/sorting is user interaction; back off warming for a few frames.
	prewarmer.reset();
	if (resetScroll) {
		// Reset scroll position
		modelScroll->offset = math::Vec();
	}

	// Compute search scores via fuzzy database
	std::map<plugin::Model*, float> searchScores;
	if (!search.empty()) {
		auto results = modelDb.search(search);
		for (auto& result : results) {
			searchScores[result.key] = result.score;
		}
	}

	// Filter ModelBoxes
	for (Widget* w : modelContainer->children) {
		ModelBox* m = dynamic_cast<ModelBox*>(w);
		assert(m);
		bool visible = isModelVisible(m->model, favorites, brand, tagId, customTagFilter, hidden);
		if (visible && !search.empty()) {
			visible = searchScores.find(m->model) != searchScores.end();
		}
		m->visible = visible;
		if (hidden && m->visible) m->modelHidden = isModelHidden(m->model);
	}

	// Sort ModelBoxes
	auto sortFuzzySearchScore = [&](Widget* w1, Widget* w2) {
		ModelBox* m1 = dynamic_cast<ModelBox*>(w1);
		ModelBox* m2 = dynamic_cast<ModelBox*>(w2);
		auto it1 = searchScores.find(m1->model);
		auto it2 = searchScores.find(m2->model);
		float s1 = (it1 != searchScores.end()) ? it1->second : 0.f;
		float s2 = (it2 != searchScores.end()) ? it2->second : 0.f;
		return s1 > s2;
	};

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

	if (sortBySearchScore && !search.empty()) {
		modelContainer->children.sort(sortFuzzySearchScore);
	}
	else {
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
				std::mt19937 rng(random::u32());
				std::shuffle(vec.begin(), vec.end(), rng);
				std::list<Widget*> s(vec.begin(), vec.end());
				modelContainer->children.swap(s);
				break;
		}
	}

	// Get modules passing search + favorites + hidden (without brand/tag filter)
	std::vector<plugin::Model*> filteredModels;
	for (Widget* w : modelContainer->children) {
		ModelBox* m = dynamic_cast<ModelBox*>(w);
		assert(m);
		if (!isModelVisible(m->model, favorites, "", emptyTagId, customTagFilter, hidden))
			continue;
		if (!search.empty() && searchScores.find(m->model) == searchScores.end())
			continue;
		filteredModels.push_back(m->model);
	}

	auto hasModel = [&](const std::string& brand, int itemTagId = -1) -> bool {
		std::set<int> tagIdp1 = tagId;
		if (itemTagId >= 0) tagIdp1.insert(itemTagId);
		for (plugin::Model* model : filteredModels) {
			if (isModelVisible(model, favorites, brand, tagIdp1, customTagFilter, hidden))
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

	auto hasModelWithCustomTag = [&](const std::string& newTag) -> bool {
		for (plugin::Model* model : filteredModels) {
			if (isModelVisible(model, favorites, brand, tagId, customTagFilter, hidden) && customTagHas(model, newTag))
				return true;
		}
		return false;
	};

	for (Widget* w : sidebar->customTagList->children) {
		CustomTagItem* item = dynamic_cast<CustomTagItem*>(w);
		assert(item);
		item->disabled = !customTagFilter.count(item->tagName) && !hasModelWithCustomTag(item->tagName);
	}

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
	customTagFilter.clear();
	refresh(true);
}

void ModuleBrowser::onShow(const event::Show& e) {
	sidebar->refreshCustomTagList();
	refresh(false);
	OpaqueWidget::onShow(e);
}

void ModuleBrowser::onHoverScroll(const event::HoverScroll& e) {
	if ((vcv::ui::getWindowMods() & RACK_MOD_MASK) == RACK_MOD_CTRL) {
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