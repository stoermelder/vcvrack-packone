#pragma once
#include "Mb.hpp"
#include "Mb_selection_source.hpp"
#include "Mb_selection_source_index.hpp"
#include "Mb_selection_source_filesystem.hpp"
#include <tag.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace selection {

// Forward declaration from Mb_selection_preview.hpp
struct SelectionPreviewWidget;

struct SelectionBrowserSidebar : widget::Widget {
	SelectionPreviewWidget* preview;

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
	struct SelectionChoiceButton : ui::ChoiceButton {
		SelectionBrowser* browser;
	};

	struct SourceButton : ui::ChoiceButton {
		SelectionBrowser* browser;
		void onAction(const event::Action& e) override;
		void step() override;
	};

	struct SourceItem : ui::MenuItem {
		SelectionBrowser* browser;
		SelectionSource* source;
		void onAction(const event::Action& e) override;
		void step() override;
	};

	struct FavoriteButton : ui::Button {
		SelectionBrowser* browser;
		void onAction(const event::Action& e) override {
			browser->favoriteFilter ^= true;
			browser->sidebar->loadContainer();
		}
		void step() override {
			text = browser->favoriteFilter
				? (std::string("Favorites ") + CHECKMARK(true))
				: "Favorites";
			Button::step();
		}
	};

	struct ClearButton : ui::Button {
		SelectionBrowser* browser;
		void onAction(const event::Action& e) override {
			browser->clear();
		}
	};

	ui::SequentialLayout* headerLayout;
	SourceButton* sourceButton;
	SelectionChoiceButton* tagButton;
	SelectionChoiceButton* customTagButton;
	FavoriteButton* favoriteButton;
	ClearButton* clearButton;

	SelectionBrowserSidebar* sidebar;
	SelectionPreviewWidget* preview;

	/** List of all configured data sources. */
	std::vector<SelectionSource*> sources;
	/** Index of the currently active source in `sources`, or -1. */
	int activeSourceIndex = -1;

	/** Currently selected predefined tag names for filtering. */
	std::set<std::string> tagFilter;
	/** Currently selected custom tag strings for filtering. */
	std::set<std::string> customTagFilter;
	/** Whether to show only favorite files. */
	bool favoriteFilter = false;

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
	/** Clear all active tag filters (both predefined and custom). */
	void clear();

	/** Check if a file passes the current tag filters. */
	bool isFileTagFiltered(const std::string& fileId) const;
};


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne
