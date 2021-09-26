#include "../plugin.hpp"

namespace StoermelderPackOne {
namespace MidiLoopback {

const int LOOPBACK_DEVICE_NUM = 4;
const int LOOPBACK_DRIVER_ID = 80627554;


struct LoopbackDevice : rack::midi::OutputDevice, rack::midi::InputDevice {
	void sendMessage(const midi::Message& message) override {
		onMessage(message);
	};
};

struct LoopbackDriver : midi::Driver {
	LoopbackDevice devices[LOOPBACK_DEVICE_NUM];

	std::string getName() override {
		return "Loopback";
	}

	std::vector<int> getInputDeviceIds() override {
		std::vector<int> deviceIds;
		for (int i = 0; i < LOOPBACK_DEVICE_NUM; i++) {
			deviceIds.push_back(i);
		}
		return deviceIds;
	}

	std::string getInputDeviceName(int deviceId) override {
		if (deviceId >= 0) {
			return string::f("Port %i", deviceId + 1);
		}
		return "";
	}

	midi::InputDevice* subscribeInput(int deviceId, midi::Input* input) override {
		midi::InputDevice* device = &devices[deviceId];
		device->subscribe(input);
		return device;
	}

	void unsubscribeInput(int deviceId, midi::Input* input) override {
		midi::InputDevice* device = &devices[deviceId];
		device->unsubscribe(input);
	}

	std::vector<int> getOutputDeviceIds() override {
		std::vector<int> deviceIds;
		for (int i = 0; i < LOOPBACK_DEVICE_NUM; i++) {
			deviceIds.push_back(i);
		}
		return deviceIds;
	}

	std::string getOutputDeviceName(int deviceId) override {
		if (deviceId >= 0) {
			return string::f("Port %i", deviceId + 1);
		}
		return "";
	}

	midi::OutputDevice* subscribeOutput(int deviceId, midi::Output* output) override {
		midi::OutputDevice* device = &devices[deviceId];
		device->subscribe(output);
		return device;
	}

	void unsubscribeOutput(int deviceId, midi::Output* output) override {
		midi::OutputDevice* device = &devices[deviceId];
		device->unsubscribe(output);
	}
};


LoopbackDriver* midiDriver = NULL;

void init() {
	int driverId = LOOPBACK_DRIVER_ID;
	midiDriver = new LoopbackDriver();
	midi::addDriver(driverId, midiDriver);
}

bool isLoaded() {
	return midiDriver != NULL;
}

} // namespace MidiLoopback
} // namespace StoermelderPackOne