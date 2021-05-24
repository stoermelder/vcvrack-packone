#include "plugin.hpp"
#include "MidiCat.hpp"
#include "components/LedTextField.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatCtxModule : MidiCatMapBase {
	enum ParamIds {
		PARAM_MAP,
		NUM_PARAMS
	};
	enum InputIds {
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
	std::string midiCatId;

	MidiCatCtxModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<BufferedTriggerParamQuantity>(PARAM_MAP, 0.f, 1.f, 0.f, "Start parameter mapping");
		onReset();
	}

	void onReset() override {
		Module::onReset();
		midiCatId = "";
	}

	std::string getMidiCatId() override {
		return midiCatId;
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "midiCatId", json_string(midiCatId.c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		midiCatId = json_string_value(json_object_get(rootJ, "midiCatId"));
	}
};


struct IdTextField : StoermelderTextField {
	MidiCatCtxModule* module;
	void step() override {
		StoermelderTextField::step();
		if (!module) return;
		if (isFocused) module->midiCatId = text;
		else text = module->midiCatId;
	}
};

struct MidiCatCtxWidget : ThemedModuleWidget<MidiCatCtxModule> {
	MidiCatCtxWidget(MidiCatCtxModule* module)
		: ThemedModuleWidget<MidiCatCtxModule>(module, "MidiCatCtx") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 284.4f), module, MidiCatCtxModule::LIGHT_APPLY));
		addChild(createParamCentered<TL1105>(Vec(15.0f, 306.7f), module, MidiCatCtxModule::PARAM_MAP));

		IdTextField* textField = createWidget<IdTextField>(Vec(5.3f, 329.5f));
		textField->box.size = Vec(21.f, 13.f);
		textField->maxTextLength = 2;
		textField->module = module;
		addChild(textField);
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatCtx = createModel<StoermelderPackOne::MidiCat::MidiCatCtxModule, StoermelderPackOne::MidiCat::MidiCatCtxWidget>("MidiCatCtx");