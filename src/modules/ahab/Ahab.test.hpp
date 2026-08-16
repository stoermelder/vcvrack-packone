#pragma once

// Brings Ahab.cpp (AhabModule / AhabWidget / modelAhab) into the test TU
// exactly once. The module-side test headers include this so they are
// self-contained (IntelliSense-clean) without risking a double definition of
// AhabModule when several headers are included by Ahab.test.cpp.

#ifndef STOERMELDER_PACKONE_AHAB_TEST_MODULE_CPP_INCLUDED
#define STOERMELDER_PACKONE_AHAB_TEST_MODULE_CPP_INCLUDED
#include "Ahab.cpp"
#endif

#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Ahab.hpp"
#include "AhabSim.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Ahab;


// Mock MIDI devices used by the Ahab MIDI tests (AhabMidi.test.hpp) and by the
// preset integration test (AhabModule.test.hpp). Included after Ahab.cpp so
// AhabModule is fully defined (setupMockMidiOutput reaches into midiOutPort).

// Mock MIDI OutputDevice for testing MIDI output messages
struct MockMidiOutputDevice : public rack::midi::OutputDevice {
	std::vector<rack::midi::Message> sentMessages;
	
	void sendMessage(const rack::midi::Message& message) override {
		sentMessages.push_back(message);
	}
	
	void clear() {
		sentMessages.clear();
	}
	
	size_t getMessageCount() const {
		return sentMessages.size();
	}
	
	const rack::midi::Message& getMessage(size_t index) const {
		return sentMessages.at(index);
	}
};

// Helper to replace midiOutPort's device with our mock
static MockMidiOutputDevice* setupMockMidiOutput(AhabModule* m) {
	MockMidiOutputDevice* mockDevice = new MockMidiOutputDevice();
	// Access the private device pointer through the base Port class
	m->midiOutPort.outputDevice = mockDevice;
	m->midiOutPort.device = mockDevice;
	m->midiOutPort.channel = -1; // Don't override message channel
	return mockDevice;
}

// Static singleton to use as safe fallback after mock device cleanup
static void cleanupMockMidiOutput(AhabModule* m, MockMidiOutputDevice* mockDevice) {
	if (mockDevice && m) {
		// Reset the port first to clear internal state
		m->midiOutPort.reset();
		// Now delete the mock device
		delete mockDevice;
	}
}

// Mock MIDI Input for capturing virtual driver messages
struct MockMidiVirtualInput : public rack::midi::Input {
	std::vector<rack::midi::Message> receivedMessages;
	
	void onMessage(const rack::midi::Message& message) override {
		receivedMessages.push_back(message);
	}
	
	void clear() {
		receivedMessages.clear();
	}
	
	size_t getMessageCount() const {
		return receivedMessages.size();
	}
	
	const rack::midi::Message& getMessage(size_t index) const {
		return receivedMessages.at(index);
	}
	
	std::vector<int> getDeviceIds() override {
		return {0, 1, 2, 3};
	}
	
	int getDefaultDeviceId() override {
		return 0;
	}
	
	std::string getDeviceName(int deviceId) override {
		return string::f("Virtual Port %i", deviceId + 1);
	}
	
	std::vector<int> getChannels() override {
		return {-1}; // All channels
	}
};

// Helper to setup mock virtual MIDI input
static MockMidiVirtualInput* setupMockVirtualMidiInput(int portId) {
	MockMidiVirtualInput* mockInput = new MockMidiVirtualInput();
	mockInput->setDriverId(0x4CCC434C); // Ahab virtual MIDI driver ID
	mockInput->setDeviceId(portId);
	// Subscribe to the Ahab MIDI driver so messages are actually delivered
	if (auto* driver = rack::midi::getDriver(0x4CCC434C)) {
		driver->subscribeInput(portId, mockInput);
	}
	return mockInput;
}

// Helper to cleanup mock virtual MIDI input
static void cleanupMockVirtualMidiInput(MockMidiVirtualInput* mockInput) {
	if (mockInput) {
		// Unsubscribe from the driver before deleting
		if (auto* driver = rack::midi::getDriver(0x4CCC434C)) {
			driver->unsubscribeInput(mockInput->getDeviceId(), mockInput);
		}
		delete mockInput;
	}
}

// Enqueue one sim step and drain it through the module's process() (the same
// path the UI "step" command uses). One drain == one VM tick. Shared by the
// end-to-end MIDI and CV operator tests.
static void stepSim(AhabModule* m) {
	m->sim->stepRequest();
	m->process({});
}


// Recording fake for AhabUdpOutput — records every send/set call so the module
// can be tested for UDP/OSC event routing without any sockets. The module owns
// the fake (std::unique_ptr), so tests allocate it with `new` and let
// Test::destroyModule(m) delete it.
struct RecordingUdpOutput : AhabUdpOutput {
	// sendUdpDatagram payloads (raw bytes)
	std::vector<std::vector<uint8_t>> udpDatagrams;
	// sendOscInts calls (OSC path + int payload)
	std::vector<std::pair<std::string, std::vector<I32>>> oscInts;
	// setUdpDestination / setOscDestination calls (for the config tests)
	std::vector<std::pair<std::string, std::string>> udpDestinations;
	std::vector<std::pair<std::string, std::string>> oscDestinations;

	// Destination state backing the getters (mirrors the real class defaults).
	std::string udpAddress = "127.0.0.1";
	std::string udpPort = "49161";
	std::string oscAddress = "127.0.0.1";
	std::string oscPort = "49162";

	void setUdpDestination(const std::string& address, const std::string& port) override {
		udpDestinations.emplace_back(address, port);
		udpAddress = address;
		udpPort = port;
	}
	std::string getUdpAddress() const override { return udpAddress; }
	std::string getUdpPort() const override { return udpPort; }

	void setOscDestination(const std::string& address, const std::string& port) override {
		oscDestinations.emplace_back(address, port);
		oscAddress = address;
		oscPort = port;
	}
	std::string getOscAddress() const override { return oscAddress; }
	std::string getOscPort() const override { return oscPort; }

	void sendUdpDatagram(const char* data, Usz size) override {
		udpDatagrams.emplace_back(data, data + size);
	}
	void sendOscInts(const char* osc_path, I32 const* vals, Usz count) override {
		std::vector<I32> v;
		if (vals && count > 0) v.assign(vals, vals + count);
		oscInts.emplace_back(std::string(osc_path), std::move(v));
	}

	void toJson(json_t* simJ) const override {
		json_object_set_new(simJ, "udpAddress", json_string(udpAddress.c_str()));
		json_object_set_new(simJ, "udpPort", json_string(udpPort.c_str()));
		json_object_set_new(simJ, "oscAddress", json_string(oscAddress.c_str()));
		json_object_set_new(simJ, "oscPort", json_string(oscPort.c_str()));
	}
	void fromJson(json_t* simJ) override {
		json_t* a = json_object_get(simJ, "udpAddress");
		if (a && json_is_string(a)) udpAddress = json_string_value(a);
		json_t* p = json_object_get(simJ, "udpPort");
		if (p && json_is_string(p)) udpPort = json_string_value(p);
		json_t* oa = json_object_get(simJ, "oscAddress");
		if (oa && json_is_string(oa)) oscAddress = json_string_value(oa);
		json_t* op = json_object_get(simJ, "oscPort");
		if (op && json_is_string(op)) oscPort = json_string_value(op);
	}
};

// Construct an AhabModule with an injected UDP output (e.g. a
// RecordingUdpOutput fake). Test::createModule uses the dylib factory, which
// cannot inject, so injection tests build the module directly and mirror its
// post-construction setup (id + onSampleRateChange).
static AhabModule* createModuleWithUdp(AhabUdpOutput* udpOutput) {
	AhabModule* m = new AhabModule(udpOutput);
	m->id = Test::getModuleId();
	Module::SampleRateChangeEvent e;
	e.sampleRate = APP->engine->getSampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);
	return m;
}
