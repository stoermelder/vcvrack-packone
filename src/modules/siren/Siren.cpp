#include "../../plugin.hpp"
#include "../../pluginsettings.hpp"
#include "SirenDataSource.hpp"
#include "SirenFileSystem.hpp"
#include "SirenAudio.hpp"
#include "SirenBrowserPane.hpp"
#include "SirenPreviewPane.hpp"
#include <widget/ZoomWidget.hpp>

#include <osdialog.h>
#include <ghc/filesystem.hpp>

namespace StoermelderPackOne {
namespace Siren {

// ─── helper: settings file paths ─────────────────────────────────────────────

static std::string settingsDirPath() {
	return rack::asset::user("Stoermelder-P1");
}

static std::string sirenFilePath() {
	return settingsDirPath() + "/siren.json";
}

static std::string sirenCacheDirPath() {
	return settingsDirPath() + "/siren-cache";
}

// ─── global siren settings ───────────────────────────────────────────────────

struct SirenSettings {
	std::vector<std::string> rootFolders;
	int activeRootIdx = -1;
	std::string lastFile;
	float lastPlayheadPos = 0.f;

	void save() const {
		json_t* j = toJson();
		rack::system::createDirectories(settingsDirPath());
		FILE* f = fopen(sirenFilePath().c_str(), "w");
		if (f) { json_dumpf(j, f, JSON_INDENT(2) | JSON_REAL_PRECISION(9)); fclose(f); }
		json_decref(j);
	}

	void load() {
		FILE* f = fopen(sirenFilePath().c_str(), "r");
		if (!f) return;
		json_error_t err;
		json_t* j = json_loadf(f, 0, &err);
		fclose(f);
		if (!j) return;
		fromJson(j);
		json_decref(j);
	}

	json_t* toJson() const {
		json_t* j = json_object();
		json_t* rootsJ = json_array();
		for (const std::string& r : rootFolders)
			json_array_append_new(rootsJ, json_string(r.c_str()));
		json_object_set_new(j, "rootFolders", rootsJ);
		json_object_set_new(j, "activeRootIdx", json_integer(activeRootIdx));
		json_object_set_new(j, "lastFile", json_string(lastFile.c_str()));
		json_object_set_new(j, "lastPlayheadPos", json_real(lastPlayheadPos));
		return j;
	}

	void fromJson(json_t* j) {
		rootFolders.clear();
		json_t* rootsJ = json_object_get(j, "rootFolders");
		if (rootsJ && json_is_array(rootsJ)) {
			size_t i; json_t* v;
			json_array_foreach(rootsJ, i, v) {
				if (json_is_string(v)) rootFolders.push_back(json_string_value(v));
			}
		}
		json_t* v;
		v = json_object_get(j, "activeRootIdx"); if (v) activeRootIdx = (int)json_integer_value(v);
		v = json_object_get(j, "lastFile");      if (v) lastFile = json_string_value(v);
		v = json_object_get(j, "lastPlayheadPos"); if (v) lastPlayheadPos = (float)json_real_value(v);
	}
} sirenSettings;

// ─── module ───────────────────────────────────────────────────────────────────

struct SirenModule : Module {
	enum OutputIds { OUTPUT_L, OUTPUT_R, NUM_OUTPUTS };
	enum ParamIds  { NUM_PARAMS };
	enum InputIds  { NUM_INPUTS };
	enum LightIds  { NUM_LIGHTS };

	int panelTheme = -1;  // required by ThemedModuleWidget

	// Patch-local state (persisted in the Rack patch, not in siren.json)
	std::string lastFilePath;
	float lastPlayheadPos = 0.f;
	int activeRootIdx = -1;

	// Shared audio state — written by UI thread, read by audio thread
	// Pointer is safe: owned by SirenWidget, never deleted while module is alive
	SirenPreviewPane* previewPane = nullptr;

	SirenModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configOutput(OUTPUT_L, "Left audio out");
		configOutput(OUTPUT_R, "Right audio out");
	}

	void process(const ProcessArgs& args) override {
		if (!previewPane) return;

		float l = 0.f, r = 0.f;
		previewPane->fillAudio(&l, &r, 1, args.sampleRate);
		outputs[OUTPUT_L].setVoltage(l);
		outputs[OUTPUT_R].setVoltage(r);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "lastFile", json_string(lastFilePath.c_str()));
		json_object_set_new(rootJ, "lastPlayheadPos", json_real(lastPlayheadPos));
		json_object_set_new(rootJ, "activeRootIdx", json_integer(activeRootIdx));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* v;
		v = json_object_get(rootJ, "lastFile");        if (v) lastFilePath = json_string_value(v);
		v = json_object_get(rootJ, "lastPlayheadPos"); if (v) lastPlayheadPos = (float)json_real_value(v);
		v = json_object_get(rootJ, "activeRootIdx");   if (v) activeRootIdx = (int)json_integer_value(v);
	}
};

// ─── module widget ────────────────────────────────────────────────────────────

struct SirenWidget : ThemedModuleWidget<SirenModule> {
	TaskWorker taskWorker{"Siren"};
	SirenDragState dragState;

	SirenBrowserPane* browserPane = nullptr;
	SirenPreviewPane* previewPane = nullptr;

	explicit SirenWidget(SirenModule* module)
		: ThemedModuleWidget<SirenModule>(module, "Siren")
	{
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Browser pane (left) inside a ZoomWidget for reduced visual scale
		{
			const float zoom = 0.60f;
			const Vec displaySize = Vec(172.3f, 338.6f);
			const Vec logicalSize = displaySize.div(zoom);

			widget::ZoomWidget* zw = new widget::ZoomWidget;
			zw->box.pos  = Vec(20.f, 17.9f);
			zw->box.size = displaySize;
			zw->setZoom(zoom);

			browserPane = new SirenBrowserPane;
			browserPane->box.pos = Vec(0.f, 0.f);
			browserPane->dragState = &dragState;
			browserPane->worker = &taskWorker;
			browserPane->init(&taskWorker);
			browserPane->setSize(logicalSize);
			browserPane->onFileSelected = [this](const std::string& path) {
				onFileSelected(path);
			};
			browserPane->onAddRoot = [this]() {
				char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
				if (!path) return;
				std::string p(path);
				free(path);
				if (std::find(sirenSettings.rootFolders.begin(), sirenSettings.rootFolders.end(), p)
				    == sirenSettings.rootFolders.end()) {
					sirenSettings.rootFolders.push_back(p);
					sirenSettings.activeRootIdx = (int)sirenSettings.rootFolders.size() - 1;
					browserPane->setRoots(sirenSettings.rootFolders, sirenSettings.activeRootIdx);
				}
			};
			browserPane->onRemoveRoot = [this](int idx) {
				if (idx < 0 || idx >= (int)sirenSettings.rootFolders.size()) return;
				sirenSettings.rootFolders.erase(sirenSettings.rootFolders.begin() + idx);
				sirenSettings.activeRootIdx = sirenSettings.rootFolders.empty() ? -1 : 0;
				browserPane->setRoots(sirenSettings.rootFolders, sirenSettings.activeRootIdx);
			};
			browserPane->onSelectRoot = [this](int idx) {
				sirenSettings.activeRootIdx = idx;
				browserPane->setRoots(sirenSettings.rootFolders, idx);
			};
			zw->addChild(browserPane);
			addChild(zw);
		}

		// Preview pane (right)
		previewPane = new SirenPreviewPane;
		previewPane->box.pos = Vec(192.3f, 17.9f);
		previewPane->box.size = Vec(307.8f, 338.6f);
		previewPane->init(&taskWorker, &dragState);
		previewPane->cacheDir = sirenCacheDirPath();
		addChild(previewPane);

		// Wire preview pane to audio engine
		if (module) module->previewPane = previewPane;

		// Refresh browser when preview pane modifies metadata
		previewPane->onMetadataChanged = [this]() {
			browserPane->rebuildRowWidgets();
		};

		// Load global settings and restore state
		sirenSettings.load();
		if (module) {
			// Patch state takes priority over global settings if the patch was saved
			if (!module->lastFilePath.empty()) {
				sirenSettings.activeRootIdx = module->activeRootIdx;
			}
		}
		browserPane->setRoots(sirenSettings.rootFolders, sirenSettings.activeRootIdx);
		std::string restoreFile = module ? module->lastFilePath : sirenSettings.lastFile;
		float restorePos = module ? module->lastPlayheadPos : sirenSettings.lastPlayheadPos;
		if (!restoreFile.empty()) {
			DataSource* src = browserPane->activeDataSource;
			previewPane->loadFile(restoreFile, src ? src->getMetadata() : nullptr);
			previewPane->playheadPos = restorePos;
		}

		addOutput(createOutputCentered<StoermelderPort>(Vec(532.5f, 284.3f), module, SirenModule::OUTPUT_L));
		addOutput(createOutputCentered<StoermelderPort>(Vec(532.5f, 326.2f), module, SirenModule::OUTPUT_R));
	}

	~SirenWidget() override {
		// Sync preview state back into module fields (for patch save) and global settings
		if (previewPane) {
			sirenSettings.lastFile = previewPane->currentPath;
			sirenSettings.lastPlayheadPos = previewPane->playheadPos;
			if (module) {
				module->lastFilePath = previewPane->currentPath;
				module->lastPlayheadPos = previewPane->playheadPos;
				module->activeRootIdx = sirenSettings.activeRootIdx;
			}
		}
		sirenSettings.save();
		// Metadata is saved by FileSystemDataSource destructor via ~SirenBrowserPane
	}

	std::string activeRoot() const {
		int idx = sirenSettings.activeRootIdx;
		if (idx >= 0 && idx < (int)sirenSettings.rootFolders.size())
			return sirenSettings.rootFolders[idx];
		return "";
	}

	void onFileSelected(const std::string& path) {
		sirenSettings.lastFile = path;
		DataSource* src = browserPane->activeDataSource;
		previewPane->loadFile(path, src ? src->getMetadata() : nullptr);
	}

	void appendContextMenu(ui::Menu* menu) override {
		ThemedModuleWidget<SirenModule>::appendContextMenu(menu);
		menu->addChild(new ui::MenuSeparator);

		// Root folder management
		menu->addChild(createMenuLabel("Sample Roots"));
		for (int i = 0; i < (int)sirenSettings.rootFolders.size(); i++) {
			const std::string& root = sirenSettings.rootFolders[i];
			std::string label = root;
			bool active = (i == sirenSettings.activeRootIdx);
			menu->addChild(createCheckMenuItem(label, "", [=]() { return active; }, [this, i]() {
				sirenSettings.activeRootIdx = i;
				browserPane->setRoots(sirenSettings.rootFolders, i);
			}));
		}

		menu->addChild(createMenuItem("Add root folder...", "", [this]() {
			char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
			if (!path) return;
			std::string p(path);
			free(path);
			if (std::find(sirenSettings.rootFolders.begin(), sirenSettings.rootFolders.end(), p)
			    == sirenSettings.rootFolders.end()) {
				sirenSettings.rootFolders.push_back(p);
				sirenSettings.activeRootIdx = (int)sirenSettings.rootFolders.size() - 1;
				browserPane->setRoots(sirenSettings.rootFolders, sirenSettings.activeRootIdx);
			}
		}));

		if (!sirenSettings.rootFolders.empty()) {
			menu->addChild(createMenuItem("Remove active root folder", "", [this]() {
				int idx = sirenSettings.activeRootIdx;
				if (idx < 0 || idx >= (int)sirenSettings.rootFolders.size()) return;
				sirenSettings.rootFolders.erase(sirenSettings.rootFolders.begin() + idx);
				sirenSettings.activeRootIdx = sirenSettings.rootFolders.empty() ? -1 : 0;
				browserPane->setRoots(sirenSettings.rootFolders, sirenSettings.activeRootIdx);
			}));
		}
	}
};

} // namespace Siren
} // namespace StoermelderPackOne

Model* modelSiren = createModel<StoermelderPackOne::Siren::SirenModule,
                                StoermelderPackOne::Siren::SirenWidget>("Siren");
