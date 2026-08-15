#include "../../plugin.hpp"
#include "../../components/LedTextDisplay.hpp"
#include "../../components/MidiWidget.hpp"
#include <osdialog.h>
#include <list>
#include <iomanip>
#include <chrono>
#include "MidiProcessor.hpp"

namespace StoermelderPackOne {
namespace MidiMon {

const int BUFFERSIZE = 800;

enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

using LogEntry = std::tuple<LOG_FORMAT, float, int64_t, std::string>;

struct MidiMonModule : Module, MidiProcessorHandler {
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
	bool showRpnNrpnMsg;
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
	bool showFrame;

	/** [Stored to JSON] */
	MidiProcessor midiProcessor;

	ClockDividerEx processDivider;
	dsp::RingBuffer<LogEntry, 4096> midiLogMessages;
	bool isProcessing = false;

	MidiMonModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		processDivider.setDivision(512);
		midiProcessor.subscribe(this);

		ResetEvent re;
		onReset(re);
	}

	void onReset(const ResetEvent& e) override {
		showNoteMsg = true;
		showKeyPressure = true;
		showCcMsg = true;
		showCcExMsg = true;
		showRpnNrpnMsg = false;
		showProgChangeMsg = true;
		showChannelPressurelMsg = true;
		showPitchWheelMsg = true;

		showSysExMsg = false;
		showSysExData = false;
		showClockMsg = false;
		showSystemMsg = true;
		showFrame = false;

		midiProcessor.reset();

		logTimestampReset();
		Module::onReset(e);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		if (isProcessing) {
			logTimestampReset();
		}
	}

	void processBypass(const ProcessArgs& args) override {
		// Drain the queue while bypassed
		midiProcessor.processBypass(args.frame);
		Module::processBypass(args);
	}

	void process(const ProcessArgs& args) override {
		isProcessing = true;
		if (processDivider.process()) {
			midiProcessor.process(args.frame);
		}
	}

	void logMessage(bool showMessage, LOG_FORMAT logFormat, float timestamp, int64_t frame, std::string s) {
		if (!midiLogMessages.full() && showMessage) {
			midiLogMessages.push(std::make_tuple(logFormat, timestamp, frame, s));
		}
	}

	void logTimestampReset() {
		std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		char buf[100] = {0};
		std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
		logMessage(true, LOG_FORMAT::TIMESTAMP, 0.f, 0LL, std::string(buf));
		logMessage(true, LOG_FORMAT::TIMESTAMP, 0.f, 0LL, string::f("sample rate %i", int(APP->engine->getSampleRate())));
	}

	// MidiProcessorHandler
	bool processMidi(const MessageEx& m) override {
		std::string s;
		float timestamp = float(m.frame) / APP->engine->getSampleRate();
		int64_t frame = m.frame;
		switch (m.type) {
			case MessageEx::Type::NOTE_ON:
				s = string::f("ch%02d note on  %i vel %i", m.getChannel() + 1, m.getNote(), m.getValue());
				logMessage(showNoteMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, s);
				break;
			case MessageEx::Type::NOTE_OFF:
				s = string::f("ch%02d note off %i vel %i", m.getChannel() + 1, m.getNote(), m.getValue());
				logMessage(showNoteMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, s);
				break;
			case MessageEx::Type::KEY_PRESSURE:
				s = string::f("ch%02d key-pressure %i vel %i", m.getChannel() + 1, m.getNote(), m.getValue());
				logMessage(showKeyPressure, LOG_FORMAT::TIMESTAMP, timestamp, frame, s);
				break;
			case MessageEx::Type::CC:
				s = string::f("ch%02d cc%i=%i", m.getChannel() + 1, m.getNote(), m.getValue());
				logMessage(showCcMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, s);
				break;
			case MessageEx::Type::CC_14BIT:
				s = string::f("ch%02d 14-bit cc%i=%i", m.getChannel() + 1, m.getNote(), m.getValue());
				logMessage(showCcExMsg, LOG_FORMAT::INDENTED, 0.f, 0LL, s);
				break;
			case MessageEx::Type::RPN:
				if (m.getParamNumber() < 0) {
					s = string::f("ch%02d rpn/nrpn reset", m.getChannel() + 1);
				}
				else if (m.hasValue()) {
					s = string::f("ch%02d rpn param=%i value=%i", m.getChannel() + 1, m.getParamNumber(), m.getValue());
				}
				else if (m.getParamNumber() == 0) {
					s = string::f("ch%02d rpn param=0 (Pitch Bend Sensitivity)", m.getChannel() + 1);
				}
				else if (m.getParamNumber() == 1) {
					s = string::f("ch%02d rpn param=1 (Fine Tuning)", m.getChannel() + 1);
				}
				else if (m.getParamNumber() == 2) {
					s = string::f("ch%02d rpn param=2 (Coarse Tuning)", m.getChannel() + 1);
				}
				else if (m.getParamNumber() == 3) {
					s = string::f("ch%02d rpn param=3 (Tuning Program Select)", m.getChannel() + 1);
				}
				else if (m.getParamNumber() == 4) {
					s = string::f("ch%02d rpn param=4 (Tuning Bank Select)", m.getChannel() + 1);
				}
				logMessage(showRpnNrpnMsg, LOG_FORMAT::INDENTED, 0.f, 0LL, s);
				break;
			case MessageEx::Type::NRPN:
				if (m.hasValue()) {
					s = string::f("ch%02d nrpn param=%i value=%i", m.getChannel() + 1, m.getParamNumber(), m.getValue());
				}
				else {
					s = string::f("ch%02d nrpn param=%i selected", m.getChannel() + 1, m.getParamNumber());
				}
				logMessage(showRpnNrpnMsg, LOG_FORMAT::INDENTED, 0.f, 0LL, s);
				break;
			case MessageEx::Type::PROGRAM_CHANGE:
				s = string::f("ch%02d program=%i", m.getChannel() + 1, m.getNote());
				logMessage(showProgChangeMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, s);
				break;
			case MessageEx::Type::CHANNEL_PRESSURE:
				s = string::f("ch%02d channel-pressure=%i", m.getChannel() + 1, m.getNote());
				logMessage(showChannelPressurelMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, s);
				break;
			case MessageEx::Type::PITCH_BEND:
				s = string::f("ch%02d pitchbend=%i", m.getChannel() + 1, m.getValue());
				logMessage(showPitchWheelMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, s);
				break;
			case MessageEx::Type::SYSEX:
				logMessage(showSysExMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, string::f("sysex (%i data bytes)", m.getSysExSize() - 2));
				if (showSysExData) {
					std::ostringstream ss;
					ss << std::hex;
					for (int i = 0; i < m.getSysExSize(); i++) {
						ss << std::setw(2) << std::setfill('0') << static_cast<int>(m.getSysExByte(i)) << " ";
					}
					logMessage(true, LOG_FORMAT::TEXT, 0.f, 0LL, ss.str());
				}
				break;
			case MessageEx::Type::SONG_POINTER:
				logMessage(showSystemMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, string::f("song pointer=%i", m.getValue()));
				break;
			case MessageEx::Type::SONG_SELECT:
				logMessage(showSystemMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, string::f("song select=%i", m.getNote()));
				break;
			case MessageEx::Type::CLOCK:
				logMessage(showClockMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, "clock tick");
				break;
			case MessageEx::Type::START:
				logMessage(showSystemMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, "start");
				break;
			case MessageEx::Type::CONTINUE:
				logMessage(showSystemMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, "continue");
				break;
			case MessageEx::Type::STOP:
				logMessage(showSystemMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, "stop");
				break;
			case MessageEx::Type::RESET:
				logMessage(showSystemMsg, LOG_FORMAT::TIMESTAMP, timestamp, frame, "reset");
				break;
			default:
				break;
		}
		return false;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "showNoteMsg", json_boolean(showNoteMsg));
		json_object_set_new(rootJ, "showKeyPressure", json_boolean(showKeyPressure));
		json_object_set_new(rootJ, "showCcMsg", json_boolean(showCcMsg));
		json_object_set_new(rootJ, "showCcExMsg", json_boolean(showCcExMsg));
		json_object_set_new(rootJ, "showRpnNrpnMsg", json_boolean(showRpnNrpnMsg));
		json_object_set_new(rootJ, "showProgChangeMsg", json_boolean(showProgChangeMsg));
		json_object_set_new(rootJ, "showChannelPressurelMsg", json_boolean(showChannelPressurelMsg));
		json_object_set_new(rootJ, "showPitchWheelMsg", json_boolean(showPitchWheelMsg));

		json_object_set_new(rootJ, "showSysExMsg", json_boolean(showSysExMsg));
		json_object_set_new(rootJ, "showSysExData", json_boolean(showSysExData));
		json_object_set_new(rootJ, "showClockMsg", json_boolean(showClockMsg));
		json_object_set_new(rootJ, "showSystemMsg", json_boolean(showSystemMsg));
		json_object_set_new(rootJ, "showFrame", json_boolean(showFrame));

		json_object_set_new(rootJ, "midiInput", midiProcessor.getInput().toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* showNoteMsgJ = json_object_get(rootJ, "showNoteMsg");
		if (showNoteMsgJ) showNoteMsg = json_boolean_value(showNoteMsgJ);
		json_t* showKeyPressureJ = json_object_get(rootJ, "showKeyPressure");
		if (showKeyPressureJ) showKeyPressure = json_boolean_value(showKeyPressureJ);
		json_t* showCcMsgJ = json_object_get(rootJ, "showCcMsg");
		if (showCcMsgJ) showCcMsg = json_boolean_value(showCcMsgJ);
		json_t* showCcExMsgJ = json_object_get(rootJ, "showCcExMsg");
		showCcExMsg = showCcExMsgJ ? json_boolean_value(showCcExMsgJ) : showCcMsg;
		json_t* showRpnNrpnMsgJ = json_object_get(rootJ, "showRpnNrpnMsg");
		if (showRpnNrpnMsgJ) showRpnNrpnMsg = json_boolean_value(showRpnNrpnMsgJ);
		json_t* showProgChangeMsgJ = json_object_get(rootJ, "showProgChangeMsg");
		if (showProgChangeMsgJ) showProgChangeMsg = json_boolean_value(showProgChangeMsgJ);
		json_t* showChannelPressurelMsgJ = json_object_get(rootJ, "showChannelPressurelMsg");
		if (showChannelPressurelMsgJ) showChannelPressurelMsg = json_boolean_value(showChannelPressurelMsgJ);
		json_t* showPitchWheelMsgJ = json_object_get(rootJ, "showPitchWheelMsg");
		if (showPitchWheelMsgJ) showPitchWheelMsg = json_boolean_value(showPitchWheelMsgJ);
		json_t* showSysExMsgJ = json_object_get(rootJ, "showSysExMsg");
		if (showSysExMsgJ) showSysExMsg = json_boolean_value(showSysExMsgJ);
		json_t* showSysExDataJ = json_object_get(rootJ, "showSysExData");
		if (showSysExDataJ) showSysExData = json_boolean_value(showSysExDataJ);
		json_t* showClockMsgJ = json_object_get(rootJ, "showClockMsg");
		if (showClockMsgJ) showClockMsg = json_boolean_value(showClockMsgJ);
		json_t* showSystemMsgJ = json_object_get(rootJ, "showSystemMsg");
		if (showSystemMsgJ) showSystemMsg = json_boolean_value(showSystemMsgJ);
		json_t* showFrameJ = json_object_get(rootJ, "showFrame");
		if (showFrameJ) showFrame = json_boolean_value(showFrameJ);

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ) midiProcessor.getInput().fromJson(midiInputJ);
	}
};


struct LogDisplay : LedTextDisplay {
	std::list<LogEntry>* buffer;
	bool* showFrame = nullptr;
	bool dirty = true;

	LogDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		bgColor.a = 0.f;
		fontSize = 9.2f;
		textOffset.y += 2.f;
	}

	void step() override {
		LedTextDisplay::step();
		if (dirty) {
			text = "";
			size_t size = std::min(buffer->size(), (size_t)(box.size.x / fontSize) + 1);
			size_t i = 0;
			bool frameMode = showFrame && *showFrame;
			for (LogEntry s : *buffer) {
				if (i >= size) break;
				LOG_FORMAT f = std::get<0>(s);
				float timestamp = std::get<1>(s);
				int64_t frame = std::get<2>(s);
				switch (f) {
					case LOG_FORMAT::TIMESTAMP:
						if (frameMode)
							text += string::f("[%9" PRId64 "] %s\n", frame, std::get<3>(s).c_str());
						else
							text += string::f("[%9.4f] %s\n", timestamp, std::get<3>(s).c_str());
						break;
					case LOG_FORMAT::TEXT:
						text += string::f("%s\n", std::get<3>(s).c_str());
						break;
					case LOG_FORMAT::INDENTED:
						text += string::f("     %s\n", std::get<3>(s).c_str());
						break;
					default:
						break;
				};
			}
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}
};

struct MidiMonWidget : ThemedModuleWidget<MidiMonModule> {
	MidiMonModule* module;
	LogDisplay* logDisplay;
	std::list<LogEntry> buffer;
	
	MidiMonWidget(MidiMonModule* module)
		: ThemedModuleWidget<MidiMonModule>(module, "MidiMon") {
		this->module = module;
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget<>* midiInputWidget = createWidget<MidiWidget<>>(Vec(0.f, 36.4f));
		midiInputWidget->box.size = Vec(240.f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiProcessor.getInput() : NULL, "In");
		addChild(midiInputWidget);

		LedDisplay* textDisplay = createWidget<LedDisplay>(Vec(0.f, 107.4f));
		textDisplay->box.size = Vec(240.f, 236.0f);
		addChild(textDisplay);

		logDisplay = createWidget<LogDisplay>(Vec());
		logDisplay->buffer = &buffer;
		logDisplay->box.size = textDisplay->box.size.minus(Vec(0.f, 4.f));
		textDisplay->addChild(logDisplay);

		if (!module) {
			// fake data for module browser
			std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			char buf[100] = {0};
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
			buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, (int64_t)0, std::string(buf)));
			buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, (int64_t)0, string::f("sample rate %i", int(APP->engine->getSampleRate()))));
			buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, (int64_t)0, string::f("ch%i cc%i=%i", 5, 33, 101)));
			buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, (int64_t)0, string::f("ch%i note on  %i vel %i", 6, 41, 66)));
			buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, (int64_t)0, string::f("ch%i note off %i vel %i", 3, 66, 83)));
			buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, (int64_t)0, string::f("ch%i cc%i=%i", 3, 20, 4)));
			buffer.push_front(std::make_tuple(LOG_FORMAT::TIMESTAMP, 0.f, (int64_t)0, string::f("ch%i cc%i=%i", 3, 63, 52)));
		}
	}

	void step() override {
		ThemedModuleWidget<MidiMonModule>::step();
		if (!module) return;
		logDisplay->showFrame = &module->showFrame;
		while (!module->midiLogMessages.empty()) {
			if (buffer.size() == BUFFERSIZE) buffer.pop_back();
			auto s = module->midiLogMessages.shift();
			buffer.push_front(s);
		}
		logDisplay->dirty = true;
		logDisplay->setSize(Vec(240.f, std::max(236.0f, buffer.size() * 16.f)));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiMonModule>::appendContextMenu(menu);
		MidiMonModule* module = dynamic_cast<MidiMonModule*>(this->module);

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("MIDI channel messages", "", [=](Menu* menu) {
			menu->addChild(createBoolPtrMenuItem("Note on/off", "", &module->showNoteMsg));
			menu->addChild(createBoolPtrMenuItem("Key pressure", "", &module->showKeyPressure));
			menu->addChild(createBoolPtrMenuItem("CC", "", &module->showCcMsg));
			menu->addChild(createBoolPtrMenuItem("CC (14-bit)", "", &module->showCcExMsg));
			menu->addChild(createBoolPtrMenuItem("CC (RPN/NRPN)", "", &module->showRpnNrpnMsg));
			menu->addChild(createBoolPtrMenuItem("Program change", "", &module->showProgChangeMsg));
			menu->addChild(createBoolPtrMenuItem("Channel pressure", "", &module->showChannelPressurelMsg));
			menu->addChild(createBoolPtrMenuItem("Pitch wheel", "", &module->showPitchWheelMsg));
		}));
#ifndef METAMODULE
		menu->addChild(createSubmenuItem("MIDI system messages", "", [=](Menu* menu) {
			menu->addChild(createBoolPtrMenuItem("Clock", "", &module->showClockMsg));
			menu->addChild(createBoolPtrMenuItem("Other", "", &module->showSystemMsg));
			menu->addChild(createBoolPtrMenuItem("SysEx", "", &module->showSysExMsg));
			menu->addChild(createBoolPtrMenuItem("SysEx Data", "", &module->showSysExData));
		}));
#endif
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Show engine frame", "", &module->showFrame));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Clear log", "", [this]() { resetLog(); }));
#ifndef METAMODULE
		menu->addChild(createMenuItem("Export log", "", [this]() { exportLogDialog(); }));
#endif
	}

	void resetLog() {
		buffer.clear();
		module->logTimestampReset();
		logDisplay->reset();
	}

#ifndef METAMODULE
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
		fputs(string::f("MIDI driver: %s\n", module->midiProcessor.getInput().getDriver()->getName().c_str()).c_str(), file);
		fputs(string::f("MIDI device: %s\n", module->midiProcessor.getInput().getDeviceName(module->midiProcessor.getInput().deviceId).c_str()).c_str(), file);
		fputs(string::f("MIDI channel: %s\n", module->midiProcessor.getInput().getChannelName(module->midiProcessor.getInput().channel).c_str()).c_str(), file);
		fputs("--------------------------------------------------------------------\n", file);

		bool frameMode = module->showFrame;
		for (auto rit = buffer.rbegin(); rit != buffer.rend(); rit++) {
			auto s = *rit;
			LOG_FORMAT f = std::get<0>(s);
			float timestamp = std::get<1>(s);
			int64_t frame = std::get<2>(s);
			switch (f) {
				case LOG_FORMAT::TIMESTAMP:
					if (frameMode)
						fputs(string::f("[%15" PRId64 "] %s\n", frame, std::get<3>(s).c_str()).c_str(), file);
					else
						fputs(string::f("[%11.4f] %s\n", timestamp, std::get<3>(s).c_str()).c_str(), file);
					break;
				case LOG_FORMAT::TEXT:
					fputs(string::f("%s\n", std::get<3>(s).c_str()).c_str(), file);
					break;
				case LOG_FORMAT::INDENTED:
					fputs(string::f("                       %s\n", std::get<3>(s).c_str()).c_str(), file);
					break;
				default:
					break;
			}
		}
	}

	void exportLogDialog() {
		std::string log = asset::user("MidiMon.log");
		std::string dir = system::getDirectory(log);
		std::string filename = system::getFilename(log);

		char* path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), filename.c_str(), NULL);
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
#endif
};

} // namespace MidiMon
} // namespace StoermelderPackOne

Model* modelMidiMon = createModel<StoermelderPackOne::MidiMon::MidiMonModule, StoermelderPackOne::MidiMon::MidiMonWidget>("MidiMon");