#include "plugin.hpp"
#include "MidiCat.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatFineModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_01,
		INPUT_001,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	MidiCatFineModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_01, "Fine 10% range gate");
		configInput(INPUT_001, "Fine 1% range gate");
		onReset();
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};


struct MidiCatFineWidget : ThemedModuleWidget<MidiCatFineModule> {
	MidiCatFineWidget(MidiCatFineModule* module)
		: ThemedModuleWidget<MidiCatFineModule>(module, "MidiCatFine", "MidiCat.md#fine-expander") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 293.5f), module, MidiCatFineModule::INPUT_01));
		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 331.7f), module, MidiCatFineModule::INPUT_001));
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatFine = createModel<StoermelderPackOne::MidiCat::MidiCatFineModule, StoermelderPackOne::MidiCat::MidiCatFineWidget>("MidiCatFine");