#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Spin {

struct SpinModule : Module {
	enum ParamIds {
		PARAM_ONLY,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_DEC,
		OUTPUT_INC,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	float delta = 0.f;
	dsp::PulseGenerator decPulse;
	dsp::PulseGenerator incPulse;

	SpinModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_ONLY, 0.f, 1.f, 1.f, "Only active while parameter-hovering");
		onReset();
	}

	void process(const ProcessArgs& args) override {
		if (delta < 0.f) {
			incPulse.trigger();
			delta = 0.f;
		}
		if (delta > 0.f) {
			decPulse.trigger();
			delta = 0.f;
		}

		outputs[OUTPUT_INC].setVoltage(decPulse.process(args.sampleTime) * 10.f);
		outputs[OUTPUT_DEC].setVoltage(incPulse.process(args.sampleTime) * 10.f);
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


struct SpinContainer : widget::Widget {
	SpinModule* module;

	void onHoverScroll(const event::HoverScroll& e) override {
		if (module->params[SpinModule::PARAM_ONLY].getValue() == 1.f) {
			Widget* w = APP->event->getHoveredWidget();
			if (!w) return;
			ParamWidget* p = dynamic_cast<ParamWidget*>(w);
			if (!p) return;
			ParamQuantity* q = p->paramQuantity;
			if (!q) return;
		}

		module->delta = e.scrollDelta.y;
		e.consume(this);
	}
};

struct SpinWidget : ThemedModuleWidget<SpinModule> {
	SpinContainer* mwContainer;

	SpinWidget(SpinModule* module) 
		: ThemedModuleWidget<SpinModule>(module, "Spin") {
		setModule(module);

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 254.8f), module, SpinModule::OUTPUT_INC));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 290.5f), module, SpinModule::OUTPUT_DEC));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 332.9f), module, SpinModule::PARAM_ONLY));

		if (module) {
			mwContainer = new SpinContainer;
			mwContainer->module = module;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(mwContainer);
		}
	}

	~SpinWidget() {
		if (module) {
			APP->scene->rack->removeChild(mwContainer);
			delete mwContainer;
		}
	}
};

} // namespace Spin
} // namespace StoermelderPackOne

Model* modelSpin = createModel<StoermelderPackOne::Spin::SpinModule, StoermelderPackOne::Spin::SpinWidget>("Spin");