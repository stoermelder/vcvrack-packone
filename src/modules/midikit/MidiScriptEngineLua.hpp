#include "MidiScriptEngine.hpp"
extern "C" {
	#include "minilua.h"
}
#include "minilua.json.hpp"
#include "minilua.util.hpp"
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

	// Script execution budget
	// A count hook (lua_sethook, LUA_MASKCOUNT) fires every interruptInterval
	// instructions; past interruptCountLimit countHook() aborts the script via
	// luaL_error(), so a `while true do end` can't wedge the shared worker.
	// Reset by beginScriptExecution() per callback (worker-thread only).
	static const int interruptInterval = 10000;  // hook fires every 10k instructions
	static const int interruptCountLimit = 10000;   // 10k counts * 10k instr = 100M
	int interruptCount = 0;

	// Whether the count hook should budget the running code. False during
	// registerAPI()'s trusted stubs and lua_close() finalizers; true only
	// inside a user callback, set by beginScriptExecution(). Not thread_local:
	// getEngine(L) resolves the engine from the Lua registry on any thread.
	bool hookArmed = false;

	static void countHook(lua_State* L, lua_Debug* ar) {
		(void)ar;
		MidiScriptEngineLua* e = getEngine(L);
		if (!e || !e->hookArmed) return;
		if (++e->interruptCount >= interruptCountLimit) {
			luaL_error(L, "script exceeded execution budget");
		}
	}

	// Resets the budget; call before every user lua_pcall.
	void beginScriptExecution() {
		interruptCount = 0;
		hookArmed = true;
	}

	// ── Memory watchdog ──────────────────────────────────────────────────────
	// miniLua's allocator grows via realloc() with no cap — and, worse, large
	// strings are "external" (zero-copy, data allocated through the allocator
	// but invisible to lua_gc(LUA_GCCOUNT)) — so the only reliable footprint is
	// the allocator's own net byte count. Rather than fail individual
	// allocations, the engine watches that count after every user callback and,
	// once past memoryLimit, tears the state down (unloadScriptOnWorker()): the
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
			// After the close, which resets the log.
			unloadScriptOnWorker();
			handler->writeLog(string::f("Script exceeded the %d KB memory limit and was stopped", (int)(memoryLimit / 1024)));
		}
	}

	// The lifecycle hooks are resolved once at load and kept as registry refs,
	// not re-looked-up per dispatch — so defining/reassigning a hook later has
	// no effect; only what was present at load runs. LUA_NOREF = "not defined".
	// Asymmetry: Lua calls hooks as bare functions; QuickJS as methods (rackObj
	// as thisVal). onLoad/onUnload live on the rack table; onMessage on
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
	// rack.onBroadcast. Defined at load means the engine receives broadcasts.
	int onBroadcastRef = LUA_NOREF;
	int onLoadRef = LUA_NOREF;
	int onUnloadRef = LUA_NOREF;

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


	void loadScriptOnWorker(const char* script, const std::string& initialConfigJson) override {
		assert(onWorkerThread());
		// The caller closed the previous script (unloadScriptOnWorker()): its
		// interpreter, hooks and menu entries are gone and the module's script
		// state is reset.
		assert(L == nullptr);
		assert(onLoadRef == LUA_NOREF && onUnloadRef == LUA_NOREF && onMessageRef == LUA_NOREF && onTriggerRef == LUA_NOREF);
		assert(contextMenus.empty());

		// The store goes back to its default size first, so a script that was
		// refused (or asks for less) never inherits the previous script's.
		sizeStore(0);

		// Install the initial config as part of THIS queued task, before any
		// script code runs — ScriptHost::load() is fire-and-forget, so a separate
		// call could race top-level code that already called rack.getConfig().
		// Empty means "fresh, empty config" (script switch), not "leave
		// whatever was there" — config belongs to the script that wrote it.
		json_t* initial = initialConfigJson.empty() ? nullptr : json_loads(initialConfigJson.c_str(), 0, NULL);
		installConfig(initial);

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

		if (!checkRequires(topics)) return;

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

		// luaopen_base/string still register functions that read files or accept
		// precompiled bytecode, which the VM doesn't verify. A script travels inside
		// the patch file, so remove them. load goes too: it defaults to mode "bt".
		static const char* const removedGlobals[] = { "dofile", "loadfile", "load" };
		for (const char* name : removedGlobals) {
			lua_pushnil(L);
			lua_setglobal(L, name);
		}
		lua_getglobal(L, "string");
		lua_pushnil(L);
		lua_setfield(L, -2, "dump");
		lua_pop(L, 1);

		// ── Script execution budget ─────────────────────────────────────────
		// Install the count hook. hookArmed stays false until the first
		// beginScriptExecution(), so registerAPI()'s trusted stubs aren't budgeted.
		hookArmed = false;
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
			std::string message = string::f("Error loading script: %s", err ? err : "(unknown)");
			lua_pop(L, 1);
			// Logged after the close, which resets the log.
			unloadScriptOnWorker();
			handler->writeLog(message, false);
			return;
		}

		handler->writeLog("Script loaded", false);

		// Cache the lifecycle hooks once (see declarations): onLoad/onUnload
		// from rack; onMessage from midi; onTrigger/onTipsyMessage from trig.
		// rack.getConfig/setConfig are NOT hooks — they are live calls, bound
		// once in registerAPI() above like every other rack.* function.
		lua_getglobal(L, "rack");
		if (lua_istable(L, -1)) {
			onLoadRef = cacheHookRef("onLoad");
			onUnloadRef = cacheHookRef("onUnload");
			onBroadcastRef = cacheHookRef("onBroadcast");
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

		if (onMessageRef == LUA_NOREF) {
			handler->writeLog("No midi.onMessage(midiPort, msg) function defined — incoming MIDI is ignored", false);
		}

		// Before onLoad(), so broadcasts sent from other modules' onLoad() during the
		// same patch load are not lost.
		if (onBroadcastRef != LUA_NOREF && domain) {
			domain->bus->join(this);
		}

		callOnLoad();
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

	// Tears down the Lua state. See MidiScriptEngine::unloadScriptOnWorker().
	void unloadScriptOnWorker() override {
		assert(onWorkerThread());
		if (L) {
			// From here on only immediate MIDI gets out (see beginUnload()).
			handler->beginUnload();
			callOnUnload();
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
			onBroadcastRef = LUA_NOREF;
			// Disarm the hook first: lua_close runs finalizers with no pcall
			// boundary, where a hook luaL_error would longjmp nowhere.
			hookArmed = false;
			lua_close(L);
			L = nullptr;
			// Keeps "no state ⇒ zero bytes" true unconditionally, rather than
			// relying on every allocatedBytes reader to guard on L itself.
			allocatedBytes.store(0, std::memory_order_relaxed);
		}
		if (domain) domain->bus->leave(this);
		discardInQueues();
		handler->endUnload();
	}

	// Runs the script's onLoad() hook. No argument: config is restored via
	// rack.getConfig() (installed into workingConfig before load), not passed
	// as a hook parameter. Uses onLoadRef.
	void callOnLoad() {
		if (onLoadRef == LUA_NOREF) return;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onLoadRef);
		beginStore(0);
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 0, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onLoad error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}
	}

	// Runs onUnload(). Its return value is discarded — teardown-only; config
	// comes from rack.setConfig(), not from teardown. Messages it sends (e.g.
	// all-notes-off) go out as it calls them, between the caller's
	// beginUnload() and endUnload().
	void callOnUnload() {
		// Start with an empty store like every callback: handles from the last
		// callback are invalid here.
		beginStore(0);
		if (onUnloadRef == LUA_NOREF) return;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onUnloadRef);
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 0, 1, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onUnload error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
			return;
		}
		lua_pop(L, 1); // pop (and discard) the return value
	}

	// ── Config JSON helpers ──────────────────────────────────────────────────
	// MiniLua has no JSON library, so rack.getConfig()/setConfig() values
	// convert to/from JSON via jansson. Lua tables serialize as arrays when
	// keys are exactly 1..n, otherwise as objects.

	// Recursively converts a Lua value at idx into a jansson json_t*. Returns
	// NULL for unsupported types (including a value past configMaxDepth) or a
	// depth-first walk that overflowed the JSON size cap once serialized.
	// Leaves the Lua stack untouched.
	//
	// depth counts from 1 (the value passed to setConfig() itself); the depth
	// guard is what makes a self-referencing table terminate instead of
	// recursing until the C stack is exhausted — previously reachable from
	// script code via this exact path.
	static json_t* luaValueToJson(lua_State* L, int idx, int depth = 1) {
		if (depth > MidiScriptEngine::configMaxDepth) return NULL;
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
			case LUA_TTABLE: return luaTableToJsonValue(L, idx, depth);
			default: return NULL;
		}
	}

	// Converts a Lua table into a jansson value, detecting whether it is an
	// array (integer keys exactly 1..n) or an object. `depth` is the depth of
	// THIS table (matching the value depth luaValueToJson was called with);
	// elements one level deeper are converted at depth + 1.
	static json_t* luaTableToJsonValue(lua_State* L, int idx, int depth) {
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
				json_t* val = luaValueToJson(L, -1, depth + 1);
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
				json_t* val = luaValueToJson(L, -1, depth + 1);
				if (!val) {
					// An unsupported/too-deep/cyclic nested value rejects the
					// WHOLE table, not just this key — silently dropping the
					// key would let a cyclic table sail through setConfig()
					// with a hole where the cycle was, which is not what
					// "rejected" means. lua_next() needs the key on the stack
					// to keep iterating, but this table is being abandoned,
					// so pop both key and value and stop.
					lua_pop(L, 2);
					json_decref(obj);
					return NULL;
				}
				if (!key.empty()) {
					json_object_set_new(obj, key.c_str(), val);
				}
				else {
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
			midiInQueue.tryPush(midiPort, msg);
		}
	}

	void processInTick(int trigPort, uint8_t channel, int64_t frame) override {
		if (L) {
			pushInQueue(tickInQueue, std::make_tuple(trigPort, channel, frame));
		}
	}

	std::string getInputName(int i) override {
		if (!L) return "";
		return callLuaTableFunc("input", "onTooltip", i + 1);
	}

	std::string getParamName(int i) override {
		if (!L) return "";
		return callLuaTableFunc("param", "onTooltip", i + 1);
	}

	std::string getParamFormatValue(int i) override {
		if (!L) return "";
		return callLuaTableFunc("param", "onValueText", i + 1);
	}

	// Releases the stored script callbacks. Called only from
	// unloadScriptOnWorker(), so like every other toucher of contextMenus this
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
		// A UI query: runs behind MIDI dispatch (see runLowPriority).
		runLowPriority([this, callback]() {
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
	void invokeContextMenuCallback(int callbackId, const ScriptMenuClick& click) override {
		// The whole body runs on the worker thread, incl. the spec lookup:
		// contextMenus is worker-owned, so no lock. The caller ignores timing,
		// so the read needn't be synchronous on the UI thread.
		runAsync([this, callbackId, click]() {
			assert(onWorkerThread());
			if (!L) return;
			auto it = contextMenus.find(callbackId);
			if (it == contextMenus.end()) return;
			std::vector<ScriptMenuArg> args;
			if (!menuCallArgs(it->second.spec, click, args)) return;
			lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.callbackRef);
			for (const ScriptMenuArg& a : args) {
				switch (a.kind) {
					case ScriptMenuArg::Kind::Bool: lua_pushboolean(L, a.b); break;
					case ScriptMenuArg::Kind::Int: lua_pushinteger(L, a.i); break;
					case ScriptMenuArg::Kind::String: lua_pushlstring(L, a.s.data(), a.s.size()); break;
				}
			}
			// A callback like onLoad: MIDI it sends goes out as it calls midiOut.*.
			beginStore(0);
			inCallback = true;
			beginScriptExecution();
			int status = lua_pcall(L, static_cast<int>(args.size()), 0, 0);
			inCallback = false;
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
	bool getMemoryUsage(size_t& used, size_t& total) override {
		if (!L) return false;
		used = allocatedBytes.load(std::memory_order_relaxed);
		total = memoryLimit;
		return true;
	}


	void dispatchMidiMessage(int midiPort, Message& msg) override {
		if (!L) return;

		storeIncoming(QueuedMessage(msg));

		// Calls the cached onMessageRef. No-op if never defined (LUA_NOREF).
		if (onMessageRef == LUA_NOREF) return;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onMessageRef);
		lua_pushinteger(L, midiPort + 1);
		lua_pushinteger(L, static_cast<lua_Integer>(slotToHandle(0)));
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onMessage error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

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
		beginStore(0);
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onTrigger error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		checkMemoryLimit();
	}

	// Dispatches an assembled message to onNrpn/onRpn/onCc14bit as a handle,
	// exactly like dispatchMidiMessage() does for onMessage: the message lands in
	// store slot 0 and the callback receives (midiPort, handle), reading it through
	// midi.getControl()/getValue()/getChannel(). No-op if the hook was never
	// defined.
	void dispatchAssembled(int ref, const char* name, int midiPort, const QueuedMessage& q) {
		if (!L) return;
		if (ref == LUA_NOREF) return;

		storeIncoming(q);

		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		lua_pushinteger(L, midiPort + 1);
		lua_pushinteger(L, static_cast<lua_Integer>(slotToHandle(0)));   // the message just stored
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("%s error: %s", name, err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

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
	// Dispatches onBroadcast(value, topic) via the cached onBroadcastRef. The value
	// is converted into a fresh Lua value, never shared with the sender. The topic
	// is nil when the sender gave none.
	void dispatchBroadcast(const MidiScript::InboundBroadcast& msg) override {
		if (!L) return;
		if (onBroadcastRef == LUA_NOREF) return;

		lua_rawgeti(L, LUA_REGISTRYINDEX, onBroadcastRef);
		if (!pushJsonAsLua(L, msg.value.get())) {
			lua_pop(L, 1); // pop the function
			handler->writeLog("onBroadcast: value could not be converted (ignored)");
			return;
		}
		if (msg.hasTopic) lua_pushlstring(L, msg.topic.data(), msg.topic.size());
		else lua_pushnil(L);
		beginStore(0);
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onBroadcast error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		checkMemoryLimit();
	}

	void dispatchTipsyMessage(const MidiScript::TipsyMessage& msg) override {
		if (!L) return;
		if (onTipsyMessageRef == LUA_NOREF) return;

		lua_rawgeti(L, LUA_REGISTRYINDEX, onTipsyMessageRef);
		lua_pushlstring(L, reinterpret_cast<const char*>(msg.data), msg.dataSize);
		lua_pushstring(L, msg.mime);
		beginStore(0);
		inCallback = true;
		beginScriptExecution();
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onTipsyMessage error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

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

	// The store slot of the message handle at `stackPos`, or luaL_argerror for
	// anything that is not a live handle of the running callback: a handle
	// from an earlier callback, one used outside a callback (top level,
	// param.onTooltip, ...), out of range.
	static size_t checkHandle(lua_State* L, int stackPos, const char* what = "invalid message index") {
		auto* e = getEngine(L);
		if (!lua_isinteger(L, stackPos) && !lua_isnumber(L, stackPos)) {
			luaL_argerror(L, stackPos, "message index expected");
		}
		long slot = e->handleToSlot(static_cast<int64_t>(lua_tointeger(L, stackPos)));
		if (slot < 0) luaL_argerror(L, stackPos, what);
		return static_cast<size_t>(slot);
	}

	// Validate a message handle (stack arg at `stackPos`) and return its slot.
	static ScriptMessage* getMsg(lua_State* L, int stackPos) {
		return &getEngine(L)->msgStore[checkHandle(L, stackPos)];
	}

	// Raises "<fn>: message store full (N handles; ...)". The text is formatted
	// into a plain buffer: luaL_error longjmps past C++ destructors.
	static void luaStoreFull(lua_State* L, const char* fn) {
		char buf[192];
		getEngine(L)->storeFullMessage(buf, sizeof buf, fn);
		luaL_error(L, "%s", buf);
	}

	// A message handle is only valid inside the callback that created it, so
	// the creators refuse to run anywhere else (top level, param.onTooltip, ...).
	// Raised before the store is touched.
	static void requireCallback(lua_State* L, const char* fn) {
		if (!getEngine(L)->inCallback) luaL_error(L, "%s: only allowed inside a callback", fn);
	}


	void registerAPI() {
		// ── rack table ──────────────────────────────────────────────────────
		lua_newtable(L);
		setTableFunc("log",      lua_rack_log);
		setTableFunc("overlay",  lua_rack_overlay);
		setTableFunc("getEventFrame", lua_rack_getEventFrame);
		setTableFunc("msToFrames",    lua_rack_msToFrames);
		setTableFunc("framesToMs",    lua_rack_framesToMs);
		setTableFunc("random",   lua_rack_random);
		setTableFunc("setRandomSeed", lua_rack_setRandomSeed);
		setTableFunc("registerContextMenu", lua_rack_registerContextMenu);
		setTableFunc("unregisterContextMenu", lua_rack_unregisterContextMenu);
		setTableFunc("getConfig", lua_rack_getConfig);
		setTableFunc("setConfig", lua_rack_setConfig);
		setTableFunc("sendBroadcast", lua_rack_sendBroadcast);
		lua_setglobal(L, "rack");

		// ── number table ─────────────────────────────────────────────────────
		// Mostly wraps existing Lua math.*; provided for JS script compatibility.
		lua_newtable(L);
		setTableFunc("crossfade", lua_number_crossfade);
		setTableFunc("rescale",   lua_number_rescale);
		setTableFunc("toString",  lua_number_toString);
		lua_setglobal(L, "number");

		// ── string.split ─────────────────────────────────────────────────────
		// Modelled on JavaScript's str.split(sep, limit): sep is a plain string, not
		// a pattern; empty fields are kept ("a,,b" gives three); an empty sep gives
		// the single bytes; no sep gives the whole string. Also callable as s:split(sep).
		if (luaL_dostring(L, LUA_STRING_SPLIT_SOURCE) != LUA_OK) {
			handler->writeLog("Error loading string.split", false);
			lua_pop(L, 1);
		}

		// ── json table ───────────────────────────────────────────────────────
		// Pure-Lua library (json.encode/json.decode); it returns its table.
		if (luaL_loadbuffer(L, LUA_JSON_SOURCE, sizeof(LUA_JSON_SOURCE) - 1, "json") == LUA_OK
				&& lua_pcall(L, 0, 1, 0) == LUA_OK) {
			lua_setglobal(L, "json");
		}
		else {
			handler->writeLog("Error loading json library", false);
			lua_pop(L, 1);
		}

		// ── input table ──────────────────────────────────────────────────────
		// Default onTooltip provided in Lua; scripts may override.
		luaL_dostring(L,
			"input = {\n"
			"    onTooltip = function(i) return 'Port ' .. i end\n"
			"}\n"
		);
		lua_getglobal(L, "input");
		setTableFunc("enable",     lua_input_enable);
		setTableFunc("getVoltage", lua_input_getVoltage);
		setTableFunc("isHigh",     lua_input_isHigh);
		setTableFunc("isLow",      lua_input_isLow);
		setTableInt("count",       inputCount);
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
		setTableInt("inCount",      inputTrigCount);
		setTableInt("outCount",     outputTrigCount);
		lua_setglobal(L, "trig");

		// ── param table ──────────────────────────────────────────────────────
		luaL_dostring(L,
			"param = {\n"
			"    onTooltip   = function(i) return 'Param ' .. i end,\n"
			"    onValueText = function(i) return '' end\n"
			"}\n"
		);
		lua_getglobal(L, "param");
		setTableFunc("enable",   lua_param_enable);
		setTableFunc("getValue", lua_param_getValue);
		setTableInt("count",     paramCount);
		lua_pop(L, 1);

		// ── midi table ───────────────────────────────────────────────────────
		lua_newtable(L);
		setTableFunc("create",          lua_midi_create);
		setTableFunc("clone",           lua_midi_clone);
		setTableFunc("createNRPN",      lua_midi_createNrpn);
		setTableFunc("createRPN",       lua_midi_createRpn);
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
		setTableFunc("setRPN",          lua_midi_setRpn);
		setTableFunc("setPitchWheel",   lua_midi_setPitchWheel);
		setTableFunc("setProgramChange",lua_midi_setProgramChange);
		setTableFunc("setRaw",          lua_midi_setRaw);
		setTableFunc("setSysEx",        lua_midi_setSysEx);
		setTableFunc("setValue",        lua_midi_setValue);
		setTableFunc("enableNrpnIn",    lua_midi_enableNrpnIn);
		setTableFunc("enableRpnIn",     lua_midi_enableRpnIn);
		setTableFunc("enableCc14bitIn", lua_midi_enableCc14bitIn);
		setTableFunc("enablePorts",     lua_midi_enablePorts);
		setTableInt("portCount",        midiInputCount);
		lua_setglobal(L, "midi");

		// ── midiOut table ────────────────────────────────────────────────────
		lua_newtable(L);
		setTableFunc("enablePorts",        lua_midiOut_enablePorts);
		setTableFunc("enableTiming",       lua_midiOut_enableTiming);
		setTableFunc("selectPort",         lua_midiOut_selectPort);
		setTableFunc("send",               lua_midiOut_send);
		setTableFunc("sendAfterMs",        lua_midiOut_sendAfterMs);
		setTableFunc("sendAtFrame",        lua_midiOut_sendAtFrame);
		setTableFunc("sendAfterTrigger",   lua_midiOut_sendAfterTrigger);
		setTableFunc("cancel",             lua_midiOut_cancel);
		setTableInt("portCount",           midiOutputCount);
		lua_setglobal(L, "midiOut");
	}

	// Helper: set an integer field on the table currently at the top of the stack
	void setTableInt(const char* name, int value) {
		lua_pushinteger(L, value);
		lua_setfield(L, -2, name);
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
					formatNumber(lua_tonumber(L, i), buf, sizeof(buf));
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

	// rack.registerContextMenu(options) — registers one item in the module's
	// context menu:
	//   { type = "boolean", label, onGetValue = fn() -> bool, onChange = fn(checked) }
	//   { type = "options", label, options = {...}, onGetValue = fn() -> int, onChange = fn(idx, label) }
	// onGetValue is optional (defaults to 0) and evaluated lazily on the worker
	// thread when the menu is built, so it always reflects the live config.
	// Callbacks are stored as registry refs and fired on the worker thread.
	static int lua_rack_registerContextMenu(lua_State* L) {
		auto* e = getEngine(L);
		if (lua_gettop(L) < 1 || !lua_istable(L, 1)) {
			return luaL_error(L, "registerContextMenu: expected a table");
		}

		ScriptMenuItem spec;

		lua_getfield(L, 1, "type");
		if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "registerContextMenu: type must be a string");
		std::string type = lua_tostring(L, -1);
		lua_pop(L, 1);
		if (type == "options") spec.type = ScriptMenuItem::Type::Options;
		else if (type == "boolean") spec.type = ScriptMenuItem::Type::Boolean;
		else if (type == "action") spec.type = ScriptMenuItem::Type::Action;
		else if (type == "file") spec.type = ScriptMenuItem::Type::File;
		else return luaL_error(L, "registerContextMenu: type must be \"boolean\", \"options\", \"action\" or \"file\"");

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
		// Only boolean and options items have a value to show.
		bool hasValue = spec.type == ScriptMenuItem::Type::Boolean || spec.type == ScriptMenuItem::Type::Options;
		if (hasValue && lua_isfunction(L, -1)) {
			onGetValueRef = luaL_ref(L, LUA_REGISTRYINDEX); // pops onGetValue
		}
		else {
			lua_pop(L, 1); // ignore non-function onGetValue
		}

		assert(e->onWorkerThread());
		int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops the onChange function

		// Registering a label that is already there replaces that item in place
		// (same position, same callback id) instead of adding a second one - this
		// is how a script updates the options of a menu at runtime. The old refs
		// are released; a callback that is running right now has its function on
		// the Lua stack (see invokeContextMenuCallback()), so it stays alive.
		for (auto& kv : e->contextMenus) {
			if (kv.second.spec.label != spec.label) continue;
			luaL_unref(L, LUA_REGISTRYINDEX, kv.second.callbackRef);
			if (kv.second.onGetValueRef != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, kv.second.onGetValueRef);
			}
			spec.callbackId = kv.first;
			kv.second.spec = spec;
			kv.second.callbackRef = ref;
			kv.second.onGetValueRef = onGetValueRef;
			lua_pushboolean(L, 1);
			return 1;
		}

		spec.callbackId = e->nextContextMenuCallbackId++;
		ContextMenuEntry entry;
		entry.spec = spec;
		entry.callbackRef = ref;
		entry.onGetValueRef = onGetValueRef;
		e->contextMenus[spec.callbackId] = entry;

		lua_pushboolean(L, 1);
		return 1;
	}

	// rack.unregisterContextMenu(label) — removes the item registered under
	// `label`. Returns true if there was one, false otherwise. The item's refs
	// are released; a callback that is running right now has its function on the
	// Lua stack (see invokeContextMenuCallback()), so it stays alive.
	static int lua_rack_unregisterContextMenu(lua_State* L) {
		auto* e = getEngine(L);
		if (lua_gettop(L) < 1 || lua_type(L, 1) != LUA_TSTRING) {
			return luaL_error(L, "unregisterContextMenu: label must be a string");
		}
		size_t len;
		const char* s = lua_tolstring(L, 1, &len);
		std::string label(s, len);

		assert(e->onWorkerThread());
		for (auto it = e->contextMenus.begin(); it != e->contextMenus.end(); ++it) {
			if (it->second.spec.label != label) continue;
			luaL_unref(L, LUA_REGISTRYINDEX, it->second.callbackRef);
			if (it->second.onGetValueRef != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, it->second.onGetValueRef);
			}
			e->contextMenus.erase(it);
			lua_pushboolean(L, 1);
			return 1;
		}
		lua_pushboolean(L, 0);
		return 1;
	}

	// rack.getConfig(key [, default]) — reads a value previously written with
	// rack.setConfig(), or `default` (nil if omitted) when unset. A malformed
	// key is rejected the same way as setConfig(): logged, and treated as
	// unset rather than silently returning the default forever.
	//
	// Live call, not a hook: reads workingConfig directly, never
	// publishedConfig — see MidiScriptEngine::getConfigValue().
	static int lua_rack_getConfig(lua_State* L) {
		auto* e = getEngine(L);
		assert(e->onWorkerThread());
		const char* key = (lua_type(L, 1) == LUA_TSTRING) ? lua_tostring(L, 1) : nullptr;
		if (!MidiScriptEngine::isValidConfigKey(key)) {
			e->handler->writeLog(string::f("getConfig: invalid key \"%s\" (ignored)", key ? key : "(not a string)"));
			lua_pushnil(L);
			return 1;
		}
		json_t* val = e->getConfigValue(key);
		if (!val) {
			// Unset: return the default (arg 2), or nil if none was given.
			if (lua_gettop(L) >= 2) {
				lua_pushvalue(L, 2);
			}
			else {
				lua_pushnil(L);
			}
			return 1;
		}
		if (!pushJsonAsLua(L, val)) lua_pushnil(L);
		return 1;
	}

	// rack.setConfig(key, value) — persists `value` under `key`, or removes
	// the key if `value` is nil. A malformed key, an unsupported value
	// (function/userdata), one nesting past configMaxDepth, or one that would
	// push the whole config past configMaxBytes once serialized are all
	// rejected the same way: the key/config is left unchanged, one line is
	// logged, and the script keeps running.
	static int lua_rack_setConfig(lua_State* L) {
		auto* e = getEngine(L);
		assert(e->onWorkerThread());
		const char* key = (lua_type(L, 1) == LUA_TSTRING) ? lua_tostring(L, 1) : nullptr;
		if (!MidiScriptEngine::isValidConfigKey(key)) {
			e->handler->writeLog(string::f("setConfig: invalid key \"%s\" (ignored)", key ? key : "(not a string)"));
			return 0;
		}
		// nil deletes the key — luaValueToJson(nil) would otherwise yield
		// json_null(), which is a value ("stored null"), not a deletion.
		if (lua_isnoneornil(L, 2)) {
			e->setConfigValue(key, nullptr);
			return 0;
		}
		json_t* val = luaValueToJson(L, 2);
		if (!val) {
			e->handler->writeLog(string::f("setConfig: value for \"%s\" is not JSON-serializable, too deeply nested, or cyclic (ignored)", key));
			return 0;
		}
		// Enforce the total-size cap by trial: build what the config WOULD be,
		// measure it, and only commit (via setConfigValue, which republishes)
		// if it fits. json_object_set (not _new) so the trial copy doesn't
		// steal `val` before we know whether we're keeping it.
		json_t* trial = json_copy(e->workingConfig.get());
		json_object_set(trial, key, val);
		char* dump = json_dumps(trial, JSON_COMPACT);
		size_t size = dump ? strlen(dump) : 0;
		if (dump) free(dump);
		json_decref(trial);
		if (size > MidiScriptEngine::configMaxBytes) {
			e->handler->writeLog(string::f("setConfig: \"%s\" would push the config past the %d KB limit (ignored)", key, (int)(MidiScriptEngine::configMaxBytes / 1024)));
			json_decref(val);
			return 0;
		}
		e->setConfigValue(key, val); // takes ownership of val
		return 0;
	}

	// rack.sendBroadcast(value [, topic]) — sends `value`, with an optional string
	// topic, to every other module whose script defines rack.onBroadcast, and
	// returns how many it reached. Same value rules as setConfig(); a value or
	// topic that is rejected or too large is logged, returns 0 and the script
	// keeps running. No argument is a script error.
	static int lua_rack_sendBroadcast(lua_State* L) {
		auto* e = getEngine(L);
		assert(e->onWorkerThread());
		// Before any C++ object exists: luaL_error longjmps past destructors.
		if (lua_gettop(L) < 1) return luaL_error(L, "sendBroadcast: requires a value");
		std::string topic;
		bool hasTopic = false;
		if (!lua_isnoneornil(L, 2)) {
			if (lua_type(L, 2) != LUA_TSTRING) {
				e->handler->writeLog("sendBroadcast: topic must be a string (ignored)");
				lua_pushinteger(L, 0);
				return 1;
			}
			size_t len = 0;
			const char* t = lua_tolstring(L, 2, &len);
			topic.assign(t, len);
			hasTopic = true;
		}
		json_t* val = luaValueToJson(L, 1);
		if (!val) {
			e->handler->writeLog("sendBroadcast: value is not JSON-serializable, too deeply nested, or cyclic (ignored)");
			lua_pushinteger(L, 0);
			return 1;
		}
		lua_pushinteger(L, e->sendBroadcast(val, hasTopic ? &topic : nullptr)); // takes ownership of val
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

	static int lua_rack_setRandomSeed(lua_State* L) {
		if (!getEngine(L)->setRandomSeedFromNumber(luaL_checknumber(L, 1))) {
			return luaL_error(L, "rack.setRandomSeed: seed must be a finite number");
		}
		return 0;
	}

	static int lua_rack_random(lua_State* L) {
		lua_pushnumber(L, getEngine(L)->nextRandom());
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

	// Formats d like a plain number: integral values below 2^53 print exactly
	// ("%.0f", so frame counters stay exact — a float would round above 2^24),
	// other values with up to 6 decimals, trailing zeros (and a trailing '.')
	// trimmed. Very large magnitudes use "%g" so they can't overflow the buffer.
	static void formatNumber(double d, char* buf, size_t bufSize) {
		if (std::fabs(d) >= 1e15) {
			snprintf(buf, bufSize, "%g", d);
			return;
		}
		if (d == std::floor(d)) {
			snprintf(buf, bufSize, "%.0f", d);
			return;
		}
		snprintf(buf, bufSize, "%f", d);
		char* end = buf + strlen(buf) - 1;
		while (end > buf && *end == '0') { *end = '\0'; end--; }
		if (end > buf && *end == '.') { *end = '\0'; }
	}

	static int lua_number_toString(lua_State* L) {
		char buf[32];
		formatNumber(luaL_checknumber(L, 1), buf, sizeof(buf));
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
		if (n < 2 || n > 3) luaL_error(L, "trig.setGate: expected (port [,ch], duration)");
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
		e->handler->setTrig(i - 1, ch - 1, duration / 1000.f, e->frameForTrig());
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
		e->handler->setTrigVoltage(i - 1, ch - 1, 10.f, e->frameForTrig());
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
		e->handler->setTrigVoltage(i - 1, ch - 1, 0.f, e->frameForTrig());
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
		e->handler->setTrig(i - 1, ch - 1, 1e-3f, e->frameForTrig());
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
		// Optional fallback for an index above the variant's param count
		// (e.g. param 3 on MIDI-µKIT), so a script needn't check param.count.
		if (i > e->paramCount && i >= 1 && lua_gettop(L) >= 2) {
			lua_pushnumber(L, luaL_checknumber(L, 2));
			return 1;
		}
		if (i < 1 || i > e->paramCount) luaL_argerror(L, 1, "param index out of range");
		lua_pushnumber(L, e->handler->getParamValue(i - 1));
		return 1;
	}

	// ── midi.* ────────────────────────────────────────────────────────────────

	// midi.enablePorts(count) — enables MIDI inputs 1..count.
	static int lua_midi_enablePorts(lua_State* L) {
		auto* e = getEngine(L);
		int count = static_cast<int>(luaL_checkinteger(L, 1));
		if (count < 1 || count > e->midiInputCount) luaL_argerror(L, 1, "invalid input port count");
		e->handler->enableMidiIn(count);
		return 0;
	}

	// midiOut.enablePorts(count) — enables MIDI outputs 1..count.
	static int lua_midiOut_enablePorts(lua_State* L) {
		auto* e = getEngine(L);
		int count = static_cast<int>(luaL_checkinteger(L, 1));
		if (count < 1 || count > e->midiOutputCount) luaL_argerror(L, 1, "invalid output port count");
		e->handler->enableMidiOut(count);
		return 0;
	}

	// midiOut.enableTiming([reportLate]) — sample-accurate output for this script.
	static int lua_midiOut_enableTiming(lua_State* L) {
		getEngine(L)->handler->enableTiming(lua_toboolean(L, 1) != 0);
		return 0;
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
		requireCallback(L, "midi.create");
		size_t* s = &e->msgCount;
		if (*s >= e->msgStore.size()) luaStoreFull(L, "midi.create");
		e->msgStore[*s] = ScriptMessage();
		lua_pushinteger(L, static_cast<lua_Integer>(e->slotToHandle((*s)++)));
		return 1;
	}

	static int lua_midi_clone(lua_State* L) {
		auto* e = getEngine(L);
		requireCallback(L, "midi.clone");
		size_t src = checkHandle(L, 1);
		// A group handle is cloned as a group: all its slots, with the chain flags.
		if (e->msgCount + MidiScriptEngine::groupSize(e->msgStore[src]) > e->msgStore.size()) luaStoreFull(L, "midi.clone");
		lua_pushinteger(L, static_cast<lua_Integer>(e->slotToHandle(e->cloneGroup(src))));
		return 1;
	}

	// Shared by midi.createNRPN() and midi.createRPN(): the same 4-handle chain,
	// filled with CC 99/98 or CC 101/100 by the matching setter.
	static int luaCreateParam(lua_State* L, bool rpn) {
		auto* e = getEngine(L);
		const char* name = rpn ? "midi.createRPN" : "midi.createNRPN";
		requireCallback(L, name);
		size_t* s = &e->msgCount;
		if (*s + 4 > e->msgStore.size()) luaStoreFull(L, name);
		e->msgStore[*s + 0] = ScriptMessage();
		e->msgStore[*s + 0].isNrpn = true;
		e->msgStore[*s + 0].isRpn = rpn;
		e->msgStore[*s + 0].in.type = rpn ? StoermelderPackOne::MessageEx::Type::RPN : StoermelderPackOne::MessageEx::Type::NRPN;
		e->msgStore[*s + 1] = ScriptMessage();
		e->msgStore[*s + 2] = ScriptMessage();
		e->msgStore[*s + 3] = ScriptMessage();
		lua_Integer idx = static_cast<lua_Integer>(e->slotToHandle(*s));
		*s += 4;
		lua_pushinteger(L, idx);
		return 1;
	}

	static int lua_midi_createNrpn(lua_State* L) {
		return luaCreateParam(L, false);
	}
	static int lua_midi_createRpn(lua_State* L) {
		return luaCreateParam(L, true);
	}

	static int lua_midi_createCc14bit(lua_State* L) {
		auto* e = getEngine(L);
		requireCallback(L, "midi.createCc14bit");
		size_t* s = &e->msgCount;
		if (*s + 2 > e->msgStore.size()) luaStoreFull(L, "midi.createCc14bit");
		// 2 consecutive entries, filled by setCc14bit: CC cc (value MSB) and
		// CC cc+32 (value LSB), sent atomically as a pair.
		e->msgStore[*s + 0] = ScriptMessage();
		e->msgStore[*s + 0].isCc14bit = true;
		e->msgStore[*s + 0].in.type = StoermelderPackOne::MessageEx::Type::CC_14BIT;
		e->msgStore[*s + 1] = ScriptMessage();
		lua_Integer idx = static_cast<lua_Integer>(e->slotToHandle(*s));
		*s += 2;
		lua_pushinteger(L, idx);
		return 1;
	}

	static int lua_midi_getChanPressure(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getNote());
		return 1;
	}

	static int lua_midi_getChannel(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
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
		ScriptMessage* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getSize());
		return 1;
	}

	static int lua_midi_getNote(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getNote());
		return 1;
	}

	static int lua_midi_getPitchWheel(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		uint16_t value = (static_cast<uint16_t>(m->in.msg.getValue()) << 7) | m->in.msg.getNote();
		lua_pushinteger(L, value);
		return 1;
	}

	static int lua_midi_getProgramChange(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushinteger(L, m->in.msg.getNote());
		return 1;
	}

	static int lua_midi_getSysEx(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
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
		ScriptMessage* m = getMsg(L, 1);
		// Payload length only — f0/f7 framing excluded.
		lua_pushinteger(L, std::max(0, m->in.msg.getSize() - 2));
		return 1;
	}

	static int lua_midi_getRaw(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
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
		ScriptMessage* m = getMsg(L, 1);
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
		ScriptMessage* m = getMsg(L, 1);
		if (isAssembled(m)) lua_pushinteger(L, m->in.paramNumber);
		else if (m->in.msg.getStatus() == 0xb) lua_pushinteger(L, m->in.msg.getNote());
		else lua_pushinteger(L, -1);
		return 1;
	}

	// True when the message carries a decode result from MidiProcessor, i.e. it
	// arrived assembled rather than as a raw CC.
	static bool isAssembled(const ScriptMessage* m) {
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
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.type == StoermelderPackOne::MessageEx::Type::NRPN);
		return 1;
	}
	static int lua_midi_isRpn(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.type == StoermelderPackOne::MessageEx::Type::RPN);
		return 1;
	}
	static int lua_midi_isCc14bit(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.type == StoermelderPackOne::MessageEx::Type::CC_14BIT);
		return 1;
	}

	// is-type helpers
	static int lua_midi_isCc(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xb);
		return 1;
	}
	static int lua_midi_isChanPressure(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xd);
		return 1;
	}
	static int lua_midi_isClock(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0x8);
		return 1;
	}
	static int lua_midi_isContinue(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0xb);
		return 1;
	}
	static int lua_midi_isKeyPressure(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xa);
		return 1;
	}
	static int lua_midi_isNoteOff(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0x8);
		return 1;
	}
	static int lua_midi_isNoteOn(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0x9);
		return 1;
	}
	static int lua_midi_isPitchWheel(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xe);
		return 1;
	}
	static int lua_midi_isProgramChange(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xc);
		return 1;
	}
	static int lua_midi_isStart(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0xa);
		return 1;
	}
	static int lua_midi_isStop(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0xc);
		return 1;
	}
	static int lua_midi_isSysEx(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		lua_pushboolean(L, m->in.msg.getStatus() == 0xf && m->in.msg.getChannel() == 0x0);
		return 1;
	}

	// set-type helpers

	static int lua_midi_setCc(lua_State* L) {
		// midi.setCc(msg, channel, cc, value)
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setCc: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint8_t cc = clampInt<uint8_t>(luaL_checknumber(L, 3), 0, 127);
		uint8_t value = clampInt<uint8_t>(luaL_checknumber(L, 4), 0, 127);
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
			// atomically when the pair is sent.
			size_t idx1 = checkHandle(L, 1);
			if (!e->msgStore[idx1].isCc14bit) luaL_argerror(L, 1, "message is not a 14-bit CC pair");
			uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
			uint8_t cc = clampInt<uint8_t>(luaL_checknumber(L, 3), 0, 31);
			uint16_t value = clampInt<uint16_t>(luaL_checknumber(L, 4), 0, 16383);
			e->fillGroup(idx1, OutGroup::CC14, ch - 1, cc, value);
			return 0;
		}

		// midi.setCc14bit(msg1, msg2, channel, cc, value) — two independent
		// handles, sent as separate messages (no atomicity).
		ScriptMessage* m1 = getMsg(L, 1);
		ScriptMessage* m2 = &e->msgStore[checkHandle(L, 2, "invalid msg2 index")];
		if (const char* groupErr = groupSetterError(*m1)) luaL_error(L, "midi.setCc14bit: %s", groupErr);
		if (const char* groupErr = groupSetterError(*m2)) luaL_error(L, "midi.setCc14bit: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 3), 1, 16);
		uint8_t cc = clampInt<uint8_t>(luaL_checknumber(L, 4), 0, 31);
		uint16_t value = clampInt<uint16_t>(luaL_checknumber(L, 5), 0, 16383);
		if (m1->in.msg.getSize() != 3) m1->in.msg.setSize(3);
		if (m2->in.msg.getSize() != 3) m2->in.msg.setSize(3);
		m1->in.msg.setStatus(0xb); m2->in.msg.setStatus(0xb);
		m1->in.msg.setChannel(ch - 1);
		m2->in.msg.setChannel(ch - 1);
		m1->in.msg.setNote(cc);
		m2->in.msg.setNote(cc + 32);
		m1->in.msg.setValue((value >> 7) & 0x7f);
		m2->in.msg.setValue(value & 0x7f);
		return 0;
	}

	static int lua_midi_setChannel(lua_State* L) {
		size_t idx = checkHandle(L, 1);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		getEngine(L)->setGroupChannel(idx, ch - 1);
		return 0;
	}

	static int lua_midi_setChanPressure(lua_State* L) {
		// midi.setChanPressure(msg, channel, value)
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setChanPressure: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint8_t val = clampInt<uint8_t>(luaL_checknumber(L, 3), 0, 127);
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
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setKeyPressure: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint8_t note = clampInt<uint8_t>(luaL_checknumber(L, 3), 0, 127);
		uint8_t vel = clampInt<uint8_t>(luaL_checknumber(L, 4), 0, 127);
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0xa);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(note);
		m->in.msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNote(lua_State* L) {
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setNote: %s", groupErr);
		uint8_t value = clampInt<uint8_t>(luaL_checknumber(L, 2), 0, 127);
		m->in.msg.setNote(value);
		return 0;
	}

	static int lua_midi_setNoteOff(lua_State* L) {
		// midi.setNoteOff(msg, channel, note [, velocity])
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setNoteOff: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint8_t note = clampInt<uint8_t>(luaL_checknumber(L, 3), 0, 127);
		uint8_t vel = clampInt<uint8_t>(luaL_optnumber(L, 4, 0), 0, 127);
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0x8);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(note);
		m->in.msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNoteOn(lua_State* L) {
		// midi.setNoteOn(msg, channel, note, velocity)
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setNoteOn: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint8_t note = clampInt<uint8_t>(luaL_checknumber(L, 3), 0, 127);
		uint8_t vel = clampInt<uint8_t>(luaL_checknumber(L, 4), 0, 127);
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0x9);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(note);
		m->in.msg.setValue(vel);
		return 0;
	}

	// Shared by midi.setNRPN() and midi.setRPN(): (handle, channel, number, value).
	// An RPN selects with CC 101/100 instead of 99/98; data entry is the same.
	static int luaSetParam(lua_State* L, bool rpn) {
		auto* e = getEngine(L);
		size_t idx = checkHandle(L, 1, "invalid nrpn index");
		ScriptMessage* s1 = &e->msgStore[idx];
		if (!s1->isNrpn || s1->isRpn != rpn) luaL_argerror(L, 1, rpn ? "message is not an RPN" : "message is not an NRPN");
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint16_t number = clampInt<uint16_t>(luaL_checknumber(L, 3), 0, 16383);
		uint16_t value = clampInt<uint16_t>(luaL_checknumber(L, 4), 0, 16383);

		e->fillGroup(idx, rpn ? OutGroup::RPN : OutGroup::NRPN, ch - 1, number, value);
		return 0;
	}

	static int lua_midi_setNrpn(lua_State* L) {
		return luaSetParam(L, false);
	}
	static int lua_midi_setRpn(lua_State* L) {
		return luaSetParam(L, true);
	}

	static int lua_midi_setPitchWheel(lua_State* L) {
		// midi.setPitchWheel(msg, channel, value)
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setPitchWheel: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint16_t value = clampInt<uint16_t>(luaL_checknumber(L, 3), 0, 16383);
		if (m->in.msg.getSize() != 3) m->in.msg.setSize(3);
		m->in.msg.setStatus(0xe);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(value & 0x7f);
		m->in.msg.setValue((value >> 7) & 0x7f);
		return 0;
	}

	static int lua_midi_setProgramChange(lua_State* L) {
		// midi.setProgramChange(msg, channel, program)
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setProgramChange: %s", groupErr);
		uint8_t ch = clampInt<uint8_t>(luaL_checknumber(L, 2), 1, 16);
		uint8_t prg = clampInt<uint8_t>(luaL_checknumber(L, 3), 0, 127);
		// Program Change is a 2-byte message (status + program), not 3: a stray
		// third byte goes out as a second Program Change to program 0 on ALSA.
		if (m->in.msg.getSize() != 2) m->in.msg.setSize(2);
		m->in.msg.setStatus(0xc);
		m->in.msg.setChannel(ch - 1);
		m->in.msg.setNote(prg);
		return 0;
	}

	static int lua_midi_setRaw(lua_State* L) {
		// midi.setRaw(msg, hexstring)
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setRaw: %s", groupErr);
		size_t len;
		const char* raw = luaL_checklstring(L, 2, &len);
		std::string data(raw, len);
		if (data.length() % 2 != 0) {
			luaL_error(L, "midi.setRaw: hex string length must be even");
		}
		if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
			luaL_error(L, "midi.setRaw: invalid hex string");
		}
		if (data.length() / 2 > static_cast<size_t>(MidiScriptEngine::sysExMaxPayloadLength) + 2) {
			luaL_error(L, "midi.setRaw: message exceeds maximum of %d bytes", MidiScriptEngine::sysExMaxPayloadLength + 2);
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
		ScriptMessage* m = getMsg(L, 1);
		if (const char* groupErr = groupSetterError(*m)) luaL_error(L, "midi.setSysEx: %s", groupErr);
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
		size_t idx = checkHandle(L, 1);
		ScriptMessage* m = &getEngine(L)->msgStore[idx];
		if (MidiScriptEngine::groupSetterError(*m) != nullptr) {
			// A group: the combined 14-bit value, once its setter has given it a number.
			uint16_t value = clampInt<uint16_t>(luaL_checknumber(L, 2), 0, 16383);
			if (!getEngine(L)->setGroupValue(idx, value)) luaL_error(L, "midi.setValue: %s", MidiScriptEngine::groupSetterError(*m));
			return 0;
		}
		uint8_t value = clampInt<uint8_t>(luaL_checknumber(L, 2), 0, 127);
		m->in.msg.setValue(value);
		return 0;
	}

	// ── midiOut.* ─────────────────────────────────────────────────────────────
	// Output port is set via midiOut.selectPort(n) and applied to every message
	// sent until selectPort() is called again.

	// The send bindings check every argument (checkHandle() raises) before the
	// one sendEntry() call, which never does: luaL_error longjmps past C++
	// destructors.
	static int lua_midiOut_send(lua_State* L) {
		// midiOut.send(msg)
		auto* e = getEngine(L);
		size_t idx = checkHandle(L, 1);
		e->sendEntry(e->msgStore[idx], e->selectedPort, e->frameForSend());
		return 0;
	}

	static int lua_midiOut_sendAfterMs(lua_State* L) {
		// midiOut.sendAfterMs(msg, ms)
		auto* e = getEngine(L);
		double ms = luaL_checknumber(L, 2);

		size_t idx = checkHandle(L, 1);
		e->sendEntry(e->msgStore[idx], e->selectedPort, e->frameAfterMs(ms), 0, 0, 0, true);
		return 0;
	}

	// midiOut.sendAtFrame(msg, frame) — send at an absolute engine frame.
	static int lua_midiOut_sendAtFrame(lua_State* L) {
		auto* e = getEngine(L);
		double frame = luaL_checknumber(L, 2);

		size_t idx = checkHandle(L, 1);
		// A negative frame means "now": a plain send, which cancel() leaves alone.
		int64_t f = frameAtFrame(frame);
		e->sendEntry(e->msgStore[idx], e->selectedPort, f, 0, 0, 0, f >= 0);
		return 0;
	}

	// rack.getEventFrame() — frame of the event being handled, -1 outside one.
	static int lua_rack_getEventFrame(lua_State* L) {
		lua_pushnumber(L, static_cast<lua_Number>(getEngine(L)->currentInFrame));
		return 1;
	}

	// rack.msToFrames(ms) — whole frames in `ms` milliseconds at the current sample rate.
	static int lua_rack_msToFrames(lua_State* L) {
		lua_pushinteger(L, static_cast<lua_Integer>(getEngine(L)->msToFrames(luaL_checknumber(L, 1))));
		return 1;
	}

	// rack.framesToMs(frames) — milliseconds in `frames` frames at the current sample rate.
	static int lua_rack_framesToMs(lua_State* L) {
		lua_pushnumber(L, getEngine(L)->framesToMs(luaL_checknumber(L, 1)));
		return 1;
	}

	// midiOut.cancel([msg]) — drop scheduled messages on the selected port: all of
	// them, or those with msg's address. Every check comes before cancelEntry()
	// (luaL_error longjmps, see sendEntry()).
	static int lua_midiOut_cancel(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		if (n > 1) luaL_error(L, "midiOut.cancel: bad args");
		if (n == 0) {
			e->cancelEntry(nullptr);
			return 0;
		}
		size_t idx = checkHandle(L, 1);
		if (!isCancelPattern(e->msgStore[idx])) luaL_argerror(L, 1, "message has no status byte");
		e->cancelEntry(&e->msgStore[idx]);
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

		size_t idx = checkHandle(L, 1);
		// Read now, so the schedule is relative to the tick count at the call.
		int64_t currentTicks = e->handler->getTrigTicks(trigPort - 1, channel - 1);
		e->sendEntry(e->msgStore[idx], e->selectedPort, -1, uint8_t(channel - 1), uint64_t(currentTicks + ticks), trigPort - 1, true);
		return 0;
	}

	// Shared by midi.enableNrpnIn() and midi.enableRpnIn(): both take the same
	// arguments and differ only in which kind they arm.
	static int luaEnableParamIn(lua_State* L, int kind, const char* name) {
		// midi.enableNrpnIn(midiPort [, channel] [, dataEntry]) / midi.enableRpnIn(...)
		//   midiPort: 1-based; channel: 1-based MIDI channel, omitted/nil = all;
		//   dataEntry: "lsb" (default, fire on CC 38) or "msb" (also fire on CC 6).
		// Every check comes before the handler call (luaL_error longjmps).
		auto* e = getEngine(L);
		if (lua_gettop(L) > 3) return luaL_error(L, "%s: bad args", name);
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
		bool msbDataEntry = false;
		if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
			std::string mode = luaL_checkstring(L, 3);
			if (mode != "lsb" && mode != "msb") {
				return luaL_error(L, "%s: dataEntry must be \"lsb\" or \"msb\"", name);
			}
			msbDataEntry = mode == "msb";
		}
		e->handler->enableNrpnIn(midiPort - 1, kind, channel, msbDataEntry);
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
