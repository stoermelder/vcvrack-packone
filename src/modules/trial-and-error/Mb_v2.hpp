#pragma once
#include "Mb.hpp"
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace v2 {

struct ModuleBrowser : widget::OpaqueWidget {
	ui::SequentialLayout* headerLayout;
	ui::TextField* searchField;
	ui::ChoiceButton* brandButton;
	ui::ChoiceButton* tagButton;
	ui::ChoiceButton* customTagButton;
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
	bool favorite = false;
	bool hidden = false;

	std::map<plugin::Model*, float> prefilteredModelScores;
	std::map<plugin::Model*, int> modelOrders;

	ModuleBrowser();
	void step() override;
	void draw(const DrawArgs& args) override;
	void refresh();
	void clear();
	void updateZoom();
	bool isModelVisible(plugin::Model* model, const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter);
	bool hasVisibleModel(const std::string& brand, const std::set<int>& tagIds, bool favorite, bool hidden, const std::set<std::string>& customTagFilter);
	void onShow(const event::Show& e) override;
	void onHoverScroll(const event::HoverScroll& e) override;
};

} // namespace v2
} // namespace Mb
} // namespace StoermelderPackOne
