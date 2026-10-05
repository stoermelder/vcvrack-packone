#include "../../plugin.hpp"
#include "../../vcv/api.hpp"
#include "../../components/LedTextDisplay.hpp"
#include "../../components/MidiWidget.hpp"
#include <list>
#include <sstream>
#include <iomanip>
#include <chrono>
#include "MidiProcessor.hpp"
#include "MidiText.hpp"

namespace StoermelderPackOne {
namespace MidiMon {

const int BUFFERSIZE = 800;

enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

/** A formatted log line, as shown by the widget. */
using LogEntry = std::tuple<LOG_FORMAT, float, int64_t, std::string>;

/** A log line as the dsp thread records it: plain data, no strings. Building a
 *  std::string allocates, which the dsp thread must not do, so the text is only
 *  assembled from this on the UI thread (LogDecoder). */
struct RawEntry {
	enum class Kind : uint8_t {
		MESSAGE,			// text = the message
		DATE,				// frame = time_t
		SAMPLE_RATE,		// text.x = sample rate
		SYSEX_DATA			// bytes[0..count), more = the line continues in the next entry
	};
	enum { SYSEX_CHUNK = 24 };

	Kind kind = Kind::MESSAGE;
	LOG_FORMAT format = LOG_FORMAT::TIMESTAMP;
	bool more = false;
	uint8_t count = 0;
	MidiText::Fields text;
	float timestamp = 0.f;
	int64_t frame = 0;
	uint8_t bytes[SYSEX_CHUNK];
};

/** Turns RawEntry records into LogEntry lines. UI thread (or tests) only. */
struct LogDecoder {
	std::string sysexLine;

	template <typename F>
	void decode(const RawEntry& r, F emit) {
		using K = RawEntry::Kind;
		std::string s;
		switch (r.kind) {
			case K::MESSAGE:
				s = MidiText::format(r.text);
				break;
			case K::DATE: {
				std::time_t t = (std::time_t)r.frame;
				char buf[100] = {0};
				std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
				s = buf;
				break;
			}
			case K::SAMPLE_RATE:
				s = string::f("sample rate %i", r.text.x);
				break;
			case K::SYSEX_DATA: {
				// Every chunk ends in a space, so the next one continues the line.
				sysexLine += MidiText::hexBytes(r.bytes, r.count) + " ";
				if (r.more) return;
				s = std::move(sysexLine);
				sysexLine.clear();
				break;
			}
		}
		emit(LogEntry(r.format, r.timestamp, r.kind == RawEntry::Kind::DATE ? 0LL : r.frame, std::move(s)));
	}
};

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
	// The lock-free input queue: the audio thread never takes a lock or frees here.
	MidiCProcessor midiProcessor;

	ClockDividerEx processDivider;
	dsp::RingBuffer<RawEntry, 4096> midiLogMessages;
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

	/** Dsp thread: plain data only, no allocation. */
	void logMessage(bool showMessage, LOG_FORMAT format, const MessageEx& m, const MidiText::Fields& text) {
		if (!showMessage || midiLogMessages.full()) return;
		RawEntry r;
		r.kind = RawEntry::Kind::MESSAGE;
		r.format = format;
		r.text = text;
		r.timestamp = format == LOG_FORMAT::TIMESTAMP ? float(m.frame) / APP->engine->getSampleRate() : 0.f;
		r.frame = format == LOG_FORMAT::TIMESTAMP ? m.frame : 0LL;
		midiLogMessages.push(r);
	}

	void logTimestampReset() {
		RawEntry r;
		r.format = LOG_FORMAT::TIMESTAMP;
		r.kind = RawEntry::Kind::DATE;
		r.frame = (int64_t)std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		if (!midiLogMessages.full()) midiLogMessages.push(r);
		r.kind = RawEntry::Kind::SAMPLE_RATE;
		r.frame = 0;
		r.text.x = int(APP->engine->getSampleRate());
		if (!midiLogMessages.full()) midiLogMessages.push(r);
	}

	// MidiProcessorHandler
	bool processMidi(const MessageEx& m) override {
		using K = MidiText::Kind;
		const LOG_FORMAT TS = LOG_FORMAT::TIMESTAMP;
		const LOG_FORMAT IND = LOG_FORMAT::INDENTED;
		const MidiText::Fields f = MidiText::classify(m);
		switch (f.kind) {
			case K::NOTE_ON:
			case K::NOTE_OFF:
				logMessage(showNoteMsg, TS, m, f);
				break;
			case K::KEY_PRESSURE:
				logMessage(showKeyPressure, TS, m, f);
				break;
			case K::CC:
				logMessage(showCcMsg, TS, m, f);
				break;
			case K::CC_14BIT:
				logMessage(showCcExMsg, IND, m, f);
				break;
			case K::RPN_RESET:
			case K::RPN_VALUE:
			case K::RPN_PARAM:
			case K::NRPN_VALUE:
			case K::NRPN_PARAM:
				logMessage(showRpnNrpnMsg, IND, m, f);
				break;
			case K::PROGRAM_CHANGE:
				logMessage(showProgChangeMsg, TS, m, f);
				break;
			case K::CHANNEL_PRESSURE:
				logMessage(showChannelPressurelMsg, TS, m, f);
				break;
			case K::PITCH_BEND:
				logMessage(showPitchWheelMsg, TS, m, f);
				break;
			case K::SYSEX: {
				int size = m.getSysExSize();
				int chunks = showSysExData ? (size + RawEntry::SYSEX_CHUNK - 1) / RawEntry::SYSEX_CHUNK : 0;
				// All or nothing: a partial data line would glue onto the next entry.
				if (midiLogMessages.capacity() < (size_t)(chunks + (showSysExMsg ? 1 : 0))) break;
				logMessage(showSysExMsg, TS, m, f);
				for (int i = 0; i < chunks; i++) {
					RawEntry r;
					r.kind = RawEntry::Kind::SYSEX_DATA;
					r.format = LOG_FORMAT::TEXT;
					r.count = (uint8_t)std::min((int)RawEntry::SYSEX_CHUNK, size - i * (int)RawEntry::SYSEX_CHUNK);
					r.more = i + 1 < chunks;
					for (int j = 0; j < r.count; j++) r.bytes[j] = m.getSysExByte(i * RawEntry::SYSEX_CHUNK + j);
					midiLogMessages.push(r);
				}
				break;
			}
			case K::SONG_POINTER:
			case K::SONG_SELECT:
			case K::START:
			case K::CONTINUE:
			case K::STOP:
			case K::RESET:
				logMessage(showSystemMsg, TS, m, f);
				break;
			case K::CLOCK:
				logMessage(showClockMsg, TS, m, f);
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


// The log text, newest line first. It is the content of a ScrollWidget, so it is as
// tall as all its lines and scrolling, clipping and the scrollbar are the ScrollWidget's.
struct LogDisplay : LedTextDisplay {
	std::list<LogEntry>* buffer;
	bool* showFrame = nullptr;
	bool dirty = true;
	// At least the height of the ScrollWidget's viewport.
	float minHeight = 0.f;

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
			float lines = 0.f;
			bool frameMode = showFrame && *showFrame;
			for (LogEntry s : *buffer) {
				LOG_FORMAT f = std::get<0>(s);
				float timestamp = std::get<1>(s);
				int64_t frame = std::get<2>(s);
				switch (f) {
					case LOG_FORMAT::TIMESTAMP:
						lines += 1.f;
						if (frameMode)
							text += string::f("[%9" PRId64 "] %s\n", frame, std::get<3>(s).c_str());
						else
							text += string::f("[%9.4f] %s\n", timestamp, std::get<3>(s).c_str());
						break;
					case LOG_FORMAT::TEXT:
						lines += 1.f;
						text += string::f("%s\n", std::get<3>(s).c_str());
						break;
					case LOG_FORMAT::INDENTED:
						lines += 1.f;
						text += string::f("     %s\n", std::get<3>(s).c_str());
						break;
					default:
						break;
				};
			}
			// A line is one fontSize high; a long one wraps into more, which only the
			// font can measure, so without a window the entries are counted.
			float h = lines * fontSize;
			if (APP->window && !text.empty()) {
				std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
				NVGcontext* vg = APP->window->vg;
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, fontSize);
				float bounds[4];
				nvgTextBoxBounds(vg, textOffset.x, textOffset.y + fontSize, box.size.x - 2 * textOffset.x, text.c_str(), NULL, bounds);
				h = std::max(h, bounds[3] - bounds[1]);
			}
			box.size.y = std::max(minHeight, h + 2.f * textOffset.y);
			dirty = false;
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
	rack::ui::ScrollWidget* logScroll;
	std::list<LogEntry> buffer;
	LogDecoder decoder;
	bool lastFrameMode = false;

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

		// The lines scroll, newest on top; the scrollbar shows once they outgrow the area.
		logScroll = new rack::ui::ScrollWidget;
		logScroll->box.pos.y = 3.f;
		// A narrow scrollbar is drawn partly beyond its box: stay clear of the right edge.
		logScroll->box.size = Vec(textDisplay->box.size.x - 3.f, textDisplay->box.size.y - 2.f * logScroll->box.pos.y);
		logScroll->verticalScrollbar->box.size.x = 8.f;
		logScroll->horizontalScrollbar->hide();
		textDisplay->addChild(logScroll);

		logDisplay = new LogDisplay;
		logDisplay->buffer = &buffer;
		logDisplay->box.size = Vec(logScroll->box.size.x - logScroll->verticalScrollbar->box.size.x, logScroll->box.size.y);
		logDisplay->minHeight = logScroll->box.size.y;
		logScroll->container->addChild(logDisplay);

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
		bool frameMode = module->showFrame;
		if (frameMode != lastFrameMode) {
			lastFrameMode = frameMode;
			logDisplay->dirty = true;
		}
		while (!module->midiLogMessages.empty()) {
			RawEntry r = module->midiLogMessages.shift();
			decoder.decode(r, [&](LogEntry&& e) {
				if (buffer.size() == BUFFERSIZE) buffer.pop_back();
				buffer.push_front(std::move(e));
				logDisplay->dirty = true;
				// A view scrolled back to older lines stays on them as the new one pushes them down.
				if (logScroll->offset.y > 0.f) logScroll->offset.y += logDisplay->fontSize;
			});
		}
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
		logScroll->offset = Vec();
	}

#ifndef METAMODULE
	void exportLog(std::string filename) {
		INFO("Saving file %s", filename.c_str());

		// Build the whole log in memory, then write it in one call.
		std::ostringstream ss;
		ss << rack::APP_NAME << " v" << rack::APP_VERSION << "\n";
		ss << system::getOperatingSystemInfo() << "\n";
		// MIDI may be unconfigured (no driver/device/channel); mirror the MidiWidget's
		// display fallbacks so export never dereferences a NULL driver.
		auto& input = module->midiProcessor.getInput();
		auto* driver = input.getDriver();
		ss << "MIDI driver: " << (driver ? driver->getName() : "(No driver)") << "\n";
		ss << "MIDI device: " << (input.deviceId >= 0 ? input.getDeviceName(input.deviceId) : "(No device)") << "\n";
		ss << "MIDI channel: " << (input.channel >= 0 ? input.getChannelName(input.channel) : "(All channels)") << "\n";
		ss << "--------------------------------------------------------------------\n";

		bool frameMode = module->showFrame;
		for (auto rit = buffer.rbegin(); rit != buffer.rend(); rit++) {
			auto s = *rit;
			LOG_FORMAT f = std::get<0>(s);
			float timestamp = std::get<1>(s);
			int64_t frame = std::get<2>(s);
			switch (f) {
				case LOG_FORMAT::TIMESTAMP:
					if (frameMode)
						ss << "[" << std::setw(15) << frame << "] " << std::get<3>(s) << "\n";
					else
						ss << "[" << std::fixed << std::setprecision(4) << std::setw(11) << timestamp << "] " << std::get<3>(s) << "\n";
					break;
				case LOG_FORMAT::TEXT:
					ss << std::get<3>(s) << "\n";
					break;
				case LOG_FORMAT::INDENTED:
					ss << "                       " << std::get<3>(s) << "\n";
					break;
				default:
					break;
			}
		}

		if (!vcv::fs::write(filename, ss.str())) {
			std::string message = string::f("Could not write to file %s", filename.c_str());
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, message.c_str());
		}
	}

	void exportLogDialog() {
		std::string log = asset::user("MidiMon.log");
		std::string dir = vcv::fs::getDirectory(log);
		std::string filename = vcv::fs::getFilename(log);

		std::string path = vcv::ui::saveDialog("Log file (.log):log;Text file (.txt):txt", dir, filename);
		if (path.empty()) {
			// No path selected
			return;
		}

		exportLog(path);
	}
#endif
};

} // namespace MidiMon
} // namespace StoermelderPackOne

Model* modelMidiMon = createModel<StoermelderPackOne::MidiMon::MidiMonModule, StoermelderPackOne::MidiMon::MidiMonWidget>("MidiMon");