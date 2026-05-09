#include "Mb_selection.hpp"
#include "Mb_selection_preview.hpp"
#include <helpers.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace selection {

std::string rootFolder;
std::string currentFolder;


struct FolderItem : ui::MenuItem {
	std::string folderPath;
	std::string folderName;

	void onAction(const event::Action& e) override {
		currentFolder = folderPath;
		SelectionBrowserSidebar* sidebar = getAncestorOfType<SelectionBrowserSidebar>();
		if (sidebar) sidebar->loadFolder();
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

void SelectionBrowserSidebar::step() {
	fileScroll->box.pos.y = 0.f;
	fileScroll->box.size.y = box.size.y;
	fileList->box.size.x = fileScroll->box.size.x = box.size.x;
	widget::Widget::step();
}

void SelectionBrowserSidebar::loadFolder() {
	if (currentFolder.empty()) {
		fileList->clearChildren();
		return;
	}

	auto endsWith = [](const std::string& str, const std::string& suffix) {
		return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
	};

	fileList->clearChildren();

	// Add ".." item to go to parent folder if we're not at root
	if (currentFolder != rootFolder) {
		std::string parentFolder = system::getDirectory(currentFolder);
		FolderItem* upItem = new FolderItem;
		upItem->folderPath = parentFolder;
		upItem->folderName = "..";
		upItem->text = "📁 ..";
		upItem->box.size.x = fileList->box.size.x;
		fileList->addChild(upItem);
	}

	// Add folders first
	auto entries = system::getEntries(currentFolder);
	std::vector<std::string> folders;
	std::vector<std::string> files;

	for (const std::string& entry : entries) {
		if (system::isDirectory(entry)) {
			folders.push_back(entry);
		} 
		else if (system::isFile(entry)) {
			if (endsWith(entry, ".vcvs")) {
				files.push_back(entry);
			}
		}
	}

	// Sort folders and files alphabetically (case-insensitive)
	std::sort(folders.begin(), folders.end(), [](const std::string& a, const std::string& b) {
		return string::lowercase(system::getFilename(a)) < string::lowercase(system::getFilename(b));
	});
	std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
		return string::lowercase(system::getFilename(a)) < string::lowercase(system::getFilename(b));
	});

	// Add folder items
	for (const std::string& folder : folders) {
		FolderItem* item = new FolderItem;
		item->folderPath = folder;
		item->folderName = system::getFilename(folder);
		item->text = "📁 " + item->folderName;
		item->box.size.x = fileList->box.size.x;
		fileList->addChild(item);
	}

	// Add file items
	for (const std::string& file : files) {
		FileItem* item = new FileItem;
        item->sidebar = this;
		item->file = file;
		item->text = system::getFilename(file);
		item->box.size.x = fileList->box.size.x;
		fileList->addChild(item);
	}
}

void SelectionBrowserSidebar::onShow(const event::Show& e) {
	if (currentFolder.empty() && !rootFolder.empty())
		currentFolder = rootFolder;
	loadFolder();
	widget::Widget::onShow(e);
}


SelectionBrowser::SelectionBrowser(SppPreview::SelectionPreviewContainer* c) {
	sidebar = new SelectionBrowserSidebar;
	addChild(sidebar);

	preview = new SelectionPreview(c);
	addChild(preview);
	sidebar->preview = preview;
}

void SelectionBrowser::step() {
	if (!visible) return;
	box = parent->box.zeroPos().grow(math::Vec(-70, -70));

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
