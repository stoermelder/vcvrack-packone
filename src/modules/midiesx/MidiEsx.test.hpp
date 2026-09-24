#pragma once

// Shared preamble for the MIDIESX test suite.
// Included by MidiEsx.test.cpp, which pulls the test cases in from MidiEsx.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiEsx.hpp"
#include "MidiEsx.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiEsx;

static Test::TestContext<> testContext;

// =====================================================
// Helper functions
// =====================================================

// Collect samples from port 0 by repeatedly calling nextBit()
static std::vector<float> collectSamples(MidiEsxModule* module, int maxSamples = 512) {
	std::vector<float> samples;
	samples.reserve(maxSamples);
	for (int i = 0; i < maxSamples; ++i) {
		float v = module->port[0].nextBit();
		samples.push_back(v);
		if (module->port[0].bitQueue.size() == 0) break;
	}
	return samples;
}

// Count set bits in a MIDI message
int countMessageBits(const rack::midi::Message& message) {
	int count = 0;
	for (int i = 0; i < message.getSize(); ++i) {
		unsigned char b = message.bytes[i];
		for (int j = 0; j < 8; ++j) {
			if (b & (1u << j)) ++count;
		}
	}
	return count;
}

// Verify bit stream length for expected message size (16 bits per byte)
static bool verifyBitStreamLength(const std::vector<float>& bits, size_t messageBytes) {
	return bits.size() >= messageBytes * 16;
}

// Count transitions in bit stream (verifies data is actually encoded)
static int countTransitions(const std::vector<float>& bits) {
	int transitions = 0;
	for (size_t i = 1; i < bits.size(); ++i) {
		if ((bits[i] > 0.5f) != (bits[i-1] > 0.5f)) {
			transitions++;
		}
	}
	return transitions;
}