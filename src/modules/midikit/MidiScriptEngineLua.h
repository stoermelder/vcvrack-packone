#include "MidiScriptEngine.h"
extern "C" {
	#include "minilua.h"
}
#include "../../utils/TaskWorker.hpp"
#include <algorithm>
#include <iomanip>
#include <regex>
#include <sstream>

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
	// Next value handed to MessageEx::sendOrder. Never reset: it only needs to
	// be monotonic within a single callback, and the store is reset per callback.
	size_t sendCounter = 0;
	// True only while onMidiMessage() is executing. The message store is reset on
	// every callback, so handles created outside one are silently invalidated —
	// this lets midi.create() warn instead of failing quietly.
	bool inCallback = false;
	// Sticky output port selected via midiOut.selectPort(), 0-based. Stays in
	// effect across callbacks until changed again.
	int selectedPort = 0;

	// ─── Lua state & threading ────────────────────────────────────────────────

	// Registry key used to store `this` as a lightuserdata inside each lua_State
	static constexpr const char* REGISTRY_KEY = "stoermelder_MidiScriptEngineLua";

	// Chunk name the script is loaded under. Lua prefixes every error it raises
	// with "<chunkname>:<line>:", so this is what the user sees in the log.
	// The "=" prefix tells Lua to use the name verbatim rather than decorating
	// it as [string "..."].
	static constexpr const char* CHUNK_NAME = "=script";

	lua_State* L = nullptr;

	// closeState() here is a no-op fallback (L is already nullptr): onUnload()
	// must run via MidiKitModule's destructor, while this object is still
	// fully alive — writeLog/input.*/trig.*/param.* are pure virtual here and
	// only overridden on the derived class, so calling them post-destruction
	// (e.g. from a script's onUnload) would be undefined behaviour.
	~MidiScriptEngineLua() {
		closeState();
	}


	void loadScript(const char* script) override {
		closeState();

		if (script[0] == '\0') {
			handler->writeLog("No script", false);
			return;
		}

		// ── Parse file header ────────────────────────────────────────────────
		// Supports both Lua-style block comments and JS-style /** */ comments:
		//
		//   JS style:
		//     /**
		//      * @engine Lua
		//      * @author ...
		//      */
		//
		//   Lua style:
		//     --[[
		//     @engine Lua
		//     @author ...
		//     --]]

		// ── Parse @key value tags from the header block only ────────────────
		// Handles both Lua (--[[ ... --]]) and JS (/** ... */) comment styles.
		// Scans line by line so the capture never spills into the script body.
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
		// luaL_loadbuffer rather than luaL_dostring: the latter passes the whole
		// script text as the chunk name, so Lua's own "chunk:LINE:" prefix comes
		// out as [string "/**..."]:12: with the source dumped inline. Naming the
		// chunk "script" makes that prefix read as "script:12:" instead.
		if (luaL_loadbuffer(L, script, strlen(script), CHUNK_NAME) != LUA_OK ||
		    lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("Error loading script: %s", err ? err : "(unknown)"), false);
			lua_pop(L, 1);
			closeState();
			return;
		}

		handler->writeLog("Script loaded", false);
		callOnLoad();
	}

	void closeState() {
		if (L) {
			callOnUnload();
			lua_close(L);
			L = nullptr;
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

	void callOptionalHook(const char* name) {
		// Callbacks live on the rack table (rack.onMidiMessage etc.), not on
		// the global scope.
		lua_getglobal(L, "rack");
		if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
		lua_getfield(L, -1, name);
		if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
		msgCount = 0;
		inCallback = true;
		int status = lua_pcall(L, 0, 0, 0);
		inCallback = false;
		if (status != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			handler->writeLog(string::f("%s error: %s", name, err ? err : "(unknown)"));
			lua_pop(L, 1); // pop error message
		}
		lua_pop(L, 1); // pop rack table
		flushMsgStore();
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
	// midiOutQueue, in send() order rather than handle-creation order: a
	// script may create several messages and send them in a different order,
	// and the receiver must observe send() order.
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

	// Returns current bytes in use by the Lua heap, or false if no script is loaded.
	// minilua's default allocator
	// grows via realloc() with no cap, so only an absolute byte count is available.
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

		lua_getglobal(L, "rack");
		if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
		lua_getfield(L, -1, "onMidiMessage");
		if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
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
		lua_pop(L, 1); // pop rack table

		flushMsgStore();
	}

	// Dispatches onTrigger(trigPort) when the trigger input fires. No-op if
	// the script never defined it.
	void dispatchTrigger(int trigPort) override {
		if (!L) return;

		lua_getglobal(L, "rack");
		if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
		lua_getfield(L, -1, "onTrigger");
		if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
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
		lua_pop(L, 1); // pop rack table

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
		// Concatenate every argument into one log line, coercing each value
		// with the same per-type contract as a single value - so scripts can
		// log numbers/booleans directly instead of wrapping every one in
		// number.toString(). Numbers use the same format as number.toString();
		// strings are logged verbatim; nil logs as "null" to match JS; any
		// other value falls back to Lua's own stringification (luaL_tolstring)
		// so the call never errors.
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

	// Formats f with up to 6 decimal places, then trims trailing zeros (and a
	// trailing '.' if nothing is left after the point) so an integral value
	// prints as "42" rather than "42.000000", matching the old %i/%f split
	// without needing two branches — and non-integers print only as many
	// decimals as they actually have, up to 6, instead of always six.
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

	// Warns when a message is created outside a callback (onMidiMessage/
	// onLoad/onUnload) — the store resets every callback, silently
	// invalidating such a handle before use. See midi.create() in SCRIPTING.md.
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
		// Copy only the MIDI payload; the clone starts as a fresh, unsent
		// message (send/tick/midiPort/isNrpn at defaults) so it can be modified
		// and sent independently of the source.
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
		// Status 0xf is the realtime/SysEx family (clock, start/stop/continue,
		// SysEx framing) — none of those carry a channel, and the low nibble is
		// a sub-type selector instead (see the is* predicates below), so the
		// old "+ 1" on that nibble returned a plausible-looking but meaningless
		// channel number (#A4). -1 is unambiguous: 1-16 is the only valid
		// channel range, so a script can check `> 0` without needing a
		// try/catch around every call.
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
		// Payload length only — the f0/f7 framing is excluded, so a script can
		// check the size before reading the payload with getSysEx.
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
	//
	// Output port is not an argument here — it's set via midiOut.selectPort(n)
	// and applied to every message sent until selectPort() is called again.

	// Returns the message at stack position 1, stamped with the currently
	// selected output port, or luaL_error on bad args.
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
};

} // namespace Lua
} // namespace MidiScript
} // namespace StoermelderPackOne
