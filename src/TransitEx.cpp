#include "plugin.hpp"
#include "digital.hpp"
#include "TransitBase.hpp"

namespace StoermelderPackOne {
namespace Transit {

template <int NUM_PRESETS>
struct TransitExModule : TransitBase<NUM_PRESETS> {
	enum ParamIds {
		ENUMS(PARAM_PRESET, NUM_PRESETS),
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_PRESET, NUM_PRESETS * 3),
		NUM_LIGHTS
	};

	TransitExModule() {
		TransitBase<NUM_PRESETS>::panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < NUM_PRESETS; i++) {
			Module::configParam<TransitParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			TransitParamQuantity<NUM_PRESETS>* pq = (TransitParamQuantity<NUM_PRESETS>*)Module::paramQuantities[PARAM_PRESET + i];
			pq->module = this;
			pq->i = i;
			TransitBase<NUM_PRESETS>::presetButton[i].param = &Module::params[PARAM_PRESET + i];
		}

		TransitBase<NUM_PRESETS>::onReset();
	}

	void onReset() override { 
		for (int i = 0; i < NUM_PRESETS; i++) {
			TransitBase<NUM_PRESETS>::presetSlotUsed[i] = false;
			TransitBase<NUM_PRESETS>::presetSlot[i].clear();
		}
    }

	Param* transitParam(int i) override {
		return &Module::params[PARAM_PRESET + i];
	}

	Light* transitLight(int i) override {
		return &Module::lights[LIGHT_PRESET + i];
	}

	void dataFromJson(json_t* rootJ) override {
		// Hack for preventing duplicating this module
		if (APP->engine->getModule(Module::id) != NULL && !TransitBase<NUM_PRESETS>::idFixHasMap()) return;

		TransitBase<NUM_PRESETS>::dataFromJson(rootJ);
	}
};

template <int NUM_PRESETS>
struct TransitExWidget : ThemedModuleWidget<TransitExModule<NUM_PRESETS>> {
	typedef TransitExWidget<NUM_PRESETS> WIDGET;
	typedef ThemedModuleWidget<TransitExModule<NUM_PRESETS>> BASE;
	typedef TransitExModule<NUM_PRESETS> MODULE;
	
	TransitExWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "TransitEx") {
		BASE::setModule(module);

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (288.7f / (NUM_PRESETS - 1));
			BASE::addParam(createParamCentered<LEDButton>(Vec(15.0f, 45.4f + o), module, MODULE::PARAM_PRESET + i));
			BASE::addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(15.0f, 45.4f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitEx = createModel<StoermelderPackOne::Transit::TransitExModule<12>, StoermelderPackOne::Transit::TransitExWidget<12>>("TransitEx");