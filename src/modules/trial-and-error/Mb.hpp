#pragma once
#include "../../plugin.hpp"
#include <plugin.hpp>
#include <FuzzySearchDatabase.hpp>

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
extern std::map<std::string, std::set<Model*>> customTagModels;

void customTagAdd(Model* model, const std::string& tag);
void customTagRemove(Model* model, const std::string& tag);
bool customTagHas(Model* model, const std::string& tag);
std::set<std::string> customTagsForModel(Model* model);
std::set<std::string> customTagsAll();

struct AutoTagResult {
	int total = 0;
	std::map<std::string, int> perTag; // tag name → new assignment count
};

AutoTagResult autoTagApply(bool dryRun = false);

// Shared fuzzy search database — initialized once by BrowserOverlay, re-initialized when searchDescriptions changes
extern fuzzysearch::Database<plugin::Model*> modelDb;
extern bool searchDescriptions;
extern bool sortBySearchScore;
extern bool favoriteHighlight;
void modelDbInit();


// Browser overlay

enum class MODE {
	V06,
	V1,
	V2
};

struct BrowserOverlay : widget::OpaqueWidget {
	Widget* mbWidgetBackup;
	Widget* mbV06;
	Widget* mbV1;
	Widget* mbV2;

	MODE* mode;

	BrowserOverlay();
	~BrowserOverlay();

	void step() override;
	void draw(const DrawArgs& args) override;
	void onButton(const event::Button& e) override;
};

} // namespace Mb
} // namespace StoermelderPackOne