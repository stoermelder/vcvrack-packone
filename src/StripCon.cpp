#include "Strip.hpp"
#include "components/LedTextField.hpp"

namespace StoermelderPackOne {
namespace StripCon {

template <size_t PORTS>
struct StripConModule : Strip::StripConBase {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, PORTS),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	std::string conId;

	StripConModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	std::string getConnId() override {
		return conId;
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "conId", json_string(conId.c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		conId = json_string_value(json_object_get(rootJ, "conId"));
	}
}; // struct StripConModule


template <class MODULE>
struct IdTextField : StoermelderTextField {
	MODULE* module;
	void step() override {
		StoermelderTextField::step();
		if (!module) return;
		if (isFocused) module->conId = text;
		else text = module->conId;
	}
};

struct StripCon4Widget : ThemedModuleWidget<StripConModule<4>> {
	typedef StripConModule<4> MODULE;
	StripCon4Widget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "StripCon") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		IdTextField<MODULE>* textField = createWidget<IdTextField<MODULE>>(Vec(7.1f, 36.4f));
		textField->box.size = Vec(33.1f, 13.2);
		textField->module = module;
		addChild(textField);

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 72.2f), module, MODULE::INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 109.1f), module, MODULE::INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 145.5f), module, MODULE::INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 181.9f), module, MODULE::INPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 218.3f), module, MODULE::OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 254.7f), module, MODULE::OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 291.1f), module, MODULE::OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, MODULE::OUTPUT + 3));
	}
}; // struct StripConWidget


} // namespace StripCon
} // namespace StoermelderPackOne

Model* modelStripCon4 = createModel<StoermelderPackOne::StripCon::StripConModule<4>, StoermelderPackOne::StripCon::StripCon4Widget>("StripCon4");