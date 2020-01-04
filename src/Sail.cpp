#include "plugin.hpp"


namespace Sail {

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

	bool mod;
	bool locked = false;
	float base;
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
		base = std::numeric_limits<float>::min();
	}

	void process(const ProcessArgs& args) override {
		if (locked) {
			if (base == std::numeric_limits<float>::min()) {
				base = inputs[INPUT_VALUE].getVoltage();
				delta = 0.f;
			}
			else {
				delta = inputs[INPUT_VALUE].getVoltage() - base;
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
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
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

		float delta = module->delta;
		module->base = std::numeric_limits<float>::min();
		if (delta != 0.f) {
			if (module->mod) delta /= 10.f;
			q->moveScaledValue(delta / 10.f);
		}
	}
};

} // namespace Sail

Model* modelSail = createModel<Sail::SailModule, Sail::SailWidget>("Sail");