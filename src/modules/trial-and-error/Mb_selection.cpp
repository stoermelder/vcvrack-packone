#include "Mb_selection.hpp"
#include "Mb_selection_preview.hpp"
#include "Mb_selection_source.hpp"
#include <helpers.hpp>
#include <tag.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace selection {


struct AsyncContainerLoadResult {
	int generation;
	std::string container;
	std::vector<std::string> containers;
	std::vector<std::string> files;
};

struct AsyncContainerLoadWidget : widget::Widget {
	std::shared_ptr<AsyncContainerLoadResult> result;
	SelectionBrowserSidebar* sidebar;

	AsyncContainerLoadWidget(SelectionBrowserSidebar* sb) : sidebar(sb) {}

	void step() override {
		if (result) {
			if (sidebar && sidebar->loadGeneration_ == result->generation)
				sidebar->populateFileList(result.get());
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
	SelectionBrowserSidebar* sidebar;

	AsyncFileJsonWidget(SelectionBrowserSidebar* sb) : sidebar(sb) {}

	void step() override {
		if (result) {
			if (sidebar && result->json) {
				if (sidebar->currentFile == result->fileId) {
					if (!sidebar->preview->setSelection(result->fileId, result->json))
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


struct ContainerItem : ui::MenuItem {
	SelectionSource* source;
	std::string containerPath;
	std::string containerName;

	void onAction(const event::Action& e) override {
		source->setContainer(containerPath);
		SelectionBrowserSidebar* sidebar = getAncestorOfType<SelectionBrowserSidebar>();
		if (sidebar) sidebar->loadContainer();
	}
};

struct FileItem : ui::MenuItem {
	SelectionBrowserSidebar* sidebar;
	std::string fileId;
	void onAction(const event::Action& e) override {
		sidebar->currentFile = fileId;
		SelectionSource* src = sidebar->source;
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
		e.consume(this);
	}

	void draw(const DrawArgs& args) override {
		if (fileId == sidebar->currentFile) {
			nvgFillColor(args.vg, nvgRGBA(0xf0, 0xf0, 0xf0, 80));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgFill(args.vg);
		}
		MenuItem::draw(args);
	}
};


SelectionBrowserSidebar::SelectionBrowserSidebar() {
	fileScroll = new ui::ScrollWidget;
	addChild(fileScroll);

	fileList = new ui::List;
	fileScroll->container->addChild(fileList);
}

SelectionBrowserSidebar::~SelectionBrowserSidebar() {
	if (source) {
		source->onDetach();
		delete source;
		source = nullptr;
	}
}

void SelectionBrowserSidebar::step() {
	fileScroll->box.pos.y = 0.f;
	fileScroll->box.size.y = box.size.y;
	fileList->box.size.x = fileScroll->box.size.x = box.size.x;
	widget::Widget::step();
}

void SelectionBrowserSidebar::loadContainer() {
	if (!source) return;

	if (source->getContainer().empty())
		source->setContainer(source->getRootContainer());

	std::string container = source->getContainer();
	if (container.empty()) {
		fileList->clearChildren();
		return;
	}

	fileList->clearChildren();

	// Add ".." item synchronously (no I/O needed)
	if (container != source->getRootContainer()) {
		std::string parentContainer = source->getParentContainer(container);
		ContainerItem* upItem = new ContainerItem;
		upItem->source = source;
		upItem->containerPath = parentContainer;
		upItem->containerName = "..";
		upItem->text = "📁 ..";
		upItem->box.size.x = fileList->box.size.x;
		fileList->addChild(upItem);
	}

	int gen = ++loadGeneration_;
	AsyncContainerLoadWidget* asyncWidget = new AsyncContainerLoadWidget(this);
	APP->scene->addChild(asyncWidget);
	SelectionSource* src = source;
	taskWorker.work([asyncWidget, src, container, gen]() {
		auto res = std::make_shared<AsyncContainerLoadResult>();
		res->generation = gen;
		res->container = container;
		res->containers = src->getContainers(container);
		res->files = src->getFiles(container);
		asyncWidget->result = res;
	});
}

void SelectionBrowserSidebar::populateFileList(const AsyncContainerLoadResult* res) {
	SelectionBrowser* browser = getAncestorOfType<SelectionBrowser>();
	if (browser && preview)
		preview->browser = browser;

	for (const std::string& folder : res->containers) {
		ContainerItem* item = new ContainerItem;
		item->source = source;
		item->containerPath = folder;
		item->containerName = source->getFilename(folder);
		item->text = "📁 " + item->containerName;
		item->box.size.x = fileList->box.size.x;
		fileList->addChild(item);
	}

	for (const std::string& file : res->files) {
		if (browser && !browser->isFileTagFiltered(file))
			continue;
		FileItem* item = new FileItem;
		item->sidebar = this;
		item->fileId = file;
		item->text = source->getFilename(file);
		item->box.size.x = fileList->box.size.x;
		fileList->addChild(item);
	}
}

void SelectionBrowserSidebar::onShow(const event::Show& e) {
	if (source) {
		if (source->getContainer().empty())
			source->setContainer(source->getRootContainer());
		loadContainer();
	}
	widget::Widget::onShow(e);
}


// ---- Button implementations ----

struct TagItem : ChoiceFilterItem<SelectionBrowser> {
	SelectionSourceIndex* index;
	std::string fileId;
	std::string tagName;
	bool hasTag = false;
	void onAction(const event::Action& e) override {
		auto it = browser->tagFilter.find(tagName);
		if (it != browser->tagFilter.end())
			browser->tagFilter.erase(it);
		else
			browser->tagFilter.insert(tagName);
		browser->sidebar->loadContainer();
		e.unconsume();
	}
	void step() override {
		selected = hasTag;
		ChoiceFilterItem<SelectionBrowser>::step();
	}
};

struct TagButton : SelectionBrowser::SelectionChoiceButton {
	void onAction(const event::Action& e) override {
		SelectionSource* src = browser->getSource();
		if (!src) return;
		SelectionSourceIndex* index = src->getIndex();
		if (!index) return;

		// Compute fileId for the currently active file
		std::string previewFilePath = browser->preview->fileId;
		std::string rootDir = src->getRootContainer();
		std::string activeFileId;
		if (!previewFilePath.empty() && previewFilePath.size() > rootDir.size()) {
			activeFileId = previewFilePath.substr(rootDir.size() + 1);
		}

		// Get currently assigned tags for the active file
		std::set<std::string> activeFileTags;
		if (!activeFileId.empty()) {
			std::vector<std::string> ft = index->getTags(activeFileId);
			activeFileTags = std::set<std::string>(ft.begin(), ft.end());
		}

		std::vector<widget::Widget*> items;

		for (int id = 0; id < (int)rack::tag::tagAliases.size(); id++) {
			TagItem* item = new TagItem;
			item->setRawText(rack::tag::tagAliases[id][0]);
			item->tagName = rack::tag::tagAliases[id][0];
			item->browser = browser;
			item->index = index;
			item->fileId = activeFileId;
			item->hasTag = activeFileTags.count(item->tagName) > 0;
			items.push_back(item);
		}

		openLayoutMenu<SelectionBrowser>(this, items);
	}

	void step() override {
		SelectionSource* src = browser->getSource();
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


struct CustomTagItem : ChoiceFilterItem<SelectionBrowser> {
	std::string tagName;
	void onAction(const event::Action& e) override {
		auto it = browser->customTagFilter.find(tagName);
		if (it != browser->customTagFilter.end())
			browser->customTagFilter.erase(it);
		else
			browser->customTagFilter.insert(tagName);
		browser->sidebar->loadContainer();
		e.unconsume();
	}
	void step() override {
		selected = browser->customTagFilter.find(tagName) != browser->customTagFilter.end();
		ChoiceFilterItem<SelectionBrowser>::step();
	}
};

struct CustomTagButton : SelectionBrowser::SelectionChoiceButton {
	void onAction(const event::Action& e) override {
		SelectionSource* src = browser->getSource();
		if (!src) return;
		SelectionSourceIndex* index = src->getIndex();
		if (!index) return;

		std::vector<widget::Widget*> items;

		auto unsortedTags = customTagsAll();
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

		openLayoutMenu<SelectionBrowser>(this, items);
	}

	void step() override {
		SelectionSource* src = browser->getSource();
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


// ---- SourceButton and SourceItem ----

void SelectionBrowser::SourceButton::onAction(const event::Action& e) {
	ui::Menu* menu = createMenu();
	menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));
	menu->box.size.x = box.size.x;

	for (size_t i = 0; i < browser->sources.size(); i++) {
		SelectionSource* src = browser->sources[i];
		SourceItem* item = new SourceItem;
		item->text = src->getSourceName();
		item->source = src;
		item->browser = browser;
		item->disabled = false;
		menu->addChild(item);
	}

	if (!menu->children.empty()) {
		menu->addChild(new MenuSeparator);
	}

	menu->addChild(createMenuItem("Add .vcvs folder...", "", [this] {
		SelectionSource* newSrc = filesystem::vcvs::createSource();
		if (newSrc) {
			browser->addSource(newSrc);
		}
	}));
	menu->addChild(createMenuItem("Add .vcv folder...", "", [this] {
		SelectionSource* newSrc = filesystem::vcv::createSource();
		if (newSrc) {
			browser->addSource(newSrc);
		}
	}));
	menu->addChild(createMenuItem("Remove source", "", [this] {
		if (browser->activeSourceIndex >= 0 && browser->activeSourceIndex < (int)browser->sources.size()) {
			browser->removeSource(browser->activeSourceIndex);
		}
	}, browser->sources.empty()));
}


void SelectionBrowser::SourceButton::step() {
	SelectionSource* src = browser->getSource();
	if (src) {
		text = src->getSourceName();
	}
	else {
		text = "No source";
	}
	text = string::ellipsize(text, 40);
	ChoiceButton::step();
}

void SelectionBrowser::SourceItem::onAction(const event::Action& e) {
	browser->activeSourceIndex = -1;
	for (size_t i = 0; i < browser->sources.size(); i++) {
		if (browser->sources[i] == source) {
			browser->activeSourceIndex = (int)i;
			break;
		}
	}
	browser->preview->clearSelection();
	browser->sidebar->source = browser->getSource();
	browser->sidebar->loadContainer();
}

void SelectionBrowser::SourceItem::step() {
	SelectionSource* active = browser->getSource();
	rightText = CHECKMARK(source == active);
	MenuItem::step();
}


// ---- SelectionBrowser ----

SelectionBrowser::SelectionBrowser() {
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

	sidebar = new SelectionBrowserSidebar;
	addChild(sidebar);

	preview = new SelectionPreviewWidget;
	preview->browser = this;
	addChild(preview);
	sidebar->preview = preview;

	// Handles cache clearing on application exit
	auto helper = SelectionBrowserHelper::getInstance();
	if (!helper) {
		helper = new SelectionBrowserHelper;
		APP->scene->menuBar->addChild(helper);
	}
}

SelectionBrowser::~SelectionBrowser() {
	for (SelectionSource* source : sources) {
		if (source) {
			source->onDetach();
			delete source;
		}
	}
	sources.clear();
	// Sidebar destructor must not delete source since we own it
	sidebar->source = nullptr;
}

SelectionSource* SelectionBrowser::getSource() const {
	if (activeSourceIndex >= 0 && activeSourceIndex < (int)sources.size())
		return sources[activeSourceIndex];
	return nullptr;
}

void SelectionBrowser::setSources(const std::vector<SelectionSource*>& newSources, int activeIndex) {
	// Detach and delete old sources
	for (SelectionSource* source : sources) {
		if (source) {
			source->onDetach();
			delete source;
		}
	}
	sources = newSources;
	activeSourceIndex = math::clamp(activeIndex, 0, (int)sources.size() - 1);
	for (SelectionSource* source : sources) {
		if (source) source->onAttach();
	}
	sidebar->source = getSource();
}

void SelectionBrowser::addSource(SelectionSource* newSource) {
	if (!newSource) return;
	
	sources.push_back(newSource);
	activeSourceIndex = (int)sources.size() - 1;
	SelectionBrowserHelper* helper = SelectionBrowserHelper::getInstance();
	newSource->setHelper(helper);
	newSource->setCacheDir(helper->cacheDir);
	newSource->onAttach();

	sidebar->source = getSource();
	sidebar->loadContainer();
}

void SelectionBrowser::removeSource(int index) {
	if (index < 0 || index >= (int)sources.size()) return;
	SelectionSource* removed = sources[index];
	if (removed) {
		removed->onDetach();
		delete removed;
	}
	sources.erase(sources.begin() + index);
	// Adjust active index
	if (activeSourceIndex >= (int)sources.size())
		activeSourceIndex = (int)sources.size() - 1;
	sidebar->source = getSource();
	sidebar->loadContainer();
}

void SelectionBrowser::clear() {
	tagFilter.clear();
	customTagFilter.clear();
	favoriteFilter = false;
	sidebar->loadContainer();
}

bool SelectionBrowser::isFileTagFiltered(const std::string& fileId) const {
	SelectionSource* src = getSource();
	if (!src) return true;
	SelectionSourceIndex* index = src->getIndex();
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

void SelectionBrowser::step() {
	if (!visible) return;
	box = parent->box.zeroPos().grow(math::Vec(-40, -40));

	const float margin = 20.f;

	headerLayout->box.size.x = box.size.x;

	sidebar->box.pos = Vec(margin, headerLayout->box.getBottom() + margin);
	sidebar->box.size.x = 270.f;
	sidebar->box.size.y = box.size.y - headerLayout->box.getBottom() - 2 * margin;

	preview->box.pos = Vec(sidebar->box.size.x + 2 * margin, headerLayout->box.getBottom() + margin);
	preview->box.size.x = box.size.x - sidebar->box.size.x - 3 * margin;
	preview->box.size.y = sidebar->box.size.y;

	widget::OpaqueWidget::step();
}

void SelectionBrowser::draw(const DrawArgs& args) {
	bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, 0);
	widget::OpaqueWidget::draw(args);

	// Update status display time when source status changes
	// Status format: "0:message" shows indefinitely, "2:message" shows for 2 seconds then clears
	SelectionSource* src = getSource();
	if (src) {
		std::string currentStatus = src->getStatus();
		if (currentStatus != lastStatus) {
			if (!currentStatus.empty()) {
				// Parse timeout from prefix (e.g., "0:" or "2:")
				float timeout = 2.0f;
				if (currentStatus.substr(0, 2) == "0:") {
					timeout = 0.0f;  // 0 means indefinite
					currentStatus = currentStatus.substr(2);
				} else if (currentStatus.substr(0, 2) == "2:") {
					currentStatus = currentStatus.substr(2);
				}
				lastStatus = currentStatus;
				statusDisplayUntil = timeout > 0.0f ? glfwGetTime() + timeout : 0.0f;
			} else {
				// Empty string means clear the status
				lastStatus = "";
				statusDisplayUntil = 0.0f;
			}
		}
	}

	// Show status text (lastStatus already has prefix stripped)
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
		float x = preview->box.pos.x + 10.f;
		float y = preview->box.pos.y + preview->box.size.y - 24.f;
		bndIconLabelValue(args.vg, x, y, preview->box.size.y, 20.f, -1,
			bndGetTheme()->menuTheme.textColor, BND_LEFT,
			BND_LABEL_FONT_SIZE, statusText.c_str(), NULL);
	}
}


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne
