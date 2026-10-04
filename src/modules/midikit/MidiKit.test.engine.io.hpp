// The script's view of the module: inputs, trigger in/out, params, MIDI output port selection and scheduled sends.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.

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
	ModuleScaffold mods;
	auto checkEnabled = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		using StoermelderPackOne::MidiScript::ScriptPortInfo;
		bool enabled[4];
		for (int i = 0; i < 4; i++) {
			enabled[i] = reinterpret_cast<ScriptPortInfo*>(m->inputInfos[i])->enabled;
		}
		Test::destroyModule(m);
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
	ModuleScaffold mods;
	auto ticksPerChannel = [](const std::string& script) {
		MidiKitModule* m = createModule();
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
		Test::destroyModule(m);
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
	ModuleScaffold mods;
	auto checkTriggerActive = [](const std::string& script) {
		MidiKitModule* m = createModule();
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
		Test::destroyModule(m);
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
	ModuleScaffold mods;
	auto checkGateLength = [](const std::string& script) {
		MidiKitModule* m = createModule();
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

		Test::destroyModule(m);
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
	ModuleScaffold mods;
	auto checkEnabled = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		using StoermelderPackOne::MidiScript::ScriptParamQuantity;
		bool enabled[4];
		for (int i = 0; i < 4; i++) {
			enabled[i] = reinterpret_cast<ScriptParamQuantity*>(m->paramQuantities[i])->enabled;
		}
		Test::destroyModule(m);
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
	ModuleScaffold mods;
	auto valueAt = [](const std::string& script, float paramValue) {
		MidiKitModule* m = createModule();
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
		Test::destroyModule(m);
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

