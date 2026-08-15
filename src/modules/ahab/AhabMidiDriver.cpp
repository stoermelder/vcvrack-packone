#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Ahab {
namespace Midi {

const int AHAB_PORT_NUM = 4;
const int AHAB_DRIVER_ID = 0x4CCC434C;

struct AhabMidiInputDevice : rack::midi::InputDevice {
	int deviceId;

	std::string getName() override {
		return string::f("Port %i", deviceId + 1);
	}

	// Allow programmatic injection of messages into this virtual port
	void injectMessage(const rack::midi::Message& message) {
		onMessage(message);
	}
};

struct AhabMidiDriver : rack::midi::Driver {
	AhabMidiInputDevice devices[AHAB_PORT_NUM];

	AhabMidiDriver() {
		for (int i = 0; i < AHAB_PORT_NUM; i++)
			devices[i].deviceId = i;
	}

	std::string getName() override {
		return "AHAB Virtual";
	}

	std::vector<int> getInputDeviceIds() override {
		std::vector<int> ids;
		for (int i = 0; i < AHAB_PORT_NUM; ++i)
			ids.push_back(i);
		return ids;
	}

	int getDefaultInputDeviceId() override { 
		return 0;
	}

	std::string getInputDeviceName(int deviceId) override {
		if (deviceId >= 0 && deviceId < AHAB_PORT_NUM)
			return string::f("Port %i", deviceId + 1);
		return "";
	}

	rack::midi::InputDevice* subscribeInput(int deviceId, rack::midi::Input* input) override {
		auto* dev = &devices[deviceId];
		dev->InputDevice::subscribe(input);
		return dev;
	}

	void unsubscribeInput(int deviceId, rack::midi::Input* input) override {
		auto* dev = &devices[deviceId];
		dev->InputDevice::unsubscribe(input);
	}
};

static AhabMidiDriver* midiDriver = NULL;

void init() {
	if (midiDriver) return;
	if (pluginSettings.ahabMidiVirtualEnabled) {
		midiDriver = new AhabMidiDriver();
		rack::midi::addDriver(AHAB_DRIVER_ID, midiDriver);
	}
}

bool isLoaded() { 
	return midiDriver != NULL; 
}

int numPorts() { 
	return AHAB_PORT_NUM;
}

// Reset virtual ports by sending All Notes Off / All Sound Off / Reset Controllers
// to all channels so connected inputs are put into a known state.
void reset(int deviceId) {
	if (!midiDriver) return;
	if (deviceId < 0 || deviceId >= AHAB_PORT_NUM) return;
	for (int ch = 0; ch < 16; ++ch) {
		for (int note = 0; note <= 127; note++) {
			// Note off
			midi::Message m;
			m.setStatus(0x8);
			m.setChannel(ch);
			m.setNote(note);
			m.setValue(0);
			m.setFrame(APP->engine->getFrame());
			midiDriver->devices[deviceId].injectMessage(m);
		}
	}
}

// Programmatic API: inject a MIDI message into a virtual port so subscribed
// inputs receive it.
void sendToPort(int deviceId, const rack::midi::Message& m) {
	if (!midiDriver) return;
	if (deviceId < 0 || deviceId >= AHAB_PORT_NUM) return;
	midiDriver->devices[deviceId].injectMessage(m);
}

} // namespace Midi
} // namespace Ahab
} // namespace StoermelderPackOne
