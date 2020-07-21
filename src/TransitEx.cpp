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

	/** [Stored to JSON] */
	int panelTheme = 0;

	TransitExModule() {
		panelTheme = pluginSettings.panelThemeDefault;
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

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(TransitBase<NUM_PRESETS>::presetSlotUsed[i]));
			if (TransitBase<NUM_PRESETS>::presetSlotUsed[i]) {
				json_t* slotJ = json_array();
				for (size_t j = 0; j < TransitBase<NUM_PRESETS>::presetSlot[i].size(); j++) {
					json_t* vJ = json_real(TransitBase<NUM_PRESETS>::presetSlot[i][j]);
					json_array_append_new(slotJ, vJ);
				}
				json_object_set(presetJ, "slot", slotJ);
			}
			json_array_append_new(presetsJ, presetJ);
		}
		json_object_set_new(rootJ, "presets", presetsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		// Hack for preventing duplicating this module
		if (APP->engine->getModule(Module::id) != NULL) return;

		json_t* presetsJ = json_object_get(rootJ, "presets");
		json_t* presetJ;
		size_t presetIndex;
		json_array_foreach(presetsJ, presetIndex, presetJ) {
			TransitBase<NUM_PRESETS>::presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			TransitBase<NUM_PRESETS>::presetSlot[presetIndex].clear();
			if (TransitBase<NUM_PRESETS>::presetSlotUsed[presetIndex]) {
				json_t* slotJ = json_object_get(presetJ, "slot");
				json_t* vJ;
				size_t j;
				json_array_foreach(slotJ, j, vJ) {
					float v = json_real_value(vJ);
					TransitBase<NUM_PRESETS>::presetSlot[presetIndex].push_back(v);
				}
			}
		}
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