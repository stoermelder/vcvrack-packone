#include "../../plugin.hpp"
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

struct PitchDefectProcessor {
	static constexpr size_t BUFFER_SIZE = 16384;
	std::mt19937 rng;
	std::uniform_real_distribution<float> uniform;

	float sampleBuffers[PORT_MAX_CHANNELS][BUFFER_SIZE];
	size_t writeIndices[PORT_MAX_CHANNELS];
	float pitchStates[PORT_MAX_CHANNELS];
	int defectCounters[PORT_MAX_CHANNELS];
	float defectSpeeds[PORT_MAX_CHANNELS];
	float readPhases[PORT_MAX_CHANNELS];
	float returnTime[PORT_MAX_CHANNELS];
	size_t nextTriggerSamples[PORT_MAX_CHANNELS];
	dsp::SlewLimiter slewLimiter[PORT_MAX_CHANNELS];
	float sampleRate;

	// Unique per-instance parameters
	float baseProb;
	float speedMin;
	float speedMax;

	PitchDefectProcessor() {
		baseProb = 0.02f + 0.4f * random::uniform();
		speedMin = 0.1f + 0.7f * random::uniform();
		speedMax = 1.1f + 0.4f * random::uniform();
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			slewLimiter[i].setRiseFall(10.f, 10.f);
		}
		reset();
	}

	void reset() {
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			for (size_t j = 0; j < BUFFER_SIZE; j++) {
				sampleBuffers[i][j] = 0.0f;
			}
			writeIndices[i] = 0;
			pitchStates[i] = 0.0f;
			defectCounters[i] = 0;
			defectSpeeds[i] = 1.0f;
			nextTriggerSamples[i] = 0;
			slewLimiter[i].reset();
		}

		uniform = std::uniform_real_distribution<float>(0.0f, 1.0f);
		rng.seed(std::random_device()());
	}

	void setRateChange(float sampleRate) {
		this->sampleRate = sampleRate;
		reset();
	}

	void process(float* in, int channels, float sampleTime) {
		// Store samples
		for (int c = 0; c < channels; ++c) {
			sampleBuffers[c][writeIndices[c]] = in[c];
			writeIndices[c] = (writeIndices[c] + 1) % BUFFER_SIZE;
		}

		// Trigger effect
		for (int c = 0; c < channels; ++c) {
			if (defectCounters[c] <= 0) {
				if (nextTriggerSamples[c] == 0) {
					if (uniform(rng) < baseProb) {
						// Defect duration: 0.2 to 0.6 seconds
						int duration = static_cast<int>(sampleRate * (0.2f + 0.6f * uniform(rng)));
						defectCounters[c] = duration;
						defectSpeeds[c] = crossfade(speedMin, speedMax, uniform(rng));

						// Align read phase to current write index at start of effect
						readPhases[c] = static_cast<float>(writeIndices[c]);
						returnTime[c] = sampleTime * crossfade(2.f, 30.f, uniform(rng));
					}

					// Always schedule next check: 1 to 8 seconds later
					size_t gap = static_cast<size_t>(sampleRate * (1.0f + 7.0f * uniform(rng))) + 1;
					nextTriggerSamples[c] = gap;
				}
				nextTriggerSamples[c]--;
			}
		}

		// Process audio with interpolated read pointer
		for (int c = 0; c < channels; ++c) {
			// Get interpolation amount from slew limiter
			float s = slewLimiter[c].process(sampleTime, defectCounters[c] > 0);

			if (defectCounters[c] > 0) {
				// Apply pitch shift: advance read phase
				readPhases[c] += defectSpeeds[c];

				// Wrap read phase to [0, BUFFER_SIZE)
				while (readPhases[c] >= BUFFER_SIZE) readPhases[c] -= BUFFER_SIZE;
				while (readPhases[c] < 0) readPhases[c] += BUFFER_SIZE;

				defectCounters[c]--;
			} else {
				// Smoothly interpolate read phase back to write index
				float writeIndexFloat = static_cast<float>(writeIndices[c]);
				float diff = writeIndexFloat - readPhases[c];

				// Wrap diff into [-BUFFER_SIZE/2, BUFFER_SIZE/2) range
				diff = diff - BUFFER_SIZE * std::floor(diff / BUFFER_SIZE + 0.5f);

				// Smooth interpolation (tune this factor for response speed)
				readPhases[c] += diff * returnTime[c];
				// To be extra safe
				readPhases[c] = std::fmod(readPhases[c] + BUFFER_SIZE, BUFFER_SIZE);
			}

			// Interpolate sample from buffer
			int index1 = static_cast<int>(readPhases[c]);
			int index2 = (index1 + 1) % BUFFER_SIZE;
			float frac = readPhases[c] - static_cast<float>(index1);
			float d = sampleBuffers[c][index1] * (1.0f - frac) + sampleBuffers[c][index2] * frac;

			// Crossfade between dry and wet
			in[c] = crossfade(in[c], d, s);
		}
	}

	json_t* dataToJson() {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "baseProb", json_real(baseProb));
		json_object_set_new(rootJ, "speedMin", json_real(speedMin));
		json_object_set_new(rootJ, "speedMax", json_real(speedMax));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) {
		json_t* baseProbJ = json_object_get(rootJ, "baseProb");
		if (baseProbJ) baseProb = json_real_value(baseProbJ);

		json_t* speedMinJ = json_object_get(rootJ, "speedMin");
		if (speedMinJ) speedMin = json_real_value(speedMinJ);

		json_t* speedMaxJ = json_object_get(rootJ, "speedMax");
		if (speedMaxJ) speedMax = json_real_value(speedMaxJ);
	}
};

struct CrushDefectProcessor {
	std::mt19937 rng;
	std::uniform_real_distribution<float> uniform;

	// Bitcrush effect parameters
	struct BitcrushParams {
		float bit_depth;    		// Number of bits to quantize to (1-16)
		float crush_rate;   		// Crushed sample rate (e.g., 22050 Hz)
		float levels;
	};

	// Bitcrush effect state
	struct BitcrushState {
		float phase;                 // Current phase for sample rate reduction
		float last_sample[16];       // Last sample for each channel
	};

	BitcrushParams g_params[PORT_MAX_CHANNELS];
	BitcrushState g_state[PORT_MAX_CHANNELS];
	int defectCounters[PORT_MAX_CHANNELS];
	size_t nextTriggerSamples[PORT_MAX_CHANNELS];
	dsp::SlewLimiter slewLimiter[PORT_MAX_CHANNELS];
	float sampleRate;

	// Unique per-instance parameters
	float baseProb;
	float speedMin;
	float speedMax;

	CrushDefectProcessor() {
		baseProb = 0.1f + 0.5f * random::uniform();
		speedMax = 0.2f + 0.3f * random::uniform();
		speedMin = 0.1f + 0.1f * random::uniform();
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			slewLimiter[i].setRiseFall(350.f, 350.f);
		}
		reset();
	}

	void reset() {
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			defectCounters[i] = 0;
			nextTriggerSamples[i] = 0;
			slewLimiter[i].reset();
		}

		uniform = std::uniform_real_distribution<float>(0.0f, 1.0f);
		rng.seed(std::random_device()());
	}

	void setRateChange(float sampleRate) {
		this->sampleRate = sampleRate;
		reset();
	}

	void process(float* in, int channels, float sampleTime) {
		// Trigger effect
		for (int c = 0; c < channels; ++c) {
			if (defectCounters[c] <= 0) {
				if (nextTriggerSamples[c] == 0) {
					if (uniform(rng) < baseProb) {
						// Defect duration: 0.4 to 1.4 seconds
						int duration = static_cast<int>(sampleRate * (0.4f + 1.0f * uniform(rng)));
						defectCounters[c] = duration;
						g_params[c].crush_rate = sampleRate * crossfade(speedMin, speedMax, uniform(rng));
						g_params[c].bit_depth = crossfade(0.2f, 12.f, uniform(rng));
						// Calculate quantization levels based on bit depth
						g_params[c].levels = powf(2.0f, g_params[c].bit_depth) - 1.0f;
					}

					// Always schedule next check: 4 to 14 seconds later
					size_t gap = static_cast<size_t>(sampleRate * (4.0f + 10.0f * uniform(rng))) + 1;
					nextTriggerSamples[c] = gap;
				}
				nextTriggerSamples[c]--;
			}
		}

		for (int c = 0; c < channels; ++c) {
			float s = slewLimiter[c].process(sampleTime, defectCounters[c] > 0);
			if (s > 0.f) {			
				// Update phase for sample rate reduction
				g_state[c].phase += g_params[c].crush_rate / sampleRate;
				
				if (g_state[c].phase >= 1.0f) {
					// Quantization: reduce bit depth
					float quantized = roundf(in[c] * g_params[c].levels) / g_params[c].levels;
					g_state[c].last_sample[c] = quantized;
				}
				
				// Apply last processed sample (sample and hold)
				float d = g_state[c].last_sample[c];
				
				// Decrement phase if needed
				if (g_state[c].phase >= 1.0f) {
					g_state[c].phase -= 1.0f;
				}

				in[c] = crossfade(in[c], d, s);
				defectCounters[c]--;
			}
		}
	}

	json_t* dataToJson() {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "baseProb", json_real(baseProb));
		json_object_set_new(rootJ, "speedMin", json_real(speedMin));
		json_object_set_new(rootJ, "speedMax", json_real(speedMax));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) {
		json_t* baseProbJ = json_object_get(rootJ, "baseProb");
		if (baseProbJ) baseProb = json_real_value(baseProbJ);

		json_t* speedMinJ = json_object_get(rootJ, "speedMin");
		if (speedMinJ) speedMin = json_real_value(speedMinJ);

		json_t* speedMaxJ = json_object_get(rootJ, "speedMax");
		if (speedMaxJ) speedMax = json_real_value(speedMaxJ);
	}
};

struct DropoutDefectProcessor {
	std::mt19937 rng;
	std::uniform_real_distribution<float> uniform;

	int defectCounters[PORT_MAX_CHANNELS];
	float dropoutDepth[PORT_MAX_CHANNELS];
	size_t nextTriggerSamples[PORT_MAX_CHANNELS];
	dsp::SlewLimiter slewLimiter[PORT_MAX_CHANNELS];
	float sampleRate;

	// Unique per-instance parameters
	float baseProb;
	float dropoutMax;

	DropoutDefectProcessor() {
		baseProb = 0.02f + 0.6f * random::uniform();
		dropoutMax = 0.5f * random::uniform();
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			slewLimiter[i].setRiseFall(100.f, 100.f);
		}
		reset();
	}

	void reset() {
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			defectCounters[i] = 0;
			dropoutDepth[i] = 1.0f;
			nextTriggerSamples[i] = 0;
			slewLimiter[i].reset();
		}

		uniform = std::uniform_real_distribution<float>(0.0f, 1.0f);
		rng.seed(std::random_device()());
	}

	void setRateChange(float sampleRate) {
		this->sampleRate = sampleRate;
		reset();
	}

	void process(float* in, int channels, float sampleTime) {
		// Trigger effect
		for (int c = 0; c < channels; ++c) {
			if (defectCounters[c] <= 0) {
				if (nextTriggerSamples[c] == 0) {
					if (uniform(rng) < baseProb) {
						// Defect duration: 0.05 to 0.5 seconds
						int duration = static_cast<int>(sampleRate * (0.05f + 0.45f * uniform(rng)));
						defectCounters[c] = duration;
						dropoutDepth[c] = dropoutMax * uniform(rng);
					}

					// Always schedule next check: 1 to 10 seconds later
					size_t gap = static_cast<size_t>(sampleRate * (1.0f + 7.0f * uniform(rng))) + 1;
					nextTriggerSamples[c] = gap;
				}
				nextTriggerSamples[c]--;
			}
		}

		for (int c = 0; c < channels; ++c) {
			float s = slewLimiter[c].process(sampleTime, defectCounters[c] > 0);
			if (s > 0.f) {
				// Apply dropout effect
				in[c] *= crossfade(1.f, dropoutDepth[c], s);
				defectCounters[c]--;
			}
		}
	}

	json_t* dataToJson() {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "baseProb", json_real(baseProb));
		json_object_set_new(rootJ, "dropoutMax", json_real(dropoutMax));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) {
		json_t* baseProbJ = json_object_get(rootJ, "baseProb");
		if (baseProbJ) baseProb = json_real_value(baseProbJ);

		json_t* dropoutMaxJ = json_object_get(rootJ, "dropoutMax");
		if (dropoutMaxJ) dropoutMax = json_real_value(dropoutMaxJ);
	}
};


struct DirtModule : Module {
	enum ParamIds {
		PARAM_NOISE,
		PARAM_CROSSTALK,
		PARAM_CRACKE,
		PARAM_PITCH,
		PARAM_CRUSH,
		PARAM_DROPOUT,
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
	CrushDefectProcessor crushDefect;
	DropoutDefectProcessor dropoutDefect;
	PitchDefectProcessor pitchDefect;

	DirtModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(PARAM_NOISE, 0.f, 1.f, 1.f, "White noise", {"Off", "On"});
		configSwitch(PARAM_CROSSTALK, 0.f, 1.f, 1.f, "Crosstalk between channels of a polyphonic cable", {"Off", "On"});
		configSwitch(PARAM_CRACKE, 0.f, 1.f, 1.f, "Crackle", {"Off", "On"});
		configSwitch(PARAM_PITCH, 0.f, 1.f, 1.f, "Pitch defects", {"Off", "On"});
		configSwitch(PARAM_CRUSH, 0.f, 1.f, 1.f, "Crush defects", {"Off", "On"});
		configSwitch(PARAM_DROPOUT, 0.f, 1.f, 1.f, "Dropout defects", {"Off", "On"});
		configInput(INPUT, "Polyphonic");
		configOutput(OUTPUT, "Polyphonic");
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			noise[i].reset();
 		}
		crosstalk.reset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		crushDefect.setRateChange(e.sampleRate);
		dropoutDefect.setRateChange(e.sampleRate);
		pitchDefect.setRateChange(e.sampleRate);
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[INPUT].getChannels();
		if (!inputs[INPUT].isConnected()) return;

		float in[channels];
		inputs[INPUT].readVoltages(in);

		if (params[PARAM_PITCH].getValue() > 0.f) {
			pitchDefect.process(in, channels, args.sampleTime);
		}

		if (params[PARAM_CRUSH].getValue() > 0.f) {
			crushDefect.process(in, channels, args.sampleTime);
		}

		if (params[PARAM_DROPOUT].getValue() > 0.f) {
			dropoutDefect.process(in, channels, args.sampleTime);
		}

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

		json_object_set_new(rootJ, "pitchDefect", pitchDefect.dataToJson());
		json_object_set_new(rootJ, "crushDefect", crushDefect.dataToJson());
		json_object_set_new(rootJ, "dropoutDefect", dropoutDefect.dataToJson());

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

		json_t* pitchDefectJ = json_object_get(rootJ, "pitchDefect");
		if (pitchDefectJ) pitchDefect.dataFromJson(pitchDefectJ);
		else params[PARAM_PITCH].setValue(0.f);

		json_t* crushDefectJ = json_object_get(rootJ, "crushDefect");
		if (crushDefectJ) crushDefect.dataFromJson(crushDefectJ);
		else params[PARAM_CRUSH].setValue(0.f);

		json_t* dropoutDefectJ = json_object_get(rootJ, "dropoutDefect");
		if (dropoutDefectJ) dropoutDefect.dataFromJson(dropoutDefectJ);
		else params[PARAM_DROPOUT].setValue(0.f);
	}
};

struct DirtWidget : ThemedModuleWidget<DirtModule> {
	DirtWidget(DirtModule* module)
		: ThemedModuleWidget<DirtModule>(module, "Dirt") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<CKSS>(Vec(22.5f, 64.2f), module, DirtModule::PARAM_NOISE));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 102.6f), module, DirtModule::PARAM_CROSSTALK));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 141.0f), module, DirtModule::PARAM_CRACKE));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 179.5f), module, DirtModule::PARAM_PITCH));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 217.9f), module, DirtModule::PARAM_CRUSH));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 256.3f), module, DirtModule::PARAM_DROPOUT));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 291.1f), module, DirtModule::INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, DirtModule::OUTPUT));
	}
};

} // namespace Dirt
} // namespace StoermelderPackOne

Model* modelDirt = createModel<StoermelderPackOne::Dirt::DirtModule, StoermelderPackOne::Dirt::DirtWidget>("Dirt");