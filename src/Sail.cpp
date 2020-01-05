#include "plugin.hpp"


namespace Sail {

enum MODE {
	RELATIVE = 0,
	ABSOLUTE = 1
};

struct SailModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_VALUE,
		INPUT_MOD,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_ACTIVE,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	MODE mode;
	/** [Stored to JSON] */
	bool valueFiltering;

	bool mod;
	float valueBase;
	float value[2];

	ParamQuantity* paramQuantity;

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;
	dsp::ExponentialFilter valueFilter;

	SailModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		processDivider.setDivision(32);
		lightDivider.setDivision(512);
		valueFilter.setTau(1 / 30.f);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		paramQuantity = NULL;
		mode = MODE::RELATIVE;
		valueFiltering = true;
	}

	void process(const ProcessArgs& args) override {
		if (processDivider.process()) {
			ParamQuantity* q = paramQuantity;
			if (q) {
				value[1] = value[0];
				value[0] = inputs[INPUT_VALUE].getVoltage();

				if (valueBase == std::numeric_limits<float>::min())
					valueBase = valueFilter.out = value[0];
				if (valueFiltering)
					value[0] = valueFilter.process(args.sampleTime * processDivider.getDivision(), value[0]);

				switch (mode) {
					case MODE::RELATIVE: {
						float d = value[0] - value[1];
						if (valueBase != value[0] && d != 0.f) {
							bool m = mod || inputs[INPUT_MOD].getVoltage() > 1.f;
							if (m) d /= 10.f;
							q->moveScaledValue(d / 10.f);
						}
						break;
					}
					case MODE::ABSOLUTE: {
						float d = value[0] - value[1];
						if (valueBase != value[0] && d != 0.f) {
							q->setScaledValue(value[0] / 10.f);
						}
						break;
					}
				}
			}
			else {
				valueBase = std::numeric_limits<float>::min();
			}
		}

		if (lightDivider.process()) {
			lights[LIGHT_ACTIVE].setSmoothBrightness(paramQuantity ? 1.f : 0.f, args.sampleTime * lightDivider.getDivision());
		}
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "valueFiltering", json_boolean(valueFiltering));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mode = (MODE)json_integer_value(json_object_get(rootJ, "mode"));
		valueFiltering = json_boolean_value(json_object_get(rootJ, "valueFiltering"));
	}
};


struct SailWidget : ThemedModuleWidget<SailModule> {
	SailWidget(SailModule* module)
		: ThemedModuleWidget<SailModule>(module, "Sail") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(22.5f, 244.7f), module, SailModule::LIGHT_ACTIVE));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 283.5f), module, SailModule::INPUT_MOD));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 327.7f), module, SailModule::INPUT_VALUE));
	}

	void step() override {
		ThemedModuleWidget<SailModule>::step();
		if (!module) return;

		Widget* w = APP->event->getHoveredWidget();
		if (!w) { module->paramQuantity = NULL; return; }
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) { module->paramQuantity = NULL; return; }
		ParamQuantity* q = p->paramQuantity;
		if (!q) { module->paramQuantity = NULL; return; }

		module->paramQuantity = q;
		module->mod = APP->window->getMods() & GLFW_MOD_SHIFT;
	}

	void appendContextMenu(Menu* menu) override {
		ModuleWidget::appendContextMenu(menu);

		struct ModeMenuItem : MenuItem {
			SailModule* module;
			ModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct ModeItem : MenuItem {
					SailModule* module;
					MODE mode;
					void onAction(const event::Action& e) override {
						module->mode = mode;
					}
					void step() override {
						rightText = module->mode == mode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<ModeItem>(&MenuItem::text, "Relative", &ModeItem::module, module, &ModeItem::mode, MODE::RELATIVE));
				menu->addChild(construct<ModeItem>(&MenuItem::text, "Absolute", &ModeItem::module, module, &ModeItem::mode, MODE::ABSOLUTE));
				return menu;
			}
		};

		struct ValueFilteringItem : MenuItem {
			SailModule* module;
			void onAction(const event::Action& e) override {
				module->valueFiltering ^= true;
			}
			void step() override {
				rightText = module->valueFiltering ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Mode", &ModeMenuItem::module, module));
		menu->addChild(construct<ValueFilteringItem>(&MenuItem::text, "Smoothing", &ValueFilteringItem::module, module));
	}
};

} // namespace Sail

Model* modelSail = createModel<Sail::SailModule, Sail::SailWidget>("Sail");