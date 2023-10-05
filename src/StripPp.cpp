#include "plugin.hpp"
#include "Strip.hpp"
#include "strippp/SelectionPreview.cpp"
#include "components/MenuLabelEx.hpp"

namespace StoermelderPackOne {
namespace Strip {

static std::list<std::string> recentFiles;

static void addRecentFile(std::string file) {
	recentFiles.remove(file);
	recentFiles.emplace_front(file);
	if (recentFiles.size() > 10) recentFiles.remove(recentFiles.back());
}


struct StripPpModule : Module {
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

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	bool showPreview = true;

	MODE mode = MODE::LEFTRIGHT;

	StripPpModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "showPreview", json_boolean(showPreview));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		showPreview = json_boolean_value(json_object_get(rootJ, "showPreview"));
	}
};



struct StripPpWidget : StripWidgetBase<StripPpModule> {
	struct StripPpContainer : Widget {
		StripPpWidget* mw;
		math::Vec dragPos;
		StoermelderPackOne::SppPreview::SelectionPreview* sp;
		std::function<void()> callback;

		StripPpContainer() {
			sp = new StoermelderPackOne::SppPreview::SelectionPreview;
			sp->hide();
			addChild(sp);
		}

		void draw(const DrawArgs& args) override {
			sp->box.pos = APP->scene->rack->getMousePos();
			Widget::draw(args);
		}

		void onHoverKey(const event::HoverKey& e) override {
			if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT | RACK_MOD_CTRL)) {
				switch (e.key) {
					case GLFW_KEY_V:
						mw->groupSelectionPasteClipboard();
						e.consume(this);
						break;
					case GLFW_KEY_B:
						mw->groupSelectionLoad();
						e.consume(this);
						break;
				}
			}
			if (e.action == GLFW_PRESS && (e.mods == RACK_MOD_MASK) == 0 && e.key == GLFW_KEY_ESCAPE) {
				sp->hide();
				e.consume(this);
			}
			Widget::onHoverKey(e);
		}

		void onButton(const ButtonEvent& e) override {
			if (e.action == GLFW_PRESS && sp->isVisible()) {
				if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
					callback();
					sp->hide();
					e.consume(this);
				}
				if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
					sp->hide();
					e.consume(this);
				}
			}
			Widget::onButton(e);
		}

		void showSelectionPreview(std::string path, std::function<void()> action) {
			callback = action;
			sp->loadSelectionFile(path);
			sp->show();
		}
	};

	StripPpContainer* stripPpContainer;
	bool active = false;

	StripPpWidget(StripPpModule* module)
		: StripWidgetBase<StripPpModule>(module, "StripPp") {
		this->module = module;
		this->disableDuplicateAction = true;
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, 0.f)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(0.f, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 330.0f), module, StripPpModule::LIGHT_ACTIVE));

		if (module) {
			active = registerSingleton("StripPp", this);
			if (active) {
				stripPpContainer = new StripPpContainer;
				stripPpContainer->mw = this;
				// This is where the magic happens: add a new widget on top-level to Rack
				APP->scene->rack->addChild(stripPpContainer);
			}
		}
	}

	~StripPpWidget() {
		if (module && active) {
			unregisterSingleton("StripPp", this);
			APP->scene->rack->removeChild(stripPpContainer);
			delete stripPpContainer;
		}
	}

	void step() override {
		if (module) {
			module->lights[StripPpModule::LIGHT_ACTIVE].setBrightness(active);
		}
		StripWidgetBase<StripPpModule>::step();
	}

	void groupSelectionLoad(std::string path = "") {
		if (module->showPreview) {
			if (path.empty()) {
				path = groupSelectionLoadFileDialog(false);
			}
			if (!path.empty()) {
				stripPpContainer->showSelectionPreview(path, [=]() {
					groupSelectionLoadFile(path);
					addRecentFile(path);
				});
			}
		}
		else {
			if (path.empty()) {
				path = groupSelectionLoadFileDialog(true);
			}
			else {
				groupSelectionLoadFile(path);
			}
			if (!path.empty()) {
				addRecentFile(path);
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		StripWidgetBase<StripPpModule>::appendContextMenu(menu);
		if (!active) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Show preview", "", &module->showPreview));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Selection"));
		menu->addChild(createMenuItem("Paste", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+V", [=]() { groupSelectionPasteClipboard(); }));
		menu->addChild(createMenuItem("Import", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+B", [=]() { groupSelectionLoad(); }));

		if (module->showPreview) {
			menu->addChild(construct<MenuLabelEx>(&MenuLabelEx::text, "Abort import", &MenuLabelEx::rightText, "Esc/right-click"));
		}

		if (recentFiles.size() > 0) {
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuLabel("Recent selections"));
			for (std::string file : recentFiles) {
				menu->addChild(createMenuItem(file, "", [=]() { groupSelectionLoad(file); }));
			}
		}
	}
};


} // namespace Strip
} // namespace StoermelderPackOne

Model* modelStripPp = createModel<StoermelderPackOne::Strip::StripPpModule, StoermelderPackOne::Strip::StripPpWidget>("StripPp");