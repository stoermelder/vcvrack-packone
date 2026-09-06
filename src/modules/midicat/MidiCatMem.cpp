#include "../../plugin.hpp"
#include "../../utils/StripIdFixModule.hpp"
#include "MidiCat.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatMemModule : MidiCatMemBase, StripIdFixModule {
	enum ParamIds {
		PARAM_APPLY,
		PARAM_PREV,
		PARAM_NEXT,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_PREV,
		INPUT_NEXT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_APPLY,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	/** Owned by the UI thread */
	std::map<std::pair<std::string, std::string>, MemModule*> midiMap;
	/** [Stored to JSON] */
	/** Owned by the UI thread */
	std::set<int64_t> moduleRestriction;

	ClockDividerEx processDivider;
	dsp::SchmittTrigger prevTrigger;
	dsp::SchmittTrigger nextTrigger;
	dsp::SchmittTrigger applyTrigger;

	MidiCatMemModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch<BufferedSwitchQuantity>(PARAM_PREV, 0.f, 1.f, 0.f, "Scan for previous module mapping");
		paramQuantities[PARAM_PREV]->description = "Scans the connected modules for the previous MIDI-to-parameter mapping preset.";
		configSwitch<BufferedSwitchQuantity>(PARAM_NEXT, 0.f, 1.f, 0.f, "Scan for next module mapping");
		paramQuantities[PARAM_NEXT]->description = "Scans the connected modules for the next MIDI-to-parameter mapping preset.";
		configSwitch<BufferedSwitchQuantity>(PARAM_APPLY, 0.f, 1.f, 0.f, "Apply mapping");
		paramQuantities[PARAM_APPLY]->description = "Applies the currently selected mapping preset to the connected target module.";
		configInput(INPUT_PREV, "Previous preset trigger");
		inputInfos[INPUT_PREV]->description = "Triggers a scan for the previous module mapping preset.";
		configInput(INPUT_NEXT, "Next preset trigger");
		inputInfos[INPUT_NEXT]->description = "Triggers a scan for the next module mapping preset.";
		processDivider.setDivision(48);

		ResetEvent re;
		onReset(re);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		notifyModuleListeners("MidiCat");
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		resetMap();
		moduleRestriction.clear();
	}

	void resetMap() {
		for (auto it : midiMap) {
			delete it.second;
		}
		midiMap.clear();
	}

	void process(const ProcessArgs& args) override {
		leftExpander.producerMessage = &midiMap;
		leftExpander.messageFlipRequested = true;

		if (processDivider.process()) {
			if (prevTrigger.process(params[PARAM_PREV].getValue() + inputs[INPUT_PREV].getVoltage())) {
				reinterpret_cast<BufferedSwitchQuantity*>(paramQuantities[PARAM_PREV])->setBuffer();
			}
			if (nextTrigger.process(params[PARAM_NEXT].getValue() + inputs[INPUT_NEXT].getVoltage())) {
				reinterpret_cast<BufferedSwitchQuantity*>(paramQuantities[PARAM_NEXT])->setBuffer();
			}
			if (applyTrigger.process(params[PARAM_APPLY].getValue())) {
				reinterpret_cast<BufferedSwitchQuantity*>(paramQuantities[PARAM_APPLY])->setBuffer();
			}
		}
	}

	std::map<std::pair<std::string, std::string>, MemModule*>* getMemStorage() override {
		return &midiMap;
	}

	std::set<int64_t>* getMemModuleRestriction() override {
		return &moduleRestriction;
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* midiMapJ = json_array();
		for (auto it : midiMap) {
			json_t* midiMapJJ = json_object();
			json_object_set_new(midiMapJJ, "pluginSlug", json_string(it.first.first.c_str()));
			json_object_set_new(midiMapJJ, "moduleSlug", json_string(it.first.second.c_str()));

			auto a = it.second;
			json_object_set_new(midiMapJJ, "pluginName", json_string(a->pluginName.c_str()));
			json_object_set_new(midiMapJJ, "moduleName", json_string(a->moduleName.c_str()));
			json_t* paramMapJ = json_array();
			for (auto p : a->paramMap) {
				json_t* paramMapJJ = json_object();
				json_object_set_new(paramMapJJ, "paramId", json_integer(p->paramId));
				json_object_set_new(paramMapJJ, "cc", json_integer(p->cc));
				json_object_set_new(paramMapJJ, "ccMode", json_integer((int)p->ccMode));
				json_object_set_new(paramMapJJ, "cc14bit", json_boolean(p->cc14bit));
				json_object_set_new(paramMapJJ, "note", json_integer(p->note));
				json_object_set_new(paramMapJJ, "noteMode", json_integer((int)p->noteMode));
				json_object_set_new(paramMapJJ, "label", json_string(p->label.c_str()));
				json_object_set_new(paramMapJJ, "midiOptions", json_integer(p->midiOptions));
				json_object_set_new(paramMapJJ, "slew", json_real(p->slew));
				json_object_set_new(paramMapJJ, "min", json_real(p->min));
				json_object_set_new(paramMapJJ, "max", json_real(p->max));
				json_object_set_new(paramMapJJ, "curve", json_real(p->curve));
				json_object_set_new(paramMapJJ, "lightFirstId", json_integer(p->lightFirstId));
				json_object_set_new(paramMapJJ, "lightNumColors", json_integer(p->lightNumColors));
				json_array_append_new(paramMapJ, paramMapJJ);
			}
			json_object_set_new(midiMapJJ, "paramMap", paramMapJ);

			json_array_append_new(midiMapJ, midiMapJJ);
		}
		json_object_set_new(rootJ, "midiMap", midiMapJ);

		json_t* moduleRestrictionJ = json_array();
		for (auto it : moduleRestriction) {
			json_array_append_new(moduleRestrictionJ, json_integer(it));
		}
		json_object_set_new(rootJ, "moduleRestriction", moduleRestrictionJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		resetMap();
		json_t* midiMapJ = json_object_get(rootJ, "midiMap");
		if (!midiMapJ) return;
		size_t i;
		json_t* midiMapJJ;
		json_array_foreach(midiMapJ, i, midiMapJJ) {
			std::string pluginSlug = json_string_value(json_object_get(midiMapJJ, "pluginSlug"));
			std::string moduleSlug = json_string_value(json_object_get(midiMapJJ, "moduleSlug"));

			MemModule* a = new MemModule;
			a->pluginName = json_string_value(json_object_get(midiMapJJ, "pluginName"));
			a->moduleName = json_string_value(json_object_get(midiMapJJ, "moduleName"));
			json_t* paramMapJ = json_object_get(midiMapJJ, "paramMap");
			size_t j;
			json_t* paramMapJJ;
			json_array_foreach(paramMapJ, j, paramMapJJ) {
				MemParam* p = new MemParam;
				json_t* paramIdJ = json_object_get(paramMapJJ, "paramId");
				if (paramIdJ) p->paramId = json_integer_value(paramIdJ);
				json_t* ccJ = json_object_get(paramMapJJ, "cc");
				if (ccJ) p->cc = json_integer_value(ccJ);
				json_t* ccModeJ = json_object_get(paramMapJJ, "ccMode");
				if (ccModeJ) p->ccMode = (CCMODE)json_integer_value(ccModeJ);
				json_t* cc14bitJ = json_object_get(paramMapJJ, "cc14bit");
				if (cc14bitJ) p->cc14bit = json_boolean_value(cc14bitJ);
				json_t* noteJ = json_object_get(paramMapJJ, "note");
				if (noteJ) p->note = json_integer_value(noteJ);
				json_t* noteModeJ = json_object_get(paramMapJJ, "noteMode");
				if (noteModeJ) p->noteMode = (NOTEMODE)json_integer_value(noteModeJ);
				json_t* labelJ = json_object_get(paramMapJJ, "label");
				if (labelJ) p->label = json_string_value(labelJ);
				json_t* midiOptionsJ = json_object_get(paramMapJJ, "midiOptions");
				if (midiOptionsJ) p->midiOptions = json_integer_value(midiOptionsJ);
				json_t* slewJ = json_object_get(paramMapJJ, "slew");
				if (slewJ) p->slew = json_real_value(slewJ);
				json_t* minJ = json_object_get(paramMapJJ, "min");
				if (minJ) p->min = json_real_value(minJ);
				json_t* maxJ = json_object_get(paramMapJJ, "max");
				if (maxJ) p->max = json_real_value(maxJ);
				json_t* curveJ = json_object_get(paramMapJJ, "curve");
				if (curveJ) p->curve = json_real_value(curveJ);
				json_t* lightFirstIdJ = json_object_get(paramMapJJ, "lightFirstId");
				if (lightFirstIdJ) p->lightFirstId = json_integer_value(lightFirstIdJ);
				json_t* lightNumColorsJ = json_object_get(paramMapJJ, "lightNumColors");
				if (lightNumColorsJ) p->lightNumColors = json_integer_value(lightNumColorsJ);
				a->paramMap.push_back(p);
			}
			midiMap[std::pair<std::string, std::string>(pluginSlug, moduleSlug)] = a;
		}

		moduleRestriction.clear();
		json_t* moduleRestrictionJ = json_object_get(rootJ, "moduleRestriction");
		if (moduleRestrictionJ) {
			json_t* moduleRestrictionJJ;
			json_array_foreach(moduleRestrictionJ, i, moduleRestrictionJJ) {
				moduleRestriction.insert(idFix(json_integer_value(moduleRestrictionJJ)));
			}
		}

		idFixClearMap();
	}
};


struct MemDisplay : StoermelderLedDisplay {
	MidiCatMemModule* module;
	void step() override {
		StoermelderLedDisplay::step();
		if (!module) return;
		text = string::f("%i", (int)module->midiMap.size());
	}
};

struct MidiCatMemWidget : ThemedModuleWidget<MidiCatMemModule> {
	MidiCatMemWidget(MidiCatMemModule* module)
		: ThemedModuleWidget<MidiCatMemModule>(module, "MidiCatMem", "MidiCat.md#mem-expander") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createParamCentered<TL1105>(Vec(15.0f, 177.8f), module, MidiCatMemModule::PARAM_PREV));
		addInput(createInputCentered<StoermelderPort>(Vec(15.0f, 201.5f), module, MidiCatMemModule::INPUT_PREV));
		addChild(createParamCentered<TL1105>(Vec(15.0f, 235.6f), module, MidiCatMemModule::PARAM_NEXT));
		addInput(createInputCentered<StoermelderPort>(Vec(15.0f, 260.3f), module, MidiCatMemModule::INPUT_NEXT));
		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 284.4f), module, MidiCatMemModule::LIGHT_APPLY));
		addChild(createParamCentered<TL1105>(Vec(15.0f, 306.7f), module, MidiCatMemModule::PARAM_APPLY));
		MemDisplay* memDisplay = createWidgetCentered<MemDisplay>(Vec(15.0f, 336.2f));
		memDisplay->module = module;
		addChild(memDisplay);
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatMem = createModel<StoermelderPackOne::MidiCat::MidiCatMemModule, StoermelderPackOne::MidiCat::MidiCatMemWidget>("MidiCatEx");