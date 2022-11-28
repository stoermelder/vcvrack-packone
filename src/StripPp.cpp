#include "Strip.hpp"
#include "sb/SelectionPreview.cpp"

namespace StoermelderPackOne {
namespace Strip {

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
			if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL)) {
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
			if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
				sp->hide();
			}
		}

		void onButton(const ButtonEvent& e) override {
			if (sp->isVisible()) {
				callback();
				sp->hide();
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
		setModule(module);

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

	void groupSelectionLoad() {
		if (module->showPreview) {
			std::string path = groupSelectionLoadFileDialog(false);
			if (path != "") {
				stripPpContainer->showSelectionPreview(path, [=]() {
					groupSelectionLoadFile(path);
				});
			}
		}
		else {
			groupSelectionLoadFileDialog(true); 
		}
	}

	void appendContextMenu(Menu* menu) override {
		StripWidgetBase<StripPpModule>::appendContextMenu(menu);
		if (!active) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Show preview", "", &module->showPreview));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Selection"));
		menu->addChild(createMenuItem("Paste", RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "+V", [=]() { groupSelectionPasteClipboard(); }));
		menu->addChild(createMenuItem("Import", RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "+B", [=]() { groupSelectionLoad(); }));
	}
};


} // namespace Strip
} // namespace StoermelderPackOne

Model* modelStripPp = createModel<StoermelderPackOne::Strip::StripPpModule, StoermelderPackOne::Strip::StripPpWidget>("StripPp");