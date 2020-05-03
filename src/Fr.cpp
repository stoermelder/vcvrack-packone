#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Fr {

struct FrModule : Module {
	enum ParamIds {
		PARAM_TOGGLE,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_FR,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	FrModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_TOGGLE, 0.f, 1.f, 0.f, "Toggle framerate widget");
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


struct ToggleButton : TL1105 {
	FrModule* module;
	void step() override {
		if (module) {
			module->lights[FrModule::LIGHT_FR].setBrightness(APP->scene->frameRateWidget->visible);
		}
		TL1105::step();
	}
	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (APP->scene->frameRateWidget->visible) {
				APP->scene->frameRateWidget->hide();
			}
			else {
				APP->scene->frameRateWidget->show();
			}
		}
	}
};

struct FrWidget : ThemedModuleWidget<FrModule> {
	FrWidget(FrModule* module)
		: ThemedModuleWidget<FrModule>(module, "Fr") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 291.3f), module, FrModule::LIGHT_FR));
		ToggleButton* button = createParamCentered<ToggleButton>(Vec(15.0f, 306.7f), module, FrModule::PARAM_TOGGLE);
		button->module = module;
		addParam(button);
	}
};

} // namespace Fr
} // namespace StoermelderPackOne

Model* modelFr = createModel<StoermelderPackOne::Fr::FrModule, StoermelderPackOne::Fr::FrWidget>("Fr");