#include "catch2/plugin.hpp"
#include "test_context.hpp"
#include "../modules/midiesx/MidiEsx.hpp"
#include "../modules/midiesx/MidiEsx.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiEsx;

// Define the single instance used by tests
static Test::TestContext<> testContext;

// Helper: collect samples from port 0 by repeatedly calling nextBit()
static std::vector<float> collectSamples(MidiEsxModule* module, int maxSamples = 512) {
	std::vector<float> samples;
	samples.reserve(maxSamples);
	for (int i = 0; i < maxSamples; ++i) {
		float v = module->port[0].nextBit();
		samples.push_back(v);
		// Stop early if buffer drained
		if (module->port[0].bitQueue.size() == 0) break;
	}
	return samples;
}

int countMessageBits(const rack::midi::Message& message) {
	int count = 0;
	for (int i = 0; i < message.getSize(); ++i) {
		unsigned char b = message.bytes[i];
		// Count set bits in the byte
		for (int j = 0; j < 8; ++j) {
			if (b & (1u << j)) ++count;
		}
	}
	return count;
}


TEST_CASE("Encoding creates fractional samples correctly (approx)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");

	// bytes: 0x90 (10010000)=2, 60 (00111100)=4, 100 (01100100)=3 -> total 9
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);

	// Calculate bit weight for reference (start+stop bits included)
	float bitWeight = 1.f - (countMessageBits(msg) + 2) / (float(msg.getSize()) * 8.f + 4.f);

	module->onMessage(0, msg);
	auto samples = collectSamples(module, 128);
	float bitWeightReal = 0.f;
	for (float s : samples) bitWeightReal += s;
	bitWeightReal /= float(samples.size());

	REQUIRE(bitWeightReal == Catch::Approx(bitWeight).margin(0.1f));

	Test::destroyModule(module);
}
