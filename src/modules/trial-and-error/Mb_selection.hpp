#pragma once
#include "Mb.hpp"
#include "Mb_selection_preview.hpp"
#include "Mb_selection_source.hpp"
#include "Mb_selection_source_filesystem.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {


struct SelectionBrowserSidebar : widget::Widget {
	SelectionPreview* preview;

	ui::ScrollWidget* fileScroll;
	ui::List* fileList;
	std::string currentFile;

	/** The data source used by this sidebar. */
	SelectionSource* source = nullptr;

	SelectionBrowserSidebar();
	~SelectionBrowserSidebar();
	void step() override;
	void loadContainer();
	void onShow(const event::Show& e) override;
};

struct SelectionBrowser : widget::OpaqueWidget {
	SelectionBrowserSidebar* sidebar;
	SelectionPreview* preview;

	/** List of all configured data sources. */
	std::vector<SelectionSource*> sources;
	/** Index of the currently active source in `sources`, or -1. */
	int activeSourceIndex = -1;

	SelectionBrowser();
	~SelectionBrowser();
	void step() override;
	void draw(const DrawArgs& args) override;

	/** Get the currently active selection source, or nullptr. */
	SelectionSource* getSource() const;
	/** Replace all sources and activate the given one. */
	void setSources(const std::vector<SelectionSource*>& newSources, int activeIndex = 0);
	/** Add a new source and make it active. */
	void addSource(SelectionSource* newSource);
	/** Remove a source by index. Falls back to the first source if the active one is removed. */
	void removeSource(int index);
};


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne
