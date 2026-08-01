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
	// Must be initialised: top-level script code runs during loadScript(), before
	// process() sets this to 1, and it bounds every msgStore index check below.
	size_t msgCount = 0;
	// True only while onMidiMessage() is executing. The message store is reset
	// on every callback, so handles created outside one are silently
	// invalidated — this lets midi.create() warn instead of failing quietly.
	bool inCallback = false;
	// Sticky output port selected via midi.selectPort(), 0-based. Stays in
	// effect across callbacks until changed again.
	int selectedPort = 0;

	// closeState() here is a no-op fallback (js is already NULL): onUnload()
	// must run via MidiKitModule's destructor, while this object is still
	// fully alive — writeLog/input.*/trig.*/param.* are pure virtual here and
	// only overridden on the derived class, so calling them post-destruction
	// (e.g. from a script's onUnload) would be undefined behaviour.
	~MidiScriptEngineElk() {
		closeState();
	}

	void setWorker(std::shared_ptr<ITaskWorker> w) { 
		taskWorker = std::move(w);
	}

	void runAsync(std::function<void()> task) override {
		taskWorker->work(task, APP);
	}

	// Formats an Elk error with the source position it was raised at.
	//
	// Elk reports errors as a bare string ("ERROR: parse error") with no
	// position of its own, which leaves a script author with nothing to go on.
	// js_errpos() returns the byte offset of the offending token, so the line
	// and column are recovered by counting newlines up to that offset.
	//
	// "code" must be the exact buffer that was passed to js_eval(), since the
	// offset indexes into it. Falls back to the bare message when no position
	// is available, so the caller never has to special-case that.
	std::string formatError(const char* code, const char* message) {
		size_t pos = js_errpos(js);
		size_t len = strlen(code);
		if (pos == static_cast<size_t>(~0) || pos > len) return message;

		int line = 1;
		size_t lineStart = 0;
		for (size_t i = 0; i < pos; i++) {
			if (code[i] == '\n') {
				line++;
				lineStart = i + 1;
			}
		}
		int column = int(pos - lineStart) + 1;

		// The source line itself, trimmed, so the log shows what failed without
		// the author having to count lines in an external editor.
		size_t lineEnd = lineStart;
		while (lineEnd < len && code[lineEnd] != '\n' && code[lineEnd] != '\r') lineEnd++;
		std::string text(code + lineStart, lineEnd - lineStart);
		size_t first = text.find_first_not_of(" \t");
		text = (first == std::string::npos) ? "" : text.substr(first);

		std::string s = string::f("line %i:%i: %s", line, column, message);
		if (!text.empty()) s += string::f("  >  %s", text.c_str());
		return s;
	}

	void loadScript(const char* script) override {
		closeState();

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
				// The capture runs up to the next "@" (or the end of the header),
				// so it picks up the whitespace that separated the tags. Trim it,
				// otherwise a lone "@engine Elk" yields "Elk " and fails to match.
				size_t last = text.find_last_not_of(" \t");
				text = (last == std::string::npos) ? "" : text.substr(0, last + 1);
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

		// rack
		jsval_t _rack = js_mkobj(js);														// let rack = {}
		js_set(js, js_glob(js), "rack", _rack);
		js_set(js, _rack, "log", js_mkfun(js_rack_log));									// void rack.log(...)
		js_set(js, _rack, "overlay", js_mkfun(js_rack_overlay));							// void rack.overlay(string, [string], [string])
		js_set(js, _rack, "getFrame", js_mkfun(js_rack_getFrame));							// int rack.getFrame()

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
		js_set(js, _number, "toFixed", js_mkfun(js_number_toFixed));

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
		js_set(js, _midi, "selectPort", js_mkfun(js_midi_selectPort));						// void midi.selectPort(midiPort)
		js_set(js, _midi, "create", js_mkfun(js_midi_create));								// let msg = midi.create()
		js_set(js, _midi, "clone", js_mkfun(js_midi_clone));								// let msg2 = midi.clone(msg)
		js_set(js, _midi, "createNRPN", js_mkfun(js_midi_createNrpn));						// let nrpn = midi.createNrpn()
		js_set(js, _midi, "getChanPressure", js_mkfun(js_midi_getChanPressure));			// int midi.getChanPressure(msg)
		js_set(js, _midi, "getChannel", js_mkfun(js_midi_getChannel));						// int midi.getChannel(msg)
		js_set(js, _midi, "getLength", js_mkfun(js_midi_getLength));						// int midi.getLength(msg)
		js_set(js, _midi, "getNote", js_mkfun(js_midi_getNote));							// int midi.getNote(msg)
		js_set(js, _midi, "getPitchWheel", js_mkfun(js_midi_getPitchWheel));				// int midi.getPitchWheel(msg)
		//js_set(js, _midi, "getType", js_mkfun(js_midi_getType));							// int midi.getType(msg)
		js_set(js, _midi, "getRaw", js_mkfun(js_midi_getRaw));								// string midi.getRaw(msg)
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
		js_set(js, _midi, "setRaw", js_mkfun(js_midi_setRaw));								// void midi.setRaw(msg, string)
		js_set(js, _midi, "setSysEx", js_mkfun(js_midi_setSysEx));							// void midi.setSysEx(msg, string)
		js_set(js, _midi, "setValue", js_mkfun(js_midi_setValue));							// void midi.setValue(msg, int)

		// midiOut
		jsval_t _midiOut = js_mkobj(js);
		js_set(js, js_glob(js), "midiOut", _midiOut);										// let midiOut = {}
		js_set(js, _midiOut, "send", js_mkfun(js_midiOut_send));							// void midiOut.send(msg)
		js_set(js, _midiOut, "sendAfterMs", js_mkfun(js_midiOut_sendAfterMs));				// void midiOut.sendAfterMs(msg, ms)
		js_set(js, _midiOut, "sendAfterTrigger", js_mkfun(js_midiOut_sendAfterTrigger));	// void midiOut.sendAfterTrigger(msg, [trigPort], ticks)

		// Pre-registered as no-ops: Elk's `typeof` errors on a truly
		// undeclared identifier (unlike real JS), so existence can't be
		// probed otherwise. A script overrides one with plain assignment,
		// NOT `let` — `let onLoad = ...` would collide and fail to parse.
		js_set(js, js_glob(js), "onLoad", js_mkfun(js_noop));								// onLoad = function() {}
		js_set(js, js_glob(js), "onUnload", js_mkfun(js_noop));								// onUnload = function() {}
		js_set(js, js_glob(js), "onMidiMessage", js_mkfun(js_noop));						// onMidiMessage = function(midiPort, msg) {}

		jsval_t r = js_eval(js, script, ~0U);
		if (js_type(r) == JS_ERR) {
			writeLog("Error while loading script", false);
			// Both reads go through js, so they must happen before closeState()
			writeLog(formatError(script, js_str(js, r)), false);
			closeState();
		}
		else {
			writeLog("Script loaded", false);
			if (js_eval(js, "onMidiMessage", ~0U) == js_mkfun(js_noop)) {
				writeLog("No onMidiMessage(midiPort, msg) function defined — incoming MIDI is ignored", false);
			}
			callOnLoad();
		}
	}

	// Unregisters this engine's context from jsMap. The key returned by
	// js_create() is jsMem itself, i.e. an address inside this object, so an
	// entry left behind after destruction is a dangling key — and a later
	// engine allocated at the same address would collide with it.
	void closeState() {
		if (js != NULL) {
			callOnUnload();
			jsMap.erase(js);
			js = NULL;
			// no need for free() here as "js" completely operates in jsMem
		}
	}

	// Runs after top-level code, once the script is known to have loaded.
	void callOnLoad() {
		callOptionalHook("onLoad");
	}

	// Runs right before this script's state is torn down (replaced, module
	// reset, or module destroyed) — the only place a script can reliably
	// clean up, e.g. an all-notes-off for anything still sounding.
	void callOnUnload() {
		callOptionalHook("onUnload");
	}

	// No-op if the script never overrode the pre-registered default (compared
	// by identity, not Elk equality — see the note in loadScript()). Skipping
	// msgCount's reset in that case matters: a handle built at top level must
	// survive until the next real callback if there's no onLoad to consume it.
	void callOptionalHook(const char* name) {
		if (js_eval(js, name, ~0U) == js_mkfun(js_noop)) return;

		msgCount = 0;
		inCallback = true;
		jsval_t r = js_eval(js, string::f("%s()", name).c_str(), ~0U);
		inCallback = false;
		if (js_type(r) == JS_ERR) {
			writeLog(string::f("%s error: %s", name, js_str(js, r)));
		}
		flushMsgStore();
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
		// No `js` guard: onUnload()'s messages are queued just before js is
		// nulled and must still drain afterwards.
		if (!midiOutQueue.empty()) {
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

			inCallback = true;
			jsval_t r = js_eval(js, string::f("onMidiMessage(%i, 0)", midiPort + 1).c_str(), ~0U);
			inCallback = false;
			if (js_type(r) == JS_ERR) {
				// No line number here, deliberately: for a call into a script
				// function elk swaps js->code to the function body it stored in
				// JS memory, so js_errpos() indexes into that copy rather than
				// into the script buffer and cannot be mapped back to a line.
				writeLog(string::f("onMidiMessage error: %s", js_str(js, r)));
			}

			flushMsgStore();
		}
	}

	// Pushes every message sent during the callback that just ran into
	// midiOutQueue. Shared by onMidiMessage/onLoad/onUnload.
	void flushMsgStore() {
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

	// Default body for onLoad/onUnload, in case a script never assigns its own.
	static jsval_t js_noop(struct js* js, jsval_t* args, int nargs) {
		return js_mknull();
	}

	// rack

	static jsval_t js_rack_log(struct js* js, jsval_t* args, int nargs) {
		if (nargs < 1) return js_mkerr(js, "log: bad args");
		// Concatenate every argument into one log line, coercing each value
		// with the same per-type contract as a single value - so scripts can
		// log numbers/booleans directly instead of wrapping every one in
		// number.toString(). Numbers use the same format as number.toString();
		// strings are logged verbatim (no added quotes); null/undefined log as
		// "null"/"undefined"; anything else falls back to the engine's own
		// stringification so the call never errors.
		std::string log;
		for (int i = 0; i < nargs; i++) {
			switch (js_type(args[i])) {
				case JS_NUM: {
					char str[32];
					formatNumber(js_getnum(args[i]), str, sizeof(str));
					log += str;
					break;
				}
				case JS_TRUE:
					log += "true";
					break;
				case JS_FALSE:
					log += "false";
					break;
				case JS_STR:
					log += js_getstr(js, args[i], NULL);
					break;
				case JS_NULL:
					log += "null";
					break;
				case JS_UNDEF:
					log += "undefined";
					break;
				default:
					log += js_str(js, args[i]);
					break;
			}
		}
		jsMap[js]->writeLog(log);
		return js_mknull();
	}

	static jsval_t js_rack_overlay(struct js* js, jsval_t* args, int nargs) {
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

	static jsval_t js_rack_getFrame(struct js* js, jsval_t* args, int nargs) {
		return js_mknum(double(APP->engine->getFrame()));
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

	// Formats f with up to 6 decimal places, then trims trailing zeros (and a
	// trailing '.' if nothing is left after the point) so an integral value
	// prints as "42" rather than "42.000000", matching the old %i/%f split
	// without needing two branches — and non-integers print only as many
	// decimals as they actually have, up to 6, instead of always six.
	static void formatNumber(float f, char* str, size_t strSize) {
		snprintf(str, strSize, "%f", f);
		char* end = str + strlen(str) - 1;
		while (end > str && *end == '0') { *end = '\0'; end--; }
		if (end > str && *end == '.') { *end = '\0'; }
	}

	static jsval_t js_number_toString(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "number.toString: bad args");
		float f = js_getnum(args[0]);
		char str[32];
		formatNumber(f, str, sizeof(str));
		return js_mkstr(js, str, strlen(str));
	}

	static jsval_t js_number_toFixed(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "dd")) return js_mkerr(js, "number.toFixed: bad args");
		float f = js_getnum(args[0]);
		int digits = static_cast<int>(js_getnum(args[1]));
		if (digits < 0 || digits > 20) return js_mkerr(js, "number.toFixed: digits out of range");
		char str[64];
		snprintf(str, sizeof(str), "%.*f", digits, f);
		return js_mkstr(js, str, strlen(str));
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
		uint8_t ch = 1;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "input.getVoltage: bad channel");
		return js_mknum(jsMap[js]->getInputVoltage(i - 1, ch - 1));
	}

	static jsval_t js_input_isHigh(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "input.isHigh: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputCount) return js_mkerr(js, "input.isHigh: bad index");
		uint8_t ch = 1;
		if (nargs == 2) ch = js_getnum(args[1]);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return js_mkerr(js, "input.isHigh: bad channel");
		return js_mkbool(jsMap[js]->getInputVoltage(i - 1, ch - 1) > 0.7f);
	}

	static jsval_t js_input_isLow(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d") && !js_chkargs(args, nargs, "dd")) return js_mkerr(js, "input.isLow: bad args");
		int i = js_getnum(args[0]);
		if (i < 1 || i > jsMap[js]->inputCount) return js_mkerr(js, "input.isLow: bad index");
		uint8_t ch = 1;
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
		// ">= msgCount" and not "> msgCount - 1": msgCount is unsigned, so the
		// subtraction wraps to SIZE_MAX when it is 0 and admits every index.
		if (idx >= jsMap[js]->msgCount) return js_mkerr(js, string::f("midi.%s: invalid msg", n).c_str());
		return f(args, jsMap[js]->msgStore[idx]);
	}

	inline static jsval_t js_midi_isType(struct js* js, jsval_t* args, int nargs, uint8_t t, const char* n) {
		return js_midi(js, args, nargs, "d", n, [t](jsval_t* args, MessageEx& s) {
			return js_mkbool(s.msg.getStatus() == t);
		});
	}

	// Warns when a message is created outside a callback (onMidiMessage/
	// onLoad/onUnload) — the store resets every callback, silently
	// invalidating such a handle before use.
	inline static void warnIfOutsideCallback(struct js* js, const char* fn) {
		MidiScriptEngineElk* e = jsMap[js];
		if (!e->inCallback) {
			e->writeLog(string::f("%s: called outside a callback; the message "
				"is discarded when the next MIDI message arrives", fn), false);
		}
	}

	static jsval_t js_midi_selectPort(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "midi.selectPort: bad args");
		int midiPort = js_getnum(args[0]);
		if (midiPort < 1 || midiPort > jsMap[js]->midiOutputCount) return js_mkerr(js, "midi.selectPort: invalid output index");
		jsMap[js]->selectedPort = midiPort - 1;
		return js_mknull();
	}

	static jsval_t js_midi_create(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "")) return js_mkerr(js, "midi.create: bad args");
		warnIfOutsideCallback(js, "midi.create");
		size_t* s = &jsMap[js]->msgCount;
		if (*s == msgStoreSize) return js_mkerr(js, "midi.create: maximum reached");
		jsMap[js]->msgStore[*s] = MessageEx();
		return js_mknum((*s)++);
	}

	static jsval_t js_midi_clone(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "d")) return js_mkerr(js, "midi.clone: bad args");
		size_t idx = js_getnum(args[0]);
		if (idx >= jsMap[js]->msgCount) return js_mkerr(js, "midi.clone: invalid msg");
		warnIfOutsideCallback(js, "midi.clone");
		size_t* s = &jsMap[js]->msgCount;
		if (*s == msgStoreSize) return js_mkerr(js, "midi.clone: maximum reached");
		// Copy only the MIDI payload; the clone starts as a fresh, unsent
		// message (send/tick/midiPort/isNrpn at defaults) so it can be modified
		// and sent independently of the source.
		MessageEx clone;
		clone.msg = jsMap[js]->msgStore[idx].msg;
		jsMap[js]->msgStore[*s] = clone;
		return js_mknum((*s)++);
	}

	static jsval_t js_midi_createNrpn(struct js* js, jsval_t* args, int nargs) {
		if (!js_chkargs(args, nargs, "")) return js_mkerr(js, "midi.createNrpn: bad args");
		warnIfOutsideCallback(js, "midi.createNRPN");
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

	static jsval_t js_midi_getChanPressure(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getChanPressure", [](jsval_t* args, MessageEx& s) {
			return js_mknum(s.msg.getNote());
		});
	}

	static jsval_t js_midi_getChannel(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getChannel", [](jsval_t* args, MessageEx& s) {
			// Status 0xf is the realtime/SysEx family (clock, start/stop/continue,
			// SysEx framing) — none of those carry a channel, and the low nibble
			// is a sub-type selector instead (see the is* predicates below), so
			// the old "+ 1" on that nibble returned a plausible-looking but
			// meaningless channel number (#A4). -1 is unambiguous: 1-16 is the
			// only valid channel range, so a script can check `> 0` without
			// needing to special-case realtime messages via try/catch.
			if (s.msg.getStatus() == 0xf) return js_mknum(-1);
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
			uint16_t value = (static_cast<uint16_t>(s.msg.getValue()) << 7) | s.msg.getNote();
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

	static jsval_t js_midi_getRaw(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "d", "getRaw", [js](jsval_t* args, MessageEx& s) {
			std::ostringstream ss;
			ss << std::hex;
			for (int i = 0; i < s.msg.getSize(); i++) {
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
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
			uint8_t cc = js_getnum(args[2]);
			uint8_t value = std::max(0, std::min(127, static_cast<int>(js_getnum(args[3]))));
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
			if (idx2 >= jsMap[js]->msgCount) return js_mkerr(js, string::f("midi.setCc14bit: invalid msg").c_str());
			MessageEx& s2 = jsMap[js]->msgStore[idx2];
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[2]))));
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
			s1.msg.setValue(static_cast<int8_t>(value));
			s2.msg.setValue(static_cast<int8_t>((value - static_cast<int8_t>(value)) * 128.f));
			return js_mknull();
		});
	}

	static jsval_t js_midi_setChannel(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dd", "setChannel", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
			s.msg.setChannel(ch - 1);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setChanPressure(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ddd", "setChanPressure", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
			uint8_t value = js_getnum(args[2]);
			// Channel pressure is a 2-byte message (status + pressure), not 3 —
			// the pressure lives in bytes[1], read back via getChanPressure/getNote.
			if (s.msg.getSize() != 2) s.msg.setSize(2);
			s.msg.setStatus(0xd);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(value);
			return js_mknull();
		});
	}


	static jsval_t js_midi_setKeyPressure(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "dddd", "setKeyPressure", [](jsval_t* args, MessageEx& s) {
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
			uint8_t note = js_getnum(args[2]);
			uint8_t vel = std::max(0, std::min(127, static_cast<int>(js_getnum(args[3]))));
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
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
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
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
			uint8_t note = js_getnum(args[2]);
			uint8_t vel = std::max(0, std::min(127, static_cast<int>(js_getnum(args[3]))));
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
		if (idx >= jsMap[js]->msgCount) return js_mkerr(js, "midi.setNrpn: invalid msg");
		MessageEx* s1 = &jsMap[js]->msgStore[idx];
		if (!s1->isNrpn) return js_mkerr(js, "midi.setNrpn: invalid nrpn message");
		MessageEx* s2 = &jsMap[js]->msgStore[idx + 1];
		MessageEx* s3 = &jsMap[js]->msgStore[idx + 2];
		MessageEx* s4 = &jsMap[js]->msgStore[idx + 3];

		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
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
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
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
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(js_getnum(args[1]))));
			uint8_t prg = js_getnum(args[2]);
			if (s.msg.getSize() != 3) s.msg.setSize(3);
			s.msg.setStatus(0xc);
			s.msg.setChannel(ch - 1);
			s.msg.setNote(prg);
			return js_mknull();
		});
	}

	static jsval_t js_midi_setRaw(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ds", "setRaw", [js](jsval_t* args, MessageEx& s) {
			std::string data = js_getstr(js, args[1], NULL);
			if (data.length() % 2 != 0) {
				return js_mkerr(js, "midi.setRaw: invalid string length");
			}
			if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
				return js_mkerr(js, "midi.setRaw: invalid hexstring");
			}
			s.msg.setSize(data.length() / 2);
			for (size_t i = 0; i < data.length(); i += 2) {
				std::string bs = data.substr(i, 2);
				char byte = static_cast<char>(strtol(bs.c_str(), NULL, 16));
				s.msg.bytes[i / 2] = byte;
			}
			return js_mknull();
		});
	}

	static jsval_t js_midi_setSysEx(struct js* js, jsval_t* args, int nargs) {
		return js_midi(js, args, nargs, "ds", "setSysEx", [js](jsval_t* args, MessageEx& s) {
			std::string data = js_getstr(js, args[1], NULL);
			if (data.length() % 2 != 0) {
				return js_mkerr(js, "midi.setSysEx: invalid string length");
			}
			if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
				return js_mkerr(js, "midi.setSysEx: invalid hexstring");
			}
			if (data.length() / 2 > MidiScriptEngine::sysExMaxPayloadLength) {
				return js_mkerr(js, string::f("midi.setSysEx: payload exceeds maximum of %d bytes", MidiScriptEngine::sysExMaxPayloadLength).c_str());
			}
			for (size_t i = 0; i < data.length(); i += 2) {
				uint8_t byte = static_cast<uint8_t>(strtol(data.substr(i, 2).c_str(), NULL, 16));
				if (byte > 0x7f) {
					return js_mkerr(js, "midi.setSysEx: payload bytes must be 7-bit (00-7f)");
				}
			}
			s.msg.setSize(data.length() / 2 + 2);
			s.msg.bytes[0] = 0xf0;
			for (size_t i = 0; i < data.length(); i += 2) {
				std::string bs = data.substr(i, 2);
				char byte = static_cast<char>(strtol(bs.c_str(), NULL, 16));
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
		std::string chkargs1s = string::f("d%s", chkargs);
		const char* chkargs1 = chkargs1s.c_str();
		if (!js_chkargs(args, nargs, chkargs1)) return js_mkerr(js, string::f("midiOut.%s: bad args", n).c_str());
		size_t idx = js_getnum(args[0]);
		if (idx >= jsMap[js]->msgCount) return js_mkerr(js, string::f("midiOut.%s: invalid msg", n).c_str());
		jsMap[js]->msgStore[idx].midiPort = jsMap[js]->selectedPort;
		return f(&args[1], jsMap[js]->msgStore[idx]);
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
		if (nargs == 2) {
			return js_midiOut(js, args, nargs, "d", "sendAfterTrigger", [js](jsval_t* args, MessageEx& s) {
				int64_t currentTicks = jsMap[js]->getTrigTicks(0);
				int ticks = js_getnum(args[0]);
				s.send = true;
				s.tick = currentTicks + ticks;
				return js_mknull();
			});
		}
		if (nargs == 3) {
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