#include "MidiScriptEngine.h"
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
		Message msg;
		bool isNrpn = false;
		bool send = false;
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

	// The five lifecycle hooks (onMidiMessage/onTrigger/onLoad/onUnload/onSave)
	// are resolved from rack once at load and kept as registry refs, not
	// re-looked-up per dispatch — so defining/reassigning a hook later has no
	// effect; only what was present at load runs. LUA_NOREF = "not defined".
	// Asymmetry: Lua calls hooks as bare functions; QuickJS as methods (rackObj
	// as thisVal). Predates caching, unused by any preset.
	int onMidiMessageRef = LUA_NOREF;
	int onTriggerRef = LUA_NOREF;
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

	// Usually a no-op (L already nullptr): the real closeState() runs from
	// MidiKitModule's destructor while this object is fully alive — the
	// handler callbacks are pure virtual, so calling them post-destruction
	// would be UB.
	~MidiScriptEngineLua() {
		closeState();
	}


	// Engine selection: true when the script's header declares Lua. The same
	// substring check the module used to run itself (Q26) — so the module has
	// no third header parser.
	bool testScript(const std::string& script) override {
		return script.find("@engine Lua") != std::string::npos;
	}

	void loadScript(const char* script, const std::string& persistedConfigJson = "") override {
		closeState();
		resetTipsyOutput();

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

		if (topics.find("engine") == topics.end() || topics["engine"] != "Lua") {
			handler->writeLog("Script is not compatible with this engine (expected @engine Lua)", false);
			return;
		}

		if (topics.find("author") != topics.end()) {
			handler->writeLog(string::f("Author: %s", topics["author"].c_str()), false);
		}
		if (topics.find("description") != topics.end()) {
			handler->writeLog(topics["description"], false);
		}

		// ── Create Lua state ─────────────────────────────────────────────────

		L = luaL_newstate();

		// Store engine pointer in registry so C callbacks can retrieve it
		lua_pushlightuserdata(L, this);
		lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);

		// Open only safe standard libraries (no io/os/package/debug)
		luaL_requiref(L, "_G",       luaopen_base,   1); lua_pop(L, 1);
		luaL_requiref(L, "math",     luaopen_math,   1); lua_pop(L, 1);
		luaL_requiref(L, "string",   luaopen_string, 1); lua_pop(L, 1);
		luaL_requiref(L, "table",    luaopen_table,  1); lua_pop(L, 1);

		// ── Register engine API ──────────────────────────────────────────────
		registerAPI();

		// ── Load and run script ──────────────────────────────────────────────
		// luaL_loadbuffer, not luaL_dostring: the latter names the chunk with
		// the whole script text, so errors read as [string "/**..."]:12:.
		// Naming the chunk "script" makes them read as "script:12:".
		if (luaL_loadbuffer(L, script, strlen(script), CHUNK_NAME) != LUA_OK ||
		    lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("Error loading script: %s", err ? err : "(unknown)"), false);
			lua_pop(L, 1);
			closeState();
			return;
		}

		handler->writeLog("Script loaded", false);

		// Resolve and cache the five lifecycle hooks once (see declarations).
		lua_getglobal(L, "rack");
		if (lua_istable(L, -1)) {
			onLoadRef = cacheHookRef("onLoad");
			onUnloadRef = cacheHookRef("onUnload");
			onSaveRef = cacheHookRef("onSave");
			onMidiMessageRef = cacheHookRef("onMidiMessage");
			onTriggerRef = cacheHookRef("onTrigger");
		}
		lua_pop(L, 1); // pop rack table (or whatever "rack" turned out to be)

		if (onMidiMessageRef == LUA_NOREF) {
			handler->writeLog("No onMidiMessage(midiPort, msg) function defined — incoming MIDI is ignored", false);
		}

		callOnLoad(persistedConfigJson);
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
	// safe to touch from the worker thread, hence runSyncString().
	bool captureConfig(std::string& out) override {
		// Answered from the cached ref, without a worker round-trip: onSaveRef
		// is a plain member, written only at load time and in closeState().
		if (L && onSaveRef == LUA_NOREF) {
			out.clear();
			return true;
		}
		if (!L) return false;
		return runSyncString([this]() -> std::string {
			if (!L) return "";
			int nRet = callOnSave();
			std::string configJson;
			if (nRet > 0) {
				configJson = luaTableToJson(L, -1);
				lua_pop(L, 1);
			}
			return configJson;
		}, out);
	}

	// Tears down the Lua state. See MidiScriptEngine::closeState().
	std::string closeState() override {
		if (L) {
			callOnUnload();
			// onUnload()'s teardown messages (e.g. all-notes-off) must go out.
			flushMsgStore();
			clearContextMenus();
			// lua_close invalidates these anyway; reset for hygiene.
			onMidiMessageRef = LUA_NOREF;
			onTriggerRef = LUA_NOREF;
			onLoadRef = LUA_NOREF;
			onUnloadRef = LUA_NOREF;
			onSaveRef = LUA_NOREF;
			lua_close(L);
			L = nullptr;
		}
		return "";
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
		int status = lua_pcall(L, 0, 1, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onSave error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
			return 0;
		}
		// Stack is now [result].
		if (lua_istable(L, -1)) {
			return 1;
		}
		lua_pop(L, 1); // pop non-table result
		return 0;
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

	void processInMessage(int midiPort, Message& msg) override {
		if (L) {
			midiInQueue.push(std::make_tuple(midiPort, msg));
		}
	}

	void processInTick(int trigPort) override {
		if (L) {
			tickInQueue.push(trigPort);
		}
	}

	// Pushes every message sent during the callback that just ran into
	// midiOutQueue, in send() order, not handle-creation order: a script may
	// create several messages and send them in a different order, and the
	// receiver must observe send() order.
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

	// Releases the stored script callbacks. Called only from closeState(), which
	// — like loadScript() — runs inline on the calling thread. Load/teardown
	// never overlap dispatch (the module doesn't process while replacing a
	// script), so this needs no lock even though other touchers are on the
	// worker.
	void clearContextMenus() {
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
			int status = lua_pcall(L, nargs, 0, 0);
			if (status != LUA_OK) {
				const char* err = lua_tostring(L, -1);
				handler->writeLog(string::f("Context menu callback error: %s", err ? err : "(unknown)"));
				lua_pop(L, 1); // pop error message
			}
		});
	}

	// Current bytes used by the Lua heap, or false if no script is loaded.
	// minilua's allocator grows via realloc() with no cap, so only an absolute
	// count is available.
	bool getMemoryUsage(size_t& used) {
		if (!L) return false;
		int kb = lua_gc(L, LUA_GCCOUNT);
		int b = lua_gc(L, LUA_GCCOUNTB);
		used = (size_t) kb * 1024 + (size_t) b;
		return true;
	}


	void dispatchMidiMessage(int midiPort, Message& msg) override {
		if (!L) return;

		msgStore[0].msg = msg;
		msgStore[0].send = false;
		msgStore[0].tick = 0;
		msgStore[0].isNrpn = false;
		msgCount = 1;

		// Calls the cached onMidiMessageRef. No-op if never defined (LUA_NOREF).
		if (onMidiMessageRef == LUA_NOREF) return;
		lua_rawgeti(L, LUA_REGISTRYINDEX, onMidiMessageRef);
		lua_pushinteger(L, midiPort + 1);
		lua_pushinteger(L, 0);
		inCallback = true;
		int status = lua_pcall(L, 2, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onMidiMessage error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		flushMsgStore();
	}

	// Dispatches onTrigger(trigPort) via the cached onTriggerRef. No-op if
	// the script never defined it.
	void dispatchTrigger(int trigPort) override {
		if (!L) return;
		if (onTriggerRef == LUA_NOREF) return;

		lua_rawgeti(L, LUA_REGISTRYINDEX, onTriggerRef);
		lua_pushinteger(L, trigPort + 1);
		msgCount = 0;
		inCallback = true;
		int status = lua_pcall(L, 1, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("onTrigger error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}

		flushMsgStore();
	}

	std::string callLuaTableFunc(const char* tableName, const char* funcName, int arg) {
		// stack must be balanced on return
		lua_getglobal(L, tableName);
		if (!lua_istable(L, -1)) { lua_pop(L, 1); return ""; }

		lua_getfield(L, -1, funcName);
		if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return ""; }

		lua_pushinteger(L, arg);
		std::string result;
		if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
			const char* s = lua_tostring(L, -1);
			result = s ? s : "";
			lua_pop(L, 1);
		}
		else {
			lua_pop(L, 1); // error message
		}
		lua_pop(L, 1); // table
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
		setTableFunc("getTicks",    lua_trig_getTicks);
		setTableFunc("isHigh",      lua_trig_isHigh);
		setTableFunc("isLow",       lua_trig_isLow);
		setTableFunc("setGate",     lua_trig_setGate);
		setTableFunc("setHigh",     lua_trig_setHigh);
		setTableFunc("setLow",      lua_trig_setLow);
		setTableFunc("setTrigger",  lua_trig_setTrigger);
		setTableFunc("sendTipsy",   lua_trig_sendTipsy);
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
		setTableFunc("getChanPressure", lua_midi_getChanPressure);
		setTableFunc("getChannel",      lua_midi_getChannel);
		setTableFunc("getLength",       lua_midi_getLength);
		setTableFunc("getNote",         lua_midi_getNote);
		setTableFunc("getPitchWheel",   lua_midi_getPitchWheel);
		setTableFunc("getProgramChange",lua_midi_getProgramChange);
		setTableFunc("getRaw",          lua_midi_getRaw);
		setTableFunc("getSysEx",        lua_midi_getSysEx);
		setTableFunc("getSysExLength",  lua_midi_getSysExLength);
		setTableFunc("getValue",        lua_midi_getValue);
		setTableFunc("isCc",            lua_midi_isCc);
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

	static int lua_trig_getTicks(lua_State* L) {
		auto* e = getEngine(L);
		int i = static_cast<int>(luaL_checkinteger(L, 1));
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		lua_pushinteger(L, static_cast<lua_Integer>(e->handler->getTrigTicks(i - 1)));
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
		e->handler->setTrig(i - 1, ch - 1, duration);
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
		clone.msg = src->msg;
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

	static int lua_midi_getChanPressure(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->msg.getNote());
		return 1;
	}

	static int lua_midi_getChannel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		// Status 0xf (realtime/SysEx) carries no channel; the low nibble is a
		// sub-type selector, so "+ 1" returned a meaningless channel (#A4).
		// -1 is unambiguous: 1-16 is the only valid range, so a script can
		// check `> 0` without a try/catch.
		if (m->msg.getStatus() == 0xf) {
			lua_pushinteger(L, -1);
			return 1;
		}
		lua_pushinteger(L, m->msg.getChannel() + 1);
		return 1;
	}

	static int lua_midi_getLength(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->msg.getSize());
		return 1;
	}

	static int lua_midi_getNote(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->msg.getNote());
		return 1;
	}

	static int lua_midi_getPitchWheel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint16_t value = (static_cast<uint16_t>(m->msg.getValue()) << 7) | m->msg.getNote();
		lua_pushinteger(L, value);
		return 1;
	}

	static int lua_midi_getProgramChange(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->msg.getNote());
		return 1;
	}

	static int lua_midi_getSysEx(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 1; i < m->msg.getSize() - 1; i++) {
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(m->msg.bytes[i]);
		}
		std::string s = ss.str();
		lua_pushlstring(L, s.c_str(), s.size());
		return 1;
	}

	static int lua_midi_getSysExLength(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		// Payload length only — f0/f7 framing excluded.
		lua_pushinteger(L, std::max(0, m->msg.getSize() - 2));
		return 1;
	}

	static int lua_midi_getRaw(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 0; i < m->msg.getSize(); i++) {
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(m->msg.bytes[i]);
		}
		std::string s = ss.str();
		lua_pushlstring(L, s.c_str(), s.size());
		return 1;
	}

	static int lua_midi_getValue(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushinteger(L, m->msg.getValue());
		return 1;
	}

	// is-type helpers
	static int lua_midi_isCc(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xb);
		return 1;
	}
	static int lua_midi_isChanPressure(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xd);
		return 1;
	}
	static int lua_midi_isClock(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xf && m->msg.getChannel() == 0x8);
		return 1;
	}
	static int lua_midi_isContinue(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xf && m->msg.getChannel() == 0xb);
		return 1;
	}
	static int lua_midi_isKeyPressure(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xa);
		return 1;
	}
	static int lua_midi_isNoteOff(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0x8);
		return 1;
	}
	static int lua_midi_isNoteOn(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0x9);
		return 1;
	}
	static int lua_midi_isPitchWheel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xe);
		return 1;
	}
	static int lua_midi_isProgramChange(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xc);
		return 1;
	}
	static int lua_midi_isStart(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xf && m->msg.getChannel() == 0xa);
		return 1;
	}
	static int lua_midi_isStop(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xf && m->msg.getChannel() == 0xc);
		return 1;
	}
	static int lua_midi_isSysEx(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		lua_pushboolean(L, m->msg.getStatus() == 0xf && m->msg.getChannel() == 0x0);
		return 1;
	}

	// set-type helpers

	static int lua_midi_setCc(lua_State* L) {
		// midi.setCc(msg, channel, cc, value)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t cc = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t value = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_checkinteger(L, 4)))));
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xb);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(cc);
		m->msg.setValue(value);
		return 0;
	}

	static int lua_midi_setCc14bit(lua_State* L) {
		// midi.setCc14bit(msg1, msg2, channel, cc, value)
		auto* e = getEngine(L);
		MessageEx* m1 = getMsg(L, 1);
		int idx2 = static_cast<int>(luaL_checkinteger(L, 2));
		if (idx2 < 0 || static_cast<size_t>(idx2) >= e->msgCount) luaL_argerror(L, 2, "invalid msg2 index");
		MessageEx* m2 = &e->msgStore[idx2];
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 3)))));
		uint8_t cc = static_cast<uint8_t>(luaL_checkinteger(L, 4));
		double value = luaL_checknumber(L, 5);
		if (m1->msg.getSize() != 3) m1->msg.setSize(3);
		if (m2->msg.getSize() != 3) m2->msg.setSize(3);
		m1->msg.setStatus(0xb); m2->msg.setStatus(0xb);
		m1->msg.setChannel(ch - 1);
		m2->msg.setChannel(ch - 1);
		m1->msg.setNote(cc);
		m2->msg.setNote(cc + 32);
		m1->msg.setValue(static_cast<int8_t>(value));
		m2->msg.setValue(static_cast<int8_t>((value - static_cast<int8_t>(value)) * 128.f));
		return 0;
	}

	static int lua_midi_setChannel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		m->msg.setChannel(ch - 1);
		return 0;
	}

	static int lua_midi_setChanPressure(lua_State* L) {
		// midi.setChanPressure(msg, channel, value)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t val = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		// Channel pressure is a 2-byte message (status + pressure), not 3 —
		// the pressure lives in bytes[1], read back via getChanPressure/getNote.
		if (m->msg.getSize() != 2) m->msg.setSize(2);
		m->msg.setStatus(0xd);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(val);
		return 0;
	}

	static int lua_midi_setKeyPressure(lua_State* L) {
		// midi.setKeyPressure(msg, channel, note, velocity)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t note = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t vel = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_checkinteger(L, 4)))));
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xa);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(note);
		m->msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNote(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint8_t value = static_cast<uint8_t>(luaL_checkinteger(L, 2));
		m->msg.setNote(value);
		return 0;
	}

	static int lua_midi_setNoteOff(lua_State* L) {
		// midi.setNoteOff(msg, channel, note [, velocity])
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t note = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t vel = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_optinteger(L, 4, 0)))));
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0x8);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(note);
		m->msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNoteOn(lua_State* L) {
		// midi.setNoteOn(msg, channel, note, velocity)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t note = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		uint8_t vel = static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(luaL_checkinteger(L, 4)))));
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0x9);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(note);
		m->msg.setValue(vel);
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
		return 0;
	}

	static int lua_midi_setPitchWheel(lua_State* L) {
		// midi.setPitchWheel(msg, channel, value)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint16_t value = static_cast<uint16_t>(luaL_checkinteger(L, 3));
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xe);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(value & 0x7f);
		m->msg.setValue((value >> 7) & 0x7f);
		return 0;
	}

	static int lua_midi_setProgramChange(lua_State* L) {
		// midi.setProgramChange(msg, channel, program)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch = static_cast<uint8_t>(std::max(1, std::min(16, static_cast<int>(luaL_checkinteger(L, 2)))));
		uint8_t prg = static_cast<uint8_t>(luaL_checkinteger(L, 3));
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xc);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(prg);
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
		m->msg.setSize(static_cast<int>(data.length() / 2));
		for (size_t i = 0; i < data.length(); i += 2) {
			char byte = static_cast<char>(strtol(data.substr(i, 2).c_str(), nullptr, 16));
			m->msg.bytes[i / 2] = byte;
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
		m->msg.setSize(static_cast<int>(data.length() / 2 + 2));
		m->msg.bytes[0] = 0xf0;
		for (size_t i = 0; i < data.length(); i += 2) {
			char byte = static_cast<char>(strtol(data.substr(i, 2).c_str(), nullptr, 16));
			m->msg.bytes[i / 2 + 1] = byte;
		}
		m->msg.bytes[m->msg.getSize() - 1] = 0xf7;
		return 0;
	}

	static int lua_midi_setValue(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint8_t value = static_cast<uint8_t>(luaL_checkinteger(L, 2));
		m->msg.setValue(value);
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
		m->msg.frame = -1;
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
		m->msg.frame = currentFrame + frame;
		m->tick = 0;
		return 0;
	}

	static int lua_midiOut_sendAfterTrigger(lua_State* L) {
		// midiOut.sendAfterTrigger(msg, [trigPort,] ticks)
		//   2 args: msg, ticks              (trig port defaults to 1)
		//   3 args: msg, trigPort, ticks

		auto* e = getEngine(L);
		int n = lua_gettop(L);

		int ticks = static_cast<int>(luaL_checkinteger(L, n));
		int trigPort = 0;  // 0 = use trig port 0

		if (n == 3) {
			trigPort = static_cast<int>(luaL_checkinteger(L, 2));
		}

		if (trigPort < 0 || trigPort > e->inputTrigCount) {
			luaL_error(L, "midiOut.sendAfterTrigger: invalid trig port index");
		}

		MessageEx* m = getPortMsg(L);
		int64_t currentTicks = e->handler->getTrigTicks(trigPort == 0 ? 0 : trigPort - 1);
		m->send = true;
		m->sendOrder = e->sendCounter++;
		m->msg.frame = -1;
		m->tick = currentTicks + ticks;
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
		
		bool success = e->sendTipsy(mimeType, reinterpret_cast<const unsigned char*>(data), static_cast<uint32_t>(dataLen));
		
		if (!success) {
			luaL_error(L, "trig.sendTipsy: failed to initiate message");
		}
		
		return 0;
	}
};

} // namespace Lua
} // namespace MidiScript
} // namespace StoermelderPackOne
