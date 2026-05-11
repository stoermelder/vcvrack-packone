#include "Mb_selection.hpp"
#include "Mb_selection_preview.hpp"
#include "Mb_selection_source.hpp"
#include <helpers.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace selection {


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
	std::string file;
	void onAction(const event::Action& e) override {
		sidebar->currentFile = file;
		sidebar->preview->clearChildren();
		sidebar->preview->loadSelectionFile(file);
		e.unconsume();
	}
	void draw(const DrawArgs& args) override {
		if (file == sidebar->currentFile) {
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

	const std::string& currentContainer = source->getContainer();
	if (currentContainer.empty()) {
		fileList->clearChildren();
		return;
	}

	fileList->clearChildren();

	// Add ".." item to go to parent folder if we're not at root
	if (currentContainer != source->getRootContainer()) {
		std::string parentContainer = source->getParentContainer(currentContainer);
		ContainerItem* upItem = new ContainerItem;
		upItem->source = source;
		upItem->containerPath = parentContainer;
		upItem->containerName = "..";
		upItem->text = "📁 ..";
		upItem->box.size.x = fileList->box.size.x;
		fileList->addChild(upItem);
	}

	// Add containers first
	auto entries = source->getEntries(currentContainer);
	std::vector<std::string> containers;
	std::vector<std::string> files;

	for (const std::string& entry : entries) {
		if (source->isDirectory(entry)) {
			containers.push_back(entry);
		}
		else if (source->isFile(entry)) {
			if (SelectionSource::endsWith(entry, ".vcvs")) {
				files.push_back(entry);
			}
		}
	}

	// Sort containers and files alphabetically (case-insensitive)
	std::sort(containers.begin(), containers.end(), [this](const std::string& a, const std::string& b) {
		return string::lowercase(source->getFilename(a)) < string::lowercase(source->getFilename(b));
	});
	std::sort(files.begin(), files.end(), [this](const std::string& a, const std::string& b) {
		return string::lowercase(source->getFilename(a)) < string::lowercase(source->getFilename(b));
	});

	// Add container items
	for (const std::string& folder : containers) {
		ContainerItem* item = new ContainerItem;
		item->source = source;
		item->containerPath = folder;
		item->containerName = source->getFilename(folder);
		item->text = "📁 " + item->containerName;
		item->box.size.x = fileList->box.size.x;
		fileList->addChild(item);
	}

	// Add file items
	for (const std::string& file : files) {
		FileItem* item = new FileItem;
		item->sidebar = this;
		item->file = file;
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


SelectionBrowser::SelectionBrowser() {
	sidebar = new SelectionBrowserSidebar;
	addChild(sidebar);

	preview = new SelectionPreview;
	addChild(preview);
	sidebar->preview = preview;
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
	sources.push_back(newSource);
	activeSourceIndex = (int)sources.size() - 1;
	if (newSource) newSource->onAttach();
	sidebar->source = getSource();
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
}

void SelectionBrowser::step() {
	if (!visible) return;
	box = parent->box.zeroPos().grow(math::Vec(-40, -40));

	const float margin = 20.f;

	sidebar->box.pos = Vec(margin, margin);
	sidebar->box.size.x = 270.f;
	sidebar->box.size.y = box.size.y - 2 * margin;

	preview->box.pos = Vec(sidebar->box.size.x + 2 * margin, margin);
	preview->box.size.x = box.size.x - sidebar->box.size.x - 3 * margin;
	preview->box.size.y = sidebar->box.size.y;

	widget::OpaqueWidget::step();
}

void SelectionBrowser::draw(const DrawArgs& args) {
	bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, 0);
	widget::OpaqueWidget::draw(args);
}


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne
