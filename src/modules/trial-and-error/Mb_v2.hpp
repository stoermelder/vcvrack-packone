#pragma once
#include "Mb.hpp"
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace v2 {

// MB's own persisted sort setting, decoupled from Rack core's settings::browserSort
// so it can include modes (e.g. "Newest") that Rack core doesn't know about.
enum class BrowserSort {
	UPDATED = 0,
	LAST_USED,
	MOST_USED,
	BRAND,
	NAME,
	RANDOM,
	NEWEST,
};
extern BrowserSort browserSort;

struct ModuleBrowser : widget::OpaqueWidget {
	ui::SequentialLayout* headerLayout;
	ui::TextField* searchField;
	ui::ChoiceButton* brandButton;
	ui::ChoiceButton* tagButton;
	ui::ChoiceButton* customTagButton;
	ui::ChoiceButton* widthButton;
	ui::Button* favoriteButton;
	ui::Button* clearButton;
	ui::Label* countLabel;
	ui::ChoiceButton* sortButton;
	ui::ChoiceButton* zoomButton;

	ui::ScrollWidget* modelScroll;
	widget::Widget* modelMargin;
	ui::SequentialLayout* modelContainer;

	std::string search;
	std::string brand;
	std::set<int> tagIds;
	std::set<std::string> customTagFilter;
	int widthFilterRef = 0;  // reference HP value (0 = no filter)
	int widthFilterMode = 0; // 0=off, 1=exact, 2=≤, 3=≥
	int widthSortDir = 0;    // 0=off, 1=narrow→wide, -1=wide→narrow
	bool favorite = false;
	bool hidden = false;

	std::map<plugin::Model*, float> prefilteredModelScores;
	std::map<plugin::Model*, int> modelOrders;

	plugin::Model* selectedModel = nullptr;

	ModuleBrowser();
	void step() override;
	void draw(const DrawArgs& args) override;
	void refresh(bool scrollTo = true);
	void clear();
	void updateZoom();
	void navigateSelection(int key);
	bool isModelVisible(plugin::Model* model, const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter, int widthFilterRef, int widthFilterMode);
	bool hasVisibleModel(const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter, int widthFilterRef, int widthFilterMode);
	void onShow(const event::Show& e) override;
	void onHoverScroll(const event::HoverScroll& e) override;
};

} // namespace v2
} // namespace Mb
} // namespace StoermelderPackOne
