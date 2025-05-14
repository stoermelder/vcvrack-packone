#include "plugin.hpp"
#include "components/LedTextDisplay.hpp"
#include "components/MidiWidget.hpp"
#include "components/LogDisplay.hpp"
#include <osdialog.h>
#include <list>
#include <iomanip>

namespace StoermelderPackOne {
namespace MidiMon {

const int BUFFERSIZE = 800;

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
	bool showNoteMsg;
	/** [Stored to JSON] */
	bool showKeyPressure;
	/** [Stored to JSON] */
	bool showCcMsg;
	/** [Stored to JSON] */
	bool showCcExMsg;
	/** [Stored to JSON] */
	bool showProgChangeMsg;
	/** [Stored to JSON] */
	bool showChannelPressurelMsg;
	/** [Stored to JSON] */
	bool showPitchWheelMsg;

	/** [Stored to JSON] */
	bool showSysExMsg;
	/** [Stored to JSON] */
	bool showSysExData;
	/** [Stored to JSON] */
	bool showClockMsg;
	/** [Stored to JSON] */
	bool showSystemMsg;

	/** [Stored to JSON] */
	midi::InputQueue midiInput;

	dsp::RingBuffer<std::tuple<LOG_FORMAT, float, std::string>, 512> midiLogMessages;
	uint64_t sample;

	std::list<std::tuple<uint64_t, midi::Message>> ccQueue;
	int16_t ccNrpnParam[16] = {-1};
	int16_t ccRpnParam[16] = {-1};

	MidiMonModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void onReset() override {
		showNoteMsg = true;
		showKeyPressure = true;
		showCcMsg = true;
		showCcExMsg = true;
		showProgChangeMsg = true;
		showChannelPressurelMsg = true;
		showPitchWheelMsg = true;

		showSysExMsg = false;
		showSysExData = false;
		showClockMsg = false;
		showSystemMsg = true;

		for (int i = 0; i < 16; i++) {
			ccNrpnParam[i] = -1;
			ccRpnParam[i] = -1;
		}

		resetTimestamp();
		Module::onReset();
	}

	void onSampleRateChange() override {
		if (sample != 0) resetTimestamp();
	}

	void resetTimestamp() {
		std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		char buf[100] = {0};
		std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
		midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, std::string(buf)));
		midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, string::f("sample rate %i", int(APP->engine->getSampleRate()))));
		sample = 0;
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
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
					std::string s = string::f("ch%i note on  %i vel %i", ch + 1, note, vel);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
				} break;
			case 0x8: // note off
				if (!midiLogMessages.full() && showNoteMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t note = msg.getNote();
					uint8_t vel = msg.getValue();
					std::string s = string::f("ch%i note off %i vel %i", ch + 1, note, vel);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
				} break;
			case 0xa: // key pressure
				if (!midiLogMessages.full() && showKeyPressure) {
					uint8_t ch = msg.getChannel();
					uint8_t note = msg.getNote();
					uint8_t value = msg.getValue();
					std::string s = string::f("ch%i key-pressure %i vel %i", ch + 1, note, value);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
				} break;
			case 0xb: // cc
				if (!midiLogMessages.full() && showCcMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t cc = msg.getNote();
					int8_t value = msg.bytes[2];
					std::string s = string::f("ch%i cc%i=%i", ch + 1, cc, value);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
					if (!midiLogMessages.full() && showCcExMsg) {
						processMidiCcEx(msg);
					}
				} break;
			case 0xc: // program change
				if (!midiLogMessages.full() && showProgChangeMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t prog = msg.getNote();
					std::string s = string::f("ch%i program=%i", ch + 1, prog);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
				} break;
			case 0xd: // channel pressure
				if (!midiLogMessages.full() && showChannelPressurelMsg) {
					uint8_t ch = msg.getChannel();
					uint8_t value = msg.getNote();
					std::string s = string::f("ch%i channel-pressure=%i", ch + 1, value);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
				} break;
			case 0xe: // pitch wheel
				if (!midiLogMessages.full() && showPitchWheelMsg) {
					uint8_t ch = msg.getChannel();
					uint16_t value = ((uint16_t)msg.getValue() << 7) | msg.getNote();
					std::string s = string::f("ch%i pitchwheel=%i", ch + 1, value);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
				} break;
			case 0xf: // system
				if (!midiLogMessages.full()) {
					switch (msg.getChannel()) {
						case 0x0: // sysex
							if (showSysExMsg) {
								std::string s = string::f("sysex (%i bytes)", msg.getSize());
								midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
								if (showSysExData) { // sysex bytes
									std::ostringstream ss;
									ss << std::hex;
									for (int i = 0; i < msg.getSize(); i++) {
										ss << std::setw(2) << std::setfill('0') << static_cast<int>(msg.bytes[i]) << " ";
									}
									midiLogMessages.push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, ss.str()));
								}
							} break;
						case 0x2: // song pointer
							if (showSystemMsg) {
								uint16_t value = ((uint16_t)msg.getValue() << 7) | msg.getNote();
								std::string s = string::f("song pointer=%i", value);
								midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
							} break;
						case 0x3: // song select
							if (showSystemMsg) {
								uint8_t song = msg.getNote();
								std::string s = string::f("song select=%i", song);
								midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, s));
							} break;
						case 0x8: // timing clock
							if (showClockMsg) {
								midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, "clock tick"));
							} break;
						case 0xa: // start
							if (showSystemMsg) {
								midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, "start"));
							} break;
						case 0xb: // continue
							if (showSystemMsg) {
								midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, "continue"));
							} break;
						case 0xc: // stop
							if (showSystemMsg) {
								midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, "stop"));
							} break;
						default:
							break;
					}
				} break;
			default:
				break;
		}
	}

	void processMidiCcEx(midi::Message& msg) {
		uint8_t ch = msg.getChannel();
		uint8_t cc = msg.getNote();
		int8_t value = msg.bytes[2];

		// RPN param
		if (cc == 100 && ccQueue.size() > 0) {
			std::tuple<uint64_t, midi::Message> prev = ccQueue.back();
			uint64_t prevSample = std::get<0>(prev);
			midi::Message prevMsg = std::get<1>(prev);

			float delta = float(sample - prevSample) * APP->engine->getSampleTime();
			if (prevMsg.getChannel() == ch && prevMsg.getNote() == 101 && delta < 0.1f) {
				int16_t msb = prevMsg.bytes[2];
				if (msb == 127 && value == 127) {
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, string::f("ch%i rpn/nrpn reset", ch + 1)));
					ccNrpnParam[ch] = ccRpnParam[ch] = -1;
				}
				if (msb == 0) {
					switch (value) {
						case 0: // Pitch Bend Sensitivity
							midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, string::f("ch%i rpn param=Pitch Bend Sensitivity", ch + 1)));
							ccNrpnParam[ch] = -1;
							ccRpnParam[ch] = 0;
							break;
						case 1: // Fine Tuning
							midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, string::f("ch%i rpn param=Fine Tuning", ch + 1)));
							ccNrpnParam[ch] = -1;
							ccRpnParam[ch] = 1;
							break;
						case 2: // Coarse Tuning
							midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, string::f("ch%i rpn param=Coarse Tuning", ch + 1)));
							ccNrpnParam[ch] = -1;
							ccRpnParam[ch] = 2;
							break;
						case 3: // Tuning Program Select
							midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, string::f("ch%i rpn param=Tuning Program Select", ch + 1)));
							ccNrpnParam[ch] = -1;
							ccRpnParam[ch] = 3;
							break;
						case 4: // Tuning Bank Select
							midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, string::f("ch%i rpn param=Tuning Bank Select", ch + 1)));
							ccNrpnParam[ch] = -1;
							ccRpnParam[ch] = 4;
							break;
						default:
							break;
					}
				}
			}
		}

		// NRPN param
		if (cc == 98 && ccQueue.size() > 0) {
			std::tuple<uint64_t, midi::Message> prev = ccQueue.back();
			uint64_t prevSample = std::get<0>(prev);
			midi::Message prevMsg = std::get<1>(prev);

			float delta = float(sample - prevSample) * APP->engine->getSampleTime();
			if (prevMsg.getChannel() == ch && prevMsg.getNote() == 99 && delta < 0.1f) {
				int16_t number = int16_t(prevMsg.bytes[2]) * 128 + value;
				midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, string::f("ch%i nrpn param=%i", ch + 1, number)));
				ccNrpnParam[ch] = number;
				ccRpnParam[ch] = -1;
			}
		}

		// RPN/NRPN data entry
		bool isDataEntry = false;
		if (cc == 6 && (ccNrpnParam[ch] >= 0 || ccRpnParam[ch] >= 0)) {
			// cc38 is optional for LSB
			// TODO
		}
		if (cc == 38 && ccQueue.size() > 0 && (ccNrpnParam[ch] >= 0 || ccRpnParam[ch] >= 0)) {
			std::tuple<uint64_t, midi::Message> prev = ccQueue.back();
			uint64_t prevSample = std::get<0>(prev);
			midi::Message prevMsg = std::get<1>(prev);

			float delta = float(sample - prevSample) * APP->engine->getSampleTime();
			if (prevMsg.getChannel() == ch && prevMsg.getNote() == 6 && delta < 0.1f) {
				int16_t value1 = int16_t(prevMsg.bytes[2]) * 128 + value;
				if (ccRpnParam[ch] >= 0) {
					std::string s = string::f("ch%i rpn %i=%i", ch + 1, ccRpnParam[ch], value1);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, s));
				}
				if (ccNrpnParam[ch] >= 0) {
					std::string s = string::f("ch%i nrpn %i=%i", ch + 1, ccNrpnParam[ch], value1);
					midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, s));
				}
				isDataEntry = true;
			}
		}

		// 14-bit CC
		if (32 <= cc && cc < 64 && ccQueue.size() > 0 && !isDataEntry) {
			std::tuple<uint64_t, midi::Message> prev = ccQueue.back();
			uint64_t prevSample = std::get<0>(prev);
			midi::Message prevMsg = std::get<1>(prev);

			float delta = float(sample - prevSample) * APP->engine->getSampleTime();
			if (ch == prevMsg.getChannel() && cc == prevMsg.getNote() + 32 && delta < 0.1f) {
				int16_t value1 = int16_t(prevMsg.bytes[2]) * 128 + value;
				std::string s = string::f("ch%i cc%i 14-bit=%i", ch + 1, prevMsg.getNote(), value1);
				midiLogMessages.push(std::make_tuple(LOG_FORMAT::INDENTED, 0.f, s));
			}
		}

		ccQueue.push_back(std::make_tuple(sample, msg));
		if (ccQueue.size() > 3) {
			ccQueue.pop_front();
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "showNoteMsg", json_boolean(showNoteMsg));
		json_object_set_new(rootJ, "showKeyPressure", json_boolean(showKeyPressure));
		json_object_set_new(rootJ, "showCcMsg", json_boolean(showCcMsg));
		json_object_set_new(rootJ, "showCcExMsg", json_boolean(showCcExMsg));
		json_object_set_new(rootJ, "showProgChangeMsg", json_boolean(showProgChangeMsg));
		json_object_set_new(rootJ, "showChannelPressurelMsg", json_boolean(showChannelPressurelMsg));
		json_object_set_new(rootJ, "showPitchWheelMsg", json_boolean(showPitchWheelMsg));

		json_object_set_new(rootJ, "showSysExMsg", json_boolean(showSysExMsg));
		json_object_set_new(rootJ, "showSysExData", json_boolean(showSysExMsg));
		json_object_set_new(rootJ, "showClockMsg", json_boolean(showClockMsg));
		json_object_set_new(rootJ, "showSystemMsg", json_boolean(showSystemMsg));

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		showNoteMsg = json_boolean_value(json_object_get(rootJ, "showNoteMsg"));
		showKeyPressure = json_boolean_value(json_object_get(rootJ, "showKeyPressure"));
		showCcMsg = json_boolean_value(json_object_get(rootJ, "showCcMsg"));
		json_t* showCcExMsgJ = json_object_get(rootJ, "showCcExMsg");
		showCcExMsg = showCcExMsgJ ? json_boolean_value(showCcExMsgJ) : showCcMsg;
		showProgChangeMsg = json_boolean_value(json_object_get(rootJ, "showProgChangeMsg"));
		showChannelPressurelMsg = json_boolean_value(json_object_get(rootJ, "showChannelPressurelMsg"));
		showPitchWheelMsg = json_boolean_value(json_object_get(rootJ, "showPitchWheelMsg"));

		showSysExMsg = json_boolean_value(json_object_get(rootJ, "showSysExMsg"));
		showSysExData = json_boolean_value(json_object_get(rootJ, "showSysExData"));
		showClockMsg = json_boolean_value(json_object_get(rootJ, "showClockMsg"));
		showSystemMsg = json_boolean_value(json_object_get(rootJ, "showSystemMsg"));

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ) midiInput.fromJson(midiInputJ);
	}
};


struct MidiMonWidget : ThemedModuleWidget<MidiMonModule> {
	LogDisplay* logDisplay;
	std::list<std::tuple<LOG_FORMAT, float, std::string>> buffer;
	
	MidiMonWidget(MidiMonModule* module)
		: ThemedModuleWidget<MidiMonModule>(module, "MidiMon") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget<>* midiInputWidget = createWidget<MidiWidget<>>(Vec(55.f, 36.4f));
		midiInputWidget->box.size = Vec(130.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL);
		addChild(midiInputWidget);

		LedDisplay* textDisplay = createWidget<LedDisplay>(Vec(10.f, 108.7f));
		textDisplay->box.size = Vec(219.9f, 234.1f);
		addChild(textDisplay);

		logDisplay = createWidget<LogDisplay>(Vec());
		logDisplay->buffer = &buffer;
		logDisplay->box.size = textDisplay->box.size.minus(Vec(0.f, 4.f));
		textDisplay->addChild(logDisplay);
	}

	void step() override {
		ThemedModuleWidget<MidiMonModule>::step();
		if (!module) return;
		MidiMonModule* module = reinterpret_cast<MidiMonModule*>(this->module);
		while (!module->midiLogMessages.empty()) {
			if (buffer.size() == BUFFERSIZE) buffer.pop_back();
			std::tuple<LOG_FORMAT, float, std::string> s = module->midiLogMessages.shift();
			buffer.push_front(s);
			logDisplay->dirty = true;
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiMonModule>::appendContextMenu(menu);
		MidiMonModule* module = dynamic_cast<MidiMonModule*>(this->module);

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Channel MIDI messages", "", [=](Menu* menu) {
			menu->addChild(createBoolPtrMenuItem("Note on/off", "", &module->showNoteMsg));
			menu->addChild(createBoolPtrMenuItem("Key pressure", "", &module->showKeyPressure));
			menu->addChild(createBoolPtrMenuItem("CC", "", &module->showCcMsg));
			menu->addChild(createBoolPtrMenuItem("CC (14-bit/RPN/NRPN)", "", &module->showCcExMsg));
			menu->addChild(createBoolPtrMenuItem("Program change", "", &module->showProgChangeMsg));
			menu->addChild(createBoolPtrMenuItem("Channel pressure", "", &module->showChannelPressurelMsg));
			menu->addChild(createBoolPtrMenuItem("Pitch wheel", "", &module->showPitchWheelMsg));
		}));
		menu->addChild(createSubmenuItem("System MIDI messages", "", [=](Menu* menu) {
			menu->addChild(createBoolPtrMenuItem("Clock", "", &module->showClockMsg));
			menu->addChild(createBoolPtrMenuItem("Other", "", &module->showSystemMsg));
			menu->addChild(createBoolPtrMenuItem("SysEx", "", &module->showSysExMsg));
			menu->addChild(createBoolPtrMenuItem("SysEx Data", "", &module->showSysExData));
		}));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Clear log", "", [this]() { resetLog(); }));
		menu->addChild(createMenuItem("Export log", "", [this]() { exportLogDialog(); }));
	}

	void resetLog() {
		buffer.clear();
		module->resetTimestamp();
		logDisplay->reset();
	}

	void exportLog(std::string filename) {
		INFO("Saving file %s", filename.c_str());

		FILE* file = fopen(filename.c_str(), "w");
		if (!file) {
			std::string message = string::f("Could not write to file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
		}
		DEFER({
			fclose(file);
		});

		fputs(string::f("%s v%s\n", rack::APP_NAME.c_str(), rack::APP_VERSION.c_str()).c_str(), file);
		fputs(string::f("%s\n", system::getOperatingSystemInfo().c_str()).c_str(), file);
		fputs(string::f("MIDI driver: %s\n", module->midiInput.getDriver()->getName().c_str()).c_str(), file);
		fputs(string::f("MIDI device: %s\n", module->midiInput.getDeviceName(module->midiInput.deviceId).c_str()).c_str(), file);
		fputs(string::f("MIDI channel: %s\n", module->midiInput.getChannelName(module->midiInput.channel).c_str()).c_str(), file);
		fputs("--------------------------------------------------------------------\n", file);

		for (std::list<std::tuple<LOG_FORMAT, float, std::string>>::reverse_iterator rit = buffer.rbegin(); rit != buffer.rend(); rit++) {
			std::tuple<LOG_FORMAT, float, std::string> s = *rit;
			LOG_FORMAT f = std::get<0>(s);
			float timestamp = std::get<1>(s);
			switch (f) {
				case LOG_FORMAT::TIMESTAMP:
					fputs(string::f("[%11.4f] %s\n", timestamp, std::get<2>(s).c_str()).c_str(), file);
					break;
				case LOG_FORMAT::TEXT:
					fputs(string::f("%s\n", std::get<2>(s).c_str()).c_str(), file);
					break;
				case LOG_FORMAT::INDENTED:
					fputs(string::f("                       %s\n", std::get<2>(s).c_str()).c_str(), file);
					break;
				default:
					break;
			}
		}
	}

	void exportLogDialog() {
		static const char PRESET_FILTERS[] = "*:*";
		osdialog_filters* filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		std::string log = asset::user("MidiMon.log");
		std::string dir = system::getDirectory(log);
		std::string filename = system::getFilename(log);

		char* path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		std::string pathStr = path;
		exportLog(pathStr);
	}
};

} // namespace MidiMon
} // namespace StoermelderPackOne

Model* modelMidiMon = createModel<StoermelderPackOne::MidiMon::MidiMonModule, StoermelderPackOne::MidiMon::MidiMonWidget>("MidiMon");