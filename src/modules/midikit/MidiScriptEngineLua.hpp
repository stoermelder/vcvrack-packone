#include "MidiScriptEngine.hpp"
extern "C" {
	#include "minilua.h"
}
#include <jansson.h>
#include "../../utils/TaskWorker.hpp"
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace StoermelderPackOne {
namespace MidiScript {
namespace Lua {

struct MidiScriptEngineLua : MidiScriptEngine {

	MidiScriptEngineLua(MidiScriptEngineHandler* handler, int inputCount, int inputTrigCount, int outputTrigCount, int paramCount, int midiInputCount, int midiOutputCount)
		: MidiScriptEngine(handler, inputCount, inputTrigCount, outputTrigCount, paramCount, midiInputCount, midiOutputCount) {}

	// ─── Message store ────────────────────────────────────────────────────────

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

	static const int msgStoreSize = 32;
	MessageEx msgStore[msgStoreSize];
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
	// A count hook (lua_sethook, LUA_MASKCOUNT) fires every interruptInterval
	// instructions; past interruptCountLimit countHook() aborts the script via
	// luaL_error(), so a `while true do end` can't wedge the shared worker.
	// Reset by beginScriptExecution() per callback (worker-thread only).
	static const int interruptInterval = 10000;  // hook fires every 10k instructions
	static const int interruptCountLimit = 10000;   // 10k counts * 10k instr = 100M
	int interruptCount = 0;

	// The hook gets a lua_State*, not the engine — reach it via a function-local
	// static thread_local (header-only, C++14: no out-of-line definition).
	static MidiScriptEngineLua*& currentEngine() {
		static thread_local MidiScriptEngineLua* e = nullptr;
		return e;
	}

	static void countHook(lua_State* L, lua_Debug* ar) {
		(void)ar;
		MidiScriptEngineLua* e = currentEngine();
		// Ignore foreign states (registerAPI stubs, teardown finalizers).
		if (!e || e->L != L) return;
		if (++e->interruptCount >= interruptCountLimit) {
			luaL_error(L, "script exceeded execution budget");
		}
	}

	// Resets the budget; call before every user lua_pcall.
	void beginScriptExecution() {
		interruptCount = 0;
		currentEngine() = this;
	}

	// ── Memory watchdog ──────────────────────────────────────────────────────
	// miniLua's allocator grows via realloc() with no cap — and, worse, large
	// strings are "external" (zero-copy, data allocated through the allocator
	// but invisible to lua_gc(LUA_GCCOUNT)) — so the only reliable footprint is
	// the allocator's own net byte count. Rather than fail individual
	// allocations, the engine watches that count after every user callback and,
	// once past memoryLimit, tears the state down (closeStateOnWorker()): the
	// script stops running and its memory is freed instead of the process being
	// ground down. Mirrors the QuickJS engine's JS_SetMemoryLimit(1 MiB) cap in
	// spirit (same threshold).
	static const size_t memoryLimit = 1024 * 1024;
	// Net bytes handed to the script's Lua state by the allocator (external
	// strings included). Written by memoryLimitedAlloc() on the worker thread;
	// read by checkMemoryLimit() (worker) and getMemoryUsage() (UI). Atomic
	// for the cross-thread UI read; relaxed is plenty for a display + watchdog.
	std::atomic<size_t> allocatedBytes{0};
	// Set when the limit is hit so the teardown+log can't re-fire; the teardown
	// also nulls L, which alone stops all further dispatch. Reset per load.
	bool memoryLimitExceeded = false;

	// Lua state allocator (installed via lua_newstate): forwards to
	// realloc/free, never failing, and keeps `allocatedBytes` as the running
	// net byte count. `ud` is `this`.
	static void* memoryLimitedAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
		MidiScriptEngineLua* e = static_cast<MidiScriptEngineLua*>(ud);
		// When ptr is NULL, this is a brand-new allocation (luaM_malloc_'s
		// firsttry(g, NULL, tag, size) call): Lua passes a GC type tag in osize
		// here, NOT an old size — per the frealloc contract, "osize" is only
		// meaningful when block != NULL. Treating that tag as a real byte count
		// undercounted every fresh allocation by a few bytes; the error
		// accumulated over thousands of allocations until allocatedBytes wrapped
		// around (size_t underflow), which spuriously tripped the memory limit
		// on scripts using only a few KB.
		size_t realOsize = ptr ? osize : 0;
		if (nsize == 0) {
			e->allocatedBytes.fetch_sub(realOsize, std::memory_order_relaxed);
			free(ptr);
			return NULL;
		}
		void* newptr = realloc(ptr, nsize);
		if (newptr != NULL) {
			e->allocatedBytes.fetch_add(nsize - realOsize, std::memory_order_relaxed);
		}
		return newptr;
	}

	// Replaces luaL_newstate()'s standard panic handler (static inside
	// minilua, hence not reachable): prints and returns 0 so Lua aborts, as
	// upstream does. Only reachable on an unprotected error, which the
	// pcall-wrapped callbacks prevent.
	static int luaPanic(lua_State* L) {
		const char* msg = lua_tostring(L, -1);
		fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n", msg ? msg : "error object is not a string");
		return 0;
	}

	// Stops the engine if the script's Lua heap (allocator-tracked, external
	// strings included) has grown past memoryLimit. Called after every user
	// callback; once it tears the state down, L is null and every later
	// dispatch path no-ops, so this fires at most once per script.
	void checkMemoryLimit() {
		if (!L || memoryLimitExceeded) return;
		if (allocatedBytes.load(std::memory_order_relaxed) > memoryLimit) {
			memoryLimitExceeded = true;
			handler->writeLog(string::f("Script exceeded the %d KB memory limit and was stopped", (int)(memoryLimit / 1024)));
			closeStateOnWorker();
		}
	}

	// The lifecycle hooks are resolved once at load and kept as registry refs,
	// not re-looked-up per dispatch — so defining/reassigning a hook later has
	// no effect; only what was present at load runs. LUA_NOREF = "not defined".
	// Asymmetry: Lua calls hooks as bare functions; QuickJS as methods (rackObj
	// as thisVal). onLoad/onUnload/onSave live on the rack table; onMessage on
	// the midi table; onTrigger/onTipsyMessage on the trig table (onTrigger so
	// trig.enableIn() can gate it). Predates caching, unused by any preset.
	int onMessageRef = LUA_NOREF;
	// Assembled extended-CC callbacks, on the midi table beside onMessage. Only
	// fire for what the script enabled via midi.enableNrpnIn()/enableRpnIn()/
	// enableCc14bitIn().
	int onNrpnRef = LUA_NOREF;
	int onRpnRef = LUA_NOREF;
	int onCc14bitRef = LUA_NOREF;
	int onTriggerRef = LUA_NOREF;
	int onTipsyMessageRef = LUA_NOREF;
	int onLoadRef = LUA_NOREF;
	int onUnloadRef = LUA_NOREF;
	int onSaveRef = LUA_NOREF;

	// Script-registered context menus. The callback lives in the registry as an
	// integer ref (luaL_ref), not the spec, since the UI thread only reads
	// presentation copies.
	struct ContextMenuEntry {
		ScriptMenuItem spec;
		int callbackRef;
		int onGetValueRef = LUA_NOREF;
	};
	// Worker-thread-owned (registerContextMenu/getContextMenus/
	// invokeContextMenuCallback); clearContextMenus() only from load/teardown,
	// which never overlaps dispatch. No mutex — see clearContextMenus().
	std::unordered_map<int, ContextMenuEntry> contextMenus;
	int nextContextMenuCallbackId = 1;

	// ─── Lua state & threading ────────────────────────────────────────────────

	// Registry key used to store `this` as a lightuserdata inside each lua_State
	static constexpr const char* REGISTRY_KEY = "stoermelder_MidiScriptEngineLua";

	// Chunk name the script is loaded under. Lua prefixes errors with
	// "<chunkname>:<line>:", so this is what the user sees. The "=" prefix
	// tells Lua to use the name verbatim rather than [string "..."].
	static constexpr const char* CHUNK_NAME = "=script";

	lua_State* L = nullptr;



	// Engine selection: true when the script's header declares @engine minilua@v1.
	// The same substring check the module used to run itself (Q26) — so the
	// module has no third header parser.
	bool testScript(const std::string& script) override {
		return script.find("@engine minilua@v1") != std::string::npos;
	}


	void loadScriptOnWorker(const char* script, const std::string& persistedConfigJson) override {
		assert(onWorkerThread());
		closeStateOnWorker();
		handler->sendTipsyOutReset();

		if (script[0] == '\0') {
			return;
		}

		// ── Parse file header ────────────────────────────────────────────────
		// Extracts @key value tags from the header block (Lua --[[ ... --]] or
		// JS /** ... */), line by line so the capture never spills into the
		// script body.
		std::map<std::string, std::string> topics;
		{
			// Regex: optional leading "* " or "-- " noise, then @key  value
			const std::regex tag_re(R"((?:\*|--)?[\s]*@([a-z]+)\s+(.*?)\s*$)");
			std::istringstream ss(script);
			std::string line;
			bool inHeader = false;
			while (std::getline(ss, line)) {
				// Detect header open  --[[  or  /**
				if (!inHeader) {
					if (line.find("--[[") != std::string::npos ||
					    line.find("/**")  != std::string::npos)
						inHeader = true;
					continue;  // tags can only appear after the opener
				}
				// Detect header close  --]]  or  */
				if (line.find("--]]") != std::string::npos ||
				    line.find("*/")   != std::string::npos)
					break;
				// Try to match a @tag line
				std::smatch m;
				if (std::regex_search(line, m, tag_re)) {
					topics[m[1].str()] = m[2].str();
				}
			}
		}

		if (topics.find("engine") == topics.end() || topics["engine"] != "minilua@v1") {
			handler->writeLog("Script is not compatible with this engine (expected @engine minilua@v1)", false);
			return;
		}

		if (topics.find("author") != topics.end()) {
			handler->writeLog(string::f("Author: %s", topics["author"].c_str()), false);
		}
		if (topics.find("description") != topics.end()) {
			handler->writeLog(topics["description"], false);
		}

		// ── Create Lua state ─────────────────────────────────────────────────
		// lua_newstate (not luaL_newstate) so every allocation — including the
		// buffers behind miniLua's zero-copy external strings — runs through
		// memoryLimitedAlloc and the net byte count is exact. luaL_newstate's
		// panic handler is replicated via lua_atpanic.
		L = lua_newstate(&memoryLimitedAlloc, this, luaL_makeseed(NULL));
		if (!L) {
			handler->writeLog("Error creating Lua state", false);
			return;
		}
		lua_atpanic(L, &luaPanic);
		allocatedBytes.store(0, std::memory_order_relaxed);
		memoryLimitExceeded = false;   // fresh state starts under the limit

		// Store engine pointer in registry so C callbacks can retrieve it
		lua_pushlightuserdata(L, this);
		lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);

		// Open only safe standard libraries (no io/os/package/debug)
		luaL_requiref(L, "_G",       luaopen_base,   1); lua_pop(L, 1);
		luaL_requiref(L, "math",     luaopen_math,   1); lua_pop(L, 1);
		luaL_requiref(L, "string",   luaopen_string, 1); lua_pop(L, 1);
		luaL_requiref(L, "table",    luaopen_table,  1); lua_pop(L, 1);

		// ── Script execution budget ─────────────────────────────────────────
		// Install the count hook. currentEngine stays null until the first
		// beginScriptExecution(), so registerAPI()'s trusted stubs aren't budgeted.
		currentEngine() = nullptr;
		interruptCount = 0;
		lua_sethook(L, &countHook, LUA_MASKCOUNT, interruptInterval);

		// ── Register engine API ──────────────────────────────────────────────
		registerAPI();

		// ── Load and run script ──────────────────────────────────────────────
		// luaL_loadbuffer, not luaL_dostring: the latter names the chunk with
		// the whole script text, so errors read as [string "/**..."]:12:.
		// Naming the chunk "script" makes them read as "script:12:".
		beginScriptExecution();
		if (luaL_loadbuffer(L, script, strlen(script), CHUNK_NAME) != LUA_OK ||
		    lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("Error loading script: %s", err ? err : "(unknown)"), false);
			lua_pop(L, 1);
			closeStateOnWorker();
			return;
		}

		handler->writeLog("Script loaded", false);

		// Cache the lifecycle hooks once (see declarations): onLoad/onUnload/
		// onSave from rack; onMessage from midi; onTrigger/onTipsyMessage from trig.
		lua_getglobal(L, "rack");
		if (lua_istable(L, -1)) {
			onLoadRef = cacheHookRef("onLoad");
			onUnloadRef = cacheHookRef("onUnload");
			onSaveRef = cacheHookRef("onSave");
		}
		lua_pop(L, 1); // pop rack table (or whatever "rack" turned out to be)

		lua_getglobal(L, "midi");
		if (lua_istable(L, -1)) {
			onMessageRef = cacheHookRef("onMessage");
			onNrpnRef = cacheHookRef("onNrpn");
			onRpnRef = cacheHookRef("onRpn");
			onCc14bitRef = cacheHookRef("onCc14bit");
		}
		lua_pop(L, 1); // pop midi table (or whatever "midi" turned out to be)

		lua_getglobal(L, "trig");
		if (lua_istable(L, -1)) {
			onTriggerRef = cacheHookRef("onTrigger");
			onTipsyMessageRef = cacheHookRef("onTipsyMessage");
		}
		lua_pop(L, 1); // pop trig table (or whatever "trig" turned out to be)

		hasOnSave.store(onSaveRef != LUA_NOREF, std::memory_order_release);

		if (onMessageRef == LUA_NOREF) {
			handler->writeLog("No midi.onMessage(midiPort, msg) function defined — incoming MIDI is ignored", false);
		}

		callOnLoad(persistedConfigJson);
		// Top-level code and onLoad() may have grown the heap past the limit;
		// stop the engine (freeing the memory) if so.
		checkMemoryLimit();
	}

	// Reads rack[name], keeping a registry ref to it if it's a function, else
	// LUA_NOREF. Assumes "rack" is on top of the stack and leaves it there —
	// the pushed value is always popped, directly or via luaL_ref.
	int cacheHookRef(const char* name) {
		lua_getfield(L, -1, name);
		if (!lua_isfunction(L, -1)) {
			lua_pop(L, 1);
			return LUA_NOREF;
		}
		return luaL_ref(L, LUA_REGISTRYINDEX); // pops the function, returns its ref
	}

	// See MidiScriptEngine::captureConfig() for the contract. Lua state is only
	// safe to touch from the worker thread, hence runSync().
	bool captureConfig(std::string& out) override {
		// Answered from the atomic, without a worker round-trip: L/onSaveRef are
		// worker-owned and must not be read from here. No script, or a script
		// without onSave(), both mean "nothing to persist" — a definite answer,
		// so true with an empty `out`, and the caller clears its stored config.
		if (!hasOnSave.load(std::memory_order_acquire)) {
			out.clear();
			return true;
		}
		return runSync([this]() -> std::string {
			if (!L) return "";
			int nRet = callOnSave();
			// callOnSave() may tear the state down if the script exceeded the
			// memory limit; nothing to persist in that case.
			if (!L) return "";
			std::string configJson;
			if (nRet > 0) {
				configJson = luaTableToJson(L, -1);
				lua_pop(L, 1);
			}
			return configJson;
		}, out);
	}

	// Tears down the Lua state. See MidiScriptEngine::closeStateOnWorker().
	void closeStateOnWorker() override {
		assert(onWorkerThread());
		if (L) {
			callOnUnload();
			// onUnload()'s teardown messages (e.g. all-notes-off) must go out.
			flushMsgStore();
			clearContextMenus();
			// lua_close invalidates these anyway; reset for hygiene.
			onMessageRef = LUA_NOREF;
			onNrpnRef = LUA_NOREF;
			onRpnRef = LUA_NOREF;
			onCc14bitRef = LUA_NOREF;
			onTriggerRef = LUA_NOREF;
			onTipsyMessageRef = LUA_NOREF;
			onLoadRef = LUA_NOREF;
			onUnloadRef = LUA_NOREF;
			onSaveRef = LUA_NOREF;
			hasOnSave.store(false, std::memory_order_release);
			// Null currentEngine first: lua_close runs finalizers with no pcall
			// boundary, where a hook luaL_error would longjmp nowhere.
			if (currentEngine() == this) currentEngine() = nullptr;
			lua_close(L);
			L = nullptr;
			// Keeps "no state ⇒ zero bytes" true unconditionally, rather than
			// relying on every allocatedBytes reader to guard on L itself.
			allocatedBytes.store(0, std::memory_order_relaxed);
		}
	}

	// Runs the script's onLoad() hook, passing the persisted config (decoded
	// from JSON) as its argument, or no argument if none. Uses onLoadRef.
	void callOnLoad(const std::string& persistedConfigJson) {
		if (onLoadRef == LUA_NOREF) return;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onLoadRef);
		int nargs = 0;
		if (!persistedConfigJson.empty() && jsonToLuaTable(L, persistedConfigJson)) {
			nargs = 1;
		}
		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, nargs, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onLoad error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}
		flushMsgStore();
	}

	// Runs onUnload(). Its return value is discarded — teardown-only; config
	// comes from onSave(). Messages are NOT flushed here: closeState() flushes
	// them for teardown.
	void callOnUnload() {
		if (onUnloadRef == LUA_NOREF) return;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onUnloadRef);
		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 0, 1, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onUnload error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
			flushMsgStore();
			return;
		}
		lua_pop(L, 1); // pop (and discard) the return value
	}

	// Runs onSave(). Returns 1 with its return value on top of the stack (kept
	// only if a table — the only persistable shape), else 0. Messages are NOT
	// flushed here: captureConfig() discards them, so a save is silent.
	int callOnSave() {
		if (onSaveRef == LUA_NOREF) return 0;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onSaveRef);
		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 0, 1, 0);
		inCallback = false;
		int nRet = 0;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onSave error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}
		// Stack is now [result].
		else if (lua_istable(L, -1)) {
			nRet = 1;
		}
		else {
			lua_pop(L, 1); // pop non-table result
		}
		// May tear the state down if the script exceeded the memory limit; the
		// caller guards on L afterwards.
		checkMemoryLimit();
		return nRet;
	}

	// ── Config persistence JSON helpers ──────────────────────────────────────
	// MiniLua has no JSON library, so config tables convert to/from JSON via
	// jansson. Lua tables serialize as arrays when keys are exactly 1..n,
	// otherwise as objects.

	// Converts the Lua value at the given index into a JSON string. Returns ""
	// if the value is not a table or contains an unsupported value type.
	static std::string luaTableToJson(lua_State* L, int idx) {
		json_t* j = luaValueToJson(L, idx);
		if (!j) return "";
		char* s = json_dumps(j, JSON_COMPACT);
		json_decref(j);
		std::string result = s ? s : "";
		if (s) free(s);
		return result;
	}

	// Recursively converts a Lua value at idx into a jansson json_t*. Returns
	// NULL for unsupported types. Leaves the Lua stack untouched.
	static json_t* luaValueToJson(lua_State* L, int idx) {
		switch (lua_type(L, idx)) {
			case LUA_TNIL: return json_null();
			case LUA_TBOOLEAN: return json_boolean(lua_toboolean(L, idx) != 0);
			case LUA_TNUMBER: {
				lua_Number n = lua_tonumber(L, idx);
				lua_Integer i = static_cast<lua_Integer>(n);
				if (n == static_cast<lua_Number>(i)) return json_integer(static_cast<json_int_t>(i));
				return json_real(static_cast<double>(n));
			}
			case LUA_TSTRING: {
				size_t len;
				const char* s = lua_tolstring(L, idx, &len);
				return json_stringn(s, len);
			}
			case LUA_TTABLE: return luaTableToJsonValue(L, idx);
			default: return NULL;
		}
	}

	// Converts a Lua table into a jansson value, detecting whether it is an
	// array (integer keys exactly 1..n) or an object.
	static json_t* luaTableToJsonValue(lua_State* L, int idx) {
		int absIdx = lua_absindex(L, idx);

		// Classify the table: an array when every key is a positive integer
		// and the keys are exactly 1..n (no gaps, no extra fields).
		size_t highest = 0;
		size_t totalKeys = 0;
		bool arrayLike = true;
		lua_pushnil(L);
		while (lua_next(L, absIdx) != 0) {
			// key at -2, value at -1
			totalKeys++;
			if (lua_type(L, -2) == LUA_TNUMBER) {
				lua_Number k = lua_tonumber(L, -2);
				lua_Integer ki = static_cast<lua_Integer>(k);
				if (k == static_cast<lua_Number>(ki) && ki > 0) {
					if (static_cast<size_t>(ki) > highest) highest = static_cast<size_t>(ki);
				}
				else {
					arrayLike = false;
				}
			}
			else {
				arrayLike = false;
			}
			lua_pop(L, 1); // pop value, keep key for the next lua_next
		}
		// NOTE: lua_next() already pops the final key when it returns 0, so
		// the stack is balanced here — no extra pop.

		if (arrayLike && totalKeys == highest) {
			json_t* arr = json_array();
			for (size_t i = 1; i <= highest; i++) {
				lua_rawgeti(L, absIdx, static_cast<lua_Integer>(i));
				json_t* val = luaValueToJson(L, -1);
				lua_pop(L, 1);
				if (!val) {
					json_decref(arr);
					return NULL;
				}
				json_array_append_new(arr, val);
			}
			return arr;
		}
		else {
			json_t* obj = json_object();
			lua_pushnil(L);
			while (lua_next(L, absIdx) != 0) {
				// key at -2, value at -1
				std::string key = luaKeyToString(L, -2);
				json_t* val = luaValueToJson(L, -1);
				if (!key.empty() && val) {
					json_object_set_new(obj, key.c_str(), val);
				}
				else if (val) {
					json_decref(val);
				}
				lua_pop(L, 1); // pop value, keep key for the next lua_next
			}
			// NOTE: lua_next() already pops the final key when it returns 0,
			// so the stack is balanced here — no extra pop.
			return obj;
		}
	}

	// Converts a Lua key (string or number) at idx into a std::string. Returns
	// "" for unsupported key types.
	static std::string luaKeyToString(lua_State* L, int idx) {
		if (lua_type(L, idx) == LUA_TSTRING) {
			size_t len;
			const char* s = lua_tolstring(L, idx, &len);
			return std::string(s, len);
		}
		if (lua_type(L, idx) == LUA_TNUMBER) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(lua_tointeger(L, idx)));
			return buf;
		}
		return "";
	}

	// Parses a JSON string into a Lua table pushed onto the stack. Returns
	// true on success (table on top), false otherwise (nothing pushed).
	static bool jsonToLuaTable(lua_State* L, const std::string& json) {
		json_error_t error;
		json_t* j = json_loads(json.c_str(), 0, &error);
		if (!j) return false;
		bool ok = pushJsonAsLua(L, j);
		json_decref(j);
		return ok;
	}

	// Recursively converts a jansson value into a Lua value pushed onto the
	// stack. Returns true on success, false otherwise (nothing is pushed).
	static bool pushJsonAsLua(lua_State* L, json_t* j) {
		if (json_is_object(j)) {
			lua_newtable(L);
			const char* key;
			json_t* val;
			json_object_foreach(j, key, val) {
				if (!pushJsonAsLua(L, val)) {
					lua_pop(L, 1); // pop the partial table
					return false;
				}
				lua_setfield(L, -2, key);
			}
			return true;
		}
		else if (json_is_array(j)) {
			lua_newtable(L);
			size_t index;
			json_t* val;
			json_array_foreach(j, index, val) {
				if (!pushJsonAsLua(L, val)) {
					lua_pop(L, 1); // pop the partial table
					return false;
				}
				lua_rawseti(L, -2, static_cast<lua_Integer>(index) + 1); // 1-based
			}
			return true;
		}
		else if (json_is_number(j)) {
			lua_pushnumber(L, static_cast<lua_Number>(json_number_value(j)));
			return true;
		}
		else if (json_is_string(j)) {
			lua_pushstring(L, json_string_value(j));
			return true;
		}
		else if (json_is_boolean(j)) {
			lua_pushboolean(L, json_is_true(j) != 0);
			return true;
		}
		else if (json_is_null(j)) {
			lua_pushnil(L);
			return true;
		}
		return false;
	}

	void processInMessage(int midiPort, const MidiScript::QueuedMessage& msg) override {
		if (L) {
			midiInQueue.push(std::make_tuple(midiPort, msg));
		}
	}

	void processInTick(int trigPort, uint8_t channel) override {
		if (L) {
			tickInQueue.push(std::make_tuple(trigPort, channel));
		}
	}

	// Sends every message emitted during the callback that just ran through the
	// handler, in send() order, not handle-creation order: a script may create
	// several messages and send them in a different order, and the receiver
	// must observe send() order.
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


	std::string getInputName(int i) override {
		if (!L) return "";
		return callLuaTableFunc("input", "getName", i + 1);
	}

	std::string getParamName(int i) override {
		if (!L) return "";
		return callLuaTableFunc("param", "getName", i + 1);
	}

	std::string getParamFormatValue(int i) override {
		if (!L) return "";
		return callLuaTableFunc("param", "getValueFormat", i + 1);
	}

	// Releases the stored script callbacks. Called only from
	// closeStateOnWorker(), so like every other toucher of contextMenus this
	// runs on the worker thread — hence no lock.
	void clearContextMenus() {
		assert(onWorkerThread());
		if (L) {
			for (auto& kv : contextMenus) {
				luaL_unref(L, LUA_REGISTRYINDEX, kv.second.callbackRef);
				if (kv.second.onGetValueRef != LUA_NOREF) {
					luaL_unref(L, LUA_REGISTRYINDEX, kv.second.onGetValueRef);
				}
			}
		}
		contextMenus.clear();
		nextContextMenuCallbackId = 1;
	}

	void getContextMenus(const std::function<void(const std::vector<ScriptMenuItem>&)>& callback) override {
		// The whole snapshot (incl. each onGetValue ref) is built on the worker
		// thread, so no copy is needed on the UI thread.
		runAsync([this, callback]() {
			assert(onWorkerThread());
			if (!L) return;
			struct Snapshot { int id; ScriptMenuItem spec; int onGetValueRef; };
			std::vector<Snapshot> snap;
			snap.reserve(contextMenus.size());
			for (const auto& kv : contextMenus) {
				snap.push_back({kv.first, kv.second.spec, kv.second.onGetValueRef});
			}
			// callbackIds are assigned monotonically at registration, so sorting
			// by them yields registration order — the unordered_map's own
			// iteration order is unspecified.
			std::sort(snap.begin(), snap.end(), [](const Snapshot& a, const Snapshot& b) {
				return a.id < b.id;
			});
			std::vector<ScriptMenuItem> result;
			result.reserve(snap.size());
			for (const Snapshot& s : snap) {
				ScriptMenuItem spec = s.spec;
				int ref = s.onGetValueRef;
				if (ref != LUA_NOREF) {
					lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
					beginScriptExecution();
					int status = lua_pcall(L, 0, 1, 0);
					if (status == LUA_OK) {
						if (spec.type == ScriptMenuItem::Type::Boolean) {
							spec.checked = lua_toboolean(L, -1) != 0;
						}
						else {
							lua_Integer sel = lua_tointeger(L, -1);
							spec.selected = static_cast<int>(std::max<lua_Integer>(0, std::min<lua_Integer>(sel, static_cast<lua_Integer>(spec.options.size()) - 1)));
						}
						lua_pop(L, 1); // pop result
					}
					else {
						const char* err = lua_tostring(L, -1);
						handler->writeLog(string::f("Context menu error: %s", err ? err : "(unknown)"));
						lua_pop(L, 1); // pop error message
					}
				}
				result.push_back(spec);
			}
			// Run the caller's callback with the evaluated specs. It only touches
			// memory the caller owns (never constructs widgets), so it's safe on
			// the worker thread.
			checkMemoryLimit();
			callback(result);
		});
	}

	// Fires a menu item's onChange callback on the worker thread. Presentation
	// state isn't stored on the spec — the next menu build re-evaluates it from
	// onGetValue, so onChange's config changes are picked up automatically. The
	// call is deferred to runAsync() with all other Lua work.
	void invokeContextMenuCallback(int callbackId, int value) override {
		// The whole body runs on the worker thread, incl. the spec lookup:
		// contextMenus is worker-owned, so no lock. The caller ignores timing,
		// so the read needn't be synchronous on the UI thread.
		runAsync([this, callbackId, value]() {
			assert(onWorkerThread());
			if (!L) return;
			auto it = contextMenus.find(callbackId);
			if (it == contextMenus.end()) return;
			const ContextMenuEntry& entry = it->second;
			ScriptMenuItem::Type type = entry.spec.type;
			std::string label;
			if (type != ScriptMenuItem::Type::Boolean) {
				if (value < 0 || value >= static_cast<int>(entry.spec.options.size())) return;
				label = entry.spec.options[value];
			}
			int ref = entry.callbackRef;
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
			int nargs;
			if (type == ScriptMenuItem::Type::Boolean) {
				lua_pushboolean(L, value != 0);
				nargs = 1;
			}
			else {
				lua_pushinteger(L, value);
				lua_pushlstring(L, label.c_str(), label.size());
				nargs = 2;
			}
			beginScriptExecution();
			int status = lua_pcall(L, nargs, 0, 0);
			if (status != LUA_OK) {
				const char* err = lua_tostring(L, -1);
				handler->writeLog(string::f("Context menu callback error: %s", err ? err : "(unknown)"));
				lua_pop(L, 1); // pop error message
			}
			checkMemoryLimit();
		});
	}

	// Current/total bytes for the Lua state, or false if no script is loaded.
	// Unlike lua_gc(LUA_GCCOUNT) — which misses miniLua's zero-copy external
	// strings —`used` reports the allocator-tracked footprint (external
	// strings included), the same number the memory watchdog enforces against.
	// `total` is memoryLimit: unlike QuickJS's JS_SetMemoryLimit, nothing here
	// rejects individual allocations at that ceiling — checkMemoryLimit() only
	// notices and tears the state down after the fact — but it's the same
	// number the watchdog acts on, so showing it alongside `used` (matching
	// the QuickJS UI) tells the user how close a script is to being stopped.
	bool getMemoryUsage(size_t& used, size_t& total) {
		if (!L) return false;
		used = allocatedBytes.load(std::memory_order_relaxed);
		total = memoryLimit;
		return true;
	}


	void dispatchMidiMessage(int midiPort, Message& msg) override {
		if (!L) return;

		// Assigning the whole QueuedMessage (not just .msg) also resets the decode
		// fields to their defaults, so a plain message cannot report the type or
		// parameter of an assembled one that used slot 0 before it.
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

		// Calls the cached onMessageRef. No-op if never defined (LUA_NOREF).
		if (onMessageRef == LUA_NOREF) return;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onMessageRef);
		lua_pushinteger(L, midiPort + 1);
		lua_pushinteger(L, 0);
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onMessage error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		flushMsgStore();
		checkMemoryLimit();
	}

	// Dispatches onTrigger(trigPort, channel) via the cached onTriggerRef.
	// No-op if never defined; the module only enqueues ticks for enabled channels.
	void dispatchTrigger(int trigPort, uint8_t channel) override {
		if (!L) return;
		if (onTriggerRef == LUA_NOREF) return;

		lua_rawgeti(L, LUA_REGISTRYINDEX, onTriggerRef);
		lua_pushinteger(L, trigPort + 1);
		lua_pushinteger(L, channel + 1);
		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onTrigger error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		flushMsgStore();
		checkMemoryLimit();
	}

	// Dispatches an assembled message to onNrpn/onRpn/onCc14bit as a handle,
	// exactly like dispatchMidiMessage() does for onMessage: the message lands in
	// store slot 0 and the callback receives (midiPort, 0), reading it through
	// midi.getControl()/getValue()/getChannel(). No-op if the hook was never
	// defined.
	void dispatchAssembled(int ref, const char* name, int midiPort, const QueuedMessage& q) {
		if (!L) return;
		if (ref == LUA_NOREF) return;

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

		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		lua_pushinteger(L, midiPort + 1);
		lua_pushinteger(L, 0);   // handle 0 — the message just stored
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("%s error: %s", name, err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		flushMsgStore();
		checkMemoryLimit();
	}

	void dispatchNrpn(int midiPort, const QueuedMessage& q, bool isRpn) override {
		dispatchAssembled(isRpn ? onRpnRef : onNrpnRef, isRpn ? "onRpn" : "onNrpn", midiPort, q);
	}

	void dispatchCc14bit(int midiPort, const QueuedMessage& q) override {
		dispatchAssembled(onCc14bitRef, "onCc14bit", midiPort, q);
	}

	// Dispatches onTipsyMessage(data, mimeType) via the cached onTipsyMessageRef.
	// No-op if never defined. `data` is pushed with an explicit length so NUL
	// bytes survive — Lua strings are not NUL-terminated.
	void dispatchTipsyMessage(const MidiScript::TipsyMessage& msg) override {
		if (!L) return;
		if (onTipsyMessageRef == LUA_NOREF) return;

		lua_rawgeti(L, LUA_REGISTRYINDEX, onTipsyMessageRef);
		lua_pushlstring(L, reinterpret_cast<const char*>(msg.data), msg.dataSize);
		lua_pushstring(L, msg.mime);
		msgCount = 0;
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onTipsyMessage error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		flushMsgStore();
		checkMemoryLimit();
	}

	std::string callLuaTableFunc(const char* tableName, const char* funcName, int arg) {
		// stack must be balanced on return
		lua_getglobal(L, tableName);
		if (!lua_istable(L, -1)) { lua_pop(L, 1); return ""; }

		lua_getfield(L, -1, funcName);
		if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return ""; }

		lua_pushinteger(L, arg);
		std::string result;
		beginScriptExecution();
		if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
			const char* s = lua_tostring(L, -1);
			result = s ? s : "";
			lua_pop(L, 1);
		}
		else {
			lua_pop(L, 1); // error message
		}
		lua_pop(L, 1); // table
		checkMemoryLimit();
		return result;
	}

	// Retrieve engine from registry — used in all static C callbacks
	static MidiScriptEngineLua* getEngine(lua_State* L) {
		lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
		auto* e = static_cast<MidiScriptEngineLua*>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		return e;
	}

	// Validate a msgStore index (stack arg at `stackPos`, 0-based)
	static MessageEx* getMsg(lua_State* L, int stackPos) {
		auto* e = getEngine(L);
		if (!lua_isinteger(L, stackPos) && !lua_isnumber(L, stackPos)) {
			luaL_argerror(L, stackPos, "message index expected");
			return nullptr;
		}
		int idx = static_cast<int>(lua_tointeger(L, stackPos));
		if (idx < 0 || static_cast<size_t>(idx) >= e->msgCount) {
			luaL_argerror(L, stackPos, "invalid message index");
			return nullptr;
		}
		return &e->msgStore[idx];
	}


	void registerAPI() {
		// ── rack table ──────────────────────────────────────────────────────
		lua_newtable(L);
		setTableFunc("log",      lua_rack_log);
		setTableFunc("overlay",  lua_rack_overlay);
		setTableFunc("getFrame", lua_rack_getFrame);
		setTableFunc("random",   lua_rack_random);
		setTableFunc("registerContextMenu", lua_rack_registerContextMenu);
		lua_setglobal(L, "rack");

		// ── number table ─────────────────────────────────────────────────────
		// Mostly wraps existing Lua math.*; provided for JS script compatibility.
		lua_newtable(L);
		setTableFunc("crossfade", lua_number_crossfade);
		setTableFunc("rescale",   lua_number_rescale);
		setTableFunc("toString",  lua_number_toString);
		lua_setglobal(L, "number");

		// ── input table ──────────────────────────────────────────────────────
		// Default getName provided in Lua; scripts may override.
		luaL_dostring(L,
			"input = {\n"
			"    getName = function(i) return 'Port ' .. i end\n"
			"}\n"
		);
		lua_getglobal(L, "input");
		setTableFunc("enable",     lua_input_enable);
		setTableFunc("getVoltage", lua_input_getVoltage);
		setTableFunc("isHigh",     lua_input_isHigh);
		setTableFunc("isLow",      lua_input_isLow);
		lua_pop(L, 1);

		// ── trig table ───────────────────────────────────────────────────────
		lua_newtable(L);
		setTableFunc("enableIn",    lua_trig_enableIn);
		setTableFunc("getTicks",    lua_trig_getTicks);
		setTableFunc("isHigh",      lua_trig_isHigh);
		setTableFunc("isLow",       lua_trig_isLow);
		setTableFunc("setGate",     lua_trig_setGate);
		setTableFunc("setHigh",     lua_trig_setHigh);
		setTableFunc("setLow",      lua_trig_setLow);
		setTableFunc("setTrigger",  lua_trig_setTrigger);
		setTableFunc("sendTipsy",   lua_trig_sendTipsy);
		setTableFunc("enableTipsyIn", lua_trig_enableTipsyIn);
		lua_setglobal(L, "trig");

		// ── param table ──────────────────────────────────────────────────────
		luaL_dostring(L,
			"param = {\n"
			"    getName        = function(i) return 'Param ' .. i end,\n"
			"    getValueFormat = function(i) return '' end\n"
			"}\n"
		);
		lua_getglobal(L, "param");
		setTableFunc("enable",   lua_param_enable);
		setTableFunc("getValue", lua_param_getValue);
		lua_pop(L, 1);

		// ── midi table ───────────────────────────────────────────────────────
		lua_newtable(L);
		setTableFunc("create",          lua_midi_create);
		setTableFunc("clone",           lua_midi_clone);
		setTableFunc("createNRPN",      lua_midi_createNrpn);
		setTableFunc("createCc14bit",   lua_midi_createCc14bit);
		setTableFunc("getChanPressure", lua_midi_getChanPressure);
		setTableFunc("getChannel",      lua_midi_getChannel);
		setTableFunc("getLength",       lua_midi_getLength);
		setTableFunc("getNote",         lua_midi_getNote);
		setTableFunc("getPitchWheel",   lua_midi_getPitchWheel);
		setTableFunc("getProgramChange",lua_midi_getProgramChange);
		setTableFunc("getRaw",          lua_midi_getRaw);
		setTableFunc("getSysEx",        lua_midi_getSysEx);
		setTableFunc("getSysExLength",  lua_midi_getSysExLength);
		setTableFunc("getControl",      lua_midi_getControl);
		setTableFunc("getValue",        lua_midi_getValue);
		setTableFunc("isCc",            lua_midi_isCc);
		setTableFunc("isCc14bit",       lua_midi_isCc14bit);
		setTableFunc("isNrpn",          lua_midi_isNrpn);
		setTableFunc("isRpn",           lua_midi_isRpn);
		setTableFunc("isChanPressure",  lua_midi_isChanPressure);
		setTableFunc("isClock",         lua_midi_isClock);
		setTableFunc("isContinue",      lua_midi_isContinue);
		setTableFunc("isKeyPressure",   lua_midi_isKeyPressure);
		setTableFunc("isNoteOff",       lua_midi_isNoteOff);
		setTableFunc("isNoteOn",        lua_midi_isNoteOn);
		setTableFunc("isPitchWheel",    lua_midi_isPitchWheel);
		setTableFunc("isProgramChange", lua_midi_isProgramChange);
		setTableFunc("isStart",         lua_midi_isStart);
		setTableFunc("isStop",          lua_midi_isStop);
		setTableFunc("isSysEx",         lua_midi_isSysEx);
		setTableFunc("setCc",           lua_midi_setCc);
		setTableFunc("setCc14bit",      lua_midi_setCc14bit);
		setTableFunc("setChannel",      lua_midi_setChannel);
		setTableFunc("setChanPressure", lua_midi_setChanPressure);
		setTableFunc("setKeyPressure",  lua_midi_setKeyPressure);
		setTableFunc("setNote",         lua_midi_setNote);
		setTableFunc("setNoteOff",      lua_midi_setNoteOff);
		setTableFunc("setNoteOn",       lua_midi_setNoteOn);
		setTableFunc("setNRPN",         lua_midi_setNrpn);
		setTableFunc("setPitchWheel",   lua_midi_setPitchWheel);
		setTableFunc("setProgramChange",lua_midi_setProgramChange);
		setTableFunc("setRaw",          lua_midi_setRaw);
		setTableFunc("setSysEx",        lua_midi_setSysEx);
		setTableFunc("setValue",        lua_midi_setValue);
		setTableFunc("enableNrpnIn",    lua_midi_enableNrpnIn);
		setTableFunc("enableRpnIn",     lua_midi_enableRpnIn);
		setTableFunc("enableCc14bitIn", lua_midi_enableCc14bitIn);
		lua_setglobal(L, "midi");

		// ── midiOut table ────────────────────────────────────────────────────
		lua_newtable(L);
		setTableFunc("selectPort",         lua_midiOut_selectPort);
		setTableFunc("send",               lua_midiOut_send);
		setTableFunc("sendAfterMs",        lua_midiOut_sendAfterMs);
		setTableFunc("sendAfterTrigger",   lua_midiOut_sendAfterTrigger);
		lua_setglobal(L, "midiOut");
	}

	// Helper: push a C function as a field on the table currently at the top of the stack
	void setTableFunc(const char* name, lua_CFunction fn) {
		lua_pushcfunction(L, fn);
		lua_setfield(L, -2, name);
	}

	// ─── Static C callbacks ───────────────────────────────────────────────────

	// ── log / overlay ─────────────────────────────────────────────────────────

	static int lua_rack_log(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		if (n < 1) return luaL_error(L, "log: bad args");
		// Concatenate every argument into one log line with the same per-type
		// contract as a single value: numbers via formatNumber (same as
		// number.toString()), strings verbatim, nil as "null" (matching JS),
		// else luaL_tolstring — so the call never errors.
		std::string log;
		for (int i = 1; i <= n; i++) {
			switch (lua_type(L, i)) {
				case LUA_TNUMBER: {
					char buf[32];
					formatNumber(static_cast<float>(lua_tonumber(L, i)), buf, sizeof(buf));
					log += buf;
					break;
				}
				case LUA_TBOOLEAN:
					log += lua_toboolean(L, i) ? "true" : "false";
					break;
				case LUA_TSTRING: {
					size_t len;
					log += lua_tolstring(L, i, &len);
					break;
				}
				case LUA_TNIL:
					log += "null";
					break;
				default: {
					size_t len;
					const char* s = luaL_tolstring(L, i, &len);
					log += s;
					lua_pop(L, 1);  // luaL_tolstring leaves the string on the stack
					break;
				}
			}
		}
		e->handler->writeLog(log);
		return 0;
	}

	static int lua_rack_overlay(lua_State* L) {
		int n = lua_gettop(L);
		const char* s1 = luaL_checkstring(L, 1);
		const char* s2 = n >= 2 ? luaL_checkstring(L, 2) : "";
		const char* s3 = n >= 3 ? luaL_checkstring(L, 3) : "";
		getEngine(L)->handler->writeOverlay(s1, s2, s3);
		return 0;
	}

	static int lua_rack_getFrame(lua_State* L) {
		lua_pushnumber(L, static_cast<lua_Number>(APP->engine->getFrame()));
		return 1;
	}

	// rack.registerContextMenu(options) — registers one item in the module's
	// context menu:
	//   { type = "boolean", label, onGetValue = fn() -> bool, onChange = fn(checked) }
	//   { type = "options", label, options = {...}, onGetValue = fn() -> int, onChange = fn(idx, label) }
	// onGetValue is optional (defaults to 0) and evaluated lazily on the worker
	// thread when the menu is built, so it always reflects the live config.
	// Callbacks are stored as registry refs and fired on the worker thread.
	static int lua_rack_registerContextMenu(lua_State* L) {
		auto* e = getEngine(L);
		if (lua_gettop(L) < 1 || !lua_istable(L, 1))
			return luaL_error(L, "registerContextMenu: expected a table");

		ScriptMenuItem spec;

		lua_getfield(L, 1, "type");
		if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "registerContextMenu: type must be a string");
		std::string type = lua_tostring(L, -1);
		lua_pop(L, 1);
		if (type == "options") spec.type = ScriptMenuItem::Type::Options;
		else if (type == "boolean") spec.type = ScriptMenuItem::Type::Boolean;
		else return luaL_error(L, "registerContextMenu: type must be \"boolean\" or \"options\"");

		lua_getfield(L, 1, "label");
		if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "registerContextMenu: label must be a string");
		size_t labelLen;
		const char* label = lua_tolstring(L, -1, &labelLen);
		if (labelLen == 0) return luaL_error(L, "registerContextMenu: label must be a non-empty string");
		spec.label.assign(label, labelLen);
		lua_pop(L, 1);

		lua_getfield(L, 1, "onChange");
		if (!lua_isfunction(L, -1)) return luaL_error(L, "registerContextMenu: onChange must be a function");

		if (spec.type == ScriptMenuItem::Type::Options) {
			lua_getfield(L, 1, "options");
			if (!lua_istable(L, -1)) return luaL_error(L, "registerContextMenu: options must be a non-empty array of strings");
			lua_len(L, -1);
			lua_Integer len = lua_tointeger(L, -1);
			lua_pop(L, 1); // pop length; options table is now on top
			if (len <= 0) return luaL_error(L, "registerContextMenu: options must be a non-empty array of strings");
			spec.options.resize(static_cast<size_t>(len));
			for (lua_Integer i = 1; i <= len; i++) {
				lua_rawgeti(L, -1, i); // push options[i]
				if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "registerContextMenu: options must contain only strings");
				size_t olen;
				const char* os = lua_tolstring(L, -1, &olen);
				spec.options[static_cast<size_t>(i - 1)].assign(os, olen);
				lua_pop(L, 1);
			}
			lua_pop(L, 1); // pop options table
		}

		// The current value isn't read at registration; it's evaluated lazily
		// from onGetValue when the menu is built (optional, defaults to 0). Its
		// ref is taken before onChange's so an options-validation error can't
		// leak it.
		lua_getfield(L, 1, "onGetValue");
		int onGetValueRef = LUA_NOREF;
		if (lua_isfunction(L, -1)) {
			onGetValueRef = luaL_ref(L, LUA_REGISTRYINDEX); // pops onGetValue
		}
		else {
			lua_pop(L, 1); // ignore non-function onGetValue
		}

		assert(e->onWorkerThread());
		spec.callbackId = e->nextContextMenuCallbackId++;
		int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops the onChange function
		ContextMenuEntry entry;
		entry.spec = spec;
		entry.callbackRef = ref;
		entry.onGetValueRef = onGetValueRef;
		e->contextMenus[spec.callbackId] = entry;

		lua_pushboolean(L, 1);
		return 1;
	}

	// ── number.* ──────────────────────────────────────────────────────────────

	static int lua_number_crossfade(lua_State* L) {
		float a = static_cast<float>(luaL_checknumber(L, 1));
		float b = static_cast<float>(luaL_checknumber(L, 2));
		float p = static_cast<float>(luaL_checknumber(L, 3));
		lua_pushnumber(L, rack::crossfade(a, b, p));
		return 1;
	}

	static int lua_rack_random(lua_State* L) {
		lua_pushnumber(L, rack::random::uniform());
		return 1;
	}

	static int lua_number_rescale(lua_State* L) {
		int n = lua_gettop(L);
		float x = static_cast<float>(luaL_checknumber(L, 1));
		float xMin = static_cast<float>(luaL_checknumber(L, 2));
		float xMax = static_cast<float>(luaL_checknumber(L, 3));
		float yMin = static_cast<float>(luaL_checknumber(L, 4));
		float yMax = static_cast<float>(luaL_checknumber(L, 5));
		if (n >= 6) {
			float a = static_cast<float>(luaL_checknumber(L, 6));
			x = rack::rescale(x, xMin, xMax, 1.f, static_cast<float>(M_E));
			x = std::exp(std::pow(std::log(x), dsp::exp2_taylor5(a)));
			x = rack::rescale(x, 1.f, static_cast<float>(M_E), yMin, yMax);
		}
		else {
			x = rack::rescale(x, xMin, xMax, yMin, yMax);
		}
		lua_pushnumber(L, x);
		return 1;
	}

	// Formats f with up to 6 decimals, trimming trailing zeros (and a trailing
	// '.') so 42.0 prints as "42" — matching the old %i/%f split without two
	// branches — and non-integers print only the decimals they actually have.
	static void formatNumber(float f, char* buf, size_t bufSize) {
		snprintf(buf, bufSize, "%f", f);
		char* end = buf + strlen(buf) - 1;
		while (end > buf && *end == '0') { *end = '\0'; end--; }
		if (end > buf && *end == '.') { *end = '\0'; }
	}

	static int lua_number_toString(lua_State* L) {
		float f = static_cast<float>(luaL_checknumber(L, 1));
		char buf[32];
		formatNumber(f, buf, sizeof(buf));
		lua_pushstring(L, buf);
		return 1;
	}

	// ── input.* ───────────────────────────────────────────────────────────────

	static int lua_input_enable(lua_State* L) {
		auto* e = getEngine(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		e->handler->enableInput(i - 1);
		return 0;
	}

	static int lua_input_getVoltage(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		uint8_t ch = 1;
		if (n >= 2) ch = static_cast<uint8_t>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushnumber(L, e->handler->getInputVoltage(i - 1, ch - 1));
		return 1;
	}

	static int lua_input_isHigh(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		uint8_t ch = 1;
		if (n >= 2) ch = static_cast<uint8_t>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->handler->getInputVoltage(i - 1, ch - 1) > 0.7f);
		return 1;
	}

	static int lua_input_isLow(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		uint8_t ch = 1;
		if (n >= 2) ch = static_cast<uint8_t>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->handler->getInputVoltage(i - 1, ch - 1) < 0.7f);
		return 1;
	}

	// ── trig.* ────────────────────────────────────────────────────────────────

	// trig.enableIn(trigPort, [channel = 1]) — enables trig.onTrigger on that
	// (port, channel); the callback is unused until called. The module also
	// gates all other trigger processing on the enabled state.
	static int lua_trig_enableIn(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = static_cast<int>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->handler->enableTrigger(i - 1, ch - 1);
		return 0;
	}

	static int lua_trig_getTicks(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = static_cast<int>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushinteger(L, static_cast<lua_Integer>(e->handler->getTrigTicks(i - 1, ch - 1)));
		return 1;
	}

	static int lua_trig_isHigh(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = static_cast<int>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->handler->getTrigVoltage(i - 1, ch - 1) > 0.7f);
		return 1;
	}

	static int lua_trig_isLow(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = static_cast<int>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->handler->getTrigVoltage(i - 1, ch - 1) < 0.7f);
		return 1;
	}

	static int lua_trig_setGate(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		if (n < 2) luaL_error(L, "trig.setGate: expected (port [,ch], duration)");
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		float duration;
		if (n == 3) {
			ch = static_cast<int>(luaL_checkinteger(L, 2));
			duration = static_cast<float>(luaL_checknumber(L, 3));
		}
		else {
			duration = static_cast<float>(luaL_checknumber(L, 2));
		}
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		// The script API is milliseconds (per docs); dsp::PulseGenerator::trigger()
		// takes seconds, so convert here.
		e->handler->setTrig(i - 1, ch - 1, duration / 1000.f);
		return 0;
	}

	static int lua_trig_setHigh(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = static_cast<int>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->handler->setTrigVoltage(i - 1, ch - 1, 10.f);
		return 0;
	}

	static int lua_trig_setLow(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = static_cast<int>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->handler->setTrigVoltage(i - 1, ch - 1, 0.f);
		return 0;
	}

	static int lua_trig_setTrigger(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = static_cast<int>(luaL_checkinteger(L, 2));
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->handler->setTrig(i - 1, ch - 1);
		return 0;
	}

	// ── param.* ───────────────────────────────────────────────────────────────

	static int lua_param_enable(lua_State* L) {
		auto* e = getEngine(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->paramCount) luaL_argerror(L, 1, "param index out of range");
		e->handler->enableParam(i - 1);
		return 0;
	}

	static int lua_param_getValue(lua_State* L) {
		auto* e = getEngine(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->paramCount) luaL_argerror(L, 1, "param index out of range");
		lua_pushnumber(L, e->handler->getParamValue(i - 1));
		return 1;
	}

	// ── midi.* ────────────────────────────────────────────────────────────────

	// Warns when a message is created outside a callback — the store resets
	// every callback, silently invalidating such a handle (see midi.create()
	// in SCRIPTING.md).
	static void warnIfOutsideCallback(MidiScriptEngineLua* e, const char* fn) {
		if (!e->inCallback) {
			e->handler->writeLog(string::f("%s: called outside a callback; the message "
				"is discarded when the next MIDI message arrives", fn), false);
		}
	}

	static int lua_midiOut_selectPort(lua_State* L) {
		auto* e = getEngine(L);
		int midiPort = static_cast<int>(luaL_checkinteger(L, 1));
		if (midiPort < 1 || midiPort > e->midiOutputCount) luaL_argerror(L, 1, "invalid output port index");
		e->selectedPort = midiPort - 1;
		return 0;
	}

	static int lua_midi_create(lua_State* L) {
		auto* e = getEngine(L);
		warnIfOutsideCallback(e, "midi.create");
		size_t* s = &e->msgCount;
		if (*s >= static_cast<size_t>(msgStoreSize)) {
			luaL_error(L, "midi.create: message store full");
		}
		e->msgStore[*s] = MessageEx();
		lua_pushinteger(L, static_cast<lua_Integer>((*s)++));
		return 1;
	}

	static int lua_midi_clone(lua_State* L) {
		auto* e = getEngine(L);
		MessageEx* src = getMsg(L, 1);
		warnIfOutsideCallback(e, "midi.clone");
		size_t* s = &e->msgCount;
		if (*s >= static_cast<size_t>(msgStoreSize)) {
			luaL_error(L, "midi.clone: message store full");
		}
		// Copy only the MIDI payload; the clone starts fresh and unsent (all
		// fields at defaults) so it can be modified and sent independently.
		MessageEx clone;
		clone.in.msg = src->in.msg;
		e->msgStore[*s] = clone;
		lua_pushinteger(L, static_cast<lua_Integer>((*s)++));
		return 1;
	}

	static int lua_midi_createNrpn(lua_State* L) {
		auto* e = getEngine(L);
		warnIfOutsideCallback(e, "midi.createNRPN");
		size_t* s = &e->msgCount;
		if (*s + 4 > static_cast<size_t>(msgStoreSize)) {
			luaL_error(L, "midi.createNRPN: message store full");
		}
		e->msgStore[*s + 0] = MessageEx();
		e->msgStore[*s + 0].isNrpn = true;
		e->msgStore[*s + 1] = MessageEx();
		e->msgStore[*s + 2] = MessageEx();
		e->msgStore[*s + 3] = MessageEx();
		lua_Integer idx = static_cast<lua_Integer>(*s);
		*s += 4;
		lua_pushinteger(L, idx);
		return 1;
	}

	static int lua_midi_createCc14bit(lua_State* L) {
		auto* e = getEngine(L);
		warnIfOutsideCallback(e, "midi.createCc14bit");
		size_t* s = &e->msgCount;
		if (*s + 2 > static_cast<size_t>(msgStoreSize)) {
			luaL_error(L, "midi.createCc14bit: message store full");
		}
		// 2 consecutive entries, filled by setCc14bit: CC cc (value MSB) and
		// CC cc+32 (value LSB), flushed atomically as a pair.
		e->msgStore[*s + 0] = MessageEx();
		e->msgStore[*s + 0].isCc14bit = true;
		e->msgStore[*s + 1] = MessageEx();
		lua_Integer idx = static_cast<lua_Integer>(*s);
		*s += 2;
		lua_pushinteger(L, idx);
		return 1;
	}

	static int lua_midi_getChanPressure(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getNote());
		return 1;
	}

	static int lua_midi_getChannel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		// Status 0xf (realtime/SysEx) carries no channel; the low nibble is a
		// sub-type selector, so "+ 1" returned a meaningless channel (#A4).
		// -1 is unambiguous: 1-16 is the only valid range, so a script can
		// check `> 0` without a try/catch.
		if (m->in.msg.getStatus() == 0xf) {
			lua_pushinteger(L, -1);
			return 1;
		}
		lua_pushinteger(L, m->in.msg.getChannel() + 1);
		return 1;
	}

	static int lua_midi_getLength(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getSize());
		return 1;
	}

	static int lua_midi_getNote(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getNote());
		return 1;
	}

	static int lua_midi_getPitchWheel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint16_t value = (static_cast<uint16_t>(m->in.msg.getValue()) << 7) | m->in.msg.getNote();
		lua_pushinteger(L, value);
		return 1;
	}

	static int lua_midi_getProgramChange(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getNote());
		return 1;
	}

	static int lua_midi_getSysEx(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 1; i < m->in.msg.getSize() - 1; i++) {
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(m->in.msg.bytes[i]);
		}
		std::string s = ss.str();
		lua_pushlstring(L, s.c_str(), s.size());
		return 1;
	}

	static int lua_midi_getSysExLength(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		// Payload length only — f0/f7 framing excluded.
		lua_pushinteger(L, std::max(0, m->in.msg.getSize() - 2));
		return 1;
	}

	static int lua_midi_getRaw(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 0; i < m->in.msg.getSize(); i++) {
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(m->in.msg.bytes[i]);
		}
		std::string s = ss.str();
		lua_pushlstring(L, s.c_str(), s.size());
		return 1;
	}

	// Type-aware, like StoermelderPackOne::MessageEx::getValue(): the combined
	// 0-16383 quantity on an assembled NRPN/RPN/14-bit CC, the raw 7-bit data
	// byte on everything else. Assembled messages are new, so no existing script
	// can be relying on the old answer for one.
	static int lua_midi_getValue(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		if (isAssembled(m)) lua_pushinteger(L, m->in.extraValue);
		else lua_pushinteger(L, m->in.msg.getValue());
		return 1;
	}

	// Which controller/parameter the message addresses: the controller number of
	// a plain CC, the MSB controller of a 14-bit CC, the parameter number of an
	// NRPN/RPN, or -1 for anything that addresses none (notes, clock, ...).
	// Answers for plain CCs too, so scripts have one spelling for "which knob
	// moved" regardless of how the device encodes it.
	static int lua_midi_getControl(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		if (isAssembled(m)) lua_pushinteger(L, m->in.paramNumber);
		else if (m->in.msg.getStatus() == 0xb) lua_pushinteger(L, m->in.msg.getNote());
		else lua_pushinteger(L, -1);
		return 1;
	}

	// True when the message carries a decode result from MidiProcessor, i.e. it
	// arrived assembled rather than as a raw CC.
	static bool isAssembled(const MessageEx* m) {
		switch (m->in.type) {
			case StoermelderPackOne::MessageEx::Type::NRPN:
			case StoermelderPackOne::MessageEx::Type::RPN:
			case StoermelderPackOne::MessageEx::Type::CC_14BIT:
				return true;
			default:
				return false;
		}
	}

	static int lua_midi_isNrpn(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.type == StoermelderPackOne::MessageEx::Type::NRPN);
		return 1;
	}
	static int lua_midi_isRpn(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.type == StoermelderPackOne::MessageEx::Type::RPN);
		return 1;
	}
	static int lua_midi_isCc14bit(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.type == StoermelderPackOne::MessageEx::Type::CC_14BIT);
		return 1;
	}

	// is-type helpers
	static int lua_midi_isCc(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xb);
		return 1;
	}
	static int lua_midi_isChanPressure(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xd);
		return 1;
	}
	static int lua_midi_isClock(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0x8);
		return 1;
	}
	static int lua_midi_isContinue(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0xb);
		return 1;
	}
	static int lua_midi_isKeyPressure(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xa);
		return 1;
	}
	static int lua_midi_isNoteOff(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0x8);
		return 1;
	}
	static int lua_midi_isNoteOn(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0x9);
		return 1;
	}
	static int lua_midi_isPitchWheel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xe);
		return 1;
	}
	static int lua_midi_isProgramChange(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xc);
		return 1;
	}
	static int lua_midi_isStart(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0xa);
		return 1;
	}
	static int lua_midi_isStop(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0xc);
		return 1;
	}
	static int lua_midi_isSysEx(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0x0);
		return 1;
	}

	// set-type helpers

	static int lua_midi_setCc(lua_State* L) {
		// midi.setCc(msg, channel, cc, value)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t cc = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t value = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_checkinteger(L, 4)))));
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0xb);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(cc);
		m->in.msg.setValue(value);
		return 0;
	}

	static int lua_midi_setCc14bit(lua_State* L) {
		auto* e = getEngine(L);

		if (lua_gettop(L) == 4) {
			// midi.setCc14bit(msg, channel, cc, value) — msg is the first
			// handle of a createCc14bit() pair; both CCs are filled and sent
			// atomically when the pair is flushed.
			MessageEx* m1 = getMsg(L, 1);
			if (!m1->isCc14bit) luaL_argerror(L, 1, "message is not a 14-bit CC pair");
			MessageEx* m2 = &e->msgStore[static_cast<size_t>(m1 - e->msgStore) + 1];
			uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
			uint8_t cc = static_cast<uint8_t>(luaL_checkinteger(L, 3));
			double value = luaL_checknumber(L, 4);
			if (m1->in.msg.getSize() != 3) m1->in.msg.setSize(3);
			if (m2->in.msg.getSize() != 3) m2->in.msg.setSize(3);
			m1->in.msg.setStatus(0xb); m2->in.msg.setStatus(0xb);
			m1->in.msg.setChannel(ch - 1);
			m2->in.msg.setChannel(ch - 1);
			m1->in.msg.setNote(cc);
			m2->in.msg.setNote(cc + 32);
			m1->in.msg.setValue(static_cast<int8_t>(value));
			m2->in.msg.setValue(static_cast<int8_t>((value - static_cast<int8_t>(value)) * 128.f));
			return 0;
		}

		// midi.setCc14bit(msg1, msg2, channel, cc, value) — two independent
		// handles, sent as separate messages (no atomicity).
		MessageEx* m1 = getMsg(L, 1);
		int idx2 = static_cast<int>(luaL_checkinteger(L, 2));
		if (idx2 < 0 || static_cast<size_t>(idx2) >= e->msgCount) luaL_argerror(L, 2, "invalid msg2 index");
		MessageEx* m2 = &e->msgStore[idx2];
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 3)))));
		uint8_t cc = static_cast<uint8_t>(luaL_checkinteger(L, 4));
		double value = luaL_checknumber(L, 5);
		if (m1->in.msg.getSize() != 3) m1->in.msg.setSize(3);
		if (m2->in.msg.getSize() != 3) m2->in.msg.setSize(3);
		m1->in.msg.setStatus(0xb); m2->in.msg.setStatus(0xb);
		m1->in.msg.setChannel(ch - 1);
		m2->in.msg.setChannel(ch - 1);
		m1->in.msg.setNote(cc);
		m2->in.msg.setNote(cc + 32);
		m1->in.msg.setValue(static_cast<int8_t>(value));
		m2->in.msg.setValue(static_cast<int8_t>((value - static_cast<int8_t>(value)) * 128.f));
		return 0;
	}

	static int lua_midi_setChannel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		m->in.msg.setChannel(ch - 1);
		return 0;
	}

	static int lua_midi_setChanPressure(lua_State* L) {
		// midi.setChanPressure(msg, channel, value)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t val = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		// Channel pressure is a 2-byte message (status + pressure), not 3 —
		// the pressure lives in bytes[1], read back via getChanPressure/getNote.
		if (m->in.msg.getSize() != 2) m->in.msg.setSize(2);
		m->in.msg.setStatus(0xd);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(val);
		return 0;
	}

	static int lua_midi_setKeyPressure(lua_State* L) {
		// midi.setKeyPressure(msg, channel, note, velocity)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t note = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t vel = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_checkinteger(L, 4)))));
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0xa);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(note);
		m->in.msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNote(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint8_t value = static_cast<uint8_t>(luaL_checkinteger(L, 2));
		m->in.msg.setNote(value);
		return 0;
	}

	static int lua_midi_setNoteOff(lua_State* L) {
		// midi.setNoteOff(msg, channel, note [, velocity])
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t note = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t vel = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_optinteger(L, 4, 0)))));
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0x8);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(note);
		m->in.msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNoteOn(lua_State* L) {
		// midi.setNoteOn(msg, channel, note, velocity)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t note = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t vel = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_checkinteger(L, 4)))));
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0x9);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(note);
		m->in.msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNrpn(lua_State* L) {
		// midi.setNRPN(nrpn, channel, number, value)
		auto* e = getEngine(L);
		int idx = static_cast<int>(luaL_checkinteger(L, 1));
		if (idx < 0 || static_cast<size_t>(idx) >= e->msgCount) luaL_argerror(L, 1, "invalid nrpn index");
		MessageEx* s1 = &e->msgStore[idx];
		if (!s1->isNrpn) luaL_argerror(L, 1, "message is not an NRPN");
		MessageEx* s2 = &e->msgStore[idx + 1];
		MessageEx* s3 = &e->msgStore[idx + 2];
		MessageEx* s4 = &e->msgStore[idx + 3];

		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint16_t number = static_cast<uint16_t>(luaL_checkinteger(L, 3));
		uint16_t value = static_cast<uint16_t>(luaL_checkinteger(L, 4));

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
		return 0;
	}

	static int lua_midi_setPitchWheel(lua_State* L) {
		// midi.setPitchWheel(msg, channel, value)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint16_t value = static_cast<uint16_t>(luaL_checkinteger(L, 3));
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0xe);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(value & 0x7f);
		m->in.msg.setValue((value >> 7) & 0x7f);
		return 0;
	}

	static int lua_midi_setProgramChange(lua_State* L) {
		// midi.setProgramChange(msg, channel, program)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t prg = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0xc);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(prg);
		return 0;
	}

	static int lua_midi_setRaw(lua_State* L) {
		// midi.setRaw(msg, hexstring)
		MessageEx* m = getMsg(L, 1);
		size_t len;
		const char* raw = luaL_checklstring(L, 2, &len);
		std::string data(raw, len);
		if (data.length() % 2 != 0) {
			luaL_error(L, "midi.setRaw: hex string length must be even");
		}
		if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
			luaL_error(L, "midi.setRaw: invalid hex string");
		}
		m->in.msg.setSize(static_cast<int>(data.length() / 2));
		for (size_t i = 0; i < data.length(); i += 2) {
			char byte = static_cast<char>(strtol(data.substr(i, 2).c_str(), nullptr, 16));
			m->in.msg.bytes[i / 2] = byte;
		}
		return 0;
	}

	static int lua_midi_setSysEx(lua_State* L) {
		// midi.setSysEx(msg, hexstring)
		MessageEx* m = getMsg(L, 1);
		size_t len;
		const char* raw = luaL_checklstring(L, 2, &len);
		std::string data(raw, len);
		if (data.length() % 2 != 0) {
			luaL_error(L, "midi.setSysEx: hex string length must be even");
		}
		if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
			luaL_error(L, "midi.setSysEx: invalid hex string");
		}
		if (data.length() / 2 > static_cast<size_t>(MidiScriptEngine::sysExMaxPayloadLength)) {
			luaL_error(L, "midi.setSysEx: payload exceeds maximum of %d bytes", MidiScriptEngine::sysExMaxPayloadLength);
		}
		for (size_t i = 0; i < data.length(); i += 2) {
			uint8_t byte = static_cast<uint8_t>(strtol(data.substr(i, 2).c_str(), nullptr, 16));
			if (byte > 0x7f) {
				luaL_error(L, "midi.setSysEx: payload bytes must be 7-bit (00-7f)");
			}
		}
		m->in.msg.setSize(static_cast<int>(data.length() / 2 + 2));
		m->in.msg.bytes[0] = 0xf0;
		for (size_t i = 0; i < data.length(); i += 2) {
			char byte = static_cast<char>(strtol(data.substr(i, 2).c_str(), nullptr, 16));
			m->in.msg.bytes[i / 2 + 1] = byte;
		}
		m->in.msg.bytes[m->in.msg.getSize() - 1] = 0xf7;
		return 0;
	}

	static int lua_midi_setValue(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint8_t value = static_cast<uint8_t>(luaL_checkinteger(L, 2));
		m->in.msg.setValue(value);
		return 0;
	}

	// ── midiOut.* ─────────────────────────────────────────────────────────────
	// Output port is set via midiOut.selectPort(n) and applied to every message
	// sent until selectPort() is called again.

	// Returns the message at stack position 1, stamped with the selected output
	// port, or luaL_error on bad args.
	static MessageEx* getPortMsg(lua_State* L) {
		auto* e = getEngine(L);
		int idx = static_cast<int>(luaL_checkinteger(L, 1));
		if (idx < 0 || static_cast<size_t>(idx) >= e->msgCount) {
			luaL_error(L, "midiOut: invalid message index");
		}

		e->msgStore[idx].midiPort = e->selectedPort;
		return &e->msgStore[idx];
	}

	static int lua_midiOut_send(lua_State* L) {
		// midiOut.send(msg)
		MessageEx* m = getPortMsg(L);
		m->send = true;
		m->sendOrder = getEngine(L)->sendCounter++;
		m->in.msg.frame = -1;
		m->tick = 0;
		return 0;
	}

	static int lua_midiOut_sendAfterMs(lua_State* L) {
		// midiOut.sendAfterMs(msg, ms)
		float ms = static_cast<float>(luaL_checknumber(L, 2));

		MessageEx* m = getPortMsg(L);
		int64_t currentFrame = APP->engine->getFrame();
		int64_t frame = static_cast<int64_t>(ms / 1000.f / APP->engine->getSampleTime());
		m->send = true;
		m->sendOrder = getEngine(L)->sendCounter++;
		m->in.msg.frame = currentFrame + frame;
		m->tick = 0;
		return 0;
	}

	static int lua_midiOut_sendAfterTrigger(lua_State* L) {
		// midiOut.sendAfterTrigger(msg, ticks, [trigPort], [channel])
		//   2 args: msg, ticks                     (trig port 1, channel 1)
		//   3 args: msg, ticks, trigPort
		//   4 args: msg, ticks, trigPort, channel

		auto* e = getEngine(L);
		int n = lua_gettop(L);

		int ticks = static_cast<int>(luaL_checkinteger(L, 2));
		int trigPort = 1;
		int channel = 1;

		if (n >= 3) trigPort = static_cast<int>(luaL_checkinteger(L, 3));
		if (n >= 4) channel = static_cast<int>(luaL_checkinteger(L, 4));

		if (trigPort < 1 || trigPort > e->inputTrigCount) {
			luaL_error(L, "midiOut.sendAfterTrigger: invalid trig port index");
		}
		if (channel < 1 || channel > PORT_MAX_CHANNELS) {
			luaL_argerror(L, 4, "channel out of range");
		}

		MessageEx* m = getPortMsg(L);
		int64_t currentTicks = e->handler->getTrigTicks(trigPort - 1, channel - 1);
		m->channel = (uint8_t)(channel - 1);
		m->send = true;
		m->sendOrder = e->sendCounter++;
		m->in.msg.frame = -1;
		m->tick = currentTicks + ticks;
		return 0;
	}

	// Shared by midi.enableNrpnIn() and midi.enableRpnIn(): both take the same
	// arguments and differ only in which kind they arm.
	static int luaEnableParamIn(lua_State* L, int kind, const char* name) {
		// midi.enableNrpnIn(midiPort [, channel]) / midi.enableRpnIn(...)
		//   midiPort: 1-based; channel: 1-based MIDI channel, omitted = all.
		auto* e = getEngine(L);
		int midiPort = static_cast<int>(luaL_checkinteger(L, 1));
		if (midiPort < 1 || midiPort > e->midiInputCount) {
			return luaL_error(L, "%s: midiPort out of range", name);
		}
		int channel = -1;
		if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
			channel = static_cast<int>(luaL_checkinteger(L, 2));
			if (channel < 1 || channel > 16) {
				return luaL_error(L, "%s: channel must be 1-16", name);
			}
			channel -= 1;
		}
		e->handler->enableNrpnIn(midiPort - 1, kind, channel);
		return 0;
	}

	static int lua_midi_enableNrpnIn(lua_State* L) {
		return luaEnableParamIn(L, 0, "midi.enableNrpnIn");
	}

	static int lua_midi_enableRpnIn(lua_State* L) {
		return luaEnableParamIn(L, 1, "midi.enableRpnIn");
	}

	static int lua_midi_enableCc14bitIn(lua_State* L) {
		// midi.enableCc14bitIn(midiPort [, cc] [, channel])
		//   cc: the 0-31 MSB controller (its LSB is cc + 32), omitted = all of
		//   them. channel: 1-based MIDI channel, omitted = all.
		auto* e = getEngine(L);
		int midiPort = static_cast<int>(luaL_checkinteger(L, 1));
		if (midiPort < 1 || midiPort > e->midiInputCount) {
			return luaL_error(L, "midi.enableCc14bitIn: midiPort out of range");
		}
		int cc = -1;
		if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
			cc = static_cast<int>(luaL_checkinteger(L, 2));
			// Only CC 0-31 have a defined LSB partner; rejecting the rest here is
			// a better error than silently never delivering.
			if (cc < 0 || cc > 31) {
				return luaL_error(L, "midi.enableCc14bitIn: cc must be 0-31");
			}
		}
		int channel = -1;
		if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
			channel = static_cast<int>(luaL_checkinteger(L, 3));
			if (channel < 1 || channel > 16) {
				return luaL_error(L, "midi.enableCc14bitIn: channel must be 1-16");
			}
			channel -= 1;
		}
		e->handler->enableCc14bitIn(midiPort - 1, cc, channel);
		return 0;
	}

	static int lua_trig_enableTipsyIn(lua_State* L) {
		// trig.enableTipsyIn([enabled])
		//   Optional boolean: true (the default) decodes a Tipsy stream from the
		//   trigger input, false disables it. Tipsy input is only supported on the
		//   first trigger input, so — like trig.sendTipsy() — there is no port
		//   argument.
		auto* e = getEngine(L);
		bool enabled = (lua_gettop(L) < 1) || lua_toboolean(L, 1);
		e->handler->enableTipsyIn(enabled ? 0 : -1);
		return 0;
	}

	static int lua_trig_sendTipsy(lua_State* L) {
		// trig.sendTipsy(data, [mimeType])
		//   data: string (binary data to encode)
		//   mimeType: optional string (default "text/plain")
		
		auto* e = getEngine(L);
		
		if (lua_gettop(L) < 1) {
			luaL_error(L, "trig.sendTipsy: requires data argument");
		}
		
		size_t dataLen = 0;
		const char* data = luaL_checklstring(L, 1, &dataLen);
		
		const char* mimeType = "text/plain";
		if (lua_gettop(L) >= 2) {
			mimeType = luaL_checkstring(L, 2);
		}
		
		if (!mimeType || !data) {
			luaL_error(L, "trig.sendTipsy: invalid arguments");
		}
		
		bool success = e->handler->sendTipsyOut(mimeType, reinterpret_cast<const unsigned char*>(data), static_cast<uint32_t>(dataLen));
		
		if (!success) {
			luaL_error(L, "trig.sendTipsy: failed to initiate message");
		}
		
		return 0;
	}
};

} // namespace Lua
} // namespace MidiScript
} // namespace StoermelderPackOne
