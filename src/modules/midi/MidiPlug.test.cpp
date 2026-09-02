#include "../../test/framework.hpp"
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


TEST_CASE("Construction and reset", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");

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

}


TEST_CASE("Preset JSON null-guards", "[MidiPlug][JSON]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

}

TEST_CASE("JSON round-trip preserves state", "[MidiPlug][JSON]") {
	Test::ModuleScaffold<Module2> mods;
	Module2* m = mods.create("MidiPlug");
	Module2* m2 = mods.create("MidiPlug");

	SECTION("Scalar settings round-trip") {
		// Non-default value (default is pluginSettings.panelThemeDefault, usually 0)
		m->panelTheme = 1;

		json_t* j = m->dataToJson();
		// Start m2 at a different value so dataFromJson() is genuinely exercised
		m2->panelTheme = 0;
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
	}

	SECTION("MIDI outputs (midiOutput array) round-trip") {
		// Distinctive per-output channel and plug mode
		m->midiOutput[0].channel = 7;
		m->midiOutput[0].plugMode = MODE::FILTER;
		m->midiOutput[1].channel = 2;
		m->midiOutput[1].plugMode = MODE::BLOCK;

		json_t* j = m->dataToJson();
		// The midiOutput array must be serialized with one entry per output
		json_t* midiOutputJ = json_object_get(j, "midiOutput");
		REQUIRE(midiOutputJ != nullptr);
		REQUIRE(json_array_size(midiOutputJ) == (size_t) 2);
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->midiOutput[0].channel == 7);
		REQUIRE(m2->midiOutput[0].plugMode == MODE::FILTER);
		REQUIRE(m2->midiOutput[1].channel == 2);
		REQUIRE(m2->midiOutput[1].plugMode == MODE::BLOCK);
	}

	SECTION("MIDI inputs (midiInput array) round-trip") {
		// Distinctive per-input channel (the midiInput array was previously untested)
		m->midiInput[0].channel = 5;
		m->midiInput[1].channel = 3;

		json_t* j = m->dataToJson();
		// The midiInput array must be serialized with one entry per input
		json_t* midiInputJ = json_object_get(j, "midiInput");
		REQUIRE(midiInputJ != nullptr);
		REQUIRE(json_array_size(midiInputJ) == (size_t) 2);
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->midiInput[0].channel == 5);
		REQUIRE(m2->midiInput[1].channel == 3);
	}

}


TEST_CASE("Routes both inputs to both outputs", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");
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
}


TEST_CASE("Thru passes messages unchanged", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");
	CaptureDevice* out = attachCapture(module, 0);
	module->midiOutput[0].channel = -1;

	feed(module, 0, Test::makeMidiMessage(0xb, 9, 1, 127));
	REQUIRE(out->sent.size() == 1);
	REQUIRE(out->sent[0].getChannel() == 9);

	delete out;
}


TEST_CASE("REPLACE rewrites channel", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");
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
}


TEST_CASE("FILTER keeps only the target channel", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");
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
}


TEST_CASE("BLOCK drops the target channel", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");
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
}


TEST_CASE("FILTER/BLOCK ignore system messages", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");
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
}


TEST_CASE("processBypass drains both input queues without sending output", "[MidiPlug]") {
	Test::ModuleScaffold<Module2> mods;
	auto module = mods.create("MidiPlug");
	CaptureDevice* out0 = attachCapture(module, 0);
	CaptureDevice* out1 = attachCapture(module, 1);

	module->midiInput[0].onMessage(Test::makeMidiMessage(0xb, 2, 7, 64));
	module->midiInput[1].onMessage(Test::makeMidiMessage(0x9, 4, 60, 100));

	Module::ProcessArgs args = Test::makeProcessArgs(1);
	module->processBypass(args);

	REQUIRE(out0->sent.empty());
	REQUIRE(out1->sent.empty());

	// Queues are drained; a subsequent normal process() call has nothing left to forward.
	module->process(Test::makeProcessArgs(2));
	REQUIRE(out0->sent.empty());
	REQUIRE(out1->sent.empty());

	delete out0;
	delete out1;
}