#include "plugin.hpp"
#include "components/LedTextDisplay.hpp"
#include <osdialog.h>

namespace StoermelderPackOne {
namespace MidiMon {

const int LINEBUFFER = 200;

struct MidiMonModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	bool showClockMsg;
	/** [Stored to JSON] */
	bool showSystemMsg;
	/** [Stored to JSON] */
	bool showNoteMsg;
	/** [Stored to JSON] */
	bool showPitchWheelMsg;
	/** [Stored to JSON] */
	bool showChannelPressurelMsg;
	/** [Stored to JSON] */
	bool showCcMsg;
	/** [Stored to JSON] */
	midi::InputQueue midiInput;

	dsp::RingBuffer<std::string, 512> midiLogMessages;
	int sample;

	MidiMonModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void onReset() override {
		showClockMsg = false;
		showSystemMsg = true;
		showNoteMsg = true;
		showPitchWheelMsg = true;
		showChannelPressurelMsg = true;
		showCcMsg = false;
		sample = 0;
		Module::onReset();
	}

	void onSampleRateChange() override {
		sample = 0;
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.shift(&msg)) {
			processMidi(msg);
		}
		sample++;
	}

	void processMidi(midi::Message& msg) {
		float timestamp = float(sample) / APP->engine->getSampleRate();
		switch (msg.getStatus()) {
			case 0x9: // note on
				if (!midiLogMessages.full() && showNoteMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t note = msg.getNote();
					uint8_t vel = msg.getValue();
					std::string s = string::f("[%8.3f] ch%i note %i vel %i", timestamp, ch + 1, note, vel);
					midiLogMessages.push(s);
				}
				break;
			case 0x8: // note off
				if (!midiLogMessages.full() && showNoteMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t note = msg.getNote();
					uint8_t vel = msg.getValue();
					std::string s = string::f("[%8.3f] ch%i note off %i vel %i", timestamp, ch + 1, note, vel);
					midiLogMessages.push(s);
				}
				break;
			case 0xa: // key pressure
				if (!midiLogMessages.full() && showNoteMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t note = msg.getNote();
					uint8_t value = msg.getValue();
					std::string s = string::f("[%8.3f] ch%i key-press %i vel %i", timestamp, ch + 1, note, value);
					midiLogMessages.push(s);
				}
				break;
			case 0xd: // channel pressure
				if (!midiLogMessages.full() && showChannelPressurelMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t value = msg.getNote();
					std::string s = string::f("[%8.3f] ch%i chan-pressure=%i", timestamp, ch + 1, value);
					midiLogMessages.push(s);
				}
				break;
			case 0xe: // pitch wheel
				if (!midiLogMessages.full() && showPitchWheelMsg) {
					uint8_t ch = msg.getChannel();
					uint16_t value = ((uint16_t)msg.getValue() << 7) | msg.getNote();
					std::string s = string::f("[%8.3f] ch%i pitchwheel=%i", timestamp, ch + 1, value);
					midiLogMessages.push(s);
				} 
				break;
			case 0xb: // cc
				if (!midiLogMessages.full() && showCcMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t cc = msg.getNote();
					int8_t value = msg.bytes[2];
					std::string s = string::f("[%8.3f] ch%i cc%i=%i", timestamp, ch + 1, cc, value);
					midiLogMessages.push(s);
				}
				break;
			case 0xf: // system
				if (!midiLogMessages.full()) {
					switch (msg.getChannel()) {
						case 0x8: // clock
							if (showClockMsg) midiLogMessages.push(string::f("[%8.3f] clock tick", timestamp));
							break;
						case 0xa: // start
							if (showSystemMsg) midiLogMessages.push(string::f("[%8.3f] start", timestamp));
							break;
						case 0xb: // continue
							if (showSystemMsg) midiLogMessages.push(string::f("[%8.3f] continue", timestamp));
							break;
						case 0xc: // stop
							if (showSystemMsg) midiLogMessages.push(string::f("[%8.3f] stop", timestamp));
							break;
						default:
							break;
					}
				}
				break;
			default:
				break;
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "showClockMsg", json_boolean(showClockMsg));
		json_object_set_new(rootJ, "showSystemMsg", json_boolean(showSystemMsg));
		json_object_set_new(rootJ, "showNoteMsg", json_boolean(showNoteMsg));
		json_object_set_new(rootJ, "showPitchWheelMsg", json_boolean(showPitchWheelMsg));
		json_object_set_new(rootJ, "showChannelPressurelMsg", json_boolean(showChannelPressurelMsg));
		json_object_set_new(rootJ, "showCcMsg", json_boolean(showCcMsg));

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		showClockMsg = json_boolean_value(json_object_get(rootJ, "showClockMsg"));
		showSystemMsg = json_boolean_value(json_object_get(rootJ, "showSystemMsg"));
		showNoteMsg = json_boolean_value(json_object_get(rootJ, "showNoteMsg"));
		showPitchWheelMsg = json_boolean_value(json_object_get(rootJ, "showPitchWheelMsg"));
		showChannelPressurelMsg = json_boolean_value(json_object_get(rootJ, "showChannelPressurelMsg"));
		showCcMsg = json_boolean_value(json_object_get(rootJ, "showCcMsg"));

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ) midiInput.fromJson(midiInputJ);
	}
};


struct MidiDisplay : LedTextDisplay {
	MidiMonModule* module;
	std::list<std::string>* buffer;
	bool dirty = true;

	MidiDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		fontSize = 10.f;
	}
	void step() override {
		LedTextDisplay::step();
		if (dirty) {
			text = "";
			size_t size = std::min(buffer->size(), (size_t)19);
			size_t i = 0;
			for (std::string s : *buffer) {
				if (i >= size) break;
				text += s + "\n";
				i++;
			}
		}
	}
};

struct MidiMonMidiWidget : MidiWidget {
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

struct MidiMonWidget : ThemedModuleWidget<MidiMonModule> {
	MidiDisplay* textField;
	std::list<std::string> buffer;
	
	MidiMonWidget(MidiMonModule* module)
		: ThemedModuleWidget<MidiMonModule>(module, "MidiMon") {
		setModule(module);

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiMonMidiWidget* midiInputWidget = createWidget<MidiMonMidiWidget>(Vec(55.f, 36.4f));
		midiInputWidget->box.size = Vec(130.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL);
		addChild(midiInputWidget);

		textField = createWidget<MidiDisplay>(Vec(10.f, 108.7f));
		textField->module = module;
		textField->buffer = &buffer;
		textField->box.size = Vec(219.9f, 234.1f);
		addChild(textField);
	}

	void step() override {
		ThemedModuleWidget<MidiMonModule>::step();
		if (!module) return;
		MidiMonModule* module = reinterpret_cast<MidiMonModule*>(this->module);
		while (!module->midiLogMessages.empty()) {
			if (buffer.size() == LINEBUFFER) buffer.pop_back();
			std::string s = module->midiLogMessages.shift();
			buffer.push_front(s);
			textField->dirty = true;
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiMonModule>::appendContextMenu(menu);
		MidiMonModule* module = dynamic_cast<MidiMonModule*>(this->module);

		struct MsgItem : MenuItem {
			bool* s;
			void step() override {
				rightText = CHECKMARK(*s);
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				*s ^= true;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Show MIDI messages for..."));
		menu->addChild(construct<MsgItem>(&MenuItem::text, "Clock", &MsgItem::s, &module->showClockMsg));
		menu->addChild(construct<MsgItem>(&MenuItem::text, "System", &MsgItem::s, &module->showSystemMsg));
		menu->addChild(construct<MsgItem>(&MenuItem::text, "Notes", &MsgItem::s, &module->showNoteMsg));
		menu->addChild(construct<MsgItem>(&MenuItem::text, "Pitch wheel", &MsgItem::s, &module->showPitchWheelMsg));
		menu->addChild(construct<MsgItem>(&MenuItem::text, "Channel pressure", &MsgItem::s, &module->showChannelPressurelMsg));
		menu->addChild(construct<MsgItem>(&MenuItem::text, "CCs", &MsgItem::s, &module->showCcMsg));

		struct ExportItem : MenuItem {
			MidiMonWidget* mw;
			void onAction(const event::Action& e) override {
				mw->saveFileDialog();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ExportItem>(&MenuItem::text, "Export log", &ExportItem::mw, this));
	}

	void saveFile(std::string filename) {
		INFO("Saving file %s", filename.c_str());

		FILE* file = fopen(filename.c_str(), "w");
		if (!file) {
			std::string message = string::f("Could not write to file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
		}
		DEFER({
			fclose(file);
		});

		for (std::list<std::string>::reverse_iterator rit = buffer.rbegin(); rit != buffer.rend(); rit++) {
			std::string s = *rit;
			fputs(s.append("\n").c_str(), file);
		}
	}

	void saveFileDialog() {
		std::string dir = asset::user("patches");
		char* path = osdialog_file(OSDIALOG_SAVE, NULL, "Midi.log", NULL);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		std::string pathStr = path;
		saveFile(pathStr);
	}
};

} // namespace MidiMon
} // namespace StoermelderPackOne

Model* modelMidiMon = createModel<StoermelderPackOne::MidiMon::MidiMonModule, StoermelderPackOne::MidiMon::MidiMonWidget>("MidiMon");