#include "Mb_v2.hpp"
#include <tag.hpp>
#include <settings.hpp>
#include <componentlibrary.hpp>
#include <thread>
#include <algorithm>
#include <numeric>

namespace StoermelderPackOne {
namespace Mb {
namespace v2 {

static ModuleWidget* chooseModel(plugin::Model* model) {
	engine::Module* addedModule = model->createModule();
	APP->engine->addModule(addedModule);

	ModuleWidget* moduleWidget = model->createModuleWidget(addedModule);
	assert(moduleWidget);
	APP->scene->rack->addModuleAtMouse(moduleWidget);

	moduleWidget->loadTemplate();

	history::ModuleAdd* h = new history::ModuleAdd;
	h->name = "create module";
	h->setModule(moduleWidget);
	APP->history->push(h);

	APP->scene->browser->hide();
	modelUsageTouch(model);
	return moduleWidget;
}

static void toggleModelFavorite(plugin::Model* model) {
	auto it = favoriteModels.find(model);
	if (it != favoriteModels.end())
		favoriteModels.erase(model);
	else
		favoriteModels.insert(model);
	hiddenModels.erase(model);

	ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
	if (browser && browser->favorite)
		browser->refresh();
}

static void toggleModelHidden(plugin::Model* model) {
	auto it = hiddenModels.find(model);
	if (it != hiddenModels.end())
		hiddenModels.erase(model);
	else
		hiddenModels.insert(model);

	ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
	if (browser) browser->refresh();
}

static bool isModelHidden(plugin::Model* model) {
	return hiddenModels.find(model) != hiddenModels.end();
}


static void openLayoutMenu(widget::Widget* button, std::vector<widget::Widget*> items) {
	// Container that draws a menu background and holds the layout
	struct MenuContainer : widget::OpaqueWidget {
		ui::ScrollWidget* scroll;
		ui::SequentialLayout* layout;

		MenuContainer() {
			scroll = new ui::ScrollWidget;
			addChild(scroll);

			// Create horizontal sequential layout inside container
			layout = new ui::SequentialLayout;
			layout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
			//layout->alignment = ui::SequentialLayout::CENTER_ALIGNMENT;
			layout->margin = Vec(5, 5);
			layout->spacing = Vec(5, 5);
			layout->box.size.y = 1.f;
			scroll->container->addChild(layout);
		}

		void step() override {
			box.size.y = std::min(layout->box.size.y, parent->box.size.y - box.pos.y - 20.f);
			scroll->box.size = box.size;
			layout->box.size.x = box.size.x;
			OpaqueWidget::step();
		}

		void draw(const widget::Widget::DrawArgs& args) override {
			//nvgFontFaceId(ctx, bnd_font);
        	nvgFontSize(args.vg, BND_LABEL_FONT_SIZE);		
			bndMenuBackground(args.vg, 0, 0, box.size.x, box.size.y, 0);
			OpaqueWidget::draw(args);
		}
	};

	auto browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
	Vec browserPos = browser->getAbsoluteOffset(Vec(0, 0));

	// Create menu container
	MenuContainer* container = new MenuContainer;
	float menuX = browserPos.x + browser->box.size.x * 0.15f;
	float menuY = button->getAbsoluteOffset(Vec(0, button->box.size.y)).y + 2.f;
	container->box.pos = Vec(menuX, menuY);
	container->box.size.x = browser->box.size.x * 0.7f;

	// Add items to layout
	for (widget::Widget* item : items) {
		container->layout->addChild(item);
	}

	ui::MenuOverlay* overlay = new ui::MenuOverlay;
	APP->scene->addChild(overlay);
	overlay->addChild(container);
}


// Widgets

struct ModuleWidgetContainer : widget::Widget {
	void draw(const DrawArgs& args) override {
		Widget::draw(args);
		Widget::drawLayer(args, 1);
	}
};


struct ModelBox : widget::OpaqueWidget {
	plugin::Model* model = NULL;
	ui::Tooltip* tooltip = NULL;
	widget::Widget* previewWidget = NULL;
	widget::ZoomWidget* zoomWidget = NULL;
	widget::FramebufferWidget* fb = NULL;
	ModuleWidgetContainer* mwc = NULL;
	ModuleWidget* moduleWidget = NULL;

	void setModel(plugin::Model* m) {
		model = m;
		updateZoom();
	}

	void updateZoom() {
		float zoom = std::pow(2.f, settings::browserZoom);
		if (previewWidget) {
			fb->setDirty();
			zoomWidget->setZoom(zoom);
			box.size.x = moduleWidget->box.size.x * zoom;
		}
		else {
			box.size.x = 12 * RACK_GRID_WIDTH * zoom;
		}
		box.size.y = RACK_GRID_HEIGHT * zoom;
		box.size = box.size.ceil();
	}

	void createPreview() {
		if (previewWidget) return;

		previewWidget = new widget::TransparentWidget;
		addChild(previewWidget);

		zoomWidget = new widget::ZoomWidget;
		previewWidget->addChild(zoomWidget);

		fb = new widget::FramebufferWidget;
		if (math::isNear(APP->window->pixelRatio, 1.0)) {
			fb->oversample = 2.0;
		}
		zoomWidget->addChild(fb);

		mwc = new ModuleWidgetContainer;
		fb->addChild(mwc);

		moduleWidget = model->createModuleWidget(NULL);
		mwc->addChild(moduleWidget);
		mwc->box.size = moduleWidget->box.size;

		moduleWidget->step();
		updateZoom();
	}

	void draw(const DrawArgs& args) override {
		createPreview();

		nvgBeginPath(args.vg);
		float r = 10;
		float c = 5;
		nvgRect(args.vg, -r, -r, box.size.x + 2 * r, box.size.y + 2 * r);
		NVGcolor shadowColor = nvgRGBAf(0, 0, 0, 0.5);
		NVGcolor transparentColor = nvgRGBAf(0, 0, 0, 0);
		nvgFillPaint(args.vg, nvgBoxGradient(args.vg, 0, 0, box.size.x, box.size.y, c, r, shadowColor, transparentColor));
		nvgFill(args.vg);

		float b = math::clamp(settings::rackBrightness + 0.2f, 0.f, 1.f);
		nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 1));

		OpaqueWidget::draw(args);

		if (favoriteModels.find(model) != favoriteModels.end()) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgStrokeWidth(args.vg, 2);
			nvgStrokeColor(args.vg, componentlibrary::SCHEME_YELLOW);
			nvgStroke(args.vg);
		}
	}

	void setTooltip(ui::Tooltip* tt) {
		if (tooltip) {
			tooltip->requestDelete();
			tooltip = NULL;
		}
		if (tt) {
			APP->scene->addChild(tt);
			tooltip = tt;
		}
	}

	void onButton(const event::Button& e) override {
		OpaqueWidget::onButton(e);

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0) {
			ModuleWidget* mw = chooseModel(model);
			e.consume(mw);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			toggleModelFavorite(model);
			e.consume(this);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
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
		std::string text = model->plugin->brand + " " + model->name;
		text += "\nTags: ";
		int i = 0;
		for (int tagId : model->tagIds) {
			if (i++ > 0) text += ", ";
			text += rack::tag::tagAliases[tagId][0];
		}
		if (!model->description.empty())
			text += "\n" + model->description;
		ui::Tooltip* tt = new ui::Tooltip;
		tt->text = text;
		setTooltip(tt);
	}

	void onLeave(const event::Leave& e) override {
		setTooltip(NULL);
	}

	void onHide(const event::Hide& e) override {
		setTooltip(NULL);
		OpaqueWidget::onHide(e);
	}

	void createContextMenu() {
		Menu* menu = createMenu();

		struct FilterBrandItem : MenuItem {
			std::string brand;
			void onAction(const event::Action& e) override {
				ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
				if (browser) {
					browser->brand = brand;
					browser->refresh();
				}
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, model->plugin->name.c_str()));
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, model->name.c_str()));
		menu->addChild(construct<FilterBrandItem>(
			&MenuItem::text, string::f("Filter by \"%s\"", model->plugin->brand.c_str()),
			&FilterBrandItem::brand, model->plugin->brand));
		menu->addChild(new MenuSeparator);

		struct ModuleUrlItem : MenuItem {
			std::string url;
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, url);
				t.detach();
			}
		};

		bool sep = false;
		if (!model->plugin->pluginUrl.empty()) {
			ModuleUrlItem* item = new ModuleUrlItem;
			item->text = "Website";
			item->url = model->plugin->pluginUrl;
			menu->addChild(item);
			sep = true;
		}
		if (!model->plugin->manualUrl.empty()) {
			ModuleUrlItem* item = new ModuleUrlItem;
			item->text = "Manual";
			item->url = model->plugin->manualUrl;
			menu->addChild(item);
			sep = true;
		}

		struct FavoriteModelItem : MenuItem {
			plugin::Model* model;
			FavoriteModelItem(plugin::Model* m) {
				text = "Favorite";
				model = m;
			}
			void onAction(const event::Action& e) override {
				toggleModelFavorite(model);
			}
			void step() override {
				rightText = string::f("%s %s", CHECKMARK(favoriteModels.find(model) != favoriteModels.end()), RACK_MOD_CTRL_NAME "+F");
				MenuItem::step();
			}
		};

		struct HiddenModelItem : MenuItem {
			plugin::Model* model;
			HiddenModelItem(plugin::Model* m) {
				text = "Hide";
				model = m;
			}
			void onAction(const event::Action& e) override {
				toggleModelHidden(model);
			}
			void step() override {
				rightText = string::f("%s %s", CHECKMARK(isModelHidden(model)), RACK_MOD_CTRL_NAME "+H");
				MenuItem::step();
			}
		};

		if (sep) menu->addChild(new MenuSeparator);
		menu->addChild(new FavoriteModelItem(model));
		menu->addChild(new HiddenModelItem(model));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Custom Tags"));

		struct NewCustomTagField : ui::TextField {
			plugin::Model* model;

			void onSelectKey(const event::SelectKey& e) override {
				if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
					std::string tag = string::trim(text);
					if (!tag.empty()) {
						for (const auto& existing : customTagsAll()) {
							if (string::lowercase(existing) == string::lowercase(tag)) {
								tag = existing;
								break;
							}
						}
						customTagAdd(model, tag);
						ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
						if (browser) browser->refresh();
					}
					ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
					if (overlay) overlay->requestDelete();
					e.consume(this);
					return;
				}
				if (!e.getTarget())
					ui::TextField::onSelectKey(e);
			}
		};

		struct ToggleCustomTagItem : MenuItem {
			plugin::Model* model;
			std::string tagName;
			void onAction(const event::Action& e) override {
				if (customTagHas(model, tagName))
					customTagRemove(model, tagName);
				else
					customTagAdd(model, tagName);
				ModuleBrowser* browser = APP->scene->getFirstDescendantOfType<ModuleBrowser>();
				if (browser) browser->refresh();
			}
			void step() override {
				rightText = CHECKMARK(customTagHas(model, tagName));
				MenuItem::step();
			}
		};

		NewCustomTagField* ntf = new NewCustomTagField;
		ntf->box.size.x = 150.f;
		ntf->placeholder = "New tag...";
		ntf->model = model;
		menu->addChild(ntf);
		APP->event->setSelectedWidget(ntf);

		for (const auto& tag : customTagsAll()) {
			ToggleCustomTagItem* item = new ToggleCustomTagItem;
			item->text = tag;
			item->model = model;
			item->tagName = tag;
			menu->addChild(item);
		}
	}
};


struct BrowserSearchField : ui::TextField {
	ModuleBrowser* browser;

	void step() override {
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
				return;
			}
			case GLFW_KEY_BACKSPACE: {
				if (text == "") {
					if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
						browser->clear();
					}
					e.consume(this);
				}
				break;
			}
			case GLFW_KEY_SPACE: {
				if (string::trim(text) == "" && (e.mods & RACK_MOD_MASK) == 0) {
					if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
						browser->favorite ^= true;
						browser->refresh();
					}
					setText("");
					propagate = false;
					e.consume(this);
				}
				if ((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL || (e.mods & RACK_MOD_MASK) == RACK_MOD_SHIFT) {
					if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
						browser->hidden ^= true;
						browser->refresh();
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
		browser->search = string::trim(text);
		browser->refresh();
	}

	void onAction(const event::Action& e) override {
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


struct FilterItem : ui::Button {
	ModuleBrowser* browser;
	bool disabled = false;
	std::string rawText;
	bool selected = false;

	void setRawText(std::string s) {
		rawText = s;
		text = s;

		NVGcontext* vg = APP->window->vg;
		nvgFontSize(vg, BND_LABEL_FONT_SIZE);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		float bounds[4];
		nvgTextBounds(vg, 0.f, 0.f, rawText.c_str(), NULL, bounds);
		box.size.x = bounds[2] - bounds[0] + 30.f;
		box.size.y = bounds[3] - bounds[1] + 8.f;
	}

	void onDragDrop(const DragDropEvent& e) override {
		if (!disabled) Button::onDragDrop(e);
	}

	void draw(const DrawArgs& args) override {
		text = string::f("%s %s", rawText, selected ? CHECKMARK(true) : "");

		BNDwidgetState state = BND_DEFAULT;
		if (!disabled) {
			if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
			if (APP->event->getDraggedWidget() == this) state = BND_ACTIVE;
		}
		if (disabled) nvgSave(args.vg);
		if (disabled) nvgGlobalAlpha(args.vg, 0.35f);
		bndToolButton(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE, state, -1, text.c_str());
		if (disabled) nvgRestore(args.vg);
	}
};


struct BrandItem : FilterItem {
	std::string brand;
	void onAction(const event::Action& e) override {
		browser->brand = (browser->brand == brand) ? "" : brand;
		browser->refresh();
	}
	void step() override {
		selected = (browser->brand == brand);
		FilterItem::step();
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
			item->disabled = (b != browser->brand) && !browser->hasVisibleModel(b, browser->tagIds, browser->favorite, browser->hidden, browser->customTagFilter);
			items.push_back(item);
		}

		openLayoutMenu(this, items);
	}

	void step() override {
		text = "Brand";
		if (!browser->brand.empty())
			text += ": " + browser->brand;
		text = string::ellipsize(text, 20);
		ChoiceButton::step();
	}
};


struct TagItem : FilterItem {
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
		FilterItem::step();
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
			if (browser->tagIds.count(id)) {
				item->disabled = false;
			} 
			else {
				std::set<int> newTagIds = browser->tagIds;
				newTagIds.insert(id);
				item->disabled = !browser->hasVisibleModel(browser->brand, newTagIds, browser->favorite, browser->hidden, browser->customTagFilter);
			}
			items.push_back(item);
		}

		openLayoutMenu(this, items);
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


struct CustomTagFilterItem : FilterItem {
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
		FilterItem::step();
	}
};

struct CustomTagButton : ui::ChoiceButton {
	ModuleBrowser* browser;

	void onAction(const event::Action& e) override {
		std::vector<widget::Widget*> items;

		auto allTags = customTagsAll();
		for (const auto& tag : allTags) {
			CustomTagFilterItem* item = new CustomTagFilterItem;
			item->setRawText(tag);
			item->tagName = tag;
			item->browser = browser;
			if (browser->customTagFilter.count(tag)) {
				item->disabled = false;
			} 
			else {
				std::set<std::string> newFilter = browser->customTagFilter;
				newFilter.insert(tag);
				item->disabled = !browser->hasVisibleModel(browser->brand, browser->tagIds, browser->favorite, browser->hidden, newFilter);
			}
			items.push_back(item);
		}

		openLayoutMenu(this, items);
	}

	void step() override {
		text = "Custom Tags";
		if (!browser->customTagFilter.empty()) {
			text += ": ";
			bool first = true;
			for (const auto& t : browser->customTagFilter) {
				if (!first) text += ", ";
				text += t;
				first = false;
			}
		}
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
		settings::BrowserSort sort;
		void onAction(const event::Action& e) override {
			settings::browserSort = sort;
			browser->refresh();
		}
		void step() override {
			rightText = CHECKMARK(settings::browserSort == (int)sort);
			MenuItem::step();
		}
	};

	void onAction(const event::Action& e) override {
		Menu* menu = createMenu();
		menu->box.pos = getAbsoluteOffset(Vec(0, box.size.y));
		menu->box.size.x = box.size.x;

		static const struct { settings::BrowserSort id; const char* name; } sorts[] = {
			{ settings::BROWSER_SORT_UPDATED, "Recently updated" },
			{ settings::BROWSER_SORT_LAST_USED, "Last used" },
			{ settings::BROWSER_SORT_MOST_USED, "Most used" },
			{ settings::BROWSER_SORT_BRAND, "Brand" },
			{ settings::BROWSER_SORT_NAME, "Module name" },
			{ settings::BROWSER_SORT_RANDOM, "Random" },
		};
		for (auto& s : sorts) {
			SortItem* item = new SortItem;
			item->text = s.name;
			item->sort = s.id;
			item->browser = browser;
			menu->addChild(item);
		}
	}

	void step() override {
		static const char* sortNames[] = {
			"Recently updated", "Last used", "Most used", "Brand", "Module name", "Random"
		};
		text = "Sort: ";
		int sort = (int)settings::browserSort;
		if (sort >= 0 && sort <= (int)settings::BROWSER_SORT_RANDOM)
			text += sortNames[sort];
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
		ModelBox* m1 = reinterpret_cast<ModelBox*>(w1);
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
	ctb->box.size.x = 150;
	ctb->browser = this;
	headerLayout->addChild(ctb);
	customTagButton = ctb;

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

	OpaqueWidget::step();
}

void ModuleBrowser::draw(const DrawArgs& args) {
	bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, 0);
	Widget::draw(args);
}

bool ModuleBrowser::isModelVisible(plugin::Model* model, const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter) {
	if (favorite && favoriteModels.find(model) == favoriteModels.end())
		return false;
	if (!brand.empty() && model->plugin->brand != brand)
		return false;
	for (int tagId : tagIds) {
		if (std::find(model->tagIds.begin(), model->tagIds.end(), tagId) == model->tagIds.end())
			return false;
	}
	for (const auto& ct : customTagFilter) {
		if (!customTagHas(model, ct))
			return false;
	}
	if (!hidden && hiddenModels.find(model) != hiddenModels.end())
		return false;
	return true;
}

bool ModuleBrowser::hasVisibleModel(const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter) {
	for (const auto& pair : prefilteredModelScores) {
		if (isModelVisible(pair.first, brand, tagIds, favorite, hidden, customTagFilter))
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

void ModuleBrowser::refresh() {
	modelScroll->offset = math::Vec();
	prefilteredModelScores.clear();

	for (Widget* w : modelContainer->children) {
		ModelBox* m = reinterpret_cast<ModelBox*>(w);
		m->visible = isModelVisible(m->model, brand, tagIds, favorite, hidden, customTagFilter);
	}

	auto applyBrowserSort = [&]() {
		if (settings::browserSort == settings::BROWSER_SORT_UPDATED) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(-p->modifiedTimestamp, p->brand, p->name, order);
			});
		}
		else if (settings::browserSort == settings::BROWSER_SORT_LAST_USED) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				auto u = modelUsage.find(m->model);
				int64_t ts = (u != modelUsage.end()) ? u->second->usedTimestamp : std::numeric_limits<int64_t>::min();
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(-ts, -p->modifiedTimestamp, p->brand, p->name, order);
			});
		}
		else if (settings::browserSort == settings::BROWSER_SORT_MOST_USED) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				auto u = modelUsage.find(m->model);
				int count = (u != modelUsage.end()) ? u->second->usedCount : 0;
				int64_t ts = (u != modelUsage.end()) ? u->second->usedTimestamp : std::numeric_limits<int64_t>::min();
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(-count, -ts, -p->modifiedTimestamp, p->brand, p->name, order);
			});
		}
		else if (settings::browserSort == settings::BROWSER_SORT_BRAND) {
			sortModelContainer(modelContainer, [this](ModelBox* m) {
				plugin::Plugin* p = m->model->plugin;
				int order = get(modelOrders, m->model, 0);
				return std::make_tuple(p->brand, p->name, order);
			});
		}
		else if (settings::browserSort == settings::BROWSER_SORT_NAME) {
			sortModelContainer(modelContainer, [](ModelBox* m) {
				return std::make_tuple(m->model->name, m->model->plugin->brand);
			});
		}
		else if (settings::browserSort == settings::BROWSER_SORT_RANDOM) {
			std::vector<std::reference_wrapper<Widget*>> vec(modelContainer->children.begin(), modelContainer->children.end());
			std::random_shuffle(vec.begin(), vec.end());
			std::list<Widget*> s(vec.begin(), vec.end());
			modelContainer->children.swap(s);
		}
	};

	if (search.empty()) {
		for (Widget* w : modelContainer->children) {
			ModelBox* m = reinterpret_cast<ModelBox*>(w);
			prefilteredModelScores[m->model] = 1.f;
		}

		applyBrowserSort();
	}
	else {
		auto results = modelDb.search(search);
		for (auto& result : results)
			prefilteredModelScores[result.key] = result.score;

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
			if (m->visible && prefilteredModelScores.find(m->model) == prefilteredModelScores.end())
				m->visible = false;
		}
	}

	int count = 0;
	for (Widget* w : modelContainer->children) {
		if (w->visible) count++;
	}
	countLabel->text = string::f("Modules (%d)", count);
}

void ModuleBrowser::clear() {
	search = "";
	searchField->setText("");
	brand = "";
	tagIds = {};
	customTagFilter = {};
	favorite = false;
	hidden = false;
	refresh();
}

void ModuleBrowser::onShow(const event::Show& e) {
	refresh();
	OpaqueWidget::onShow(e);
}

void ModuleBrowser::onHoverScroll(const event::HoverScroll& e) {
	if ((APP->window->getMods() & RACK_MOD_MASK) == RACK_MOD_CTRL) {
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
