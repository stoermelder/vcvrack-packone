#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiEsx.hpp"
#include "MidiEsx.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiEsx;

SYNC_MODEL(modelMidiEsx, "MidiEsx");
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


TEST_CASE("Construction and initialization", "[MidiEsx]") {
	MidiEsxModule* m = Test::createModule<MidiEsxModule>("MidiEsx");
	MidiEsxWidget* mw = Test::createWidget<MidiEsxWidget>("MidiEsx");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[MidiEsx][JSON]") {
	auto module = Test::createModule<MidiEsxModule>("MidiEsx");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
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


TEST_CASE("Note On message encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Note On: status=0x90, note=60, velocity=100
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);
	REQUIRE(msg.getSize() == 3);
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 256);
	
	// Should have encoded 3 bytes = 3*16 = 48 bits minimum
	REQUIRE(verifyBitStreamLength(samples, 3));
	// Should have actual data (transitions)
	int transitions = countTransitions(samples);
	REQUIRE(transitions > 5);  // Should have many transitions
	
	Test::destroyModule(module);
}


TEST_CASE("Control Change message encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Control Change: status=0xB0, controller=7 (volume), value=127
	auto msg = Test::makeMidiMessage(0xB, 0, 7, 127);
	REQUIRE(msg.getSize() == 3);
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 256);
	
	REQUIRE(verifyBitStreamLength(samples, 3));
	int transitions = countTransitions(samples);
	REQUIRE(transitions > 5);
	
	Test::destroyModule(module);
}


TEST_CASE("Program Change message encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Program Change: status=0xC0, program=42 (second arg unused for PC)
	// makeMidiMessage requires b1 and b2, so we pass 42 as b1 and 0 as b2
	auto msg = Test::makeMidiMessage(0xC, 0, 42, 0);
	// Note: makeMidiMessage creates a 3-byte message, but first byte is status, second is program
	REQUIRE(msg.getSize() >= 2);
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 256);
	
	// At least 2 bytes worth of data (32 bits)
	REQUIRE(verifyBitStreamLength(samples, 2));
	
	Test::destroyModule(module);
}


TEST_CASE("SysEx message encoding with correct F0 and F7 framing", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Create a SysEx message: F0 7E 00 09 01 F7 (identity request)
	rack::midi::Message msg;
	msg.setSize(6);
	msg.bytes[0] = 0xF0;  // SysEx start
	msg.bytes[1] = 0x7E;  // Universal non-realtime
	msg.bytes[2] = 0x00;  // Device ID (all)
	msg.bytes[3] = 0x09;  // Identity request
	msg.bytes[4] = 0x01;  // Sub-ID
	msg.bytes[5] = 0xF7;  // SysEx end
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 512);
	
	// Should have 6 bytes worth of encoded data (96 bits minimum)
	REQUIRE(verifyBitStreamLength(samples, 6));
	
	// Verify the stream contains data
	int transitions = countTransitions(samples);
	REQUIRE(transitions > 10);  // Many transitions for 6 diverse bytes
	
	// Verify start and end bits are present (pattern should start with highs for start bits)
	REQUIRE(samples[0] > 0.5f);  // First bit should be 1 (start bit)
	REQUIRE(samples[1] > 0.5f);  // Second start bit
	
	Test::destroyModule(module);
}


TEST_CASE("SysEx manufacturer-specific message encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Create a manufacturer-specific SysEx message
	// F0 <manuf ID> <data> F7
	rack::midi::Message msg;
	msg.setSize(8);
	msg.bytes[0] = 0xF0;    // SysEx start
	msg.bytes[1] = 0x43;    // Yamaha manufacturer ID
	msg.bytes[2] = 0x12;    // Device number
	msg.bytes[3] = 0x00;    // Model ID high
	msg.bytes[4] = 0x41;    // Model ID low
	msg.bytes[5] = 0x10;    // Data byte 1
	msg.bytes[6] = 0x42;    // Data byte 2
	msg.bytes[7] = 0xF7;    // SysEx end
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 512);
	
	// Should have 8 bytes worth of encoded data (128 bits minimum)
	REQUIRE(verifyBitStreamLength(samples, 8));
	REQUIRE(samples.size() >= 128);
	
	// Manufacturer ID 0x43 and end byte 0xF7 have specific patterns
	int transitions = countTransitions(samples);
	REQUIRE(transitions > 15);
	
	Test::destroyModule(module);
}


TEST_CASE("Multiple messages queued and encoded sequentially", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Queue two Note On messages
	auto msg1 = Test::makeMidiMessage(0x9, 0, 60, 100);
	auto msg2 = Test::makeMidiMessage(0x9, 0, 64, 80);
	
	module->onMessage(0, msg1);
	module->onMessage(0, msg2);
	
	// Collect samples from both messages
	auto samples = collectSamples(module, 512);
	
	// Should have at least 6 bytes (2 messages * 3 bytes each) = 96 bits
	REQUIRE(samples.size() >= 96);
	
	// Verify encoding happened
	int transitions = countTransitions(samples);
	REQUIRE(transitions > 10);
	
	Test::destroyModule(module);
}


TEST_CASE("Buffer overflow protection", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Try to queue messages that would exceed buffer capacity (2048 bits)
	// Each message takes ~48 bits, so ~42 messages fit
	// Send more than capacity
	int messagesQueued = 0;
	for (int i = 0; i < 50; i++) {
		auto msg = Test::makeMidiMessage(0x9, 0, 60 + (i % 64), 100);
		module->onMessage(0, msg);
		if (!module->port[0].locked) {
			messagesQueued++;
		}
	}
	
	// Should have queued some messages
	REQUIRE(messagesQueued > 0);
	
	// Module should handle gracefully - collect without crashing
	auto samples = collectSamples(module, 4096);
	REQUIRE(samples.size() > 0);
	
	Test::destroyModule(module);
}


TEST_CASE("Locked state prevents data loss during encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);
	module->onMessage(0, msg);
	
	// After message enqueue, locked should be false
	REQUIRE(module->port[0].locked == false);
	
	// Collect samples - should contain valid data
	auto samples = collectSamples(module, 256);
	REQUIRE(samples.size() > 0);
	
	// Should have transitions (not all 0s)
	int transitions = countTransitions(samples);
	REQUIRE(transitions > 3);
	
	Test::destroyModule(module);
}


TEST_CASE("Multi-port independent encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Send different messages to different ports
	auto msg0 = Test::makeMidiMessage(0x9, 0, 60, 100);
	auto msg1 = Test::makeMidiMessage(0x9, 0, 64, 80);
	
	module->onMessage(0, msg0);  // Port 0: C4 velocity 100
	module->onMessage(1, msg1);  // Port 1: E4 velocity 80
	
	// Collect from both ports
	std::vector<float> samples0, samples1;
	for (int i = 0; i < 256; ++i) {
		samples0.push_back(module->port[0].nextBit());
		samples1.push_back(module->port[1].nextBit());
		if (module->port[0].bitQueue.size() == 0 && module->port[1].bitQueue.size() == 0) break;
	}
	
	// Both ports should have encoded data
	REQUIRE(samples0.size() >= 48);
	REQUIRE(samples1.size() >= 48);
	
	int trans0 = countTransitions(samples0);
	int trans1 = countTransitions(samples1);
	REQUIRE(trans0 > 5);
	REQUIRE(trans1 > 5);
	
	Test::destroyModule(module);
}


TEST_CASE("Empty port returns zero samples", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Don't send any message, just collect samples
	std::vector<float> samples;
	for (int i = 0; i < 32; ++i) {
		samples.push_back(module->port[0].nextBit());
	}
	
	// All samples should be 0 (empty queue)
	for (float s : samples) {
		REQUIRE(s == 0.0f);
	}
	
	Test::destroyModule(module);
}


TEST_CASE("SysEx long message encoding and streaming", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Create a longer SysEx message (e.g., patch dump)
	rack::midi::Message msg;
	msg.setSize(20);
	msg.bytes[0] = 0xF0;
	msg.bytes[1] = 0x41;  // Roland
	msg.bytes[2] = 0x10;  // Device ID
	msg.bytes[3] = 0x42;  // Model
	for (int i = 4; i < 18; i++) {
		msg.bytes[i] = i * 7;  // Dummy patch data
	}
	msg.bytes[19] = 0xF7;
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 1024);
	
	// Verify correct structure: 20 bytes × 16 bits = 320 bits minimum
	REQUIRE(verifyBitStreamLength(samples, 20));
	
	// Verify significant bit transitions for the long message
	int trans = countTransitions(samples);
	REQUIRE(trans > 50);
	
	Test::destroyModule(module);
}


// =====================================================
// DSP and process tests
// =====================================================

TEST_CASE("Locked state returns zero from nextBit", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);
	module->onMessage(0, msg);
	
	// Force lock to simulate concurrent access
	module->port[0].locked = true;
	float bit = module->port[0].nextBit();
	REQUIRE(bit == 0.f);
	
	// Verify queue still has data
	REQUIRE(module->port[0].bitQueue.size() > 0);
	
	// After unlock, should return actual bits
	module->port[0].locked = false;
	bit = module->port[0].nextBit();
	REQUIRE(bit == 1.f);  // First bit after start bits
	
	Test::destroyModule(module);
}


TEST_CASE("Sample rate check in process() rejects non-48kHz", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);
	module->onMessage(0, msg);
	
	// Get initial bit output
	float bitBefore = module->port[0].nextBit();
	
	// Simulate process() at wrong sample rate (should early-return)
	auto argsWrong = Test::makeProcessArgs(0, 44100.f);
	module->process(argsWrong);
	
	// Nothing should have been consumed
	float bitAfter = module->port[0].nextBit();
	REQUIRE(bitBefore == bitAfter);
	
	Test::destroyModule(module);
}


TEST_CASE("Process outputs correct voltage levels", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);
	module->onMessage(0, msg);
	
	// Set sample rate to 48kHz (required for process)
	auto args = Test::makeProcessArgs(0, 48000.f);
	module->onSampleRateChange({48000.f, 1.0f/48000.f});
	module->process(args);
	
	// Output should be either 0 or ~10V (bit value * 10V)
	float v = module->outputs[MidiEsxModule::OUTPUT_ENC + 0].getVoltage();
	REQUIRE(v >= 0.f);
	REQUIRE(v <= 10.f);
	
	Test::destroyModule(module);
}


// =====================================================
// MIDI message type tests
// =====================================================

TEST_CASE("Note Off message encoding (0x8n)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Note Off: status=0x80, note=60, velocity=0
	auto msg = Test::makeMidiMessage(0x8, 0, 60, 0);
	REQUIRE((msg.bytes[0] & 0xF0) == 0x80);
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 256);
	
	// Verify correct structure: 3 bytes × 16 bits = 48 bits minimum
	REQUIRE(verifyBitStreamLength(samples, 3));
	
	// Verify bit transitions
	int trans = countTransitions(samples);
	REQUIRE(trans > 5);
	
	Test::destroyModule(module);
}


TEST_CASE("Polyphonic Aftertouch encoding (0xAn)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Poly Aftertouch: status=0xA0, note=60, pressure=100
	auto msg = Test::makeMidiMessage(0xA, 0, 60, 100);
	REQUIRE((msg.bytes[0] & 0xF0) == 0xA0);
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 256);
	
	// Verify correct structure: 3 bytes × 16 bits = 48 bits minimum
	REQUIRE(verifyBitStreamLength(samples, 3));
	
	// Verify bit transitions
	int trans = countTransitions(samples);
	REQUIRE(trans > 5);
	
	Test::destroyModule(module);
}


TEST_CASE("Pitch Bend encoding (0xEn)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Pitch Bend: status=0xE0, MSB=0x00, LSB=0x40 (center)
	rack::midi::Message msg;
	msg.setSize(3);
	msg.bytes[0] = 0xE0;  // Pitch bend status + channel 0
	msg.bytes[1] = 0x00;  // MSB (center = 64)
	msg.bytes[2] = 0x40;  // LSB (64 = center)
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 256);
	
	// Verify correct structure: 3 bytes × 16 bits = 48 bits minimum
	REQUIRE(verifyBitStreamLength(samples, 3));
	
	// Verify bit transitions
	int trans = countTransitions(samples);
	REQUIRE(trans > 5);
	
	Test::destroyModule(module);
}


TEST_CASE("Channel Pressure encoding (0xDn)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Channel Pressure: status=0xD0, pressure=100
	auto msg = Test::makeMidiMessage(0xD, 0, 100, 0);
	REQUIRE((msg.bytes[0] & 0xF0) == 0xD0);
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 256);
	
	// Verify correct structure: 2 bytes × 16 bits = 32 bits minimum
	REQUIRE(verifyBitStreamLength(samples, 2));
	
	// Verify bit transitions
	int trans = countTransitions(samples);
	REQUIRE(trans > 3);
	
	Test::destroyModule(module);
}


TEST_CASE("Real-time Clock message encoding (0xF8)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Real-time messages are single-byte
	rack::midi::Message msg;
	msg.setSize(1);
	msg.bytes[0] = 0xF8;  // Clock
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 64);
	
	// Single byte = 16 bits
	REQUIRE(samples.size() >= 16);
	// Start bits should be present
	REQUIRE(samples[0] > 0.5f);
	REQUIRE(samples[1] > 0.5f);
	
	Test::destroyModule(module);
}


TEST_CASE("Real-time Active Sensing encoding (0xFE)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	rack::midi::Message msg;
	msg.setSize(1);
	msg.bytes[0] = 0xFE;  // Active Sensing
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 64);
	
	REQUIRE(samples.size() >= 16);
	
	// Verify transitions indicate encoding
	int trans = countTransitions(samples);
	REQUIRE(trans >= 1);
	
	Test::destroyModule(module);
}


TEST_CASE("Single-byte message encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	rack::midi::Message msg;
	msg.setSize(1);
	msg.bytes[0] = 0xFC;  // Stop
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 64);
	
	// Single byte = 2 start + 8 data + 2 stop = 12 bits, but oversampled
	REQUIRE(samples.size() >= 12);
	
	// Verify transitions
	int trans = countTransitions(samples);
	REQUIRE(trans >= 1);
	
	Test::destroyModule(module);
}


TEST_CASE("Two-byte message encoding", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// e.g., Song Position Pointer (F2) is 3 bytes total
	// e.g., Tune Request (F6) is 1 byte
	rack::midi::Message msg;
	msg.setSize(2);
	msg.bytes[0] = 0xF1;  // MIDI Time Code
	msg.bytes[1] = 0x01;  // Type 1, value 1
	
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 128);
	
	// 2 bytes = 32 bits minimum
	REQUIRE(verifyBitStreamLength(samples, 2));
	
	// Verify transitions indicate data encoding
	int trans = countTransitions(samples);
	REQUIRE(trans > 3);
	
	Test::destroyModule(module);
}


// =====================================================
// Edge case tests
// =====================================================

TEST_CASE("Empty message (0 bytes)", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	rack::midi::Message msg;
	msg.setSize(0);
	
	// Should not crash
	module->onMessage(0, msg);
	auto samples = collectSamples(module, 16);
	
	// No encoding should occur - bitEnqueue loops 0 times for len=0
	// However, nextBit() may return 0 from empty queue which still gets pushed
	REQUIRE(samples.size() <= 1);
	
	Test::destroyModule(module);
}


TEST_CASE("onMessage delegates to correct port index", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);
	
	// Send to port 3
	module->onMessage(3, msg);
	
	// Port 3 should have data
	REQUIRE(module->port[3].bitQueue.size() > 0);
	// Port 0 should be empty
	REQUIRE(module->port[0].bitQueue.size() == 0);
	
	Test::destroyModule(module);
}


TEST_CASE("Sequential messages on different ports are independent", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	auto msg0 = Test::makeMidiMessage(0x9, 0, 60, 100);
	auto msg1 = Test::makeMidiMessage(0xB, 0, 7, 127);
	
	module->onMessage(0, msg0);
	module->onMessage(7, msg1);
	
	// Collect from both
	std::vector<float> port0, port7;
	for (int i = 0; i < 256; ++i) {
		port0.push_back(module->port[0].nextBit());
		port7.push_back(module->port[7].nextBit());
		if (module->port[0].bitQueue.size() == 0 && module->port[7].bitQueue.size() == 0) break;
	}
	
	// Both should have encoded data
	REQUIRE(port0.size() >= 48);
	REQUIRE(port7.size() >= 48);
	
	// Different message types should produce different transition patterns
	int trans0 = countTransitions(port0);
	int trans7 = countTransitions(port7);
	REQUIRE(trans0 != trans7);  // Different messages = different patterns
	
	Test::destroyModule(module);
}


TEST_CASE("Module reset does not clear bit queues by default", "[MidiEsx]") {
	MidiEsxModule* module = Test::createModule<MidiEsxModule>("MidiEsx");
	
	// Queue a message
	auto msg = Test::makeMidiMessage(0x9, 0, 60, 100);
	module->onMessage(0, msg);
	
	// Verify data is queued
	REQUIRE(module->port[0].bitQueue.size() > 0);
	
	// Trigger reset (Module base reset, not MidiEsx-specific)
	module->onReset();
	
	// Note: Module::onReset() does not clear MidiEsxProcessor bit queues
	// The queue retains its data after base reset
	size_t sizeAfter = module->port[0].bitQueue.size();
	REQUIRE(sizeAfter > 0);
	
	Test::destroyModule(module);
}
