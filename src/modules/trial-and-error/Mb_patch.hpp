#pragma once
#include "Mb.hpp"
#include "Mb_patch_source.hpp"
#include "Mb_patch_sourceindex.hpp"
#include "Mb_patch_source_filesystem.hpp"
#include "Mb_patch_source_patchstorage.hpp"
#include "Mb_patch_helper.hpp"
#include "../../utils/TaskWorker.hpp"
#include <tag.hpp>

namespace StoermelderPackOne {
namespace Mb {
namespace patch {

// Forward declarations
struct DescriptionTextField;
struct AsyncContainerLoadResult;
struct Browser;
struct BrowserSearchField;
struct SourceButton;
struct PreviewWidget;
struct StatusBarWidget;
struct MissingModulesWidget;


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
	PatchSource* source = nullptr;

	/** Incremented on each loadContainer() call; used to discard stale async results. */
	int loadGeneration_ = 0;

	/** Persistent worker thread shared by container loads and file-json loads. */
	TaskWorker taskWorker;

	/** Currently selected index in the file list for keyboard navigation. */
	int selectedIndex = -1;

	/** The container path of the parent directory (used when navigating up from index 0). */
	std::string parentContainerPath;

	/** The path of the container we just left (to select when navigating back). */
	std::string previousContainerPath;

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

	/** Navigate to the next item in the file list. */
	void navigateDown();
	/** Navigate to the previous item in the file list. */
	void navigateUp();
	/** Open the currently selected container or file. */
	void navigateRight();
	/** Go up one level if not at root. */
	void navigateLeft();
	/** Clear the selection. */
	void clearSelection();
	/** Update selection visual and scroll to selected item. */
	void updateSelection();
};

struct Browser : widget::OpaqueWidget {
	struct PatchChoiceButton : ui::ChoiceButton {
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
	PatchChoiceButton* tagButton;
	PatchChoiceButton* customTagButton;
	FavoriteButton* favoriteButton;
	ClearButton* clearButton;

	BrowserSidebar* sidebar;
	PreviewWidget* preview;
	/** Overlay widget showing missing modules when previewing a patch with unavailable plugins. */
	MissingModulesWidget* missingModulesWidget = nullptr;
	std::vector<PatchSource*> sources;
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
	void onSelectKey(const event::SelectKey& e) override;

	/** Get the currently active patch source, or nullptr. */
	PatchSource* getSource() const;
	/** Replace all sources and activate the given one. */
	void setSources(const std::vector<PatchSource*>& newSources, int activeIndex = 0);
	/** Add a new source and make it active. */
	void addSource(PatchSource* newSource);
	/** Remove a source by index. Falls back to the first source if the active one is removed. */
	void removeSource(int index);
	/** Clear all active tag filters (both predefined and custom). */
	void clear();

	/** Check if a file passes the current tag filters. */
	bool isFileTagFiltered(const std::string& fileId) const;
};


} // namespace patch
} // namespace Mb
} // namespace StoermelderPackOne