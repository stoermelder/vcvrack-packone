#include "plugin.hpp"
#include "digital.hpp"
#include "EightFaceMk2Base.hpp"

namespace StoermelderPackOne {
namespace EightFaceMk2 {

template <int NUM_PRESETS>
struct EightFaceMk2ExModule : EightFaceMk2Base<NUM_PRESETS> {
	typedef EightFaceMk2Base<NUM_PRESETS> BASE;

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

	EightFaceMk2ExModule() {
		BASE::panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < NUM_PRESETS; i++) {
			Module::configParam<EightFaceMk2ParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			EightFaceMk2ParamQuantity<NUM_PRESETS>* pq = (EightFaceMk2ParamQuantity<NUM_PRESETS>*)Module::paramQuantities[PARAM_PRESET + i];
			pq->module = this;
			pq->id = i;
			BASE::presetButton[i].param = &Module::params[PARAM_PRESET + i];

			BASE::slot[i].param = &Module::params[PARAM_PRESET + i];
			BASE::slot[i].lights = &Module::lights[LIGHT_PRESET + i * 3];
			BASE::slot[i].presetSlotUsed = &BASE::presetSlotUsed[i];
			BASE::slot[i].preset = &BASE::preset[i];
			BASE::slot[i].presetButton = &BASE::presetButton[i];
		}

		BASE::onReset();
	}

	~EightFaceMk2ExModule() {
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (BASE::presetSlotUsed[i]) {
				for (json_t* vJ : BASE::preset[i]) {
					json_decref(vJ);
				}
			}
		}
	}

	void onReset() override { 
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (BASE::presetSlotUsed[i]) {
				for (json_t* vJ : BASE::preset[i]) {
					json_decref(vJ);
				}
				BASE::preset[i].clear();
			}
			BASE::presetSlotUsed[i] = false;
			BASE::textLabel[i] = "";
			BASE::preset[i].clear();
			BASE::lights[LIGHT_PRESET + (i * 3) + 0].setBrightness(0.f);
			BASE::lights[LIGHT_PRESET + (i * 3) + 1].setBrightness(0.f);
			BASE::lights[LIGHT_PRESET + (i * 3) + 2].setBrightness(0.f);
		}
	}

	EightFaceMk2Slot* faceSlot(int i) override {
		return &BASE::slot[i];
	}

	int faceSlotCmd(SLOT_CMD cmd, int i) override {
		// Retrieve module from scene as this is called from the GUI thread
		ModuleWidget* mw =  APP->scene->rack->getModule(BASE::ctrlModuleId);
		if (!mw) return -1;
		Module* m = mw->module;
		if (!m) return -1;
		EightFaceMk2Base<NUM_PRESETS>* tm = dynamic_cast<EightFaceMk2Base<NUM_PRESETS>*>(m);
		if (!tm) return -1;
		return tm->faceSlotCmd(cmd, i + BASE::ctrlOffset * NUM_PRESETS);
	}

	void dataFromJson(json_t* rootJ) override {
		// Hack for preventing duplicating this module
		if (APP->engine->getModule(Module::id) != NULL && !BASE::idFixHasMap()) return;

		BASE::dataFromJson(rootJ);
	}
};

template <int NUM_PRESETS>
struct EightFaceMk2ExWidget : ThemedModuleWidget<EightFaceMk2ExModule<NUM_PRESETS>> {
	typedef EightFaceMk2ExWidget<NUM_PRESETS> WIDGET;
	typedef ThemedModuleWidget<EightFaceMk2ExModule<NUM_PRESETS>> BASE;
	typedef EightFaceMk2ExModule<NUM_PRESETS> MODULE;
	
	EightFaceMk2ExWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "EightFaceMk2Ex") {
		BASE::setModule(module);

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (164.8f / (NUM_PRESETS - 1));
			EightFaceMk2LedButton<NUM_PRESETS>* ledButton = createParamCentered<EightFaceMk2LedButton<NUM_PRESETS>>(Vec(15.f, 140.6f + o), module, MODULE::PARAM_PRESET + i);
			ledButton->module = module;
			ledButton->id = i;
			BASE::addParam(ledButton);
			BASE::addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(15.f, 140.6f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}
	}
};

} // namespace EightFaceMk2
} // namespace StoermelderPackOne

Model* modelEightFaceMk2Ex = createModel<StoermelderPackOne::EightFaceMk2::EightFaceMk2ExModule<8>, StoermelderPackOne::EightFaceMk2::EightFaceMk2ExWidget<8>>("EightFaceMk2Ex");