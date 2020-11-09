#include "Mb.hpp"
#include <widget/OpaqueWidget.hpp>
#include <widget/TransparentWidget.hpp>
#include <widget/ZoomWidget.hpp>
#include <ui/ScrollWidget.hpp>
#include <ui/SequentialLayout.hpp>
#include <ui/MarginLayout.hpp>
#include <ui/Label.hpp>
#include <ui/TextField.hpp>
#include <ui/MenuOverlay.hpp>
#include <ui/List.hpp>
#include <ui/MenuItem.hpp>
#include <ui/Button.hpp>
#include <ui/RadioButton.hpp>
#include <ui/ChoiceButton.hpp>
#include <ui/Tooltip.hpp>
#include <app/ModuleWidget.hpp>
#include <app/Scene.hpp>
#include <plugin.hpp>
#include <app.hpp>
#include <plugin/Model.hpp>
#include <string.hpp>
#include <history.hpp>
#include <settings.hpp>
#include <tag.hpp>

#include <set>
#include <algorithm>

namespace StoermelderPackOne {
namespace Mb {
namespace v06 {

static const float itemMargin = 2.0;

static std::string sAuthorFilter;
static int sTagFilter = -1;


bool isMatch(std::string s, std::string search) {
	s = string::lowercase(s);
	search = string::lowercase(search);
	return (s.find(search) != std::string::npos);
}

static bool isModelMatch(Model *model, std::string search) {
	if (search.empty())
		return true;
	std::string s;
	s += model->plugin->slug;
	s += " ";
	s += model->plugin->brand;
	s += " ";
	s += model->name;
	s += " ";
	s += model->slug;
	for (auto tag : model->tags) {
		s += " ";
		s += tag::tagAliases[tag][0];
	}
	return isMatch(s, search);
}


struct FavoriteRadioButton : RadioButton {
	Model *model = NULL;

	struct FavoriteQuantity : Quantity {
		float value = 0.f;
		void setValue(float v) override { value = v; }
		float getValue() override { return value; }
		std::string getLabel() override { return "★"; }
	};

	FavoriteRadioButton() {
		quantity = new FavoriteQuantity;
	}

	~FavoriteRadioButton() {
		delete quantity;
	}

	void onAction(const event::Action& e) override;
};


struct SeparatorItem : OpaqueWidget {
	SeparatorItem() {
		box.size.y = 2*BND_WIDGET_HEIGHT + 2*itemMargin;
	}

	void setText(std::string text) {
		clearChildren();
		Label *label = new Label;
		label->setPosition(Vec(0, 12 + itemMargin));
		label->text = text;
		label->fontSize = 20;
		label->color.a *= 0.5;
		addChild(label);
	}
};


struct BrowserListItem : OpaqueWidget {
	bool selected = false;

	BrowserListItem() {
		box.size.y = BND_WIDGET_HEIGHT + 2*itemMargin;
	}

	void draw(const DrawArgs& args) override {
		BNDwidgetState state = selected ? BND_HOVER : BND_DEFAULT;
		bndMenuItem(args.vg, 0.0, 0.0, box.size.x, box.size.y, state, -1, "");
		Widget::draw(args);
	}

	void onDragStart(const event::DragStart &e) override;

	void onDragDrop(const event::DragDrop &e) override {
		if (e.origin != this)
			return;
		doAction();
	}

	void doAction();
};


struct ModelItem : BrowserListItem {
	Model *model;
	Label *pluginLabel = NULL;

	void setModel(Model *model) {
		clearChildren();
		assert(model);
		this->model = model;

		FavoriteRadioButton *favoriteButton = new FavoriteRadioButton;
		favoriteButton->setPosition(Vec(8, itemMargin));
		favoriteButton->box.size.x = 20;
		//favoriteButton->quantity->label = "★";
		addChild(favoriteButton);

		// Set favorite button initial state
		auto it = favoriteModels.find(model);
		if (it != favoriteModels.end())
			favoriteButton->quantity->setValue(1);
		favoriteButton->model = model;

		Label *nameLabel = new Label;
		nameLabel->setPosition(favoriteButton->box.getTopRight());
		nameLabel->text = model->name;
		addChild(nameLabel);

		pluginLabel = new Label;
		pluginLabel->setPosition(Vec(0, itemMargin));
		pluginLabel->alignment = Label::RIGHT_ALIGNMENT;
		pluginLabel->text = model->plugin->slug + " " + model->plugin->version;
		pluginLabel->color.a = 0.5;
		addChild(pluginLabel);
	}

	void step() override {
		BrowserListItem::step();
		if (pluginLabel)
			pluginLabel->box.size.x = box.size.x - BND_SCROLLBAR_WIDTH;
	}

	void onAction(const event::Action &e) override {
		ModuleWidget *moduleWidget = model->createModuleWidget();
		if (!moduleWidget)
			return;
		APP->scene->rack->addModuleAtMouse(moduleWidget);

		// Push ModuleAdd history action
		history::ModuleAdd* h = new history::ModuleAdd;
		h->name = "create module";
		h->setModule(moduleWidget);
		APP->history->push(h);

		// Hide Module Browser
		APP->scene->moduleBrowser->hide();
		APP->event->setSelected(moduleWidget);

		// Update usage data
		modelUsageTouch(model);

		// Move module nearest to the mouse position
		//moduleWidget->box.pos = APP->scene->rack->mousePos.minus(moduleWidget->box.size.div(2));
		//APP->scene->rack->requestModulePos(moduleWidget, moduleWidget->box.pos);
		e.consume(moduleWidget);
	}
};


struct AuthorItem : BrowserListItem {
	std::string author;

	void setAuthor(std::string author) {
		clearChildren();
		this->author = author;
		Label *authorLabel = new Label;
		authorLabel->setPosition(Vec(0, 0 + itemMargin));
		if (author.empty())
			authorLabel->text = "Show all modules";
		else
			authorLabel->text = author;
		addChild(authorLabel);
	}

	void onAction(const event::Action &e) override;
};


struct TagItem : BrowserListItem {
	int tag;

	void setTag(int tag) {
		clearChildren();
		this->tag = tag;
		Label *tagLabel = new Label;
		tagLabel->setPosition(Vec(0, 0 + itemMargin));
		if (tag == -1)
			tagLabel->text = "Show all tags";
		else
			tagLabel->text = tag::tagAliases[tag][0];
		addChild(tagLabel);
	}

	void onAction(const event::Action &e) override;
};


struct ClearFilterItem : BrowserListItem {
	ClearFilterItem() {
		Label *label = new Label;
		label->setPosition(Vec(0, 0 + itemMargin));
		label->text = "Back";
		addChild(label);
	}

	void onAction(const event::Action &e) override;
};


struct BrowserList : List {
	int selected = 0;

	void step() override {
		incrementSelection(0);
		// Find and select item
		int i = 0;
		for (Widget *child : children) {
			BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
			if (item) {
				item->selected = (i == selected);
				i++;
			}
		}
		List::step();
	}

	void incrementSelection(int delta) {
		selected += delta;
		selected = clamp(selected, 0, countItems() - 1);
	}

	int countItems() {
		int n = 0;
		for (Widget *child : children) {
			BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
			if (item) {
				n++;
			}
		}
		return n;
	}

	void selectItem(Widget *w) {
		int i = 0;
		for (Widget *child : children) {
			BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
			if (item) {
				if (child == w) {
					selected = i;
					break;
				}
				i++;
			}
		}
	}

	BrowserListItem *getSelectedItem() {
		int i = 0;
		for (Widget *child : children) {
			BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
			if (item) {
				if (i == selected) {
					return item;
				}
				i++;
			}
		}
		return NULL;
	}

	void scrollSelected() {
		BrowserListItem *item = getSelectedItem();
		if (item) {
			ScrollWidget *parentScroll = dynamic_cast<ScrollWidget*>(parent->parent);
			if (parentScroll)
				parentScroll->scrollTo(item->box);
		}
	}
};


struct ModuleBrowser;

struct SearchModuleField : TextField {
	ModuleBrowser *moduleBrowser;
	void onChange(const event::Change& e) override;
	void onSelectKey(const event::SelectKey &e) override;

	void onShow(const event::Show& e) override {
		selectAll();
		TextField::onShow(e);
	}
};


struct ModuleBrowser : OpaqueWidget {
	SearchModuleField *searchField;
	ScrollWidget *moduleScroll;
	BrowserList *moduleList;
	std::set<std::string, string::CaseInsensitiveCompare> availableAuthors;
	std::set<int> availableTags;

	ModuleBrowser() {
		box.size.x = 450;
		sAuthorFilter = "";
		sTagFilter = -1;

		// Search
		searchField	= new SearchModuleField();
		searchField->box.size.x = box.size.x;
		searchField->moduleBrowser = this;
		addChild(searchField);

		moduleList = new BrowserList();
		moduleList->box.size = Vec(box.size.x, 0.0);

		// Module Scroll
		moduleScroll = new ScrollWidget();
		moduleScroll->box.pos.y = searchField->box.size.y;
		moduleScroll->box.size.x = box.size.x;
		moduleScroll->container->addChild(moduleList);
		addChild(moduleScroll);

		// Collect authors
		for (Plugin *plugin : rack::plugin::plugins) {
			for (Model *model : plugin->models) {
				// Insert author
				if (!model->plugin->brand.empty())
					availableAuthors.insert(model->plugin->brand);
				// Insert tag
				for (auto tag : model->tags) {
					if (tag != -1)
						availableTags.insert(tag);
				}
			}
		}

		// Trigger search update
		clearSearch();
		refreshSearch();
	}

	void draw(const DrawArgs& args) override {
		bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE);
		Widget::draw(args);
	}

	void clearSearch() {
		searchField->setText("");
	}

	bool isModelFiltered(Model *model) {
		if (!sAuthorFilter.empty() && model->plugin->brand != sAuthorFilter)
			return false;
		if (sTagFilter != -1) {
			auto it = std::find(model->tags.begin(), model->tags.end(), sTagFilter);
			if (it == model->tags.end())
				return false;
		}
		return true;
	}

	void refreshSearch() {
		std::string search = searchField->text;
		moduleList->clearChildren();
		moduleList->selected = 0;
		bool filterPage = !(sAuthorFilter.empty() && sTagFilter == -1);

		if (!filterPage) {
			// Favorites
			if (!favoriteModels.empty()) {
				SeparatorItem *item = new SeparatorItem();
				item->setText("Favorites");
				moduleList->addChild(item);
			}
			for (Model *model : favoriteModels) {
				if (isModelFiltered(model) && isModelMatch(model, search)) {
					ModelItem *item = new ModelItem();
					item->setModel(model);
					moduleList->addChild(item);
				}
			}
			// Author items
			{
				SeparatorItem *item = new SeparatorItem();
				item->setText("Authors");
				moduleList->addChild(item);
			}
			for (std::string author : availableAuthors) {
				if (isMatch(author, search)) {
					AuthorItem *item = new AuthorItem();
					item->setAuthor(author);
					moduleList->addChild(item);
				}
			}
			// Tag items
			{
				SeparatorItem *item = new SeparatorItem();
				item->setText("Tags");
				moduleList->addChild(item);
			}
			for (int tag : availableTags) {
				if (isMatch(tag::tagAliases[tag][0], search)) {
					TagItem *item = new TagItem();
					item->setTag(tag);
					moduleList->addChild(item);
				}
			}
		}
		else {
			// Clear filter
			ClearFilterItem *item = new ClearFilterItem();
			moduleList->addChild(item);
		}

		if (filterPage || !search.empty()) {
			if (!search.empty()) {
				SeparatorItem *item = new SeparatorItem();
				item->setText("Modules");
				moduleList->addChild(item);
			}
			else if (filterPage) {
				SeparatorItem *item = new SeparatorItem();
				if (!sAuthorFilter.empty())
					item->setText(sAuthorFilter);
				else if (sTagFilter != -1)
					item->setText("Tag: " + tag::tagAliases[sTagFilter][0]);
				moduleList->addChild(item);
			}
			// Modules
			for (Plugin *plugin : rack::plugin::plugins) {
				for (Model *model : plugin->models) {
					if (isModelFiltered(model) && isModelMatch(model, search)) {
						ModelItem *item = new ModelItem();
						item->setModel(model);
						moduleList->addChild(item);
					}
				}
			}
		}
	}

	void step() override {
		if (!visible) return;
		box.pos = parent->box.size.minus(box.size).div(2).round();
		box.pos.y = 60;
		box.size.y = parent->box.size.y - 2 * box.pos.y;
		moduleScroll->box.size.y = std::min(box.size.y - moduleScroll->box.pos.y, moduleList->box.size.y);
		box.size.y = std::min(box.size.y, moduleScroll->box.getBottomRight().y);

		APP->event->setSelected(searchField);
		Widget::step();
	}
};


// Implementations of inline methods above

void BrowserListItem::doAction() {
	event::Context context;
	event::Action eAction;
	eAction.context = &context;
	//eAction.consume(this);
	onAction(eAction);
	if (eAction.isConsumed()) {
		//Mb::BrowserOverlay* overlay = getAncestorOfType<Mb::BrowserOverlay>();
		//overlay->hide();
		ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
		sAuthorFilter = "";
		sTagFilter = -1;
		moduleBrowser->clearSearch();
		moduleBrowser->refreshSearch();
	}
}

void AuthorItem::onAction(const event::Action &e) {
	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	sAuthorFilter = author;
	moduleBrowser->clearSearch();
	moduleBrowser->refreshSearch();
	//e.isConsumed = false;
}

void TagItem::onAction(const event::Action &e) {
	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	sTagFilter = tag;
	moduleBrowser->clearSearch();
	moduleBrowser->refreshSearch();
	//e.isConsumed = false;
}

void ClearFilterItem::onAction(const event::Action &e) {
	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	sAuthorFilter = "";
	sTagFilter = -1;
	moduleBrowser->refreshSearch();
	//e.isConsumed = false;
}

void FavoriteRadioButton::onAction(const event::Action &e) {
	if (!model)
		return;
	if (quantity->getValue() > 0.f) {
		favoriteModels.insert(model);
	}
	else {
		auto it = favoriteModels.find(model);
		if (it != favoriteModels.end())
			favoriteModels.erase(it);
	}

	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	if (moduleBrowser)
		moduleBrowser->refreshSearch();
}

void BrowserListItem::onDragStart(const event::DragStart &e) {
	BrowserList *list = dynamic_cast<BrowserList*>(parent);
	if (list) {
		list->selectItem(this);
	}
}

void SearchModuleField::onChange(const event::Change& e) {
	moduleBrowser->refreshSearch();
}

void SearchModuleField::onSelectKey(const event::SelectKey &e) {
	if (e.action == GLFW_PRESS) {
		switch (e.key) {
			case GLFW_KEY_ESCAPE: {
				BrowserOverlay* overlay = getAncestorOfType<BrowserOverlay>();
				overlay->hide();
				APP->event->setSelected(NULL);
				e.consume(this);
				return;
			} break;
			case GLFW_KEY_UP: {
				moduleBrowser->moduleList->incrementSelection(-1);
				moduleBrowser->moduleList->scrollSelected();
				e.consume(this);
			} break;
			case GLFW_KEY_DOWN: {
				moduleBrowser->moduleList->incrementSelection(1);
				moduleBrowser->moduleList->scrollSelected();
				e.consume(this);
			} break;
			case GLFW_KEY_PAGE_UP: {
				moduleBrowser->moduleList->incrementSelection(-5);
				moduleBrowser->moduleList->scrollSelected();
				e.consume(this);
			} break;
			case GLFW_KEY_PAGE_DOWN: {
				moduleBrowser->moduleList->incrementSelection(5);
				moduleBrowser->moduleList->scrollSelected();
				e.consume(this);
			} break;
			case GLFW_KEY_ENTER: {
				BrowserListItem *item = moduleBrowser->moduleList->getSelectedItem();
				if (item) {
					item->doAction();
					e.consume(this);
					return;
				}
			} break;
		}
	}

	if (!e.isConsumed()) {
		TextField::onSelectKey(e);
	}
}


} // namespace v06
} // namespace Mb
} // namespace StoermelderPackOne