#include "../../plugin.hpp"
#include "Mb.hpp"
#include "Mb_v1.hpp"
#include "Mb_v2.hpp"
#include "Mb_v06.hpp"
#include "Mb_autotag.hpp"
#include "Mb_autotag_widgets.hpp"
#include "Mb_patch.hpp"
#include <osdialog.h>
#include <tag.hpp>
#include <chrono>
#include <thread>

namespace StoermelderPackOne {
namespace Mb {

fuzzysearch::Database<plugin::Model*> modelDb;
bool searchDescriptions = false;
bool sortBySearchScore = true;
bool favoriteHighlight = true;

void modelDbInit() {
	modelDb = fuzzysearch::Database<plugin::Model*>();
	modelDb.setWeights({0.9f, 0.75f, 1.0f, 0.8f, 0.9f});
	modelDb.setThreshold(pluginSettings.mbSearchThreshold);
	for (plugin::Plugin* p : rack::plugin::plugins) {
		for (plugin::Model* model : p->models) {
			std::string tagStr;
			for (int tagId : model->tagIds) {
				for (const std::string& alias : rack::tag::tagAliases[tagId]) {
					tagStr += alias;
					tagStr += " ";
				}
			}
			std::vector<std::string> fields = {
				model->plugin->brand,
				model->plugin->name,
				model->name,
				searchDescriptions ? model->description : "",
				tagStr,
			};
			modelDb.addEntry(model, fields);
		}
	}
}

std::set<Model*> favoriteModels;
std::set<Model*> hiddenModels;
std::map<Model*, ModelUsage*> modelUsage;
std::map<std::string, std::set<Model*>> customTagModels;
std::map<Model*, std::set<int>> predefinedTagsAdded;
std::map<Model*, std::set<int>> predefinedTagsRemoved;

FavoriteMode favoriteMode = FavoriteMode::VCVRACK;

bool isModelFavorite(Model* model) {
	switch (favoriteMode) {
		case FavoriteMode::VCVRACK:
			return model->isFavorite();
		case FavoriteMode::MB:
			return favoriteModels.find(model) != favoriteModels.end();
		case FavoriteMode::BOTH:
			return model->isFavorite() || favoriteModels.find(model) != favoriteModels.end();
	}
}

void setModelFavorite(Model* model, bool favorite) {
	if (favoriteMode == FavoriteMode::VCVRACK || favoriteMode == FavoriteMode::BOTH) {
		model->setFavorite(favorite);
	}
	if (favoriteMode == FavoriteMode::MB || favoriteMode == FavoriteMode::BOTH) {
		if (favorite)
			favoriteModels.insert(model);
		else
			favoriteModels.erase(model);
	}
	// Remove from hidden when favoriting
	if (favorite) {
		hiddenModels.erase(model);
	}
}

// Returns the existing map key that matches tag case-insensitively, or tag itself.
static std::string customTagResolveKey(const std::string& tag) {
	std::string lower = string::lowercase(tag);
	for (auto& pair : customTagModels) {
		if (string::lowercase(pair.first) == lower)
			return pair.first;
	}
	return tag;
}

void customTagAdd(Model* model, const std::string& tag) {
	customTagModels[customTagResolveKey(tag)].insert(model);
}

void customTagRemove(Model* model, const std::string& tag) {
	auto it = customTagModels.find(customTagResolveKey(tag));
	if (it == customTagModels.end()) return;
	it->second.erase(model);
	if (it->second.empty())
		customTagModels.erase(it);
}

bool customTagHas(Model* model, const std::string& tag, bool resolveKey) {
	const std::string _tag = resolveKey ? customTagResolveKey(tag) : tag;
	auto it = customTagModels.find(_tag);
	if (it == customTagModels.end()) return false;
	return it->second.find(model) != it->second.end();
}

void customTagDelete(const std::string& tag) {
	customTagModels.erase(customTagResolveKey(tag));
}

std::set<std::string> customTagsForModel(Model* model) {
	std::set<std::string> result;
	for (auto& pair : customTagModels) {
		if (pair.second.find(model) != pair.second.end())
			result.insert(pair.first);
	}
	return result;
}


// Predefined tag modifications

void predefinedTagAdd(Model* model, int tagId) {
	predefinedTagsAdded[model].insert(tagId);
	// Remove from removed list if it was there
	predefinedTagsRemoved[model].erase(tagId);
	if (predefinedTagsRemoved[model].empty())
		predefinedTagsRemoved.erase(model);
}

void predefinedTagRemove(Model* model, int tagId) {
	predefinedTagsRemoved[model].insert(tagId);
	// Remove from added list if it was there
	predefinedTagsAdded[model].erase(tagId);
	if (predefinedTagsAdded[model].empty())
		predefinedTagsAdded.erase(model);
}

bool predefinedTagHasAdded(Model* model, int tagId) {
	auto it = predefinedTagsAdded.find(model);
	if (it == predefinedTagsAdded.end()) return false;
	return it->second.find(tagId) != it->second.end();
}

bool predefinedTagHasRemoved(Model* model, int tagId) {
	auto it = predefinedTagsRemoved.find(model);
	if (it == predefinedTagsRemoved.end()) return false;
	return it->second.find(tagId) != it->second.end();
}

void predefinedTagDelete(int tagId) {
	// Remove from all models' added lists
	for (auto& pair : predefinedTagsAdded) {
		pair.second.erase(tagId);
	}
	// Remove from all models' removed lists
	for (auto& pair : predefinedTagsRemoved) {
		pair.second.erase(tagId);
	}
}

std::set<int> getEffectiveTagIds(Model* model) {
	std::set<int> result;
	
	// Add original tags
	for (int tagId : model->tagIds) {
		if (!predefinedTagHasRemoved(model, tagId)) {
			result.insert(tagId);
		}
	}
	
	// Add tags that were added
	auto itAdded = predefinedTagsAdded.find(model);
	if (itAdded != predefinedTagsAdded.end()) {
		for (int tagId : itAdded->second) {
			result.insert(tagId);
		}
	}
	
	return result;
}

std::set<std::string> getEffectiveTagNames(Model* model) {
	std::set<std::string> result;
	std::set<int> tagIds = getEffectiveTagIds(model);
	for (int tagId : tagIds) {
		if (tagId >= 0 && tagId < (int)rack::tag::tagAliases.size()) {
			result.insert(rack::tag::tagAliases[tagId][0]);
		}
	}
	return result;
}

// Helper to find tag ID by name (case-insensitive)
static int findTagIdByName(const std::string& name) {
	std::string lower = string::lowercase(name);
	for (int i = 0; i < (int)rack::tag::tagAliases.size(); i++) {
		for (const auto& alias : rack::tag::tagAliases[i]) {
			if (string::lowercase(alias) == lower) {
				return i;
			}
		}
	}
	return -1;
}


// JSON storage

json_t* moduleBrowserToJson(bool includeUsageData) {
	json_t* rootJ = json_object();

	json_t* favoritesJ = json_array();
	for (Model* model : favoriteModels) {
		json_t* slugJ = json_object();
		json_object_set_new(slugJ, "plugin", json_string(model->plugin->slug.c_str()));
		json_object_set_new(slugJ, "model", json_string(model->slug.c_str()));
		json_array_append_new(favoritesJ, slugJ);
	}
	json_object_set_new(rootJ, "favorites", favoritesJ);
	json_object_set_new(rootJ, "favoriteMode", json_integer((int)favoriteMode));

	json_t* hiddenJ = json_array();
	for (Model* model : hiddenModels) {
		json_t* slugJ = json_object();
		json_object_set_new(slugJ, "plugin", json_string(model->plugin->slug.c_str()));
		json_object_set_new(slugJ, "model", json_string(model->slug.c_str()));
		json_array_append_new(hiddenJ, slugJ);
	}
	json_object_set_new(rootJ, "hidden", hiddenJ);

	json_t* customTagsJ = json_object();
	for (auto& pair : customTagModels) {
		json_t* modelsJ = json_array();
		for (Model* model : pair.second) {
			json_t* slugJ = json_object();
			json_object_set_new(slugJ, "plugin", json_string(model->plugin->slug.c_str()));
			json_object_set_new(slugJ, "model", json_string(model->slug.c_str()));
			json_array_append_new(modelsJ, slugJ);
		}
		json_object_set_new(customTagsJ, pair.first.c_str(), modelsJ);
	}
	json_object_set_new(rootJ, "customTags", customTagsJ);

	// Save predefined tag modifications
	// Merge added and removed tags into a single entry per model
	json_t* predefinedTagsJ = json_array();
	// First, collect all models with any modifications
	std::set<Model*> allModifiedModels;
	for (auto& pair : predefinedTagsAdded) allModifiedModels.insert(pair.first);
	for (auto& pair : predefinedTagsRemoved) allModifiedModels.insert(pair.first);
	
	for (Model* model : allModifiedModels) {
		json_t* entryJ = json_object();
		json_object_set_new(entryJ, "plugin", json_string(model->plugin->slug.c_str()));
		json_object_set_new(entryJ, "model", json_string(model->slug.c_str()));
		
		// Added tags
		json_t* addedJ = json_array();
		auto itAdded = predefinedTagsAdded.find(model);
		if (itAdded != predefinedTagsAdded.end()) {
			for (int tagId : itAdded->second) {
				if (tagId >= 0 && tagId < (int)rack::tag::tagAliases.size()) {
					json_array_append_new(addedJ, json_string(rack::tag::tagAliases[tagId][0].c_str()));
				}
			}
		}
		json_object_set_new(entryJ, "added", addedJ);
		
		// Removed tags
		json_t* removedJ = json_array();
		auto itRemoved = predefinedTagsRemoved.find(model);
		if (itRemoved != predefinedTagsRemoved.end()) {
			for (int tagId : itRemoved->second) {
				if (tagId >= 0 && tagId < (int)rack::tag::tagAliases.size()) {
					json_array_append_new(removedJ, json_string(rack::tag::tagAliases[tagId][0].c_str()));
				}
			}
		}
		json_object_set_new(entryJ, "removed", removedJ);
		
		json_array_append_new(predefinedTagsJ, entryJ);
	}
	json_object_set_new(rootJ, "predefinedTags", predefinedTagsJ);

	if (includeUsageData) {
		json_t* usageJ = json_array();
		for (auto t : modelUsage) {
			json_t* slugJ = json_object();
			json_object_set_new(slugJ, "plugin", json_string(t.first->plugin->slug.c_str()));
			json_object_set_new(slugJ, "model", json_string(t.first->slug.c_str()));
			json_object_set_new(slugJ, "usedCount", json_integer(t.second->usedCount));
			json_object_set_new(slugJ, "usedTimestamp", json_integer(t.second->usedTimestamp));
			json_array_append_new(usageJ, slugJ);
		}
		json_object_set_new(rootJ, "usage", usageJ);
	}

	return rootJ;
}

void moduleBrowserFromJson(json_t* rootJ) {
	json_t* favoritesJ = json_object_get(rootJ, "favorites");
	if (favoritesJ) {
		favoriteModels.clear();
		size_t i;
		json_t* slugJ;
		json_array_foreach(favoritesJ, i, slugJ) {
			json_t* pluginJ = json_object_get(slugJ, "plugin");
			json_t* modelJ = json_object_get(slugJ, "model");
			if (!pluginJ || !modelJ)
				continue;
			std::string pluginSlug = json_string_value(pluginJ);
			std::string modelSlug = json_string_value(modelJ);
			Model* model = plugin::getModel(pluginSlug, modelSlug);
			if (!model)
				continue;
			favoriteModels.insert(model);
		}
	}
	json_t* favoriteModeJ = json_object_get(rootJ, "favoriteMode");
	if (favoriteModeJ) {
		favoriteMode = (FavoriteMode)json_integer_value(favoriteModeJ);
	}
	else {
		favoriteMode = FavoriteMode::MB;
	}

	json_t* hiddenJ = json_object_get(rootJ, "hidden");
	if (hiddenJ) {
		hiddenModels.clear();
		size_t i;
		json_t* slugJ;
		json_array_foreach(hiddenJ, i, slugJ) {
			json_t* pluginJ = json_object_get(slugJ, "plugin");
			json_t* modelJ = json_object_get(slugJ, "model");
			if (!pluginJ || !modelJ)
				continue;
			std::string pluginSlug = json_string_value(pluginJ);
			std::string modelSlug = json_string_value(modelJ);
			Model* model = plugin::getModel(pluginSlug, modelSlug);
			if (!model)
				continue;
			hiddenModels.insert(model);
		}
	}

	json_t* customTagsJ = json_object_get(rootJ, "customTags");
	if (customTagsJ) {
		customTagModels.clear();
		const char* tagName;
		json_t* modelsJ;
		json_object_foreach(customTagsJ, tagName, modelsJ) {
			size_t i;
			json_t* slugJ;
			json_array_foreach(modelsJ, i, slugJ) {
				json_t* pluginJ = json_object_get(slugJ, "plugin");
				json_t* modelJ = json_object_get(slugJ, "model");
				if (!pluginJ || !modelJ) continue;
				std::string pluginSlug = json_string_value(pluginJ);
				std::string modelSlug = json_string_value(modelJ);
				Model* model = plugin::getModel(pluginSlug, modelSlug);
				if (!model) continue;
				customTagModels[tagName].insert(model);
			}
		}
	}

	// Load predefined tag modifications
	predefinedTagsAdded.clear();
	predefinedTagsRemoved.clear();
	
	// New format: "predefinedTags" with "added" and "removed" arrays
	json_t* predefinedTagsJ = json_object_get(rootJ, "predefinedTags");
	if (predefinedTagsJ) {
		size_t i;
		json_t* entryJ;
		json_array_foreach(predefinedTagsJ, i, entryJ) {
			json_t* pluginJ = json_object_get(entryJ, "plugin");
			json_t* modelJ = json_object_get(entryJ, "model");
			if (!pluginJ || !modelJ) continue;
			std::string pluginSlug = json_string_value(pluginJ);
			std::string modelSlug = json_string_value(modelJ);
			Model* model = plugin::getModel(pluginSlug, modelSlug);
			if (!model) continue;
			
			// Added tags
			json_t* addedJ = json_object_get(entryJ, "added");
			if (addedJ && json_is_array(addedJ)) {
				size_t j;
				json_t* tagNameJ;
				json_array_foreach(addedJ, j, tagNameJ) {
					if (json_is_string(tagNameJ)) {
						int tagId = findTagIdByName(json_string_value(tagNameJ));
						if (tagId >= 0) {
							predefinedTagsAdded[model].insert(tagId);
						}
					}
				}
			}
			
			// Removed tags
			json_t* removedJ = json_object_get(entryJ, "removed");
			if (removedJ && json_is_array(removedJ)) {
				size_t j;
				json_t* tagNameJ;
				json_array_foreach(removedJ, j, tagNameJ) {
					if (json_is_string(tagNameJ)) {
						int tagId = findTagIdByName(json_string_value(tagNameJ));
						if (tagId >= 0) {
							predefinedTagsRemoved[model].insert(tagId);
						}
					}
				}
			}
		}
	}

	json_t* usageJ = json_object_get(rootJ, "usage");
	if (usageJ) {
		for (auto t : modelUsage) {
			delete t.second;
		}
		modelUsage.clear();
		size_t i;
		json_t* slugJ;
		json_array_foreach(usageJ, i, slugJ) {
			json_t* pluginJ = json_object_get(slugJ, "plugin");
			json_t* modelJ = json_object_get(slugJ, "model");
			if (!pluginJ || !modelJ)
				continue;
			std::string pluginSlug = json_string_value(pluginJ);
			std::string modelSlug = json_string_value(modelJ);
			Model* model = plugin::getModel(pluginSlug, modelSlug);
			if (!model)
				continue;

			ModelUsage* m = new ModelUsage;
			m->usedCount = json_integer_value(json_object_get(slugJ, "usedCount"));
			m->usedTimestamp = json_integer_value(json_object_get(slugJ, "usedTimestamp"));
			modelUsage[model] = m;
		}
	}
}


// Usage data

void modelUsageTouch(Model* model) {
	ModelUsage* mu = modelUsage[model];
	if (!mu) {
		mu = new ModelUsage;
		modelUsage[model] = mu;
	}
	mu->usedCount++;
	mu->usedTimestamp = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void modelUsageReset() {
	for (auto t : modelUsage) {
		delete t.second;
	}
	modelUsage.clear();
}


// Browser overlay

BrowserOverlay::BrowserOverlay() {
	v1::modelBoxZoom = pluginSettings.mbV1zoom;
	v1::modelBoxSort = pluginSettings.mbV1sort;
	v1::hideBrands = pluginSettings.mbV1hideBrands;
	searchDescriptions = pluginSettings.mbV1searchDescriptions;
	sortBySearchScore = pluginSettings.mbSortBySearchScore;
	favoriteHighlight = pluginSettings.mbFavoriteHighlight;
	moduleBrowserFromJson(pluginSettings.mbModelsJ);
	modelDbInit();

	mbWidgetBackup = APP->scene->browser;
	mbWidgetBackup->hide();
	APP->scene->removeChild(mbWidgetBackup);

	// Clear all framebuffers of the default module browser - if Rack is shut down after adding MB
	// the default module browser is deleted after the window and the GL context has been destroyed
	// This causes Rack crashing on the Framebuffer's destruction.
	std::list<Widget*> l;
	l.push_back(mbWidgetBackup);
	while (!l.empty()) {
		Widget* w = l.front();
		l.pop_front();
		FramebufferWidget* fb = dynamic_cast<FramebufferWidget*>(w);
		if (fb) {
			fb->setDirty();
			fb->deleteFramebuffer();
		}
		for (Widget* _w : w->children) {
			l.push_back(_w);
		}
	}


	mbV06 = new v06::ModuleBrowser;
	mbV06->hide();
	addChild(mbV06);

	mbV1 = new v1::ModuleBrowser;
	mbV1->hide();
	addChild(mbV1);

	mbV2 = new v2::ModuleBrowser;
	mbV2->hide();
	addChild(mbV2);

	mbPatch = new patch::Browser;
	mbPatch->hide();
	addChild(mbPatch);

	// Configure the patch sources from saved settings
	patch::Browser* selBrowser = static_cast<patch::Browser*>(mbPatch);

	if (pluginSettings.mbDataSourcesJ) {
		std::vector<patch::PatchSource*> loadedSources;
		size_t idx;
		json_t* sourceJ;
		json_array_foreach(pluginSettings.mbDataSourcesJ, idx, sourceJ) {
			patch::PatchSource* loadedSource = patch::createSourceFromJson(sourceJ);
			if (loadedSource) {
				loadedSources.push_back(loadedSource);
			}
		}
		if (!loadedSources.empty()) {
			int activeIdx = pluginSettings.mbDataSourceFavoriteIndex >= 0 
				? pluginSettings.mbDataSourceFavoriteIndex 
				: 0;
			selBrowser->setSources(loadedSources, activeIdx);
		}
	}

	patch::PatchSource* source = selBrowser->getSource();
	if (source) {
		source->onAttach();
	}

	APP->scene->browser = this;
	APP->scene->addChild(this);
}

BrowserOverlay::~BrowserOverlay() {
	// Undo only when no other module messed with the browser
	if (APP->scene->browser == this) {
		APP->scene->browser = mbWidgetBackup;
		APP->scene->addChild(mbWidgetBackup);

		APP->scene->removeChild(this);
	}

	pluginSettings.mbV1zoom = v1::modelBoxZoom;
	pluginSettings.mbV1sort = v1::modelBoxSort;
	pluginSettings.mbV1hideBrands = v1::hideBrands;
	pluginSettings.mbV1searchDescriptions = searchDescriptions;
	pluginSettings.mbSortBySearchScore = sortBySearchScore;
	pluginSettings.mbFavoriteHighlight = favoriteHighlight;

	// Save patch sources to array
	json_decref(pluginSettings.mbDataSourcesJ);
	pluginSettings.mbDataSourcesJ = json_array();
	patch::Browser* selBrowser = static_cast<patch::Browser*>(mbPatch);
	for (patch::PatchSource* source : selBrowser->sources) {
		if (source) {
			json_array_append_new(pluginSettings.mbDataSourcesJ, source->toJson());
		}
	}

	json_decref(pluginSettings.mbModelsJ);
	pluginSettings.mbModelsJ = moduleBrowserToJson();
	
	pluginSettings.saveToJson();
}

void BrowserOverlay::step() {
	bool doActivate = false;
	// Hide active browser
	if (visibleBefore && !visible) {
		if (mbActive) mbActive->hide();
		visibleBefore = visible;
		return;
	}
	// Show a browser
	if (!visibleBefore && visible) {
		if (mbActive) mbActive->hide();
		visibleBefore = visible;
		doActivate = true;
	}
	// Show selection browser on held Ctrl key
	if (doActivate && (APP->window->getMods() & RACK_MOD_CTRL) == RACK_MOD_CTRL) {
		if (mbActive) mbActive->hide();
		mbPatch->show();
		mbActive = mbPatch;
		doActivate = false;
	}
	// Show one of the other browsers
	if (doActivate) {
		switch (*mode) {
			case MODE::V06:
				mbV06->show();
				mbActive = mbV06;
				break;
			case MODE::V1:
				mbV1->show();
				mbActive = mbV1;
				break;
			case MODE::V2:
				if (visible) mbV2->show();
				mbActive = mbV2;
				break;
		}
	}

	box = parent->box.zeroPos();
	// Only step if visible, since there are potentially thousands of descendants that 
	// don't need to be stepped.
	if (visible) OpaqueWidget::step();
}

void BrowserOverlay::draw(const DrawArgs& args) {
	nvgBeginPath(args.vg);
	nvgRect(args.vg, RECT_ARGS(parent->box.zeroPos()));
	nvgFillColor(args.vg, nvgRGBA(0x0, 0x0, 0x0, 0xB0));
	nvgFill(args.vg);
	OpaqueWidget::draw(args);
}

void BrowserOverlay::onButton(const event::Button& e) {
	OpaqueWidget::onButton(e);
	if (e.getTarget() != this)
		return;

	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		hide();
		e.consume(this);
	}
}


// Module

struct MbModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_ACTIVE,
		NUM_LIGHTS
	};

	MbModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		Module::ResetEvent re;
		onReset(re);
	}

	MODE mode = MODE::V2;

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "mode", json_integer((int)mode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		mode = (MODE)json_integer_value(json_object_get(rootJ, "mode"));
	}
};


struct MbMenuButton : ui::Button {
	ModuleWidget* mw;
	MbMenuButton() {
		text = "Browser";
	}
	void onAction(const ActionEvent& e) override {
		ui::Menu* menu = createMenu();
		menu->cornerFlags = BND_CORNER_TOP;
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));
		mw->appendContextMenu(menu);
	}
	void step() override {
		box.size.x = bndLabelWidth(APP->window->vg, -1, text.c_str()) + 1.0;
		Widget::step();
	}
	void draw(const DrawArgs& args) override {
		BNDwidgetState state = BND_DEFAULT;
		if (APP->event->hoveredWidget == this)
			state = BND_HOVER;
		if (APP->event->draggedWidget == this)
			state = BND_ACTIVE;
		bndMenuItem(args.vg, 0.0, 0.0, box.size.x, box.size.y, state, -1, text.c_str());
		Widget::draw(args);
	}
};

struct MbWidget : ModuleWidget {
	SppPreview::PatchPreviewContainer<Mb::BrowserOverlay>* sppPreviewContainer;
	BrowserOverlay* browserOverlay;
	MbMenuButton* menubarButton;
	bool active = false;

	MbWidget(MbModule* module) {
		setModule(module);
		setPanel(Svg::load(asset::plugin(pluginInstance, "res/Mb.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 330.0f), module, MbModule::LIGHT_ACTIVE));

		if (module) {
			active = registerSingleton("Mb", this);
			if (active) {
				sppPreviewContainer = new SppPreview::PatchPreviewContainer<Mb::BrowserOverlay>;
				APP->scene->rack->addChild(sppPreviewContainer);

				browserOverlay = new BrowserOverlay;
				browserOverlay->mode = &module->mode;
				browserOverlay->hide();

				// Add Browser button to menu bar after "View"
				ui::SequentialLayout* layout = APP->scene->menuBar->getFirstDescendantOfType<ui::SequentialLayout>();
				if (layout) {
					menubarButton = new MbMenuButton;
					menubarButton->mw = this;
					// Insert after "View" button by finding it in children
					auto it = layout->children.begin();
					for (; it != layout->children.end(); ++it) {
						ui::Button* btn = dynamic_cast<ui::Button*>(*it);
						if (btn && btn->text == string::translate("MenuBar.view")) {
							++it;
							break;
						}
					}
					layout->children.insert(it, menubarButton);
				}
			}
		}
	}

	~MbWidget() {
		if (module && active) {
			// Remove Browser button from menu bar
			if (menubarButton) {
				ui::SequentialLayout* layout = APP->scene->menuBar ? APP->scene->menuBar->getFirstDescendantOfType<ui::SequentialLayout>() : nullptr;
				if (layout) {
					// Check if button is still in the layout's children
					for (auto it = layout->children.begin(); it != layout->children.end(); ++it) {
						if (*it == menubarButton) {
							layout->children.erase(it);
							break;
						}
					}
				}
				delete menubarButton;
				menubarButton = nullptr;
			}
			unregisterSingleton("Mb", this);
			delete browserOverlay;

			APP->scene->rack->removeChild(sppPreviewContainer);
			delete sppPreviewContainer;
		}
	}

	void step() override {
		if (module) {
			module->lights[MbModule::LIGHT_ACTIVE].setBrightness(active);
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		MbModule* module = dynamic_cast<MbModule*>(this->module);
		bool isMenuBar = menu->children.size() == 0;

		struct ModeV1Item : MenuItem {
			MbModule* module;
			void onAction(const event::Action& e) override {
				module->mode = MODE::V1;
			}
			void step() override {
				rightText = module->mode == MODE::V1 ? "✔" : "";
				MenuItem::step();
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(Rack::createPtrSlider(&v1::modelBoxZoom, PREVIEW_MIN, PREVIEW_MAX, 0.9f, "Preview", "", 100.f, 180.0f));
				menu->addChild(createBoolPtrMenuItem("Hide brand list", "", &v1::hideBrands));
				return menu;
			}
		};

		if (!isMenuBar) {
			menu->addChild(new MenuSeparator());
		}
		menu->addChild(createCheckMenuItem("v0.6", "",
			[module]() { return module->mode == MODE::V06; },
			[module]() { module->mode = MODE::V06; }
		));
		menu->addChild(construct<ModeV1Item>(&MenuItem::text, "v1 mod", &ModeV1Item::module, module));
		menu->addChild(createCheckMenuItem("v2 mod", "",
			[module]() { return module->mode == MODE::V2; },
			[module]() { module->mode = MODE::V2; }
		));
		menu->addChild(createSubmenuItem("Patch browser", RACK_MOD_CTRL_NAME "+Right click", [=](Menu* menu) {
			patch::Browser* selBrowser = static_cast<patch::Browser*>(browserOverlay->mbPatch);
			int activeIdx = selBrowser->activeSourceIndex;

			menu->addChild(createMenuItem("Add .vcvs folder source...", "", [=]() {
				patch::PatchSource* newSrc = patch::filesystem::vcvs::createSource();
				if (newSrc) { selBrowser->addSource(newSrc); }
			}));
			menu->addChild(createMenuItem("Add .vcv folder source...", "", [=]() {
				patch::PatchSource* newSrc = patch::filesystem::vcv::createSource();
				if (newSrc) { selBrowser->addSource(newSrc); }
			}));
			menu->addChild(createMenuItem("Add PatchStorage source", "", [=]() {
				patch::PatchSource* newSrc = patch::patchstorage::initSource();
				if (newSrc) { selBrowser->addSource(newSrc); }
			}, !patch::patchstorage::canCreate()));

			// Remove current source
			menu->addChild(createMenuItem("Remove selected source", "", [=]() {
				selBrowser->removeSource(activeIdx);
			}));

			if (!selBrowser->sources.empty()) {
				menu->addChild(new MenuSeparator());
				// List all sources with activate checkbox
				for (size_t i = 0; i < selBrowser->sources.size(); i++) {
					patch::PatchSource* src = selBrowser->sources[i];
					if (!src) continue;
					std::string label = src->getSourceName();
					menu->addChild(createCheckMenuItem(label, "",
						[selBrowser, i]() { return selBrowser->activeSourceIndex == (int)i; },
						[selBrowser, i]() { selBrowser->activeSourceIndex = i; selBrowser->sidebar->source = selBrowser->getSource(); }
					));
				}
			}

			// Source-specific menu items (delegated to the source for pluggability)
			patch::PatchSource* activeSource = selBrowser->getSource();
			if (activeSource) {
				menu->addChild(new MenuSeparator());
				menu->addChild(createMenuLabel("Active source"));
				activeSource->appendSourceMenuItems(menu);
			}
		}));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("v1 & v2 settings"));
		menu->addChild(Rack::createSlider(
			[]() { return pluginSettings.mbSearchThreshold; },
			[](float v) { pluginSettings.mbSearchThreshold = v; modelDb.setThreshold(v); },
			0.5f, 1.0f, 0.5f, "Search threshold", "%", 100.f, 140.0f
		));
		menu->addChild(createCheckMenuItem("Search in descriptions", "",
			[]() { return searchDescriptions; },
			[]() { searchDescriptions ^= true; modelDbInit(); }
		));
		menu->addChild(createBoolPtrMenuItem("Sort by search score", "", &sortBySearchScore));
		menu->addChild(createSubmenuItem("Favorite mode", "", [](Menu* menu) {
			menu->addChild(createCheckMenuItem("Built-in (VCV Rack)", "",
				[&]() { return favoriteMode == FavoriteMode::VCVRACK; },
				[&]() { favoriteMode = FavoriteMode::VCVRACK; }
			));
			menu->addChild(createCheckMenuItem("Legacy (MB)", "",
				[&]() { return favoriteMode == FavoriteMode::MB; },
				[&]() { favoriteMode = FavoriteMode::MB; }
			));
			/*
			menu->addChild(createCheckMenuItem("Both", "",
				[&]() { return favoriteMode == FavoriteMode::BOTH; },
				[&]() { favoriteMode = FavoriteMode::BOTH; }
			));
			*/
		}));
		menu->addChild(createBoolPtrMenuItem("Highlight favorites", "", &favoriteHighlight));
		menu->addChild(createCheckMenuItem("Magnifier overlay", "",
			[]() { return pluginSettings.mbMagnifierEnabled; },
			[]() { pluginSettings.mbMagnifierEnabled ^= true; }
		));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Custom tags for modules"));
		menu->addChild(createMenuItem("Auto-generate custom tags", "", []() {
			auto result = std::make_shared<AutoTagResult>(customTagAuto());
			if (result->total == 0) {
				osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "No new tag assignments found.");
				return;
			}
			ui::MenuOverlay* overlay = new ui::MenuOverlay;
			overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
			AutoTagConfirmWidget* w = new AutoTagConfirmWidget(result);
			overlay->addChild(w);
			APP->scene->addChild(overlay);
		}));
		menu->addChild(createMenuItem("Auto-generate 'MetaModule' tag", "", []() {
			if (!osdialog_message(OSDIALOG_INFO, OSDIALOG_OK_CANCEL, "This will connect to https://metamodule.info and download the module list. Continue?"))
				return;

			// Create loading overlay
			ui::MenuOverlay* loadingOverlay = new ui::MenuOverlay;
			loadingOverlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
			APP->scene->addChild(loadingOverlay);

			AsyncTagResultWidget* asyncWidget = new AsyncTagResultWidget(loadingOverlay);
			APP->scene->addChild(asyncWidget);

			// Run on background thread to avoid blocking UI
			std::thread([asyncWidget]() {
				auto result = std::make_shared<AutoTagResult>(customTagMetamodule());
				asyncWidget->result = result;
			}).detach();
		}));

		struct SearchTagField : ui::TextField {
			std::string query;
			void onSelectKey(const event::SelectKey& e) override {
				if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
					query = string::trim(text);
					if (!query.empty()) {
						AutoTagResult preview = customTagSearch(query);
						if (preview.total == 0) {
							osdialog_message(OSDIALOG_INFO, OSDIALOG_OK,
								string::f("No untagged modules found for \"%s\"", query.c_str()).c_str());
						}
						else {
							auto resultWrap = std::make_shared<AutoTagResult>(preview);
							ui::MenuOverlay* overlay = new ui::MenuOverlay;
							overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
							AutoTagConfirmWidget* w = new AutoTagConfirmWidget(resultWrap);
							overlay->addChild(w);
							APP->scene->addChild(overlay);
						}
					}
					e.consume(this);
					return;
				}
				ui::TextField::onSelectKey(e);
			}
		};

		menu->addChild(createSubmenuItem("Auto-generate tag from search", "", [](Menu* menu) {
			SearchTagField* stf = new SearchTagField;
			stf->box.size.x = 200.f;
			stf->placeholder = "Search term (= tag name)...";
			menu->addChild(stf);
		}));

		auto unsortedTags = customTagsAll();
		if (!unsortedTags.empty()) {
			menu->addChild(new MenuSeparator());
			std::vector<std::string> tags(unsortedTags.begin(), unsortedTags.end());
			std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
				return string::lowercase(a) < string::lowercase(b);
			});
			if (!tags.empty()) {
				menu->addChild(createSubmenuItem("Delete custom tag", "",
					[tags](Menu* menu) {
						Rack::addGroupedMenuItems<std::string>(menu, tags, [](const std::string& tag) -> ui::MenuItem* {
							MenuItem* item = createMenuItem(tag, "", [tag]() { customTagDelete(tag); });
							return item;
						});
					}
				));
			}
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Browser settings", "",
			[&](Menu* menu) {
				menu->addChild(createMenuItem("Export", "", [&]() { this->exportSettingsDialog(); }));
				menu->addChild(createMenuItem("Import", "", [&]() { this->importSettingsDialog(); }));
				menu->addChild(new MenuSeparator());
				menu->addChild(createMenuItem("Reset usage data", "", []() { modelUsageReset(); }));
			}
		));

		if (isMenuBar) {
			menu->addChild(createMenuLabel("provided by stoermelder MB"));
		}
	}

	void exportSettings(std::string filename) {
		INFO("Saving settings %s", filename.c_str());

		json_t* rootJ = moduleBrowserToJson(false);

		DEFER({
			json_decref(rootJ);
		});

		FILE* file = fopen(filename.c_str(), "w");
		if (!file) {
			std::string message = string::f("Could not write to patch file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
		}
		DEFER({
			fclose(file);
		});

		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
	}

	void exportSettingsDialog() {
		osdialog_filters* filters = osdialog_filters_parse(":json");
		DEFER({
			osdialog_filters_free(filters);
		});

		char* path = osdialog_file(OSDIALOG_SAVE, "", "stoermelder-mb.json", filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		std::string pathStr = path;
		std::string extension = system::getExtension(system::getFilename(pathStr));
		if (extension.empty()) {
			pathStr += ".json";
		}

		exportSettings(pathStr);
	}

	void importSettings(std::string filename) {
		INFO("Loading settings %s", filename.c_str());

		FILE* file = fopen(filename.c_str(), "r");
		if (!file) {
			std::string message = string::f("Could not load file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			fclose(file);
		});

		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		if (!rootJ) {
			std::string message = string::f("File is not a valid file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		moduleBrowserFromJson(rootJ);
	}

	void importSettingsDialog() {
		osdialog_filters* filters = osdialog_filters_parse(":json");
		DEFER({
			osdialog_filters_free(filters);
		});

		char* path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		importSettings(path);
	}
};

} // namespace Mb
} // namespace StoermelderPackOne

Model* modelMb = createModel<StoermelderPackOne::Mb::MbModule, StoermelderPackOne::Mb::MbWidget>("Mb");