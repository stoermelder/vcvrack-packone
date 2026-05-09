#include "../../plugin.hpp"
#include "MidiCat.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatFineModule : MidiCatFineBase {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_LOWRANGE,
		INPUT_HIGHRANGE,
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
	/** [Stored to JSON] */
	float highRange;
	float lowRange = 0.1f;
	
	MidiCatFineModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_LOWRANGE, "Lower precision range (10%) gate");
		configInput(INPUT_HIGHRANGE, "Higher precision range (1/2/5%) gate");

		ResetEvent re;
		onReset(re);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		notifyExpanderListeners("MidiCat");
	}

	void onReset(const ResetEvent& e) override {
		panelTheme = pluginSettings.panelThemeDefault;
		highRange = 0.01f;
		Module::onReset(e);
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "highRange", json_real(highRange));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		json_t* highRangeJ = json_object_get(rootJ, "highRange");
		if (highRangeJ) highRange = json_real_value(highRangeJ);
	}

	float getLowRangeVoltage() override { 
		return inputs[INPUT_LOWRANGE].getVoltage();
	}
	float getHighRangeVoltage() override { 
		return inputs[INPUT_HIGHRANGE].getVoltage();
	}

	float getHighRange() override { 
		return highRange; 
	}
	float getLowRange() override { 
		return lowRange; 
	}
};


struct MidiCatFineWidget : ThemedModuleWidget<MidiCatFineModule> {
	MidiCatFineWidget(MidiCatFineModule* module)
		: ThemedModuleWidget<MidiCatFineModule>(module, "MidiCatFine", "MidiCat.md#fine-expander") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 293.5f), module, MidiCatFineModule::INPUT_LOWRANGE));
		addInput(createInputCentered<StoermelderPort>(Vec(15.f, 331.7f), module, MidiCatFineModule::INPUT_HIGHRANGE));
	}

	void appendContextMenu(ui::Menu* menu) override {
		ThemedModuleWidget<MidiCatFineModule>::appendContextMenu(menu);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Higher precision range"));
		menu->addChild(Rack::createValuePtrMenuItem("1%", &module->highRange, 0.01f));
		menu->addChild(Rack::createValuePtrMenuItem("2%", &module->highRange, 0.02f));
		menu->addChild(Rack::createValuePtrMenuItem("5%", &module->highRange, 0.05f));
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatFine = createModel<StoermelderPackOne::MidiCat::MidiCatFineModule, StoermelderPackOne::MidiCat::MidiCatFineWidget>("MidiCatFine");