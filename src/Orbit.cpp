#include "plugin.hpp"
#include "components/Knobs.hpp"

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


enum class DISTRIBUTION {
	EXTERNAL = 0,
	NORMAL = 1,
	NORMAL_MIRROR = 3,
	UNIFORM = 2
};

struct OrbitModule : Module {
	enum ParamIds {
		PARAM_SPREAD,
		PARAM_DRIFT,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_SPREAD,
		INPUT_DIST,
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
	/** [Stored to JSON] */
	DISTRIBUTION dist;

	float pan[PORT_MAX_CHANNELS];
	dsp::SchmittTrigger trigger[PORT_MAX_CHANNELS];
	dsp::ExponentialFilter clickFilter[PORT_MAX_CHANNELS];
	LinearDrift<float> linearDrift[PORT_MAX_CHANNELS];

	OrbitModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_SPREAD, "Spread amount CV");
		inputInfos[INPUT_SPREAD]->description = "Normalized to 10V (full stereo field).";
		configInput(INPUT_DIST, "Distribution");
		inputInfos[INPUT_DIST]->description = "Optional, used if distribution is set to \"External\", 0..10V.";
		configInput(INPUT_IN, "Signal");
		configInput(INPUT_TRIG, "Stereo spread trigger");
		inputInfos[INPUT_TRIG]->description = "Polyphonic, normalized to the first channel.";
		configOutput(OUTPUT_L, "Left channel");
		outputInfos[OUTPUT_L]->description = "Downmixed signal, optional polyphonic by context menu option.";
		configOutput(OUTPUT_R, "Right channel");
		outputInfos[OUTPUT_R]->description = "Downmixed signal, optional polyphonic by context menu option.";
		configParam(PARAM_SPREAD, 0.f, 1.f, 0.5f, "Maximum stereo spread", "%", 0.f, 100.f);
		configParam(PARAM_DRIFT, -1.f, 1.f, 0.f, "Stereo drift (-1..0 --> L/R, 0..+1 --> center)");
		onReset();
	}

	void onReset() override {
		polyOut = false;
		dist = DISTRIBUTION::NORMAL;
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			pan[c] = 0.5f;
			clickFilter[c].setTau(0.005f);
			linearDrift[c].setMinCenterMax(0.f, 0.5, 1.f);
		}
		Module::onReset();
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[INPUT_IN].getChannels();
		float drift = params[PARAM_DRIFT].getValue();

		float outL[PORT_MAX_CHANNELS];
		float sumL = 0.f;
		float outR[PORT_MAX_CHANNELS];
		float sumR = 0.f;

		for (int c = 0; c < channels; c++) {
			linearDrift[c].setDrift(drift);

			if (trigger[c].process(inputs[INPUT_TRIG].getPolyVoltage(c))) {
				float spread = inputs[INPUT_SPREAD].getNormalVoltage(10.f) / 10.f * params[PARAM_SPREAD].getValue();
				float p = 0.5f; // position between 0 and 1, 0.5 is center
				switch (dist) {
					case DISTRIBUTION::EXTERNAL:
						p = inputs[INPUT_DIST].getPolyVoltage(c) / 10.f + 0.5f;
						break;
					case DISTRIBUTION::NORMAL:
						p = random::normal() / 6.f + 0.5f;
						break;
					case DISTRIBUTION::NORMAL_MIRROR:
						p = random::normal();
						p = (3.f * sgn(-p) + p) / 6.f + 0.5f;
						break;
					case DISTRIBUTION::UNIFORM:
						p = random::uniform();
						break;
				}
				pan[c] = clamp(p * spread, 0.f, 1.f);
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
			outputs[OUTPUT_L].setVoltage(sumL);
			outputs[OUTPUT_R].setChannels(1);
			outputs[OUTPUT_R].setVoltage(sumR);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "polyOut", json_boolean(polyOut));
		json_object_set_new(rootJ, "dist", json_integer((int)dist));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		polyOut = json_boolean_value(json_object_get(rootJ, "polyOut"));
		dist = (DISTRIBUTION)json_integer_value(json_object_get(rootJ, "dist"));
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
		addInput(createInputCentered<StoermelderPort>(Vec(37.5f, 236.2f), module, OrbitModule::INPUT_DIST));

		addInput(createInputCentered<StoermelderPort>(Vec(23.5f, 281.9f), module, OrbitModule::INPUT_IN));
		addInput(createInputCentered<StoermelderPort>(Vec(51.5f, 281.9f), module, OrbitModule::INPUT_TRIG));
		addOutput(createOutputCentered<StoermelderPort>(Vec(23.5f, 327.7f), module, OrbitModule::OUTPUT_L));
		addOutput(createOutputCentered<StoermelderPort>(Vec(51.5f, 327.7f), module, OrbitModule::OUTPUT_R));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<OrbitModule>::appendContextMenu(menu);
		OrbitModule* module = dynamic_cast<OrbitModule*>(this->module);

		menu->addChild(new MenuSeparator());
		menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem("Distribution",
			{
				{ DISTRIBUTION::NORMAL, "Normal" },
				{ DISTRIBUTION::NORMAL_MIRROR, "Normal \"mirrored\"" },
				{ DISTRIBUTION::UNIFORM, "Uniform" },
				{ DISTRIBUTION::EXTERNAL, "External" }
			},
			&module->dist
		));
		menu->addChild(createBoolPtrMenuItem("Polyphonic output", &module->polyOut));
	}
};

} // namespace Orbit
} // namespace StoermelderPackOne

Model* modelOrbit = createModel<StoermelderPackOne::Orbit::OrbitModule, StoermelderPackOne::Orbit::OrbitWidget>("Orbit");