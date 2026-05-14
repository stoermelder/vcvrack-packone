#pragma once
#include "Mb.hpp"
#include "Mb_selection_source.hpp"
#include "Mb_selection_source_index.hpp"
#include "Mb_selection_source_filesystem.hpp"
#include "Mb_selection_source_patchstorage.hpp"
#include "Mb_selection_helper.hpp"
#include "../../utils/TaskWorker.hpp"
#include <tag.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace selection {

// Forward declarations
struct DescriptionTextField;
struct AsyncContainerLoadResult;
struct Browser;
struct BrowserSearchField;
struct SourceButton;
struct PreviewWidget;
struct StatusBarWidget;

struct BrowserSidebar : widget::Widget {
	PreviewWidget* preview;

	ui::ScrollWidget* fileScroll;
	ui::List* fileList;
	std::string currentFileId;

	// Footer widgets for description and tags
	widget::OpaqueWidget* footerContainer;
	DescriptionTextField* descriptionField;
	ui::SequentialLayout* tagsLayout;

	/** The data source used by this sidebar. */
	SelectionSource* source = nullptr;

	/** Incremented on each loadContainer() call; used to discard stale async results. */
	int loadGeneration_ = 0;

	/** Persistent worker thread shared by container loads and file-json loads. */
	TaskWorker taskWorker;

	BrowserSidebar();
	~BrowserSidebar();
	void setText(const std::string& newText);
	void step() override;
	void loadContainer();
	void loadSearchResults(const std::string& query);
	void onShow(const event::Show& e) override;
	void refreshDescriptionAndTags();
	void populateList(const AsyncContainerLoadResult* res);
	void refreshFileList();
};


struct Browser : widget::OpaqueWidget {
	struct SelectionChoiceButton : ui::ChoiceButton {
		Browser* browser;
	};

	struct FavoriteButton : ui::Button {
		Browser* browser;
		void onAction(const event::Action& e) override {
			browser->favoriteFilter ^= true;
			browser->sidebar->refreshFileList();
		}
		void step() override {
			text = browser->favoriteFilter
				? (std::string("Favorites ") + CHECKMARK(true))
				: "Favorites";
			Button::step();
		}
	};

	struct ClearButton : ui::Button {
		Browser* browser;
		void onAction(const event::Action& e) override {
			browser->clear();
		}
	};

	ui::SequentialLayout* headerLayout;
	BrowserSearchField* searchField;
	SourceButton* sourceButton;
	SelectionChoiceButton* tagButton;
	SelectionChoiceButton* customTagButton;
	FavoriteButton* favoriteButton;
	ClearButton* clearButton;

	BrowserSidebar* sidebar;
	PreviewWidget* preview;

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
	/** Current search query string. */
	std::string searchQuery;

	StatusBarWidget* statusBar;

	Browser();
	~Browser();
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