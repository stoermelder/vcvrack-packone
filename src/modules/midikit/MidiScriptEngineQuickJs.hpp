#include "MidiScriptEngine.hpp"
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
		// The message itself, plus the decode result when it came in assembled
		// (NRPN/RPN/14-bit CC). Reusing MidiScript::QueuedMessage rather than
		// restating its fields: it already carries exactly what an incoming
		// message needs, and sharing the type means midi.getControl()/getValue()
		// read the same struct the module filled in — no field-by-field copy to
		// drift.
		//
		// `in` also holds the message a script BUILDS for output; the decode
		// fields simply stay at their defaults there.
		QueuedMessage in;
		// Outgoing chain markers, set by createNRPN()/createCc14bit(). Note these
		// are about a message being built for SEND, unlike in.type which reports
		// how a received message was decoded — same words, opposite direction.
		bool isNrpn = false;
		bool isCc14bit = false;
		bool send = false;
		uint8_t channel = 0;   // trigger input channel, for sendAfterTrigger() scheduling
		uint64_t tick = 0;
		// Monotonic stamp assigned when midiOut.send() is called, so the out
		// queue can be flushed in send() order rather than handle order.
		size_t sendOrder = 0;
	};

	// Retrieves the engine owning ctx, stashed via JS_SetContextOpaque at
	// context creation. O(1); avoids a shared map mutated on the GUI thread and
	// read on the worker thread without synchronization.
	static MidiScriptEngineQuickJs* getEngine(JSContext* ctx) {
		return static_cast<MidiScriptEngineQuickJs*>(JS_GetContextOpaque(ctx));
	}

	JSRuntime* rt = NULL;
	JSContext* ctx = NULL;

	static const size_t memoryLimit = 1024 * 1024;

	const static int msgStoreSize = 32;
	MessageEx msgStore[msgStoreSize];
	// Must be initialised: top-level script code runs during loadScript(),
	// before process() sets this to 1; it bounds every msgStore check.
	size_t msgCount = 0;
	// Next MessageEx::sendOrder value. Never reset: only needs to be monotonic
	// within a single callback (the store resets per callback).
	size_t sendCounter = 0;
	// True only inside a script callback. The store resets every callback, so
	// handles created outside one are silently invalidated — lets midi.create()
	// warn instead of failing quietly.
	bool inCallback = false;
	// Sticky output port selected via midiOut.selectPort(), 0-based. Stays in
	// effect across callbacks until changed again.
	int selectedPort = 0;

	// ── Script execution budget ──────────────────────────────────────────────
	// A per-runtime interrupt handler is polled every 10k instructions; past
	// interruptCountLimit it aborts the script with an UNCATCHABLE "interrupted"
	// error, so a `while(true)` can't wedge the shared worker. Reset by
	// beginScriptExecution() per callback (worker-thread only).
	static const int interruptCountLimit = 10000;   // 10k polls * 10k instr = 100M
	int interruptCount = 0;

	// JS_SetInterruptHandler callback; opaque is `this`. Non-zero throws the
	// uncatchable "interrupted" error that aborts the script.
	static int jsInterruptHandler(JSRuntime* rt, void* opaque) {
		(void)rt;
		MidiScriptEngineQuickJs* e = static_cast<MidiScriptEngineQuickJs*>(opaque);
		return ++e->interruptCount >= interruptCountLimit ? 1 : 0;
	}

	// Resets the budget; call before every user JS_Call/JS_Eval.
	void beginScriptExecution() {
		interruptCount = 0;
	}

	// Script-registered context menus. The JSValue callback lives here (not in
	// ScriptMenuItem) because it's only ever touched on the worker thread; the
	// UI thread reads presentation copies.
	struct ContextMenuEntry {
		ScriptMenuItem spec;
		JSValue callbackFn;
		JSValue onGetValueFn = JS_UNDEFINED;
	};
	// Worker-thread-owned (registerContextMenu/getContextMenus/
	// invokeContextMenuCallback); clearContextMenus() only from load/teardown,
	// which never overlaps dispatch. No mutex — see clearContextMenus().
	std::unordered_map<int, ContextMenuEntry> contextMenus;
	int nextContextMenuCallbackId = 1;

	// The lifecycle hooks are resolved once at load and cached here, not
	// re-looked-up per dispatch — so defining/reassigning one later has no
	// effect; only what was present at load runs. Matched in Lua so both
	// engines behave the same. Asymmetry: QuickJS calls hooks as methods
	// (rackObj/trigObj as thisVal, so `this` works); Lua calls them as bare
	// functions. onLoad/onUnload/onSave live on the rack object; onMessage on
	// the midi object; onTrigger/onTipsyMessage on the trig object (onTrigger
	// so trig.enableIn() can gate it). Predates caching, unused by any preset.
	JSValue rackObj = JS_UNDEFINED;
	JSValue midiObj = JS_UNDEFINED;
	JSValue trigObj = JS_UNDEFINED;
	JSValue onMessageFn = JS_UNDEFINED;
	// Assembled extended-CC callbacks, on the midi object beside onMessage. Only
	// fire for what the script enabled via midi.enableNrpnIn()/enableRpnIn()/
	// enableCc14bitIn().
	JSValue onNrpnFn = JS_UNDEFINED;
	JSValue onRpnFn = JS_UNDEFINED;
	JSValue onCc14bitFn = JS_UNDEFINED;
	JSValue onTriggerFn = JS_UNDEFINED;
	JSValue onTipsyMessageFn = JS_UNDEFINED;
	JSValue onLoadFn = JS_UNDEFINED;
	JSValue onUnloadFn = JS_UNDEFINED;
	JSValue onSaveFn = JS_UNDEFINED;


	std::string jsToStdString(JSValueConst v) {
		const char* s = JS_ToCString(ctx, v);
		std::string r = s ? s : "";
		if (s) JS_FreeCString(ctx, s);
		return r;
	}

	// Formats a QuickJS exception with its source position: reads message +
	// "stack" (file/line info when available, e.g. a parse-time SyntaxError)
	// straight off the exception object.
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

	// Engine selection: true when the script's header declares @engine QuickJs@v1.
	// The same substring check the module used to run itself (Q26) — so the
	// module has no third header parser.
	bool testScript(const std::string& script) override {
		return script.find("@engine QuickJs@v1") != std::string::npos;
	}


	void loadScriptOnWorker(const char* script, const std::string& persistedConfigJson) override {
		assert(onWorkerThread());
		closeStateOnWorker();
		handler->sendTipsyOutReset();

		if (script[0] == '\0') {
			return;
		}

		// Analyze file header of this pattern:
		//	/**
		//	 * @target stoermelder MIDI-KIT
		//	 * @engine QuickJs@v1
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
			// Tags are "@word value". Values are sliced from each tag's end to
			// the next tag's start, so "@" inside a value (e.g. the "QuickJs@v1"
			// version) is kept; trailing whitespace is trimmed so a lone tag
			// matches exactly.
			const std::regex tag_re(R"(@([a-z]+)\s+)");
			auto words_begin = std::sregex_iterator(header.begin(), header.end(), tag_re);
			auto words_end = std::sregex_iterator();
			for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
				std::string topic = (*i)[1].str();
				size_t valStart = (*i).position(0) + (*i).length();
				auto j = i;
				++j;
				size_t valEnd = (j != words_end) ? (*j).position(0) : header.size();
				std::string text = header.substr(valStart, valEnd - valStart);
				size_t last = text.find_last_not_of(" \t");
				text = (last == std::string::npos) ? "" : text.substr(0, last + 1);
				topics[topic] = text;
			}
		}

		if (topics.find("engine") == topics.end() || topics["engine"] != "QuickJs@v1") {
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
		// Budget: aborts scripts that exceed interruptCountLimit (jsInterruptHandler).
		JS_SetInterruptHandler(rt, &jsInterruptHandler, this);
		ctx = JS_NewContext(rt);
		JS_SetContextOpaque(ctx, this);

		registerApi();

		beginScriptExecution();
		JSValue r = JS_Eval(ctx, script, strlen(script), "script", JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			handler->writeLog("Error while loading script", false);
			handler->writeLog(formatError(exc), false);
			JS_FreeValue(ctx, exc);
			closeStateOnWorker();
		}
		else {
			JS_FreeValue(ctx, r);
			handler->writeLog("Script loaded", false);

			// Callbacks live on the predefined objects, not the global scope.
			// rack holds onLoad/onUnload/onSave; the midi object holds onMessage
			// (the incoming-MIDI entry point); the trig object holds
			// onTrigger/onTipsyMessage. All are cached once here (see
			// declarations).
			JSValue glob = JS_GetGlobalObject(ctx);
			rackObj = JS_GetPropertyStr(ctx, glob, "rack");
			midiObj = JS_GetPropertyStr(ctx, glob, "midi");
			trigObj = JS_GetPropertyStr(ctx, glob, "trig");
			JS_FreeValue(ctx, glob);
			// A script can clobber "rack" (e.g. "rack = null;"); JS_GetPropertyStr
			// throws on null/undefined, leaving a pending exception on ctx — so
			// only look up hooks when rackObj is an object, else JS_UNDEFINED.
			if (JS_IsObject(rackObj)) {
				onLoadFn = cacheCallableProp(rackObj, "onLoad");
				onUnloadFn = cacheCallableProp(rackObj, "onUnload");
				onSaveFn = cacheCallableProp(rackObj, "onSave");
			}
			else {
				JS_FreeValue(ctx, rackObj);
				rackObj = JS_UNDEFINED;
			}
			// midi.onMessage is the incoming-MIDI entry point, resolved once and
			// clobber-guarded like the others; midiObj is its thisVal.
			if (JS_IsObject(midiObj)) {
				onMessageFn = cacheCallableProp(midiObj, "onMessage");
				onNrpnFn = cacheCallableProp(midiObj, "onNrpn");
				onRpnFn = cacheCallableProp(midiObj, "onRpn");
				onCc14bitFn = cacheCallableProp(midiObj, "onCc14bit");
			}
			else {
				JS_FreeValue(ctx, midiObj);
				midiObj = JS_UNDEFINED;
			}
			// trig.onTrigger/trig.onTipsyMessage come from the trig object
			// (resolved once, clobber-guarded); trigObj is the thisVal for both.
			if (JS_IsObject(trigObj)) {
				onTriggerFn = cacheCallableProp(trigObj, "onTrigger");
				onTipsyMessageFn = cacheCallableProp(trigObj, "onTipsyMessage");
			}
			else {
				JS_FreeValue(ctx, trigObj);
				trigObj = JS_UNDEFINED;
			}

			hasOnSave.store(!JS_IsUndefined(onSaveFn), std::memory_order_release);

			if (JS_IsUndefined(onMessageFn)) {
				handler->writeLog("No midi.onMessage(midiPort, msg) function defined — incoming MIDI is ignored", false);
			}
			// Pass any persisted config to onLoad(); parsePersistedConfig()
			// returns JS_UNDEFINED when there is none or invalid JSON, so the
			// script falls back to its defaults.
			JSValue config = parsePersistedConfig(persistedConfigJson);
			callOnLoad(config);
			// JS_UNDEFINED is a shared atom; JS_FreeValue is a no-op for it.
			JS_FreeValue(ctx, config);
		}
	}

	// Reads obj[name] and keeps a ref to it if callable, else JS_UNDEFINED.
	// Used once at load time to cache the hooks.
	JSValue cacheCallableProp(JSValueConst obj, const char* name) {
		JSValue v = JS_GetPropertyStr(ctx, obj, name);
		if (JS_IsFunction(ctx, v)) return v;
		JS_FreeValue(ctx, v);
		return JS_UNDEFINED;
	}

	// See MidiScriptEngine::captureConfig() for the contract. The QuickJS
	// context is only safe to touch from the worker thread, hence
	// runSync().
	bool captureConfig(std::string& out) override {
		// Answered from the atomic, without a worker round-trip: ctx/onSaveFn are
		// worker-owned and must not be read from here. No script, or a script
		// without onSave(), both mean "nothing to persist" — a definite answer,
		// so true with an empty `out`, and the caller clears its stored config.
		if (!hasOnSave.load(std::memory_order_acquire)) {
			out.clear();
			return true;
		}
		return runSync([this]() -> std::string {
			if (!ctx) return "";
			JSValue ret = callOnSave();
			std::string configJson;
			if (!JS_IsUndefined(ret) && !JS_IsNull(ret) && !JS_IsException(ret)) {
				// JSONStringify can run user toJSON() methods — budget it too.
				beginScriptExecution();
				JSValue jsonVal = JS_JSONStringify(ctx, ret, JS_UNDEFINED, JS_UNDEFINED);
				if (!JS_IsException(jsonVal)) {
					configJson = jsToStdString(jsonVal);
				}
				JS_FreeValue(ctx, jsonVal);
			}
			JS_FreeValue(ctx, ret);
			return configJson;
		}, out);
	}

	// Frees the QuickJS runtime/context. See
	// MidiScriptEngine::closeStateOnWorker().
	void closeStateOnWorker() override {
		assert(onWorkerThread());
		if (ctx != NULL) {
			JSValue ret = callOnUnload();
			JS_FreeValue(ctx, ret);
			// onUnload()'s teardown messages (e.g. all-notes-off) must go out.
			flushMsgStore();
			clearContextMenus();
			// JS_FreeContext would collect these anyway; free and reset first so
			// nothing stale outlives ctx/rt going to NULL below.
			JS_FreeValue(ctx, rackObj);
			JS_FreeValue(ctx, midiObj);
			JS_FreeValue(ctx, trigObj);
			JS_FreeValue(ctx, onMessageFn);
			JS_FreeValue(ctx, onNrpnFn);
			JS_FreeValue(ctx, onRpnFn);
			JS_FreeValue(ctx, onCc14bitFn);
			JS_FreeValue(ctx, onTriggerFn);
			JS_FreeValue(ctx, onTipsyMessageFn);
			JS_FreeValue(ctx, onLoadFn);
			JS_FreeValue(ctx, onUnloadFn);
			JS_FreeValue(ctx, onSaveFn);
			rackObj = JS_UNDEFINED;
			midiObj = JS_UNDEFINED;
			trigObj = JS_UNDEFINED;
			onMessageFn = JS_UNDEFINED;
			onNrpnFn = JS_UNDEFINED;
			onRpnFn = JS_UNDEFINED;
			onCc14bitFn = JS_UNDEFINED;
			onTriggerFn = JS_UNDEFINED;
			onTipsyMessageFn = JS_UNDEFINED;
			onLoadFn = JS_UNDEFINED;
			onUnloadFn = JS_UNDEFINED;
			onSaveFn = JS_UNDEFINED;
			hasOnSave.store(false, std::memory_order_release);
			JS_FreeContext(ctx);
			JS_FreeRuntime(rt);
			ctx = NULL;
			rt = NULL;
		}
	}

	// Runs the script's onLoad() hook, passing the parsed persisted config as
	// its single argument (or no argument if none). Uses the cached onLoadFn.
	void callOnLoad(JSValue persistedConfig) {
		if (!JS_IsFunction(ctx, onLoadFn)) return;

		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		JSValue r;
		if (JS_IsUndefined(persistedConfig)) {
			r = JS_Call(ctx, onLoadFn, rackObj, 0, NULL);
		}
		else {
			JSValue args[1] = { persistedConfig };
			r = JS_Call(ctx, onLoadFn, rackObj, 1, args);
		}
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

	// Runs onUnload(). Its return value is discarded by the caller — teardown-
	// only; config comes from onSave(). Messages are NOT flushed here:
	// closeState() flushes them for teardown.
	JSValue callOnUnload() {
		if (!JS_IsFunction(ctx, onUnloadFn)) return JS_UNDEFINED;

		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		JSValue r = JS_Call(ctx, onUnloadFn, rackObj, 0, NULL);
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

	// Runs onSave(), returning its return value (the config to persist) or
	// JS_UNDEFINED if missing/errored. Messages are NOT flushed here:
	// captureConfig() discards them, so a save is silent.
	JSValue callOnSave() {
		if (!JS_IsFunction(ctx, onSaveFn)) return JS_UNDEFINED;

		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		JSValue r = JS_Call(ctx, onSaveFn, rackObj, 0, NULL);
		inCallback = false;
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			handler->writeLog(string::f("onSave error: %s", jsToStdString(exc).c_str()));
			JS_FreeValue(ctx, exc);
			return JS_UNDEFINED;
		}
		return r;
	}

	// Parses a persisted-config JSON string into a JSValue for rack.onLoad(),
	// or JS_UNDEFINED when empty/invalid. Caller must JS_FreeValue the result
	// (a no-op for JS_UNDEFINED).
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

	void processInMessage(int midiPort, const MidiScript::QueuedMessage& msg) override {
		if (ctx) {
			midiInQueue.push(std::make_tuple(midiPort, msg));
		}
	}

	void processInTick(int trigPort, uint8_t channel) override {
		if (ctx) {
			tickInQueue.push(std::make_tuple(trigPort, channel));
		}
	}

	void dispatchMidiMessage(int midiPort, Message& msg) override {
		if (ctx) {
			// Assigning the whole QueuedMessage (not just .msg) also resets the
			// decode fields to their defaults, so a plain message cannot report
			// the type or parameter of an assembled one that used slot 0 before.
			msgStore[0].in = QueuedMessage(msg);
			msgStore[0].send = false;
			msgStore[0].tick = 0;
			// Slot 0 is reused for the incoming message, but it can have been a
			// chain leader (NRPN/14-bit CC) in an onLoad/onUnload/onSave callback
			// whose store started at 0 — clear both leader flags so a stale one
			// can't make the flush emit a chain from the incoming message.
			msgStore[0].isNrpn = false;
			msgStore[0].isCc14bit = false;
			msgCount = 1;

			inCallback = true;
			// Calls the cached onMessageFn with midiObj as thisVal — no by-name
			// lookup. !JS_IsUndefined, not JS_IsFunction: cacheCallableProp()
			// guarantees a function or JS_UNDEFINED, so the tag test suffices
			// and is cheaper on this per-dispatch path.
			if (!JS_IsUndefined(onMessageFn)) {
				beginScriptExecution();
				JSValue args[2] = { JS_NewInt32(ctx, midiPort + 1), JS_NewInt32(ctx, 0) };
				JSValue r = JS_Call(ctx, onMessageFn, midiObj, 2, args);
				JS_FreeValue(ctx, args[0]);
				JS_FreeValue(ctx, args[1]);
				inCallback = false;
				if (JS_IsException(r)) {
					JS_FreeValue(ctx, r);
					JSValue exc = JS_GetException(ctx);
					handler->writeLog(string::f("onMessage error: %s", jsToStdString(exc).c_str()));
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

	// Dispatches an assembled message to onNrpn/onRpn/onCc14bit as a handle,
	// exactly like dispatchMidiMessage() does for onMessage: the message lands in
	// store slot 0 and the callback receives (midiPort, 0), reading it through
	// midi.getControl()/getValue()/getChannel(). No-op if the hook was never
	// defined.
	void dispatchAssembled(JSValue fn, const char* name, int midiPort, const QueuedMessage& q) {
		if (!ctx || JS_IsUndefined(fn)) return;

		// The whole QueuedMessage lands in the slot, so the decode result travels
		// with the bytes and no field-by-field copy can drift.
		msgStore[0].in = q;
		msgStore[0].send = false;
		msgStore[0].tick = 0;
		// Slot 0 is reused across callbacks, so clear the outgoing chain flags for
		// the same reason dispatchMidiMessage() does.
		msgStore[0].isNrpn = false;
		msgStore[0].isCc14bit = false;
		msgCount = 1;

		inCallback = true;
		beginScriptExecution();
		JSValue args[2] = { JS_NewInt32(ctx, midiPort + 1), JS_NewInt32(ctx, 0) };
		JSValue r = JS_Call(ctx, fn, midiObj, 2, args);
		JS_FreeValue(ctx, args[0]);
		JS_FreeValue(ctx, args[1]);
		inCallback = false;
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, r);
			JSValue exc = JS_GetException(ctx);
			handler->writeLog(string::f("%s error: %s", name, jsToStdString(exc).c_str()));
			JS_FreeValue(ctx, exc);
		}
		else {
			JS_FreeValue(ctx, r);
		}
		flushMsgStore();
	}

	void dispatchNrpn(int midiPort, const QueuedMessage& q, bool isRpn) override {
		dispatchAssembled(isRpn ? onRpnFn : onNrpnFn, isRpn ? "onRpn" : "onNrpn", midiPort, q);
	}

	void dispatchCc14bit(int midiPort, const QueuedMessage& q) override {
		dispatchAssembled(onCc14bitFn, "onCc14bit", midiPort, q);
	}

	// Dispatches onTrigger(trigPort, channel). No-op if never defined; the
	// module only enqueues ticks for channels the script enabled.
	void dispatchTrigger(int trigPort, uint8_t channel) override {
		if (ctx && !JS_IsUndefined(onTriggerFn)) {
			msgCount = 0;
			inCallback = true;
			beginScriptExecution();
			// Calls the cached onTriggerFn with trigObj as thisVal.
			JSValue args[2] = { JS_NewInt32(ctx, trigPort + 1), JS_NewInt32(ctx, channel + 1) };
			JSValue r = JS_Call(ctx, onTriggerFn, trigObj, 2, args);
			JS_FreeValue(ctx, args[0]);
			JS_FreeValue(ctx, args[1]);
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
			flushMsgStore();
		}
	}

	// Dispatches onTipsyMessage(data, mimeType) when a Tipsy message finishes
	// decoding. No-op if never defined. `data` is a string; binary payloads
	// survive intact (JS strings hold arbitrary 16-bit code units).
	void dispatchTipsyMessage(const MidiScript::TipsyMessage& msg) override {
		if (ctx) {
			msgCount = 0;
			inCallback = true;
			// Calls the cached onTipsyMessageFn with trigObj as thisVal.
			if (!JS_IsUndefined(onTipsyMessageFn)) {
				beginScriptExecution();
				JSValue args[2] = {
					JS_NewStringLen(ctx, reinterpret_cast<const char*>(msg.data), msg.dataSize),
					JS_NewString(ctx, msg.mime)
				};
				JSValue r = JS_Call(ctx, onTipsyMessageFn, trigObj, 2, args);
				JS_FreeValue(ctx, args[0]);
				JS_FreeValue(ctx, args[1]);
				inCallback = false;
				if (JS_IsException(r)) {
					JS_FreeValue(ctx, r);
					JSValue exc = JS_GetException(ctx);
					handler->writeLog(string::f("onTipsyMessage error: %s", jsToStdString(exc).c_str()));
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

	// Sends every message emitted during the callback that just ran through the
	// handler (shared by onMessage/onLoad/onUnload/onTrigger). Emitted in
	// send() order (sendOrder), not handle-creation order: a script may create
	// and send messages in different orders, and the receiver must observe
	// send() order.
	//
	// Return values are ignored: a drop is expected under output saturation,
	// and the module already reports it once per episode. The engine has no
	// better response than to carry on.
	void flushMsgStore() {
		std::vector<size_t> order;
		for (size_t i = 0; i < msgCount; i++) {
			if (msgStore[i].send) order.push_back(i);
		}
		std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
			return msgStore[a].sendOrder < msgStore[b].sendOrder;
		});
		for (size_t i : order) {
			if (msgStore[i].isNrpn) {
				// NRPN is 4 consecutive entries in msgStore, emitted atomically.
				const Message group[4] = {
					msgStore[i].in.msg, msgStore[i + 1].in.msg,
					msgStore[i + 2].in.msg, msgStore[i + 3].in.msg
				};
				handler->sendMidi(msgStore[i].midiPort, group, 4, msgStore[i].channel, msgStore[i].tick);
			}
			else if (msgStore[i].isCc14bit) {
				// A 14-bit CC pair is 2 consecutive entries in msgStore (CC cc /
				// CC cc+32), emitted atomically — a receiver must never see the
				// MSB without its LSB.
				const Message group[2] = { msgStore[i].in.msg, msgStore[i + 1].in.msg };
				handler->sendMidi(msgStore[i].midiPort, group, 2, msgStore[i].channel, msgStore[i].tick);
			}
			else {
				handler->sendMidi(msgStore[i].midiPort, &msgStore[i].in.msg, 1, msgStore[i].channel, msgStore[i].tick);
			}
		}
	}

	// Calls a global "name(i+1)" function returning a string, e.g.
	// input.getName(i)/param.getName(i)/param.getValueFormat(i). Falls back to
	// "" if unset or the call raises.
	std::string callGlobalStringFn(const char* objName, const char* fnName, int i) {
		if (!ctx) return "";
		JSValue glob = JS_GetGlobalObject(ctx);
		JSValue obj = JS_GetPropertyStr(ctx, glob, objName);
		JSValue fn = JS_GetPropertyStr(ctx, obj, fnName);
		std::string result;
		if (JS_IsFunction(ctx, fn)) {
			beginScriptExecution();
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

	// Frees the stored script callbacks. Called only from closeStateOnWorker(),
	// so like every other toucher of contextMenus this runs on the worker
	// thread — hence no lock.
	void clearContextMenus() {
		assert(onWorkerThread());
		if (ctx) {
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
		// needs no copy on the UI thread. Each onGetValue function is dup'd
		// here — QuickJS refcounts aren't atomic, so JS_DupValue only runs on
		// the worker thread.
		runAsync([this, callback]() {
			assert(onWorkerThread());
			if (!ctx) return;
			struct Snapshot { int id; ScriptMenuItem spec; JSValue onGetValueFn; };
			std::vector<Snapshot> snap;
			snap.reserve(contextMenus.size());
			for (const auto& kv : contextMenus) {
				snap.push_back({kv.first, kv.second.spec, JS_DupValue(ctx, kv.second.onGetValueFn)});
			}
			// callbackIds are assigned monotonically at registration, so sorting
			// by them yields registration order — the unordered_map's own
			// iteration order is unspecified.
			std::sort(snap.begin(), snap.end(), [](const Snapshot& a, const Snapshot& b) {
				return a.id < b.id;
			});
			std::vector<ScriptMenuItem> result;
			result.reserve(snap.size());
			// Uses the cached rackObj (same as dispatch), not a fresh lookup, so
			// hooks and menus agree on which rack object is real.
			for (const Snapshot& s : snap) {
				ScriptMenuItem spec = s.spec;
				// Each snapshot owns one dup'd reference; freed after use below.
				JSValue fn = s.onGetValueFn;
				if (JS_IsFunction(ctx, fn)) {
					beginScriptExecution();
					JSValue r = JS_Call(ctx, fn, rackObj, 0, NULL);
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
			// Run the caller's callback with the evaluated specs. It only touches
			// memory the caller owns (never constructs widgets), so it's safe on
			// the worker thread.
			callback(result);
		});
	}

	// Fires a menu item's onChange callback on the worker thread. Presentation
	// state isn't stored on the spec — the next menu build re-evaluates it from
	// onGetValue, so onChange's config changes are picked up automatically. The
	// call is deferred to runAsync() with all other JS work.
	void invokeContextMenuCallback(int callbackId, int value) override {
		// The whole body runs on the worker thread, incl. the spec lookup:
		// contextMenus is worker-owned, so no lock. The caller ignores timing,
		// so the read needn't be synchronous on the UI thread.
		runAsync([this, callbackId, value]() {
			assert(onWorkerThread());
			if (!ctx) return;
			auto it = contextMenus.find(callbackId);
			if (it == contextMenus.end()) return;
			const ContextMenuEntry& entry = it->second;
			ScriptMenuItem::Type type = entry.spec.type;
			std::string label;
			if (type != ScriptMenuItem::Type::Boolean) {
				if (value < 0 || value >= static_cast<int>(entry.spec.options.size())) return;
				label = entry.spec.options[value];
			}
			// Dup so the call below owns a reference even if the script's
			// onChange re-registers menus and rewrites the map.
			JSValue fn = JS_DupValue(ctx, entry.callbackFn);

			// Uses the cached rackObj — see getContextMenus() above.
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
			beginScriptExecution();
			JSValue r = JS_Call(ctx, fn, rackObj, argc, args);
			for (int i = 0; i < argc; i++) JS_FreeValue(ctx, args[i]);
			JS_FreeValue(ctx, fn);
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

	// Current/total bytes in use by the QuickJS heap, or false if no script is
	// loaded.
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
		JS_SetPropertyStr(ctx, _trig, "enableIn", JS_NewCFunction(ctx, js_trig_enableIn, "enableIn", 2));
		JS_SetPropertyStr(ctx, _trig, "getTicks", JS_NewCFunction(ctx, js_trig_getTicks, "getTicks", 1));
		JS_SetPropertyStr(ctx, _trig, "isHigh", JS_NewCFunction(ctx, js_trig_isHigh, "isHigh", 2));
		JS_SetPropertyStr(ctx, _trig, "isLow", JS_NewCFunction(ctx, js_trig_isLow, "isLow", 2));
		JS_SetPropertyStr(ctx, _trig, "setGate", JS_NewCFunction(ctx, js_trig_setGate, "setGate", 3));
		JS_SetPropertyStr(ctx, _trig, "setHigh", JS_NewCFunction(ctx, js_trig_setHigh, "setHigh", 2));
		JS_SetPropertyStr(ctx, _trig, "setLow", JS_NewCFunction(ctx, js_trig_setLow, "setLow", 2));
		JS_SetPropertyStr(ctx, _trig, "setTrigger", JS_NewCFunction(ctx, js_trig_setTrigger, "setTrigger", 2));
		JS_SetPropertyStr(ctx, _trig, "sendTipsy", JS_NewCFunction(ctx, js_trig_sendTipsy, "sendTipsy", 2));
		JS_SetPropertyStr(ctx, _trig, "enableTipsyIn", JS_NewCFunction(ctx, js_trig_enableTipsyIn, "enableTipsyIn", 0));

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
		JS_SetPropertyStr(ctx, _midi, "createCc14bit", JS_NewCFunction(ctx, js_midi_createCc14bit, "createCc14bit", 0));
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
		JS_SetPropertyStr(ctx, _midi, "getControl", JS_NewCFunction(ctx, js_midi_getControl, "getControl", 1));
		JS_SetPropertyStr(ctx, _midi, "isCc", JS_NewCFunction(ctx, js_midi_isCc, "isCc", 1));
		JS_SetPropertyStr(ctx, _midi, "isCc14bit", JS_NewCFunction(ctx, js_midi_isCc14bit, "isCc14bit", 1));
		JS_SetPropertyStr(ctx, _midi, "isNrpn", JS_NewCFunction(ctx, js_midi_isNrpn, "isNrpn", 1));
		JS_SetPropertyStr(ctx, _midi, "isRpn", JS_NewCFunction(ctx, js_midi_isRpn, "isRpn", 1));
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
		JS_SetPropertyStr(ctx, _midi, "enableNrpnIn", JS_NewCFunction(ctx, js_midi_enableNrpnIn, "enableNrpnIn", 2));
		JS_SetPropertyStr(ctx, _midi, "enableRpnIn", JS_NewCFunction(ctx, js_midi_enableRpnIn, "enableRpnIn", 2));
		JS_SetPropertyStr(ctx, _midi, "enableCc14bitIn", JS_NewCFunction(ctx, js_midi_enableCc14bitIn, "enableCc14bitIn", 3));

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
		// Concatenate every argument into one log line with the same per-type
		// contract as a single value: numbers via formatNumber (same as
		// number.toString()), strings verbatim, null/undefined as "null"/
		// "undefined", else JS_ToCString — so the call never errors.
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
	// context menu:
	//   { type: "boolean", label, onGetValue: fn() -> bool, onChange: fn(checked) }
	//   { type: "options", label, options: [..], onGetValue: fn() -> int, onChange: fn(idx, label) }
	// onGetValue is optional (defaults to 0) and evaluated lazily on the worker
	// thread when the menu is built, so it always reflects the live config —
	// unlike a value captured at registration. Returns true on success.
	// Callbacks are stored (owned) by the engine and fired on the worker thread.
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

		// The current value isn't read at registration; it's evaluated lazily
		// from onGetValue when the menu is built, so it always reflects the
		// live config. onGetValue is optional and defaults to 0.
		JSValue onGetValueV = JS_GetPropertyStr(ctx, argv[0], "onGetValue");
		if (!JS_IsFunction(ctx, onGetValueV)) {
			// Not a function — ignore and default to value 0.
			JS_FreeValue(ctx, onGetValueV);
			onGetValueV = JS_UNDEFINED;
		}

		assert(e->onWorkerThread());
		spec.callbackId = e->nextContextMenuCallbackId++;
		ContextMenuEntry entry;
		entry.spec = spec;
		// Ownership of onChangeV/onGetValueV transfers to the map; freed in
		// clearContextMenus().
		entry.callbackFn = onChangeV;
		entry.onGetValueFn = onGetValueV;
		e->contextMenus[spec.callbackId] = entry;

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

	// Formats f with up to 6 decimals, trimming trailing zeros (and a trailing
	// '.') so 42.0 prints as "42" — matching the old %i/%f split without two
	// branches — and non-integers print only the decimals they actually have.
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

	// trig.enableIn(trigPort, [channel = 1]) — enables trig.onTrigger on that
	// (port, channel); the callback is unused until called.
	static JSValue js_trig_enableIn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.enableIn: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputTrigCount) return jsThrow(ctx, "trig.enableIn: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.enableIn: bad channel");
		getEngine(ctx)->handler->enableTrigger(i - 1, ch - 1);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_getTicks(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, "trig.getTicks: bad args");
		int i = static_cast<int>(argNum(ctx, argv[0]));
		if (i < 1 || i > getEngine(ctx)->inputTrigCount) return jsThrow(ctx, "trig.getTicks: bad index");
		int ch = 1;
		if (argc == 2) ch = static_cast<int>(argNum(ctx, argv[1]));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) return jsThrow(ctx, "trig.getTicks: bad channel");
		return JS_NewFloat64(ctx, double(getEngine(ctx)->handler->getTrigTicks(i - 1, ch - 1)));
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
		// The script API is milliseconds (per docs); dsp::PulseGenerator::trigger()
		// takes seconds, so convert here.
		float duration = argNum(ctx, argv[argc - 1]);
		getEngine(ctx)->handler->setTrig(i - 1, ch - 1, duration / 1000.f);
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

	// Shared by midi.enableNrpnIn() and midi.enableRpnIn(): same arguments, they
	// differ only in which kind they arm.
	static JSValue jsEnableParamIn(JSContext* ctx, int argc, JSValueConst* argv, int kind, const char* name) {
		// midi.enableNrpnIn(midiPort [, channel]) / midi.enableRpnIn(...)
		//   midiPort: 1-based; channel: 1-based MIDI channel, omitted = all.
		if (argc < 1 || argc > 2 || !argIsNumber(ctx, argv[0]) || (argc == 2 && !argIsNumber(ctx, argv[1])))
			return jsThrow(ctx, std::string(name) + ": bad args");
		int port = static_cast<int>(argNum(ctx, argv[0]));
		if (port < 1 || port > getEngine(ctx)->midiInputCount) return jsThrow(ctx, std::string(name) + ": bad midiPort");
		int ch = -1;
		if (argc == 2) {
			ch = static_cast<int>(argNum(ctx, argv[1]));
			if (ch < 1 || ch > 16) return jsThrow(ctx, std::string(name) + ": bad channel");
			ch -= 1;
		}
		getEngine(ctx)->handler->enableNrpnIn(port - 1, kind, ch);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_enableNrpnIn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return jsEnableParamIn(ctx, argc, argv, 0, "midi.enableNrpnIn");
	}

	static JSValue js_midi_enableRpnIn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return jsEnableParamIn(ctx, argc, argv, 1, "midi.enableRpnIn");
	}

	static JSValue js_midi_enableCc14bitIn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		// midi.enableCc14bitIn(midiPort [, cc] [, channel])
		//   cc: the 0-31 MSB controller (its LSB is cc + 32), omitted = all of
		//   them. channel: 1-based MIDI channel, omitted = all.
		if (argc < 1 || argc > 3 || !argIsNumber(ctx, argv[0])
			|| (argc >= 2 && !argIsNumber(ctx, argv[1]))
			|| (argc == 3 && !argIsNumber(ctx, argv[2])))
			return jsThrow(ctx, "midi.enableCc14bitIn: bad args");
		int port = static_cast<int>(argNum(ctx, argv[0]));
		if (port < 1 || port > getEngine(ctx)->midiInputCount) return jsThrow(ctx, "midi.enableCc14bitIn: bad midiPort");
		int cc = -1;
		if (argc >= 2) {
			cc = static_cast<int>(argNum(ctx, argv[1]));
			// Only CC 0-31 have a defined LSB partner; rejecting the rest here is
			// a better error than silently never delivering.
			if (cc < 0 || cc > 31) return jsThrow(ctx, "midi.enableCc14bitIn: cc must be 0-31");
		}
		int ch = -1;
		if (argc == 3) {
			ch = static_cast<int>(argNum(ctx, argv[2]));
			if (ch < 1 || ch > 16) return jsThrow(ctx, "midi.enableCc14bitIn: bad channel");
			ch -= 1;
		}
		getEngine(ctx)->handler->enableCc14bitIn(port - 1, cc, ch);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_isType(JSContext* ctx, int argc, JSValueConst* argv, uint8_t t, const char* n) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, string::f("midi.%s: invalid msg", n).c_str());
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.in.msg.getStatus() == t);
	}

	// Warns when a message is created outside a callback (onMessage/
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
		// Copy only the MIDI payload; the clone starts fresh and unsent (all
		// fields at defaults) so it can be modified and sent independently.
		MessageEx clone;
		clone.in.msg = getEngine(ctx)->msgStore[idx].in.msg;
		getEngine(ctx)->msgStore[*s] = clone;
		return JS_NewFloat64(ctx, double((*s)++));
	}

	static JSValue js_midi_createNrpn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "midi.createNrpn: bad args");
		warnIfOutsideCallback(ctx, "midi.createNRPN");
		size_t* s = &getEngine(ctx)->msgCount;
		if (*s + 4 > msgStoreSize) return jsThrow(ctx, "midi.createNRPN: message store full");
		getEngine(ctx)->msgStore[*s + 0] = MessageEx();
		getEngine(ctx)->msgStore[*s + 0].isNrpn = true;
		getEngine(ctx)->msgStore[*s + 1] = MessageEx();
		getEngine(ctx)->msgStore[*s + 2] = MessageEx();
		getEngine(ctx)->msgStore[*s + 3] = MessageEx();
		size_t _s = *s;
		(*s) += 4;
		return JS_NewFloat64(ctx, double(_s));
	}

	static JSValue js_midi_createCc14bit(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		if (argc != 0) return jsThrow(ctx, "midi.createCc14bit: bad args");
		warnIfOutsideCallback(ctx, "midi.createCc14bit");
		size_t* s = &getEngine(ctx)->msgCount;
		if (*s + 2 > msgStoreSize) return jsThrow(ctx, "midi.createCc14bit: message store full");
		// 2 consecutive entries, filled by setCc14bit: CC cc (value MSB) and
		// CC cc+32 (value LSB), flushed atomically as a pair.
		getEngine(ctx)->msgStore[*s + 0] = MessageEx();
		getEngine(ctx)->msgStore[*s + 0].isCc14bit = true;
		getEngine(ctx)->msgStore[*s + 1] = MessageEx();
		size_t _s = *s;
		(*s) += 2;
		return JS_NewFloat64(ctx, double(_s));
	}

	static JSValue js_midi_getChanPressure(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getChanPressure: invalid msg");
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].in.msg.getNote());
	}

	static JSValue js_midi_getChannel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getChannel: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		// Status 0xf (realtime/SysEx) carries no channel; the low nibble is a
		// sub-type selector, so a plausible-looking channel there would be
		// meaningless. -1 is unambiguous: 1-16 is the only valid range, so a
		// script can check `> 0` without special-casing realtime via try/catch.
		if (s.in.msg.getStatus() == 0xf) return JS_NewFloat64(ctx, -1);
		return JS_NewFloat64(ctx, s.in.msg.getChannel() + 1);
	}

	static JSValue js_midi_getLength(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getLength: invalid msg");
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].in.msg.getSize());
	}

	static JSValue js_midi_getNote(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getNote: invalid msg");
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].in.msg.getNote());
	}

	static JSValue js_midi_getPitchWheel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getPitchWheel: invalid msg");
		Message& msg = getEngine(ctx)->msgStore[idx].in.msg;
		uint16_t value = (static_cast<uint16_t>(msg.getValue()) << 7) | msg.getNote();
		return JS_NewFloat64(ctx, value);
	}

	static JSValue js_midi_getProgramChange(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getProgramChange: invalid msg");
		return JS_NewFloat64(ctx, getEngine(ctx)->msgStore[idx].in.msg.getNote());
	}

	static JSValue js_midi_getSysEx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getSysEx: invalid msg");
		Message& msg = getEngine(ctx)->msgStore[idx].in.msg;
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
		// Payload length only — f0/f7 framing excluded.
		return JS_NewFloat64(ctx, std::max(0, getEngine(ctx)->msgStore[idx].in.msg.getSize() - 2));
	}

	static JSValue js_midi_getRaw(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getRaw: invalid msg");
		Message& msg = getEngine(ctx)->msgStore[idx].in.msg;
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 0; i < msg.getSize(); i++) {
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(msg.bytes[i]);
		}
		std::string str = ss.str();
		return JS_NewStringLen(ctx, str.c_str(), str.length());
	}

	// Type-aware, like StoermelderPackOne::MessageEx::getValue(): the combined
	// 0-16383 quantity on an assembled NRPN/RPN/14-bit CC, the raw 7-bit data
	// byte on everything else. Assembled messages are new, so no existing script
	// can be relying on the old answer for one.
	static JSValue js_midi_getValue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getValue: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		if (isAssembled(s)) return JS_NewFloat64(ctx, s.in.extraValue);
		return JS_NewFloat64(ctx, s.in.msg.getValue());
	}

	// Which controller/parameter the message addresses: the controller number of
	// a plain CC, the MSB controller of a 14-bit CC, the parameter number of an
	// NRPN/RPN, or -1 for anything that addresses none (notes, clock, ...).
	// Answers for plain CCs too, so scripts have one spelling for "which knob
	// moved" regardless of how the device encodes it.
	static JSValue js_midi_getControl(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.getControl: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		if (isAssembled(s)) return JS_NewFloat64(ctx, s.in.paramNumber);
		if (s.in.msg.getStatus() == 0xb) return JS_NewFloat64(ctx, s.in.msg.getNote());
		return JS_NewFloat64(ctx, -1);
	}

	// True when the message carries a decode result from MidiProcessor, i.e. it
	// arrived assembled rather than as a raw CC.
	static bool isAssembled(const MessageEx& s) {
		switch (s.in.type) {
			case StoermelderPackOne::MessageEx::Type::NRPN:
			case StoermelderPackOne::MessageEx::Type::RPN:
			case StoermelderPackOne::MessageEx::Type::CC_14BIT:
				return true;
			default:
				return false;
		}
	}

	static JSValue js_midi_isAssembledType(JSContext* ctx, int argc, JSValueConst* argv,
			StoermelderPackOne::MessageEx::Type want, const char* name) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, std::string(name) + ": invalid msg");
		return JS_NewBool(ctx, getEngine(ctx)->msgStore[idx].in.type == want);
	}

	static JSValue js_midi_isNrpn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isAssembledType(ctx, argc, argv, StoermelderPackOne::MessageEx::Type::NRPN, "midi.isNrpn");
	}
	static JSValue js_midi_isRpn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isAssembledType(ctx, argc, argv, StoermelderPackOne::MessageEx::Type::RPN, "midi.isRpn");
	}
	static JSValue js_midi_isCc14bit(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		return js_midi_isAssembledType(ctx, argc, argv, StoermelderPackOne::MessageEx::Type::CC_14BIT, "midi.isCc14bit");
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
		return JS_NewBool(ctx, s.in.msg.getStatus() == 0xf && s.in.msg.getChannel() == 0x8);
	}

	static JSValue js_midi_isContinue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isContinue: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.in.msg.getStatus() == 0xf && s.in.msg.getChannel() == 0xb);
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
		return JS_NewBool(ctx, s.in.msg.getStatus() == 0xf && s.in.msg.getChannel() == 0xa);
	}

	static JSValue js_midi_isStop(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isStop: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.in.msg.getStatus() == 0xf && s.in.msg.getChannel() == 0xc);
	}

	static JSValue js_midi_isSysEx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 1 || !getMsgArg(ctx, argv[0], idx)) return jsThrow(ctx, "midi.isSysEx: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		return JS_NewBool(ctx, s.in.msg.getStatus() == 0xf && s.in.msg.getChannel() == 0x0);
	}

	static JSValue js_midi_setCc(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 4 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
			return jsThrow(ctx, "midi.setCc: bad args");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t cc = static_cast<uint8_t>(argNum(ctx, argv[2]));
		uint8_t value = std::max(0, std::min(127, static_cast<int>(argNum(ctx, argv[3]))));
		if (s.in.msg.getSize() != 3) s.in.msg.setSize(3);
		s.in.msg.setStatus(0xb);
		s.in.msg.setChannel(ch - 1);
		s.in.msg.setNote(cc);
		s.in.msg.setValue(value);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setCc14bit(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		auto* e = getEngine(ctx);

		if (argc == 4) {
			// midi.setCc14bit(msg, channel, cc, value) — msg is the first
			// handle of a createCc14bit() pair; both CCs are filled and sent
			// atomically when the pair is flushed.
			size_t idx1;
			if (!getMsgArg(ctx, argv[0], idx1) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
				return jsThrow(ctx, "midi.setCc14bit: invalid msg");
			MessageEx& s1 = e->msgStore[idx1];
			if (!s1.isCc14bit) return jsThrow(ctx, "midi.setCc14bit: message is not a 14-bit CC pair");
			MessageEx& s2 = e->msgStore[idx1 + 1];
			uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
			uint8_t cc = static_cast<uint8_t>(argNum(ctx, argv[2]));
			double value = argNum(ctx, argv[3]);
			if (s1.in.msg.getSize() != 3) s1.in.msg.setSize(3);
			if (s2.in.msg.getSize() != 3) s2.in.msg.setSize(3);
			s1.in.msg.setStatus(0xb);
			s2.in.msg.setStatus(0xb);
			s1.in.msg.setChannel(ch - 1);
			s2.in.msg.setChannel(ch - 1);
			s1.in.msg.setNote(cc);
			s2.in.msg.setNote(cc + 32);
			s1.in.msg.setValue(static_cast<int8_t>(value));
			s2.in.msg.setValue(static_cast<int8_t>((value - static_cast<int8_t>(value)) * 128.f));
			return JS_UNDEFINED;
		}

		// midi.setCc14bit(msg1, msg2, channel, cc, value) — two independent
		// handles, sent as separate messages (no atomicity).
		size_t idx1, idx2;
		if (argc < 5 || !getMsgArg(ctx, argv[0], idx1) || !getMsgArg(ctx, argv[1], idx2) ||
			!argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]) || !argIsNumber(ctx, argv[4]))
			return jsThrow(ctx, "midi.setCc14bit: invalid msg");
		MessageEx& s1 = e->msgStore[idx1];
		MessageEx& s2 = e->msgStore[idx2];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[2]))));
		uint8_t cc = static_cast<uint8_t>(argNum(ctx, argv[3]));
		double value = argNum(ctx, argv[4]);
		if (s1.in.msg.getSize() != 3) s1.in.msg.setSize(3);
		if (s2.in.msg.getSize() != 3) s2.in.msg.setSize(3);
		s1.in.msg.setStatus(0xb);
		s2.in.msg.setStatus(0xb);
		s1.in.msg.setChannel(ch - 1);
		s2.in.msg.setChannel(ch - 1);
		s1.in.msg.setNote(cc);
		s2.in.msg.setNote(cc + 32);
		s1.in.msg.setValue(static_cast<int8_t>(value));
		s2.in.msg.setValue(static_cast<int8_t>((value - static_cast<int8_t>(value)) * 128.f));
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setChannel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midi.setChannel: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		s.in.msg.setChannel(ch - 1);
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
		if (s.in.msg.getSize() != 2) s.in.msg.setSize(2);
		s.in.msg.setStatus(0xd);
		s.in.msg.setChannel(ch - 1);
		s.in.msg.setNote(value);
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
		if (s.in.msg.getSize() != 3) s.in.msg.setSize(3);
		s.in.msg.setStatus(0xa);
		s.in.msg.setChannel(ch - 1);
		s.in.msg.setNote(note);
		s.in.msg.setValue(vel);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setNote(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midi.setNote: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t value = static_cast<uint8_t>(argNum(ctx, argv[1]));
		s.in.msg.setNote(value);
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
		if (s.in.msg.getSize() != 3) s.in.msg.setSize(3);
		s.in.msg.setStatus(0x8);
		s.in.msg.setChannel(ch - 1);
		s.in.msg.setNote(note);
		s.in.msg.setValue(vel);
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
		if (s.in.msg.getSize() != 3) s.in.msg.setSize(3);
		s.in.msg.setStatus(0x9);
		s.in.msg.setChannel(ch - 1);
		s.in.msg.setNote(note);
		s.in.msg.setValue(vel);
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
		// flushMsgStore() sends s1..s4 in this order, as MidiProcessor's NRPN
		// state machine requires (CC99/98 select the number, CC6/38 the value).
		s1->in.msg.setStatus(0xb);
		s1->in.msg.setChannel(ch - 1);
		s1->in.msg.setNote(99);
		s1->in.msg.setValue((number >> 7) & 0x7f);
		s2->in.msg.setStatus(0xb);
		s2->in.msg.setChannel(ch - 1);
		s2->in.msg.setNote(98);
		s2->in.msg.setValue(number & 0x7f);
		s3->in.msg.setStatus(0xb);
		s3->in.msg.setChannel(ch - 1);
		s3->in.msg.setNote(6);
		s3->in.msg.setValue((value >> 7) & 0x7f);
		s4->in.msg.setStatus(0xb);
		s4->in.msg.setChannel(ch - 1);
		s4->in.msg.setNote(38);
		s4->in.msg.setValue(value & 0x7f);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setPitchWheel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 3 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "midi.setPitchWheel: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint16_t value = static_cast<uint16_t>(argNum(ctx, argv[2]));
		if (s.in.msg.getSize() != 3) s.in.msg.setSize(3);
		s.in.msg.setStatus(0xe);
		s.in.msg.setChannel(ch - 1);
		s.in.msg.setNote(value & 0x7f);
		s.in.msg.setValue((value >> 7) & 0x7f);
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setProgramChange(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 3 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
			return jsThrow(ctx, "midi.setProgramChange: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t ch = std::max(static_cast<uint8_t>(1), std::min(static_cast<uint8_t>(16), static_cast<uint8_t>(argNum(ctx, argv[1]))));
		uint8_t prg = static_cast<uint8_t>(argNum(ctx, argv[2]));
		if (s.in.msg.getSize() != 3) s.in.msg.setSize(3);
		s.in.msg.setStatus(0xc);
		s.in.msg.setChannel(ch - 1);
		s.in.msg.setNote(prg);
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
		s.in.msg.setSize(data.length() / 2);
		for (size_t i = 0; i < data.length(); i += 2) {
			std::string bs = data.substr(i, 2);
			char byte = static_cast<char>(strtol(bs.c_str(), NULL, 16));
			s.in.msg.bytes[i / 2] = byte;
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
		s.in.msg.setSize(data.length() / 2 + 2);
		s.in.msg.bytes[0] = 0xf0;
		for (size_t i = 0; i < data.length(); i += 2) {
			std::string bs = data.substr(i, 2);
			char byte = static_cast<char>(strtol(bs.c_str(), NULL, 16));
			s.in.msg.bytes[i / 2 + 1] = byte;
		}
		s.in.msg.bytes[s.in.msg.getSize() - 1] = 0xf7;
		return JS_UNDEFINED;
	}

	static JSValue js_midi_setValue(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		size_t idx;
		if (argc < 2 || !getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midi.setValue: invalid msg");
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		uint8_t value = static_cast<uint8_t>(argNum(ctx, argv[1]));
		s.in.msg.setValue(value);
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
		s.in.msg.frame = -1;
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
		s.in.msg.frame = currentFrame + frame;
		return JS_UNDEFINED;
	}

	static JSValue js_midiOut_sendAfterTrigger(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		// midiOut.sendAfterTrigger(msg, ticks, [trigPort], [channel])
		//   2 args: msg, ticks                          (trig port 1, channel 1)
		//   3 args: msg, ticks, trigPort
		//   4 args: msg, ticks, trigPort, channel
		size_t idx;
		int trigPort = 1;
		int channel = 1;
		if (argc == 2) {
			if (!getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1])) return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
		}
		else if (argc == 3) {
			if (!getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]))
				return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
			trigPort = static_cast<int>(argNum(ctx, argv[2]));
		}
		else if (argc == 4) {
			if (!getMsgArg(ctx, argv[0], idx) || !argIsNumber(ctx, argv[1]) || !argIsNumber(ctx, argv[2]) || !argIsNumber(ctx, argv[3]))
				return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
			trigPort = static_cast<int>(argNum(ctx, argv[2]));
			channel = static_cast<int>(argNum(ctx, argv[3]));
		}
		else {
			return jsThrow(ctx, "midiOut.sendAfterTrigger: bad args");
		}
		if (trigPort < 1 || trigPort > getEngine(ctx)->inputTrigCount) return jsThrow(ctx, "midiOut.sendAfterTrigger: bad trigInput index");
		if (channel < 1 || channel > PORT_MAX_CHANNELS) return jsThrow(ctx, "midiOut.sendAfterTrigger: bad channel");
		int ticks = static_cast<int>(argNum(ctx, argv[1]));
		MessageEx& s = getEngine(ctx)->msgStore[idx];
		s.midiPort = getEngine(ctx)->selectedPort;
		int64_t currentTicks = getEngine(ctx)->handler->getTrigTicks(trigPort - 1, channel - 1);
		s.channel = (uint8_t)(channel - 1);
		s.send = true;
		s.sendOrder = getEngine(ctx)->sendCounter++;
		s.tick = currentTicks + ticks;
		return JS_UNDEFINED;
	}

	static JSValue js_trig_enableTipsyIn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		// trig.enableTipsyIn([enabled])
		//   Optional boolean: true (the default) decodes a Tipsy stream from the
		//   trigger input, false disables it. Tipsy input is only supported on the
		//   first trigger input, so — like trig.sendTipsy() — there is no port
		//   argument.
		MidiScriptEngineQuickJs* e = getEngine(ctx);
		bool enabled = (argc < 1) || (JS_ToBool(ctx, argv[0]) != 0);
		e->handler->enableTipsyIn(enabled ? 0 : -1);
		return JS_UNDEFINED;
	}

	static JSValue js_trig_sendTipsy(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
		// trig.sendTipsy(data, [mimeType])
		//   data: string or ArrayBuffer (binary data to encode)
		//   mimeType: optional string (default "text/plain")
		
		if (argc < 1) return jsThrow(ctx, "trig.sendTipsy: requires data argument");
		
		// Handle both string and ArrayBuffer for data. A string's C buffer must
		// stay alive until sendTipsyOut() copies it (see the free below).
		const unsigned char* data = nullptr;
		uint32_t dataLen = 0;
		bool dataIsString = false;
		
		if (JS_IsString(argv[0])) {
			size_t len;
			const char* str = JS_ToCStringLen(ctx, &len, argv[0]);
			if (!str) {
				return jsThrow(ctx, "trig.sendTipsy: invalid data");
			}
			data = reinterpret_cast<const unsigned char*>(str);
			dataLen = static_cast<uint32_t>(len);
			dataIsString = true;
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
			if (!mimeDyn) {
				if (dataIsString) JS_FreeCString(ctx, reinterpret_cast<const char*>(data));
				return jsThrow(ctx, "trig.sendTipsy: invalid mimeType");
			}
			mimeType = mimeDyn;
		}
		
		auto* e = getEngine(ctx);
		bool success = e->handler->sendTipsyOut(mimeType, data, dataLen);
		// sendTipsyOut() copies both payloads synchronously; free the C strings
		// now that they're no longer needed.
		if (dataIsString) JS_FreeCString(ctx, reinterpret_cast<const char*>(data));
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
