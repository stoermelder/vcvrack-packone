#include "MidiScriptEngine.h"
#include "elk.h"
#include "../../utils/TaskWorker.hpp"
#include <iomanip>
#include <regex>

namespace StoermelderPackOne {
namespace MidiScript {
namespace Elk {

struct MidiScriptEngineElk : MidiScriptEngine {
	struct MessageEx {
		int midiPort = 0;
		Message msg;
		bool isNrpn = false;
		bool send = false;
		uint64_t tick = 0;
	};

	static std::map<struct js*, MidiScriptEngineElk*> jsMap;

	char jsMem[64 * 1024];
	struct js* js = NULL;

	std::shared_ptr<ITaskWorker> taskWorker;
	dsp::RingBuffer<std::tuple<int, Message>, 128> midiInQueue;
	dsp::RingBuffer<std::tuple<int, Message, uint64_t>, 128> midiOutQueue;

	const static int msgStoreSize = 32;
	MessageEx msgStore[msgStoreSize];
	size_t msgCount;

	void setWorker(std::shared_ptr<ITaskWorker> w) { taskWorker = std::move(w); }

	void runAsync(std::function<void()> task) override {
		taskWorker->work(task, APP);
	}

	void loadScript(const char* script) override {
		if (js != NULL) {
			jsMap.erase(js);
			js = NULL;
			// no need for free() here as "js" completely operates in jsMem
		}

		if (script[0] == '\0') {
			writeLog("No script", false);
			return;
		}

		// Analyze file header of this pattern:
		//	/**
		//	 * @target stoermelder MIDI-KIT
		//	 * @engine Elk
		//	 * @author stoermelder
		//	 * @description Routes incoming CC messages on MIDI channel 1 to a MIDI channel set by parameter 1 on the panel
		//	 */
		//

		std::map<std::string, std::string> topics;
		std::string str = script;
		// remove " * " trailing in comment lines
		str = std::regex_replace(str, std::regex(R"(\n\s+\*\s+)"), "");
		// remove remaining newlines
		str = std::regex_replace(str, std::regex(R"(\n)"), "");
		// match /** ... */ on the beginning of the string
		const std::regex header_regex(R"(^\/\*\*(.*)\*\/.*$)");
		std::smatch m1;
		if (std::regex_search(str, m1, header_regex)) {
			std::string header = m1[1].str();
			// match items in header according to "@topic text"
			const std::regex at_regex(R"(@([a-z]+)\s([^@]*))");
			auto words_begin = std::sregex_iterator(header.begin(), header.end(), at_regex);
			auto words_end = std::sregex_iterator();
			for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
				std::smatch m2 = *i;
				std::string topic = m2[1].str();
				std::string text = m2[2].str();
				topics[topic] = text;
			}
		}

		if (topics.find("engine") == topics.end() || topics["engine"] != "Elk") {
			writeLog("Script is not compatible with MIDI-KIT", false);
			return;
		}

		if (topics.find("author") != topics.end()) {
			writeLog(string::f("Author: %s", topics["author"].c_str()), false);
		}
		if (topics.find("description") != topics.end()) {
			writeLog(topics["description"], false);
		}

		js = js_create(jsMem, sizeof(jsMem));
		jsMap[js] = this;

		// global functions
		js_set(js, js_glob(js), "log", js_mkfun(js_log));									// void log(string)
		js_set(js, js_glob(js), "overlay", js_mkfun(js_overlay));							// void overlay(string)

		// number
		jsval_t _number = js_mkobj(js);														// let number = {}
		js_set(js, js_glob(js), "number", _number);
		js_set(js, _number, "abs", js_mkfun(js_number_abs));
		js_set(js, _number, "ceil", js_mkfun(js_number_ceil));
		js_set(js, _number, "crossfade", js_mkfun(js_number_crossfade));
		js_set(js, _number, "floor", js_mkfun(js_number_floor));
		js_set(js, _number, "max", js_mkfun(js_number_max));
		js_set(js, _number, "min", js_mkfun(js_number_min));
		js_set(js, _number, "random", js_mkfun(js_number_random));
		js_set(js, _number, "rescale", js_mkfun(js_number_rescale));
		js_set(js, _number, "toString", js_mkfun(js_number_toString));

		// input
		jsval_t _input = js_eval(js,														// let input = {}
			"let input = {"
			"	getName: function(i) { return \"Port \" + number.toString(i); }"
			"};"
			"input;", ~0U);
		js_set(js, _input, "enable", js_mkfun(js_input_enable));							// void input.enable(int)
		js_set(js, _input, "getVoltage", js_mkfun(js_input_getVoltage));					// float input.getVoltage(int, [int])
		js_set(js, _input, "isHigh", js_mkfun(js_input_isHigh));							// bool input.isHigh(int, [int])
		js_set(js, _input, "isLow", js_mkfun(js_input_isLow));								// bool input.isLow(int, [int])

		// trig
		jsval_t _trig = js_mkobj(js);
		js_set(js, js_glob(js), "trig", _trig);												// let trig = {}
		js_set(js, _trig, "getTicks", js_mkfun(js_trig_getTicks));							// bool trig.getTicks(int, [int])
		js_set(js, _trig, "isHigh", js_mkfun(js_trig_isHigh));								// bool trig.isHigh(int, [int])
		js_set(js, _trig, "isLow", js_mkfun(js_trig_isLow));								// bool trig.isLow(int, [int])
		js_set(js, _trig, "setGate", js_mkfun(js_trig_setGate));							// void trig.setGate(int, [int], float)
		js_set(js, _trig, "setHigh", js_mkfun(js_trig_setHigh));							// void trig.setHigh(int, [int])
		js_set(js, _trig, "setLow", js_mkfun(js_trig_setLow));								// void trig.setLow(int, [int])
		js_set(js, _trig, "setTrigger", js_mkfun(js_trig_setTrigger));						// void trig.setTrigger(int, [int])

		// param
		jsval_t _param = js_eval(js,
			"let param = {"
			"	getName: function(i) { return \"Param \" + number.toString(i); },"
			"	getValueFormat: function(i) { return \"\"; }"
			"};"
			"param;", ~0U);
		js_set(js, _param, "enable", js_mkfun(js_param_enable));							// void param.enable(int)
		js_set(js, _param, "getValue", js_mkfun(js_param_getValue));						// float param.getValue(int)

		// midi
		jsval_t _midi = js_mkobj(js);
		js_set(js, js_glob(js), "midi", _midi);												// let midi = {}
		js_set(js, _midi, "create", js_mkfun(js_midi_create));								// let msg = midi.create()
		js_set(js, _midi, "createNRPN", js_mkfun(js_midi_createNrpn));						// let nrpn = midi.createNrpn()
		js_set(js, _midi, "getChannel", js_mkfun(js_midi_getChannel));						// int midi.getChannel(msg)
		js_set(js, _midi, "getLength", js_mkfun(js_midi_getLength));						// int midi.getLength(msg)
		js_set(js, _midi, "getNote", js_mkfun(js_midi_getNote));							// int midi.getNote(msg)
		js_set(js, _midi, "getPitchWheel", js_mkfun(js_midi_getPitchWheel));				// int midi.getPitchWheel(msg)
		//js_set(js, _midi, "getType", js_mkfun(js_midi_getType));							// int midi.getType(msg)
		js_set(js, _midi, "getSysExData", js_mkfun(js_midi_getSysExData));					// string midi.getSysExData(msg)
		js_set(js, _midi, "getValue", js_mkfun(js_midi_getValue));							// int midi.getValue(msg)
		js_set(js, _midi, "isCc", js_mkfun(js_midi_isCc));									// bool midi.isCc(msg)
		js_set(js, _midi, "isChanPressure", js_mkfun(js_midi_isChanPressure));				// bool midi.isChannelPressure(msg)
		js_set(js, _midi, "isClock", js_mkfun(js_midi_isClock));							// bool midi.isClock(msg)
		js_set(js, _midi, "isContinue", js_mkfun(js_midi_isContinue));						// bool midi.isCcontinue(msg)
		js_set(js, _midi, "isKeyPressure", js_mkfun(js_midi_isKeyPressure));				// bool midi.isKeyPressure(msg)
		js_set(js, _midi, "isNoteOff", js_mkfun(js_midi_isNoteOff));						// bool midi.isNoteOff(msg)
		js_set(js, _midi, "isNoteOn", js_mkfun(js_midi_isNoteOn));							// bool midi.isNoteOn(msg)
		js_set(js, _midi, "isProgramChange", js_mkfun(js_midi_isProgramChange));			// bool midi.isProgramChange(msg)
		js_set(js, _midi, "isPitchWheel", js_mkfun(js_midi_isPitchWheel));					// bool midi.isPitchWheel(msg)
		js_set(js, _midi, "isStart", js_mkfun(js_midi_isStart));							// bool midi.isStart(msg)
		js_set(js, _midi, "isStop", js_mkfun(js_midi_isStop));								// bool midi.isStop(msg)
		js_set(js, _midi, "isSysEx", js_mkfun(js_midi_isSysEx));							// bool midi.isSysEx(msg)
		js_set(js, _midi, "setCc", js_mkfun(js_midi_setCc));								// void midi.setCc(msg, channel, cc, value)
		js_set(js, _midi, "setCc14bit", js_mkfun(js_midi_setCc14bit));						// void midi.setCc14bit(msg1, msg2, channel, cc, value)
		js_set(js, _midi, "setChannel", js_mkfun(js_midi_setChannel));						// void midi.setChannel(msg, int)
		js_set(js, _midi, "setChanPressure", js_mkfun(js_midi_setChanPressure));			// void midi.setChannelPressure(msg, channel, value)
		js_set(js, _midi, "setKeyPressure", js_mkfun(js_midi_setKeyPressure));				// void midi.setKeyPressure(msg, channel, note, velocity)
		js_set(js, _midi, "setNote", js_mkfun(js_midi_setNote));							// void midi.setNote(msg, int)
		js_set(js, _midi, "setNoteOff", js_mkfun(js_midi_setNoteOff));						// void midi.setNoteOff(msg, channel, note)
		js_set(js, _midi, "setNoteOn", js_mkfun(js_midi_setNoteOn));						// void midi.setNoteOn(msg, channel, note, velocity)
		js_set(js, _midi, "setNRPN", js_mkfun(js_midi_setNrpn));							// void midi.setNrpn(nrpn, channel, number, value);
		js_set(js, _midi, "setPitchWheel", js_mkfun(js_midi_setPitchWheel));				// bool midi.setPitchWheel(msg, value)
		js_set(js, _midi, "setProgramChange", js_mkfun(js_midi_setProgramChange));			// void midi.setProgramChange(msg, channel, prg)
		//js_set(js, _midi, "setType", js_mkfun(js_midi_setType));							// void midi.setType(msg)
		js_set(js, _midi, "setSysEx", js_mkfun(js_midi_setSysEx));							// void midi.setSysEx(msg, string)
		js_set(js, _midi, "setValue", js_mkfun(js_midi_setValue));							// void midi.setValue(msg, int)

		// midiOut
		jsval_t _midiOut = js_mkobj(js);
		js_set(js, js_glob(js), "midiOut", _midiOut);										// let midiOut = {}
		js_set(js, _midiOut, "send", js_mkfun(js_midiOut_send));							// void midiOut.send([midiPort], msg)
		js_set(js, _midiOut, "sendAfterMs", js_mkfun(js_midiOut_sendAfterMs));				// void midiOut.sendAfterMs([midiPort], msg, ms)
		js_set(js, _midiOut, "sendAfterTrigger", js_mkfun(js_midiOut_sendAfterTrigger));	// void midiOut.sendAfterTrigger([midiPort], msg, [trigPort], ticks)

		jsval_t r = js_eval(js, script, ~0U);
		if (js_type(r) == JS_ERR) {
			writeLog(string::f("Error while loading script: %s", js_str(js, r)), false);
			js = NULL;
		}
		else {
			writeLog("Script loaded", false);
		}
	}

	void processInMessage(int midiPort, Message& msg) override {
		if (js) {
			midiInQueue.push(std::make_tuple(midiPort, msg));
		}
	}

	void process() override {
		if (js && midiInQueue.size() > 0) {
			runAsync([this]() {
				while (!midiInQueue.empty()) {
					auto t = midiInQueue.shift();
					int midiPort = std::get<0>(t);
					midi::Message msg = std::get<1>(t);
					process(midiPort, msg);
				}
			});
		}
	}

	bool processOutMessage(int& midiPort, Message& msg, int& ticks) override {
		if (js && !midiOutQueue.empty()) {
			auto t = midiOutQueue.shift();
			midiPort = std::get<0>(t);
			msg = std::get<1>(t);
			ticks = std::get<2>(t);
			return true;
		}
		return false;
	}

	void process(int midiPort, Message& msg) {
		if (js) {
			msgStore[0].msg = msg;
			msgStore[0].send = false;
			msgStore[0].tick = 0;
			msgCount = 1;

			jsval_t r = js_eval(js, string::f("processMidi(%i, 0)", midiPort + 1).c_str(), ~0U);
			if (js_type(r) == JS_ERR) {
				writeLog(js_str(js, r));
			}

			for (size_t i = 0; i < msgCount; i++) {
				if (msgStore[i].send) {
					midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i].msg, msgStore[i].tick));
					if (msgStore[i].isNrpn) {
						midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i + 1].msg, msgStore[i].tick));
						midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i + 2].msg, msgStore[i].tick));
						midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i + 3].msg, msgStore[i].tick));
						i += 3;
					}
				}
			}
		}
	}

	std::string getInputName(int i) override {
		if (js) {
			jsval_t r = js_eval(js, string::f("input.getName(%i);", i + 1).c_str(), ~0U);
			std::string s = js_getstr(js, r, NULL);
			return s;
		}
		return "";
	}

	std::string getParamName(int i) override {
		if (js) {
			jsval_t r = js_eval(js, string::f("param.getName(%i);", i + 1).c_str(), ~0U);
			std::string s = js_getstr(js, r, NULL);
			return s;
		}
		return "";
	}

	std::string getParamFormatValue(int i) override {
		if (js) {
			jsval_t r = js_eval(js, string::f("param.getValueFormat(%i);", i + 1).c_str(), ~0U);
			std::string s = js_getstr(js, r, NULL);
			return s;
		}
		return "";
	}


	inline static jsval_t js_mkbool(bool b) {
		return b ? js_mktrue() : js_mkfalse();
	}


	static jsval_t js_log(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "s")) return js_mkerr(js, "log: bad args");
		const char* log = js_getstr(js, args[0], NULL);
		jsMap[js]->writeLog(log);
		return js_mknull();
	}

	static jsval_t js_overlay(struct js* js, jsval_t* args, int nargs) {
		if (js_chkargs(args, nargs, "s")) {
			const char* s1 = js_getstr(js, args[0], NULL);
			jsMap[js]->writeOverlay(s1, "", "");
			return js_mknull();
		}
		if (js_chkargs(args, nargs, "ss")) {
			const char* s1 = js_getstr(js, args[0], NULL);
			const char* s2 = js_getstr(js, args[1], NULL);
			jsMap[js]->writeOverlay(s1, s2, "");
			return js_mknull();
		}
		if (js_chkargs(args, nargs, "sss")) {
			const char* s1 = js_getstr(js, args[0], NULL);
			const char* s2 = js_getstr(js, args[1], NULL);
			const char* s3 = js_getstr(js, args[2], NULL);
			jsMap[js]->writeOverlay(s1, s2, s3);
			return js_mknull();
		}
		return js_mkerr(js, "overlay: bad args");
	}

	// number

	static jsval_t js_number_abs(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "number.abs: bad args");
		float f = js_getnum(args[0]);
		return js_mknum(std::abs(f));
	}

	static jsval_t js_number_ceil(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "number.ceil: bad args");
		float f = js_getnum(args[0]);
		return js_mknum(std::ceil(f));
	}

	static jsval_t js_number_crossfade(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "ddd")) return js_mkerr(js, "number.crossfade: bad args");
		float a = js_getnum(args[0]);
		float b = js_getnum(args[1]);
		float p = js_getnum(args[2]);
		return js_mknum(rack::crossfade(a, b, p));
	}

	static jsval_t js_number_floor(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "number.floor: bad args");
		float f = js_getnum(args[0]);
		return js_mknum(std::floor(f));
	}

	static jsval_t js_number_max(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "dd")) return js_mkerr(js, "number.max: bad args");
		float f1 = js_getnum(args[0]);
		float f2 = js_getnum(args[1]);
		return js_mknum(std::max(f1, f2));
	}

	static jsval_t js_number_min(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "dd")) return js_mkerr(js, "number.min: bad args");
		float f1 = js_getnum(args[0]);
		float f2 = js_getnum(args[1]);
		return js_mknum(std::min(f1, f2));
	}

	static jsval_t js_number_random(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "")) return js_mkerr(js, "number.random: bad args");
		return js_mknum(rack::random::uniform());
	}

	static jsval_t js_number_rescale(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "ddddd") && !js_chkargs(args, nargs, "dddddd")) return js_mkerr(js, "number.rescale: bad args");
		float x = js_getnum(args[0]);
		float xMin = js_getnum(args[1]);
		float xMax = js_getnum(args[2]);
		float yMin = js_getnum(args[3]);
		float yMax = js_getnum(args[4]);
		if (nargs == 6) {
			float a = js_getnum(args[5]);
			x = rack::rescale(x, xMin, xMax, 1.f, M_E);
			x = std::exp(std::pow(std::log(x), dsp::exp2_taylor5(a)));
			x = rack::rescale(x, 1.f, M_E, yMin, yMax);
			return js_mknum(x);
		}
		else {
			return js_mknum(rack::rescale(x, xMin, xMax, yMin, yMax));
		}
	}

	static jsval_t js_number_toString(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "number.toString: bad args");
		float f = js_getnum(args[0]);
		char str[32];
		if (ceilf(f) == f)
			snprintf(str, sizeof(str), "%i", (int)f);
		else
			snprintf(str, sizeof(str), "%f", f);
		return js_mkstr(js, str, 6);
	}

	// input

	static jsval_t js_input_enable(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "input.enable: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputCount) return js_mkerr(js, "input.enable: bad index");
		jsMap[js]->enableInput(i - 1);
		return js_mknull();
	}

	static jsval_t js_input_getVoltage(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "input.getVoltage: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputCount) return js_mkerr(js, "input.getVoltage: bad index");
		uint8_t ch = 0;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "input.getVoltage: bad channel");
		return js_mknum(jsMap[js]->getInputVoltage(i - 1, ch - 1));
	}

	static jsval_t js_input_isHigh(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "input.isHigh: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputCount) return js_mkerr(js, "input.isHigh: bad index");
		uint8_t ch = 0;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "input.isHigh: bad channel");
		return js_mkbool(jsMap[js]->getInputVoltage(i - 1, ch - 1) > 0.7f);
	}

	static jsval_t js_input_isLow(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "input.isLow: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputCount) return js_mkerr(js, "input.isLow: bad index");
		uint8_t ch = 0;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "input.isLow: bad channel");
		return js_mkbool(jsMap[js]->getInputVoltage(i - 1, ch - 1) < 0.7f);
	}

	// trig

	static jsval_t js_trig_getTicks(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "trig.getTicks: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputTrigCount) return js_mkerr(js, "trig.getTicks: bad index");
		return js_mknum(jsMap[js]->getTrigTicks(i - 1));
	}

	static jsval_t js_trig_isHigh(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "trig.isHigh: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputTrigCount) return js_mkerr(js, "trig.isHigh: bad index");
		int ch = 1;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "trig.isHigh: bad channel");
		return js_mkbool(jsMap[js]->getTrigVoltage(i - 1, ch - 1) > 0.7f);
	}

	static jsval_t js_trig_isLow(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "trig.isLow: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputTrigCount) return js_mkerr(js, "trig.isLow: bad index");
		int ch = 1;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "trig.isLow: bad channel");
		return js_mkbool(jsMap[js]->getTrigVoltage(i - 1, ch - 1) < 0.7f);
	}

	static jsval_t js_trig_setGate(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "dd") && !js_chkargs(args, nargs, "ddd")) return js_mkerr(js, "trig.setGate: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->outputTrigCount) return js_mkerr(js, "trig.setGate: bad index");
		int ch = 1;
		if (nargs == 3) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "trig.setGate: bad channel");
		float duration = js_getnum(args[nargs - 1]);
		jsMap[js]->setTrig(i - 1, ch - 1, duration);
		return js_mknull();
	}

	static jsval_t js_trig_setHigh(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "trig.setHigh: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->outputTrigCount) return js_mkerr(js, "trig.setHigh: bad index");
		int ch = 1;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "trig.setHigh: bad channel");
		jsMap[js]->setTrigVoltage(i - 1, ch - 1, 10.f);
		return js_mknull();
	}

	static jsval_t js_trig_setLow(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "trig.setHigh: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->outputTrigCount) return js_mkerr(js, "trig.setLow: bad index");
		int ch = 1;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "trig.setLow: bad channel");
		jsMap[js]->setTrigVoltage(i - 1, ch - 1, 0.f);
		return js_mknull();
	}

	static jsval_t js_trig_setTrigger(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "trig.setHigh: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->outputTrigCount) return js_mkerr(js, "trig.setTrigger: bad index");
		int ch = 1;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "trig.setHigh: bad channel");
		jsMap[js]->setTrig(i - 1, ch - 1);
		return js_mknull();
	}

	// param

	static jsval_t js_param_enable(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "param.enable: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->paramCount) return js_mkerr(js, "param.enable: bad index");
		jsMap[js]->enableParam(i - 1);
		return js_mknull();
	}

	static jsval_t js_param_getValue(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "param.getValue: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->paramCount) return js_mkerr(js, "param.getValue: bad index");
		return js_mknum(jsMap[js]->getParamValue(i - 1));
	}

	// midi

	inline static jsval_t js_midi(struct js* js, jsval_t* args, int nargs, const char* chkargs, const char* n, std::function<jsval_t(jsval_t*, MessageEx&)> f) {
		if (!js_chkargs(args, nargs, chkargs)) return js_mkerr(js, string::f("midi.%s: invalid msg", n).c_str());
		size_t idx = js_getnum(args[0]);
		if (idx > jsMap[js]->msgCount - 1) return js_mkerr(js, string::f("midi.%s: invalid msg", n).c_str());
		return f(args, jsMap[js]->msgStore[idx]);
	}

	inline static jsval_t js_midi_isType(struct js* js, jsval_t* args, int nargs, uint8_t t, const char* n) {
		return js_midi(js, args, nargs, "d", n, [t](jsval_t* args, MessageEx& s) {
			return js_mkbool(s.msg.getStatus() == t);
		});
	}

	static jsval_t js_midi_create(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "")) return js_mkerr(js, "midi.create: bad args");
		size_t* s = &jsMap[js]->msgCount;
		if (*s == msgStoreSize) return js_mkerr(js, "midi.create: maximum reached");
		jsMap[js]->msgStore[*s] = MessageEx();
		return js_mknum((*s)++);
	}

	static jsval_t js_midi_createNrpn(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "")) return js_mkerr(js, "midi.createNrpn: bad args");
		size_t* s = &jsMap[js]->msgCount;
		if (*s + 4 >= msgStoreSize) return js_mkerr(js, "midi.createNrpn: buffer maximum reached");
		jsMap[js]->msgStore[*s + 0] = MessageEx();
		jsMap[js]->msgStore[*s + 0].isNrpn = true;
		jsMap[js]->msgStore[*s + 1] = MessageEx();
		jsMap[js]->msgStore[*s + 2] = MessageEx();
		jsMap[js]->msgStore[*s + 3] = MessageEx();
		size_t _s = *s;
		(*s) += 4;
		return js_mknum(_s);
	}

	static jsval_t js_midi_getChannel(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getChannel", [](jsval_t* args, MessageEx& s) {
			// TODO: check for message type
			return js_mknum(s.msg.getChannel() + 1);
		});
	}

	static jsval_t js_midi_getLength(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getLength", [](jsval_t* args, MessageEx& s) {
			return js_mknum(s.msg.getSize());
		});
	}

	static jsval_t js_midi_getNote(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getNote", [](jsval_t* args, MessageEx& s) {
			return js_mknum(s.msg.getNote());
		});
	}

	static jsval_t js_midi_getPitchWheel(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getPitchWheel", [](jsval_t* args, MessageEx& s) {
			uint16_t value = ((uint16_t)s.msg.getValue() << 7) | s.msg.getNote();
			return js_mknum(value);
		});
	}

	static jsval_t js_midi_getSysExData(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getSysExData", [js](jsval_t* args, MessageEx& s) {
			std::ostringstream ss;
			ss << std::hex;
			for (int i = 1; i < s.msg.getSize() - 1; i++) {
				ss << std::setw(2) << std::setfill('0') << static_cast<int>(s.msg.bytes[i]);
			}
			std::string str = ss.str();
			return js_mkstr(js, str.c_str(), str.length());
		});
	}

	static jsval_t js_midi_getValue(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getValue", [](jsval_t* args, MessageEx& s) {
			return js_mknum(s.msg.getValue());
		});
	}

	static jsval_t js_midi_isCc(struct js* js, jsval_t* args, int nargs) {
		return js_midi_isType(js, args, nargs, 0xb, "isCc");
	}

	static jsval_t js_midi_isChanPressure(struct js* js, jsval_t* args, int nargs) {
		return js_midi_isType(js, args, nargs, 0xd, "isChannelPressure");
	}

	static jsval_t js_midi_isClock(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "isClock", [](jsval_t* args, MessageEx& s) {
			return js_mkbool(s.msg.getStatus() == 0xf && s.msg.getChannel() == 0x8);
		});
	}

	static jsval_t js_midi_isContinue(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "isContinue", [](jsval_t* args, MessageEx& s) {
			return js_mkbool(s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xb);
		});
	}

	static jsval_t js_midi_isKeyPressure(struct js* js, jsval_t* args, int nargs) {
		return js_midi_isType(js, args, nargs, 0xa, "isKeyPressure");
	}

	static jsval_t js_midi_isNoteOff(struct js* js, jsval_t* args, int nargs) {
		return js_midi_isType(js, args, nargs, 0x8, "isNoteOff");
	}

	static jsval_t js_midi_isNoteOn(struct js* js, jsval_t* args, int nargs) {
		return js_midi_isType(js, args, nargs, 0x9, "isNoteOn");
	}

	static jsval_t js_midi_isPitchWheel(struct js* js, jsval_t* args, int nargs) {
		return js_midi_isType(js, args, nargs, 0xe, "isPitchWheel");
	}

	static jsval_t js_midi_isProgramChange(struct js* js, jsval_t* args, int nargs) {
		return js_midi_isType(js, args, nargs, 0xc, "isProgramChange");
	}

	static jsval_t js_midi_isStart(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "isStart", [](jsval_t* args, MessageEx& s) {
			return js_mkbool(s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xa);
		});
	}

	static jsval_t js_midi_isStop(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "isStop", [](jsval_t* args, MessageEx& s) {
			return js_mkbool(s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xc);
		});
	}

	static jsval_t js_midi_isSysEx(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "isSysEx", [](jsval_t* args, MessageEx& s) {
			return js_mkbool(s.msg.getStatus() == 0xf && s.msg.getChannel() == 0x0);
		});
	}

	static jsval_t js_midi_setCc(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dddd", "setCc", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			uint8_t cc = js_getnum(args[2]);
			int8_t value = js_getnum(args[3]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0xb);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(cc);
			s.msg.setValue(value);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setCc14bit(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ddddd", "setCc", [js](jsval_t* args, MessageEx& s1) {
			size_t idx2 = js_getnum(args[1]);
			if (idx2 > jsMap[js]->msgCount - 1) return js_mkerr(js, string::f("midi.setCc14bit: invalid msg").c_str());
			MessageEx& s2 = jsMap[js]->msgStore[idx2];
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[2])));
			uint8_t cc = js_getnum(args[3]);
			double value = js_getnum(args[4]);
			if (s1.msg.getSize() != 3) s1.msg.setSize(3);
			if (s2.msg.getSize() != 3) s2.msg.setSize(3);
			s1.msg.setStatus(0xb);
			s2.msg.setStatus(0xb);
			s1.msg.setChannel(ch - 1);
			s2.msg.setChannel(ch - 1);
			s1.msg.setNote(cc);
			s2.msg.setNote(cc + 32);
			s1.msg.setValue((int8_t)value);
			s2.msg.setValue((int8_t)(((value - ((int8_t)value))) * 128.f));
			return js_mknull();
		});
	}

	static jsval_t js_midi_setChannel(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dd", "setChannel", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			s.msg.setChannel(ch - 1);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setChanPressure(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ddd", "setChanPressure", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			uint8_t value = js_getnum(args[2]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0xd);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(value);
			return js_mknull();
		});
	}


	static jsval_t js_midi_setKeyPressure(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dddd", "setKeyPressure", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			uint8_t note = js_getnum(args[2]);
			int8_t vel = js_getnum(args[3]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0xa);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(note);
			s.msg.setValue(vel);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setNote(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dd", "setNote", [](jsval_t* args, MessageEx& s) {
			uint8_t value = js_getnum(args[1]);
			s.msg.setNote(value);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setNoteOff(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ddd", "setNoteOff", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			uint8_t note = js_getnum(args[2]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0x8);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(note);
			s.msg.setValue(0);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setNoteOn(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dddd", "setNoteOn", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			uint8_t note = js_getnum(args[2]);
			int8_t vel = js_getnum(args[3]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0x9);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(note);
			s.msg.setValue(vel);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setNrpn(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "dddd")) return js_mkerr(js, "midi.setNrpn: invalid args");
		size_t idx = js_getnum(args[0]);
		if (idx > jsMap[js]->msgCount - 1) return js_mkerr(js, "midi.setNrpn: invalid msg");
		MessageEx* s1 = &jsMap[js]->msgStore[idx];
		if (!s1->isNrpn) return js_mkerr(js, "midi.setNrpn: invalid nrpn message");
		MessageEx* s2 = &jsMap[js]->msgStore[idx + 1];
		MessageEx* s3 = &jsMap[js]->msgStore[idx + 2];
		MessageEx* s4 = &jsMap[js]->msgStore[idx + 3];

		uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
		uint16_t number = js_getnum(args[2]);
		uint16_t value = js_getnum(args[3]);
		s1->msg.setStatus(0xb);
		s1->msg.setChannel(ch - 1);
		s1->msg.setNote(98);
		s1->msg.setValue(number & 0x7f);
		s2->msg.setStatus(0xb);
		s2->msg.setChannel(ch - 1);
		s2->msg.setNote(99);
		s2->msg.setValue((number >> 7) & 0x7f);
		s3->msg.setStatus(0xb);
		s3->msg.setChannel(ch - 1);
		s3->msg.setNote(38);
		s3->msg.setValue(value & 0x7f);
		s4->msg.setStatus(0xb);
		s4->msg.setChannel(ch - 1);
		s4->msg.setNote(6);
		s4->msg.setValue((value >> 7) & 0x7f);
		return js_mknull();
	}

	static jsval_t js_midi_setPitchWheel(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ddd", "setPitchWheel", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			uint16_t value = js_getnum(args[2]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0xe);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(value & 0x7f);
			s.msg.setValue((value >> 7) & 0x7f);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setProgramChange(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ddd", "setProgramChange", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max((uint8_t)1, std::min((uint8_t)16, (uint8_t)js_getnum(args[1])));
			uint8_t prg = js_getnum(args[2]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0xc);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(prg);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setSysEx(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ds", "setSysEx", [js](jsval_t* args, MessageEx& s) {
			std::string data = js_getstr(js, args[1], NULL);
			if (data.length() % 2 != 0)
				return js_mkerr(js, "midi.setSysEx: invalid string length");
			if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
				return js_mkerr(js, "midi.setSysEx: invalid hexstring");
			data.erase(std::remove_if(data.begin(), data.end(), ::isspace), data.end());
			s.msg.setSize(data.length() / 2 + 2);
			s.msg.bytes[0] = 0xf0;
			for (size_t i = 0; i < data.length(); i += 2) {
				std::string bs = data.substr(i, 2);
				char byte = (char)strtol(bs.c_str(), NULL, 16);
				s.msg.bytes[i / 2 + 1] = byte;
			}
			s.msg.bytes[s.msg.getSize() - 1] = 0xf7;
			return js_mknull();
		});
	}

	static jsval_t js_midi_setValue(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dd", "setValue", [](jsval_t* args, MessageEx& s) {
			uint8_t value = js_getnum(args[1]);
			s.msg.setValue(value);
			return js_mknull();
		});
	}

	// midiOut

	inline static jsval_t js_midiOut(struct js* js, jsval_t* args, int nargs, const char* chkargs, const char* n, std::function<jsval_t(jsval_t*, MessageEx&)> f) {
		if (nargs == (int)strlen(chkargs) + 1) {
			std::string chkargs1s = string::f("d%s", chkargs);
			const char* chkargs1 = chkargs1s.c_str();
			if (!js_chkargs(args, nargs, chkargs1)) return js_mkerr(js, string::f("midiOut.%s: bad args", n).c_str());
			size_t idx = js_getnum(args[0]);
			if (idx > jsMap[js]->msgCount - 1) return js_mkerr(js, string::f("midiOut.%s: invalid msg", n).c_str());
			jsMap[js]->msgStore[idx].midiPort = 0;
			return f(&args[1], jsMap[js]->msgStore[idx]);
		}
		if (nargs == (int)strlen(chkargs) + 2) {
			std::string chkargs1s = string::f("dd%s", chkargs);
			const char* chkargs1 = chkargs1s.c_str();
			if (!js_chkargs(args, nargs, chkargs1)) return js_mkerr(js, string::f("midiOut.%s: bad args", n).c_str());
			int midiPort = js_getnum(args[0]);
			if (midiPort < 1 || midiPort > jsMap[js]->midiOutputCount) return js_mkerr(js, string::f("midiOut.%s: invalid output index", n).c_str());
			size_t idx = js_getnum(args[1]);
			if (idx > jsMap[js]->msgCount - 1) return js_mkerr(js, string::f("midiOut.%s: invalid msg", n).c_str());
			jsMap[js]->msgStore[idx].midiPort = midiPort;
			return f(&args[2], jsMap[js]->msgStore[idx]);
		}

		return js_mkerr(js, string::f("midiOut.%s: invalid number of args", n).c_str());
	}

	static jsval_t js_midiOut_send(struct js* js, jsval_t* args, int nargs) {
		return js_midiOut(js, args, nargs, "", "send", [](jsval_t* args, MessageEx& s) {
			s.send = true;
			s.msg.frame = -1;
			return js_mknull();
		});
	}

	static jsval_t js_midiOut_sendAfterMs(struct js* js, jsval_t* args, int nargs) {
		return js_midiOut(js, args, nargs, "d", "sendAfterMs", [](jsval_t* args, MessageEx& s) {
			double ms = js_getnum(args[0]);
			int64_t currentFrame = APP->engine->getFrame();
			int64_t frame = ms / 1000.f / APP->engine->getSampleTime();
			s.send = true;
			s.msg.frame = currentFrame + frame;
			return js_mknull();
		});
	}

	static jsval_t js_midiOut_sendAfterTrigger(struct js* js, jsval_t* args, int nargs) {
		if (nargs == 2 || nargs == 3) {
			return js_midiOut(js, args, nargs, "d", "sendAfterTrigger", [js](jsval_t* args, MessageEx& s) {
				int64_t currentTicks = jsMap[js]->getTrigTicks(0);
				int ticks = js_getnum(args[0]);
				s.send = true;
				s.tick = currentTicks + ticks;
				return js_mknull();
			});
		}
		if (nargs == 4) {
			return js_midiOut(js, args, nargs, "dd", "sendAfterTrigger", [js](jsval_t* args, MessageEx& s) {
				int trigPort = js_getnum(args[0]);
				if (trigPort < 1 || trigPort > jsMap[js]->inputTrigCount) return js_mkerr(js, "midiOut.sendAfterTrigger: bad trigInput index");
				int64_t currentTicks = jsMap[js]->getTrigTicks(trigPort - 1);
				int ticks = js_getnum(args[1]);
				s.send = true;
				s.tick = currentTicks + ticks;
				return js_mknull();
			});
		}

		return js_mkerr(js, string::f("midiOut.sendAfterTrigger: bad args").c_str());
	}
};

// std::map is not thread-safe by default but new script engines are only created by inserting a new module
// which needs all Rack's engine-threads to synchronize anyway. Access to each js* is not "const" but
// only done from one thread for each js* - thread-safety should no problem here.
std::map<struct js*, MidiScriptEngineElk*> MidiScriptEngineElk::jsMap;

} // namespace Elk
} // namespace MidiScript
} // namespace StoermelderPackOne