#pragma once
#include <rack.hpp>
#include "LedDisplayCenterChoiceEx.hpp"

namespace StoermelderPackOne {

using namespace rack;

struct MidiDriverItem : ui::MenuItem {
	midi::Port* port;
	int driverId;
	void onAction(const event::Action& e) override {
		port->setDriverId(driverId);
	}
};

template <class DRIVERITEM = MidiDriverItem>
struct MidiDriverChoice : LedDisplayCenterChoiceEx {
	midi::Port* port;
	std::string prefix = "";

	void onAction(const event::Action& e) override {
		if (!port)
			return;
		createContextMenu();
	}

	virtual ui::Menu* createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("MIDI driver"));
		for (int driverId : midi::getDriverIds()) {
			DRIVERITEM* item = new DRIVERITEM;
			item->port = port;
			item->driverId = driverId;
			item->text = midi::getDriver(driverId)->getName();
			item->rightText = CHECKMARK(item->driverId == port->driverId);
			menu->addChild(item);
		}
		return menu;
	}

	void step() override {
		text = prefix + (prefix != "" ? + ": " : "");
		if (port) {
			text += port->getDriver()->getName();
			color.a = 1.f;
		}
		else {
			text += "(No driver)";
			color.a = 0.5f;
		}
	}
};


struct MidiDeviceItem : ui::MenuItem {
	midi::Port* port;
	int deviceId;
	void onAction(const event::Action& e) override {
		port->setDeviceId(deviceId);
	}
};

template <class DEVICEITEM = MidiDeviceItem>
struct MidiDeviceChoice : LedDisplayCenterChoiceEx {
	midi::Port* port;
	void onAction(const event::Action& e) override {
		if (!port)
			return;
		createContextMenu();
	}

	virtual ui::Menu* createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("MIDI device"));
		{
			DEVICEITEM* item = new DEVICEITEM;
			item->port = port;
			item->deviceId = -1;
			item->text = "(No device)";
			item->rightText = CHECKMARK(item->deviceId == port->deviceId);
			menu->addChild(item);
		}
		for (int deviceId : port->getDeviceIds()) {
			DEVICEITEM* item = new DEVICEITEM;
			item->port = port;
			item->deviceId = deviceId;
			item->text = port->getDeviceName(deviceId);
			item->rightText = CHECKMARK(item->deviceId == port->deviceId);
			menu->addChild(item);
		}
		return menu;
	}

	void step() override {
		text = port && port->deviceId >= 0 ? port->getDeviceName(port->deviceId) : "";
		if (!text.empty()) {
			color.a = 1.f;
		}
		else {
			text = "(No device)";
			color.a = 0.5f;
		}
	}
};


struct MidiChannelItem : ui::MenuItem {
	midi::Port* port;
	int channel;
	void onAction(const event::Action& e) override {
		port->channel = channel;
	}
};

template <class CHANNELITEM = MidiChannelItem>
struct MidiChannelChoice : LedDisplayCenterChoiceEx {
	midi::Port* port;
	void onAction(const event::Action& e) override {
		if (!port)
			return;
		createContextMenu();
	}

	virtual ui::Menu* createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("MIDI channel"));
		for (int channel : port->getChannels()) {
			CHANNELITEM* item = new CHANNELITEM;
			item->port = port;
			item->channel = channel;
			item->text = port->getChannelName(channel);
			item->rightText = CHECKMARK(item->channel == port->channel);
			menu->addChild(item);
		}
		return menu;
	}

	void step() override {
		text = port && port->channel >= 0 ? port->getChannelName(port->channel) : "";
		if (!text.empty()) {
			color.a = 1.0f;
		}
		else {
			text = "(No channel)";
			color.a = 0.5f;
		}
	}
};


template <class TDRIVER = MidiDriverChoice<>, class TDEVICE = MidiDeviceChoice<>, class TCHANNEL = MidiChannelChoice<>>
struct MidiWidget : LedDisplay {
	TDRIVER* driverChoice;
	LedDisplaySeparator* driverSeparator;
	TDEVICE* deviceChoice;
	LedDisplaySeparator* deviceSeparator;
	TCHANNEL* channelChoice;

	void setMidiPort(midi::Port* port, std::string prefix = "") {
		clearChildren();
		math::Vec pos;

		TDRIVER* driverChoice = createWidget<TDRIVER>(pos);
		driverChoice->box.size = Vec(box.size.x, 22.15f);
		//driverChoice->textOffset = Vec(6.f, 14.7f);
		driverChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
		driverChoice->port = port;
		driverChoice->prefix = prefix;
		addChild(driverChoice);
		pos = driverChoice->box.getBottomLeft();
		this->driverChoice = driverChoice;

		this->driverSeparator = createWidget<LedDisplaySeparator>(pos);
		this->driverSeparator->box.size.x = box.size.x;
		addChild(this->driverSeparator);

		TDEVICE* deviceChoice = createWidget<TDEVICE>(pos);
		deviceChoice->box.size = Vec(box.size.x, 22.15f);
		//deviceChoice->textOffset = Vec(6.f, 14.7f);
		deviceChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
		deviceChoice->port = port;
		addChild(deviceChoice);
		pos = deviceChoice->box.getBottomLeft();
		this->deviceChoice = deviceChoice;

		this->deviceSeparator = createWidget<LedDisplaySeparator>(pos);
		this->deviceSeparator->box.size.x = box.size.x;
		addChild(this->deviceSeparator);

		TCHANNEL* channelChoice = createWidget<TCHANNEL>(pos);
		channelChoice->box.size = Vec(box.size.x, 22.15f);
		//channelChoice->textOffset = Vec(6.f, 14.7f);
		channelChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
		channelChoice->port = port;
		addChild(channelChoice);
		this->channelChoice = channelChoice;
	}
};

} // namespace StoermelderPackOne