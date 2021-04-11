#include "plugin.hpp"
#include "components/MidiWidget.hpp"
#include "components/LedDisplayCenterChoiceEx.hpp"
#include <osdialog.h>

namespace StoermelderPackOne {
namespace MidiPlug {

template <int INPUT = 2, int OUTPUT = 2>
struct MidiPlugModule : Module {
	/** [Stored to JSON] */
	int panelTheme = 0;

	struct MidiPlugOutput : midi::Output {
		enum class MODE {
			REPLACE = 1,
			FILTER = 2,
			BLOCK = 3
		};
		MODE plugMode;

		void sendChannelMessage(midi::Message& message) {
			if (channel >= 0) {
				switch (plugMode) {
					case MODE::REPLACE:
						message.setChannel(channel);
						break;
					case MODE::FILTER:
						if (message.getChannel() != channel) return;
						break;
					case MODE::BLOCK:
						if (message.getChannel() == channel) return;
						break;
				}
			}
			if (outputDevice) {
				outputDevice->sendMessage(message);
			}
		}

		std::vector<int> getChannels() override {
			std::vector<int> channels = midi::Output::getChannels();
			channels.emplace(channels.begin(), -1);
			return channels;
		}

		void resetEx() {
			reset();
			channel = -1;
			plugMode = MODE::REPLACE;
		}

		json_t* toJsonEx() {
			json_t* rootJ = midi::Output::toJson();
			json_object_set_new(rootJ, "plugMode", json_integer((int)plugMode));
			return rootJ;
		}
		void fronJsonEx(json_t* rootJ) {
			plugMode = (MODE)json_integer_value(json_object_get(rootJ, "plugMode"));
			midi::Output::fromJson(rootJ);
		}
	};

	/** [Stored to Json] */
	midi::InputQueue midiInput[INPUT];
	/** [Stored to Json] */
	MidiPlugOutput midiOutput[OUTPUT];

	MidiPlugModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		onReset();
	}

	void onReset() override {
		for (int i = 0; i < INPUT; i++) {
			midiInput[i].reset();
		}
		for (int i = 0; i < OUTPUT; i++) {
			midiOutput[i].resetEx();
		}
	}

	void process(const ProcessArgs &args) override {
		midi::Message msg;
		for (int i = 0; i < INPUT; i++) {
			while (midiInput[i].shift(&msg)) {
				for (int j = 0; j < OUTPUT; j++) {
					midiOutput[j].sendChannelMessage(msg);
				}
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* midiInputJ = json_array();
		for (int i = 0; i < INPUT; i++) {
			json_array_append_new(midiInputJ, midiInput[i].toJson());
		}
		json_object_set_new(rootJ, "midiInput", midiInputJ);

		json_t* midiOutputJ = json_array();
		for (int i = 0; i < OUTPUT; i++) {
			json_array_append_new(midiOutputJ, midiOutput[i].toJsonEx());
		}
		json_object_set_new(rootJ, "midiOutput", midiOutputJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		for (int i = 0; i < INPUT; i++) {
			json_t* o = json_array_get(midiInputJ, i);
			midiInput[i].fromJson(o);
		}

		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		for (int i = 0; i < OUTPUT; i++) {
			json_t* o = json_array_get(midiOutputJ, i);
			midiOutput[i].fronJsonEx(o);
		}
	}
};


struct MidiPlugOutChannelChoice : MidiChannelChoice<> {
	ui::Menu* createContextMenu() override {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("MIDI channel"));
		for (int channel : port->getChannels()) {
			MidiChannelItem* item = new MidiChannelItem;
			item->port = port;
			item->channel = channel;
			item->text = channel == -1 ? "Thru" : port->getChannelName(channel);
			item->rightText = CHECKMARK(item->channel == port->channel);
			menu->addChild(item);
		}
		return menu;
	}

	void step() override {
		MidiChannelChoice<>::step();
		if (port && port->channel == -1) {
			text = "Thru";
		}
	}
};

struct MidiPlugOutModeChoice : LedDisplayCenterChoiceEx {
	typedef MidiPlugModule<>::MidiPlugOutput Output;
	Output* port;

	void onAction(const event::Action& e) override {
		if (!port) return;

		struct ModeMenuItem : MenuItem {
			Output* port;
			Output::MODE plugMode;
			void onAction(const event::Action& e) override {
				port->plugMode = plugMode;
			}
			void step() override {
				rightText = CHECKMARK(port->plugMode == plugMode);
				MenuItem::step();
			}
		};

		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("MIDI channel mode"));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Replace channel", &ModeMenuItem::plugMode, Output::MODE::REPLACE, &ModeMenuItem::port, port));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Filter to channel", &ModeMenuItem::plugMode, Output::MODE::FILTER, &ModeMenuItem::port, port));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Block channel", &ModeMenuItem::plugMode, Output::MODE::BLOCK, &ModeMenuItem::port, port));
	}

	void step() override {
		LedDisplayCenterChoiceEx::step();
		if (!port) return;
		if (port->channel == -1) {
			text = "---";
		}
		else {
			switch (port->plugMode) {
				case Output::MODE::BLOCK:
					text = "Block"; break;
				case Output::MODE::FILTER:
					text = "Filter"; break;
				case Output::MODE::REPLACE:
					text = "Replace"; break;
			}
		}
	}
};

struct MidiPlugOutWidget : MidiWidget<MidiDriverChoice<>, MidiDeviceChoice<>, MidiPlugOutChannelChoice> {
	MidiPlugOutModeChoice* channelModeChoice;

	void setMidiPortEx(MidiPlugModule<>::MidiPlugOutput* port) {
		MidiWidget<MidiDriverChoice<>, MidiDeviceChoice<>, MidiPlugOutChannelChoice>::setMidiPort(port);

		channelChoice->box.size.x = channelChoice->box.size.x / 2.f;
		Vec pos = Vec(channelChoice->box.pos.x + channelChoice->box.size.x, channelChoice->box.pos.y);

		LedDisplaySeparator* s = createWidget<LedDisplaySeparator>(pos);
		s->box.size.y = channelChoice->box.size.y;
		addChild(s);

		channelModeChoice = new MidiPlugOutModeChoice;
		channelModeChoice->box.size = channelChoice->box.size;
		channelModeChoice->box.pos = pos;
		channelModeChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
		channelModeChoice->port = port;
		addChild(channelModeChoice);
	}
};


struct MidiPlugWidget : ThemedModuleWidget<MidiPlugModule<>> {
	MidiPlugWidget(MidiPlugModule<>* module)
		: ThemedModuleWidget<MidiPlugModule<>>(module, "MidiPlug") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget<>* midiInput0Widget = createWidget<MidiWidget<>>(Vec(10.0f, 36.4f));
		midiInput0Widget->box.size = Vec(130.0f, 67.0f);
		midiInput0Widget->setMidiPort(module ? &module->midiInput[0] : NULL);
		addChild(midiInput0Widget);

		MidiWidget<>* midiInput1Widget = createWidget<MidiWidget<>>(Vec(10.0f, 107.4f));
		midiInput1Widget->box.size = Vec(130.0f, 67.0f);
		midiInput1Widget->setMidiPort(module ? &module->midiInput[1] : NULL);
		addChild(midiInput1Widget);

		MidiPlugOutWidget* midiOutput0Widget = createWidget<MidiPlugOutWidget>(Vec(10.0f, 204.8f));
		midiOutput0Widget->box.size = Vec(130.0f, 67.0f);
		midiOutput0Widget->setMidiPortEx(module ? &module->midiOutput[0] : NULL);
		addChild(midiOutput0Widget);

		MidiPlugOutWidget* midiOutput1Widget = createWidget<MidiPlugOutWidget>(Vec(10.0f, 275.8f));
		midiOutput1Widget->box.size = Vec(130.0f, 67.0f);
		midiOutput1Widget->setMidiPortEx(module ? &module->midiOutput[1] : NULL);
		addChild(midiOutput1Widget);
	}

	void appendContextMenu(Menu* menu) override {
		struct LoopbackDriverItem : MenuItem {
			void step() override {
				rightText = CHECKMARK(pluginSettings.midiLoopbackDriverEnabled);
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				if (!pluginSettings.midiLoopbackDriverEnabled) {
					std::string text = "After enabling the MIDI Loopback driver you will get an annoying error message every time you close the Rack. This will not harm your patches in any way. Furthermore it is possible to disable the MIDI Loopback driver at anytime you like.\n\nDo you want to enable the MIDI Loopback driver now?";
					if (osdialog_message(OSDIALOG_INFO, OSDIALOG_YES_NO, text.c_str())) {
						pluginSettings.midiLoopbackDriverEnabled = true;
						pluginSettings.saveToJson();
						text = "The MIDI Loopback driver will be enabled after the next restart of Rack.";
						osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, text.c_str());
					}
				} else {
					std::string text = "You're about to disable the MIDI Loopback driver. Proceed?";
					if (osdialog_message(OSDIALOG_INFO, OSDIALOG_YES_NO, text.c_str())) {
						pluginSettings.midiLoopbackDriverEnabled = false;
						pluginSettings.saveToJson();
						text = "The MIDI Loopback driver has been disabled after the next restart of Rack.";
						osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, text.c_str());
					}
				}
			}
		}; // struct LoopbackDriverItem

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LoopbackDriverItem>(&MenuItem::text, "MIDI Loopback driver"));
	}
};

} // namespace MidiPlug
} // namespace StoermelderPackOne

Model* modelMidiPlug = createModel<StoermelderPackOne::MidiPlug::MidiPlugModule<>, StoermelderPackOne::MidiPlug::MidiPlugWidget>("MidiPlug");