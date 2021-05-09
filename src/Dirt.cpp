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
	float ratioLeft;
	float ratioRight;

	void reset() {
		// Exponential distribution
		ratioLeft = -std::log(random::uniform()) * 0.015f;
		ratioRight = -std::log(random::uniform()) * 0.015f;
	}

	float process(float* in, int index, int channels) {
		float r = 0.f;
		if (index > 0) 
			r += in[index - 1] * ratioLeft;
		if (index < channels - 1)
			r += in[index + 1] * ratioRight;
		return r;
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
	NextGenerator next[PORT_MAX_CHANNELS];

	DirtModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			noise[i].reset();
			next[i].reset();
 		}
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[INPUT].getChannels();

		float in[channels];
		inputs[INPUT].readVoltages(in);

		for (int i = 0; i < channels; i++) {
			in[i] += noise[i].process();
			in[i] += next[i].process(in, i, channels);
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
			json_object_set_new(channelJ, "nextRatioLeft", json_real(next[i].ratioLeft));
			json_object_set_new(channelJ, "nextRadioRight", json_real(next[i].ratioRight));
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
			next[i].ratioLeft = json_real_value(json_object_get(channelJ, "nextRatioLeft"));
			next[i].ratioRight = json_real_value(json_object_get(channelJ, "nextRadioRight"));
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