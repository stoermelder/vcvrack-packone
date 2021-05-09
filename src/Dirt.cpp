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

struct NextGenerator {
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
		float f[channels] = {0.f};
		for (int i = 0; i < channels; i++) {
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

struct DirtModule : Module {
	enum ParamIds {
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
	NextGenerator next;

	DirtModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			noise[i].reset();
 		}
		next.reset();
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[INPUT].getChannels();

		float in[channels];
		inputs[INPUT].readVoltages(in);

		for (int i = 0; i < channels; i++) {
			in[i] += noise[i].process();
		}
		next.process(in, channels);

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
			json_object_set_new(channelJ, "nextRatio", json_real(next.ratio[i]));
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
			next.ratio[i] = json_real_value(json_object_get(channelJ, "nextRatio"));
		}
	}
};

struct DirtWidget : ThemedModuleWidget<DirtModule> {
	DirtWidget(DirtModule* module)
		: ThemedModuleWidget<DirtModule>(module, "Dirt") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 291.1f), module, DirtModule::INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, DirtModule::OUTPUT));
	}
};

} // namespace Dirt
} // namespace StoermelderPackOne

Model* modelDirt = createModel<StoermelderPackOne::Dirt::DirtModule, StoermelderPackOne::Dirt::DirtWidget>("Dirt");