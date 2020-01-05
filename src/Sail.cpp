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

	bool mod;
	bool locked = false;
	float value;
	float deltaBase;
	float delta = 0.f;

	dsp::ClockDivider lightDivider;

	SailModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		mode = MODE::RELATIVE;
		deltaBase = std::numeric_limits<float>::min();
	}

	void process(const ProcessArgs& args) override {
		if (locked) {
			if (deltaBase == std::numeric_limits<float>::min()) {
				deltaBase = inputs[INPUT_VALUE].getVoltage();
				delta = 0.f;
			}
			else {
				value = inputs[INPUT_VALUE].getVoltage();
				delta = value - deltaBase;
			}
			mod = inputs[INPUT_MOD].getVoltage() > 1.f;
		}
		else {
			delta = 0.f;
		}

		if (lightDivider.process()) {
			lights[LIGHT_ACTIVE].setSmoothBrightness(locked ? 1.f : 0.f, args.sampleTime * lightDivider.getDivision());
		}
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mode", json_integer(mode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mode = (MODE)json_integer_value(json_object_get(rootJ, "mode"));
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
		if (!w) { module->locked = false; return; }
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) { module->locked = false; return; }
		ParamQuantity* q = p->paramQuantity;
		if (!q) { module->locked = false; return; }
		module->locked = true;

		switch (module->mode) {
			case MODE::RELATIVE: {
				float delta = module->delta;
				module->deltaBase = std::numeric_limits<float>::min();
				if (delta != 0.f) {
					bool mod = module->mod || (APP->window->getMods() & GLFW_MOD_SHIFT);
					if (mod) delta /= 10.f;
					q->moveScaledValue(delta / 10.f);
				}
				break;
			}

			case MODE::ABSOLUTE: {
				float delta = module->delta;
				module->deltaBase = std::numeric_limits<float>::min();
				if (delta != 0.f) {
					q->setScaledValue(module->value / 10.f);
				}
				break;
			}
		}
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

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Mode", &ModeMenuItem::module, module));
	}
};

} // namespace Sail

Model* modelSail = createModel<Sail::SailModule, Sail::SailWidget>("Sail");