#include "plugin.hpp"
#include "components/Knobs.hpp"

namespace StoermelderPackOne {
namespace Affix {

enum class PARAM_MODE {
	VOLTAGE = 0,
	SEMITONE = 1,
	OCTAVE = 2
};

template <int CHANNELS>
struct AffixModule : Module {
	enum ParamIds {
		ENUMS(PARAM_MONO, CHANNELS),
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_POLY,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_POLY,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	struct AffixParamQuantity : ParamQuantity {
		AffixModule<CHANNELS>* mymodule;
		float v = std::numeric_limits<float>::max();

		float getValue() override {
			switch (mymodule->paramMode) {
				default:
					return ParamQuantity::getValue();
				case PARAM_MODE::SEMITONE:
				case PARAM_MODE::OCTAVE: {
					if (v == std::numeric_limits<float>::max()) v = ParamQuantity::getValue();
					return v;
				}
			}
		}

		void setValue(float value) override {
			switch (mymodule->paramMode) {
				default:
				case PARAM_MODE::VOLTAGE: {
					ParamQuantity::setValue(value);
					break;
				}
				case PARAM_MODE::SEMITONE: {
					v = clamp(value, getMinValue(), getMaxValue());
					value = std::round(value * 12.f) / 12.f;
					ParamQuantity::setValue(value);
					break;
				}
				case PARAM_MODE::OCTAVE: {
					v = clamp(value, getMinValue(), getMaxValue());
					value = std::round(value), 
					ParamQuantity::setValue(value);
					break;
				}
			}
		}

		std::string getDisplayValueString() override {
			switch (mymodule->paramMode) {
				default:
				case PARAM_MODE::VOLTAGE: {
					return ParamQuantity::getDisplayValueString();
				}
				case PARAM_MODE::SEMITONE: {
					float value = ParamQuantity::getValue();
					int cent = (int)(value * 12.f);
					int octaves = cent / 12;
					cent = cent % 12;
					return string::f("%i, %i", octaves, cent);
				}
				case PARAM_MODE::OCTAVE: {
					int octaves = (int)ParamQuantity::getValue();
					return string::f("%i", octaves);
				}
			}
		}

		void setDisplayValueString(std::string s) override {
			switch (mymodule->paramMode) {
				default:
				case PARAM_MODE::VOLTAGE: {
					ParamQuantity::setDisplayValueString(s);
					break;
				}
				case PARAM_MODE::SEMITONE: {
					int octave = 0;
					int cent = 0;
					int n = std::sscanf(s.c_str(), "%i,%i", &octave, &cent);
					if (n == 2) {
						ParamQuantity::setDisplayValue(octave + cent * 1.f / 12.f);
					}
					break;
				}
				case PARAM_MODE::OCTAVE: {
					int octave = 0;
					int n = std::sscanf(s.c_str(), "%i", &octave);
					if (n == 1) {
						ParamQuantity::setDisplayValue(octave);
					}
					break;
				}
			}
		}

		std::string getString() override {
			switch (mymodule->paramMode) {
				default:
				case PARAM_MODE::VOLTAGE: {
					return string::f("%s: %sV", ParamQuantity::getLabel().c_str(), ParamQuantity::getDisplayValueString().c_str());
				}
				case PARAM_MODE::SEMITONE: {
					float value = ParamQuantity::getValue();
					int cent = (int)(value * 12.f);
					int octaves = cent / 12;
					cent = cent % 12;
					return string::f("%s: %i oct %i semi", ParamQuantity::getLabel().c_str(), octaves, cent);
				}
				case PARAM_MODE::OCTAVE: {
					int octaves = (int)ParamQuantity::getValue();
					return string::f("%s: %i oct", ParamQuantity::getLabel().c_str(), octaves);
				}
			}
		}
	}; // AffixParamQuantity


	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	PARAM_MODE paramMode;
	/** [Stored to JSON] */
	int numberOfChannels;

	AffixModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_POLY, "Polyphonic");
		inputInfos[INPUT_POLY]->description = "(optional)";
		configOutput(OUTPUT_POLY, "Polyphonic");
		for (int i = 0; i < CHANNELS; i++) {
			AffixParamQuantity* pq = configParam<AffixParamQuantity>(PARAM_MONO + i, -5.f, 5.f, 0.f, string::f("Channel %i", i + 1));
			pq->mymodule = this;
		}
		onReset();
	}

	void onReset() override {
		paramMode = PARAM_MODE::VOLTAGE;
		numberOfChannels = 0;
		Module::onReset();
	}

	void process(const ProcessArgs& args) override {
		int lastChannel = numberOfChannels == 0 ? inputs[INPUT_POLY].getChannels() : numberOfChannels;
		for (int c = 0; c < lastChannel; c++) {
			float v = inputs[INPUT_POLY].getVoltage(c);
			if (c < CHANNELS) {
				v += params[PARAM_MONO + c].getValue();
			}
			outputs[OUTPUT_POLY].setVoltage(v, c);
		}
		outputs[OUTPUT_POLY].setChannels(lastChannel);
	}

	void setParamMode(PARAM_MODE paramMode) {
		if (this->paramMode == paramMode) return;
		this->paramMode = paramMode;
		if (this->paramMode == PARAM_MODE::SEMITONE || this->paramMode == PARAM_MODE::OCTAVE) {
			// Snap value
			for (int i = 0; i < CHANNELS; i++) {
				paramQuantities[PARAM_MONO + i]->setValue(params[PARAM_MONO + i].getValue());
			}
		}
	}

	int getChannelNumber() {
		return CHANNELS;
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "paramMode", json_integer((int)paramMode));
		json_object_set_new(rootJ, "numberOfChannels", json_integer(numberOfChannels));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		paramMode = (PARAM_MODE)json_integer_value(json_object_get(rootJ, "paramMode"));
		numberOfChannels = json_integer_value(json_object_get(rootJ, "numberOfChannels"));
	}
};


template < typename MODULE >
struct TAffixWidget : ThemedModuleWidget<MODULE> {
	TAffixWidget(MODULE* module, std::string baseName)
	: ThemedModuleWidget<MODULE>(module, baseName) {}

	struct StoermelderTrimpotAffix : StoermelderTrimpot {
		void step() override {
			StoermelderTrimpot::step();
			this->smooth = false;
		}
	};

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);

		menu->addChild(new MenuSeparator());
		menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<PARAM_MODE>("Knob mode",
			{
				{ PARAM_MODE::VOLTAGE, "Volt" },
				{ PARAM_MODE::SEMITONE, "Semitone" },
				{ PARAM_MODE::OCTAVE, "Octave" }
			},
			[=]() {
				return module->paramMode;
			},
			[=](PARAM_MODE paramMode) {
				module->setParamMode(paramMode);
			}
		));
		menu->addChild(createSubmenuItem("Channels", "",
			[=](Menu* menu) {
				for (int c = 0; c <= module->getChannelNumber(); c++) {
					menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(c == 0 ? "Automatic" : string::f("%d", c), &module->numberOfChannels, c));
				}
			}
		));
	}
};


struct AffixWidget : TAffixWidget<AffixModule<16>> {
	typedef AffixModule<16> MODULE;
	AffixWidget(MODULE* module)
		: TAffixWidget<AffixModule<16>>(module, "Affix") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(37.5f, 60.5f), module, MODULE::INPUT_POLY));

		for (int i = 0; i < 8; i++) {
			float o = i * 27.4f;
			addParam(createParamCentered<StoermelderTrimpotAffix>(Vec(23.0f, 98.2f + o), module, MODULE::PARAM_MONO + i));
			addParam(createParamCentered<StoermelderTrimpotAffix>(Vec(52.0f, 98.2f + o), module, MODULE::PARAM_MONO + i + 8));
		}

		addOutput(createOutputCentered<StoermelderPort>(Vec(37.5f, 327.2f), module, MODULE::OUTPUT_POLY));
	}
};


struct AffixMicroWidget : TAffixWidget<AffixModule<8>> {
	typedef AffixModule<8> MODULE;
	AffixMicroWidget(MODULE* module)
		: TAffixWidget<AffixModule<8>>(module, "AffixMicro") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 60.5f), module, MODULE::INPUT_POLY));

		for (int i = 0; i < 8; i++) {
			float o = i * 27.4f;
			addParam(createParamCentered<StoermelderTrimpotAffix>(Vec(22.5f, 98.2f + o), module, MODULE::PARAM_MONO + i));
		}

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.2f), module, MODULE::OUTPUT_POLY));
	}
};

} // namespace Affix
} // namespace StoermelderPackOne

Model* modelAffix = createModel<StoermelderPackOne::Affix::AffixModule<16>, StoermelderPackOne::Affix::AffixWidget>("Affix");
Model* modelAffixMicro = createModel<StoermelderPackOne::Affix::AffixModule<8>, StoermelderPackOne::Affix::AffixMicroWidget>("AffixMicro");