#include "Mb_patch.hpp"
#include "Mb_patch_preview.hpp"
#include "Mb_patch_source.hpp"
#include <helpers.hpp>
#include <tag.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace patch {


// ---- Search Field ----

struct BrowserSearchField : ui::TextField {
	Browser* browser;
	/** Timer for debouncing search requests to remote APIs */
	float searchTimer = 0.f;
	/** Debounce delay in seconds - adjust for desired responsiveness vs API load */
	static constexpr float searchDebounceDelay = 0.5f;

	void step() override {
		widget::Widget* selected = APP->event->getSelectedWidget();
		if (!selected || !dynamic_cast<ui::TextField*>(selected)) {
			APP->event->setSelectedWidget(this);
		}
		TextField::step();

		// Process debounced search timer
		if (searchTimer > 0.f) {
			searchTimer -= APP->window->getLastFrameDuration();
			if (searchTimer <= 0.f) {
				searchTimer = 0.f;
				browser->searchQuery = string::trim(text);
				browser->sidebar->loadContainer();
			}
		}
	}

	void onSelectKey(const event::SelectKey& e) override {
		bool propagate = !e.getTarget();

		switch (e.key) {
			case GLFW_KEY_ESCAPE: {
				if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
					text = "";
					searchTimer = 0.f;
					browser->searchQuery = "";
					browser->sidebar->loadContainer();
				}
				e.consume(this);
				return;
			}
			case GLFW_KEY_BACKSPACE: {
				if (text == "") {
					if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
						searchTimer = 0.f;
						browser->searchQuery = "";
						browser->sidebar->loadContainer();
					}
					e.consume(this);
				}
				break;
			}
		}

		if (propagate) {
			ui::TextField::onSelectKey(e);
		}
	}

	void onChange(const event::Change& e) override {
		// Reset debounce timer - search will trigger after delay when typing stops
		searchTimer = searchDebounceDelay;
	}
};


// ---- Filter / Topbar ----

struct TagItem : ChoiceFilterItem<Browser> {
	PatchSourceIndex* index;
	std::string tagName;
	void onAction(const event::Action& e) override {
		auto it = browser->tagFilter.find(tagName);
		if (it != browser->tagFilter.end())
			browser->tagFilter.erase(it);
		else
			browser->tagFilter.insert(tagName);
		browser->sidebar->refreshFileList();
		e.unconsume();
	}
	void step() override {
		selected = browser->tagFilter.find(tagName) != browser->tagFilter.end();
		ChoiceFilterItem<Browser>::step();
	}
};

struct TagButton : Browser::PatchChoiceButton {
	void onAction(const event::Action& e) override {
		PatchSource* src = browser->getSource();
		if (!src) return;
		PatchSourceIndex* index = src->getIndex();
		if (!index) return;

		std::vector<Widget*> items;

		auto unsortedTags = index->getTagsAll();
		std::vector<std::string> tags(unsortedTags.begin(), unsortedTags.end());
		std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
			return string::lowercase(a) < string::lowercase(b);
		});
		for (const std::string& tag : tags) {
			TagItem* item = new TagItem;
			item->setRawText(tag);
			item->tagName = tag;
			item->browser = browser;
			item->index = index;
			items.push_back(item);
		}

		openLayoutMenu<Browser>(this, items);
	}

	void step() override {
		PatchSource* src = browser->getSource();
		bool hasIndex = src && src->getIndex() != nullptr;
		if (!hasIndex) {
			text = "Tag (no index)";
			ChoiceButton::step();
			return;
		}
		text = "Tag";
		if (!browser->tagFilter.empty()) {
			text += ": ";
			bool first = true;
			for (const std::string& t : browser->tagFilter) {
				if (!first) text += ", ";
				text += t;
				first = false;
			}
		}
		text = string::ellipsize(text, 20);
		ChoiceButton::step();
	}
};


struct CustomTagItem : ChoiceFilterItem<Browser> {
	std::string tagName;
	void onAction(const event::Action& e) override {
		auto it = browser->customTagFilter.find(tagName);
		if (it != browser->customTagFilter.end())
			browser->customTagFilter.erase(it);
		else
			browser->customTagFilter.insert(tagName);
		browser->sidebar->refreshFileList();
		e.unconsume();
	}
	void step() override {
		selected = browser->customTagFilter.find(tagName) != browser->customTagFilter.end();
		ChoiceFilterItem<Browser>::step();
	}
};

struct CustomTagButton : Browser::PatchChoiceButton {
	void onAction(const event::Action& e) override {
		PatchSource* src = browser->getSource();
		if (!src) return;
		PatchSourceIndex* index = src->getIndex();
		if (!index) return;

		std::vector<widget::Widget*> items;

		auto unsortedTags = index->getCustomTagsAll();
		std::vector<std::string> tags(unsortedTags.begin(), unsortedTags.end());
		std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
			return string::lowercase(a) < string::lowercase(b);
		});
		for (const std::string& tag : tags) {
			CustomTagItem* item = new CustomTagItem;
			item->setRawText(tag);
			item->tagName = tag;
			item->browser = browser;
			items.push_back(item);
		}

		openLayoutMenu<Browser>(this, items);
	}

	void step() override {
		PatchSource* src = browser->getSource();
		bool hasIndex = src && src->getIndex() != nullptr;
		if (!hasIndex) {
			text = "Custom Tag (no index)";
			ChoiceButton::step();
			return;
		}
		text = "Custom Tag";
		if (!browser->customTagFilter.empty()) {
			text += ": ";
			bool first = true;
			for (const std::string& t : browser->customTagFilter) {
				if (!first) text += ", ";
				text += t;
				first = false;
			}
		}
		text = string::ellipsize(text, 28);
		ChoiceButton::step();
	}
};


struct SourceItem : ui::MenuItem {
	Browser* browser;
	PatchSource* source;

	void onAction(const event::Action& e) override {
		browser->activeSourceIndex = -1;
		for (size_t i = 0; i < browser->sources.size(); i++) {
			if (browser->sources[i] == source) {
				browser->activeSourceIndex = (int)i;
				break;
			}
		}
		browser->preview->clearPatch();
		browser->sidebar->source = browser->getSource();
		browser->sidebar->loadContainer();
	}

	void step() override {
		PatchSource* active = browser->getSource();
		rightText = CHECKMARK(source == active);
		MenuItem::step();
	}
};

struct SourceButton : ui::ChoiceButton {
	Browser* browser;

	void onAction(const event::Action& e) override {
		ui::Menu* menu = createMenu();
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));
		menu->box.size.x = box.size.x;

		for (size_t i = 0; i < browser->sources.size(); i++) {
			PatchSource* src = browser->sources[i];
			SourceItem* item = new SourceItem;
			item->text = src->getSourceName();
			item->source = src;
			item->browser = browser;
			item->disabled = false;
			menu->addChild(item);
		}

		menu->addChild(new MenuSeparator);
		PatchSource* src = browser->getSource();
		if (src) {
			size_t i = menu->children.size();
			src->appendSourceMenuItems(menu);
			if (i != menu->children.empty()) {
				menu->addChild(new MenuSeparator);
			}
		}

		menu->addChild(createMenuItem("Set as favorite", CHECKMARK(pluginSettings.mbDataSourceFavoriteIndex == browser->activeSourceIndex), 
		[this] {
			if (browser->activeSourceIndex >= 0 && browser->activeSourceIndex < (int)browser->sources.size()) {
				pluginSettings.mbDataSourceFavoriteIndex = browser->activeSourceIndex;
			}
		}, browser->sources.empty() || pluginSettings.mbDataSourceFavoriteIndex == browser->activeSourceIndex));
		menu->addChild(createMenuItem("Remove source", "", [this] {
			if (browser->activeSourceIndex >= 0 && browser->activeSourceIndex < (int)browser->sources.size()) {
				browser->removeSource(browser->activeSourceIndex);
			}
		}, browser->sources.empty()));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Add .vcvs folder...", "", [this] {
			PatchSource* newSrc = filesystem::vcvs::createSource();
			if (newSrc) {
				browser->addSource(newSrc);
			}
		}));
		menu->addChild(createMenuItem("Add .vcv folder...", "", [this] {
			PatchSource* newSrc = filesystem::vcv::createSource();
			if (newSrc) {
				browser->addSource(newSrc);
			}
		}));
		menu->addChild(createMenuItem("Add PatchStorage source", "", [this] {
			PatchSource* newSrc = patchstorage::initSource();
			if (newSrc) {
				browser->addSource(newSrc);
			}
		}, !patchstorage::canCreate()));
	}

	void step() override {
		PatchSource* src = browser->getSource();
		if (src) {
			text = src->getSourceName();
		}
		else {
			text = "No source";
		}
		text = string::ellipsize(text, 40);
		ChoiceButton::step();
	}
};


// ---- Async Helper ----

struct AsyncContainerLoadResult {
	int generation;
	std::string container;
	std::vector<ContainerEntry> containers;
	std::vector<ContainerEntry> files;
};

struct AsyncContainerLoadWidget : widget::Widget {
	std::shared_ptr<AsyncContainerLoadResult> result;
	BrowserSidebar* sidebar;
	void step() override {
		if (result) {
			if (sidebar && sidebar->loadGeneration_ == result->generation)
				sidebar->populateList(result.get());
			requestDelete();
		}
		Widget::step();
	}
};


struct AsyncFileJsonResult {
	std::string fileId;
	json_t* json = nullptr;
};

struct AsyncFileJsonWidget : widget::Widget {
	std::shared_ptr<AsyncFileJsonResult> result;
	BrowserSidebar* sidebar;

	AsyncFileJsonWidget(BrowserSidebar* sb) : sidebar(sb) {}

	void step() override {
		if (result) {
			if (sidebar && result->json) {
				if (sidebar->currentFileId == result->fileId) {
					if (!sidebar->preview->setPatch(result->fileId, result->json))
						json_decref(result->json);
				} else {
					json_decref(result->json);
				}
				result->json = nullptr;
			}
			requestDelete();
		}
		Widget::step();
	}
};


// ---- Sidebar Items ----

struct ContainerListItem : ui::MenuItem {
	PatchSource* source;
	std::string containerPath;
	std::string containerName;

	void onAction(const event::Action& e) override {
		source->setContainer(containerPath);
		BrowserSidebar* sidebar = getAncestorOfType<BrowserSidebar>();
		if (sidebar) sidebar->loadContainer();
	}
};

struct PatchListItem : ui::MenuItem {
	BrowserSidebar* sidebar;
	std::string fileId;

	void onAction(const event::Action& e) override {
		sidebar->currentFileId = fileId;
		PatchSource* src = sidebar->source;
		if (src) {
			AsyncFileJsonWidget* asyncWidget = new AsyncFileJsonWidget(sidebar);
			APP->scene->addChild(asyncWidget);
			std::string fid = fileId;
			auto task = [asyncWidget, src, fid]() {
				auto res = std::make_shared<AsyncFileJsonResult>();
				res->json = src->getFileJson(fid);
				res->fileId = fid;
				asyncWidget->result = res;
			};
			sidebar->taskWorker.work(std::move(task));
		}
		sidebar->refreshDescriptionAndTags();
		e.consume(this);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			sidebar->preview->createContextMenu(fileId);
			e.consume(this);
			return;
		}
		MenuItem::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		if (fileId == sidebar->currentFileId) {
			nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 80));
			nvgBeginPath(args.vg);
			Rect r = Rect(0, 0, box.size.x, box.size.y).shrink(1.f);
			nvgRect(args.vg, RECT_ARGS(r));
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);
		}
		MenuItem::draw(args);
	}
};


struct DescriptionTextField : ui::TextField {
	BrowserSidebar* sidebar = nullptr;
	bool editMode = false;
	std::string rawText;

	DescriptionTextField() {
		multiline = true;
	}

	void setText(const std::string& newText) {
		rawText = newText;
		text = string::ellipsize(newText, 200);
	}

	void onShow(const event::Show& e) override {
		selectAll();
		TextField::onShow(e);
	}

	void onButton(const ButtonEvent& e) override {
		if (editMode) {
			TextField::onButton(e);
		}
		else {
			e.consume(this);
		}
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (sidebar && sidebar->source) {
			PatchSourceIndex* idx = sidebar->source->getIndex();
			if (idx && !idx->isReadOnly()) {
				text = rawText;
				editMode = true;
			}
		}
	}

	void onDeselect(const DeselectEvent& e) override {
		text = string::trim(text);
		PatchSourceIndex* idx = sidebar->source->getIndex();
		if (idx && !sidebar->currentFileId.empty()) {
			idx->setDescription(sidebar->currentFileId, text);	
		}
		
		rawText = text;
		text = string::ellipsize(text, 200);
		editMode = false;
	}

	void draw(const DrawArgs& args) override {
		if (editMode) {
			TextField::draw(args);
		}
		else {
			if (!text.empty()) {
				bndIconLabelValue(args.vg, 0.f, 0.f, box.size.x, box.size.y, -1,
					bndGetTheme()->menuTheme.textColor, BND_LEFT,
					BND_LABEL_FONT_SIZE, text.c_str(), NULL);
			}
			else {
				bndIconLabelValue(args.vg, 0.f, 0.f, box.size.x, box.size.y, -1,
					color::alpha(bndGetTheme()->menuTheme.textColor, 0.5), BND_LEFT,
					BND_LABEL_FONT_SIZE, "No description", NULL);
			}
		}
	}
};

struct FileTagItem : ChoiceFilterItem<Browser> {
	std::string tagName;
	bool isPredefined = true;

	void draw(const DrawArgs& args) override {
		BNDwidgetState state = BND_DEFAULT;
		if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
		if (APP->event->getDraggedWidget() == this) state = BND_ACTIVE;
		bndToolButton(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE, state, -1, rawText.c_str());
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		ChoiceFilterItem<Browser>::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("%s \"%s\"", isPredefined ? "Tag" : "Custom Tag", tagName)));
		menu->addChild(createMenuItem("Add to filter", "", [this]() {
			Browser* browser = APP->scene->getFirstDescendantOfType<Browser>();
			if (isPredefined) {
				if (browser->tagFilter.find(tagName) == browser->tagFilter.end()) {
					browser->tagFilter.insert(tagName);
					browser->sidebar->refreshFileList();
				}
			}
			else {
				if (browser->customTagFilter.find(tagName) == browser->customTagFilter.end()) {
					browser->customTagFilter.insert(tagName);
					browser->sidebar->refreshFileList();
				}
			}
		}));

		PatchSourceIndex* idx = browser->sidebar->source->getIndex();
		menu->addChild(createMenuItem("Remove tag", "", [this]() {
			Browser* browser = APP->scene->getFirstDescendantOfType<Browser>();
			PatchSourceIndex* idx = browser->sidebar->source->getIndex();
			if (isPredefined) {
				idx->removeTag(browser->sidebar->currentFileId, tagName);
			}
			else {
				idx->removeCustomTag(browser->sidebar->currentFileId, tagName);
			}
			browser->sidebar->refreshFileList();
			hide();
		}, !idx || idx->isReadOnly()));
	}
};


struct TagList : ScrollWidget {
	void onButton(const ButtonEvent& e) override {
		ScrollWidget::onButton(e);
		if (!e.isConsumed() && e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			Browser* browser = APP->scene->getFirstDescendantOfType<Browser>();
			if (!browser->preview->fileId.empty()) {
				browser->preview->createContextMenu(browser->preview->fileId);
				e.consume(this);
				return;
			}
		}
	}
};


// ---- Sidebar ----

BrowserSidebar::BrowserSidebar() {
	fileScroll = new ui::ScrollWidget;
	addChild(fileScroll);

	const float width = 286.f;

	fileList = new ui::List;
	fileList->hide();
	fileScroll->container->addChild(fileList);

	// Footer container for description and tags
	footerContainer = new widget::OpaqueWidget;
	footerContainer->box.size = Vec(width, 350.f);
	addChild(footerContainer);

	Widget* sp1 = new MenuSeparator;
	sp1->box.pos = Vec(0.f, 0.f);
	sp1->box.size = Vec(width, 6.f);
	footerContainer->addChild(sp1);

	descriptionField = new DescriptionTextField;
	descriptionField->box.pos = Vec(0.f, 10.f);
	descriptionField->box.size = Vec(width, 170.f);
	descriptionField->sidebar = this;
	footerContainer->addChild(descriptionField);

	Widget* sp2 = new MenuSeparator;
	sp2->box.pos = Vec(0.f, 180.f);
	sp2->box.size = Vec(width, 6.f);
	footerContainer->addChild(sp2);

	ScrollWidget* tagsScroll = new TagList;
	tagsScroll->box.pos = Vec(0, 190.f);
	tagsScroll->box.size = Vec(width, 160.f);
	footerContainer->addChild(tagsScroll);

	tagsLayout = new ui::SequentialLayout;
	tagsLayout->box.size.x = width;
	tagsLayout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
	tagsLayout->alignment = ui::SequentialLayout::LEFT_ALIGNMENT;
	tagsLayout->spacing = Vec(4, 4);
	tagsLayout->margin = Vec(0, 0);
	tagsScroll->container->addChild(tagsLayout);
}

BrowserSidebar::~BrowserSidebar() {
	if (source) {
		source->onDetach();
		delete source;
		source = nullptr;
	}
}

void BrowserSidebar::step() {
	fileScroll->box.size.y = box.size.y - footerContainer->box.size.y - 6.f;
	fileList->box.size.x = fileScroll->box.size.x = box.size.x;

	footerContainer->box.pos = Vec(0, box.size.y - footerContainer->box.size.y);

	widget::Widget::step();
}

void BrowserSidebar::loadContainer() {
	if (!source) return;

	// If there's a search query, perform search instead of loading containers
	Browser* browser = getAncestorOfType<Browser>();
	if (browser && !browser->searchQuery.empty()) {
		loadSearchResults(browser->searchQuery);
		return;
	}

	if (source->getContainer().empty())
		source->setContainer(source->getRootContainer());

	std::string container = source->getContainer();
	if (container.empty()) {
		fileList->clearChildren();
		return;
	}
	fileList->hide();


	int gen = ++loadGeneration_;
	AsyncContainerLoadWidget* asyncWidget = new AsyncContainerLoadWidget;
	asyncWidget->sidebar = this;
	APP->scene->addChild(asyncWidget);
	PatchSource* src = source;
	taskWorker.work([asyncWidget, src, container, gen]() {
		auto res = std::make_shared<AsyncContainerLoadResult>();
		res->generation = gen;
		res->container = std::move(container);
		res->containers = std::move(src->getContainers(container));
		res->files = std::move(src->getFiles(container));
		asyncWidget->result = std::move(res);
	});
}

void BrowserSidebar::loadSearchResults(const std::string& query) {
	if (!source) return;

	fileList->clearChildren();
	fileList->hide();

	int gen = ++loadGeneration_;
	AsyncContainerLoadWidget* asyncWidget = new AsyncContainerLoadWidget;
	asyncWidget->sidebar = this;
	APP->scene->addChild(asyncWidget);
	PatchSource* src = source;
	taskWorker.work([asyncWidget, src, query, gen]() {
		auto res = std::make_shared<AsyncContainerLoadResult>();
		res->generation = gen;
		res->container = "";
		res->containers = {}; // No containers when searching
		res->files = src->search(query);
		asyncWidget->result = std::move(res);
	});
}

void BrowserSidebar::populateList(const AsyncContainerLoadResult* res) {
	// Clear existing items
	fileList->clearChildren();

	// Add ".." item if not at root
	if (!res->container.empty() && !source->getContainer().empty() && source->getContainer() != source->getRootContainer()) {
		std::string parentContainer = source->getParentContainer(source->getContainer());
		ContainerListItem* upItem = new ContainerListItem;
		upItem->source = source;
		upItem->containerPath = parentContainer;
		upItem->containerName = "..";
		upItem->text = "📁 ..";
		upItem->box.size.x = fileList->box.size.x;
		fileList->addChild(upItem);
	}

	Browser* browser = getAncestorOfType<Browser>();
	if (browser && preview) {
		preview->browser = browser;
	}

	for (const ContainerEntry& folder : res->containers) {
		ContainerListItem* item = new ContainerListItem;
		item->source = source;
		item->containerPath = folder.id;
		item->containerName = folder.displayName;
		item->text = "📁 " + string::ellipsize(item->containerName, 42);
		item->box.size.x = fileList->box.size.x;
		fileList->addChild(item);
	}

	for (const ContainerEntry& file : res->files) {
		PatchListItem* item = new PatchListItem;
		item->sidebar = this;
		item->fileId = file.id;
		item->text = string::ellipsize(file.displayName, 40);
		item->box.size.x = fileList->box.size.x;
		item->rightText = source->getIndex()->isFavorite(file.id) ? "★" : "";
		fileList->addChild(item);
	}

	refreshFileList();
	refreshDescriptionAndTags();
	fileList->show();
	fileScroll->scrollTo(Rect());
}

void BrowserSidebar::refreshFileList() {
	Browser* browser = getAncestorOfType<Browser>();
	for (Widget* w : fileList->children) {
		PatchListItem* item = dynamic_cast<PatchListItem*>(w);
		if (!item) continue;
		if (browser->isFileTagFiltered(item->fileId))
			item->show();
		else
			item->hide();
	}
}

void BrowserSidebar::onShow(const event::Show& e) {
	if (source) {
		if (source->getContainer().empty()) {
			source->setContainer(source->getRootContainer());
		}
		if (!fileList->isVisible()) {
			loadContainer();
		}
	}
	widget::Widget::onShow(e);
}

void BrowserSidebar::refreshDescriptionAndTags() {
	tagsLayout->clearChildren();
	if (!source) {
		descriptionField->setText("");
		return;
	}

	PatchSourceIndex* idx = source->getIndex();
	if (!idx) {
		descriptionField->setText("");
		return;
	}
	if (currentFileId.empty()) {
		descriptionField->setText("");
		return;
	}

	descriptionField->setText(idx->getDescription(currentFileId));

	// Get predefined tags
	std::vector<std::string> fileTags = idx->getTags(currentFileId);
	std::vector<std::string> fileCustomTags = idx->getCustomTags(currentFileId);
	std::vector<FileTagItem*> items;

	// Add predefined tags
	for (const std::string& tag : fileTags) {
		FileTagItem* item = new FileTagItem;
		item->setRawText(tag);
		item->tagName = tag;
		item->isPredefined = true;
		item->browser = getAncestorOfType<Browser>();
		items.push_back(item);
	}

	// Add custom tags
	for (const std::string& tag : fileCustomTags) {
		FileTagItem* item = new FileTagItem;
		item->setRawText(tag);
		item->tagName = tag;
		item->isPredefined = false;
		item->browser = getAncestorOfType<Browser>();
		items.push_back(item);
	}

	std::sort(items.begin(), items.end(), [](FileTagItem* i1, FileTagItem* i2) { return i1->tagName < i2->tagName; });
	for (FileTagItem* item : items) tagsLayout->addChild(item);
}


// ---- Status Bar ----

/**
 * A status bar widget that shows messages from the current PatchSource.
 * Messages with "0:" prefix show indefinitely, "2:" prefix show for 2 seconds.
 * A semi-transparent background spans the full width while text is shown.
 */
struct StatusBarWidget : widget::Widget {
	Browser* browser;

	/** Timestamp when the status text should be cleared (0 = no status). */
	float statusDisplayUntil = 0.f;
	/** The last shown status text. */
	std::string lastStatus;

	void step() override {
		PatchSource* src = browser->getSource();
		if (src) {
			std::string currentStatus = src->getStatusText();
			if (currentStatus != lastStatus) {
				if (!currentStatus.empty()) {
					// Parse timeout from prefix (e.g., "0:" or "2:")
					float timeout = 2.0f;
					if (currentStatus.substr(0, 2) == "0:") {
						timeout = 0.0f;  // 0 means indefinite
						currentStatus = currentStatus.substr(2);
					} 
					else if (currentStatus.substr(0, 2) == "2:") {
						currentStatus = currentStatus.substr(2);
					}
					lastStatus = currentStatus;
					statusDisplayUntil = timeout > 0.0f ? glfwGetTime() + timeout : 0.0f;
				} 
				else {
					// Empty string means clear the status
					lastStatus = "";
					statusDisplayUntil = 0.0f;
				}
			}
		}

		float now = glfwGetTime();
		if (lastStatus.empty() && statusDisplayUntil > 0.0f && now >= statusDisplayUntil) {
			lastStatus = "";
			statusDisplayUntil = 0.0f;
		}

		Widget::step();
	}

	void draw(const DrawArgs& args) override {
		float now = glfwGetTime();
		std::string statusText;
		if (!lastStatus.empty()) {
			statusText = lastStatus;
		} else if (statusDisplayUntil > 0.0f && now < statusDisplayUntil) {
			// Show until timeout
		} else {
			statusText = "";
		}
		if (!statusText.empty()) {
			// Draw semi-transparent background on full width
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 80));
			nvgFill(args.vg);

			// Draw status text
			nvgFillColor(args.vg, bndGetTheme()->menuTheme.textColor);
			bndIconLabelValue(args.vg, 0.f, 0.f, box.size.x, box.size.y, -1,
				bndGetTheme()->menuTheme.textColor, BND_LEFT,
				BND_LABEL_FONT_SIZE, statusText.c_str(), NULL);
		}

		Widget::draw(args);
	}
};


// ---- Browser ----

Browser::Browser() {
	headerLayout = new ui::SequentialLayout;
	headerLayout->box.pos = math::Vec(0, 0);
	headerLayout->box.size.y = 0;
	headerLayout->margin = math::Vec(10, 10);
	headerLayout->spacing = math::Vec(10, 10);
	addChild(headerLayout);

	sourceButton = new SourceButton;
	sourceButton->box.size.x = 300;
	sourceButton->browser = this;
	headerLayout->addChild(sourceButton);

	searchField = new BrowserSearchField;
	searchField->box.size.x = 150;
	searchField->browser = this;
	headerLayout->addChild(searchField);

	tagButton = new TagButton;
	tagButton->box.size.x = 150;
	tagButton->browser = this;
	headerLayout->addChild(tagButton);

	customTagButton = new CustomTagButton;
	customTagButton->box.size.x = 200;
	customTagButton->browser = this;
	headerLayout->addChild(customTagButton);

	favoriteButton = new FavoriteButton;
	favoriteButton->box.size.x = 90;
	favoriteButton->text = "Favorites";
	favoriteButton->browser = this;
	headerLayout->addChild(favoriteButton);

	clearButton = new ClearButton;
	clearButton->box.size.x = 100;
	clearButton->text = "Reset filters";
	clearButton->browser = this;
	headerLayout->addChild(clearButton);

	sidebar = new BrowserSidebar;
	addChild(sidebar);

	preview = new PreviewWidget;
	preview->browser = this;
	addChild(preview);
	sidebar->preview = preview;

	statusBar = new StatusBarWidget;
	statusBar->browser = this;
	addChild(statusBar);

	// Handles cache clearing on application exit
	auto helper = PatchHelperWidget::getInstance();
	if (!helper) {
		helper = new PatchHelperWidget;
		APP->scene->menuBar->addChild(helper);
	}
}

Browser::~Browser() {
	for (PatchSource* source : sources) {
		if (source) {
			source->onDetach();
			delete source;
		}
	}
	sources.clear();
	// Sidebar destructor must not delete source since we own it
	sidebar->source = nullptr;
}

PatchSource* Browser::getSource() const {
	if (activeSourceIndex >= 0 && activeSourceIndex < (int)sources.size())
		return sources[activeSourceIndex];
	return nullptr;
}

void Browser::setSources(const std::vector<PatchSource*>& newSources, int activeIndex) {
	// Detach and delete old sources
	for (PatchSource* source : sources) {
		if (source) {
			source->onDetach();
			delete source;
		}
	}
	sources = newSources;
	activeSourceIndex = math::clamp(activeIndex, 0, (int)sources.size() - 1);
	for (PatchSource* source : sources) {
		if (source) source->onAttach();
	}
	sidebar->source = getSource();
}

void Browser::addSource(PatchSource* newSource) {
	if (!newSource) return;
	
	sources.push_back(newSource);
	std::sort(sources.begin(), sources.end(), 
		[](PatchSource* s1, PatchSource* s2) { return s1->getSourceName() < s2->getSourceName(); }
	);
	activeSourceIndex = (int)sources.size() - 1;
	PatchHelperWidget* helper = PatchHelperWidget::getInstance();
	newSource->setHelper(helper);
	newSource->setCacheDir(helper->cacheDir);
	newSource->onAttach();

	sidebar->source = getSource();
	sidebar->loadContainer();
}

void Browser::removeSource(int index) {
	if (index < 0 || index >= (int)sources.size()) return;
	PatchSource* removed = sources[index];
	if (removed) {
		removed->onDetach();
		delete removed;
	}
	sources.erase(sources.begin() + index);
	// Adjust active index
	if (activeSourceIndex >= (int)sources.size())
		activeSourceIndex = (int)sources.size() - 1;
	// Adjust favorite index - clear if favorite source was removed
	if (pluginSettings.mbDataSourceFavoriteIndex == index)
		pluginSettings.mbDataSourceFavoriteIndex = -1;
	else if (pluginSettings.mbDataSourceFavoriteIndex > index)
		pluginSettings.mbDataSourceFavoriteIndex--;
	sidebar->source = getSource();
	sidebar->loadContainer();
}

void Browser::clear() {
	tagFilter.clear();
	customTagFilter.clear();
	favoriteFilter = false;
	searchQuery = "";
	if (searchField) searchField->setText("");
	sidebar->loadContainer();
}

bool Browser::isFileTagFiltered(const std::string& fileId) const {
	PatchSource* src = getSource();
	if (!src) return true;
	PatchSourceIndex* index = src->getIndex();
	if (!index) return true;

	// Check favorite filter
	if (favoriteFilter && !index->isFavorite(fileId))
		return false;

	// Check predefined tag filter
	if (!tagFilter.empty()) {
		std::vector<std::string> fileTags = index->getTags(fileId);
		bool hasAllSelectedTags = true;
		for (const std::string& t : tagFilter) {
			if (std::find(fileTags.begin(), fileTags.end(), t) == fileTags.end()) {
				hasAllSelectedTags = false;
				break;
			}
		}
		if (!hasAllSelectedTags) return false;
	}

	// Check custom tag filter
	if (!customTagFilter.empty()) {
		std::vector<std::string> fileCustomTags = index->getCustomTags(fileId);
		bool hasAllSelectedCustomTags = true;
		for (const std::string& t : customTagFilter) {
			if (std::find(fileCustomTags.begin(), fileCustomTags.end(), t) == fileCustomTags.end()) {
				hasAllSelectedCustomTags = false;
				break;
			}
		}
		if (!hasAllSelectedCustomTags) return false;
	}

	return true;
}

void Browser::step() {
	if (!visible) return;
	box = parent->box.zeroPos().grow(math::Vec(-40, -40));

	const float margin = 20.f;

	headerLayout->box.size.x = box.size.x;

	sidebar->box.pos = Vec(12.f, headerLayout->box.getBottom() + margin);
	sidebar->box.size.x = 286.f;
	sidebar->box.size.y = box.size.y - headerLayout->box.getBottom() - 2 * margin;

	preview->box.pos = Vec(sidebar->box.size.x + 2 * margin, headerLayout->box.getBottom() + margin);
	preview->box.size.x = box.size.x - sidebar->box.size.x - 3 * margin;
	preview->box.size.y = sidebar->box.size.y;

	statusBar->box.pos = Vec(preview->box.pos.x, preview->box.pos.y + preview->box.size.y - 22.f);
	statusBar->box.size.x = preview->box.size.x;
	statusBar->box.size.y = 22.f;

	widget::OpaqueWidget::step();
}

void Browser::draw(const DrawArgs& args) {
	bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, 0);
	widget::OpaqueWidget::draw(args);
}

} // namespace patch
} // namespace Mb
} // namespace StoermelderPackOne