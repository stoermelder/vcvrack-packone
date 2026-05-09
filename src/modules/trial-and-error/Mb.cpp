#include "../../plugin.hpp"
#include "Mb.hpp"
#include "Mb_v1.hpp"
#include "Mb_v2.hpp"
#include "Mb_v06.hpp"
#include "Mb_autotag.hpp"
#include "Mb_autotag_widgets.hpp"
#include "Mb_selection.hpp"
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
	modelDb.setThreshold(0.5f);
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

std::set<std::string> customTagsAll() {
	std::set<std::string> result;
	for (auto& pair : customTagModels)
		result.insert(pair.first);
	return result;
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
	selection::rootFolder = selection::currentFolder = pluginSettings.mbSelectionRoot;
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

	mbSelection = new selection::SelectionBrowser;
	mbSelection->hide();
	addChild(mbSelection);

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
	pluginSettings.mbSelectionRoot = selection::rootFolder;
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
		mbSelection->show();
		mbActive = mbSelection;
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

	MODE mode = MODE::V1;

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "mode", json_integer((int)mode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		mode = (MODE)json_integer_value(json_object_get(rootJ, "mode"));
	}
};


struct MbWidget : ModuleWidget {
	SppPreview::SelectionPreviewContainer<Mb::BrowserOverlay>* sppPreviewContainer;
	BrowserOverlay* browserOverlay;
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
				sppPreviewContainer = new SppPreview::SelectionPreviewContainer<Mb::BrowserOverlay>;
				APP->scene->rack->addChild(sppPreviewContainer);

				browserOverlay = new BrowserOverlay;
				browserOverlay->mode = &module->mode;
				browserOverlay->hide();
			}
		}
	}

	~MbWidget() {
		if (module && active) {
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

		menu->addChild(new MenuSeparator());
		menu->addChild(createCheckMenuItem("v0.6", "",
			[module]() { return module->mode == MODE::V06; },
			[module]() { module->mode = MODE::V06; }
		));
		menu->addChild(construct<ModeV1Item>(&MenuItem::text, "v1 mod", &ModeV1Item::module, module));
		menu->addChild(createCheckMenuItem("v2 mod", "",
			[module]() { return module->mode == MODE::V2; },
			[module]() { module->mode = MODE::V2; }
		));
		menu->addChild(createSubmenuItem("Selection browser", RACK_MOD_CTRL_NAME "+Right click", [](Menu* menu) {
			menu->addChild(createMenuLabel(selection::rootFolder.empty() ? "(no folder selected)" : selection::rootFolder));
			menu->addChild(createMenuItem("Select root folder...", "", []() {
				std::string dir = asset::user("selections");
				char* path = osdialog_file(OSDIALOG_OPEN_DIR, dir.c_str(), NULL, NULL);
				if (!path) return;
				selection::rootFolder = path;
				selection::currentFolder = path;
				free(path);
			}));
			if (!selection::rootFolder.empty()) {
				menu->addChild(createMenuItem("Open in file explorer", "", []() {
					system::openDirectory(selection::rootFolder);
				}));
			}
		}));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("v1 & v2 settings"));
		menu->addChild(createCheckMenuItem("Search descriptions", "",
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

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Custom tags"));
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
			AutoTagResult result = customTagMetamodule();
			if (result.total == 0) {
				osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "No new tag assignments found.");
				return;
			}
			auto resultWrap = std::make_shared<AutoTagResult>(result);
			ui::MenuOverlay* overlay = new ui::MenuOverlay;
			overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
			AutoTagConfirmWidget* w = new AutoTagConfirmWidget(resultWrap);
			overlay->addChild(w);
			APP->scene->addChild(overlay);
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
		std::vector<std::string> tags(unsortedTags.begin(), unsortedTags.end());
		std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
			return string::lowercase(a) < string::lowercase(b);
		});
		if (!tags.empty()) {
			menu->addChild(createSubmenuItem("Delete custom tag", "",
				[tags](Menu* menu) {
					for (const std::string& tag : tags) {
						menu->addChild(createMenuItem(tag, "", [tag]() { customTagDelete(tag); }));
					}
				}
			));
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Menu settings", "",
			[&](Menu* menu) {
				menu->addChild(createMenuItem("Export", "", [&]() { this->exportSettingsDialog(); }));
				menu->addChild(createMenuItem("Import", "", [&]() { this->importSettingsDialog(); }));
				menu->addChild(new MenuSeparator());
				menu->addChild(createMenuItem("Reset usage data", "", []() { modelUsageReset(); }));
			}
		));
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