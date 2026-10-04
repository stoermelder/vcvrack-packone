// The script's view of the module: inputs, trigger in/out, params, MIDI output port selection and scheduled sends.
//
// Part of the cross-engine suite: the shared helpers (run, requireEquivalent, EngineRun, ...)
// are in MidiKit.test.hpp.

// input.enable
// input.enable flips a flag on the module's own inputInfos, which is plain
// module state, not engine-internal — comparable directly without a log
// probe.

static const char* JS_INPUT_ENABLE = R"(/**
 * @engine QuickJs@v1
 */
input.enable(1);
input.enable(2);
)";

static const char* LUA_INPUT_ENABLE = R"(--[[
@engine minilua@v1
--]]
input.enable(1)
input.enable(2)
)";

TEST_CASE("input.enable sets identical module state in both engines", "[MidiKit][CrossEngine]") {
	auto checkEnabled = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		using StoermelderPackOne::MidiScript::ScriptPortInfo;
		bool enabled[4];
		for (int i = 0; i < 4; i++) {
			enabled[i] = reinterpret_cast<ScriptPortInfo*>(m->inputInfos[i])->enabled;
		}
		return std::vector<bool>(enabled, enabled + 4);
	};
	std::vector<bool> expected = {true, true, false, false};
	REQUIRE(checkEnabled(JS_INPUT_ENABLE) == expected);
	REQUIRE(checkEnabled(LUA_INPUT_ENABLE) == expected);
}


// input.getVoltage / input.isHigh / input.isLow

static const char* JS_INPUT_GET_VOLTAGE = R"(/**
 * @engine QuickJs@v1
 */
input.enable(1);
rack.log("PROBE:" + number.toString(input.getVoltage(1)));
rack.log("PROBE:" + number.toString(input.getVoltage(1, 1)));
rack.log("PROBE:" + (input.isHigh(1) ? "high" : "low"));
rack.log("PROBE:" + (input.isLow(1) ? "low" : "high"));
)";

static const char* LUA_INPUT_GET_VOLTAGE = R"(--[[
@engine minilua@v1
--]]
input.enable(1)
rack.log("PROBE:" .. number.toString(input.getVoltage(1)))
rack.log("PROBE:" .. number.toString(input.getVoltage(1, 1)))
rack.log("PROBE:" .. (input.isHigh(1) and "high" or "low"))
rack.log("PROBE:" .. (input.isLow(1) and "low" or "high"))
)";

TEST_CASE("input.getVoltage/isHigh/isLow read identical default state", "[MidiKit][CrossEngine]") {
	// Default (unpatched) input reads 0V, which is "low" and not "high".
	requireLoggedValues(JS_INPUT_GET_VOLTAGE, LUA_INPUT_GET_VOLTAGE, {"0", "0", "low", "low"});
}


// trig.getTicks
// getTicks depends on rising edges processed through Module::process(), so
// this drives the module directly rather than going through the probe-log
// helpers — the shape mirrors the per-engine "API trig.getTicks" case, just
// asserting the same sequence on both engines instead of one at a time.


static const char* JS_TRIG_GET_TICKS_CHANNEL = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(1, 2);
midi.onMessage = function(port, msg) {
    let a = midi.create();
    midi.setCc(a, 1, 1, 0);
    midi.setValue(a, trig.getTicks(1, 1));
    midiOut.send(a);
    let b = midi.create();
    midi.setCc(b, 1, 2, 0);
    midi.setValue(b, trig.getTicks(1, 2));
    midiOut.send(b);
};
)";

static const char* LUA_TRIG_GET_TICKS_CHANNEL = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(1, 2)
midi.onMessage = function(midiPort, msg)
    local a = midi.create()
    midi.setCc(a, 1, 1, 0)
    midi.setValue(a, trig.getTicks(1, 1))
    midiOut.send(a)
    local b = midi.create()
    midi.setCc(b, 1, 2, 0)
    midi.setValue(b, trig.getTicks(1, 2))
    midiOut.send(b)
end
)";

TEST_CASE("trig.getTicks(1, channel) counts each channel independently, in both engines", "[MidiKit][CrossEngine]") {
	auto ticksPerChannel = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;
		// Channel 1 gets two rising edges, channel 2 gets three.
		auto drive = [&](int frame, float v0, float v1) {
			m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(v0, 0);
			m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(v1, 1);
			args.frame = frame;
			m->process(args);
		};
		drive(0, 0.f, 0.f);   // prime both low
		drive(1, 10.f, 0.f);  // ch1 edge
		drive(2, 0.f, 0.f);
		drive(3, 10.f, 0.f);  // ch1 edge
		drive(4, 10.f, 10.f); // ch2 edge (ch1 stays high)
		drive(5, 0.f, 10.f);
		drive(6, 0.f, 0.f);
		drive(7, 0.f, 10.f);  // ch2 edge
		drive(8, 0.f, 0.f);
		drive(9, 0.f, 10.f);  // ch2 edge

		midi::Message in;
		in.setSize(3);
		in.setStatus(0x9);
		m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
		m->host.getActiveEngine()->process();

		int port, ticks;
		midi::Message out;
		std::vector<int> values;
		while (processOutMessage(m, port, out, ticks)) {
			values.push_back(out.getValue());
		}
		return values;
	};

	REQUIRE(ticksPerChannel(JS_TRIG_GET_TICKS_CHANNEL) == std::vector<int>{2, 3});
	REQUIRE(ticksPerChannel(LUA_TRIG_GET_TICKS_CHANNEL) == std::vector<int>{2, 3});
}


// trig.setGate / setHigh / setLow / setTrigger
// These write to the module's own trigger-output state, which is plain
// module state comparable directly across engines.

static const char* JS_TRIG_SET_FUNCTIONS = R"(/**
 * @engine QuickJs@v1
 */
trig.setGate(1, 100);
trig.setHigh(1);
trig.setLow(1);
trig.setTrigger(1);
)";

static const char* LUA_TRIG_SET_FUNCTIONS = R"(--[[
@engine minilua@v1
--]]
trig.setGate(1, 100)
trig.setHigh(1)
trig.setLow(1)
trig.setTrigger(1)
)";

TEST_CASE("trig.setTrigger produces identical output-trigger state", "[MidiKit][CrossEngine]") {
	auto checkTriggerActive = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);

		// process() only writes the trigger output voltage while a cable is
		// connected (see the isConnected() gate in MidiKitModule::process),
		// so simulate one — otherwise the pulse generator's 10V pulse is
		// never written to the port and the voltage reads 0.
		m->outputs[MidiKitModule::OUTPUT_TRIG].channels = 1;

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;
		args.frame = 0;
		m->process(args);

		float voltage = m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0);
		bool active = m->triggerOuts.triggerActive[0][0];
		return std::make_pair(voltage, active);
	};

	auto js = checkTriggerActive(JS_TRIG_SET_FUNCTIONS);
	auto lua = checkTriggerActive(LUA_TRIG_SET_FUNCTIONS);
	REQUIRE(js.first == Catch::Approx(10.0f));
	REQUIRE(js.second == true);
	REQUIRE(js == lua);
}

// Pins the ms contract of trig.setGate: the docs specify durationMs, so a
// 100 ms gate must fall after ~4410 samples at 44.1 kHz. Dedicated scripts
// (setGate only — setHigh would clear triggerActive and pin the output high).
static const char* JS_TRIG_SET_GATE_MS = R"(/**
 * @engine QuickJs@v1
 */
trig.setGate(1, 100);
)";

static const char* LUA_TRIG_SET_GATE_MS = R"(--[[
@engine minilua@v1
--]]
trig.setGate(1, 100)
)";

TEST_CASE("trig.setGate duration is milliseconds: gate falls after ~100 ms", "[MidiKit][CrossEngine]") {
	auto checkGateLength = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);

		// process() only writes the trigger output voltage while a cable is
		// connected (see the isConnected() gate in MidiKitModule::process).
		m->outputs[MidiKitModule::OUTPUT_TRIG].channels = 1;

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;
		args.frame = 0;

		// Count the samples the gate is high (10V).
		int highSamples = 0;
		for (int i = 0; i < 5000; i++) {
			m->process(args);
			if (m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0) > 5.f)
				highSamples++;
		}

		return highSamples;
	};

	auto js = checkGateLength(JS_TRIG_SET_GATE_MS);
	auto lua = checkGateLength(LUA_TRIG_SET_GATE_MS);
	// 100 ms @ 44.1 kHz = 4410 samples. The window guards float rounding at the
	// exact boundary while still failing the old seconds interpretation (which
	// would still be high at sample 5000).
	REQUIRE(js > 4300);
	REQUIRE(js < 4500);
	REQUIRE(js == lua);
}


// param.enable

static const char* JS_PARAM_ENABLE = R"(/**
 * @engine QuickJs@v1
 */
param.enable(1);
param.enable(3);
)";

static const char* LUA_PARAM_ENABLE = R"(--[[
@engine minilua@v1
--]]
param.enable(1)
param.enable(3)
)";

TEST_CASE("param.enable sets identical module state in both engines", "[MidiKit][CrossEngine]") {
	auto checkEnabled = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		using StoermelderPackOne::MidiScript::ScriptParamQuantity;
		bool enabled[4];
		for (int i = 0; i < 4; i++) {
			enabled[i] = reinterpret_cast<ScriptParamQuantity*>(m->paramQuantities[i])->enabled;
		}
		return std::vector<bool>(enabled, enabled + 4);
	};
	std::vector<bool> expected = {true, false, true, false};
	REQUIRE(checkEnabled(JS_PARAM_ENABLE) == expected);
	REQUIRE(checkEnabled(LUA_PARAM_ENABLE) == expected);
}


// param.getValue

static const char* JS_PARAM_GET_VALUE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    param.enable(1);
    let out = midi.create();
    midi.setCc(out, 1, 1, 0);
    midi.setValue(out, Math.floor(param.getValue(1) * 127));
    midiOut.send(out);
};
)";

static const char* LUA_PARAM_GET_VALUE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    param.enable(1)
    local out = midi.create()
    midi.setCc(out, 1, 1, 0)
    midi.setValue(out, math.floor(param.getValue(1) * 127))
    midiOut.send(out)
end
)";

TEST_CASE("param.getValue reads identical value in both engines", "[MidiKit][CrossEngine]") {
	auto valueAt = [](const std::string& script, float paramValue) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		m->params[MidiKitModule::PARAM + 0].setValue(paramValue);

		midi::Message in;
		in.setSize(3);
		in.setStatus(0x9);
		m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
		m->host.getActiveEngine()->process();

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		int result = out.getValue();
		return result;
	};

	// floor(value * 127)
	REQUIRE(valueAt(JS_PARAM_GET_VALUE, 0.5f) == 63);
	REQUIRE(valueAt(LUA_PARAM_GET_VALUE, 0.5f) == 63);
	REQUIRE(valueAt(JS_PARAM_GET_VALUE, 1.0f) == 127);
	REQUIRE(valueAt(LUA_PARAM_GET_VALUE, 1.0f) == 127);
}


// midiOut.selectPort

static const char* JS_SELECT_PORT = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
midi.onMessage = function(port, msg) {
    midiOut.selectPort(2);
    midiOut.send(msg);
};
)";

static const char* LUA_SELECT_PORT = R"(--[[
@engine minilua@v1
--]]
midiOut.enablePorts(2)
midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(2)
    midiOut.send(msg)
end
)";

TEST_CASE("midiOut.selectPort produces identical output port in both engines", "[MidiKit][CrossEngine]") {
	// Ports are 1-based in the script: selectPort(2) is output index 1. The default input is a Note-On on channel index 1.
	Both b = runBoth(Pair{JS_SELECT_PORT, LUA_SELECT_PORT});
	requireBytes(b, {{0x91, 60, 100}});
	requirePorts(b, {1});
}


static const char* JS_SELECT_PORT_STICKY = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
midi.onMessage = function(port, msg) {
    midiOut.selectPort(2);
    let msg1 = midi.create();
    midi.setNoteOn(msg1, 1, 60, 100);
    let msg2 = midi.create();
    midi.setNoteOn(msg2, 1, 61, 100);
    midiOut.send(msg1);
    midiOut.send(msg2);
};
)";

static const char* LUA_SELECT_PORT_STICKY = R"(--[[
@engine minilua@v1
--]]
midiOut.enablePorts(2)
midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(2)
    local msg1 = midi.create()
    midi.setNoteOn(msg1, 1, 60, 100)
    local msg2 = midi.create()
    midi.setNoteOn(msg2, 1, 61, 100)
    midiOut.send(msg1)
    midiOut.send(msg2)
end
)";

TEST_CASE("midiOut.selectPort stays selected across calls identically", "[MidiKit][CrossEngine]") {
	Both b = runBoth(Pair{JS_SELECT_PORT_STICKY, LUA_SELECT_PORT_STICKY});
	requireBytes(b, {{0x90, 60, 100}, {0x90, 61, 100}});
	requirePorts(b, {1, 1});
}


static const char* JS_SELECT_PORT_INVALID = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.selectPort(5);
};
)";

static const char* LUA_SELECT_PORT_INVALID = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(5)
end
)";

TEST_CASE("midiOut.selectPort rejects an out-of-range port identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_SELECT_PORT_INVALID, LUA_SELECT_PORT_INVALID, "onMessage error", true);
}


// A handle that holds no message cannot be sent
// An empty midi.create() handle and an unset NRPN/14-bit group have no status
// byte, so every send call raises instead of putting bare data bytes on the wire.

TEST_CASE("Sending an empty or unset handle raises, whichever send call is used", "[MidiKit][CrossEngine]") {
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	const char* calls[] = { "midiOut.send(%s)", "midiOut.sendAfterMs(%s, 10)", "midiOut.sendAtFrame(%s, 1000)", "midiOut.sendAfterTrigger(%s, 1)" };
	const char* handles[] = { "midi.create()", "midi.createNRPN()", "midi.createCc14bit()" };

	std::string body;
	int attempts = 0;
	for (const char* call : calls) {
		for (const char* handle : handles) {
			char stmt[128];
			snprintf(stmt, sizeof(stmt), call, handle);
			attempts++;
			if (lua) body += std::string("    t(function() ") + stmt + " end)\n";
			else body += std::string("    t(function() { ") + stmt + "; });\n";
		}
	}
	std::string src = lua
		? script(lang, "local function t(f) local ok, err = pcall(f) rack.log(ok and 'ok' or ('err ' .. tostring(err))) end\n"
			"midi.onMessage = function(port, msg)\n" + body + "    midiOut.send(msg)\nend")
		: script(lang, "function t(f) { try { f(); rack.log('ok'); } catch (e) { rack.log('err ' + e); } }\n"
			"midi.onMessage = function(port, msg) {\n" + body + "    midiOut.send(msg);\n};");
	Kit<> kit;
	std::string loadLog = kit.loadRaw(src);
	CATCH_INFO("load log:\n" << loadLog);
	std::vector<Out> sent = kit.dispatch(msg::noteOn(1, 60, 100));
	std::string log = kit.log();
	CATCH_INFO("log:\n" << log);

	REQUIRE(countOf(log, "has no status byte") == size_t(attempts));
	REQUIRE(log.find("ok") == std::string::npos);
	// Only the control send of the incoming note went out.
	REQUIRE(sent.size() == 1);
	REQUIRE(sent[0].bytes == std::vector<uint8_t>{0x91, 60, 100});
}


// Scheduling limits
// sendAfterMs and sendAtFrame reach at most 2 hours ahead, sendAfterTrigger at
// most 10000 trigger ticks; larger finite values are clamped. NaN and Infinity
// raise (a 60000 / bpm with bpm = 0 gives Infinity), instead of leaving the
// message queued for good or sending it at once.

static std::string schedulingScript(Lang lang, const std::string& call, const std::string& arg) {
	if (lang == Lang::Js) {
		return script(lang, "midi.onMessage = function(port, msg) {\n    try { " + call + "(msg, " + arg + "); } catch (e) { rack.log('err ' + e); }\n};");
	}
	return script(lang, "midi.onMessage = function(port, msg)\n    local ok, err = pcall(" + call + ", msg, " + arg + ")\n    if not ok then rack.log('err ' .. tostring(err)) end\nend");
}

TEST_CASE("Non-finite scheduling arguments raise and send nothing", "[MidiKit][CrossEngine]") {
	FOR_EACH_LANG;
	const char* calls[] = { "midiOut.sendAfterMs", "midiOut.sendAtFrame", "midiOut.sendAfterTrigger" };
	for (const char* call : calls) {
		for (const char* arg : { "0 / 0", "1 / 0", "-1 / 0" }) {   // NaN, Infinity, -Infinity
			CATCH_INFO(call << " " << arg);
			Kit<> kit;
			kit.loadRaw(schedulingScript(lang, call, arg));
			std::vector<Out> sent = kit.dispatch(msg::noteOn(1, 60, 100));
			std::string log = kit.log();
			CATCH_INFO("log:\n" << log);
			// The Lua sendAfterTrigger refuses non-integers with its own wording.
			REQUIRE(log.find("err ") != std::string::npos);
			REQUIRE(sent.empty());
		}
	}
}

TEST_CASE("Huge scheduling arguments are limited to 2 hours and 10000 ticks", "[MidiKit][CrossEngine]") {
	FOR_EACH_LANG;
	const int64_t limit = int64_t(7200.0 * Test::sampleRate());   // 2 hours of frames
	REQUIRE(limit == 317520000);

	SECTION("sendAfterMs") {
		Kit<> kit;
		kit.loadRaw(schedulingScript(lang, "midiOut.sendAfterMs", "1e300"));
		std::vector<Out> sent = kit.dispatch(msg::noteOn(1, 60, 100));
		REQUIRE(sent.size() == 1);
		REQUIRE(sent[0].frame == limit);
	}
	SECTION("sendAfterMs just inside the limit is not changed") {
		Kit<> kit;
		kit.loadRaw(schedulingScript(lang, "midiOut.sendAfterMs", "7200000"));
		std::vector<Out> sent = kit.dispatch(msg::noteOn(1, 60, 100));
		REQUIRE(sent.size() == 1);
		REQUIRE(sent[0].frame == limit);
	}
	SECTION("sendAtFrame") {
		Kit<> kit;
		kit.loadRaw(schedulingScript(lang, "midiOut.sendAtFrame", "1e300"));
		std::vector<Out> sent = kit.dispatch(msg::noteOn(1, 60, 100));
		REQUIRE(sent.size() == 1);
		REQUIRE(sent[0].frame == limit);
	}
	SECTION("sendAfterTrigger") {
		Kit<> kit;
		kit.loadRaw(schedulingScript(lang, "midiOut.sendAfterTrigger", "1000000000"));
		std::vector<Out> sent = kit.dispatch(msg::noteOn(1, 60, 100));
		REQUIRE(sent.size() == 1);
		REQUIRE(sent[0].ticks == 10000);
	}
}


// midiOut.sendAfterMs

static const char* JS_SEND_AFTER_MS = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, 100);
};
)";

static const char* LUA_SEND_AFTER_MS = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    midiOut.sendAfterMs(msg, 100)
end
)";

TEST_CASE("midiOut.sendAfterMs schedules the message 100 ms after the event, in both engines", "[MidiKit][CrossEngine]") {
	Both b = runBoth(Pair{JS_SEND_AFTER_MS, LUA_SEND_AFTER_MS});
	requireBytes(b, {{0x91, 60, 100}});
	// The input carries frame 0, so the message is due 100 ms of samples later.
	const int64_t due = int64_t(0.100 * Test::sampleRate());
	REQUIRE(due == 4410);
	REQUIRE(b.js.sent[0].frame == due);
	REQUIRE(b.lua.sent[0].frame == due);
}
// trig.onTrigger dispatch and the context-menu API.
//
// Part of the cross-engine suite: the shared helpers (run, requireEquivalent, EngineRun, ...)
// are in MidiKit.test.hpp.

static const char* JS_ON_TRIGGER = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
trig.onTrigger = function(trigPort) {
    rack.log("onTrigger " + number.toString(trigPort));
    let msg = midi.create();
    midi.setCc(msg, 1, 10, trigPort);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_TRIGGER = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
function trig.onTrigger(trigPort)
    rack.log("onTrigger " .. trigPort)
    local msg = midi.create()
    midi.setCc(msg, 1, 10, trigPort)
    midiOut.send(msg)
end
)";

TEST_CASE("onTrigger fires on a trigger input tick and sends an identical message in both engines", "[MidiKit][CrossEngine]") {
	auto checkOnTrigger = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);

		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		REQUIRE(log.find("onTrigger 1") != std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		auto sent = Out::of(out, port, ticks);
		return sent;
	};

	auto js = checkOnTrigger(JS_ON_TRIGGER);
	auto lua = checkOnTrigger(LUA_ON_TRIGGER);
	// CC 10 carrying the trigger port (1).
	REQUIRE(js.port == 0);
	REQUIRE(lua.port == 0);
	REQUIRE(js.bytes == std::vector<uint8_t>{0xb0, 10, 1});
	REQUIRE(lua.bytes == std::vector<uint8_t>{0xb0, 10, 1});
}


static const char* JS_ON_TRIGGER_CHANNEL = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(1, 2);
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger " + number.toString(trigPort) + " " + number.toString(channel));
};
)";

static const char* LUA_ON_TRIGGER_CHANNEL = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(1, 2)
function trig.onTrigger(trigPort, channel)
    rack.log("onTrigger " .. trigPort .. " " .. channel)
end
)";

TEST_CASE("onTrigger receives the firing channel, in both engines", "[MidiKit][CrossEngine]") {
	auto logChannels = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);

		// Channels are 1-based in the callback: index 0 -> "1", index 1 -> "2".
		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();
		m->host.getActiveEngine()->processInTick(0, 1);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		return log;
	};

	auto js = logChannels(JS_ON_TRIGGER_CHANNEL);
	auto lua = logChannels(LUA_ON_TRIGGER_CHANNEL);
	REQUIRE(js.find("onTrigger 1 1") != std::string::npos);
	REQUIRE(js.find("onTrigger 1 2") != std::string::npos);
	REQUIRE(lua.find("onTrigger 1 1") != std::string::npos);
	REQUIRE(lua.find("onTrigger 1 2") != std::string::npos);
}


TEST_CASE("Script without onTrigger silently ignores trigger ticks, in both engines", "[MidiKit][CrossEngine]") {
	auto checkNoOnTrigger = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);

		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		int port, ticks;
		midi::Message out;
		bool sentAnything = processOutMessage(m, port, out, ticks);
		return std::make_pair(log, sentAnything);
	};

	auto js = checkNoOnTrigger(JS_NO_ON_LOAD);
	auto lua = checkNoOnTrigger(LUA_NO_ON_LOAD);
	REQUIRE(js.first.empty());
	REQUIRE(lua.first.empty());
	REQUIRE(js.second == false);
	REQUIRE(lua.second == false);
}


static const char* JS_ON_TRIGGER_NOT_ENABLED = R"(/**
 * @engine QuickJs@v1
 */
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger fired");
};
)";

static const char* LUA_ON_TRIGGER_NOT_ENABLED = R"(--[[
@engine minilua@v1
--]]
trig.onTrigger = function(trigPort, channel)
    rack.log("onTrigger fired")
end
)";

TEST_CASE("trig.onTrigger is not called until trig.enableIn() is used, in both engines", "[MidiKit][CrossEngine]") {
	// trig.onTrigger is unused until the channel is enabled with trig.enableIn().
	auto checkNotEnabled = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
		// Without trig.enableIn(), a rising edge is not processed at all.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		m->process(Test::makeProcessArgs(0));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		m->process(Test::makeProcessArgs(1));
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		return log;
	};

	REQUIRE(checkNotEnabled(JS_ON_TRIGGER_NOT_ENABLED).find("onTrigger") == std::string::npos);
	REQUIRE(checkNotEnabled(LUA_ON_TRIGGER_NOT_ENABLED).find("onTrigger") == std::string::npos);
}


static const char* JS_ON_TRIGGER_ENABLE_CH1_ONLY = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger " + number.toString(channel));
};
)";

static const char* LUA_ON_TRIGGER_ENABLE_CH1_ONLY = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.onTrigger = function(trigPort, channel)
    rack.log("onTrigger " .. channel)
end
)";

TEST_CASE("trig.enableIn gates trig.onTrigger per channel, in both engines", "[MidiKit][CrossEngine]") {
	// Only channel 1 is enabled: a rising edge on channel 1 fires the callback,
	// a rising edge on channel 2 (never enabled) is not processed at all.
	auto checkPerChannel = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;
		// Prime both SchmittTriggers LOW first (a fresh trigger starts
		// uninitialized; the first low call locks it so a later rise is real).
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
		m->process(Test::makeProcessArgs(0));

		// Rising edge on channel 1 (enabled) fires the callback.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
		m->process(Test::makeProcessArgs(1));
		m->host.getActiveEngine()->process();
		std::string log1 = drainLog(m);

		// Rising edge on channel 2 (never enabled) is ignored.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
		m->process(Test::makeProcessArgs(2));
		m->host.getActiveEngine()->process();
		std::string log2 = drainLog(m);

		return std::make_pair(log1, log2);
	};

	auto js = checkPerChannel(JS_ON_TRIGGER_ENABLE_CH1_ONLY);
	auto lua = checkPerChannel(LUA_ON_TRIGGER_ENABLE_CH1_ONLY);
	REQUIRE(js.first.find("onTrigger 1") != std::string::npos);
	REQUIRE(js.second.find("onTrigger") == std::string::npos);
	REQUIRE(lua.first.find("onTrigger 1") != std::string::npos);
	REQUIRE(lua.second.find("onTrigger") == std::string::npos);
}


// send() order, not handle-creation order
// Regression test for the send-order bug: the engine used to push the
// out-queue in msgStore index (handle-creation) order, so a script that
// created several messages and then sent them in a different order had them
// reordered on the wire. The receiver must observe send() order. This
// creates A, B, C (handle order) but sends C, A, B, and asserts the wire
// order is C, A, B in both engines.

static const char* JS_SEND_ORDER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let a = midi.create();
    midi.setNoteOn(a, 1, 60, 100);
    let b = midi.create();
    midi.setNoteOn(b, 1, 62, 100);
    let c = midi.create();
    midi.setNoteOn(c, 1, 64, 100);
    midiOut.send(c);   // handle 2 sent first
    midiOut.send(a);   // handle 0 sent second
    midiOut.send(b);   // handle 1 sent last
};
)";

static const char* LUA_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local a = midi.create()
    midi.setNoteOn(a, 1, 60, 100)
    local b = midi.create()
    midi.setNoteOn(b, 1, 62, 100)
    local c = midi.create()
    midi.setNoteOn(c, 1, 64, 100)
    midiOut.send(c)
    midiOut.send(a)
    midiOut.send(b)
end
)";

TEST_CASE("out-queue is in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
	EngineRun js = run(JS_SEND_ORDER);
	EngineRun lua = run(LUA_SEND_ORDER);

	// Handle order would be 60, 62, 64; send() order is 64, 60, 62. The
	// script's channel argument is 1-based, so channel 1 = internal channel 0
	// = status nibble 0x9 | 0 = 0x90.
	std::vector<uint8_t> expectC = {0x90, 64, 100};
	std::vector<uint8_t> expectA = {0x90, 60, 100};
	std::vector<uint8_t> expectB = {0x90, 62, 100};

	REQUIRE(js.sent.size() == 3);
	REQUIRE(js.sent[0].bytes == expectC);
	REQUIRE(js.sent[1].bytes == expectA);
	REQUIRE(js.sent[2].bytes == expectB);

	REQUIRE(lua.sent.size() == 3);
	REQUIRE(lua.sent[0].bytes == expectC);
	REQUIRE(lua.sent[1].bytes == expectA);
	REQUIRE(lua.sent[2].bytes == expectB);
}
