#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Orbit {

template <typename T>
struct LinearDrift {
	T drift;
	T min, center, max;

	void setMinCenterMax(T min, T center, T max) {
		this->min = min;
		this->center = center;
		this->max = max;
	}

	void setDrift(T drift) {
		this->drift = drift;
	}

	T process(float deltaTime, T in) {
		T l = simd::ifelse(in < center, min, center);
		T h = simd::ifelse(in > center, max, center);
		T d = drift * deltaTime;
		T out = simd::ifelse(in < center, in + d, in - d);
		out = simd::clamp(out, l, h);
		return out;
	}
};

struct OrbitModule : Module {
	enum ParamIds {
		PARAM_SPREAD,
		PARAM_DRIFT,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_SPREAD,
		INPUT_SOURCE,
		INPUT_IN,
		INPUT_TRIG,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_L,
		OUTPUT_R,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	bool polyOut;

	float pan[PORT_MAX_CHANNELS];
	dsp::SchmittTrigger trigger[PORT_MAX_CHANNELS];
	dsp::ExponentialFilter clickFilter[PORT_MAX_CHANNELS];
	LinearDrift<float> linearDrift[PORT_MAX_CHANNELS];

	OrbitModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_SPREAD, 0.f, 1.f, 0.5f, "Maximum stereo spread", "%", 0.f, 100.f);
		configParam(PARAM_DRIFT, -1.f, 1.f, 0.f, "Drift (<0 --> L/R, >0 --> center)");
		onReset();
	}

	void onReset() override {
		polyOut = false;
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			pan[c] = 0.5f;
			clickFilter[c].setTau(0.005f);
			linearDrift[c].setMinCenterMax(0.f, 0.5, 1.f);
		}
		Module::onReset();
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[INPUT_IN].getChannels();
		float spread = inputs[INPUT_SPREAD].getNormalVoltage(10.f) / 10.f * params[PARAM_SPREAD].getValue();
		float drift = params[PARAM_DRIFT].getValue();

		float outL[PORT_MAX_CHANNELS];
		float sumL = 0.f;
		float outR[PORT_MAX_CHANNELS];
		float sumR = 0.f;

		for (int c = 0; c < channels; c++) {
			linearDrift[c].setDrift(drift);

			if (trigger[c].process(inputs[INPUT_TRIG].getPolyVoltage(c))) {
				float p = inputs[INPUT_SOURCE].isConnected() ? (inputs[INPUT_SOURCE].getPolyVoltage(c) / 5.f) : random::normal();
				pan[c] = clamp(p * spread / 2.f + 0.5f, 0.f, 1.f);
			}

			pan[c] = linearDrift[c].process(args.sampleTime, pan[c]);
			float p = clickFilter[c].process(args.sampleTime, pan[c]);
			float v = inputs[INPUT_IN].getVoltage(c);
			outL[c] = p * v;
			sumL += outL[c];
			outR[c] = (1.f - p) * v;
			sumR += outR[c];
		}

		if (polyOut) {
			outputs[OUTPUT_L].setChannels(channels);
			outputs[OUTPUT_R].setChannels(channels);
			outputs[OUTPUT_L].writeVoltages(outL);
			outputs[OUTPUT_R].writeVoltages(outR);
		}
		else {
			outputs[OUTPUT_L].setChannels(1);
			outputs[OUTPUT_L].setVoltage(sumL / float(channels));
			outputs[OUTPUT_R].setChannels(1);
			outputs[OUTPUT_R].setVoltage(sumR / float(channels));
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "polyOut", json_boolean(polyOut));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		polyOut = json_boolean_value(json_object_get(rootJ, "polyOut"));
	}
};

struct OrbitWidget : ThemedModuleWidget<OrbitModule> {
	OrbitWidget(OrbitModule* module)
		: ThemedModuleWidget<OrbitModule>(module, "Orbit") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<StoermelderSmallKnob>(Vec(37.5f, 60.6f), module, OrbitModule::PARAM_SPREAD));
		addInput(createInputCentered<StoermelderPort>(Vec(37.5f, 87.7f), module, OrbitModule::INPUT_SPREAD));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(37.5f, 133.9f), module, OrbitModule::PARAM_DRIFT));
		addInput(createInputCentered<StoermelderPort>(Vec(37.5f, 236.2f), module, OrbitModule::INPUT_SOURCE));

		addInput(createInputCentered<StoermelderPort>(Vec(23.5f, 281.9f), module, OrbitModule::INPUT_IN));
		addInput(createInputCentered<StoermelderPort>(Vec(51.5f, 281.9f), module, OrbitModule::INPUT_TRIG));
		addOutput(createOutputCentered<StoermelderPort>(Vec(23.5f, 327.7f), module, OrbitModule::OUTPUT_L));
		addOutput(createOutputCentered<StoermelderPort>(Vec(51.5f, 327.7f), module, OrbitModule::OUTPUT_R));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<OrbitModule>::appendContextMenu(menu);
		OrbitModule* module = dynamic_cast<OrbitModule*>(this->module);
		assert(module);

		struct PolyOutItem : MenuItem {
			OrbitModule* module;
			void onAction(const event::Action& e) override {
				module->polyOut ^= true;
			}
			void step() override {
				rightText = CHECKMARK(module->polyOut);
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PolyOutItem>(&MenuItem::text, "Polyphonic output", &PolyOutItem::module, module));
	}
};

} // namespace Orbit
} // namespace StoermelderPackOne

Model* modelOrbit = createModel<StoermelderPackOne::Orbit::OrbitModule, StoermelderPackOne::Orbit::OrbitWidget>("Orbit");