#include "plugin.hpp"
#include "MidiCat.hpp"
#include "components/LedTextField.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatClkModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT_CLOCK, 4),
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

	MidiCatClkModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 4; i++) {
			configInput(INPUT_CLOCK + i, string::f("Clock %i", i + 1));
		}
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


struct MidiCatClkWidget : ThemedModuleWidget<MidiCatClkModule> {
	MidiCatClkWidget(MidiCatClkModule* module)
		: ThemedModuleWidget<MidiCatClkModule>(module, "MidiCatClk", "MidiCat.md#clk-expander") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 212.7f), module, MidiCatClkModule::INPUT_CLOCK + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 250.8f), module, MidiCatClkModule::INPUT_CLOCK + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 289.0f), module, MidiCatClkModule::INPUT_CLOCK + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 327.2f), module, MidiCatClkModule::INPUT_CLOCK + 3));
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatClk = createModel<StoermelderPackOne::MidiCat::MidiCatClkModule, StoermelderPackOne::MidiCat::MidiCatClkWidget>("MidiCatClk");