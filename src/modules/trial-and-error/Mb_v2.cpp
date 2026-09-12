#include "Mb_v2.hpp"
#include "Mb.hpp"
#include "Mb_preview.hpp"
#include "Mb_manifests.hpp"
#include "../../vcv/ui.hpp"
#include <tag.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace v2 {

BrowserSort browserSort = BrowserSort::UPDATED;


// Widgets

struct ModelBox : ModelBoxBase {
	ModelBox() {
		// v2 uses a tighter corner than v1's shadow.
		shadowCornerRadius = 5.f;
	}

	void updateZoom() override {
		float zoom = std::pow(2.f, settings::browserZoom);
		if (preview.created()) {
			preview.setZoom(zoom);
			box.size.x = preview.width * zoom;
		}
		else {
			box.size.x = 12 * RACK_GRID_WIDTH * zoom;
		}
		box.size.y = RACK_GRID_HEIGHT * zoom;
		box.size = box.size.ceil();
	}

	const ViewportBand* getBrowserBand() override {
		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		return browser ? &browser->stepBand : NULL;
	}

	// Default argument stays on the base declaration only, so it can't drift from it.
	void refreshBrowser(bool onlyIfFavoriteFilter) override {
		ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
		if (!browser) return;
		if (onlyIfFavoriteFilter && !browser->favorite) return;
		browser->refresh(false);
	}

	// v2 shows tags in header dropdowns that rebuild themselves on open, so a plain refresh
	// is enough.
	void refreshBrowserTags() override {
		refreshBrowser(false);
	}

	void filterBrowserByBrand() override {
		ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
		if (!browser) return;
		browser->brand = model->plugin->brand;
		browser->refresh();
	}

	// v2 can also filter by panel width, once the preview has told us the model's HP.
	void appendFilterMenuItems(ui::Menu* menu) override {
		int modelHp = modelWidthGet(model);
		if (modelHp <= 0) return;
		menu->addChild(createMenuItem(string::f("Filter by %d HP", modelHp), "", [modelHp]() {
			ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
			if (!browser) return;
			browser->widthFilterRef = modelHp;
			browser->widthFilterMode = 1;
			browser->refresh();
		}));
	}

	// Records the panel width the first time the preview is built, so the HP filter and sort
	// have a value to work with.
	void onPreviewCreated() override {
		modelWidthSet(model, preview.hp());
	}

	void draw(const DrawArgs& args) override {
		ModelBoxBase::draw(args);

		ModuleBrowser* browser = getAncestorOfType<ModuleBrowser>();
		if (browser && browser->selectedModel == model) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, -5.f, -5.f, box.size.x + 10.f, box.size.y + 10.f);
			nvgStrokeWidth(args.vg, 2.5f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.7f));
			nvgStroke(args.vg);
		}
	}
};



bool handleLayoutMenuKeyEvent(const Widget::SelectKeyEvent& e, Widget* currentContainer = nullptr) {
	if (!e.isConsumed() && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
		switch (e.key) {
			case GLFW_KEY_1: {
				if (currentContainer) currentContainer->parent->requestDelete();
				event::Action a;
				auto browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
				browser->brandButton->onAction(a);
				e.consume(browser);
				return true;
			}
			case GLFW_KEY_2: {
				if (currentContainer) currentContainer->parent->requestDelete();
				event::Action a;
				auto browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
				browser->tagButton->onAction(a);
				e.consume(browser);
				return true;
			}
			case GLFW_KEY_3: {
				if (currentContainer) currentContainer->parent->requestDelete();
				event::Action a;
				auto browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
				browser->customTagButton->onAction(a);
				e.consume(browser);
				return true;
			}
		}
	}
	return false;
}

struct BrowserSearchField : ui::TextField {
	ModuleBrowser* browser;
	DropdownChoiceContainer* dropDown = nullptr;

	void step() override {
		widget::Widget* selected = APP->event->getSelectedWidget();
		if (!selected || !dynamic_cast<ui::TextField*>(selected)) {
			APP->event->setSelectedWidget(this);
		}
		TextField::step();
	}

	void onSelectKey(const event::SelectKey& e) override {
		// Handle special key events
		dropDown = APP->scene->getFirstDescendantOfType<DropdownChoiceContainer>();
		if (dropDown) {
			dropDown->onSelectKey(e);
			return;
		}

		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			switch (e.key) {
				case GLFW_KEY_DOWN:
				case GLFW_KEY_UP: {
					browser->navigateSelection(e.key);
					e.consume(this);
					return;
				}
				case GLFW_KEY_LEFT:
				case GLFW_KEY_RIGHT: {
					if (pluginSettings.mbArrowKeyNavigation) {
						browser->navigateSelection(e.key);
						e.consume(this);
						return;
					}
					break;
				}
				case GLFW_KEY_ESCAPE: {
					Mb::BrowserOverlay* overlay = getAncestorOfType<Mb::BrowserOverlay>();
					overlay->hide();
					e.consume(this);
					return;
				}
				case GLFW_KEY_BACKSPACE: {
					if (text == "") {
						browser->clear();
						e.consume(this);
						return;
					}
					break;
				}
				case GLFW_KEY_SPACE: {
					if (string::trim(text) == "" && (e.mods & RACK_MOD_MASK) == 0) {
						browser->favorite ^= true;
						browser->refresh();
						setText("");
						e.consume(this);
						return;
					}
					if ((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL || (e.mods & RACK_MOD_MASK) == RACK_MOD_SHIFT) {
						browser->hidden ^= true;
						browser->refresh();
						setText(string::trim(text));
						e.consume(this);
						return;
					}
					break;
				}
			}
			if (handleLayoutMenuKeyEvent(e)) {
				return;
			}
		}
	
  		ui::TextField::onSelectKey(e);
	}

	void onSelectText(const SelectTextEvent& e) override {
		// Handle text input
		if (dropDown) {
			dropDown->onSelectText(e);
			return;
		}
		TextField::onSelectText(e);
	}

	void onChange(const event::Change& e) override {
		browser->search = string::trim(text);
		browser->refresh();
	}

	void onAction(const event::Action& e) override {
		if (browser->selectedModel) {
			chooseModel(browser->selectedModel);
			return;
		}
		ModelBox* mb = NULL;
		for (Widget* w : browser->modelContainer->children) {
			if (w->visible) {
				mb = dynamic_cast<ModelBox*>(w);
				break;
			}
		}
		if (mb) chooseModel(mb->model);
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


struct BrandItem : DropdownChoiceItem<ModuleBrowser> {
	std::string brand;
	void onAction(const event::Action& e) override {
		browser->brand = (browser->brand == brand) ? "" : brand;
		browser->refresh();
	}
	void step() override {
		selected = (browser->brand == brand);
		disabled = !selected && !browser->hasVisibleModel(brand, browser->tagIds, browser->favorite, browser->hidden, browser->customTagFilter, browser->widthFilterRef, browser->widthFilterMode);
		DropdownChoiceItem::step();
	}
};

struct BrandButton : ui::ChoiceButton {
	ModuleBrowser* browser;

	void onAction(const event::Action& e) override {
		std::vector<widget::Widget*> items;

		std::set<std::string, string::CaseInsensitiveCompare> brands;
		for (plugin::Plugin* p : rack::plugin::plugins)
			brands.insert(p->brand);

		for (const std::string& b : brands) {
			BrandItem* item = new BrandItem;
			item->setRawText(b);
			item->brand = b;
			item->browser = browser;
			items.push_back(item);
		}

		struct Container : DropdownChoiceContainer {
			void onSelectKey(const SelectKeyEvent& e) override {
				if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_1) {
					e.consume(this);
					parent->requestDelete();
					return;
				}
				else if (handleLayoutMenuKeyEvent(e, this)) {
					return;
				}
				DropdownChoiceContainer::onSelectKey(e);
			}
		};

		openLayoutMenu<ModuleBrowser, Container>(this, items);
	}

	void step() override {
		text = "Brand";
		if (!browser->brand.empty())
			text += ": " + browser->brand;
		text = string::ellipsize(text, 20);
		ChoiceButton::step();
	}
};


struct TagItem : DropdownChoiceItem<ModuleBrowser> {
	int tagId;
	void onAction(const event::Action& e) override {
		if (tagId >= 0) {
			auto it = browser->tagIds.find(tagId);
			if (it != browser->tagIds.end())
				browser->tagIds.erase(it);
			else
				browser->tagIds.insert(tagId);
		}
		else {
			browser->tagIds = {};
		}
		browser->refresh();
	}
	void step() override {
		selected = (tagId >= 0) ? (browser->tagIds.find(tagId) != browser->tagIds.end()) : browser->tagIds.empty();
		if (!selected) {
			std::set<int> newTagIds = browser->tagIds;
			newTagIds.insert(tagId);
			disabled = !browser->hasVisibleModel(browser->brand, newTagIds, browser->favorite, browser->hidden, browser->customTagFilter, browser->widthFilterRef, browser->widthFilterMode);
		}
		else {
			disabled = false;
		}
		DropdownChoiceItem::step();
	}
};

struct TagButton : ui::ChoiceButton {
	ModuleBrowser* browser;

	void onAction(const event::Action& e) override {
		std::vector<widget::Widget*> items;

		for (int id = 0; id < (int)tag::tagAliases.size(); id++) {
			TagItem* item = new TagItem;
			item->setRawText(tag::tagAliases[id][0]);
			item->tagId = id;
			item->browser = browser;
			items.push_back(item);
		}

		struct Container : DropdownChoiceContainer {
			void onSelectKey(const SelectKeyEvent& e) override {
				if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_2) {
					e.consume(this);
					parent->requestDelete();
					return;
				}
				else if (handleLayoutMenuKeyEvent(e, this)) {
					return;
				}
				DropdownChoiceContainer::onSelectKey(e);
			}
		};

		openLayoutMenu<ModuleBrowser, Container>(this, items);
	}

	void step() override {
		text = "Tag";
		if (!browser->tagIds.empty()) {
			text += ": ";
			bool first = true;
			for (int id : browser->tagIds) {
				if (!first) text += ", ";
				text += tag::tagAliases[id][0];
				first = false;
			}
		}
		text = string::ellipsize(text, 20);
		ChoiceButton::step();
	}
};


struct CustomTagFilterItem : DropdownChoiceItem<ModuleBrowser> {
	std::string tagName;
	void onAction(const event::Action& e) override {
		if (tagName.empty()) {
			browser->customTagFilter.clear();
		}
		else {
			auto it = browser->customTagFilter.find(tagName);
			if (it != browser->customTagFilter.end())
				browser->customTagFilter.erase(it);
			else
				browser->customTagFilter.insert(tagName);
		}
		browser->refresh();
	}
	void step() override {
		selected = tagName.empty() ? browser->customTagFilter.empty() : (browser->customTagFilter.find(tagName) != browser->customTagFilter.end());
		if (!selected && !tagName.empty()) {
			std::set<std::string> newFilter = browser->customTagFilter;
			newFilter.insert(tagName);
			disabled = !browser->hasVisibleModel(browser->brand, browser->tagIds, browser->favorite, browser->hidden, newFilter, browser->widthFilterRef, browser->widthFilterMode);
		}
		else {
			disabled = false;
		}
		DropdownChoiceItem::step();
	}
};

struct CustomTagButton : ui::ChoiceButton {
	ModuleBrowser* browser;

	void onAction(const event::Action& e) override {
		std::vector<widget::Widget*> items;

		auto unsortedTags = customTagsAll();
		std::vector<std::string> tags(unsortedTags.begin(), unsortedTags.end());
		std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
			return string::lowercase(a) < string::lowercase(b);
		});
		for (const auto& tag : tags) {
			CustomTagFilterItem* item = new CustomTagFilterItem;
			item->setRawText(tag);
			item->tagName = tag;
			item->browser = browser;
			items.push_back(item);
		}

		struct Container : DropdownChoiceContainer {
			void onSelectKey(const SelectKeyEvent& e) override {
				if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_3) {
					e.consume(this);
					parent->requestDelete();
					return;
				}
				else if (handleLayoutMenuKeyEvent(e, this)) {
					return;
				}
				DropdownChoiceContainer::onSelectKey(e);
			}
		};

		openLayoutMenu<ModuleBrowser, Container>(this, items);
	}

	void step() override {
		text = "Custom Tag";
		if (!browser->customTagFilter.empty()) {
			text += ": ";
			bool first = true;
			for (const auto& t : browser->customTagFilter) {
				if (!first) text += ", ";
				text += t;
				first = false;
			}
		}
		text = string::ellipsize(text, 28);
		ChoiceButton::step();
	}
};


struct WidthItem : DropdownChoiceItem<ModuleBrowser> {
	int hp;

	void onAction(const event::Action& e) override {
		if (browser->widthFilterRef == hp) {
			browser->widthFilterMode++;
			if (browser->widthFilterMode > 3) {
				browser->widthFilterRef = 0;
				browser->widthFilterMode = 0;
			}
		} else {
			browser->widthFilterRef = hp;
			browser->widthFilterMode = 1;
		}
		browser->refresh();
	}

	void step() override {
		selected = (browser->widthFilterMode == 1 && hp == browser->widthFilterRef) ||
		           (browser->widthFilterMode == 2 && hp <= browser->widthFilterRef) ||
		           (browser->widthFilterMode == 3 && hp >= browser->widthFilterRef);
		DropdownChoiceItem::step();
	}

	void draw(const DrawArgs& args) override {
		static const char* suffix[] = {"", " =", " ≤", " ≥"};
		bool isRef = (browser->widthFilterRef == hp && browser->widthFilterMode != 0);
		if (isRef) { 
			text = rawText + suffix[browser->widthFilterMode];
		}
		else if (selected) {
			text = string::f("%s %s", rawText.c_str(), CHECKMARK(true));
		}
		else {
			text = rawText;
		}
		BNDwidgetState state = BND_DEFAULT;
		if (!disabled) {
			if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
			if (APP->event->getDraggedWidget() == this) state = BND_ACTIVE;
		}
		if (disabled) nvgSave(args.vg);
		if (disabled) nvgGlobalAlpha(args.vg, 0.35f);
		bndToolButton(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE, state, -1, text.c_str());
		if (disabled) nvgRestore(args.vg);

		DropdownChoiceContainer* container = getAncestorOfType<DropdownChoiceContainer>();
		if (container && container->selectedItem == this) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, -2.f, -2.f, box.size.x + 4.f, box.size.y + 4.f);
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.9f));
			nvgStroke(args.vg);
		}
	}
};

struct WidthButton : ui::ChoiceButton {
	ModuleBrowser* browser;

	void onAction(const event::Action& e) override {
		std::vector<widget::Widget*> items;

		std::set<int> knownWidths;
		for (auto& pair : modelWidths)
			knownWidths.insert(pair.second);

		for (int hp : knownWidths) {
			WidthItem* item = new WidthItem;
			item->setRawText(string::f("%d HP", hp));
			item->hp = hp;
			item->browser = browser;
			items.push_back(item);
		}

		openLayoutMenu<ModuleBrowser>(this, items);
	}

	void step() override {
		static const char* modePrefix[] = {"", "", "≤ ", "≥ "};
		text = "Width";
		if (browser->widthFilterMode != 0)
			text += string::f(": %s%d HP", modePrefix[browser->widthFilterMode], browser->widthFilterRef);
		text = string::ellipsize(text, 20);
		ChoiceButton::step();
	}
};


struct FavoriteButton : ui::Button {
	ModuleBrowser* browser;
	void onAction(const event::Action& e) override {
		browser->favorite ^= true;
		browser->refresh();
	}
	void step() override {
		text = browser->favorite
			? (std::string("Favorites ") + CHECKMARK(true))
			: "Favorites";
		Button::step();
	}
};


struct ClearButton : ui::Button {
	ModuleBrowser* browser;
	void onAction(const event::Action& e) override {
		browser->clear();
	}
};


struct SortButton : ui::ChoiceButton {
	ModuleBrowser* browser;

	struct SortItem : ui::MenuItem {
		ModuleBrowser* browser;
		BrowserSort sort;
		void onAction(const event::Action& e) override {
			if (disabled) return;
			browserSort = sort;
			browser->widthSortDir = 0;
			browser->refresh();
		}
		void step() override {
			if (sort == BrowserSort::NEWEST) disabled = !Mb::manifestsCacheExists();
			rightText = CHECKMARK(browser->widthSortDir == 0 && browserSort == sort);
			MenuItem::step();
		}
	};

	struct WidthSortItem : ui::MenuItem {
		ModuleBrowser* browser;
		int dir;
		void onAction(const event::Action& e) override {
			browser->widthSortDir = (browser->widthSortDir == dir) ? 0 : dir;
			browser->refresh();
		}
		void step() override {
			rightText = CHECKMARK(browser->widthSortDir == dir);
			MenuItem::step();
		}
	};

	void onAction(const event::Action& e) override {
		Menu* menu = createMenu();
		menu->box.pos = getAbsoluteOffset(Vec(0, box.size.y));
		menu->box.size.x = box.size.x;

		static const struct { BrowserSort id; const char* name; } sorts[] = {
			{ BrowserSort::UPDATED, "Recently updated" },
			{ BrowserSort::NEWEST, "Newest" },
			{ BrowserSort::LAST_USED, "Last used" },
			{ BrowserSort::MOST_USED, "Most used" },
			{ BrowserSort::BRAND, "Brand" },
			{ BrowserSort::NAME, "Module name" },
			{ BrowserSort::RANDOM, "Random" },
		};
		for (auto& s : sorts) {
			SortItem* item = new SortItem;
			item->text = s.name;
			item->sort = s.id;
			item->browser = browser;
			menu->addChild(item);
		}

		menu->addChild(new MenuSeparator);

		WidthSortItem* wasc = new WidthSortItem;
		wasc->text = "Width: narrow → wide";
		wasc->dir = 1;
		wasc->browser = browser;
		menu->addChild(wasc);

		WidthSortItem* wdesc = new WidthSortItem;
		wdesc->text = "Width: wide → narrow";
		wdesc->dir = -1;
		wdesc->browser = browser;
		menu->addChild(wdesc);
	}

	void step() override {
		if (browser->widthSortDir != 0) {
			text = browser->widthSortDir > 0 ? "Sort: narrow → wide" : "Sort: wide → narrow";
			text = string::ellipsize(text, 22);
			ChoiceButton::step();
			return;
		}
		static const char* sortNames[] = {
			"Recently updated", "Last used", "Most used", "Brand", "Module name", "Random", "Newest"
		};
		text = "Sort: ";
		int sort = (int)browserSort;
		if (sort >= 0 && sort <= (int)BrowserSort::NEWEST) {
			text += sortNames[sort];
		}
		text = string::ellipsize(text, 22);
		ChoiceButton::step();
	}
};


struct ZoomButton : ui::ChoiceButton {
	ModuleBrowser* browser;

	struct ZoomItem : ui::MenuItem {
		ModuleBrowser* browser;
		float zoom;
		void onAction(const event::Action& e) override {
			if (zoom != settings::browserZoom) {
				settings::browserZoom = zoom;
				browser->updateZoom();
			}
		}
		void step() override {
			rightText = CHECKMARK(settings::browserZoom == zoom);
			MenuItem::step();
		}
	};

	void onAction(const event::Action& e) override {
		Menu* menu = createMenu();
		menu->box.pos = getAbsoluteOffset(Vec(0, box.size.y));
		menu->box.size.x = box.size.x;

		for (float zoom = 1.f; zoom >= -2.f; zoom -= 0.5f) {
			ZoomItem* item = new ZoomItem;
			item->text = string::f("%.0f%%", std::pow(2.f, zoom) * 100.f);
			item->zoom = zoom;
			item->browser = browser;
			menu->addChild(item);
		}
	}

	void step() override {
		text = string::f("Zoom: %.0f%%", std::pow(2.f, settings::browserZoom) * 100.f);
		ChoiceButton::step();
	}
};


// Sort helper

template <typename F>
static void sortModelContainer(ui::SequentialLayout* container, F f) {
	container->children.sort([&](Widget* w1, Widget* w2) {
		assert(dynamic_cast<ModelBox*>(w1));
		ModelBox* m1 = reinterpret_cast<ModelBox*>(w1);
		assert(dynamic_cast<ModelBox*>(w2));
		ModelBox* m2 = reinterpret_cast<ModelBox*>(w2);
		return f(m1) < f(m2);
	});
}


// ModuleBrowser

ModuleBrowser::ModuleBrowser() {
	const float margin = 10;

	headerLayout = new ui::SequentialLayout;
	headerLayout->box.pos = math::Vec(0, 0);
	headerLayout->margin = math::Vec(margin, margin);
	headerLayout->spacing = math::Vec(margin, margin);
	addChild(headerLayout);

	BrowserSearchField* sf = new BrowserSearchField;
	sf->box.size.x = 150;
	sf->placeholder = "Search modules...";
	sf->browser = this;
	headerLayout->addChild(sf);
	searchField = sf;

	BrandButton* bb = new BrandButton;
	bb->box.size.x = 150;
	bb->browser = this;
	headerLayout->addChild(bb);
	brandButton = bb;

	TagButton* tb = new TagButton;
	tb->box.size.x = 150;
	tb->browser = this;
	headerLayout->addChild(tb);
	tagButton = tb;

	CustomTagButton* ctb = new CustomTagButton;
	ctb->box.size.x = 200;
	ctb->browser = this;
	headerLayout->addChild(ctb);
	customTagButton = ctb;

	WidthButton* wb = new WidthButton;
	wb->box.size.x = 114;
	wb->browser = this;
	headerLayout->addChild(wb);
	widthButton = wb;

	FavoriteButton* fb = new FavoriteButton;
	fb->box.size.x = 90;
	fb->text = "Favorites";
	fb->browser = this;
	headerLayout->addChild(fb);
	favoriteButton = fb;

	ClearButton* cb = new ClearButton;
	cb->box.size.x = 100;
	cb->text = "Reset filters";
	cb->browser = this;
	headerLayout->addChild(cb);
	clearButton = cb;

	countLabel = new ui::Label;
	countLabel->box.size.x = 110;
	headerLayout->addChild(countLabel);

	SortButton* sortBtn = new SortButton;
	sortBtn->box.size.x = 160;
	sortBtn->browser = this;
	headerLayout->addChild(sortBtn);
	sortButton = sortBtn;

	ZoomButton* zoomBtn = new ZoomButton;
	zoomBtn->box.size.x = 100;
	zoomBtn->browser = this;
	headerLayout->addChild(zoomBtn);
	zoomButton = zoomBtn;

	// Last in the row: it hides itself once warming is done, and trailing position means
	// its appearance and disappearance never shift the other controls. Its height must
	// match the other header widgets (BND_WIDGET_HEIGHT): SequentialLayout sizes the row
	// from its tallest visible child, so a taller widget would make the whole header
	// grow while visible and snap back when it hides.
	prewarmProgress = new PrewarmProgressWidget;
	prewarmProgress->prewarmer = &prewarmer;
	prewarmProgress->box.size = math::Vec(60, BND_WIDGET_HEIGHT);
	headerLayout->addChild(prewarmProgress);

	modelScroll = new ui::ScrollWidget;
	addChild(modelScroll);

	modelMargin = new widget::Widget;
	modelScroll->container->addChild(modelMargin);

	modelContainer = new ui::SequentialLayout;
	modelContainer->margin = math::Vec(margin, 2);
	modelContainer->spacing = math::Vec(margin, margin);
	modelMargin->addChild(modelContainer);

	for (plugin::Plugin* p : rack::plugin::plugins) {
		int idx = 0;
		for (plugin::Model* model : p->models) {
			ModelBox* mb = new ModelBox;
			mb->setModel(model);
			modelContainer->addChild(mb);
			modelOrders[model] = idx++;
		}
	}

	clear();
}

void ModuleBrowser::step() {
	if (!visible) return;
	box = parent->box.zeroPos().grow(math::Vec(-40, -40));

	headerLayout->box.size.x = box.size.x;

	const float margin = 10;
	modelScroll->box.pos = headerLayout->box.getBottomLeft();
	modelScroll->box.size = box.size.minus(modelScroll->box.pos);
	modelMargin->box.size.x = modelScroll->box.size.x;
	modelMargin->box.size.y = modelContainer->box.size.y + margin;
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
		modelScroll->offset, settings::browserZoom);
}

bool ModuleBrowser::isModelVisible(plugin::Model* model, const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter, int widthFilterRef, int widthFilterMode) {
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
	if (favorite && !isModelFavorite(model)) {
		return false;
	}

	// Filter brand
	if (!brand.empty() && model->plugin->brand != brand) {
		return false;
	}

	// Use effective tag IDs (with predefined tag modifications applied)
	std::set<int> effectiveTagIds = getEffectiveTagIds(model);
	for (int tagId : tagIds) {
		if (effectiveTagIds.find(tagId) == effectiveTagIds.end())
			return false;
	}

	// Filter custom tags
	for (const auto& ct : customTagFilter) {
		if (!customTagHas(model, ct))
			return false;
	}

	// Filter hidden modules (does not use the Rack's "hidden" property)
	if (!hidden && hiddenModels.find(model) != hiddenModels.end()) {
		return false;
	}

	// Filter by width (HP)
	if (widthFilterMode != 0) {
		int hp = modelWidthGet(model);
		if (hp < 0) return false;
		if (widthFilterMode == 1 && hp != widthFilterRef) return false;
		if (widthFilterMode == 2 && hp > widthFilterRef) return false;
		if (widthFilterMode == 3 && hp < widthFilterRef) return false;
	}

	return true;
}

bool ModuleBrowser::hasVisibleModel(const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter, int widthFilterRef, int widthFilterMode) {
	for (const auto& pair : prefilteredModelScores) {
		if (isModelVisible(pair.first, brand, tagIds, favorite, hidden, customTagFilter, widthFilterRef, widthFilterMode))
			return true;
	}
	return false;
}

void ModuleBrowser::updateZoom() {
	modelScroll->offset = math::Vec();
	for (Widget* w : modelContainer->children) {
		ModelBox* mb = reinterpret_cast<ModelBox*>(w);
		mb->updateZoom();
	}
}

void ModuleBrowser::refresh(bool scrollTop) {
	if (scrollTop) modelScroll->offset = math::Vec();
	prefilteredModelScores.clear();
	// Filtering/sorting is user interaction; back off warming for a few frames.
	prewarmer.reset();

	for (Widget* w : modelContainer->children) {
		ModelBox* m = reinterpret_cast<ModelBox*>(w);
		m->visible = isModelVisible(m->model, brand, tagIds, favorite, hidden, customTagFilter, widthFilterRef, widthFilterMode);
	}

	auto applyBrowserSort = [&]() {
		if (widthSortDir != 0) {
			int dir = widthSortDir;
			sortModelContainer(modelContainer, [dir](ModelBox* m) {
				int hp = modelWidthGet(m->model);
				if (hp < 0) hp = dir > 0 ? std::numeric_limits<int>::max() : 0;
				return dir > 0 ? hp : -hp;
			});
			return;
		}
		// Newest requires the manifests cache; fall back to "Recently updated" if it isn't available.
		BrowserSort sort = (browserSort == BrowserSort::NEWEST && !Mb::manifestsCacheExists())
			? BrowserSort::UPDATED : browserSort;

		if (sort == BrowserSort::NEWEST) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				int64_t ts = Mb::manifestCreationTimestampGet(m->model);
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(-ts, p->brand, p->name, order);
			});
		}
		else if (sort == BrowserSort::UPDATED) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(-p->modifiedTimestamp, p->brand, p->name, order);
			});
		}
		else if (sort == BrowserSort::LAST_USED) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				int64_t ts = modelUsageTimestamp(m->model);
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(-ts, -p->modifiedTimestamp, p->brand, p->name, order);
			});
		}
		else if (sort == BrowserSort::MOST_USED) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				int count = modelUsageCount(m->model);
				int64_t ts = modelUsageTimestamp(m->model);
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(-count, -ts, -p->modifiedTimestamp, p->brand, p->name, order);
			});
		}
		else if (sort == BrowserSort::BRAND) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(p->brand, p->name, order);
			});
		}
		else if (sort == BrowserSort::NAME) {
			sortModelContainer(modelContainer, [](ModelBox* m) {
				return std::make_tuple(m->model->name, m->model->plugin->brand);
			});
		}
		else if (sort == BrowserSort::RANDOM) {
			std::vector<std::reference_wrapper<Widget*>> vec(modelContainer->children.begin(), modelContainer->children.end());
			std::random_device rd;
			std::mt19937 g(rd());
			std::shuffle(vec.begin(), vec.end(), g);
			std::list<Widget*> s(vec.begin(), vec.end());
			modelContainer->children.swap(s);
		}
	};

	if (search.empty()) {
		for (Widget* w : modelContainer->children) {
			ModelBox* m = reinterpret_cast<ModelBox*>(w);
			prefilteredModelScores[m->model] = 1.f;
			if (hidden && m->visible) m->modelHidden = isModelHidden(m->model);
		}
		applyBrowserSort();
	}
	else {
		auto results = modelDb.search(search);
		for (auto& result : results) {
			prefilteredModelScores[result.key] = result.score;
		}

		if (Mb::sortBySearchScore) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				return -get(prefilteredModelScores, m->model, 0.f);
			});
		}
		else {
			applyBrowserSort();
		}

		for (Widget* w : modelContainer->children) {
			ModelBox* m = reinterpret_cast<ModelBox*>(w);
			if (m->visible && prefilteredModelScores.find(m->model) == prefilteredModelScores.end()) {
				m->visible = false;
			}
			if (hidden && m->visible) m->modelHidden = isModelHidden(m->model);
		}
	}

	int count = 0;
	for (Widget* w : modelContainer->children) {
		if (w->visible) count++;
	}
	countLabel->text = string::f("Modules (%d)", count);
}

void ModuleBrowser::navigateSelection(int key) {
	// Collect visible ModelBoxes in layout order
	std::vector<ModelBox*> visible;
	for (Widget* w : modelContainer->children) {
		ModelBox* mb = reinterpret_cast<ModelBox*>(w);
		if (mb->visible) visible.push_back(mb);
	}
	if (visible.empty()) return;

	// Determine starting box: keyboard selection > hovered widget > first visible
	ModelBox* current = nullptr;
	if (selectedModel) {
		for (ModelBox* mb : visible) {
			if (mb->model == selectedModel) { current = mb; break; }
		}
	}
	if (!current) {
		// Walk up from hovered widget to find a ModelBox
		Widget* w = APP->event->getHoveredWidget();
		while (w && !current) {
			current = dynamic_cast<ModelBox*>(w);
			w = w->parent;
		}
		// Only use it if it's in the visible list
		if (current) {
			bool found = false;
			for (ModelBox* mb : visible) { if (mb == current) { found = true; break; } }
			if (!found) current = nullptr;
		}
	}
	if (!current) current = visible[0];

	ModelBox* next = current;

	switch (key) {
		case GLFW_KEY_RIGHT: {
			bool found = false;
			for (ModelBox* mb : visible) {
				if (found) { next = mb; break; }
				if (mb == current) found = true;
			}
			break;
		}
		case GLFW_KEY_LEFT: {
			ModelBox* prev = nullptr;
			for (ModelBox* mb : visible) {
				if (mb == current) { if (prev) next = prev; break; }
				prev = mb;
			}
			break;
		}
		case GLFW_KEY_DOWN: {
			float currentCenterY = current->box.getCenter().y;
			float currentCenterX = current->box.getCenter().x;
			// Find the top of the next row (smallest pos.y strictly below center)
			float nextRowY = std::numeric_limits<float>::max();
			for (ModelBox* mb : visible) {
				if (mb->box.pos.y > currentCenterY && mb->box.pos.y < nextRowY)
					nextRowY = mb->box.pos.y;
			}
			if (nextRowY < std::numeric_limits<float>::max()) {
				float minDist = std::numeric_limits<float>::max();
				for (ModelBox* mb : visible) {
					if (std::abs(mb->box.pos.y - nextRowY) < 2.f) {
						float dist = std::abs(mb->box.getCenter().x - currentCenterX);
						if (dist < minDist) { minDist = dist; next = mb; }
					}
				}
			}
			break;
		}
		case GLFW_KEY_UP: {
			float currentCenterX = current->box.getCenter().x;
			// Find the top of the previous row (largest pos.y strictly above current row)
			float prevRowY = -std::numeric_limits<float>::max();
			for (ModelBox* mb : visible) {
				if (mb->box.pos.y < current->box.pos.y && mb->box.pos.y > prevRowY)
					prevRowY = mb->box.pos.y;
			}
			if (prevRowY > -std::numeric_limits<float>::max()) {
				float minDist = std::numeric_limits<float>::max();
				for (ModelBox* mb : visible) {
					if (std::abs(mb->box.pos.y - prevRowY) < 2.f) {
						float dist = std::abs(mb->box.getCenter().x - currentCenterX);
						if (dist < minDist) { minDist = dist; next = mb; }
					}
				}
			}
			break;
		}
	}

	selectedModel = next->model;

	// Scroll the selected box into view (convert to modelScroll->container space)
	Rect r = next->box;
	r.pos = r.pos.plus(modelContainer->box.pos).plus(modelMargin->box.pos);
	modelScroll->scrollTo(r);
}

void ModuleBrowser::clear() {
	search = "";
	searchField->setText("");
	brand = "";
	tagIds = {};
	customTagFilter = {};
	widthFilterRef = 0;
	widthFilterMode = 0;
	widthSortDir = 0;
	favorite = false;
	refresh();
}

void ModuleBrowser::onShow(const event::Show& e) {
	math::Vec savedOffset = modelScroll->offset;
	refresh();
	modelScroll->offset = savedOffset;
	OpaqueWidget::onShow(e);
}

void ModuleBrowser::onHoverScroll(const event::HoverScroll& e) {
	if ((vcv::ui::getWindowMods() & RACK_MOD_MASK) == RACK_MOD_CTRL) {
		float zoomDelta = e.scrollDelta.y / 50.f / 12.f;
		float newZoom = math::clamp(settings::browserZoom + zoomDelta, -2.f, 1.f);
		if (newZoom != settings::browserZoom) {
			settings::browserZoom = newZoom;
			updateZoom();
		}
		e.consume(this);
		return;
	}
	OpaqueWidget::onHoverScroll(e);
}

} // namespace v2
} // namespace Mb
} // namespace StoermelderPackOne
