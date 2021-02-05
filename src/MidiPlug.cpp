#include "plugin.hpp"

namespace StoermelderPackOne {
namespace MidiPlug {

template <int INPUT = 2, int OUTPUT = 2>
struct MidiPlugModule : Module {
	/** [Stored to JSON] */
	int panelTheme = 0;

	struct MidiPlugOutput : midi::Output {
		void sendChannelMessage(midi::Message& message) {
			if (channel >= 0) {
				message.setChannel(channel);
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

		void reset1() {
			reset();
			channel = -1;
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
			midiOutput[i].reset1();
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
			json_array_append_new(midiOutputJ, midiOutput[i].toJson());
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
			midiOutput[i].fromJson(o);
		}
	}
};


struct MidiPlugMidiWidget : MidiWidget {
	void setMidiPort(midi::Port* port) {
		MidiWidget::setMidiPort(port);

		driverChoice->textOffset = Vec(6.f, 14.7f);
		driverChoice->box.size = mm2px(Vec(driverChoice->box.size.x, 7.5f));
		driverChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);

		driverSeparator->box.pos = driverChoice->box.getBottomLeft();

		deviceChoice->textOffset = Vec(6.f, 14.7f);
		deviceChoice->box.size = mm2px(Vec(deviceChoice->box.size.x, 7.5f));
		deviceChoice->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);

		deviceSeparator->box.pos = deviceChoice->box.getBottomLeft();

		channelChoice->textOffset = Vec(6.f, 14.7f);
		channelChoice->box.size = mm2px(Vec(channelChoice->box.size.x, 7.5f));
		channelChoice->box.pos = deviceChoice->box.getBottomLeft();
		channelChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
	}
};

struct MidiPlugOutMidiWidget : MidiPlugMidiWidget {
	midi::Port* port;
	void setMidiPort(midi::Port* port) {
		this->port = port;
		MidiPlugMidiWidget::setMidiPort(port);
	}

	void step() override {
		MidiWidget::step();
		if (port->channel == -1) {
			channelChoice->text = "Thru";
		}
	}

	void onButton(const event::Button& e) override {
		MidiWidget::onButton(e);
		MenuOverlay* overlay = NULL;
		for (Widget* child : APP->scene->children) {
			overlay = dynamic_cast<MenuOverlay*>(child);
			if (overlay) break;
		}
		if (!overlay) return;
		Widget* w = overlay->children.front();
		Menu* menu = dynamic_cast<Menu*>(w);
		if (!menu) return;
		MenuLabel* menuLabel = dynamic_cast<MenuLabel*>(menu->children.front());
		if (!menuLabel || menuLabel->text != "MIDI channel") return;

		for (Widget* child : menu->children) {
			MenuItem* m = dynamic_cast<MenuItem*>(child);
			if (m && m->text == "All channels") {
				m->text = "Thru";
				break;
			}
		}
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

		MidiPlugMidiWidget* midiInput0Widget = createWidget<MidiPlugMidiWidget>(Vec(10.0f, 36.4f));
		midiInput0Widget->box.size = Vec(130.0f, 67.0f);
		midiInput0Widget->setMidiPort(module ? &module->midiInput[0] : NULL);
		addChild(midiInput0Widget);

		MidiPlugMidiWidget* midiInput1Widget = createWidget<MidiPlugMidiWidget>(Vec(10.0f, 107.4f));
		midiInput1Widget->box.size = Vec(130.0f, 67.0f);
		midiInput1Widget->setMidiPort(module ? &module->midiInput[1] : NULL);
		addChild(midiInput1Widget);

		MidiPlugOutMidiWidget* midiOutput0Widget = createWidget<MidiPlugOutMidiWidget>(Vec(10.0f, 204.8f));
		midiOutput0Widget->box.size = Vec(130.0f, 67.0f);
		midiOutput0Widget->setMidiPort(module ? &module->midiOutput[0] : NULL);
		addChild(midiOutput0Widget);

		MidiPlugOutMidiWidget* midiOutput1Widget = createWidget<MidiPlugOutMidiWidget>(Vec(10.0f, 275.8f));
		midiOutput1Widget->box.size = Vec(130.0f, 67.0f);
		midiOutput1Widget->setMidiPort(module ? &module->midiOutput[1] : NULL);
		addChild(midiOutput1Widget);
	}
};

} // namespace MidiPlug
} // namespace StoermelderPackOne

Model* modelMidiPlug = createModel<StoermelderPackOne::MidiPlug::MidiPlugModule<>, StoermelderPackOne::MidiPlug::MidiPlugWidget>("MidiPlug");