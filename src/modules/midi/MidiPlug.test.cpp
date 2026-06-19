#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiPlug.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiPlug;

SYNC_MODEL(modelMidiPlug, "MidiPlug");
static Test::TestContext<> testContext;

typedef MidiPlugModule<> Module2;
typedef Module2::MidiPlugOutput::MODE MODE;

// Mock MIDI output device that records every message sent to it.
struct CaptureDevice : rack::midi::OutputDevice {
	std::vector<rack::midi::Message> sent;
	void sendMessage(const rack::midi::Message& message) override {
		sent.push_back(message);
	}
};

// Attach a fresh capture device to output j and return it.
static CaptureDevice* attachCapture(Module2* module, int j) {
	CaptureDevice* dev = new CaptureDevice();
	module->midiOutput[j].outputDevice = dev;
	return dev;
}

// Push a message into input i and run a single process() step so it is popped.
static void feed(Module2* module, int i, const rack::midi::Message& msg, int64_t frame = 1) {
	module->midiInput[i].onMessage(msg);
	module->process(Test::makeProcessArgs(frame));
}


TEST_CASE("MidiPlug construction and reset", "[MidiPlug]") {
	auto module = Test::createModule<Module2>("MidiPlug");

	SECTION("Outputs default to Thru / Replace") {
		for (int j = 0; j < 2; j++) {
			REQUIRE(module->midiOutput[j].channel == -1);
			REQUIRE(module->midiOutput[j].plugMode == MODE::REPLACE);
		}
	}

	SECTION("onReset restores defaults") {
		module->midiOutput[0].channel = 5;
		module->midiOutput[0].plugMode = MODE::BLOCK;
		Module::ResetEvent re;
		module->onReset(re);
		REQUIRE(module->midiOutput[0].channel == -1);
		REQUIRE(module->midiOutput[0].plugMode == MODE::REPLACE);
	}

	SECTION("getChannels prepends Thru (-1)") {
		std::vector<int> ch = module->midiOutput[0].getChannels();
		REQUIRE(!ch.empty());
		REQUIRE(ch.front() == -1);
	}

	Test::destroyModule(module);
}


TEST_CASE("Preset JSON null-guards", "[MidiPlug][JSON]") {
	auto module = Test::createModule<Module2>("MidiPlug");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


TEST_CASE("MidiPlug routes both inputs to both outputs", "[MidiPlug]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	CaptureDevice* out0 = attachCapture(module, 0);
	CaptureDevice* out1 = attachCapture(module, 1);

	// Outputs stay in Thru mode (channel == -1): messages pass unchanged.
	feed(module, 0, Test::makeMidiMessage(0xb, 2, 7, 64));
	feed(module, 1, Test::makeMidiMessage(0x9, 4, 60, 100));

	REQUIRE(out0->sent.size() == 2);
	REQUIRE(out1->sent.size() == 2);
	// First message kept its original channel 2.
	REQUIRE(out0->sent[0].getChannel() == 2);
	REQUIRE(out0->sent[0].getStatus() == 0xb);

	delete out0;
	delete out1;
	Test::destroyModule(module);
}


TEST_CASE("MidiPlug Thru passes messages unchanged", "[MidiPlug]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	CaptureDevice* out = attachCapture(module, 0);
	module->midiOutput[0].channel = -1;

	feed(module, 0, Test::makeMidiMessage(0xb, 9, 1, 127));
	REQUIRE(out->sent.size() == 1);
	REQUIRE(out->sent[0].getChannel() == 9);

	delete out;
	Test::destroyModule(module);
}


TEST_CASE("MidiPlug REPLACE rewrites channel", "[MidiPlug]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	CaptureDevice* out = attachCapture(module, 0);
	module->midiOutput[0].channel = 5;
	module->midiOutput[0].plugMode = MODE::REPLACE;

	SECTION("Channel-voice messages are rewritten to the target channel") {
		// status, original channel 2 -> should become channel 5
		uint8_t statuses[] = { 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe };
		int expected = 0;
		for (uint8_t st : statuses) {
			feed(module, 0, Test::makeMidiMessage(st, 2, 40, 80), expected + 1);
			REQUIRE(out->sent.back().getChannel() == 5);
			REQUIRE(out->sent.back().getStatus() == st);
			expected++;
		}
		REQUIRE((int)out->sent.size() == expected);
	}

	SECTION("System messages (0xf) are not rewritten") {
		rack::midi::Message sys = Test::makeMidiMessage(0xf, 0, 0, 0);
		sys.bytes[0] = 0xf8; // timing clock — low nibble must survive
		feed(module, 0, sys);
		REQUIRE(out->sent.size() == 1);
		REQUIRE(out->sent[0].bytes[0] == 0xf8);
	}

	delete out;
	Test::destroyModule(module);
}


TEST_CASE("MidiPlug FILTER keeps only the target channel", "[MidiPlug]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	CaptureDevice* out = attachCapture(module, 0);
	module->midiOutput[0].channel = 3;
	module->midiOutput[0].plugMode = MODE::FILTER;

	// Matching channel passes.
	feed(module, 0, Test::makeMidiMessage(0xb, 3, 7, 64), 1);
	// Non-matching channel is dropped.
	feed(module, 0, Test::makeMidiMessage(0xb, 4, 7, 64), 2);

	REQUIRE(out->sent.size() == 1);
	REQUIRE(out->sent[0].getChannel() == 3);

	delete out;
	Test::destroyModule(module);
}


TEST_CASE("MidiPlug BLOCK drops the target channel", "[MidiPlug]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	CaptureDevice* out = attachCapture(module, 0);
	module->midiOutput[0].channel = 3;
	module->midiOutput[0].plugMode = MODE::BLOCK;

	// Matching channel is dropped.
	feed(module, 0, Test::makeMidiMessage(0xb, 3, 7, 64), 1);
	// Non-matching channel passes unchanged.
	feed(module, 0, Test::makeMidiMessage(0xb, 4, 7, 64), 2);

	REQUIRE(out->sent.size() == 1);
	REQUIRE(out->sent[0].getChannel() == 4);

	delete out;
	Test::destroyModule(module);
}


TEST_CASE("MidiPlug FILTER/BLOCK ignore system messages", "[MidiPlug]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	CaptureDevice* out = attachCapture(module, 0);
	module->midiOutput[0].channel = 3;

	// System message (0xf) always passes, regardless of channel mode.
	for (MODE m : { MODE::FILTER, MODE::BLOCK }) {
		out->sent.clear();
		module->midiOutput[0].plugMode = m;
		rack::midi::Message sys = Test::makeMidiMessage(0xf, 0, 0, 0);
		sys.bytes[0] = 0xfa; // start
		feed(module, 0, sys);
		REQUIRE(out->sent.size() == 1);
		REQUIRE(out->sent[0].bytes[0] == 0xfa);
	}

	delete out;
	Test::destroyModule(module);
}


TEST_CASE("MidiPlug JSON round-trip", "[MidiPlug][JSON]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	module->panelTheme = 1;
	module->midiOutput[0].channel = 7;
	module->midiOutput[0].plugMode = MODE::FILTER;
	module->midiOutput[1].channel = 2;
	module->midiOutput[1].plugMode = MODE::BLOCK;

	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);

	auto restored = Test::createModule<Module2>("MidiPlug");
	restored->dataFromJson(rootJ);

	REQUIRE(restored->panelTheme == 1);
	REQUIRE(restored->midiOutput[0].channel == 7);
	REQUIRE(restored->midiOutput[0].plugMode == MODE::FILTER);
	REQUIRE(restored->midiOutput[1].channel == 2);
	REQUIRE(restored->midiOutput[1].plugMode == MODE::BLOCK);

	json_decref(rootJ);
	Test::destroyModule(module);
	Test::destroyModule(restored);
}


TEST_CASE("MidiPlug preset JSON null-guards", "[MidiPlug][JSON]") {
	auto module = Test::createModule<Module2>("MidiPlug");
	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);
	Test::testPresetNullGuards(module, rootJ);
	json_decref(rootJ);
	Test::destroyModule(module);
}
