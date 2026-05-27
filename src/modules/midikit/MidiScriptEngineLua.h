#include "MidiScriptEngine.h"
extern "C" {
	#include "minilua.h"
}
#include "../../utils/TaskWorker.hpp"
#include <iomanip>
#include <regex>
#include <sstream>

namespace StoermelderPackOne {
namespace MidiScript {
namespace Lua {

struct MidiScriptEngineLua : MidiScriptEngine {

	// ─── Message store ────────────────────────────────────────────────────────

	struct MessageEx {
		int midiPort = 0;
		Message msg;
		bool isNrpn = false;
		bool send = false;
		uint64_t tick = 0;
	};

	static const int msgStoreSize = 32;
	MessageEx msgStore[msgStoreSize];
	size_t msgCount = 0;

	// ─── Lua state & threading ────────────────────────────────────────────────

	// Registry key used to store `this` as a lightuserdata inside each lua_State
	static constexpr const char* REGISTRY_KEY = "stoermelder_MidiScriptEngineLua";

	lua_State* L = nullptr;

	static std::weak_ptr<TaskWorker> taskWorkerWeakPtr;
	std::shared_ptr<TaskWorker> taskWorker;

	dsp::RingBuffer<std::tuple<int, Message>, 128> midiInQueue;
	dsp::RingBuffer<std::tuple<int, Message, uint64_t>, 128> midiOutQueue;

	// ─── Construction / destruction ───────────────────────────────────────────

	MidiScriptEngineLua() {
		if (taskWorkerWeakPtr.expired()) {
			taskWorker = std::make_shared<TaskWorker>("MidiScriptEngineLua worker");
			taskWorkerWeakPtr = taskWorker;
		}
		else {
			taskWorker = taskWorkerWeakPtr.lock();
		}
	}

	~MidiScriptEngineLua() {
		closeState();
	}

	void closeState() {
		if (L) {
			lua_close(L);
			L = nullptr;
		}
	}

	// ─── MidiScriptEngine interface ───────────────────────────────────────────

	void runAsync(std::function<void()> task) override {
		taskWorker->work(task, APP);
	}

	void loadScript(const char* script) override {
		closeState();

		if (script[0] == '\0') {
			writeLog("No script", false);
			return;
		}

		// ── Parse file header ────────────────────────────────────────────────
		// Supports both Lua-style block comments and JS-style /** */ comments:
		//
		//   Elk/JS style:
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
				if (std::regex_search(line, m, tag_re))
					topics[m[1].str()] = m[2].str();
			}
		}

		if (topics.find("engine") == topics.end() || topics["engine"] != "Lua") {
			writeLog("Script is not compatible with this engine (expected @engine Lua)", false);
			return;
		}

		if (topics.find("author") != topics.end())
			writeLog(string::f("Author: %s", topics["author"].c_str()), false);
		if (topics.find("description") != topics.end())
			writeLog(topics["description"], false);

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
		if (luaL_dostring(L, script) != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			writeLog(string::f("Error loading script: %s", err ? err : "(unknown)"), false);
			lua_pop(L, 1);
			closeState();
			return;
		}

		writeLog("Script loaded", false);
	}

	void processInMessage(int midiPort, Message& msg) override {
		if (L) {
			midiInQueue.push(std::make_tuple(midiPort, msg));
		}
	}

	void process() override {
		if (L && midiInQueue.size() > 0) {
			runAsync([this]() {
				while (!midiInQueue.empty()) {
					auto t = midiInQueue.shift();
					int port = std::get<0>(t);
					midi::Message msg = std::get<1>(t);
					processMidi(port, msg);
				}
			});
		}
	}

	bool processOutMessage(int& midiPort, Message& msg, int& ticks) override {
		if (L && !midiOutQueue.empty()) {
			auto t = midiOutQueue.shift();
			midiPort = std::get<0>(t);
			msg      = std::get<1>(t);
			ticks    = std::get<2>(t);
			return true;
		}
		return false;
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

	// ─── Private helpers ──────────────────────────────────────────────────────

	void processMidi(int midiPort, Message& msg) {
		if (!L) return;

		msgStore[0].msg    = msg;
		msgStore[0].send   = false;
		msgStore[0].tick   = 0;
		msgStore[0].isNrpn = false;
		msgCount = 1;

		// Call Lua: processMidi(port, msgIndex)   (port 1-based, msgIndex 0-based)
		lua_getglobal(L, "processMidi");
		if (!lua_isfunction(L, -1)) {
			lua_pop(L, 1);
			return;
		}
		lua_pushinteger(L, midiPort + 1);
		lua_pushinteger(L, 0);
		if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			writeLog(string::f("processMidi error: %s", err ? err : "(unknown)"));
			lua_pop(L, 1);
		}

		// Flush outgoing messages
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
		int idx = (int)lua_tointeger(L, stackPos);
		if (idx < 0 || (size_t)idx >= e->msgCount) {
			luaL_argerror(L, stackPos, "invalid message index");
			return nullptr;
		}
		return &e->msgStore[idx];
	}

	// ─── API registration ─────────────────────────────────────────────────────

	void registerAPI() {
		// ── Global functions ─────────────────────────────────────────────────
		lua_pushcfunction(L, lua_log);
		lua_setglobal(L, "log");

		lua_pushcfunction(L, lua_overlay);
		lua_setglobal(L, "overlay");

		// ── number table ─────────────────────────────────────────────────────
		// Mostly wraps existing Lua math.*; provided for Elk script compatibility.
		lua_newtable(L);
		setTableFunc("abs",       lua_number_abs);
		setTableFunc("ceil",      lua_number_ceil);
		setTableFunc("crossfade", lua_number_crossfade);
		setTableFunc("floor",     lua_number_floor);
		setTableFunc("max",       lua_number_max);
		setTableFunc("min",       lua_number_min);
		setTableFunc("random",    lua_number_random);
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
		setTableFunc("createNRPN",      lua_midi_createNrpn);
		setTableFunc("getChannel",      lua_midi_getChannel);
		setTableFunc("getLength",       lua_midi_getLength);
		setTableFunc("getNote",         lua_midi_getNote);
		setTableFunc("getPitchWheel",   lua_midi_getPitchWheel);
		setTableFunc("getSysExData",    lua_midi_getSysExData);
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
		setTableFunc("setSysEx",        lua_midi_setSysEx);
		setTableFunc("setValue",        lua_midi_setValue);
		lua_setglobal(L, "midi");

		// ── midiOut table ────────────────────────────────────────────────────
		lua_newtable(L);
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

	static int lua_log(lua_State* L) {
		const char* msg = luaL_checkstring(L, 1);
		getEngine(L)->writeLog(msg);
		return 0;
	}

	static int lua_overlay(lua_State* L) {
		int n = lua_gettop(L);
		const char* s1 = luaL_checkstring(L, 1);
		const char* s2 = n >= 2 ? luaL_checkstring(L, 2) : "";
		const char* s3 = n >= 3 ? luaL_checkstring(L, 3) : "";
		getEngine(L)->writeOverlay(s1, s2, s3);
		return 0;
	}

	// ── number.* ──────────────────────────────────────────────────────────────

	static int lua_number_abs(lua_State* L) {
		lua_pushnumber(L, std::abs((float)luaL_checknumber(L, 1)));
		return 1;
	}

	static int lua_number_ceil(lua_State* L) {
		lua_pushnumber(L, std::ceil((float)luaL_checknumber(L, 1)));
		return 1;
	}

	static int lua_number_crossfade(lua_State* L) {
		float a = (float)luaL_checknumber(L, 1);
		float b = (float)luaL_checknumber(L, 2);
		float p = (float)luaL_checknumber(L, 3);
		lua_pushnumber(L, rack::crossfade(a, b, p));
		return 1;
	}

	static int lua_number_floor(lua_State* L) {
		lua_pushnumber(L, std::floor((float)luaL_checknumber(L, 1)));
		return 1;
	}

	static int lua_number_max(lua_State* L) {
		float a = (float)luaL_checknumber(L, 1);
		float b = (float)luaL_checknumber(L, 2);
		lua_pushnumber(L, std::max(a, b));
		return 1;
	}

	static int lua_number_min(lua_State* L) {
		float a = (float)luaL_checknumber(L, 1);
		float b = (float)luaL_checknumber(L, 2);
		lua_pushnumber(L, std::min(a, b));
		return 1;
	}

	static int lua_number_random(lua_State* L) {
		lua_pushnumber(L, rack::random::uniform());
		return 1;
	}

	static int lua_number_rescale(lua_State* L) {
		int n = lua_gettop(L);
		float x    = (float)luaL_checknumber(L, 1);
		float xMin = (float)luaL_checknumber(L, 2);
		float xMax = (float)luaL_checknumber(L, 3);
		float yMin = (float)luaL_checknumber(L, 4);
		float yMax = (float)luaL_checknumber(L, 5);
		if (n >= 6) {
			float a = (float)luaL_checknumber(L, 6);
			x = rack::rescale(x, xMin, xMax, 1.f, (float)M_E);
			x = std::exp(std::pow(std::log(x), dsp::exp2_taylor5(a)));
			x = rack::rescale(x, 1.f, (float)M_E, yMin, yMax);
		}
		else {
			x = rack::rescale(x, xMin, xMax, yMin, yMax);
		}
		lua_pushnumber(L, x);
		return 1;
	}

	static int lua_number_toString(lua_State* L) {
		float f = (float)luaL_checknumber(L, 1);
		char buf[32];
		if (std::ceilf(f) == f)
			snprintf(buf, sizeof(buf), "%i", (int)f);
		else
			snprintf(buf, sizeof(buf), "%f", f);
		lua_pushstring(L, buf);
		return 1;
	}

	// ── input.* ───────────────────────────────────────────────────────────────

	static int lua_input_enable(lua_State* L) {
		auto* e = getEngine(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		e->enableInput(i - 1);
		return 0;
	}

	static int lua_input_getVoltage(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i  = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		uint8_t ch = 1;
		if (n >= 2) ch = (uint8_t)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushnumber(L, e->getInputVoltage(i - 1, ch - 1));
		return 1;
	}

	static int lua_input_isHigh(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i  = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		uint8_t ch = 1;
		if (n >= 2) ch = (uint8_t)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->getInputVoltage(i - 1, ch - 1) > 0.7f);
		return 1;
	}

	static int lua_input_isLow(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i  = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->inputCount) luaL_argerror(L, 1, "input index out of range");
		uint8_t ch = 1;
		if (n >= 2) ch = (uint8_t)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->getInputVoltage(i - 1, ch - 1) < 0.7f);
		return 1;
	}

	// ── trig.* ────────────────────────────────────────────────────────────────

	static int lua_trig_getTicks(lua_State* L) {
		auto* e = getEngine(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		lua_pushinteger(L, (lua_Integer)e->getTrigTicks(i - 1));
		return 1;
	}

	static int lua_trig_isHigh(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = (int)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->getTrigVoltage(i - 1, ch - 1) > 0.7f);
		return 1;
	}

	static int lua_trig_isLow(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->inputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = (int)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		lua_pushboolean(L, e->getTrigVoltage(i - 1, ch - 1) < 0.7f);
		return 1;
	}

	static int lua_trig_setGate(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		if (n < 2) luaL_error(L, "trig.setGate: expected (port [,ch], duration)");
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		float duration;
		if (n == 3) {
			ch = (int)luaL_checkinteger(L, 2);
			duration = (float)luaL_checknumber(L, 3);
		}
		else {
			duration = (float)luaL_checknumber(L, 2);
		}
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->setTrig(i - 1, ch - 1, duration);
		return 0;
	}

	static int lua_trig_setHigh(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = (int)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->setTrigVoltage(i - 1, ch - 1, 10.f);
		return 0;
	}

	static int lua_trig_setLow(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = (int)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->setTrigVoltage(i - 1, ch - 1, 0.f);
		return 0;
	}

	static int lua_trig_setTrigger(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->outputTrigCount) luaL_argerror(L, 1, "trig index out of range");
		int ch = 1;
		if (n >= 2) ch = (int)luaL_checkinteger(L, 2);
		if (ch < 1 || ch > PORT_MAX_CHANNELS) luaL_argerror(L, 2, "channel out of range");
		e->setTrig(i - 1, ch - 1);
		return 0;
	}

	// ── param.* ───────────────────────────────────────────────────────────────

	static int lua_param_enable(lua_State* L) {
		auto* e = getEngine(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->paramCount) luaL_argerror(L, 1, "param index out of range");
		e->enableParam(i - 1);
		return 0;
	}

	static int lua_param_getValue(lua_State* L) {
		auto* e = getEngine(L);
		int i = (int)luaL_checkinteger(L, 1);
		if (i < 1 || i > e->paramCount) luaL_argerror(L, 1, "param index out of range");
		lua_pushnumber(L, e->getParamValue(i - 1));
		return 1;
	}

	// ── midi.* ────────────────────────────────────────────────────────────────

	static int lua_midi_create(lua_State* L) {
		auto* e = getEngine(L);
		size_t* s = &e->msgCount;
		if (*s >= (size_t)msgStoreSize)
			luaL_error(L, "midi.create: message store full");
		e->msgStore[*s] = MessageEx();
		lua_pushinteger(L, (lua_Integer)(*s)++);
		return 1;
	}

	static int lua_midi_createNrpn(lua_State* L) {
		auto* e = getEngine(L);
		size_t* s = &e->msgCount;
		if (*s + 4 > (size_t)msgStoreSize)
			luaL_error(L, "midi.createNRPN: message store full");
		e->msgStore[*s + 0] = MessageEx();
		e->msgStore[*s + 0].isNrpn = true;
		e->msgStore[*s + 1] = MessageEx();
		e->msgStore[*s + 2] = MessageEx();
		e->msgStore[*s + 3] = MessageEx();
		lua_Integer idx = (lua_Integer)*s;
		*s += 4;
		lua_pushinteger(L, idx);
		return 1;
	}

	static int lua_midi_getChannel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
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
		uint16_t value = ((uint16_t)m->msg.getValue() << 7) | m->msg.getNote();
		lua_pushinteger(L, value);
		return 1;
	}

	static int lua_midi_getSysExData(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		std::ostringstream ss;
		ss << std::hex;
		for (int i = 1; i < m->msg.getSize() - 1; i++)
			ss << std::setw(2) << std::setfill('0') << static_cast<int>(m->msg.bytes[i]);
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
		MessageEx* m  = getMsg(L, 1);
		uint8_t ch    = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint8_t cc    = (uint8_t)luaL_checkinteger(L, 3);
		int8_t  value = (int8_t)luaL_checkinteger(L, 4);
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xb);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(cc);
		m->msg.setValue(value);
		return 0;
	}

	static int lua_midi_setCc14bit(lua_State* L) {
		// midi.setCc14bit(msg1, msg2, channel, cc, value)
		auto* e      = getEngine(L);
		MessageEx* m1 = getMsg(L, 1);
		int idx2      = (int)luaL_checkinteger(L, 2);
		if (idx2 < 0 || (size_t)idx2 >= e->msgCount) luaL_argerror(L, 2, "invalid msg2 index");
		MessageEx* m2 = &e->msgStore[idx2];
		uint8_t ch    = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 3)));
		uint8_t cc    = (uint8_t)luaL_checkinteger(L, 4);
		double  value = luaL_checknumber(L, 5);
		if (m1->msg.getSize() != 3) m1->msg.setSize(3);
		if (m2->msg.getSize() != 3) m2->msg.setSize(3);
		m1->msg.setStatus(0xb); m2->msg.setStatus(0xb);
		m1->msg.setChannel(ch - 1); m2->msg.setChannel(ch - 1);
		m1->msg.setNote(cc);        m2->msg.setNote(cc + 32);
		m1->msg.setValue((int8_t)value);
		m2->msg.setValue((int8_t)((value - ((int8_t)value)) * 128.f));
		return 0;
	}

	static int lua_midi_setChannel(lua_State* L) {
		MessageEx* m = getMsg(L, 1);
		uint8_t ch   = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		m->msg.setChannel(ch - 1);
		return 0;
	}

	static int lua_midi_setChanPressure(lua_State* L) {
		// midi.setChanPressure(msg, channel, value)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch   = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint8_t val  = (uint8_t)luaL_checkinteger(L, 3);
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xd);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(val);
		return 0;
	}

	static int lua_midi_setKeyPressure(lua_State* L) {
		// midi.setKeyPressure(msg, channel, note, velocity)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch   = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint8_t note = (uint8_t)luaL_checkinteger(L, 3);
		int8_t  vel  = (int8_t)luaL_checkinteger(L, 4);
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xa);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(note);
		m->msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNote(lua_State* L) {
		MessageEx* m  = getMsg(L, 1);
		uint8_t value = (uint8_t)luaL_checkinteger(L, 2);
		m->msg.setNote(value);
		return 0;
	}

	static int lua_midi_setNoteOff(lua_State* L) {
		// midi.setNoteOff(msg, channel, note)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch   = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint8_t note = (uint8_t)luaL_checkinteger(L, 3);
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0x8);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(note);
		m->msg.setValue(0);
		return 0;
	}

	static int lua_midi_setNoteOn(lua_State* L) {
		// midi.setNoteOn(msg, channel, note, velocity)
		MessageEx* m = getMsg(L, 1);
		uint8_t ch   = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint8_t note = (uint8_t)luaL_checkinteger(L, 3);
		int8_t  vel  = (int8_t)luaL_checkinteger(L, 4);
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0x9);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(note);
		m->msg.setValue(vel);
		return 0;
	}

	static int lua_midi_setNrpn(lua_State* L) {
		// midi.setNRPN(nrpn, channel, number, value)
		auto* e       = getEngine(L);
		int idx        = (int)luaL_checkinteger(L, 1);
		if (idx < 0 || (size_t)idx >= e->msgCount) luaL_argerror(L, 1, "invalid nrpn index");
		MessageEx* s1  = &e->msgStore[idx];
		if (!s1->isNrpn) luaL_argerror(L, 1, "message is not an NRPN");
		MessageEx* s2  = &e->msgStore[idx + 1];
		MessageEx* s3  = &e->msgStore[idx + 2];
		MessageEx* s4  = &e->msgStore[idx + 3];

		uint8_t  ch     = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint16_t number = (uint16_t)luaL_checkinteger(L, 3);
		uint16_t value  = (uint16_t)luaL_checkinteger(L, 4);

		s1->msg.setStatus(0xb); s1->msg.setChannel(ch - 1);
		s1->msg.setNote(98);    s1->msg.setValue(number & 0x7f);
		s2->msg.setStatus(0xb); s2->msg.setChannel(ch - 1);
		s2->msg.setNote(99);    s2->msg.setValue((number >> 7) & 0x7f);
		s3->msg.setStatus(0xb); s3->msg.setChannel(ch - 1);
		s3->msg.setNote(38);    s3->msg.setValue(value & 0x7f);
		s4->msg.setStatus(0xb); s4->msg.setChannel(ch - 1);
		s4->msg.setNote(6);     s4->msg.setValue((value >> 7) & 0x7f);
		return 0;
	}

	static int lua_midi_setPitchWheel(lua_State* L) {
		// midi.setPitchWheel(msg, channel, value)
		MessageEx* m  = getMsg(L, 1);
		uint8_t  ch   = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint16_t value = (uint16_t)luaL_checkinteger(L, 3);
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
		uint8_t ch   = (uint8_t)std::max(1, std::min(16, (int)luaL_checkinteger(L, 2)));
		uint8_t prg  = (uint8_t)luaL_checkinteger(L, 3);
		if (m->msg.getSize() != 3) m->msg.setSize(3);
		m->msg.setStatus(0xc);
		m->msg.setChannel(ch - 1);
		m->msg.setNote(prg);
		return 0;
	}

	static int lua_midi_setSysEx(lua_State* L) {
		// midi.setSysEx(msg, hexstring)
		MessageEx* m  = getMsg(L, 1);
		size_t len;
		const char* raw = luaL_checklstring(L, 2, &len);
		std::string data(raw, len);
		if (data.length() % 2 != 0)
			luaL_error(L, "midi.setSysEx: hex string length must be even");
		if (data.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
			luaL_error(L, "midi.setSysEx: invalid hex string");
		m->msg.setSize((int)(data.length() / 2 + 2));
		m->msg.bytes[0] = 0xf0;
		for (size_t i = 0; i < data.length(); i += 2) {
			char byte = (char)strtol(data.substr(i, 2).c_str(), nullptr, 16);
			m->msg.bytes[i / 2 + 1] = byte;
		}
		m->msg.bytes[m->msg.getSize() - 1] = 0xf7;
		return 0;
	}

	static int lua_midi_setValue(lua_State* L) {
		MessageEx* m  = getMsg(L, 1);
		uint8_t value = (uint8_t)luaL_checkinteger(L, 2);
		m->msg.setValue(value);
		return 0;
	}

	// ── midiOut.* ─────────────────────────────────────────────────────────────
	//
	// Overloads:
	//   midiOut.send(msg)               — no midiPort prefix → port 0
	//   midiOut.send(midiPort, msg)     — with midiPort (1-based)
	//
	// The first argument is numeric in both cases. We distinguish by looking at
	// the second argument:
	//   - If arg2 exists and is an integer, arg1 is the midiPort and arg2 is msg.
	//   - Otherwise arg1 is msg.

	// Returns {midiPort (0-based internal), msgIndex} from the optional-port
	// overload, or luaL_error on bad args.
	struct PortMsg { int port; MessageEx* m; };
	static PortMsg getPortMsg(lua_State* L) {
		auto* e = getEngine(L);
		int n = lua_gettop(L);
		if (n < 1) luaL_error(L, "midiOut: too few arguments");

		// Detect whether first arg is port or msg:
		// If n==1 → only msg  (no port given)
		// If n==2 and second arg is integer → midiPort, msg
		// If n==2 and second arg is NOT integer → msg, extra-arg (handled upstream)
		bool hasPort = (n >= 2 && (lua_isinteger(L, 1) || lua_isnumber(L, 1)) &&
		                           (lua_isinteger(L, 2) || lua_isnumber(L, 2)));
		int portArg  = 0;
		int msgArg   = 1;
		if (hasPort) { portArg = 1; msgArg = 2; }

		int port = 0;
		if (hasPort) {
			port = (int)lua_tointeger(L, portArg);
			if (port < 1 || port > e->midiOutputCount)
				luaL_error(L, "midiOut: invalid output port index");
		}

		int idx = (int)luaL_checkinteger(L, msgArg);
		if (idx < 0 || (size_t)idx >= e->msgCount)
			luaL_error(L, "midiOut: invalid message index");

		e->msgStore[idx].midiPort = port; // 0 means "port 0" internally
		return { port, &e->msgStore[idx] };
	}

	static int lua_midiOut_send(lua_State* L) {
		// midiOut.send([midiPort,] msg)
		auto pm = getPortMsg(L);
		pm.m->send = true;
		pm.m->msg.frame = -1;
		pm.m->tick = 0;
		return 0;
	}

	static int lua_midiOut_sendAfterMs(lua_State* L) {
		// midiOut.sendAfterMs([midiPort,] msg, ms)
		// Extra arg 'ms' is always the last argument.
		int n = lua_gettop(L);
		float ms = (float)luaL_checknumber(L, n);
		lua_pop(L, 1);   // pop ms; now stack matches the no-extra overload

		auto pm = getPortMsg(L);
		int64_t currentFrame = APP->engine->getFrame();
		int64_t frame = (int64_t)(ms / 1000.f / APP->engine->getSampleTime());
		pm.m->send = true;
		pm.m->msg.frame = currentFrame + frame;
		pm.m->tick = 0;
		return 0;
	}

	static int lua_midiOut_sendAfterTrigger(lua_State* L) {
		// midiOut.sendAfterTrigger([midiPort,] msg [, trigPort], ticks)
		//
		// Disambiguate:
		//   2 args: msg, ticks                     (no port, no trigPort)
		//   3 args: midiPort, msg, ticks            (port, no trigPort)
		//        or msg, trigPort, ticks            (no port, trigPort) — both 2nd/3rd are ints
		//   4 args: midiPort, msg, trigPort, ticks  (port + trigPort)
		//
		// Strategy: always last arg = ticks, second-to-last = trigPort or msg depending on count.

		auto* e = getEngine(L);
		int n = lua_gettop(L);

		int ticks    = (int)luaL_checkinteger(L, n);
		int trigPort = 0;  // 0 = use trig port 0

		if (n == 4) {
			// midiPort, msg, trigPort, ticks
			trigPort = (int)luaL_checkinteger(L, n - 1);
			lua_pop(L, 2);
		}
		else if (n == 3) {
			// Could be either (midiPort, msg, ticks) or (msg, trigPort, ticks).
			// If arg1 is clearly a port (1-based, small) and arg2 is a msg index, assume former.
			// We just assume (msg, trigPort, ticks) when there are 3 args.
			trigPort = (int)luaL_checkinteger(L, n - 1);
			lua_pop(L, 2);
		}
		else {
			lua_pop(L, 1); // pop ticks
		}

		if (trigPort < 0 || trigPort > e->inputTrigCount)
			luaL_error(L, "midiOut.sendAfterTrigger: invalid trig port index");

		auto pm = getPortMsg(L);
		int64_t currentTicks = e->getTrigTicks(trigPort == 0 ? 0 : trigPort - 1);
		pm.m->send  = true;
		pm.m->msg.frame = -1;
		pm.m->tick  = currentTicks + ticks;
		return 0;
	}
};

// ─── Static member definitions ────────────────────────────────────────────────

std::weak_ptr<TaskWorker> MidiScriptEngineLua::taskWorkerWeakPtr;

} // namespace Lua
} // namespace MidiScript
} // namespace StoermelderPackOne
