#include "../../plugin.hpp"
#include "../../utils/digital.hpp"
#include "TransitBase.hpp"

namespace StoermelderPackOne {
namespace Transit {

template <int NUM_PRESETS>
struct TransitExModule : TransitBase<NUM_PRESETS> {
	typedef TransitBase<NUM_PRESETS> BASE;

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
		BASE::panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < NUM_PRESETS; i++) {
			Module::configSwitch<TransitParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			TransitParamQuantity<NUM_PRESETS>* pq = (TransitParamQuantity<NUM_PRESETS>*)Module::paramQuantities[PARAM_PRESET + i];
			pq->module = this;
			pq->id = i;
			BASE::presetButton[i].param = &Module::params[PARAM_PRESET + i];

			BASE::slot[i].owner = this;
			BASE::slot[i].index = i;
			BASE::slot[i].indexParam = PARAM_PRESET + i;
			BASE::slot[i].indexLight = LIGHT_PRESET + i * 3;
		}

		Module::ResetEvent re;
		onReset(re);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		notifyExpanderListeners("Transit");
	}

	void onReset(const Module::ResetEvent& e) override {
		BASE::ctrlUniqueId = -1;
		for (int i = 0; i < NUM_PRESETS; i++) {
			BASE::presetSlotUsed[i] = false;
			BASE::textLabel[i] = "";
			BASE::fadeTime[i] = -1.f;
			BASE::preset[i].clear();
			BASE::slotColorSet[i] = false;
			BASE::slotColor[i] = color::WHITE;
			BASE::lights[LIGHT_PRESET + (i * 3) + 0].setBrightness(0.f);
			BASE::lights[LIGHT_PRESET + (i * 3) + 1].setBrightness(0.f);
			BASE::lights[LIGHT_PRESET + (i * 3) + 2].setBrightness(0.f);
		}
		BASE::onReset(e);
    }

	int sendSlotCmd(SLOT_CMD cmd, int i) override {
		// Retrieve module from scene as this is called from the GUI thread
		ModuleWidget* mw =  APP->scene->rack->getModule(BASE::ctrlModuleId);
		if (!mw) return -1;
		Module* m = mw->module;
		if (!m) return -1;
		TransitBase<NUM_PRESETS>* tm = dynamic_cast<TransitBase<NUM_PRESETS>*>(m);
		if (!tm) return -1;
		return tm->sendSlotCmd(cmd, i + BASE::ctrlOffset * NUM_PRESETS);
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
		BASE::disableDuplicateAction = true;

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (259.0f / (NUM_PRESETS - 1));
			TransitLedButton<NUM_PRESETS>* ledButton = createParamCentered<TransitLedButton<NUM_PRESETS>>(Vec(15.0f, 46.4f + o), module, MODULE::PARAM_PRESET + i);
			ledButton->module = module;
			ledButton->id = i;
			BASE::addParam(ledButton);
			BASE::addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(Vec(15.0f, 46.4f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitEx = createModel<StoermelderPackOne::Transit::TransitExModule<12>, StoermelderPackOne::Transit::TransitExWidget<12>>("TransitEx");