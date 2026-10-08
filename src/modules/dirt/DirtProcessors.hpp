#include "../../plugin.hpp"
#include <random>

namespace StoermelderPackOne {
namespace Dirt {


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
			g_params[i].bit_depth = 12.f;
			g_params[i].crush_rate = 0.f;
			g_params[i].levels = powf(2.0f, g_params[i].bit_depth) - 1.0f;
			g_state[i].phase = 0.f;
			for (size_t j = 0; j < 16; j++) {
				g_state[i].last_sample[j] = 0.f;
			}
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

} // namespace Dirt
} // namespace StoermelderPackOne