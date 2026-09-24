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

	CrosstalkGenerator() {
		reset();
	}

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


} // namespace Dirt
} // namespace StoermelderPackOne