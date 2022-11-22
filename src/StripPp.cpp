#include "Strip.hpp"

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
	MODE mode = MODE::LEFTRIGHT;

	StripPpModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};



struct StripPpWidget : StripWidgetBase<StripPpModule> {
	struct StripPpContainer : widget::Widget {
		StripPpWidget* mw;
		void onHoverKey(const event::HoverKey& e) override {
			if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL)) {
				switch (e.key) {
					case GLFW_KEY_V:
						mw->groupSelectionPasteClipboard();
						e.consume(this);
						break;
					case GLFW_KEY_B:
						mw->groupSelectionLoadFileDialog();
						e.consume(this);
						break;
				}
			}
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

	void appendContextMenu(Menu* menu) override {
		StripWidgetBase<StripPpModule>::appendContextMenu(menu);
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Selection"));
		menu->addChild(createMenuItem("Paste", RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "+V", [=]() { groupSelectionPasteClipboard(); }));
		menu->addChild(createMenuItem("Import", RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "+B", [=]() { groupSelectionLoadFileDialog(); }));
	}
};


} // namespace Strip
} // namespace StoermelderPackOne

Model* modelStripPp = createModel<StoermelderPackOne::Strip::StripPpModule, StoermelderPackOne::Strip::StripPpWidget>("StripPp");