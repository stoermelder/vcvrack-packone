#include "MidiScriptEngine.h"
#include "../../utils/TaskWorker.hpp"
#include "../../../dep/quickjs/quickjs.h"
#include <iomanip>
#include <regex>

namespace StoermelderPackOne {
namespace MidiScript {
namespace QuickJs {


struct MidiScriptEngineQuickJs : MidiScriptEngine {
	struct MessageEx {
		int midiPort = 0;
		Message msg;
		bool isNrpn = false;
		bool send = false;
		uint64_t tick = 0;
	};

	static std::map<JSContext*, MidiScriptEngineQuickJs*> ctxMap;

	JSRuntime* rt = NULL;
	JSContext* ctx = NULL;

	static const size_t memoryLimit = 1024 * 1024;

	const static int msgStoreSize = 32;
	MessageEx msgStore[msgStoreSize];
	// Must be initialised: top-level script code runs during loadScript(), before
	// process() sets this to 1, and it bounds every msgStore index check below.
	size_t msgCount = 0;
	// True only while onMidiMessage() is executing. The message store is reset
	// on every callback, so handles created outside one are silently
	// invalidated — this lets midi.create() warn instead of failing quietly.
	bool inCallback = false;
	// Sticky output port selected via midiOut.selectPort(), 0-based. Stays in
	// effect across callbacks until changed again.
	int selectedPort = 0;

	bool hasOnMidiMessage = false;
	bool hasOnLoad = false;
	bool hasOnUnload = false;
	bool hasOnTrigger = false;

	// closeState() here is a no-op fallback (ctx is already NULL): onUnload()
	// must run via MidiKitModule's destructor, while this object is still
	// fully alive — writeLog/input.*/trig.*/param.* are pure virtual here and
	// only overridden on the derived class, so calling them post-destruction
	// (e.g. from a script's onUnload) would be undefined behaviour.
	~MidiScriptEngineQuickJs() {
		closeState();
	}

	std::string jsToStdString(JSValueConst v) {
		const char* s = JS_ToCString(ctx, v);
		std::string r = s ? s : "";
		if (s) JS_FreeCString(ctx, s);
		return r;
	}

	// Formats a QuickJS exception with the source position it was raised at.
	// QuickJS exceptions carry their own "stack" property with file/line info 
	// when available (e.g. a SyntaxError raised during parsing), so this reads
	// message + stack straight off the exception object.
	std::string formatError(JSValueConst exc) {
		std::string message = jsToStdString(exc);
		JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
		if (!JS_IsUndefined(stack)) {
			std::string s = jsToStdString(stack);
			if (!s.empty()) message += "\n" + s;
		}
		JS_FreeValue(ctx, stack);
		return message;
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
		//	 * @engine QuickJs
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
				// otherwise a lone "@engine QuickJs" yields "QuickJs " and fails to match.
				size_t last = text.find_last_not_of(" \t");
				text = (last == std::string::npos) ? "" : text.substr(0, last + 1);
				topics[topic] = text;
			}
		}

		if (topics.find("engine") == topics.end() || topics["engine"] != "QuickJs") {
			writeLog("Script is not compatible with MIDI-KIT", false);
			return;
		}

		if (topics.find("author") != topics.end()) {
			writeLog(string::f("Author: %s", topics["author"].c_str()), false);
		}
		if (topics.find("description") != topics.end()) {
			writeLog(topics["description"], false);
		}

		rt = JS_NewRuntime();
		JS_SetMemoryLimit(rt, memoryLimit);
		JS_SetMaxStackSize(rt, 256 * 1024);
		ctx = JS_NewContext(rt);
		ctxMap[ctx] = this;

		registerApi();

		JSValue r = JS_Eval(ctx, script, strlen(script), "script", JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			writeLog("Error while loading script", false);
			writeLog(formatError(exc), false);
			JS_FreeValue(ctx, exc);
			closeState();
		}
		else {
			JS_FreeValue(ctx, r);
			writeLog("Script loaded", false);

			JSValue glob = JS_GetGlobalObject(ctx);
			hasOnLoad = isCallableProp(glob, "onLoad");
			hasOnUnload = isCallableProp(glob, "onUnload");
			hasOnMidiMessage = isCallableProp(glob, "onMidiMessage");
			hasOnTrigger = isCallableProp(glob, "onTrigger");
			JS_FreeValue(ctx, glob);

			if (!hasOnMidiMessage) {
				writeLog("No onMidiMessage(midiPort, msg) function defined — incoming MIDI is ignored", false);
			}
			callOnLoad();
		}
	}

	bool isCallableProp(JSValueConst obj, const char* name) {
		JSValue v = JS_GetPropertyStr(ctx, obj, name);
		bool r = JS_IsFunction(ctx, v);
		JS_FreeValue(ctx, v);
		return r;
	}

	// Unregisters this engine's context from ctxMap and frees the QuickJS
	// runtime/context. A dangling entry left behind after destruction would
	// collide with a later engine allocated at the same address.
	void closeState() {
		if (ctx != NULL) {
			callOnUnload();
			ctxMap.erase(ctx);
			JS_FreeContext(ctx);
			JS_FreeRuntime(rt);
			ctx = NULL;
			rt = NULL;
		}
	}

	// Runs after top-level code, once the script is known to have loaded.
	void callOnLoad() {
		callOptionalHook("onLoad", hasOnLoad);
	}

	// Runs right before this script's state is torn down (replaced, module
	// reset, or module destroyed) — the only place a script can reliably
	// clean up, e.g. an all-notes-off for anything still sounding.
	void callOnUnload() {
		callOptionalHook("onUnload", hasOnUnload);
	}

	// No-op if the script never defined this hook. Skipping msgCount's reset
	// in that case matters: a handle built at top level must survive until
	// the next real callback if there's no onLoad to consume it.
	void callOptionalHook(const char* name, bool has) {
		if (!has) return;

		msgCount = 0;
		inCallback = true;
		JSValue glob = JS_GetGlobalObject(ctx);
		JSValue fn = JS_GetPropertyStr(ctx, glob, name);
		JSValue r = JS_Call(ctx, fn, glob, 0, NULL);
		JS_FreeValue(ctx, fn);
		JS_FreeValue(ctx, glob);
		inCallback = false;
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			writeLog(string::f("%s error: %s", name, jsToStdString(exc).c_str()));
			JS_FreeValue(ctx, exc);
		}
		else {
			JS_FreeValue(ctx, r);
		}
		flushMsgStore();
	}

	void processInMessage(int midiPort, Message& msg) override {
		if (ctx) {
			midiInQueue.push(std::make_tuple(midiPort, msg));
		}
	}

	void processInTick(int trigPort) override {
		if (ctx) {
			tickInQueue.push(trigPort);
		}
	}

	void dispatchMidiMessage(int midiPort, Message& msg) override {
		if (ctx) {
			msgStore[0].msg = msg;
			msgStore[0].send = false;
			msgStore[0].tick = 0;
			msgCount = 1;

			inCallback = true;
			if (hasOnMidiMessage) {
				JSValue glob = JS_GetGlobalObject(ctx);
				JSValue fn = JS_GetPropertyStr(ctx, glob, "onMidiMessage");
				JSValue args[2] = { JS_NewInt32(ctx, midiPort + 1), JS_NewInt32(ctx, 0) };
				JSValue r = JS_Call(ctx, fn, glob, 2, args);
				JS_FreeValue(ctx, args[0]);
				JS_FreeValue(ctx, args[1]);
				JS_FreeValue(ctx, fn);
				JS_FreeValue(ctx, glob);
				inCallback = false;
				if (JS_IsException(r)) {
					JS_FreeValue(ctx, r);
					JSValue exc = JS_GetException(ctx);
					writeLog(string::f("onMidiMessage error: %s", jsToStdString(exc).c_str()));
					JS_FreeValue(ctx, exc);
				}
				else {
					JS_FreeValue(ctx, r);
				}
			}
			else {
				inCallback = false;
			}

			flushMsgStore();
		}
	}

	// Dispatches onTrigger(trigPort) when the trigger input fires. No-op if
	// the script never defined it.
	void dispatchTrigger(int trigPort) override {
		if (ctx) {
			msgCount = 0;
			inCallback = true;
			if (hasOnTrigger) {
				JSValue glob = JS_GetGlobalObject(ctx);
				JSValue fn = JS_GetPropertyStr(ctx, glob, "onTrigger");
				JSValue arg = JS_NewInt32(ctx, trigPort + 1);
				JSValue r = JS_Call(ctx, fn, glob, 1, &arg);
				JS_FreeValue(ctx, arg);
				JS_FreeValue(ctx, fn);
				JS_FreeValue(ctx, glob);
				inCallback = false;
				if (JS_IsException(r)) {
					JS_FreeValue(ctx, r);
					JSValue exc = JS_GetException(ctx);
					writeLog(string::f("onTrigger error: %s", jsToStdString(exc).c_str()));
					JS_FreeValue(ctx, exc);
				}
				else {
					JS_FreeValue(ctx, r);
				}
			}
			else {
				inCallback = false;
			}
			flushMsgStore();
		}
	}

	// Pushes every message sent during the callback that just ran into
	// midiOutQueue. Shared by onMidiMessage/onLoad/onUnload/onTrigger.
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

	// Calls a global "name(i+1)" function returning a string, e.g.
	// input.getName(i)/param.getName(i)/param.getValueFormat(i). Falls back to
	// "" if unset or if the call raises.
	std::string callGlobalStringFn(const char* objName, const char* fnName, int i) {
		if (!ctx) return "";
		JSValue glob = JS_GetGlobalObject(ctx);
		JSValue obj = JS_GetPropertyStr(ctx, glob, objName);
		JSValue fn = JS_GetPropertyStr(ctx, obj, fnName);
		std::string result;
		if (JS_IsFunction(ctx, fn)) {
			JSValue arg = JS_NewInt32(ctx, i + 1);
			JSValue r = JS_Call(ctx, fn, obj, 1, &arg);
			JS_FreeValue(ctx, arg);
			if (JS_IsException(r)) {
				JS_FreeValue(ctx, JS_GetException(ctx));
			}
			else {
				result = jsToStdString(r);
			}
			JS_FreeValue(ctx, r);
		}
		JS_FreeValue(ctx, fn);
		JS_FreeValue(ctx, obj);
		JS_FreeValue(ctx, glob);
		return result;
	}

	std::string getInputName(int i) override {
		return callGlobalStringFn("input", "getName", i);
	}

	std::string getParamName(int i) override {
		return callGlobalStringFn("param", "getName", i);
	}

	std::string getParamFormatValue(int i) override {
		return callGlobalStringFn("param", "getValueFormat", i);
	}

	// Returns current/total bytes in use by the QuickJS heap, or false if no
	// script is loaded.
	bool getMemoryUsage(size_t& used, size_t& total) {
		if (!ctx) return false;
		JSMemoryUsage s;
		JS_ComputeMemoryUsage(rt, &s);
		used = s.malloc_size;
		total = memoryLimit;
		return true;
	}


	void registerApi() {
		JSValue glob = JS_GetGlobalObject(ctx);

		// rack
		JSValue _rack = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, glob, "rack", _rack);
		JS_SetPropertyStr(ctx, _rack, "log", JS_NewCFunction(ctx, js_rack_log, "log", 1));
		JS_SetPropertyStr(ctx, _rack, "overlay", JS_NewCFunction(ctx, js_rack_overlay, "overlay", 3));
		JS_SetPropertyStr(ctx, _rack, "getFrame", JS_NewCFunction(ctx, js_rack_getFrame, "getFrame", 0));

		// number
		JSValue _number = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, glob, "number", _number);
		JS_SetPropertyStr(ctx, _number, "abs", JS_NewCFunction(ctx, js_number_abs, "abs", 1));
		JS_SetPropertyStr(ctx, _number, "ceil", JS_NewCFunction(ctx, js_number_ceil, "ceil", 1));
		JS_SetPropertyStr(ctx, _number, "crossfade", JS_NewCFunction(ctx, js_number_crossfade, "crossfade", 3));
		JS_SetPropertyStr(ctx, _number, "floor", JS_NewCFunction(ctx, js_number_floor, "floor", 1));
		JS_SetPropertyStr(ctx, _number, "max", JS_NewCFunction(ctx, js_number_max, "max", 2));
		JS_SetPropertyStr(ctx, _number, "min", JS_NewCFunction(ctx, js_number_min, "min", 2));
		JS_SetPropertyStr(ctx, _number, "random", JS_NewCFunction(ctx, js_number_random, "random", 0));
		JS_SetPropertyStr(ctx, _number, "rescale", JS_NewCFunction(ctx, js_number_rescale, "rescale", 5));
		JS_SetPropertyStr(ctx, _number, "toString", JS_NewCFunction(ctx, js_number_toString, "toString", 1));
		JS_SetPropertyStr(ctx, _number, "toFixed", JS_NewCFunction(ctx, js_number_toFixed, "toFixed", 2));

		// input
		JSValue _input = JS_Eval(ctx,
			"(function() { return {"
			"	getName: function(i) { return \"Port \" + number.toString(i); }"
			"}; })();", strlen(
			"(function() { return {"
			"	getName: function(i) { return \"Port \" + number.toString(i); }"
			"}; })();"), "<input>", JS_EVAL_TYPE_GLOBAL);
		JS_SetPropertyStr(ctx, glob, "input", _input);
		JS_SetPropertyStr(ctx, _input, "enable", JS_NewCFunction(ctx, js_input_enable, "enable", 1));
		JS_SetPropertyStr(ctx, _input, "getVoltage", JS_NewCFunction(ctx, js_input_getVoltage, "getVoltage", 2));
		JS_SetPropertyStr(ctx, _input, "isHigh", JS_NewCFunction(ctx, js_input_isHigh, "isHigh", 2));
		JS_SetPropertyStr(ctx, _input, "isLow", JS_NewCFunction(ctx, js_input_isLow, "isLow", 2));

		// trig
		JSValue _trig = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, glob, "trig", _trig);
		JS_SetPropertyStr(ctx, _trig, "getTicks", JS_NewCFunction(ctx, js_trig_getTicks, "getTicks", 1));
		JS_SetPropertyStr(ctx, _trig, "isHigh", JS_NewCFunction(ctx, js_trig_isHigh, "isHigh", 2));
		JS_SetPropertyStr(ctx, _trig, "isLow", JS_NewCFunction(ctx, js_trig_isLow, "isLow", 2));
		JS_SetPropertyStr(ctx, _trig, "setGate", JS_NewCFunction(ctx, js_trig_setGate, "setGate", 3));
		JS_SetPropertyStr(ctx, _trig, "setHigh", JS_NewCFunction(ctx, js_trig_setHigh, "setHigh", 2));
		JS_SetPropertyStr(ctx, _trig, "setLow", JS_NewCFunction(ctx, js_trig_setLow, "setLow", 2));
		JS_SetPropertyStr(ctx, _trig, "setTrigger", JS_NewCFunction(ctx, js_trig_setTrigger, "setTrigger", 2));

		// param
		const char* paramSrc =
			"(function() { return {"
			"	getName: function(i) { return \"Param \" + number.toString(i); },"
			"	getValueFormat: function(i) { return \"\"; }"
			"}; })();";
		JSValue _param = JS_Eval(ctx, paramSrc, strlen(paramSrc), "<param>", JS_EVAL_TYPE_GLOBAL);
		JS_SetPropertyStr(ctx, glob, "param", _param);
		JS_SetPropertyStr(ctx, _param, "enable", JS_NewCFunction(ctx, js_param_enable, "enable", 1));
		JS_SetPropertyStr(ctx, _param, "getValue", JS_NewCFunction(ctx, js_param_getValue, "getValue", 1));

		// midi
		JSValue _midi = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, glob, "midi", _midi);
		JS_SetPropertyStr(ctx, _midi, "create", JS_NewCFunction(ctx, js_midi_create, "create", 0));
		JS_SetPropertyStr(ctx, _midi, "clone", JS_NewCFunction(ctx, js_midi_clone, "clone", 1));
		JS_SetPropertyStr(ctx, _midi, "createNRPN", JS_NewCFunction(ctx, js_midi_createNrpn, "createNRPN", 0));
		JS_SetPropertyStr(ctx, _midi, "getChanPressure", JS_NewCFunction(ctx, js_midi_getChanPressure, "getChanPressure", 1));
		JS_SetPropertyStr(ctx, _midi, "getChannel", JS_NewCFunction(ctx, js_midi_getChannel, "getChannel", 1));
		JS_SetPropertyStr(ctx, _midi, "getLength", JS_NewCFunction(ctx, js_midi_getLength, "getLength", 1));
		JS_SetPropertyStr(ctx, _midi, "getNote", JS_NewCFunction(ctx, js_midi_getNote, "getNote", 1));
		JS_SetPropertyStr(ctx, _midi, "getPitchWheel", JS_NewCFunction(ctx, js_midi_getPitchWheel, "getPitchWheel", 1));
		JS_SetPropertyStr(ctx, _midi, "getProgramChange", JS_NewCFunction(ctx, js_midi_getProgramChange, "getProgramChange", 1));
		JS_SetPropertyStr(ctx, _midi, "getRaw", JS_NewCFunction(ctx, js_midi_getRaw, "getRaw", 1));
		JS_SetPropertyStr(ctx, _midi, "getSysEx", JS_NewCFunction(ctx, js_midi_getSysEx, "getSysEx", 1));
		JS_SetPropertyStr(ctx, _midi, "getSysExLength", JS_NewCFunction(ctx, js_midi_getSysExLength, "getSysExLength", 1));
		JS_SetPropertyStr(ctx, _midi, "getValue", JS_NewCFunction(ctx, js_midi_getValue, "getValue", 1));
		JS_SetPropertyStr(ctx, _midi, "isCc", JS_NewCFunction(ctx, js_midi_isCc, "isCc", 1));
		JS_SetPropertyStr(ctx, _midi, "isChanPressure", JS_NewCFunction(ctx, js_midi_isChanPressure, "isChanPressure", 1));
		JS_SetPropertyStr(ctx, _midi, "isClock", JS_NewCFunction(ctx, js_midi_isClock, "isClock", 1));
		JS_SetPropertyStr(ctx, _midi, "isContinue", JS_NewCFunction(ctx, js_midi_isContinue, "isContinue", 1));
		JS_SetPropertyStr(ctx, _midi, "isKeyPressure", JS_NewCFunction(ctx, js_midi_isKeyPressure, "isKeyPressure", 1));
		JS_SetPropertyStr(ctx, _midi, "isNoteOff", JS_NewCFunction(ctx, js_midi_isNoteOff, "isNoteOff", 1));
		JS_SetPropertyStr(ctx, _midi, "isNoteOn", JS_NewCFunction(ctx, js_midi_isNoteOn, "isNoteOn", 1));
		JS_SetPropertyStr(ctx, _midi, "isProgramChange", JS_NewCFunction(ctx, js_midi_isProgramChange, "isProgramChange", 1));
		JS_SetPropertyStr(ctx, _midi, "isPitchWheel", JS_NewCFunction(ctx, js_midi_isPitchWheel, "isPitchWheel", 1));
		JS_SetPropertyStr(ctx, _midi, "isStart", JS_NewCFunction(ctx, js_midi_isStart, "isStart", 1));
		JS_SetPropertyStr(ctx, _midi, "isStop", JS_NewCFunction(ctx, js_midi_isStop, "isStop", 1));
		JS_SetPropertyStr(ctx, _midi, "isSysEx", JS_NewCFunction(ctx, js_midi_isSysEx, "isSysEx", 1));
		JS_SetPropertyStr(ctx, _midi, "setCc", JS_NewCFunction(ctx, js_midi_setCc, "setCc", 4));
		JS_SetPropertyStr(ctx, _midi, "setCc14bit", JS_NewCFunction(ctx, js_midi_setCc14bit, "setCc14bit", 5));
		JS_SetPropertyStr(ctx, _midi, "setChannel", JS_NewCFunction(ctx, js_midi_setChannel, "setChannel", 2));
		JS_SetPropertyStr(ctx, _midi, "setChanPressure", JS_NewCFunction(ctx, js_midi_setChanPressure, "setChanPressure", 3));
		JS_SetPropertyStr(ctx, _midi, "setKeyPressure", JS_NewCFunction(ctx, js_midi_setKeyPressure, "setKeyPressure", 4));
		JS_SetPropertyStr(ctx, _midi, "setNote", JS_NewCFunction(ctx, js_midi_setNote, "setNote", 2));
		JS_SetPropertyStr(ctx, _midi, "setNoteOff", JS_NewCFunction(ctx, js_midi_setNoteOff, "setNoteOff", 4));
		JS_SetPropertyStr(ctx, _midi, "setNoteOn", JS_NewCFunction(ctx, js_midi_setNoteOn, "setNoteOn", 4));
		JS_SetPropertyStr(ctx, _midi, "setNRPN", JS_NewCFunction(ctx, js_midi_setNrpn, "setNRPN", 4));
		JS_SetPropertyStr(ctx, _midi, "setPitchWheel", JS_NewCFunction(ctx, js_midi_setPitchWheel, "setPitchWheel", 3));
		JS_SetPropertyStr(ctx, _midi, "setProgramChange", JS_NewCFunction(ctx, js_midi_setProgramChange, "setProgramChange", 3));
		JS_SetPropertyStr(ctx, _midi, "setRaw", JS_NewCFunction(ctx, js_midi_setRaw, "setRaw", 2));
		JS_SetPropertyStr(ctx, _midi, "setSysEx", JS_NewCFunction(ctx, js_midi_setSysEx, "setSysEx", 2));
		JS_SetPropertyStr(ctx, _midi, "setValue", JS_NewCFunction(ctx, js_midi_setValue, "setValue", 2));

		// midiOut
		JSValue _midiOut = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, glob, "midiOut", _midiOut);
		JS_SetPropertyStr(ctx, _midiOut, "selectPort", JS_NewCFunction(ctx, js_midiOut_selectPort, "selectPort", 1));
		JS_SetPropertyStr(ctx, _midiOut, "send", JS_NewCFunction(ctx, js_midiOut_send, "send", 1));
		JS_SetPropertyStr(ctx, _midiOut, "sendAfterMs", JS_NewCFunction(ctx, js_midiOut_sendAfterMs, "sendAfterMs", 2));
		JS_SetPropertyStr(ctx, _midiOut, "sendAfterTrigger", JS_NewCFunction(ctx, js_midiOut_sendAfterTrigger, "sendAfterTrigger", 3));

		JS_FreeValue(ctx, glob);
	}


	static bool argIsNumber(JSContext* ctx, JSValueConst v) {
		return JS_IsNumber(v);
	}

	static double argNum(JSContext* ctx, JSValueConst v) {
		double d = 0;
		JS_ToFloat64(ctx, &d, v);
		return d;
	}

	static JSValue jsThrow(JSContext* ctx, const std::string& msg) {
		return JS_ThrowTypeError(ctx, "%s", msg.c_str());
	}

	// rack

	static JSValue js_rack_log(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1) return jsThrow(ctx, "log: bad args");
		// Concatenate every argument into one log line, coercing each value
		// with the same per-type contract as a single value - so scripts can
		// log numbers/booleans directly instead of wrapping every one in
		// number.toString(). Numbers use the same format as number.toString();
		// strings are logged verbatim (no added quotes); null/undefined log as
		// "null"/"undefined"; anything else falls back to the engine's own
		// stringification so the call never errors.
		std::string log;
		for (int i = 0; i < argc; i++) {
			JSValueConst v = argv[i];
			if (JS_IsNumber(v)) {
				double d = 0;
				JS_ToFloat64(ctx, &d, v);
				char str[32];
				formatNumber(d, str, sizeof(str));
				log += str;
			}
			else if (JS_IsBool(v)) {
				log += JS_ToBool(ctx, v) ? "true" : "false";
			}
			else if (JS_IsString(v)) {
				const char* s = JS_ToCString(ctx, v);
				log += s ? s : "";
				if (s) JS_FreeCString(ctx, s);
			}
			else if (JS_IsNull(v)) {
				log += "null";
			}
			else if (JS_IsUndefined(v)) {
				log += "undefined";
			}
			else {
				const char* s = JS_ToCString(ctx, v);
				log += s ? s : "";
				if (s) JS_FreeCString(ctx, s);
			}
		}
		ctxMap[ctx]->writeLog(log);
		return JS_UNDEFINED;
	}

	static JSValue js_rack_overlay(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 3 || !JS_IsString(argv[0])) return jsThrow(ctx, "overlay: bad args");
		for (int i = 0; i < argc; i++) {
			if (!JS_IsString(argv[i])) return jsThrow(ctx, "overlay: bad args");
		}
		std::string s1 = argc >= 1 ? ctxMap[ctx]->jsToStdString(argv[0]) : "";
		std::string s2 = argc >= 2 ? ctxMap[ctx]->jsToStdString(argv[1]) : "";
		std::string s3 = argc >= 3 ? ctxMap[ctx]->jsToStdString(argv[2]) : "";
		ctxMap[ctx]->writeOverlay(s1, s2, s3);
		return JS_UNDEFINED;
	}

	static JSValue js_rack_getFrame(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return JS_NewFloat64(ctx, double(APP->engine->getFrame()));
	}

	// number

	static JSValue js_number_abs(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "number.abs: bad args");
		float f = argNum(ctx, argv[0]);
		return JS_NewFloat64(ctx, std::abs(f));
	}

	static JSValue js_number_ceil(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "number.ceil: bad args");
		float f = argNum(ctx, argv[0]);
		return JS_NewFloat64(ctx, std::ceil(f));
	}

	static JSValue js_number_crossfade(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 3 || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "number.crossfade: bad args");
		float a = argNum(ctx, argv[0]);
		float b = argNum(ctx, argv[1]);
		float p = argNum(ctx, argv[2]);
		return JS_NewFloat64(ctx, rack::crossfade(a, b, p));
	}

	static JSValue js_number_floor(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "number.floor: bad args");
		float f = argNum(ctx, argv[0]);
		return JS_NewFloat64(ctx, std::floor(f));
	}

	static JSValue js_number_max(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 2 || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "number.max: bad args");
		float f1 = argNum(ctx, argv[0]);
		float f2 = argNum(ctx, argv[1]);
		return JS_NewFloat64(ctx, std::max(f1, f2));
	}

	static JSValue js_number_min(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 2 || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "number.min: bad args");
		float f1 = argNum(ctx, argv[0]);
		float f2 = argNum(ctx, argv[1]);
		return JS_NewFloat64(ctx, std::min(f1, f2));
	}

	static JSValue js_number_random(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "number.random: bad args");
		return JS_NewFloat64(ctx, rack::random::uniform());
	}

	static JSValue js_number_rescale(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if ((argc != 5 && argc != 6) || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1]) ||
			!argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]) || !argIsNumber(ctx, argv[4]) ||
			(argc == 6 && !argIsNumber(ctx, argv[5])))
			return jsThrow(ctx, "number.rescale: bad args");
		float x = argNum(ctx, argv[0]);
		float xMin = argNum(ctx, argv[1]);
		float xMax = argNum(ctx, argv[2]);
		float yMin = argNum(ctx, argv[3]);
		float yMax = argNum(ctx, argv[4]);
		if (argc == 6) {
			float a = argNum(ctx, argv[5]);
			x = rack::rescale(x, xMin, xMax, 1.f, M_E);
			x = std::exp(std::pow(std::log(x), dsp::exp2_taylor5(a)));
			x = rack::rescale(x, 1.f, M_E, yMin, yMax);
			return JS_NewFloat64(ctx, x);
		}
		else {
			return JS_NewFloat64(ctx, rack::rescale(x, xMin, xMax, yMin, yMax));
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

	static JSValue js_number_toString(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "number.toString: bad args");
		float f = argNum(ctx, argv[0]);
		char str[32];
		formatNumber(f, str, sizeof(str));
		return JS_NewString(ctx, str);
	}

	static JSValue js_number_toFixed(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 2 || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "number.toFixed: bad args");
		float f = argNum(ctx, argv[0]);
		int digits = static_cast<int>(argNum(ctx, argv[1]));
		if (digits < 0 || digits > 20) return jsThrow(ctx, "number.toFixed: digits out of range");
		char str[64];
		snprintf(str, sizeof(str), "%.*f", digits, f);
		return JS_NewString(ctx, str);
	}

	// input

	static JSValue js_input_enable(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "input.enable: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->inputCount) return jsThrow(ctx, "input.enable: bad index");
		ctxMap[ctx]->enableInput(i - 1);
		return JS_UNDEFINED;
	}

	static JSValue js_input_getVoltage(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "input.getVoltage: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->inputCount) return jsThrow(ctx, "input.getVoltage: bad index");
		uint8_t ch = 1;
		if (argc == 2) ch = static_cast<uint8_t>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "input.getVoltage: bad channel");
		return JS_NewFloat64(ctx, ctxMap[ctx]->getInputVoltage(i - 1, ch - 1));
	}

	static JSValue js_input_isHigh(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "input.isHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->inputCount) return jsThrow(ctx, "input.isHigh: bad index");
		uint8_t ch = 1;
		if (argc == 2) ch = static_cast<uint8_t>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "input.isHigh: bad channel");
		return JS_NewBool(ctx, ctxMap[ctx]->getInputVoltage(i - 1, ch - 1) > 0.7f);
	}

	static JSValue js_input_isLow(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "input.isLow: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->inputCount) return jsThrow(ctx, "input.isLow: bad index");
		uint8_t ch = 1;
		if (argc == 2) ch = static_cast<uint8_t>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "input.isLow: bad channel");
		return JS_NewBool(ctx, ctxMap[ctx]->getInputVoltage(i - 1, ch - 1) < 0.7f);
	}

	// trig

	static JSValue js_trig_getTicks(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "trig.getTicks: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->inputTrigCount) return jsThrow(ctx, "trig.getTicks: bad index");
		return JS_NewFloat64(ctx, double(ctxMap[ctx]->getTrigTicks(i - 1)));
	}

	static JSValue js_trig_isHigh(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.isHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->inputTrigCount) return jsThrow(ctx, "trig.isHigh: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.isHigh: bad channel");
		return JS_NewBool(ctx, ctxMap[ctx]->getTrigVoltage(i - 1, ch - 1) > 0.7f);
	}

	static JSValue js_trig_isLow(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.isLow: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->inputTrigCount) return jsThrow(ctx, "trig.isLow: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.isLow: bad channel");
		return JS_NewBool(ctx, ctxMap[ctx]->getTrigVoltage(i - 1, ch - 1) < 0.7f);
	}

	static JSValue js_trig_setGate(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if ((argc != 2 && argc != 3) || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1]) ||
			(argc == 3 && !argIsNumber(ctx, argv[2])))
			return jsThrow(ctx, "trig.setGate: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->outputTrigCount) return jsThrow(ctx, "trig.setGate: bad index");
		int ch = 1;
		if (argc == 3) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setGate: bad channel");
		float duration = argNum(ctx, argv[argc - 1]);
		ctxMap[ctx]->setTrig(i - 1, ch - 1, duration);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_setHigh(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.setHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->outputTrigCount) return jsThrow(ctx, "trig.setHigh: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setHigh: bad channel");
		ctxMap[ctx]->setTrigVoltage(i - 1, ch - 1, 10.f);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_setLow(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.setHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->outputTrigCount) return jsThrow(ctx, "trig.setLow: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setHigh: bad channel");
		ctxMap[ctx]->setTrigVoltage(i - 1, ch - 1, 0.f);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_setTrigger(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.setHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->outputTrigCount) return jsThrow(ctx, "trig.setTrigger: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setHigh: bad channel");
		ctxMap[ctx]->setTrig(i - 1, ch - 1);
		return JS_UNDEFINED;
	}

	// param

	static JSValue js_param_enable(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "param.enable: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->paramCount) return jsThrow(ctx, "param.enable: bad index");
		ctxMap[ctx]->enableParam(i - 1);
		return JS_UNDEFINED;
	}

	static JSValue js_param_getValue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "param.getValue: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > ctxMap[ctx]->paramCount) return jsThrow(ctx, "param.getValue: bad index");
		return JS_NewFloat64(ctx, ctxMap[ctx]->getParamValue(i - 1));
	}

	// midi

	// Extracts the message-handle argument shared by every midi.* accessor.
	static bool getMsgArg(JSContext* ctx, JSValueConst v, size_t& idx) {
		if (!JS_IsNumber(v)) return false;
		double d = 0;
		JS_ToFloat64(ctx, &d, v);
		if (d < 0) return false;
		idx = static_cast<size_t>(d);
		return idx < ctxMap[ctx]->msgCount;
	}

	static JSValue js_midi_isType(JSContext* ctx, int argc, JSValueConst* argv, uint8_t t, const char* n) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, string::f("midi.%s: invalid msg", n).c_str());
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == t);
	}

	// Warns when a message is created outside a callback (onMidiMessage/
	// onLoad/onUnload) — the store resets every callback, silently
	// invalidating such a handle before use.
	static void warnIfOutsideCallback(JSContext* ctx, const char* fn) {
		MidiScriptEngineQuickJs* e = ctxMap[ctx];
		if (!e->inCallback) {
			e->writeLog(string::f("%s: called outside a callback; the message "
				"is discarded when the next MIDI message arrives", fn), false);
		}
	}

	static JSValue js_midiOut_selectPort(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "midiOut.selectPort: bad args");
		int midiPort = static_cast<int>(argNum(ctx, argv[0]));
		if (midiPort < 1 || midiPort > ctxMap[ctx]->midiOutputCount) return jsThrow(ctx, "midiOut.selectPort: invalid output index");
		ctxMap[ctx]->selectedPort = midiPort - 1;
		return JS_UNDEFINED;
	}

	static JSValue js_midi_create(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "midi.create: bad args");
		warnIfOutsideCallback(ctx, "midi.create");
		size_t* s = &ctxMap[ctx]->msgCount;
		if (*s == msgStoreSize) return jsThrow(ctx, "midi.create: maximum reached");
		ctxMap[ctx]->msgStore[*s] = MessageEx();
		return JS_NewFloat64(ctx, double((*s)++));
	}

	static JSValue js_midi_clone(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.clone: invalid msg");
		warnIfOutsideCallback(ctx, "midi.clone");
		size_t* s = &ctxMap[ctx]->msgCount;
		if (*s == msgStoreSize) return jsThrow(ctx, "midi.clone: maximum reached");
		// Copy only the MIDI payload; the clone starts as a fresh, unsent
		// message (send/tick/midiPort/isNrpn at defaults) so it can be modified
		// and sent independently of the source.
		MessageEx clone;
		clone.msg = ctxMap[ctx]->msgStore[idx].msg;
		ctxMap[ctx]->msgStore[*s] = clone;
		return JS_NewFloat64(ctx, double((*s)++));
	}

	static JSValue js_midi_createNrpn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "midi.createNrpn: bad args");
		warnIfOutsideCallback(ctx, "midi.createNRPN");
		size_t* s = &ctxMap[ctx]->msgCount;
		if (*s + 4 >= msgStoreSize) return jsThrow(ctx, "midi.createNrpn: buffer maximum reached");
		ctxMap[ctx]->msgStore[*s + 0] = MessageEx();
		ctxMap[ctx]->msgStore[*s + 0].isNrpn = true;
		ctxMap[ctx]->msgStore[*s + 1] = MessageEx();
		ctxMap[ctx]->msgStore[*s + 2] = MessageEx();
		ctxMap[ctx]->msgStore[*s + 3] = MessageEx();
		size_t _s = *s;
		(*s) += 4;
		return JS_NewFloat64(ctx, double(_s));
	}

	static JSValue js_midi_getChanPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getChanPressure: invalid msg");
		return JS_NewFloat64(ctx, ctxMap[ctx]->msgStore[idx].msg.getNote());
	}

	static JSValue js_midi_getChannel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getChannel: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		// Status 0xf is the realtime/SysEx family (clock, start/stop/continue,
		// SysEx framing) — none of those carry a channel, and the low nibble
		// is a sub-type selector instead (see the is* predicates below), so
		// returning a plausible-looking channel number there would be
		// meaningless. -1 is unambiguous: 1-16 is the only valid channel
		// range, so a script can check `> 0` without needing to special-case
		// realtime messages via try/catch.
		if (s.msg.getStatus() == 0xf) return JS_NewFloat64(ctx, -1);
		return JS_NewFloat64(ctx, s.msg.getChannel() + 1);
	}

	static JSValue js_midi_getLength(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getLength: invalid msg");
		return JS_NewFloat64(ctx, ctxMap[ctx]->msgStore[idx].msg.getSize());
	}

	static JSValue js_midi_getNote(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getNote: invalid msg");
		return JS_NewFloat64(ctx, ctxMap[ctx]->msgStore[idx].msg.getNote());
	}

	static JSValue js_midi_getPitchWheel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getPitchWheel: invalid msg");
		Message& msg = ctxMap[ctx]->msgStore[idx].msg;
		uint16_t value = (static_cast<uint16_t>(msg.getValue()) << 7) | msg.getNote();
		return JS_NewFloat64(ctx, value);
	}

	static JSValue js_midi_getProgramChange(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getProgramChange: invalid msg");
		return JS_NewFloat64(ctx, ctxMap[ctx]->msgStore[idx].msg.getNote());
	}

	static JSValue js_midi_getSysEx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getSysEx: invalid msg");
		Message& msg = ctxMap[ctx]->msgStore[idx].msg;
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 1; i < msg.getSize() - 1; i++) {
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(msg.bytes[i]);
		}
		std::string str = ss.str();
		return JS_NewStringLen(ctx, str.c_str(), str.length());
	}

	static JSValue js_midi_getSysExLength(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getSysExLength: invalid msg");
		// Payload length only — the f0/f7 framing is excluded, so a script
		// can check the size before reading the payload with getSysEx.
		return JS_NewFloat64(ctx, std::max(0, ctxMap[ctx]->msgStore[idx].msg.getSize() - 2));
	}

	static JSValue js_midi_getRaw(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getRaw: invalid msg");
		Message& msg = ctxMap[ctx]->msgStore[idx].msg;
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 0; i < msg.getSize(); i++) {
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(msg.bytes[i]);
		}
		std::string str = ss.str();
		return JS_NewStringLen(ctx, str.c_str(), str.length());
	}

	static JSValue js_midi_getValue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getValue: invalid msg");
		return JS_NewFloat64(ctx, ctxMap[ctx]->msgStore[idx].msg.getValue());
	}

	static JSValue js_midi_isCc(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isType(ctx, argc, argv, 0xb, "isCc");
	}

	static JSValue js_midi_isChanPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isType(ctx, argc, argv, 0xd, "isChannelPressure");
	}

	static JSValue js_midi_isClock(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isClock: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0x8);
	}

	static JSValue js_midi_isContinue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isContinue: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xb);
	}

	static JSValue js_midi_isKeyPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isType(ctx, argc, argv, 0xa, "isKeyPressure");
	}

	static JSValue js_midi_isNoteOff(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isType(ctx, argc, argv, 0x8, "isNoteOff");
	}

	static JSValue js_midi_isNoteOn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isType(ctx, argc, argv, 0x9, "isNoteOn");
	}

	static JSValue js_midi_isPitchWheel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isType(ctx, argc, argv, 0xe, "isPitchWheel");
	}

	static JSValue js_midi_isProgramChange(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isType(ctx, argc, argv, 0xc, "isProgramChange");
	}

	static JSValue js_midi_isStart(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isStart: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xa);
	}

	static JSValue js_midi_isStop(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isStop: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xc);
	}

	static JSValue js_midi_isSysEx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isSysEx: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0x0);
	}

	static JSValue js_midi_setCc(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 4 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
			return jsThrow(ctx, "midi.setCc: bad args");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t cc = static_cast<uint8_t>(argNum(ctx, argv[2]));
		uint8_t value = std::max(0, std::min(127, static_cast<int>(argNum(ctx, argv[3]))));
		if (s.msg.getSize() != 3) s.msg.setSize(3);
		s.msg.setStatus(0xb);
		s.msg.setChannel(ch - 1);
		s.msg.setNote(cc);
		s.msg.setValue(value);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setCc14bit(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx1, idx2;
		if (argc < 5 || !getMsgArg(ctx, argv[0], idx1) || !getMsgArg(ctx, argv[1], idx2) ||
			!argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]) || !argIsNumber(ctx, argv[4]))
			return jsThrow(ctx, "midi.setCc14bit: invalid msg");
		MessageEx& s1 = ctxMap[ctx]->msgStore[idx1];
		MessageEx& s2 = ctxMap[ctx]->msgStore[idx2];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[2]))));
		uint8_t cc = static_cast<uint8_t>(argNum(ctx, argv[3]));
		double value = argNum(ctx, argv[4]);
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
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setChannel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midi.setChannel: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		s.msg.setChannel(ch - 1);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setChanPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 3 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "midi.setChanPressure: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t value = static_cast<uint8_t>(argNum(ctx, argv[2]));
		// Channel pressure is a 2-byte message (status + pressure), not 3 —
		// the pressure lives in bytes[1], read back via getChanPressure/getNote.
		if (s.msg.getSize() != 2) s.msg.setSize(2);
		s.msg.setStatus(0xd);
		s.msg.setChannel(ch - 1);
		s.msg.setNote(value);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setKeyPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 4 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
			return jsThrow(ctx, "midi.setKeyPressure: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t note = static_cast<uint8_t>(argNum(ctx, argv[2]));
		uint8_t vel = std::max(0, std::min(127, static_cast<int>(argNum(ctx, argv[3]))));
		if (s.msg.getSize() != 3) s.msg.setSize(3);
		s.msg.setStatus(0xa);
		s.msg.setChannel(ch - 1);
		s.msg.setNote(note);
		s.msg.setValue(vel);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setNote(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midi.setNote: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t value = static_cast<uint8_t>(argNum(ctx, argv[1]));
		s.msg.setNote(value);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setNoteOff(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		// midi.setNoteOff(msg, channel, note [, velocity])
		size_t idx;
		if ((argc != 3 && argc != 4) || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) ||
			(argc == 4 && !argIsNumber(ctx, argv[3])))
			return jsThrow(ctx, "midi.setNoteOff: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t note = static_cast<uint8_t>(argNum(ctx, argv[2]));
		uint8_t vel = argc >= 4 ? std::max(0, std::min(127, static_cast<int>(argNum(ctx, argv[3])))) : 0;
		if (s.msg.getSize() != 3) s.msg.setSize(3);
		s.msg.setStatus(0x8);
		s.msg.setChannel(ch - 1);
		s.msg.setNote(note);
		s.msg.setValue(vel);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setNoteOn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 4 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
			return jsThrow(ctx, "midi.setNoteOn: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t note = static_cast<uint8_t>(argNum(ctx, argv[2]));
		uint8_t vel = std::max(0, std::min(127, static_cast<int>(argNum(ctx, argv[3]))));
		if (s.msg.getSize() != 3) s.msg.setSize(3);
		s.msg.setStatus(0x9);
		s.msg.setChannel(ch - 1);
		s.msg.setNote(note);
		s.msg.setValue(vel);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setNrpn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 4 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
			return jsThrow(ctx, "midi.setNrpn: invalid args");
		MessageEx* s1 = &ctxMap[ctx]->msgStore[idx];
		if (!s1->isNrpn) return jsThrow(ctx, "midi.setNrpn: invalid nrpn message");
		MessageEx* s2 = &ctxMap[ctx]->msgStore[idx + 1];
		MessageEx* s3 = &ctxMap[ctx]->msgStore[idx + 2];
		MessageEx* s4 = &ctxMap[ctx]->msgStore[idx + 3];

		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint16_t number = static_cast<uint16_t>(argNum(ctx, argv[2]));
		uint16_t value = static_cast<uint16_t>(argNum(ctx, argv[3]));
		// Spec order: NRPN MSB, NRPN LSB, Data Entry MSB, Data Entry LSB.
		// flushMsgStore() sends s1..s4 in this order, and MidiProcessor's NRPN
		// state machine (CC99 sets pending MSB, CC98 completes selection;
		// CC6 sets pending data MSB, CC38 completes the value) requires it.
		s1->msg.setStatus(0xb);
		s1->msg.setChannel(ch - 1);
		s1->msg.setNote(99);
		s1->msg.setValue((number >> 7) & 0x7f);
		s2->msg.setStatus(0xb);
		s2->msg.setChannel(ch - 1);
		s2->msg.setNote(98);
		s2->msg.setValue(number & 0x7f);
		s3->msg.setStatus(0xb);
		s3->msg.setChannel(ch - 1);
		s3->msg.setNote(6);
		s3->msg.setValue((value >> 7) & 0x7f);
		s4->msg.setStatus(0xb);
		s4->msg.setChannel(ch - 1);
		s4->msg.setNote(38);
		s4->msg.setValue(value & 0x7f);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setPitchWheel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 3 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "midi.setPitchWheel: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint16_t value = static_cast<uint16_t>(argNum(ctx, argv[2]));
		if (s.msg.getSize() != 3) s.msg.setSize(3);
		s.msg.setStatus(0xe);
		s.msg.setChannel(ch - 1);
		s.msg.setNote(value & 0x7f);
		s.msg.setValue((value >> 7) & 0x7f);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setProgramChange(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 3 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "midi.setProgramChange: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t prg = static_cast<uint8_t>(argNum(ctx, argv[2]));
		if (s.msg.getSize() != 3) s.msg.setSize(3);
		s.msg.setStatus(0xc);
		s.msg.setChannel(ch - 1);
		s.msg.setNote(prg);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setRaw(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !JS_IsString(argv[1])) return jsThrow(ctx, "midi.setRaw: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		std::string data = ctxMap[ctx]->jsToStdString(argv[1]);
		if (data.length() % 2 != 0) {
			return jsThrow(ctx, "midi.setRaw: invalid string length");
		}
		if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
			return jsThrow(ctx, "midi.setRaw: invalid hexstring");
		}
		s.msg.setSize(data.length() / 2);
		for (size_t i = 0; i < data.length(); i += 2) {
			std::string bs = data.substr(i, 2);
			char byte = static_cast<char>(strtol(bs.c_str(), NULL, 16));
			s.msg.bytes[i / 2] = byte;
		}
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setSysEx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !JS_IsString(argv[1])) return jsThrow(ctx, "midi.setSysEx: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		std::string data = ctxMap[ctx]->jsToStdString(argv[1]);
		if (data.length() % 2 != 0) {
			return jsThrow(ctx, "midi.setSysEx: invalid string length");
		}
		if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
			return jsThrow(ctx, "midi.setSysEx: invalid hexstring");
		}
		if (data.length() / 2 > MidiScriptEngine::sysExMaxPayloadLength) {
			return jsThrow(ctx, string::f("midi.setSysEx: payload exceeds maximum of %d bytes", MidiScriptEngine::sysExMaxPayloadLength).c_str());
		}
		for (size_t i = 0; i < data.length(); i += 2) {
			uint8_t byte = static_cast<uint8_t>(strtol(data.substr(i, 2).c_str(), NULL, 16));
			if (byte > 0x7f) {
				return jsThrow(ctx, "midi.setSysEx: payload bytes must be 7-bit (00-7f)");
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
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setValue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midi.setValue: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		uint8_t value = static_cast<uint8_t>(argNum(ctx, argv[1]));
		s.msg.setValue(value);
		return JS_UNDEFINED;
	}

	// midiOut

	static JSValue js_midiOut_send(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midiOut.send: invalid msg");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		s.midiPort = ctxMap[ctx]->selectedPort;
		s.send = true;
		s.msg.frame = -1;
		return JS_UNDEFINED;
	}

	static JSValue js_midiOut_sendAfterMs(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midiOut.sendAfterMs: bad args");
		MessageEx& s = ctxMap[ctx]->msgStore[idx];
		s.midiPort = ctxMap[ctx]->selectedPort;
		double ms = argNum(ctx, argv[1]);
		int64_t currentFrame = APP->engine->getFrame();
		int64_t frame = ms / 1000.f / APP->engine->getSampleTime();
		s.send = true;
		s.msg.frame = currentFrame + frame;
		return JS_UNDEFINED;
	}

	static JSValue js_midiOut_sendAfterTrigger(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc == 2) {
			if (!getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
			MessageEx& s = ctxMap[ctx]->msgStore[idx];
			s.midiPort = ctxMap[ctx]->selectedPort;
			int64_t currentTicks = ctxMap[ctx]->getTrigTicks(0);
			int ticks = static_cast<int>(argNum(ctx, argv[1]));
			s.send = true;
			s.tick = currentTicks + ticks;
			return JS_UNDEFINED;
		}
		if (argc == 3) {
			if (!getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
				return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
			int trigPort = static_cast<int>(argNum(ctx, argv[1]));
			if (trigPort < 1 || trigPort > ctxMap[ctx]->inputTrigCount) return jsThrow(ctx, "midiOut.sendAfterTrigger: bad trigInput index");
			MessageEx& s = ctxMap[ctx]->msgStore[idx];
			s.midiPort = ctxMap[ctx]->selectedPort;
			int64_t currentTicks = ctxMap[ctx]->getTrigTicks(trigPort - 1);
			int ticks = static_cast<int>(argNum(ctx, argv[2]));
			s.send = true;
			s.tick = currentTicks + ticks;
			return JS_UNDEFINED;
		}

		return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
	}
};

// std::map is not thread-safe by default but new script engines are only created by inserting a new module
// which needs all Rack's engine-threads to synchronize anyway. Access to each JSContext* is not "const" but
// only done from one thread for each JSContext* - thread-safety should be no problem here.
std::map<JSContext*, MidiScriptEngineQuickJs*> MidiScriptEngineQuickJs::ctxMap;

} // namespace QuickJs
} // namespace MidiScript
} // namespace StoermelderPackOne
