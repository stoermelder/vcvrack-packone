#include "plugin.hpp"

namespace StoermelderPackOne {
namespace MidiPlug {

struct MidiPlugModule : Module {
	/** [Stored to JSON] */
	int panelTheme = 0;

	struct MidiPlugOutput : midi::Output {
		void sendChannelMessage(midi::Message& message) {
			if (outputDevice) {
				outputDevice->sendMessage(message);
			}
		}
	};

	/** [Stored to Json] */
	midi::InputQueue midiInput[2];
	/** [Stored to Json] */
	MidiPlugOutput midiOutput[2];

	MidiPlugModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		onReset();
	}

	void onReset() override {
		midiInput[0].reset();
		midiInput[1].reset();
		midiOutput[0].reset();
		midiOutput[1].reset();
	}

	void process(const ProcessArgs &args) override {
		midi::Message msg;
		while (midiInput[0].shift(&msg)) {
			midiOutput[0].sendChannelMessage(msg);
			midiOutput[1].sendChannelMessage(msg);
		}
		while (midiInput[1].shift(&msg)) {
			midiOutput[0].sendChannelMessage(msg);
			midiOutput[1].sendChannelMessage(msg);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "midiInput0", midiInput[0].toJson());
		json_object_set_new(rootJ, "midiInput1", midiInput[1].toJson());
		json_object_set_new(rootJ, "midiOutput0", midiOutput[0].toJson());
		json_object_set_new(rootJ, "midiOutput1", midiOutput[1].toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* midiInput0J = json_object_get(rootJ, "midiInput0");
		if (midiInput0J) midiInput[0].fromJson(midiInput0J);
		json_t* midiInput1J = json_object_get(rootJ, "midiInput1");
		if (midiInput1J) midiInput[1].fromJson(midiInput1J);
		json_t* midiOutput0J = json_object_get(rootJ, "midiOutput0");
		if (midiOutput0J) midiOutput[0].fromJson(midiOutput0J);
		json_t* midiOutput1J = json_object_get(rootJ, "midiOutput1");
		if (midiOutput1J) midiOutput[1].fromJson(midiOutput1J);
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

struct MidiPlugWidget : ThemedModuleWidget<MidiPlugModule> {
	MidiPlugWidget(MidiPlugModule* module)
		: ThemedModuleWidget<MidiPlugModule>(module, "MidiPlug") {
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

		MidiPlugMidiWidget* midiOutput0Widget = createWidget<MidiPlugMidiWidget>(Vec(10.0f, 190.f));
		midiOutput0Widget->box.size = Vec(130.0f, 44.0f);
		midiOutput0Widget->setMidiPort(module ? &module->midiOutput[0] : NULL);
		addChild(midiOutput0Widget);

		MidiPlugMidiWidget* midiOutput1Widget = createWidget<MidiPlugMidiWidget>(Vec(10.0f, 238.f));
		midiOutput1Widget->box.size = Vec(130.0f, 44.0f);
		midiOutput1Widget->setMidiPort(module ? &module->midiOutput[1] : NULL);
		addChild(midiOutput1Widget);
	}
};

} // namespace MidiPlug
} // namespace StoermelderPackOne

Model* modelMidiPlug = createModel<StoermelderPackOne::MidiPlug::MidiPlugModule, StoermelderPackOne::MidiPlug::MidiPlugWidget>("MidiPlug");