#include "plugin.hpp"


namespace StoermelderPackOne {
namespace Affix {

enum class PARAM_MODE {
	VOLTAGE = 0,
	SEMITONE = 1,
	OCTAVE = 2
};

template < int CHANNELS >
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
		AffixModule<CHANNELS>* module;
		float v = std::numeric_limits<float>::max();

		float getValue() override {
			switch (module->paramMode) {
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
			switch (module->paramMode) {
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
			switch (module->paramMode) {
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
			switch (module->paramMode) {
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
			switch (module->paramMode) {
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
		for (int i = 0; i < CHANNELS; i++) {
			configParam<AffixParamQuantity>(PARAM_MONO + i, -5.f, 5.f, 0.f, string::f("Channel %i", i + 1));
			AffixParamQuantity* pq = dynamic_cast<AffixParamQuantity*>(paramQuantities[PARAM_MONO + i]);
			pq->module = this;
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
		assert(module);

		struct ParamModeMenuItem : MenuItem {
			MODULE* module;
			ParamModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				struct ParamModeItem : MenuItem {
					MODULE* module;
					PARAM_MODE paramMode;
					void onAction(const event::Action& e) override {
						module->setParamMode(paramMode);
					}
					void step() override {
						rightText = paramMode == module->paramMode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<ParamModeItem>(&MenuItem::text, "Volt", &ParamModeItem::module, module, &ParamModeItem::paramMode, PARAM_MODE::VOLTAGE));
				menu->addChild(construct<ParamModeItem>(&MenuItem::text, "Semitone", &ParamModeItem::module, module, &ParamModeItem::paramMode, PARAM_MODE::SEMITONE));
				menu->addChild(construct<ParamModeItem>(&MenuItem::text, "Octave", &ParamModeItem::module, module, &ParamModeItem::paramMode, PARAM_MODE::OCTAVE));
				return menu;
			}
		}; // ParamModeMenuItem

		struct ChannelNumberMenuItem : MenuItem {
			MODULE* module;
			ChannelNumberMenuItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				struct ChannelNumberItem : MenuItem {
					MODULE* module;
					int channels;
					void onAction(const event::Action& e) override {
						module->numberOfChannels = channels;
					}
					void step() override {
						rightText = channels == module->numberOfChannels ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<ChannelNumberItem>(&MenuItem::text, "Automatic", &ChannelNumberItem::module, module, &ChannelNumberItem::channels, 0));
				for (int i = 1; i <= module->getChannelNumber(); i++) {
					menu->addChild(construct<ChannelNumberItem>(&MenuItem::text, string::f("%i", i), &ChannelNumberItem::module, module, &ChannelNumberItem::channels, i));
				}
				return menu;
			}
		}; // ChannelNumberMenuItem

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ParamModeMenuItem>(&MenuItem::text, "Knob mode", &ParamModeMenuItem::module, module));
		menu->addChild(construct<ChannelNumberMenuItem>(&MenuItem::text, "Channels", &ChannelNumberMenuItem::module, module));
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