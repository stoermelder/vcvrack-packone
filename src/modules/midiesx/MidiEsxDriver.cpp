#include "MidiEsx.hpp"

namespace StoermelderPackOne {
namespace MidiEsx {

const int MIDIESX_DRIVER_ID = 80627444;
const int MIDIESX_PORTGROUP_NUM = 4;
const int MIDIESX_PORTGROUP_SIZE = 8;


struct MidiEsxDriver : rack::midi::Driver {
	struct MidiEsxOutputDevice : rack::midi::OutputDevice {
		int deviceId;
		MidiEsxDriver* driver;

		MidiEsxOutputDevice(MidiEsxDriver* driver, int deviceId) {
			this->driver = driver;
			this->deviceId = deviceId;
		}

		std::string getName() override {
			return driver->getOutputDeviceName(deviceId);
		}

		void sendMessage(const rack::midi::Message& message) override {
			driver->process(deviceId, message);
		}
	};

	std::vector<std::vector<MidiEsxOutputDevice*>> devices{MIDIESX_PORTGROUP_NUM};
	std::vector<std::tuple<int, MidiEsxMessageHandler*>> subscribers;


	MidiEsxDriver() {
		for (int i = 0; i < MIDIESX_PORTGROUP_NUM; i++) {
			devices[i].resize(MIDIESX_PORTGROUP_SIZE, NULL);
		}
	}

	~MidiEsxDriver() {
		for (int i = 0; i < MIDIESX_PORTGROUP_NUM; i++) {
			for (int j = 0; j < MIDIESX_PORTGROUP_SIZE; j++) {
				if (devices[i][j] != NULL) {
					delete devices[i][j];
				}
			}
		}
	}

	std::string getName() override {
		return "MIDI-ESX";
	}

	void process(int deviceId, const rack::midi::Message& message) {
		int portGroup = deviceId / MIDIESX_PORTGROUP_SIZE;
		int portIndex = deviceId % MIDIESX_PORTGROUP_SIZE;
		for (auto& sub : subscribers) {
			if (std::get<0>(sub) == portGroup) {
				std::get<1>(sub)->onMessage(portIndex, message);
			}
		}
	}

	std::vector<int> getOutputDeviceIds() override {
		std::vector<int> ids;
		for (int i = 0; i < MIDIESX_PORTGROUP_NUM; i++) {
			for (int j = 0; j < MIDIESX_PORTGROUP_SIZE; j++) {
				ids.push_back(i * MIDIESX_PORTGROUP_SIZE + j);
			}
		}
		return ids;
	}

	int getDefaultOutputDeviceId() override {
		return 0;
	}

	std::string getOutputDeviceName(int deviceId) override {
		if (deviceId >= 0) {
			int portGroup = deviceId / MIDIESX_PORTGROUP_SIZE;
			int portIndex = deviceId % MIDIESX_PORTGROUP_SIZE;
			return rack::string::f("Port %c / %i", 65 + portGroup, portIndex + 1);
		}
		return "";
	}

	rack::midi::OutputDevice* subscribeOutput(int deviceId, rack::midi::Output* output) override {
		if (deviceId < 0) return NULL;
		int portGroup = deviceId / MIDIESX_PORTGROUP_SIZE;
		int portIndex = deviceId % MIDIESX_PORTGROUP_SIZE;

		if (devices[portGroup][portIndex] == NULL) {
			devices[portGroup][portIndex] = new MidiEsxOutputDevice(this, deviceId);
		}
		MidiEsxOutputDevice* dev = devices[portGroup][portIndex];
		dev->subscribe(output);
		return dev;
	}

	void unsubscribeOutput(int deviceId, rack::midi::Output* output) override {
		if (deviceId < 0) return;
		int portGroup = deviceId / MIDIESX_PORTGROUP_SIZE;
		int portIndex = deviceId % MIDIESX_PORTGROUP_SIZE;

		MidiEsxOutputDevice* dev = devices[portGroup][portIndex];
		dev->unsubscribe(output);

		// Destroy device if nothing is subscribed anymore
		if (dev->subscribed.empty()) {
			delete dev;
			devices[portGroup][portIndex] = NULL;
		}
	}

	void subscribeHandler(int portGroup, MidiEsxMessageHandler* handler) {
		subscribers.push_back(std::make_tuple(portGroup, handler));
	}

	void unsubscribeHandler(int portGroup, MidiEsxMessageHandler* handler) {
		auto it = std::find(subscribers.begin(), subscribers.end(), std::make_tuple(portGroup, handler));
		if (it != subscribers.end()) subscribers.erase(it);
	}
};


MidiEsxDriver* midiDriver;

void init() {
	if (midiDriver) return;
	midiDriver = new MidiEsx::MidiEsxDriver();
	rack::midi::addDriver(MIDIESX_DRIVER_ID, midiDriver);
}

void subscribe(int portGroup, MidiEsxMessageHandler* handler) {
	if (midiDriver) midiDriver->subscribeHandler(portGroup, handler);
}

void unsubscribe(int portGroup, MidiEsxMessageHandler* handler) {
	if (midiDriver) midiDriver->unsubscribeHandler(portGroup, handler);
}

} // namespace MidiEsx
} // namespace StoermelderPackOne