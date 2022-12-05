#include "plugin.hpp"
#include <random>

namespace StoermelderPackOne {
namespace Dirt {

struct WhiteNoiseGenerator {
	std::mt19937 rng;
	std::uniform_real_distribution<float> uniform;
	float ratio;

	WhiteNoiseGenerator() {
		reset();
	}

	void reset() {
		// Exponential distribution
		ratio = -std::log(random::uniform()) * 0.004f;
		// White noise generator
		uniform = std::uniform_real_distribution<float>(-ratio, ratio);
		rng.seed(std::random_device()());
	}

	float process() {
		return uniform(rng);
	}
};

struct CrosstalkGenerator {
	float ratio[PORT_MAX_CHANNELS];

	dsp::BiquadFilter eqLow[PORT_MAX_CHANNELS];
	dsp::BiquadFilter eqHigh[PORT_MAX_CHANNELS];

	void reset() {
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) {
			eqLow[i].setParameters(dsp::BiquadFilter::LOWSHELF, 400.f / APP->engine->getSampleRate(), 1.f, 15.f);
			eqHigh[i].setParameters(dsp::BiquadFilter::HIGHSHELF, 8000.f / APP->engine->getSampleRate(), 1.f, 15.f);

			// Exponential distribution
			ratio[i] = -std::log(random::uniform()) * 0.005f;
		}
	}

	void process(float* in, int channels) {
		float f[channels];
		for (int i = 0; i < channels; i++) {
			f[i] = 0.f;
			// Apply shelfing on low and high end
			f[i] += eqLow[i].process(in[i]);
			f[i] += eqHigh[i].process(in[i]);
		}

		for (int i = 0; i < channels; i++) {
			if (i > 0) 
				in[i] += f[i - 1] * ratio[i - 1];
			if (i < channels - 1)
				in[i] += f[i + 1] * ratio[i];
		}
	}
};

struct CrackleGenerator {
	std::mt19937 rng;
	std::uniform_real_distribution<float> uniform;
	float ratio[16];

	CrackleGenerator() {
		reset();
	}

	void reset() {
		for (int i = 0; i < 16; i++) {
			ratio[i] = 8.f + 5.5f * random::uniform();
		}

		uniform = std::uniform_real_distribution<float>(0.0f, 1.0f);
		rng.seed(std::random_device()());
	}

	void process(float* in, int channels) {
		for (int i = 0; i < channels; i++) {
			// Laplace distribution
			// https://en.wikipedia.org/wiki/Laplace_distribution
			float u = uniform(rng) - 0.5f;
			float c = sgn(u) * std::log(1 - 2 * abs(u));
			// "Filter" out small values
			in[i] += abs(c) > ratio[i] ? 0.025f * c : 0.f;
		}
	}
};


struct DirtModule : Module {
	enum ParamIds {
		PARAM_NOISE,
		PARAM_CROSSTALK,
		PARAM_CRACKE,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	WhiteNoiseGenerator noise[PORT_MAX_CHANNELS];
	CrosstalkGenerator crosstalk;
	CrackleGenerator crackle;

	DirtModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(PARAM_NOISE, 0.f, 1.f, 1.f, "White noise per channel", {"Off", "On"});
		configSwitch(PARAM_CROSSTALK, 0.f, 1.f, 1.f, "Crosstalk between channels of a polyphonic cable", {"Off", "On"});
		configSwitch(PARAM_CRACKE, 0.f, 1.f, 1.f, "Crackle per channel", {"Off", "On"});
		configInput(INPUT, "Polyphonic");
		configOutput(OUTPUT, "Polyphonic");
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			noise[i].reset();
 		}
		crosstalk.reset();
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[INPUT].getChannels();

		float in[channels];
		inputs[INPUT].readVoltages(in);

		if (params[PARAM_NOISE].getValue() > 0.f) {
			for (int i = 0; i < channels; i++) {
				in[i] += noise[i].process();
			}
		}

		if (params[PARAM_CROSSTALK].getValue() > 0.f) {
			crosstalk.process(in, channels);
		}

		if (params[PARAM_CRACKE].getValue() > 0.f) {
			crackle.process(in, channels);
		}

		outputs[OUTPUT].setChannels(channels);
		outputs[OUTPUT].writeVoltages(in);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* channelsJ = json_array();
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) {
			json_t* channelJ = json_object();
			json_object_set_new(channelJ, "noiseRatio", json_real(noise[i].ratio));
			json_object_set_new(channelJ, "crosstalkRatio", json_real(crosstalk.ratio[i]));
			json_object_set_new(channelJ, "crackleRatio", json_real(crackle.ratio[i]));
			json_array_append_new(channelsJ, channelJ);
		}
		json_object_set_new(rootJ, "channels", channelsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* channelsJ = json_object_get(rootJ, "presets");
		json_t* channelJ;
		size_t i;
		json_array_foreach(channelsJ, i, channelJ) {
			noise[i].ratio = json_real_value(json_object_get(channelJ, "noiseRatio"));
			crosstalk.ratio[i] = json_real_value(json_object_get(channelJ, "crosstalkRatio"));
			crackle.ratio[i] = json_real_value(json_object_get(channelJ, "crackleRatio"));
		}
	}
};

struct DirtWidget : ThemedModuleWidget<DirtModule> {
	DirtWidget(DirtModule* module)
		: ThemedModuleWidget<DirtModule>(module, "Dirt") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<CKSS>(Vec(22.5f, 87.7f), module, DirtModule::PARAM_NOISE));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 128.1f), module, DirtModule::PARAM_CROSSTALK));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 168.5f), module, DirtModule::PARAM_CRACKE));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 291.1f), module, DirtModule::INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, DirtModule::OUTPUT));
	}
};

} // namespace Dirt
} // namespace StoermelderPackOne

Model* modelDirt = createModel<StoermelderPackOne::Dirt::DirtModule, StoermelderPackOne::Dirt::DirtWidget>("Dirt");