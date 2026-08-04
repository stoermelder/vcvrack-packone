#include "MidiScriptEngine.h"
#include "../../utils/TaskWorker.hpp"
#include "../../../dep/quickjs/quickjs.h"
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <regex>
#include <unordered_map>

namespace StoermelderPackOne {
namespace MidiScript {
namespace QuickJs {


struct MidiScriptEngineQuickJs : MidiScriptEngine {

	MidiScriptEngineQuickJs(MidiScriptEngineHandler* handler, int inputCount, int inputTrigCount, int outputTrigCount, int paramCount, int midiInputCount, int midiOutputCount)
		: MidiScriptEngine(handler, inputCount, inputTrigCount, outputTrigCount, paramCount, midiInputCount, midiOutputCount) {}

	struct MessageEx {
		int midiPort = 0;
		Message msg;
		bool isNrpn = false;
		bool send = false;
		uint64_t tick = 0;
		// Monotonic stamp assigned when midiOut.send() is called, so the out
		// queue can be flushed in send() order rather than handle order.
		size_t sendOrder = 0;
	};

	// Retrieves the engine instance owning ctx. Stashed via JS_SetContextOpaque
	// at context creation (loadScript()) — O(1), and avoids a shared map that
	// would otherwise be mutated (loadScript()/closeState(), GUI thread) and
	// read (every native callback below, worker thread) without synchronization.
	static MidiScriptEngineQuickJs* getEngine(JSContext* ctx) {
		return static_cast<MidiScriptEngineQuickJs*>(JS_GetContextOpaque(ctx));
	}

	JSRuntime* rt = NULL;
	JSContext* ctx = NULL;

	static const size_t memoryLimit = 1024 * 1024;

	const static int msgStoreSize = 32;
	MessageEx msgStore[msgStoreSize];
	// Must be initialised: top-level script code runs during loadScript(), before
	// process() sets this to 1, and it bounds every msgStore index check below.
	size_t msgCount = 0;
	// Next value handed to MessageEx::sendOrder. Never reset: it only needs to
	// be monotonic within a single callback, and the store is reset per callback.
	size_t sendCounter = 0;
	// True only while onMidiMessage() is executing. The message store is reset
	// on every callback, so handles created outside one are silently
	// invalidated — this lets midi.create() warn instead of failing quietly.
	bool inCallback = false;
	// Sticky output port selected via midiOut.selectPort(), 0-based. Stays in
	// effect across callbacks until changed again.
	int selectedPort = 0;

	// Script-registered context menus. The JSValue callback lives here (not in
	// ScriptMenuItem) because it is only ever touched on the worker thread;
	// the UI thread reads presentation copies through getContextMenus().
	struct ContextMenuEntry {
		ScriptMenuItem spec;
		JSValue callbackFn;
		JSValue onGetValueFn = JS_UNDEFINED;
	};
	std::unordered_map<int, ContextMenuEntry> contextMenus;
	int nextContextMenuCallbackId = 1;
	// Guards contextMenus/nextContextMenuCallbackId against concurrent
	// access from the UI thread (menu build / click) and the worker thread
	// (registerContextMenu, callback dispatch). Never held while running a
	// script callback, or a callback that re-registers would deadlock.
	mutable std::mutex contextMenusMutex;

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

	// Engine selection: true when this script's header declares QuickJs. The
	// same simple substring check the module used to run itself (Q26) — kept
	// here so the module has no third header parser.
	bool testScript(const std::string& script) override {
		return script.find("@engine QuickJs") != std::string::npos;
	}

	void loadScript(const char* script, const std::string& persistedConfigJson = "") override {
		closeState();
		resetTipsyOutput();

		if (script[0] == '\0') {
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
			handler->writeLog("Script is not compatible with MIDI-KIT", false);
			return;
		}

		if (topics.find("author") != topics.end()) {
			handler->writeLog(string::f("Author: %s", topics["author"].c_str()), false);
		}
		if (topics.find("description") != topics.end()) {
			handler->writeLog(topics["description"], false);
		}

		rt = JS_NewRuntime();
		JS_SetMemoryLimit(rt, memoryLimit);
		JS_SetMaxStackSize(rt, 256 * 1024);
		ctx = JS_NewContext(rt);
		JS_SetContextOpaque(ctx, this);

		registerApi();

		JSValue r = JS_Eval(ctx, script, strlen(script), "script", JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			handler->writeLog("Error while loading script", false);
			handler->writeLog(formatError(exc), false);
			JS_FreeValue(ctx, exc);
			closeState();
		}
		else {
			JS_FreeValue(ctx, r);
			handler->writeLog("Script loaded", false);

			// Callbacks live on the rack object (rack.onMidiMessage etc.), not
			// on the global scope.
			JSValue glob = JS_GetGlobalObject(ctx);
			JSValue rack = JS_GetPropertyStr(ctx, glob, "rack");
			hasOnLoad = isCallableProp(rack, "onLoad");
			hasOnUnload = isCallableProp(rack, "onUnload");
			hasOnMidiMessage = isCallableProp(rack, "onMidiMessage");
			hasOnTrigger = isCallableProp(rack, "onTrigger");
			JS_FreeValue(ctx, rack);
			JS_FreeValue(ctx, glob);

			if (!hasOnMidiMessage) {
				handler->writeLog("No onMidiMessage(midiPort, msg) function defined — incoming MIDI is ignored", false);
			}
			// Pass any persisted config to onLoad(). parsePersistedConfig()
			// returns JS_UNDEFINED when there is none (or it is not valid
			// JSON), so the script falls back to its defaults.
			JSValue config = parsePersistedConfig(persistedConfigJson);
			callOnLoad(config);
			// JS_UNDEFINED is a shared atom; JS_FreeValue is a no-op for it.
			JS_FreeValue(ctx, config);
		}
	}

	bool isCallableProp(JSValueConst obj, const char* name) {
		JSValue v = JS_GetPropertyStr(ctx, obj, name);
		bool r = JS_IsFunction(ctx, v);
		JS_FreeValue(ctx, v);
		return r;
	}

	// Runs rack.onUnload() and returns the JSON string of the value it
	// returned — the script's config to persist — without tearing down the
	// script state. Used by dataToJson() at save time. The messages onUnload()
	// queued are discarded (never flushed), so saving has no audible effect.
	std::string captureConfig() override {
		if (!ctx) return "";
		JSValue ret = callOnUnload();
		std::string configJson;
		if (!JS_IsUndefined(ret) && !JS_IsNull(ret) && !JS_IsException(ret)) {
			JSValue jsonVal = JS_JSONStringify(ctx, ret, JS_UNDEFINED, JS_UNDEFINED);
			if (!JS_IsException(jsonVal)) {
				configJson = jsToStdString(jsonVal);
			}
			JS_FreeValue(ctx, jsonVal);
		}
		JS_FreeValue(ctx, ret);
		return configJson;
	}

	// Frees the QuickJS runtime/context. Runs onUnload() first (via
	// captureConfig()) so the script's teardown messages and its config can be
	// captured; returns the captured config JSON.
	std::string closeState() override {
		if (ctx != NULL) {
			std::string configJson = captureConfig();
			// onUnload()'s teardown messages (e.g. all-notes-off) must still
			// go out even though captureConfig() itself did not flush them.
			flushMsgStore();
			clearContextMenus();
			JS_FreeContext(ctx);
			JS_FreeRuntime(rt);
			ctx = NULL;
			rt = NULL;
			return configJson;
		}
		return "";
	}

	// Runs the script's onLoad() hook after load, passing the parsed persisted
	// config as its single argument (or no argument when none was persisted).
	void callOnLoad(JSValue persistedConfig) {
		if (!hasOnLoad) return;

		msgCount = 0;
		inCallback = true;
		JSValue glob = JS_GetGlobalObject(ctx);
		JSValue rack = JS_GetPropertyStr(ctx, glob, "rack");
		JSValue fn = JS_GetPropertyStr(ctx, rack, "onLoad");
		JSValue r;
		if (JS_IsUndefined(persistedConfig)) {
			r = JS_Call(ctx, fn, rack, 0, NULL);
		}
		else {
			JSValue args[1] = { persistedConfig };
			r = JS_Call(ctx, fn, rack, 1, args);
		}
		JS_FreeValue(ctx, fn);
		JS_FreeValue(ctx, rack);
		JS_FreeValue(ctx, glob);
		inCallback = false;
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			handler->writeLog(string::f("onLoad error: %s", jsToStdString(exc).c_str()));
			JS_FreeValue(ctx, exc);
		}
		else {
			JS_FreeValue(ctx, r);
		}
		flushMsgStore();
	}

	// Runs the script's onUnload() hook and returns its return value — the
	// script's config to persist. Returns JS_UNDEFINED when the hook is
	// missing or errored. The caller owns the returned value. onUnload()'s
	// messages are NOT flushed here: closeState() flushes them (teardown
	// cleanup like all-notes-off must still go out), captureConfig() discards
	// them (a save must not have audible side effects).
	JSValue callOnUnload() {
		if (!hasOnUnload) return JS_UNDEFINED;

		msgCount = 0;
		inCallback = true;
		JSValue glob = JS_GetGlobalObject(ctx);
		JSValue rack = JS_GetPropertyStr(ctx, glob, "rack");
		JSValue fn = JS_GetPropertyStr(ctx, rack, "onUnload");
		JSValue r = JS_Call(ctx, fn, rack, 0, NULL);
		JS_FreeValue(ctx, fn);
		JS_FreeValue(ctx, rack);
		JS_FreeValue(ctx, glob);
		inCallback = false;
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			handler->writeLog(string::f("onUnload error: %s", jsToStdString(exc).c_str()));
			JS_FreeValue(ctx, exc);
			flushMsgStore();
			return JS_UNDEFINED;
		}
		return r;
	}

	// Parses a persisted-config JSON string into a JSValue for rack.onLoad(),
	// or JS_UNDEFINED when the string is empty or not valid JSON. The caller
	// must JS_FreeValue the result (a no-op for JS_UNDEFINED).
	JSValue parsePersistedConfig(const std::string& json) {
		if (json.empty()) return JS_UNDEFINED;
		JSValue parsed = JS_ParseJSON(ctx, json.c_str(), json.size(), "<config>");
		if (JS_IsException(parsed)) {
			JS_FreeValue(ctx, parsed);
			JSValue exc = JS_GetException(ctx);
			handler->writeLog(string::f("Ignoring invalid persisted script config: %s", jsToStdString(exc).c_str()), false);
			JS_FreeValue(ctx, exc);
			return JS_UNDEFINED;
		}
		return parsed;
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
				JSValue rack = JS_GetPropertyStr(ctx, glob, "rack");
				JSValue fn = JS_GetPropertyStr(ctx, rack, "onMidiMessage");
				JSValue args[2] = { JS_NewInt32(ctx, midiPort + 1), JS_NewInt32(ctx, 0) };
				JSValue r = JS_Call(ctx, fn, rack, 2, args);
				JS_FreeValue(ctx, args[0]);
				JS_FreeValue(ctx, args[1]);
				JS_FreeValue(ctx, fn);
				JS_FreeValue(ctx, rack);
				JS_FreeValue(ctx, glob);
				inCallback = false;
				if (JS_IsException(r)) {
					JS_FreeValue(ctx, r);
					JSValue exc = JS_GetException(ctx);
					handler->writeLog(string::f("onMidiMessage error: %s", jsToStdString(exc).c_str()));
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
				JSValue rack = JS_GetPropertyStr(ctx, glob, "rack");
				JSValue fn = JS_GetPropertyStr(ctx, rack, "onTrigger");
				JSValue arg = JS_NewInt32(ctx, trigPort + 1);
				JSValue r = JS_Call(ctx, fn, rack, 1, &arg);
				JS_FreeValue(ctx, arg);
				JS_FreeValue(ctx, fn);
				JS_FreeValue(ctx, rack);
				JS_FreeValue(ctx, glob);
				inCallback = false;
				if (JS_IsException(r)) {
					JS_FreeValue(ctx, r);
					JSValue exc = JS_GetException(ctx);
					handler->writeLog(string::f("onTrigger error: %s", jsToStdString(exc).c_str()));
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
	// Messages are emitted in the order midiOut.send() was called (sendOrder),
	// not in handle-creation order: a script may create several messages and
	// send them in a different order, and the receiver must observe send() order.
	void flushMsgStore() {
		std::vector<size_t> order;
		for (size_t i = 0; i < msgCount; i++) {
			if (msgStore[i].send) order.push_back(i);
		}
		std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
			return msgStore[a].sendOrder < msgStore[b].sendOrder;
		});
		for (size_t i : order) {
			midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i].msg, msgStore[i].tick));
			if (msgStore[i].isNrpn) {
				midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i + 1].msg, msgStore[i].tick));
				midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i + 2].msg, msgStore[i].tick));
				midiOutQueue.push(std::make_tuple(msgStore[i].midiPort, msgStore[i + 3].msg, msgStore[i].tick));
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

	void clearContextMenus() {
		std::lock_guard<std::mutex> lock(contextMenusMutex);
		if (ctx) {
			// Frees the stored script callbacks. Only safe on this (UI) thread
			// while ctx is still alive: closeState() calls this before freeing
			// the context, and every JS operation happens on this thread.
			for (auto& kv : contextMenus) {
				JS_FreeValue(ctx, kv.second.callbackFn);
				// JS_FreeValue on JS_UNDEFINED is a safe no-op.
				JS_FreeValue(ctx, kv.second.onGetValueFn);
			}
		}
		contextMenus.clear();
		nextContextMenuCallbackId = 1;
	}

	void getContextMenus(const std::function<void(const std::vector<ScriptMenuItem>&)>& callback) override {
		// The whole snapshot is built on the worker thread, so getContextMenus()
		// itself needs no lock/copy on the UI thread. Each onGetValue function
		// is dup'd here under the same lock that guards the map — QuickJS
		// refcounts are not atomic, so JS_DupValue must only run on the worker
		// thread. The lock is released before any script call below.
		runAsync([this, callback]() {
			if (!ctx) return;
			struct Snapshot { int id; ScriptMenuItem spec; JSValue onGetValueFn; };
			std::vector<Snapshot> snap;
			{
				std::lock_guard<std::mutex> lock(contextMenusMutex);
				snap.reserve(contextMenus.size());
				for (const auto& kv : contextMenus) {
					snap.push_back({kv.first, kv.second.spec, JS_DupValue(ctx, kv.second.onGetValueFn)});
				}
			}
			// callbackIds are assigned monotonically at registration, so sorting
			// by them yields registration order — the unordered_map's own
			// iteration order is unspecified.
			std::sort(snap.begin(), snap.end(), [](const Snapshot& a, const Snapshot& b) {
				return a.id < b.id;
			});
			std::vector<ScriptMenuItem> result;
			result.reserve(snap.size());
			JSValue glob = JS_GetGlobalObject(ctx);
			JSValue rack = JS_GetPropertyStr(ctx, glob, "rack");
			for (const Snapshot& s : snap) {
				ScriptMenuItem spec = s.spec;
				// Each snapshot owns one dup'd reference; freed after use below.
				JSValue fn = s.onGetValueFn;
				if (JS_IsFunction(ctx, fn)) {
					JSValue r = JS_Call(ctx, fn, rack, 0, NULL);
					if (JS_IsException(r)) {
						JS_FreeValue(ctx, r);
						JSValue exc = JS_GetException(ctx);
						handler->writeLog(string::f("Context menu error: %s", jsToStdString(exc).c_str()));
						JS_FreeValue(ctx, exc);
					}
					else {
						if (spec.type == ScriptMenuItem::Type::Boolean) {
							int b = JS_ToBool(ctx, r);
							if (b >= 0) spec.checked = (b != 0);
						}
						else {
							double d = 0;
							if (JS_ToFloat64(ctx, &d, r) >= 0) {
								int sel = static_cast<int>(d);
								spec.selected = std::max(0, std::min(sel, static_cast<int>(spec.options.size()) - 1));
							}
						}
						JS_FreeValue(ctx, r);
					}
				}
				JS_FreeValue(ctx, fn);
				result.push_back(spec);
			}
			JS_FreeValue(ctx, rack);
			JS_FreeValue(ctx, glob);
			// Invoke the caller's callback with the evaluated specs. It only
			// touches memory the caller owns (never constructs widgets), so it
			// is safe to run here on the worker thread.
			callback(result);
		});
	}

	// Fires a registered menu item's onChange callback on the worker thread.
	// The presentation state is not stored on the spec — the next menu build
	// re-evaluates it from onGetValue, so whatever onChange changed in the
	// script's config is picked up automatically. The actual script call is
	// deferred to runAsync() because it must run on the worker thread
	// alongside all other JS work.
	void invokeContextMenuCallback(int callbackId, int value) override {
		ScriptMenuItem::Type type;
		std::string label;
		{
			std::lock_guard<std::mutex> lock(contextMenusMutex);
			auto it = contextMenus.find(callbackId);
			if (it == contextMenus.end()) return;
			const ContextMenuEntry& entry = it->second;
			type = entry.spec.type;
			if (type != ScriptMenuItem::Type::Boolean) {
				if (value < 0 || value >= static_cast<int>(entry.spec.options.size())) return;
				label = entry.spec.options[value];
			}
		}

		runAsync([this, callbackId, value, type, label]() {
			if (!ctx) return;
			JSValue fn;
			{
				std::lock_guard<std::mutex> lock(contextMenusMutex);
				auto it = contextMenus.find(callbackId);
				if (it == contextMenus.end()) return;
				// Dup on the worker thread only: QuickJS refcounts are not
				// atomic, so JS_DupValue must not run on the UI thread.
				fn = JS_DupValue(ctx, it->second.callbackFn);
			}

			JSValue glob = JS_GetGlobalObject(ctx);
			JSValue rack = JS_GetPropertyStr(ctx, glob, "rack");
			JSValue args[2];
			int argc;
			if (type == ScriptMenuItem::Type::Boolean) {
				args[0] = JS_NewBool(ctx, value != 0);
				argc = 1;
			}
			else {
				args[0] = JS_NewInt32(ctx, value);
				args[1] = JS_NewString(ctx, label.c_str());
				argc = 2;
			}
			JSValue r = JS_Call(ctx, fn, rack, argc, args);
			for (int i = 0; i < argc; i++) JS_FreeValue(ctx, args[i]);
			JS_FreeValue(ctx, fn);
			JS_FreeValue(ctx, rack);
			JS_FreeValue(ctx, glob);
			if (JS_IsException(r)) {
				JS_FreeValue(ctx, r);
				JSValue exc = JS_GetException(ctx);
				handler->writeLog(string::f("Context menu callback error: %s", jsToStdString(exc).c_str()));
				JS_FreeValue(ctx, exc);
			}
			else {
				JS_FreeValue(ctx, r);
			}
		});
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
		JS_SetPropertyStr(ctx, _rack, "random", JS_NewCFunction(ctx, js_rack_random, "random", 0));
		JS_SetPropertyStr(ctx, _rack, "registerContextMenu", JS_NewCFunction(ctx, js_rack_registerContextMenu, "registerContextMenu", 1));

		// number
		JSValue _number = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, glob, "number", _number);
		JS_SetPropertyStr(ctx, _number, "crossfade", JS_NewCFunction(ctx, js_number_crossfade, "crossfade", 3));
		JS_SetPropertyStr(ctx, _number, "rescale", JS_NewCFunction(ctx, js_number_rescale, "rescale", 5));
		JS_SetPropertyStr(ctx, _number, "toString", JS_NewCFunction(ctx, js_number_toString, "toString", 1));

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
		JS_SetPropertyStr(ctx, _trig, "sendTipsy", JS_NewCFunction(ctx, js_trig_sendTipsy, "sendTipsy", 2));

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
		getEngine(ctx)->handler->writeLog(log);
		return JS_UNDEFINED;
	}

	static JSValue js_rack_overlay(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 3 || !JS_IsString(argv[0])) return jsThrow(ctx, "overlay: bad args");
		for (int i = 0; i < argc; i++) {
			if (!JS_IsString(argv[i])) return jsThrow(ctx, "overlay: bad args");
		}
		std::string s1 = argc >= 1 ? getEngine(ctx)->jsToStdString(argv[0]) : "";
		std::string s2 = argc >= 2 ? getEngine(ctx)->jsToStdString(argv[1]) : "";
		std::string s3 = argc >= 3 ? getEngine(ctx)->jsToStdString(argv[2]) : "";
		getEngine(ctx)->handler->writeOverlay(s1, s2, s3);
		return JS_UNDEFINED;
	}

	static JSValue js_rack_getFrame(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return JS_NewFloat64(ctx, double(APP->engine->getFrame()));
	}

	// number

	static JSValue js_number_crossfade(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 3 || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "number.crossfade: bad args");
		float a = argNum(ctx, argv[0]);
		float b = argNum(ctx, argv[1]);
		float p = argNum(ctx, argv[2]);
		return JS_NewFloat64(ctx, rack::crossfade(a, b, p));
	}

	static JSValue js_rack_random(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "rack.random: bad args");
		return JS_NewFloat64(ctx, rack::random::uniform());
	}

	// rack.registerContextMenu(options) — registers one item in the module's
	// context menu. Two variants:
	//   { type: "boolean", label, onGetValue: fn() -> bool, onChange: fn(checked) }
	//   { type: "options", label, options: [..], onGetValue: fn() -> int, onChange: fn(idx, label) }
	// onGetValue is optional (defaults to value 0) and is evaluated lazily on
	// the worker thread whenever the menu is built, so it always reflects the
	// script's current config — unlike a value captured at registration time.
	// Returns true on success. The script callbacks are stored (owned) by the
	// engine and fired through invokeContextMenuCallback()/getContextMenus()
	// on the worker thread.
	static JSValue js_rack_registerContextMenu(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		MidiScriptEngineQuickJs* e = getEngine(ctx);
		if (argc < 1 || !JS_IsObject(argv[0])) return jsThrow(ctx, "registerContextMenu: bad args");

		ScriptMenuItem spec;

		JSValue typeV = JS_GetPropertyStr(ctx, argv[0], "type");
		std::string type = JS_IsString(typeV) ? e->jsToStdString(typeV) : "";
		JS_FreeValue(ctx, typeV);
		if (type == "options") spec.type = ScriptMenuItem::Type::Options;
		else if (type == "boolean") spec.type = ScriptMenuItem::Type::Boolean;
		else return jsThrow(ctx, "registerContextMenu: type must be \"boolean\" or \"options\"");

		JSValue labelV = JS_GetPropertyStr(ctx, argv[0], "label");
		std::string label = JS_IsString(labelV) ? e->jsToStdString(labelV) : "";
		JS_FreeValue(ctx, labelV);
		if (label.empty()) return jsThrow(ctx, "registerContextMenu: label must be a non-empty string");
		spec.label = label;

		JSValue onChangeV = JS_GetPropertyStr(ctx, argv[0], "onChange");
		if (!JS_IsFunction(ctx, onChangeV)) {
			JS_FreeValue(ctx, onChangeV);
			return jsThrow(ctx, "registerContextMenu: onChange must be a function");
		}

		if (spec.type == ScriptMenuItem::Type::Options) {
			JSValue optionsV = JS_GetPropertyStr(ctx, argv[0], "options");
			if (!JS_IsArray(ctx, optionsV)) {
				JS_FreeValue(ctx, optionsV);
				JS_FreeValue(ctx, onChangeV);
				return jsThrow(ctx, "registerContextMenu: options must be a non-empty array of strings");
			}
			JSValue lengthV = JS_GetPropertyStr(ctx, optionsV, "length");
			uint32_t len = 0;
			bool lenOk = (JS_ToUint32(ctx, &len, lengthV) >= 0);
			JS_FreeValue(ctx, lengthV);
			if (!lenOk || len == 0) {
				JS_FreeValue(ctx, optionsV);
				JS_FreeValue(ctx, onChangeV);
				return jsThrow(ctx, "registerContextMenu: options must be a non-empty array of strings");
			}
			spec.options.resize(len);
			for (uint32_t i = 0; i < len; i++) {
				JSValue v = JS_GetPropertyUint32(ctx, optionsV, i);
				if (!JS_IsString(v)) {
					JS_FreeValue(ctx, v);
					JS_FreeValue(ctx, optionsV);
					JS_FreeValue(ctx, onChangeV);
					return jsThrow(ctx, "registerContextMenu: options must contain only strings");
				}
				spec.options[i] = e->jsToStdString(v);
				JS_FreeValue(ctx, v);
			}
			JS_FreeValue(ctx, optionsV);
		}

		// The current value (checked/selected) is not read at registration:
		// it is evaluated lazily from onGetValue when the menu is built, so
		// it always reflects the script's live config. onGetValue is optional
		// and defaults to value 0.
		JSValue onGetValueV = JS_GetPropertyStr(ctx, argv[0], "onGetValue");
		if (!JS_IsFunction(ctx, onGetValueV)) {
			// Not a function — ignore and default to value 0.
			JS_FreeValue(ctx, onGetValueV);
			onGetValueV = JS_UNDEFINED;
		}

		spec.callbackId = e->nextContextMenuCallbackId++;
		{
			std::lock_guard<std::mutex> lock(e->contextMenusMutex);
			ContextMenuEntry entry;
			entry.spec = spec;
			// Ownership of onChangeV/onGetValueV transfers to the map; freed
			// in clearContextMenus().
			entry.callbackFn = onChangeV;
			entry.onGetValueFn = onGetValueV;
			e->contextMenus[spec.callbackId] = entry;
		}

		return JS_NewBool(ctx, true);
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

	// input

	static JSValue js_input_enable(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "input.enable: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputCount) return jsThrow(ctx, "input.enable: bad index");
		getEngine(ctx)->handler->enableInput(i - 1);
		return JS_UNDEFINED;
	}

	static JSValue js_input_getVoltage(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "input.getVoltage: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputCount) return jsThrow(ctx, "input.getVoltage: bad index");
		uint8_t ch = 1;
		if (argc == 2) ch = static_cast<uint8_t>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "input.getVoltage: bad channel");
		return JS_NewFloat64(ctx, getEngine(ctx)->handler->getInputVoltage(i - 1, ch - 1));
	}

	static JSValue js_input_isHigh(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "input.isHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputCount) return jsThrow(ctx, "input.isHigh: bad index");
		uint8_t ch = 1;
		if (argc == 2) ch = static_cast<uint8_t>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "input.isHigh: bad channel");
		return JS_NewBool(ctx, getEngine(ctx)->handler->getInputVoltage(i - 1, ch - 1) > 0.7f);
	}

	static JSValue js_input_isLow(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "input.isLow: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputCount) return jsThrow(ctx, "input.isLow: bad index");
		uint8_t ch = 1;
		if (argc == 2) ch = static_cast<uint8_t>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "input.isLow: bad channel");
		return JS_NewBool(ctx, getEngine(ctx)->handler->getInputVoltage(i - 1, ch - 1) < 0.7f);
	}

	// trig

	static JSValue js_trig_getTicks(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "trig.getTicks: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputTrigCount) return jsThrow(ctx, "trig.getTicks: bad index");
		return JS_NewFloat64(ctx, double(getEngine(ctx)->handler->getTrigTicks(i - 1)));
	}

	static JSValue js_trig_isHigh(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.isHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputTrigCount) return jsThrow(ctx, "trig.isHigh: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.isHigh: bad channel");
		return JS_NewBool(ctx, getEngine(ctx)->handler->getTrigVoltage(i - 1, ch - 1) > 0.7f);
	}

	static JSValue js_trig_isLow(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.isLow: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputTrigCount) return jsThrow(ctx, "trig.isLow: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.isLow: bad channel");
		return JS_NewBool(ctx, getEngine(ctx)->handler->getTrigVoltage(i - 1, ch - 1) < 0.7f);
	}

	static JSValue js_trig_setGate(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if ((argc != 2 && argc != 3) || !argIsNumber(ctx, argv[0]) || !argIsNumber(ctx, argv[1]) ||
			(argc == 3 && !argIsNumber(ctx, argv[2])))
			return jsThrow(ctx, "trig.setGate: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->outputTrigCount) return jsThrow(ctx, "trig.setGate: bad index");
		int ch = 1;
		if (argc == 3) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setGate: bad channel");
		float duration = argNum(ctx, argv[argc - 1]);
		getEngine(ctx)->handler->setTrig(i - 1, ch - 1, duration);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_setHigh(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.setHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->outputTrigCount) return jsThrow(ctx, "trig.setHigh: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setHigh: bad channel");
		getEngine(ctx)->handler->setTrigVoltage(i - 1, ch - 1, 10.f);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_setLow(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.setHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->outputTrigCount) return jsThrow(ctx, "trig.setLow: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setHigh: bad channel");
		getEngine(ctx)->handler->setTrigVoltage(i - 1, ch - 1, 0.f);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_setTrigger(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.setHigh: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->outputTrigCount) return jsThrow(ctx, "trig.setTrigger: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.setHigh: bad channel");
		getEngine(ctx)->handler->setTrig(i - 1, ch - 1);
		return JS_UNDEFINED;
	}

	// param

	static JSValue js_param_enable(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "param.enable: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->paramCount) return jsThrow(ctx, "param.enable: bad index");
		getEngine(ctx)->handler->enableParam(i - 1);
		return JS_UNDEFINED;
	}

	static JSValue js_param_getValue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "param.getValue: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->paramCount) return jsThrow(ctx, "param.getValue: bad index");
		return JS_NewFloat64(ctx, getEngine(ctx)->handler->getParamValue(i - 1));
	}

	// midi

	// Extracts the message-handle argument shared by every midi.* accessor.
	static bool getMsgArg(JSContext* ctx, JSValueConst v, size_t& idx) {
		if (!JS_IsNumber(v)) return false;
		double d = 0;
		JS_ToFloat64(ctx, &d, v);
		if (d < 0) return false;
		idx = static_cast<size_t>(d);
		return idx < getEngine(ctx)->msgCount;
	}

	static JSValue js_midi_isType(JSContext* ctx, int argc, JSValueConst* argv, uint8_t t, const char* n) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, string::f("midi.%s: invalid msg", n).c_str());
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == t);
	}

	// Warns when a message is created outside a callback (onMidiMessage/
	// onLoad/onUnload) — the store resets every callback, silently
	// invalidating such a handle before use.
	static void warnIfOutsideCallback(JSContext* ctx, const char* fn) {
		MidiScriptEngineQuickJs* e = getEngine(ctx);
		if (!e->inCallback) {
			e->handler->writeLog(string::f("%s: called outside a callback; the message "
				"is discarded when the next MIDI message arrives", fn), false);
		}
	}

	static JSValue js_midiOut_selectPort(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || !argIsNumber(ctx, argv[0])) return jsThrow(ctx, "midiOut.selectPort: bad args");
		int midiPort = static_cast<int>(argNum(ctx, argv[0]));
		if (midiPort < 1 || midiPort > getEngine(ctx)->midiOutputCount) return jsThrow(ctx, "midiOut.selectPort: invalid output index");
		getEngine(ctx)->selectedPort = midiPort - 1;
		return JS_UNDEFINED;
	}

	static JSValue js_midi_create(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "midi.create: bad args");
		warnIfOutsideCallback(ctx, "midi.create");
		size_t* s = &getEngine(ctx)->msgCount;
		if (*s == msgStoreSize) return jsThrow(ctx, "midi.create: message store full");
		getEngine(ctx)->msgStore[*s] = MessageEx();
		return JS_NewFloat64(ctx, double((*s)++));
	}

	static JSValue js_midi_clone(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.clone: invalid msg");
		warnIfOutsideCallback(ctx, "midi.clone");
		size_t* s = &getEngine(ctx)->msgCount;
		if (*s == msgStoreSize) return jsThrow(ctx, "midi.clone: message store full");
		// Copy only the MIDI payload; the clone starts as a fresh, unsent
		// message (send/tick/midiPort/isNrpn at defaults) so it can be modified
		// and sent independently of the source.
		MessageEx clone;
		clone.msg = getEngine(ctx)->msgStore[idx].msg;
		getEngine(ctx)->msgStore[*s] = clone;
		return JS_NewFloat64(ctx, double((*s)++));
	}

	static JSValue js_midi_createNrpn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "midi.createNrpn: bad args");
		warnIfOutsideCallback(ctx, "midi.createNRPN");
		size_t* s = &getEngine(ctx)->msgCount;
		if (*s + 4 >= msgStoreSize) return jsThrow(ctx, "midi.createNRPN: message store full");
		getEngine(ctx)->msgStore[*s + 0] = MessageEx();
		getEngine(ctx)->msgStore[*s + 0].isNrpn = true;
		getEngine(ctx)->msgStore[*s + 1] = MessageEx();
		getEngine(ctx)->msgStore[*s + 2] = MessageEx();
		getEngine(ctx)->msgStore[*s + 3] = MessageEx();
		size_t _s = *s;
		(*s) += 4;
		return JS_NewFloat64(ctx, double(_s));
	}

	static JSValue js_midi_getChanPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getChanPressure: invalid msg");
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].msg.getNote());
	}

	static JSValue js_midi_getChannel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getChannel: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].msg.getSize());
	}

	static JSValue js_midi_getNote(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getNote: invalid msg");
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].msg.getNote());
	}

	static JSValue js_midi_getPitchWheel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getPitchWheel: invalid msg");
		Message& msg = getEngine(ctx)->msgStore[idx].msg;
		uint16_t value = (static_cast<uint16_t>(msg.getValue()) << 7) | msg.getNote();
		return JS_NewFloat64(ctx, value);
	}

	static JSValue js_midi_getProgramChange(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getProgramChange: invalid msg");
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].msg.getNote());
	}

	static JSValue js_midi_getSysEx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getSysEx: invalid msg");
		Message& msg = getEngine(ctx)->msgStore[idx].msg;
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
		return JS_NewFloat64(ctx, std::max(0, getEngine(ctx)->msgStore[idx].msg.getSize() - 2));
	}

	static JSValue js_midi_getRaw(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getRaw: invalid msg");
		Message& msg = getEngine(ctx)->msgStore[idx].msg;
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
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].msg.getValue());
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0x8);
	}

	static JSValue js_midi_isContinue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isContinue: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xa);
	}

	static JSValue js_midi_isStop(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isStop: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0xc);
	}

	static JSValue js_midi_isSysEx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isSysEx: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.msg.getStatus() == 0xf && s.msg.getChannel() == 0x0);
	}

	static JSValue js_midi_setCc(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 4 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
			return jsThrow(ctx, "midi.setCc: bad args");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s1 = getEngine(ctx)->msgStore[idx1];
		MessageEx& s2 = getEngine(ctx)->msgStore[idx2];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		s.msg.setChannel(ch - 1);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setChanPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 3 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "midi.setChanPressure: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx* s1 = &getEngine(ctx)->msgStore[idx];
		if (!s1->isNrpn) return jsThrow(ctx, "midi.setNrpn: invalid nrpn message");
		MessageEx* s2 = &getEngine(ctx)->msgStore[idx + 1];
		MessageEx* s3 = &getEngine(ctx)->msgStore[idx + 2];
		MessageEx* s4 = &getEngine(ctx)->msgStore[idx + 3];

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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		std::string data = getEngine(ctx)->jsToStdString(argv[1]);
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		std::string data = getEngine(ctx)->jsToStdString(argv[1]);
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
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t value = static_cast<uint8_t>(argNum(ctx, argv[1]));
		s.msg.setValue(value);
		return JS_UNDEFINED;
	}

	// midiOut

	static JSValue js_midiOut_send(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midiOut.send: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		s.midiPort = getEngine(ctx)->selectedPort;
		s.send = true;
		s.sendOrder = getEngine(ctx)->sendCounter++;
		s.msg.frame = -1;
		return JS_UNDEFINED;
	}

	static JSValue js_midiOut_sendAfterMs(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midiOut.sendAfterMs: bad args");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		s.midiPort = getEngine(ctx)->selectedPort;
		double ms = argNum(ctx, argv[1]);
		int64_t currentFrame = APP->engine->getFrame();
		int64_t frame = ms / 1000.f / APP->engine->getSampleTime();
		s.send = true;
		s.sendOrder = getEngine(ctx)->sendCounter++;
		s.msg.frame = currentFrame + frame;
		return JS_UNDEFINED;
	}

	static JSValue js_midiOut_sendAfterTrigger(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc == 2) {
			if (!getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
			MessageEx& s = getEngine(ctx)->msgStore[idx];
			s.midiPort = getEngine(ctx)->selectedPort;
			int64_t currentTicks = getEngine(ctx)->handler->getTrigTicks(0);
			int ticks = static_cast<int>(argNum(ctx, argv[1]));
			s.send = true;
			s.sendOrder = getEngine(ctx)->sendCounter++;
			s.tick = currentTicks + ticks;
			return JS_UNDEFINED;
		}
		if (argc == 3) {
			if (!getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
				return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
			int trigPort = static_cast<int>(argNum(ctx, argv[1]));
			if (trigPort < 1 || trigPort > getEngine(ctx)->inputTrigCount) return jsThrow(ctx, "midiOut.sendAfterTrigger: bad trigInput index");
			MessageEx& s = getEngine(ctx)->msgStore[idx];
			s.midiPort = getEngine(ctx)->selectedPort;
			int64_t currentTicks = getEngine(ctx)->handler->getTrigTicks(trigPort - 1);
			int ticks = static_cast<int>(argNum(ctx, argv[2]));
			s.send = true;
			s.sendOrder = getEngine(ctx)->sendCounter++;
			s.tick = currentTicks + ticks;
			return JS_UNDEFINED;
		}

		return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
	}

	static JSValue js_trig_sendTipsy(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		// trig.sendTipsy(data, [mimeType])
		//   data: string or ArrayBuffer (binary data to encode)
		//   mimeType: optional string (default "text/plain")
		
		if (argc < 1) return jsThrow(ctx, "trig.sendTipsy: requires data argument");
		
		// Handle both string and ArrayBuffer for data
		const unsigned char* data = nullptr;
		uint32_t dataLen = 0;
		
		if (JS_IsString(argv[0])) {
			size_t len;
			const char* str = JS_ToCStringLen(ctx, &len, argv[0]);
			if (!str) {
				return jsThrow(ctx, "trig.sendTipsy: invalid data");
			}
			data = reinterpret_cast<const unsigned char*>(str);
			dataLen = static_cast<uint32_t>(len);
			JS_FreeCString(ctx, str);
		}
		else {
			// JS_GetArrayBuffer returns NULL when the value is not an ArrayBuffer
			size_t byteLen;
			uint8_t* buffer = JS_GetArrayBuffer(ctx, &byteLen, argv[0]);
			if (!buffer) {
				return jsThrow(ctx, "trig.sendTipsy: data must be string or ArrayBuffer");
			}
			data = buffer;
			dataLen = static_cast<uint32_t>(byteLen);
		}
		
		const char* mimeType = "text/plain";
		const char* mimeDyn = nullptr;
		if (argc >= 2) {
			mimeDyn = JS_ToCString(ctx, argv[1]);
			if (!mimeDyn) return jsThrow(ctx, "trig.sendTipsy: invalid mimeType");
			mimeType = mimeDyn;
		}
		
		auto* e = getEngine(ctx);
		bool success = e->sendTipsy(mimeType, data, dataLen);
		if (mimeDyn) JS_FreeCString(ctx, mimeDyn);
		
		if (!success) {
			return jsThrow(ctx, "trig.sendTipsy: failed to initiate message");
		}
		
		return JS_UNDEFINED;
	}
};

} // namespace QuickJs
} // namespace MidiScript
} // namespace StoermelderPackOne
