#include "plugin.hpp"
#include "MidiCat.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatMapModule : MidiCatMapBase {
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

	MidiCatMapModule() {
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


struct IdDisplay : StoermelderLedDisplay {
	MidiCatMapModule* module;
	void step() override {
		StoermelderLedDisplay::step();
		if (!module) return;
		text = module->midiCatId;
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		StoermelderLedDisplay::onButton(e);
	}

	void createContextMenu() {
		struct IdField : ui::TextField {
			MidiCatMapModule* module;
			IdField() {
				box.size.x = 80.f;
			}
			void onSelectKey(const event::SelectKey& e) override {
				if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
					module->midiCatId = text;

					ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
					overlay->requestDelete();
					e.consume(this);
				}

				if (!e.getTarget() && text.length() < 2) {
					ui::TextField::onSelectKey(e);
				}
			}
		};

		Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Identifier"));
		menu->addChild(construct<IdField>(&TextField::text, module->midiCatId, &IdField::module, module));
	}
};

struct MidiCatMapWidget : ThemedModuleWidget<MidiCatMapModule> {
	MidiCatMapWidget(MidiCatMapModule* module)
		: ThemedModuleWidget<MidiCatMapModule>(module, "MidiCatMap") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 284.4f), module, MidiCatMapModule::LIGHT_APPLY));
		addChild(createParamCentered<TL1105>(Vec(15.0f, 306.7f), module, MidiCatMapModule::PARAM_MAP));
		IdDisplay* idDisplay = createWidgetCentered<IdDisplay>(Vec(15.0f, 336.2f));
		idDisplay->module = module;
		addChild(idDisplay);
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatMap = createModel<StoermelderPackOne::MidiCat::MidiCatMapModule, StoermelderPackOne::MidiCat::MidiCatMapWidget>("MidiCatMap");