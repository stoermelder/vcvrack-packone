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
