#pragma once

// Shared preamble for the MIDIPLUG test suite.
// Included by MidiPlug.test.cpp, which pulls the test cases in from MidiPlug.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiPlug.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiPlug;

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

// Push a message into input i and run a single dsp step so it is popped.
static void feed(Test::Harness& h, Module2* module, int i, const rack::midi::Message& msg) {
	module->midiInput[i].onMessage(msg);
	h.dspStep();
}