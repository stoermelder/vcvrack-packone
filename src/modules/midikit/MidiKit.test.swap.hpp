#pragma once
#include "MidiKit.test.hpp"

// Script swap
// A load, reload or clear is one worker task: the outgoing script's onUnload()
// with everything it set up, endUnload(), then the new script. The audio
// thread does its half (syncScriptGen()) once it sees the new generation, and
// whatever the replaced script handed it is told apart by that generation, not
// by when either thread gets there. These pin the order and what is dropped.

// Records what reaches the MIDI output device.
struct SwapRecorder : midi::OutputDevice {
	std::vector<midi::Message> sent;
	void sendMessage(const midi::Message& msg) override {
		sent.push_back(msg);
	}
	// Notes of the sent messages with `status`, in send order.
	std::vector<int> notes(uint8_t status) const {
		std::vector<int> out;
		for (const midi::Message& msg : sent) {
			if (msg.getStatus() == status) out.push_back(msg.getNote());
		}
		return out;
	}
};

struct SwapRig {
	ModuleScaffold mods;
	MidiKitModule* m;
	SwapRecorder rec;
	int64_t frame = 1;

	SwapRig() {
		m = mods.create();
		m->midiOuts.ports[0].outputDevice = &rec;
		m->midiOuts.ports[0].channel = -1;
		m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
		m->outputs[MidiKitModule::OUTPUT_TRIG].channels = 1;
	}

	~SwapRig() {
		// The recorder dies before `mods` destroys the module.
		m->midiOuts.ports[0].outputDevice = nullptr;
	}

	float trigOut() const {
		return m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0);
	}

	// Runs `n` samples; false if trigger output 1 was ever non-zero.
	bool step(int n = 1) {
		bool low = true;
		for (int i = 0; i < n; i++) {
			m->process(Test::makeProcessArgs(frame++));
			if (trigOut() != 0.f) low = false;
		}
		return low;
	}

	// One rising edge on trigger input 1, over two samples.
	bool pulse() {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		bool low = step();
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		return step() && low;
	}
};

static const char* SWAP_JS_PLAIN = R"(/**
 * @engine QuickJs@v1
 */
)";

static const char* SWAP_LUA_PLAIN = R"(--[[
@engine minilua@v1
--]]
)";


static const char* SWAP_JS_UNLOAD_TICKS = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
rack.onUnload = function() {
    rack.log("ticks:" + trig.getTicks(1));
};
)";

static const char* SWAP_LUA_UNLOAD_TICKS = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
rack.onUnload = function()
    rack.log(string.format("ticks:%d", trig.getTicks(1)))
end
)";

TEST_CASE("Swap: the outgoing onUnload() still sees the ticks its script counted", "[MidiKit][Swap]") {
	// The tick counters used to be reset on the UI thread before the swap, so
	// onUnload() read 0. They now restart on the audio thread, after it.
	const char* script = GENERATE(SWAP_JS_UNLOAD_TICKS, SWAP_LUA_UNLOAD_TICKS);
	// Reload into the same engine, switch engines, clear.
	std::string next = GENERATE(as<std::string>{}, "same", "other", "");
	CATCH_INFO("next: " << next);
	std::string nextScript = next;
	if (next == "same") nextScript = script;
	else if (next == "other") nextScript = (script == SWAP_JS_UNLOAD_TICKS) ? SWAP_LUA_PLAIN : SWAP_JS_PLAIN;

	SwapRig r;
	r.m->loadScript(script);
	r.step();
	for (int i = 0; i < 3; i++) r.pulse();
	REQUIRE(r.m->triggerIns.triggerTick[0][0] == 3);
	drainLog(r.m);

	r.m->loadScript(nextScript);
	REQUIRE(drainLog(r.m).find("ticks:3") != std::string::npos);

	// The new script's counters start at 0.
	r.step();
	REQUIRE(r.m->triggerIns.triggerTick[0][0] == 0);
}


TEST_CASE("Swap: the Tipsy input claim is released with the script", "[MidiKit][Swap][Tipsy]") {
	SwapRig r;
	r.m->loadScript(R"(/**
 * @engine QuickJs@v1
 */
trig.enableTipsyIn();
)");
	REQUIRE(r.m->tipsyIn.claimed() == 0);

	r.m->loadScript(SWAP_JS_PLAIN);
	REQUIRE(r.m->tipsyIn.claimed() == -1);
}


static const char* SWAP_JS_UNLOAD_ALL_KINDS = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
rack.onUnload = function() {
    let a = midi.create(); midi.setNoteOff(a, 1, 60); midiOut.send(a);
    let b = midi.create(); midi.setNoteOff(b, 1, 61); midiOut.sendAfterMs(b, 0);
    let c = midi.create(); midi.setNoteOff(c, 1, 62); midiOut.sendAfterTrigger(c, 1);
    let d = midi.create(); midi.setNoteOff(d, 1, 63); midiOut.sendAtFrame(d, 0);
    trig.setHigh(1);
    trig.sendTipsy("x");
};
)";

static const char* SWAP_LUA_UNLOAD_ALL_KINDS = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
rack.onUnload = function()
    local a = midi.create(); midi.setNoteOff(a, 1, 60); midiOut.send(a)
    local b = midi.create(); midi.setNoteOff(b, 1, 61); midiOut.sendAfterMs(b, 0)
    local c = midi.create(); midi.setNoteOff(c, 1, 62); midiOut.sendAfterTrigger(c, 1)
    local d = midi.create(); midi.setNoteOff(d, 1, 63); midiOut.sendAtFrame(d, 0)
    trig.setHigh(1)
    trig.sendTipsy("x")
end
)";

TEST_CASE("Swap: onUnload() of a replaced script gets only immediate MIDI out", "[MidiKit][Swap]") {
	// What it schedules for later, its trigger writes and its Tipsy messages
	// would outlive the script.
	const char* script = GENERATE(SWAP_JS_UNLOAD_ALL_KINDS, SWAP_LUA_UNLOAD_ALL_KINDS);
	bool clear = GENERATE(false, true);
	CATCH_INFO("clear: " << clear);

	SwapRig r;
	r.m->loadScript(script);
	r.step();
	drainLog(r.m);

	r.m->loadScript(clear ? "" : script);
	std::string unloadLog = drainLog(r.m);
	CATCH_INFO("log: " << unloadLog);
	REQUIRE(unloadLog.find("rror") == std::string::npos);

	// Long enough for the Tipsy encoder and the trigger tick to show up.
	bool low = r.step(64);
	for (int i = 0; i < 4; i++) low = r.pulse() && low;
	low = r.step(512) && low;

	REQUIRE(r.rec.notes(0x8) == std::vector<int>{60});
	REQUIRE(low);
	REQUIRE(r.m->midiOuts.ports[0].frameQueue.empty());
	REQUIRE(r.m->midiOuts.ports[0].tickQueue[0].empty());
}


TEST_CASE("Swap: the replaced script's scheduled MIDI is dropped, its due MIDI goes out", "[MidiKit][Swap]") {
	// Same outcome whether the audio thread had already moved the messages into
	// the port queues or they were still in the worker -> audio queue.
	bool drainedFirst = GENERATE(false, true);
	CATCH_INFO("drained first: " << drainedFirst);

	SwapRig r;
	r.m->loadScript(SWAP_JS_UNLOAD_TICKS);   // enables trigger input 1
	r.step();

	midi::Message afterTick = noteOn(0, 60, 100);
	midi::Message later = noteOn(0, 61, 100);
	later.frame = r.frame + 100;
	midi::Message now = noteOn(0, 62, 100);
	REQUIRE(r.m->sendMidi(0, &afterTick, 1, 0, 1));
	REQUIRE(r.m->sendMidi(0, &later, 1, 0, 0));
	REQUIRE(r.m->sendMidi(0, &now, 1, 0, 0));
	if (drainedFirst) {
		r.step(8);
		REQUIRE(r.rec.notes(0x9) == std::vector<int>{62});
	}

	r.m->loadScript(SWAP_JS_PLAIN);
	r.step(8);
	for (int i = 0; i < 4; i++) r.pulse();
	r.step(200);

	REQUIRE(r.rec.notes(0x9) == std::vector<int>{62});
}


TEST_CASE("Swap: a trigger write of the new script is kept, the replaced script's are dropped", "[MidiKit][Swap]") {
	SwapRig r;
	r.m->loadScript(SWAP_JS_PLAIN);
	r.step();

	SECTION("the new script writes before the audio thread has caught up") {
		// The worker has reset, the audio thread not yet: a clear request raised
		// by the reset used to drop this stamped write too.
		r.m->loadScript(SWAP_JS_PLAIN);
		r.m->setTrigVoltage(0, 0, 10.f, r.frame);
		r.step();
		REQUIRE(r.trigOut() == 10.f);
	}

	SECTION("the replaced script's writes, pending or still queued") {
		r.m->setTrigVoltage(0, 0, 10.f, r.frame + 50);   // pending after the next sample
		r.step();
		r.m->setTrigVoltage(0, 0, 10.f);                 // still in the queue
		r.m->loadScript(SWAP_JS_PLAIN);
		REQUIRE(r.step(100));
	}
}


TEST_CASE("Swap: events captured for the replaced script never reach the new one", "[MidiKit][Swap]") {
	static const char* JS_LOGS_MESSAGES = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    rack.log("got note " + midi.getNote(msg));
};
)";
	SwapRig r;
	r.m->loadScript(JS_LOGS_MESSAGES);
	r.step();

	SECTION("still in the engine's queue when the swap runs") {
		r.m->host.getActiveEngine()->processInMessage(0, noteOn(0, 60, 100));
		r.m->loadScript(JS_LOGS_MESSAGES);
		drainLog(r.m);
		r.step(16);
		REQUIRE(drainLog(r.m).find("got note") == std::string::npos);
	}

	SECTION("decoded before the audio thread has caught up with the swap") {
		r.m->loadScript(JS_LOGS_MESSAGES);
		drainLog(r.m);
		r.m->midiIns.ports[0].processor.processMessage(noteOn(0, 61, 100));
		r.step(16);
		REQUIRE(drainLog(r.m).find("got note") == std::string::npos);

		// Afterwards messages reach the new script.
		r.m->midiIns.ports[0].processor.processMessage(noteOn(0, 62, 100));
		r.step(16);
		REQUIRE(drainLog(r.m).find("got note 62") != std::string::npos);
	}
}


TEST_CASE("Swap: onReset() sends onUnload()'s MIDI before it resets the ports", "[MidiKit][Swap]") {
	// A reset port has no device: the note-off used to be drained into it.
	SwapRig r;
	r.m->loadScript(SWAP_JS_UNLOAD_ALL_KINDS);
	r.step();

	r.m->onReset();
	REQUIRE(r.rec.notes(0x8) == std::vector<int>{60});
	REQUIRE(r.step(100));
}


TEST_CASE("Swap: a script that fails to load is reset, and the log keeps the error", "[MidiKit][Swap]") {
	// Ending a script always resets what it set up, also when the engine ends
	// it; the reason is logged after the reset.
	const char* script = GENERATE(R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
trig.enableTipsyIn();
throw new Error("boom");
)", R"(--[[
@engine minilua@v1
--]]
midiOut.enablePorts(2)
trig.enableTipsyIn()
error("boom")
)");
	SwapRig r;
	r.m->loadScript(script);

	REQUIRE(r.m->midiOuts.enabledCount() == 1);
	REQUIRE(r.m->tipsyIn.claimed() == -1);
	std::vector<std::tuple<LOG_FORMAT, std::string>> log = drainLogEntries(r.m);
	REQUIRE_FALSE(log.empty());
	REQUIRE(std::get<0>(log.back()) != LOG_FORMAT::RESET);
	bool sawError = false;
	for (const auto& e : log) {
		if (std::get<1>(e).find("boom") != std::string::npos) sawError = true;
	}
	REQUIRE(sawError);
}


TEST_CASE("Swap: onUnload()'s held note-off survives a second reset", "[MidiKit][Swap]") {
	// In timing mode the note-off waits behind Rack's output queue. A reload into
	// a script that fails to load resets twice in one task, and the audio thread
	// may run between the two resets; neither may drop it.
	static const char* JS_TIMED_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
rack.onUnload = function() {
    let m = midi.create();
    midi.setNoteOff(m, 1, 60);
    midiOut.send(m);
};
)";
	// A block size, so the note-off is held for two blocks and a frame.
	struct BlockMock : StoermelderPackOne::vcv::EngineAccess {
		int64_t frame = 0;
		StoermelderPackOne::vcv::EngineAccess* previous;
		BlockMock() : previous(StoermelderPackOne::vcv::engineAccess) { StoermelderPackOne::vcv::engineAccess = this; }
		~BlockMock() { StoermelderPackOne::vcv::engineAccess = previous; }
		int64_t getFrame() const override { return frame; }
		int64_t getBlockFrames() const override { return 256; }
	} engine;

	bool audioBetween = GENERATE(false, true);
	CATCH_INFO("audio thread runs between the resets: " << audioBetween);

	SwapRig r;
	r.m->loadScript(JS_TIMED_UNLOAD);
	r.step(8);
	engine.frame = r.frame;

	if (audioBetween) {
		r.m->loadScript("");
		r.step(8);                    // moved into the port's frame queue
		r.m->endUnload();      // a second reset, as a failing load does
	}
	else {
		r.m->loadScript(R"(/**
 * @engine QuickJs@v1
 */
throw new Error("boom");
)");
	}
	REQUIRE(r.rec.notes(0x8).empty());   // held, not sent at once

	for (int i = 0; i < 100 && r.rec.notes(0x8).empty(); i++) r.step(100);
	REQUIRE(r.rec.notes(0x8) == std::vector<int>{60});
}
