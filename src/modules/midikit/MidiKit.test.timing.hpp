#include "MidiKit.test.hpp"

// Frame-accuracy harness for MIDI-KIT output timing.
//
// These tests answer the question no other suite asks: "on which sample did a
// message leave, and which frame did it carry?". MidiKit.test.module.hpp tests
// queue mechanics, MidiKit.perf.cpp measures wall-clock worker latency.
//
// The seam is a midi::OutputDevice attached to the module's real output.
// midi::Output::sendMessage() is non-virtual and forwards to `outputDevice`, so
// the recorder observes the production path end to end and sees the `frame`
// field exactly as Rack's RtMidiOutputDevice would — a recording subclass of
// MidiOutput would observe the module's intent one call earlier and could not
// see what the last step does to the frame.
//
// STATUS QUO: every test below pins what the module does TODAY, so the suite is
// green and a later step shows up as a deliberate, reviewed flip of one
// assertion. Each such assertion is marked "status quo". Everything is exact integer arithmetic over a
// synthetic frame counter — nothing here is wall-clock.
//
// Run alone: ./build/test/MidiKit.test "[timing]"

// Records every message that reaches the device: the frame field it carried and
// the process() frame it was handed over on.
struct TimingRecorder : midi::OutputDevice {
	struct Sent {
		int64_t frameField;   // Message::frame as the device sees it; -1 = "now"
		int64_t releasedAt;   // the engine frame of the process() call that sent it
		uint8_t status;
		uint8_t note;
		uint8_t value;
	};
	std::vector<Sent> sent;
	int64_t now = 0;

	void sendMessage(const midi::Message& msg) override {
		Sent s;
		s.frameField = msg.frame;
		s.releasedAt = now;
		s.status = msg.getStatus();
		s.note = msg.getNote();
		s.value = msg.getValue();
		sent.push_back(s);
	}
};

// Steps one module sample by sample with a synthetic frame counter. Frames
// start at 0 and advance by one per process() call, which is what Rack does
// within a block, so the module's divider phase is a pure function of frame.
struct TimingRig {
	ModuleScaffold mods;
	MidiKitModule* m;
	TimingRecorder rec;
	int64_t frame = 0;

	explicit TimingRig(const char* script) {
		m = mods.create("MidiKit");
		m->midiOuts.ports[0].outputDevice = &rec;
		m->midiOuts.ports[0].channel = -1;
		m->loadScript(script);
	}

	~TimingRig() {
		// The recorder is a member and dies before `mods` destroys the module;
		// onRemove() flushes output through the device.
		m->midiOuts.ports[0].outputDevice = nullptr;
	}

	void step() {
		rec.now = frame;
		m->process(Test::makeProcessArgs(frame));
		frame++;
	}

	void run(int64_t untilFrame) {
		while (frame < untilFrame) step();
	}

	// Queues an inbound message that Rack would release at `atFrame`.
	void inject(midi::Message msg, int64_t atFrame) {
		msg.frame = atFrame;
		m->midiIns.ports[0].processor.getInput().onMessage(msg);
	}
};

static const char* JS_PASS_THROUGH = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.send(msg);
};
)";

static const char* LUA_PASS_THROUGH = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    midiOut.send(msg)
end
)";

static const char* JS_AFTER_MS = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, 10);
};
)";

static const char* JS_AFTER_TRIGGER = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
midi.onMessage = function(port, msg) {
    midiOut.sendAfterTrigger(msg, 1);
};
)";

static const char* TIMING_SCRIPTS[] = { JS_PASS_THROUGH, LUA_PASS_THROUGH };

// The module's divider period. A message due at frame N is popped, dispatched
// and drained on the first divider tick at or after N.
static constexpr int64_t DIVIDER = 8;

// The first divider tick at or after `frame`, given the rig starts at frame 0:
// dsp::ClockDivider fires on every 8th call, i.e. frames 7, 15, 23, ...
static int64_t nextDividerTick(int64_t frame) {
	return ((frame + 1 + DIVIDER - 1) / DIVIDER) * DIVIDER - 1;
}

TEST_CASE("Timing: a pass-through reply carries no frame", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(script);

		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);

		REQUIRE(rig.rec.sent.size() == 1);
		// status quo: send() clears the frame, so Rack's
		// output thread is never asked to schedule anything.
		REQUIRE(rig.rec.sent[0].frameField == -1);
		REQUIRE(rig.rec.sent[0].note == 60);
	}
}

TEST_CASE("Timing: a reply leaves on the first divider tick after the input frame", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(script);

		// Input due at 20 → popped on the divider tick at 23.
		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);

		REQUIRE(rig.rec.sent.size() == 1);
		// status quo: up to DIVIDER-1 samples of avoidable lag.
		REQUIRE(rig.rec.sent[0].releasedAt == nextDividerTick(20));
		REQUIRE(rig.rec.sent[0].releasedAt == 23);
	}
}

TEST_CASE("Timing: a message due exactly on a divider tick is not delayed", "[MidiKit][timing]") {
	TimingRig rig(JS_PASS_THROUGH);

	rig.inject(noteOn(0, 60, 100), 23);
	rig.run(40);

	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].releasedAt == 23);
}

TEST_CASE("Timing: a message is never released before its input frame", "[MidiKit][timing]") {
	TimingRig rig(JS_PASS_THROUGH);

	rig.inject(noteOn(0, 60, 100), 100);
	rig.run(99);
	REQUIRE(rig.rec.sent.empty());

	rig.run(120);
	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].releasedAt >= 100);
}

TEST_CASE("Timing: sendAfterMs holds the message and strips its frame", "[MidiKit][timing]") {
	TimingRig rig(JS_AFTER_MS);
	const int64_t delay = int64_t(0.010 * Test::sampleRate());

	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);
	// Held in the frame queue, not sent with the pass-through's latency.
	REQUIRE(rig.rec.sent.empty());
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);

	rig.run(20 + delay + 40);
	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.empty());
	// status quo: the frame is cleared at release.
	REQUIRE(rig.rec.sent[0].frameField == -1);
}

TEST_CASE("Timing: sendAfterTrigger releases on the exact edge sample", "[MidiKit][timing]") {
	TimingRig rig(JS_AFTER_TRIGGER);
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	rig.run(8);                         // prime the trigger input low
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(45);
	REQUIRE(rig.rec.sent.empty());      // scheduled, waiting for the clock

	// Edge at frame 45, which is not a divider tick (those are 7 mod 8).
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	rig.step();
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);

	REQUIRE(rig.rec.sent.size() == 1);
	// The edge is detected per sample, so release is exact — the module
	// already has perfect timing here and discards it.
	REQUIRE(rig.rec.sent[0].releasedAt == 45);
	// sendAfterTrigger() sends the message without a frame (the tick schedules
	// it), so it doesn't carry the arrival frame of the input it was built from
	// (8); Rack sends it immediately at release.
	REQUIRE(rig.rec.sent[0].frameField == -1);
}

TEST_CASE("Timing: release jitter over a metronomic stream is the divider quantisation", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(script);

		// One message every 37 frames. 37 is coprime to the divider period, so
		// over any DIVIDER consecutive messages every phase occurs exactly once
		// and the lateness distribution is exactly uniform over 0..DIVIDER-1.
		const int64_t period = 37;
		const int count = 64;
		std::vector<int64_t> intended;
		for (int i = 0; i < count; i++) {
			int64_t f = 100 + i * period;
			intended.push_back(f);
			rig.inject(noteOn(0, 40 + (i % 40), 100), f);
		}
		rig.run(100 + count * period + 40);

		REQUIRE(rig.rec.sent.size() == size_t(count));

		int64_t minLate = INT64_MAX, maxLate = 0, sum = 0;
		int64_t histogram[DIVIDER] = {};
		for (int i = 0; i < count; i++) {
			int64_t late = rig.rec.sent[i].releasedAt - intended[i];
			REQUIRE(late >= 0);
			REQUIRE(late < DIVIDER);
			minLate = std::min(minLate, late);
			maxLate = std::max(maxLate, late);
			sum += late;
			histogram[late]++;
			// status quo: nothing carries a usable frame, so Rack cannot correct.
			REQUIRE(rig.rec.sent[i].frameField == -1);
		}
		CATCH_INFO("min=" << minLate << " max=" << maxLate << " mean=" << double(sum) / count);

		// status quo: min=0 / max=7 / mean=3.5.
		REQUIRE(minLate == 0);
		REQUIRE(maxLate == DIVIDER - 1);
		REQUIRE(sum * 2 == count * (DIVIDER - 1));
		for (int b = 0; b < DIVIDER; b++) {
			REQUIRE(histogram[b] == count / DIVIDER);
		}
	}
}

TEST_CASE("Timing: messages sharing an input frame leave in arrival order", "[MidiKit][timing]") {
	TimingRig rig(JS_PASS_THROUGH);

	rig.inject(noteOn(0, 60, 100), 20);
	rig.inject(noteOn(0, 61, 100), 20);
	rig.inject(noteOn(0, 62, 100), 20);
	rig.run(40);

	REQUIRE(rig.rec.sent.size() == 3);
	REQUIRE(rig.rec.sent[0].note == 60);
	REQUIRE(rig.rec.sent[1].note == 61);
	REQUIRE(rig.rec.sent[2].note == 62);
}

// Multi-message groups
// Only the first message of a group passes through a send binding, so the
// others must follow its schedule and keep their order while they share it.

static const char* JS_GROUP_AFTER_MS = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    if (midi.getNote(msg) === 1) {
        let nrpn = midi.createNRPN();
        midi.setNRPN(nrpn, 1, 130, 1000);
        midiOut.sendAfterMs(nrpn, 10);
    }
    else {
        let cc14 = midi.createCc14bit();
        midi.setCc14bit(cc14, 1, 1, 100.5);
        midiOut.sendAfterMs(cc14, 10);
    }
};
)";

static const char* LUA_GROUP_AFTER_MS = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    if midi.getNote(msg) == 1 then
        local nrpn = midi.createNRPN()
        midi.setNRPN(nrpn, 1, 130, 1000)
        midiOut.sendAfterMs(nrpn, 10)
    else
        local cc14 = midi.createCc14bit()
        midi.setCc14bit(cc14, 1, 1, 100.5)
        midiOut.sendAfterMs(cc14, 10)
    end
end
)";

static const char* JS_GROUP_AFTER_TRIGGER = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
midi.onMessage = function(port, msg) {
    if (midi.getNote(msg) === 1) {
        let nrpn = midi.createNRPN();
        midi.setNRPN(nrpn, 1, 130, 1000);
        midiOut.sendAfterTrigger(nrpn, 1);
    }
    else {
        let cc14 = midi.createCc14bit();
        midi.setCc14bit(cc14, 1, 1, 100.5);
        midiOut.sendAfterTrigger(cc14, 1);
    }
};
)";

static std::vector<int> controllers(const TimingRecorder& rec) {
	std::vector<int> result;
	for (auto& s : rec.sent) result.push_back(s.note);
	return result;
}

TEST_CASE("Timing: a group sent with sendAfterMs is held and released whole, in order", "[MidiKit][timing]") {
	const int64_t delay = int64_t(0.010 * Test::sampleRate());

	for (const char* script : { JS_GROUP_AFTER_MS, LUA_GROUP_AFTER_MS }) {
		CATCH_INFO(script);

		SECTION("NRPN") {
			TimingRig rig(script);
			rig.inject(noteOn(0, 1, 100), 20);
			rig.run(40);

			// Every member waits for the delay; none escapes ahead of the leader.
			REQUIRE(rig.rec.sent.empty());
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 4);

			rig.run(20 + delay + 40);
			REQUIRE(controllers(rig.rec) == std::vector<int>{99, 98, 6, 38});
			// One release frame for the whole group.
			for (auto& s : rig.rec.sent) REQUIRE(s.releasedAt == rig.rec.sent[0].releasedAt);
		}

		SECTION("14-bit CC") {
			TimingRig rig(script);
			rig.inject(noteOn(0, 2, 100), 20);
			rig.run(40);

			REQUIRE(rig.rec.sent.empty());
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 2);

			rig.run(20 + delay + 40);
			REQUIRE(controllers(rig.rec) == std::vector<int>{1, 33});
			REQUIRE(rig.rec.sent[0].releasedAt == rig.rec.sent[1].releasedAt);
		}
	}
}

TEST_CASE("Timing: a group sent with sendAfterTrigger is released in order", "[MidiKit][timing]") {
	TimingRig rig(JS_GROUP_AFTER_TRIGGER);
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	rig.run(8);
	rig.inject(noteOn(0, 1, 100), 8);
	rig.inject(noteOn(0, 2, 100), 8);
	rig.run(45);
	REQUIRE(rig.rec.sent.empty());

	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	rig.step();
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);

	// Both groups sit on the same tick, so they are released together; each stays
	// contiguous and in order.
	REQUIRE(controllers(rig.rec) == std::vector<int>{99, 98, 6, 38, 1, 33});
}

// ── Timing mode (midiOut.enableTiming()) ────────────────────────────────────
// The scripts above with the opt-in prepended; the legacy tests stay as they are.

// Inserts midiOut.enableTiming() after the header comment, for either engine.
static std::string withTiming(const char* script, const char* arg = "") {
	std::string s = script;
	size_t at = s.find("*/\n");
	if (at != std::string::npos) at += 3;
	else {
		at = s.find("--]]\n");
		REQUIRE(at != std::string::npos);
		at += 5;
	}
	bool lua = s.find("minilua") != std::string::npos;
	std::string call = std::string("midiOut.enableTiming(") + arg + (lua ? ")\n" : ");\n");
	return s.substr(0, at) + call + s.substr(at);
}

// What Rack's unstable queue needs: a frame on every message, strictly increasing.
static void requireOrderedFrames(const TimingRecorder& rec) {
	int64_t last = -1;
	for (auto& s : rec.sent) {
		REQUIRE(s.frameField >= 0);
		REQUIRE(s.frameField > last);
		last = s.frameField;
	}
}

TEST_CASE("Timing mode: the opt-in is per script and forgotten on reload", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(withTiming(script).c_str());
		REQUIRE(rig.m->midiOuts.isTimingEnabled());

		// A script without the call is back in legacy mode.
		rig.m->loadScript(script);
		REQUIRE_FALSE(rig.m->midiOuts.isTimingEnabled());

		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);
		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(rig.rec.sent[0].frameField == -1);
	}
}

TEST_CASE("Timing mode: a reply carries the frame of the message it answers", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(withTiming(script).c_str());

		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);

		REQUIRE(rig.rec.sent.size() == 1);
		// The arrival frame, not the later frame the script ran on.
		REQUIRE(rig.rec.sent[0].frameField == 20);
		REQUIRE(rig.rec.sent[0].releasedAt == 23);
		REQUIRE(rig.rec.sent[0].note == 60);
	}
}

TEST_CASE("Timing mode: sendAfterMs hands its scheduled frame to the device", "[MidiKit][timing]") {
	TimingRig rig(withTiming(JS_AFTER_MS).c_str());
	const int64_t delay = int64_t(0.010 * Test::sampleRate());

	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(20 + delay + 40);

	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].frameField > 0);
	REQUIRE(rig.rec.sent[0].frameField <= rig.rec.sent[0].releasedAt);
}

TEST_CASE("Timing mode: messages sharing a frame get strictly increasing frames in send order", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(withTiming(script).c_str());

		// A retrigger plus two more notes, all on one input frame.
		rig.inject(noteOff(0, 60), 20);
		rig.inject(noteOn(0, 60, 100), 20);
		rig.inject(noteOn(0, 64, 100), 20);
		rig.inject(noteOn(0, 67, 100), 20);
		rig.run(40);

		REQUIRE(rig.rec.sent.size() == 4);
		requireOrderedFrames(rig.rec);
		REQUIRE(rig.rec.sent[0].status == 0x8);
		REQUIRE(rig.rec.sent[1].note == 60);
		REQUIRE(rig.rec.sent[2].note == 64);
		REQUIRE(rig.rec.sent[3].note == 67);
		// All answer frame 20, spread one sample apart in send order.
		for (int i = 0; i < 4; i++) REQUIRE(rig.rec.sent[i].frameField == 20 + i);
	}
}

TEST_CASE("Timing mode: a group keeps its order and gets one frame per message", "[MidiKit][timing]") {
	const int64_t delay = int64_t(0.010 * Test::sampleRate());

	for (const char* script : { JS_GROUP_AFTER_MS, LUA_GROUP_AFTER_MS }) {
		CATCH_INFO(script);
		TimingRig rig(withTiming(script).c_str());
		rig.inject(noteOn(0, 1, 100), 20);
		rig.run(20 + delay + 40);

		REQUIRE(controllers(rig.rec) == std::vector<int>{99, 98, 6, 38});
		requireOrderedFrames(rig.rec);
	}
}

TEST_CASE("Timing mode: scheduled and immediate messages never go out of order", "[MidiKit][timing]") {
	// Note 1 is delayed, note 2 is sent at once; the device must see ordered frames.
	const char* script = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
midi.onMessage = function(port, msg) {
    if (midi.getNote(msg) === 1) midiOut.sendAfterMs(msg, 10);
    else midiOut.send(msg);
};
)";
	const int64_t delay = int64_t(0.010 * Test::sampleRate());
	TimingRig rig(script);

	rig.inject(noteOn(0, 1, 100), 20);
	rig.inject(noteOn(0, 2, 100), 450);
	rig.inject(noteOn(0, 2, 100), 470);
	rig.inject(noteOn(0, 2, 100), 470);
	rig.run(20 + delay + 100);

	REQUIRE(rig.rec.sent.size() == 4);
	requireOrderedFrames(rig.rec);
	// By frame: the note sent at 450, the delayed one due at 461, then the pair at 470.
	REQUIRE(controllers(rig.rec) == std::vector<int>{2, 1, 2, 2});
}

TEST_CASE("Timing mode: a sendAfterTrigger message is stamped with the edge's frame", "[MidiKit][timing]") {
	TimingRig rig(withTiming(JS_AFTER_TRIGGER).c_str());
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	rig.run(8);
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(45);
	REQUIRE(rig.rec.sent.empty());

	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	rig.step();
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);

	REQUIRE(rig.rec.sent.size() == 1);
	// Not the stale arrival frame (8).
	REQUIRE(rig.rec.sent[0].frameField == 45);
	REQUIRE(rig.rec.sent[0].releasedAt == 45);
}

TEST_CASE("Teardown flush is sent immediately in both modes", "[MidiKit][timing]") {
	auto script = [](bool timing) {
		std::string s = "/**\n * @engine QuickJs@v1\n */\n";
		if (timing) s += "midiOut.enableTiming();\n";
		s += "rack.onUnload = function() {\n"
		     "    let m = midi.create();\n"
		     "    midi.setNoteOff(m, 1, 60, 0);\n"
		     "    midiOut.send(m);\n"
		     "};\n";
		return s;
	};

	for (bool timing : { false, true }) {
		CATCH_INFO(timing);
		TimingRig rig(script(timing).c_str());
		rig.run(40);
		REQUIRE(rig.rec.sent.empty());

		// As onRemove() does.
		rig.m->host.unload();
		rig.m->flushMidiOut();

		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(rig.rec.sent[0].status == 0x8);
		// Frame-less even in timing mode: a framed message would sit in Rack's output
		// queue, which goes away with the device when this module releases it.
		REQUIRE(rig.rec.sent[0].frameField == -1);
	}
}

// Frames carried to the script
// Every dispatch runs with currentInFrame set to the frame of the event that
// caused it. Nothing reads it yet; these pin the plumbing.

using StoermelderPackOne::MidiScript::QueuedMessage;
using StoermelderPackOne::MidiScript::TipsyMessage;

// Dispatches through the engine's real queues and records the frame each
// callback runs under.
struct FrameProbeEngine : MidiScriptEngine {
	struct Seen {
		const char* kind;
		int64_t frame;
	};
	std::vector<Seen> seen;

	explicit FrameProbeEngine(MidiKitModule* module) : MidiScriptEngine(module, 4, 1, 1, 4, 1, 1) {
		setWorker(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	}

	void processInMessage(int midiPort, const QueuedMessage& msg) override {
		midiInQueue.tryPush(midiPort, msg);
	}
	void processInTick(int trigPort, uint8_t channel, int64_t frame) override {
		tickInQueue.push(std::make_tuple(trigPort, channel, frame));
	}
	void dispatchMidiMessage(int midiPort, midi::Message& msg) override { seen.push_back({"message", currentInFrame}); }
	void dispatchNrpn(int midiPort, const QueuedMessage& q, bool isRpn) override { seen.push_back({"nrpn", currentInFrame}); }
	void dispatchCc14bit(int midiPort, const QueuedMessage& q) override { seen.push_back({"cc14", currentInFrame}); }
	void dispatchTrigger(int trigPort, uint8_t channel) override { seen.push_back({"trigger", currentInFrame}); }
	void dispatchTipsyMessage(const TipsyMessage& msg) override { seen.push_back({"tipsy", currentInFrame}); }

	void loadScriptOnWorker(const char* script, const std::string& initialConfigJson) override { }
	bool testScript(const std::string& script) override { return false; }
	void unloadScriptOnWorker() override { }
	std::string getInputName(int i) override { return ""; }
	std::string getParamName(int i) override { return ""; }
	std::string getParamFormatValue(int i) override { return ""; }
	void getContextMenus(const std::function<void(const std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem>&)>& callback) override {
		std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem> empty;
		callback(empty);
	}
	void invokeContextMenuCallback(int callbackId, int value) override { }
	bool getMemoryUsage(size_t& used, size_t& total) override { return false; } 
};

struct ProbeRig {
	ModuleScaffold mods;
	MidiKitModule* m;
	FrameProbeEngine eng;
	int64_t frame = 0;

	ProbeRig() : m(mods.create("MidiKit")), eng(m) {
		m->host.getActiveEngine() = &eng;
	}
	// The scaffold destroys the module after eng; detach first.
	~ProbeRig() { m->host.getActiveEngine() = nullptr; }

	void run(int64_t untilFrame) {
		while (frame < untilFrame) m->process(Test::makeProcessArgs(frame++));
	}
};

TEST_CASE("Frames: a message dispatches under its arrival frame", "[MidiKit][timing]") {
	ProbeRig rig;
	midi::Message msg = noteOn(0, 60, 100);
	msg.frame = 20;
	rig.m->midiIns.ports[0].processor.getInput().onMessage(msg);
	rig.run(40);

	REQUIRE(rig.eng.seen.size() == 1);
	REQUIRE(rig.eng.seen[0].frame == 20);
	// Not left set once the dispatch is over.
	REQUIRE(rig.eng.currentInFrame == -1);
}

TEST_CASE("Frames: an assembled NRPN carries its last component's frame", "[MidiKit][timing]") {
	ProbeRig rig;
	rig.m->enableNrpnIn(0, 0, -1);

	// The four CCs arrive on different frames; the value is known on the last.
	const int ccs[4][2] = { {99, 1}, {98, 2}, {6, 3}, {38, 4} };
	for (int i = 0; i < 4; i++) {
		midi::Message msg = Test::makeMidiMessage(0xb, 0, ccs[i][0], ccs[i][1], 20 + i * 3);
		rig.m->midiIns.ports[0].processor.getInput().onMessage(msg);
	}
	rig.run(60);

	REQUIRE(rig.eng.seen.size() == 1);
	REQUIRE(rig.eng.seen[0].kind == std::string("nrpn"));
	REQUIRE(rig.eng.seen[0].frame == 29);
}

TEST_CASE("Frames: a trigger dispatches under the frame of its edge", "[MidiKit][timing]") {
	ProbeRig rig;
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	rig.m->enableTrigger(0, 0);

	rig.run(30);
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	rig.run(31);   // the edge is on frame 30
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	rig.run(50);

	REQUIRE(rig.eng.seen.size() == 1);
	REQUIRE(rig.eng.seen[0].kind == std::string("trigger"));
	REQUIRE(rig.eng.seen[0].frame == 30);
}

TEST_CASE("Frames: a Tipsy message dispatches under the frame it completed on", "[MidiKit][timing]") {
	ProbeRig rig;
	rig.m->enableTipsyIn(0);
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	const std::string data = "hello";
	REQUIRE(rig.m->sendTipsyOut("text/plain", reinterpret_cast<const unsigned char*>(data.data()), (uint32_t)data.size()));
	std::vector<float> voltages;
	float v;
	while (rig.m->tipsyOut.process(v) == TipsyOutput::Output::WROTE) voltages.push_back(v);
	REQUIRE(voltages.size() > 0);

	// One voltage per frame; the last one completes the message.
	for (float volt : voltages) {
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(volt, 0);
		rig.m->process(Test::makeProcessArgs(rig.frame++));
	}
	int64_t lastFrame = rig.frame - 1;
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);   // a held level would re-decode
	rig.run(rig.frame + 16);

	REQUIRE(rig.eng.seen.size() == 1);
	REQUIRE(rig.eng.seen[0].kind == std::string("tipsy"));
	REQUIRE(rig.eng.seen[0].frame == lastFrame);
}

TEST_CASE("Frames: sendAfterMs counts from the frame the script was dispatched on", "[MidiKit][timing]") {
	TimingRig rig(JS_AFTER_MS);

	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	// Dispatched on the divider tick at 23, not from the engine's own counter.
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
	int64_t delay = int64_t(10.0 / 1000.0 * rig.m->sampleRate.load());
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 23 + delay);
}

// sendAtFrame, rack.getEventFrame() and the sendAfterMs base

static const char* JS_AT_FRAME = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.sendAtFrame(msg, 200);
};
)";

static const char* LUA_AT_FRAME = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    midiOut.sendAtFrame(msg, 200)
end
)";

static const char* JS_AT_NEGATIVE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.sendAtFrame(msg, -500);
};
)";

static const char* LUA_AT_NEGATIVE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    midiOut.sendAtFrame(msg, -500)
end
)";

// Reports the event frame it sees by scheduling 100 frames after it.
static const char* JS_EVENT_FRAME = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
midi.onMessage = function(port, msg) {
    midiOut.sendAtFrame(msg, rack.getEventFrame() + 100);
};
trig.onTrigger = function(port, ch) {
    let m = midi.create();
    midi.setNoteOn(m, 1, 61, 100);
    midiOut.sendAtFrame(m, rack.getEventFrame() + 100);
};
)";

static const char* LUA_EVENT_FRAME = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
midi.onMessage = function(port, msg)
    midiOut.sendAtFrame(msg, rack.getEventFrame() + 100)
end
trig.onTrigger = function(port, ch)
    local m = midi.create()
    midi.setNoteOn(m, 1, 61, 100)
    midiOut.sendAtFrame(m, rack.getEventFrame() + 100)
end
)";

TEST_CASE("sendAtFrame holds a message until its frame; legacy strips it, timing keeps it", "[MidiKit][timing]") {
	const std::pair<const char*, const char*> scripts[] = { {JS_AT_FRAME, "js"}, {LUA_AT_FRAME, "lua"} };
	for (auto& sc : scripts) {
		CATCH_INFO(sc.second);

		SECTION("legacy") {
			TimingRig rig(sc.first);
			rig.inject(noteOn(0, 60, 100), 20);
			rig.run(190);
			REQUIRE(rig.rec.sent.empty());
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);

			rig.run(220);
			REQUIRE(rig.rec.sent.size() == 1);
			REQUIRE(rig.rec.sent[0].releasedAt >= 200);
			REQUIRE(rig.rec.sent[0].frameField == -1);
		}

		SECTION("timing") {
			TimingRig rig(withTiming(sc.first).c_str());
			rig.inject(noteOn(0, 60, 100), 20);
			rig.run(220);

			REQUIRE(rig.rec.sent.size() == 1);
			REQUIRE(rig.rec.sent[0].releasedAt >= 200);
			REQUIRE(rig.rec.sent[0].frameField == 200);
		}
	}
}

TEST_CASE("sendAtFrame treats a negative frame as 'now'", "[MidiKit][timing]") {
	for (const char* script : { JS_AT_NEGATIVE, LUA_AT_NEGATIVE }) {
		CATCH_INFO(script);
		TimingRig rig(script);
		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);

		// Sent at once, not parked in the frame queue ahead of everything else.
		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(rig.m->midiOuts.ports[0].frameQueue.empty());
		REQUIRE(rig.rec.sent[0].frameField == -1);
	}
}

TEST_CASE("sendAtFrame with a negative frame keeps send order", "[MidiKit][timing]") {
	// A negative frame left as is would park the message in the frame queue,
	// behind every immediate message sent after it.
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let a = midi.create();
    midi.setNoteOn(a, 1, 60, 100);
    midiOut.sendAtFrame(a, -500);
    let b = midi.create();
    midi.setNoteOn(b, 1, 61, 100);
    midiOut.send(b);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    local a = midi.create()
    midi.setNoteOn(a, 1, 60, 100)
    midiOut.sendAtFrame(a, -500)
    local b = midi.create()
    midi.setNoteOn(b, 1, 61, 100)
    midiOut.send(b)
end
)";
	for (const char* script : { js, lua }) {
		CATCH_INFO(script);
		TimingRig rig(script);
		rig.inject(noteOn(0, 1, 100), 20);
		rig.run(40);
		REQUIRE(controllers(rig.rec) == std::vector<int>{60, 61});
	}
}

TEST_CASE("rack.getEventFrame() is the frame of the event being handled", "[MidiKit][timing]") {
	for (const char* script : { JS_EVENT_FRAME, LUA_EVENT_FRAME }) {
		CATCH_INFO(script);

		SECTION("MIDI message") {
			TimingRig rig(script);
			rig.inject(noteOn(0, 60, 100), 20);
			rig.run(40);
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 120);
		}

		SECTION("trigger edge") {
			TimingRig rig(script);
			rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
			rig.run(30);
			rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
			rig.step();   // the edge is on frame 30
			rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
			rig.run(50);
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 130);
		}
	}
}

TEST_CASE("rack.getEventFrame() is -1 outside an event", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    midiOut.sendAtFrame(m, rack.getEventFrame() + 500);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local m = midi.create()
    midi.setNoteOn(m, 1, 60, 100)
    midiOut.sendAtFrame(m, rack.getEventFrame() + 500)
end
)";
	for (const char* script : { js, lua }) {
		CATCH_INFO(script);
		TimingRig rig(script);
		rig.run(20);
		// -1 + 500
		REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
		REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 499);
	}
}

TEST_CASE("Timing mode: sendAfterMs counts from the causing event", "[MidiKit][timing]") {
	TimingRig rig(withTiming(JS_AFTER_MS).c_str());
	int64_t delay = int64_t(10.0 / 1000.0 * rig.m->sampleRate.load());

	// Input at 20 is dispatched on the tick at 23; the delay counts from 20.
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 20 + delay);
}

// Makes the engine report a frame, for code that reads it before any process().
struct EngineFrameMock : StoermelderPackOne::vcv::EngineAccess {
	int64_t frame = 0;
	int64_t getFrame() const override { return frame; }
};

struct EngineFrameScope {
	EngineFrameMock mock;
	StoermelderPackOne::vcv::EngineAccess* previous;
	explicit EngineFrameScope(int64_t frame) : previous(StoermelderPackOne::vcv::engineAccess) {
		mock.frame = frame;
		StoermelderPackOne::vcv::engineAccess = &mock;
	}
	~EngineFrameScope() { StoermelderPackOne::vcv::engineAccess = previous; }
};

TEST_CASE("Timing mode: sendAfterMs with no event counts from the published frame", "[MidiKit][timing]") {
	const char* script = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
rack.onLoad = function() {
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    midiOut.sendAfterMs(m, 10);
};
)";
	EngineFrameScope engine(1000);
	ModuleScaffold mods;
	MidiKitModule* m = mods.create("MidiKit");
	m->loadScript(script);
	int64_t delay = int64_t(10.0 / 1000.0 * m->sampleRate.load());

	m->process(Test::makeProcessArgs(1000));
	for (int64_t f = 1001; f < 1010; f++) m->process(Test::makeProcessArgs(f));

	REQUIRE(m->midiOuts.ports[0].frameQueue.size() == 1);
	REQUIRE(m->midiOuts.ports[0].frameQueue.top().msg.frame == 1000 + delay);
}

TEST_CASE("Timing mode: send() takes the frame of the event it runs in", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
trig.enableIn(1, 1);
trig.onTrigger = function(port, ch) {
    let m = midi.create();
    midi.setNoteOn(m, 1, 61, 100);
    midiOut.send(m);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
midiOut.enableTiming()
trig.enableIn(1, 1)
trig.onTrigger = function(port, ch)
    local m = midi.create()
    midi.setNoteOn(m, 1, 61, 100)
    midiOut.send(m)
end
)";
	for (const char* script : { js, lua }) {
		CATCH_INFO(script);
		TimingRig rig(script);
		rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
		rig.run(30);
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		rig.step();   // the edge is on frame 30
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		rig.run(50);

		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(rig.rec.sent[0].frameField == 30);
	}
}

TEST_CASE("Timing mode: send() outside an event goes out stamped with the current frame", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
rack.onLoad = function() {
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    midiOut.send(m);
};
)";
	TimingRig rig(js);
	rig.run(20);

	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].frameField == rig.rec.sent[0].releasedAt);
}

// Tick-scheduled messages

TEST_CASE("Timing mode: a tick-scheduled group leaves in order from the edge's frame", "[MidiKit][timing]") {
	TimingRig rig(withTiming(JS_GROUP_AFTER_TRIGGER).c_str());
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	rig.run(8);
	rig.inject(noteOn(0, 1, 100), 8);   // an NRPN, due on the next tick
	rig.run(45);
	REQUIRE(rig.rec.sent.empty());

	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	rig.step();   // the edge is on frame 45
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);

	REQUIRE(controllers(rig.rec) == std::vector<int>{99, 98, 6, 38});
	// The first member sits on the edge, the others one sample apart behind it.
	for (int i = 0; i < 4; i++) REQUIRE(rig.rec.sent[i].frameField == 45 + i);
}

// On a sample where a trigger edge and a divider tick coincide, the tick queue
// drains before the script's out-queue, so a tick-scheduled message reaches the
// device ahead of an immediate one produced on that same sample. The two are
// unrelated, so this is fine; it is pinned so that a change to process() cannot
// swap it unnoticed.
TEST_CASE("Tick-scheduled messages are released before the immediate messages of the same sample", "[MidiKit][timing]") {
	const char* script = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
midi.onMessage = function(port, msg) {
    if (midi.getNote(msg) === 60) midiOut.sendAfterTrigger(msg, 1);
    else midiOut.send(msg);
};
)";
	for (bool timing : { false, true }) {
		CATCH_INFO(timing);
		TimingRig rig(timing ? withTiming(script).c_str() : script);
		rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

		rig.run(8);
		rig.inject(noteOn(0, 60, 100), 8);    // waits for the edge
		rig.inject(noteOn(0, 61, 100), 20);   // sent at once, on the tick at 23
		rig.run(23);
		REQUIRE(rig.rec.sent.empty());

		// Frame 23 is a divider tick and carries the edge.
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		rig.step();
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);

		REQUIRE(controllers(rig.rec) == std::vector<int>{60, 61});
		if (timing) requireOrderedFrames(rig.rec);
	}
}

// Late-message report: midiOut.enableTiming(true)
// Rack places a framed message one block after its frame, so a message handed
// over a whole block past its frame is already late. The engine's block start
// and size are zero headless, so a mock supplies them.

struct BlockMock : StoermelderPackOne::vcv::EngineAccess {
	int64_t blockFrame = 0;
	int64_t blockFrames = 0;
	int64_t getBlockFrame() const override { return blockFrame; }
	int64_t getBlockFrames() const override { return blockFrames; }
};

struct BlockMockScope {
	BlockMock mock;
	StoermelderPackOne::vcv::EngineAccess* previous;
	BlockMockScope(int64_t blockFrame, int64_t blockFrames) : previous(StoermelderPackOne::vcv::engineAccess) {
		mock.blockFrame = blockFrame;
		mock.blockFrames = blockFrames;
		StoermelderPackOne::vcv::engineAccess = &mock;
	}
	~BlockMockScope() { StoermelderPackOne::vcv::engineAccess = previous; }
};

static size_t countOf(const std::string& text, const std::string& what) {
	size_t n = 0;
	for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) n++;
	return n;
}

TEST_CASE("Timing report: a message handed over a block past its frame is logged", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		// The block being processed started at 1000; the reply is for frame 20.
		BlockMockScope blocks(1000, 256);
		TimingRig rig(withTiming(script, "true").c_str());
		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);

		std::string log = drainLog(rig.m);
		REQUIRE(countOf(log, "reached the output too late") == 1);
	}
}

TEST_CASE("Timing report: a message with time left is not logged", "[MidiKit][timing]") {
	BlockMockScope blocks(100, 256);
	TimingRig rig(withTiming(JS_PASS_THROUGH, "true").c_str());
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	REQUIRE(countOf(drainLog(rig.m), "reached the output too late") == 0);
}

TEST_CASE("Timing report: nothing is logged unless the script asked for it", "[MidiKit][timing]") {
	BlockMockScope blocks(1000, 256);

	SECTION("enableTiming() without the flag") {
		TimingRig rig(withTiming(JS_PASS_THROUGH).c_str());
		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);
		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(countOf(drainLog(rig.m), "reached the output too late") == 0);
	}

	SECTION("legacy mode") {
		TimingRig rig(JS_PASS_THROUGH);
		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);
		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(countOf(drainLog(rig.m), "reached the output too late") == 0);
	}

	SECTION("the flag is forgotten on reload") {
		TimingRig rig(withTiming(JS_PASS_THROUGH, "true").c_str());
		rig.m->loadScript(withTiming(JS_PASS_THROUGH));
		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);
		REQUIRE(countOf(drainLog(rig.m), "reached the output too late") == 0);
	}
}

TEST_CASE("Timing report: a script that keeps falling behind is logged once per second", "[MidiKit][timing]") {
	BlockMockScope blocks(1000000, 256);
	TimingRig rig(withTiming(JS_PASS_THROUGH, "true").c_str());
	int64_t second = int64_t(rig.m->sampleRate.load());

	rig.inject(noteOn(0, 60, 100), 20);
	rig.inject(noteOn(0, 61, 100), 30);
	rig.inject(noteOn(0, 62, 100), 40);
	rig.run(200);
	// One line for the first; the other two wait for the next second.
	std::string first = drainLog(rig.m);
	REQUIRE(countOf(first, "reached the output too late") == 1);

	// The two that waited are reported as soon as the second is over.
	rig.inject(noteOn(0, 63, 100), second + 100);
	rig.run(second + 200);
	std::string later = drainLog(rig.m);
	REQUIRE(countOf(later, "reached the output too late") == 1);
}

TEST_CASE("Timing mode: a frame-less message does not overtake earlier messages of the same callback", "[MidiKit][timing]") {
	// The first message carries the arrival frame, the second has no frame. The
	// frames increase either way, so the message sequence is what is compared.
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
midi.onMessage = function(port, msg) {
    let a = midi.create();
    midi.setNoteOn(a, 1, 60, 100);
    midiOut.send(a);
    let b = midi.create();
    midi.setNoteOn(b, 1, 61, 100);
    midiOut.sendAtFrame(b, -1);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
midiOut.enableTiming()
midi.onMessage = function(port, msg)
    local a = midi.create()
    midi.setNoteOn(a, 1, 60, 100)
    midiOut.send(a)
    local b = midi.create()
    midi.setNoteOn(b, 1, 61, 100)
    midiOut.sendAtFrame(b, -1)
end
)";
	for (const char* script : { js, lua }) {
		CATCH_INFO(script);
		TimingRig rig(script);
		rig.inject(noteOn(0, 1, 100), 20);
		rig.run(40);

		REQUIRE(controllers(rig.rec) == std::vector<int>{60, 61});
		requireOrderedFrames(rig.rec);
	}
}

TEST_CASE("sendAfterMs from rack.onLoad counts from the engine frame when no process() has run yet", "[MidiKit][timing]") {
	// The script loads before the module's first process(), with Rack already
	// well into the session: counting from frame 0 would make the message due at
	// once.
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    midiOut.sendAfterMs(m, 10);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local m = midi.create()
    midi.setNoteOn(m, 1, 60, 100)
    midiOut.sendAfterMs(m, 10)
end
)";
	for (const char* script : { js, lua }) {
		for (bool timing : { false, true }) {
			CATCH_INFO(script);
			CATCH_INFO(timing);
			EngineFrameScope engine(1000000);
			TimingRig rig(timing ? withTiming(script).c_str() : script);
			int64_t delay = int64_t(10.0 / 1000.0 * rig.m->sampleRate.load());

			rig.frame = 1000000;
			rig.run(1000010);

			REQUIRE(rig.rec.sent.empty());
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
			REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 1000000 + delay);
		}
	}
}

TEST_CASE("sendAfterMs keeps full precision in both engines", "[MidiKit][timing]") {
	// 16777217 ms (4.7 hours) has no float representation; both engines must
	// land on the same frame.
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, 16777217);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
midiOut.enableTiming()
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(msg, 16777217)
end
)";
	for (const char* script : { js, lua }) {
		CATCH_INFO(script);
		TimingRig rig(script);
		double sr = rig.m->sampleRate.load();
		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);

		REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
		REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 20 + int64_t(16777217.0 / 1000.0 * sr));
	}
}

TEST_CASE("Each send call on a handle schedules its own copy, in both engines", "[MidiKit][timing]") {
	// sendAfterTrigger() schedules by tick; a later send, sendAfterMs or
	// sendAtFrame of the same handle is a second message with its own schedule,
	// not a replacement for the first.
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
midi.onMessage = function(port, msg) {
    let n = midi.getNote(msg);
    let m = midi.clone(msg);
    midiOut.sendAfterTrigger(m, 5);
    if (n === 1) midiOut.send(m);
    else if (n === 2) midiOut.sendAfterMs(m, 10);
    else midiOut.sendAtFrame(m, 200);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
midi.onMessage = function(port, msg)
    local n = midi.getNote(msg)
    local m = midi.clone(msg)
    midiOut.sendAfterTrigger(m, 5)
    if n == 1 then midiOut.send(m)
    elseif n == 2 then midiOut.sendAfterMs(m, 10)
    else midiOut.sendAtFrame(m, 200) end
end
)";
	for (const char* script : { js, lua }) {
		for (int note : { 1, 2, 3 }) {
			CATCH_INFO(script);
			CATCH_INFO(note);
			TimingRig rig(script);
			rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
			rig.run(8);
			rig.inject(noteOn(0, note, 100), 8);
			rig.run(40);

			size_t ticks = 0;
			for (int i = 0; i < 2 * PORT_MAX_CHANNELS; i++) ticks += rig.m->midiOuts.ports[0].tickQueue[i].size();
			// The sendAfterTrigger copy still waits for its 5 clock edges...
			REQUIRE(ticks == 1);
			// ...and the second call's copy is sent at once, or held in the
			// frame queue until its frame.
			REQUIRE(rig.rec.sent.size() + rig.m->midiOuts.ports[0].frameQueue.size() == 1);
		}
	}
}

TEST_CASE("sendAfterMs with -1 waits two blocks and a frame after the base frame", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, -1);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(msg, -1)
end
)";
	for (const char* script : { js, lua }) {
		for (int64_t block : { 64, 256, 2048 }) {
			for (bool timing : { false, true }) {
				CATCH_INFO(script);
				CATCH_INFO(block);
				CATCH_INFO(timing);
				BlockMockScope blocks(0, block);
				TimingRig rig(timing ? withTiming(script).c_str() : script);
				rig.inject(noteOn(0, 60, 100), 20);
				rig.run(40);

				// Input at 20, dispatched on the divider tick at 23: timing mode counts
				// from the input, legacy mode from the frame the script ran on.
				int64_t base = timing ? 20 : 23;
				REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
				REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == base + 2 * block + 1);
			}
		}
	}
}


// Trigger output writes made inside an event are stamped with the event's
// frame plus one block in timing mode, and applied on the audio thread when that
// frame comes up, instead of whenever the worker ran the script.
static const char* JS_TRIG_STAMP = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
midi.onMessage = function(port, msg) {
    trig.setHigh(1);
};
)";

static const char* LUA_TRIG_STAMP = R"(--[[
@engine minilua@v1
--]]
midiOut.enableTiming()
midi.onMessage = function(port, msg)
    trig.setHigh(1)
end
)";

static const char* JS_TRIG_NO_TIMING = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    trig.setHigh(1);
};
)";

// The first frame (after `from`) on which trigger output 1 reads high.
static int64_t firstHighFrame(TimingRig& rig, int64_t from, int64_t until) {
	while (rig.frame < until) {
		int64_t f = rig.frame;
		rig.step();
		if (f >= from && rig.m->outputs[MidiKitModule::OUTPUT_TRIG + 0].getVoltage(0) > 5.f) return f;
	}
	return -1;
}

TEST_CASE("Timing: a trigger written in an event is applied on its stamped frame", "[MidiKit][timing]") {
	// Headless the block size is 0, which would make a stamp indistinguishable
	// from the divider tick the script runs on; a 64-frame block separates them.
	BlockMockScope blocks(0, 64);
	for (const char* script : {JS_TRIG_STAMP, LUA_TRIG_STAMP}) {
		CATCH_INFO(script);
		TimingRig rig(script);

		// Input due at 20 → the script runs on the divider tick at 23, but the
		// write is stamped 20 + one block, whatever frame the worker was at.
		rig.inject(noteOn(0, 60, 100), 20);
		REQUIRE(firstHighFrame(rig, 0, 2000) == 20 + 64);
	}
}

TEST_CASE("Timing: without enableTiming a trigger is written when the script runs", "[MidiKit][timing]") {
	TimingRig rig(JS_TRIG_NO_TIMING);

	rig.inject(noteOn(0, 60, 100), 20);
	// status quo for unstamped writes: the divider tick that dispatched it.
	REQUIRE(firstHighFrame(rig, 0, 200) == nextDividerTick(20));
}

TEST_CASE("Timing: stamped trigger writes are applied in frame order", "[MidiKit][timing]") {
	TimingRig rig(JS_TRIG_STAMP);
	auto& m = *rig.m;
	rig.step();   // the first process() carries out the audio thread's half of the load

	// Two writes pushed out of order: the earlier frame must win its slot, so the
	// output ends low (the later write is setLow).
	m.setTrigVoltage(0, 0, 0.f, 120);
	m.setTrigVoltage(0, 0, 10.f, 100);
	rig.run(110);
	REQUIRE(m.outputs[MidiKitModule::OUTPUT_TRIG + 0].getVoltage(0) == 10.f);
	rig.run(130);
	REQUIRE(m.outputs[MidiKitModule::OUTPUT_TRIG + 0].getVoltage(0) == 0.f);
}

TEST_CASE("Timing: stamped trigger writes for the same frame keep the order written", "[MidiKit][timing]") {
	TimingRig rig(JS_TRIG_STAMP);
	auto& m = *rig.m;
	rig.step();   // the first process() carries out the audio thread's half of the load

	// High then low for frame 100: the low must win, as in an unstamped script.
	m.setTrigVoltage(0, 0, 10.f, 100);
	m.setTrigVoltage(0, 0, 0.f, 100);
	rig.run(120);
	REQUIRE(m.outputs[MidiKitModule::OUTPUT_TRIG + 0].getVoltage(0) == 0.f);

	// And low then high ends high.
	m.setTrigVoltage(0, 0, 0.f, 140);
	m.setTrigVoltage(0, 0, 10.f, 140);
	rig.run(160);
	REQUIRE(m.outputs[MidiKitModule::OUTPUT_TRIG + 0].getVoltage(0) == 10.f);
}

TEST_CASE("Timing: a script reload drops stamped trigger writes that are still pending", "[MidiKit][timing]") {
	TimingRig rig(JS_TRIG_STAMP);
	auto& m = *rig.m;
	rig.step();   // the first process() carries out the audio thread's half of the load

	// One write ends up in the pending queue (the step moves it there), the other
	// is still in the ring when the reload comes; both must be dropped.
	m.setTrigVoltage(0, 0, 10.f, 100);
	rig.step();
	m.setTrigVoltage(0, 0, 10.f, 110);

	m.loadScript(JS_TRIG_STAMP);
	rig.run(200);
	REQUIRE(m.outputs[MidiKitModule::OUTPUT_TRIG + 0].getVoltage(0) == 0.f);
}


// The scheduling queues are reserved up front and bounded, so scheduling itself
// never allocates on the audio thread (the one copy left is Rack's own
// Output::sendMessage(), on its side). A message that doesn't fit goes out at once
// instead of being dropped (a dropped Note-Off would leave a note stuck), and
// the log says so once.

TEST_CASE("A full frame-scheduling queue sends the overflow at once and logs it once", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    for (let i = 0; i < 100; i++) midiOut.sendAfterMs(msg, 10000);
};
)";
	TimingRig rig(js);
	const size_t cap = MidiOutput<1>::FRAME_QUEUE_MAX;
	REQUIRE(cap == 256);
	// 4 callbacks x 100 = 400 scheduled; each callback fits the 128-entry
	// hand-off ring, and the audio thread drains it in between.
	for (int i = 0; i < 4; i++) {
		rig.inject(noteOn(0, 60 + i, 100), 10 + 20 * i);
	}
	rig.run(120);

	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == cap);
	REQUIRE(rig.rec.sent.size() == 400 - cap);

	std::string log = drainLog(rig.m);
	size_t first = log.find("schedule queue full");
	REQUIRE(first != std::string::npos);
	REQUIRE(log.find("schedule queue full", first + 1) == std::string::npos);
}

TEST_CASE("A full trigger-tick queue sends the overflow at once and logs it once", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
midi.onMessage = function(port, msg) {
    for (let i = 0; i < 20; i++) midiOut.sendAfterTrigger(msg, 1000);
};
)";
	TimingRig rig(js);
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	const size_t cap = MidiOutput<1>::TICK_QUEUE_MAX;
	REQUIRE(cap == 32);
	rig.run(8);
	// 3 callbacks x 20 = 60 scheduled against the same trigger clock.
	for (int i = 0; i < 3; i++) {
		rig.inject(noteOn(0, 60 + i, 100), 10 + 20 * i);
	}
	rig.run(100);

	REQUIRE(rig.m->midiOuts.ports[0].tickQueue[0].size() == cap);
	REQUIRE(rig.rec.sent.size() == 60 - cap);

	std::string log = drainLog(rig.m);
	REQUIRE(log.find("schedule queue full") != std::string::npos);
}

TEST_CASE("A script swap re-arms the schedule-queue-full log line", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    for (let i = 0; i < 100; i++) midiOut.sendAfterMs(msg, 10000);
};
)";
	TimingRig rig(js);
	auto overflow = [&](int64_t from) {
		for (int i = 0; i < 4; i++) rig.inject(noteOn(0, 60 + i, 100), from + 10 + 20 * i);
		rig.run(from + 120);
	};

	overflow(0);
	REQUIRE(drainLog(rig.m).find("schedule queue full") != std::string::npos);

	// More overflow under the same script stays quiet.
	overflow(rig.frame);
	REQUIRE(drainLog(rig.m).find("schedule queue full") == std::string::npos);

	// A reload is a new script: its first overflow is reported again.
	rig.m->loadScript(js);
	rig.run(rig.frame + 8);   // the audio thread catches up with the new generation
	drainLog(rig.m);
	overflow(rig.frame);
	REQUIRE(drainLog(rig.m).find("schedule queue full") != std::string::npos);
}


// ── Bursts: a 2048-entry ring, drained 128 entries at a time ─────────────────
// The ring absorbs a burst and the audio thread hands it on in bounded steps,
// one per divider tick, so one process() call never works through all of it.

static midi::Message burstNote(int note) {
	return noteOn(0, note, 100);
}

TEST_CASE("A burst of 1000 sends in one callback arrives whole and in order", "[MidiKit][timing]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let m = midi.create();
    for (let i = 0; i < 1000; i++) {
        midi.setNoteOn(m, 1, i % 128, 100);
        midiOut.send(m);
    }
};
)";
	TimingRig rig(js);
	rig.inject(noteOn(0, 60, 100), 10);
	rig.run(200);

	REQUIRE(rig.rec.sent.size() == 1000);
	for (size_t i = 0; i < 1000; i++) REQUIRE(rig.rec.sent[i].note == i % 128);
	REQUIRE(drainLog(rig.m).find("queue full") == std::string::npos);
}

TEST_CASE("One drain hands the ports at most DRAIN_BUDGET entries", "[MidiKit][timing]") {
	TimingRig rig(JS_PASS_THROUGH);
	auto& outs = rig.m->midiOuts;
	const int budget = std::decay<decltype(outs)>::type::DRAIN_BUDGET;
	REQUIRE(budget == 128);
	REQUIRE(outs.queue.capacity() == 2048);

	midi::Message msg = burstNote(60);
	for (int i = 0; i < 1000; i++) REQUIRE(outs.enqueue(0, &msg, 1, 0, 0));

	size_t expected = 0;
	for (int drain = 0; drain < 8; drain++) {
		outs.process(0, 48000.f);
		expected = std::min<size_t>(1000, expected + budget);
		REQUIRE(rig.rec.sent.size() == expected);
	}
	REQUIRE(outs.queue.empty());
}

TEST_CASE("A replaced script's leftover burst is discarded; its onUnload output and the new script's messages arrive", "[MidiKit][timing]") {
	TimingRig rig(JS_PASS_THROUGH);
	auto& outs = rig.m->midiOuts;

	// 300 messages of the replaced script (generation 0) that are not due, its
	// onUnload() output, then the new script's (generation 1).
	midi::Message later = burstNote(7);
	later.frame = 1000000;
	for (int i = 0; i < 300; i++) REQUIRE(outs.enqueue(0, &later, 1, 0, 0, 0, 0));
	midi::Message unload1 = burstNote(50);
	midi::Message unload2 = burstNote(51);
	REQUIRE(outs.enqueue(0, &unload1, 1, 0, 0, 0, 0, true));
	REQUIRE(outs.enqueue(0, &unload2, 1, 0, 0, 0, 0, true));
	for (int note = 60; note < 63; note++) {
		midi::Message fresh = burstNote(note);
		REQUIRE(outs.enqueue(0, &fresh, 1, 0, 0, 0, 1));
	}

	// The audio thread is on generation 1. The leftovers go at up to 128 per
	// drain, and nothing is delivered until they are through.
	outs.process(0, 48000.f, 1);
	outs.process(0, 48000.f, 1);
	REQUIRE(rig.rec.sent.empty());
	outs.process(0, 48000.f, 1);
	REQUIRE(rig.rec.sent.size() == 5);
	REQUIRE(rig.rec.sent[0].note == 50);
	REQUIRE(rig.rec.sent[1].note == 51);
	REQUIRE(rig.rec.sent[2].note == 60);
	REQUIRE(rig.rec.sent[4].note == 62);
	REQUIRE(outs.queue.empty());
}

TEST_CASE("The output ring takes 2048 entries and drops whole groups beyond them", "[MidiKit][timing]") {
	TimingRig rig(JS_PASS_THROUGH);
	auto& outs = rig.m->midiOuts;
	midi::Message msg = burstNote(60);
	for (int i = 0; i < 2047; i++) REQUIRE(outs.enqueue(0, &msg, 1, 0, 0));

	midi::Message group[4] = {msg, msg, msg, msg};
	REQUIRE_FALSE(outs.enqueue(0, group, 4, 0, 0));
	REQUIRE(outs.overflow.load());
	REQUIRE(outs.queue.size() == 2047);       // nothing of the group went in
	REQUIRE(outs.enqueue(0, &msg, 1, 0, 0));  // the last slot is still usable
	REQUIRE(outs.queue.full());
}

TEST_CASE("Timing: frames arriving out of order are released by frame", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(script);

		// The later frame arrives first; it must not hold the earlier one back.
		rig.inject(noteOn(0, 61, 100), 64);
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(100);

		REQUIRE(rig.rec.sent.size() == 2);
		REQUIRE(rig.rec.sent[0].note == 60);
		REQUIRE(rig.rec.sent[0].releasedAt == nextDividerTick(8));
		REQUIRE(rig.rec.sent[1].note == 61);
		REQUIRE(rig.rec.sent[1].releasedAt == nextDividerTick(64));
	}
}

TEST_CASE("Input queue overflow raises one notice per saturation", "[MidiKit][timing]") {
	TimingRig rig(JS_PASS_THROUGH);

	// Past the arrival ring's capacity within one drain.
	for (int i = 0; i < 300; i++) rig.inject(noteOn(0, 60, 100), 1000000);
	rig.run(40);

	int lines = 0;
	std::tuple<LOG_FORMAT, float, std::string> t;
	while (rig.m->log.tryPop(t)) {
		if (std::get<2>(t) == "MIDI input queue full, message(s) dropped") lines++;
	}
	REQUIRE(lines == 1);
}
