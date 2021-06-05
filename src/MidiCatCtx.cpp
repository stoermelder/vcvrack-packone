#include "plugin.hpp"
#include "MidiCat.hpp"
#include "components/LedTextField.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatCtxModule : MidiCatCtxBase {
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
		: ThemedModuleWidget<MidiCatCtxModule>(module, "MidiCatCtx", "MidiCat.md#ctx-expander") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createParamCentered<TL1105>(Vec(15.0f, 258.6f), module, MidiCatCtxModule::PARAM_MAP));

		IdTextField* textField = createWidget<IdTextField>(Vec());
		textField->textSize = 13.f;
		textField->maxTextLength = 8;
		textField->module = module;
		textField->box.size = Vec(54.f, 13.f);

		TransformWidget* tw = new TransformWidget;
		tw->addChild(textField);
		tw->box.pos = Vec(-12.f, 305.f);
		tw->box.size = Vec(120.f, 13.f);
		addChild(tw);

		math::Vec center = textField->box.getCenter();
		tw->identity();
		tw->translate(center);
		tw->rotate(-M_PI / 2);
		tw->translate(center.neg());
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatCtx = createModel<StoermelderPackOne::MidiCat::MidiCatCtxModule, StoermelderPackOne::MidiCat::MidiCatCtxWidget>("MidiCatCtx");