#pragma once
#include "../plugin.hpp"
#include <plugin.hpp>

namespace StoermelderPackOne {
namespace Mb {

// Usage data

struct ModelUsage {
	int usedCount = 0;
	int64_t usedTimestamp = -std::numeric_limits<int64_t>::infinity();
};

void modelUsageTouch(Model* model);
void modelUsageReset();

// Globals

json_t* moduleBrowserToJson(bool includeUsageData = true);
void moduleBrowserFromJson(json_t* rootJ);

extern std::set<Model*> favoriteModels;
extern std::set<Model*> hiddenModels;
extern std::map<Model*, ModelUsage*> modelUsage;


// Browser overlay

enum class MODE {
	V06,
	V1
};

struct BrowserOverlay : widget::OpaqueWidget {
	Widget* mbWidgetBackup;
	Widget* mbV06;
	Widget* mbV1;

	MODE* mode;

	BrowserOverlay();
	~BrowserOverlay();

	void step() override;
	void draw(const DrawArgs& args) override;
	void onButton(const event::Button& e) override;
};

} // namespace Mb
} // namespace StoermelderPackOne