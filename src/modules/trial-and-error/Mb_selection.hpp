#pragma once
#include "Mb.hpp"
#include "Mb_selection_preview.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {

extern std::string rootFolder;
extern std::string currentFolder;


struct SelectionBrowserSidebar : widget::Widget {
	SelectionPreview* preview;

	ui::ScrollWidget* fileScroll;
	ui::List* fileList;
	std::string currentFile;

	SelectionBrowserSidebar();
	void step() override;
	void loadFolder();
	void onShow(const event::Show& e) override;
};

struct SelectionBrowser : widget::OpaqueWidget {
	SelectionBrowserSidebar* sidebar;
	SelectionPreview* preview;

	SelectionBrowser();
	void step() override;
	void draw(const DrawArgs& args) override;
};


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne
