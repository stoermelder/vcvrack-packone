#include "plugin.hpp"
#include "mb/Mb.hpp"
#include "mb/Mb_v1.hpp"
#include "mb/Mb_v06.hpp"
#include <osdialog.h>
#include <chrono>

namespace StoermelderPackOne {
namespace Mb {

std::set<Model*> favoriteModels;
std::set<Model*> hiddenModels;
std::map<Model*, ModelUsage*> modelUsage;

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

	json_t* hiddenJ = json_array();
	for (Model* model : hiddenModels) {
		json_t* slugJ = json_object();
		json_object_set_new(slugJ, "plugin", json_string(model->plugin->slug.c_str()));
		json_object_set_new(slugJ, "model", json_string(model->slug.c_str()));
		json_array_append_new(hiddenJ, slugJ);
	}
	json_object_set_new(rootJ, "hidden", hiddenJ);

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
	v1::searchDescriptions = pluginSettings.mbV1searchDescriptions;
	moduleBrowserFromJson(pluginSettings.mbModelsJ);

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
	addChild(mbV06);

	mbV1 = new v1::ModuleBrowser;
	addChild(mbV1);

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
	pluginSettings.mbV1searchDescriptions = v1::searchDescriptions;
	json_decref(pluginSettings.mbModelsJ);
	pluginSettings.mbModelsJ = moduleBrowserToJson();
	
	pluginSettings.saveToJson();
}

void BrowserOverlay::step() {
	switch (*mode) {
		case MODE::V06:
			if (visible) mbV06->show(); else mbV06->hide();
			mbV1->hide();
			break;
		case MODE::V1:
			mbV06->hide();
			if (visible) mbV1->show(); else mbV1->hide();
			break;
	}

	box = parent->box.zeroPos();
	// Only step if visible, since there are potentially thousands of descendants that don't need to be stepped.
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
		onReset();
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
	BrowserOverlay* browserOverlay;
	bool active = false;

	MbWidget(MbModule* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Mb.svg")));

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 330.0f), module, MbModule::LIGHT_ACTIVE));

		if (module) {
			active = registerSingleton("Mb", this);
			if (active) {
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

		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Mb.md");
				t.detach();
			}
		};

		struct ModeV06Item : MenuItem {
			MbModule* module;
			void onAction(const event::Action& e) override {
				module->mode = MODE::V06;
			}
			void step() override {
				rightText = module->mode == MODE::V06 ? "✔" : "";
				MenuItem::step();
			}
		};

		struct ModeV1Item : MenuItem {
			MbModule* module;
			void onAction(const event::Action& e) override {
				module->mode = MODE::V1;
			}
			void step() override {
				rightText = module->mode == MODE::V1 ? "✔" : "";
				MenuItem::step();
			}

			struct HideBrandsItem : MenuItem {
				void onAction(const event::Action& e) override {
					v1::hideBrands ^= true;
				}
				void step() override {
					rightText = v1::hideBrands ? "✔" : "";
					MenuItem::step();
				}
			};

			struct SearchDescriptionsItem : MenuItem {
				void onAction(const event::Action& e) override {
					v1::searchDescriptions ^= true;
				}
				void step() override {
					rightText = v1::searchDescriptions ? "✔" : "";
					MenuItem::step();
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(new v1::ModelZoomSlider);
				menu->addChild(construct<HideBrandsItem>(&MenuItem::text, "Hide brand list"));
				menu->addChild(construct<SearchDescriptionsItem>(&MenuItem::text, "Search descriptions"));
				return menu;
			}
		};

		struct ExportItem : MenuItem {
			MbWidget* mw;
			void onAction(const event::Action& e) override {
				mw->exportSettingsDialog();
			}
		};

		struct ImportItem : MenuItem {
			MbWidget* mw;
			void onAction(const event::Action& e) override {
				mw->importSettingsDialog();
			}
		};

		struct ResetUsageDataItem : MenuItem {
			void onAction(const event::Action& e) override {
				modelUsageReset();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ModeV06Item>(&MenuItem::text, "v0.6", &ModeV06Item::module, module));
		menu->addChild(construct<ModeV1Item>(&MenuItem::text, "v1 mod", &ModeV1Item::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ExportItem>(&MenuItem::text, "Export favorites & hidden", &ExportItem::mw, this));
		menu->addChild(construct<ImportItem>(&MenuItem::text, "Import favorites & hidden", &ImportItem::mw, this));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ResetUsageDataItem>(&MenuItem::text, "Reset usage data"));
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