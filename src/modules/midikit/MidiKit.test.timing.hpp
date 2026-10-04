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
		uint8_t channel;
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
		s.channel = msg.getChannel();
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
        midi.setCc14bit(cc14, 1, 1, 12864);
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
        midi.setCc14bit(cc14, 1, 1, 12864)
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
        midi.setCc14bit(cc14, 1, 1, 12864);
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
	std::shared_ptr<StoermelderPackOne::MidiScript::WorkerDomain> ownedDomain;

	explicit FrameProbeEngine(MidiKitModule* module) : MidiScriptEngine(module, 4, 1, 1, 4, 1, 1) {
		ownedDomain = std::make_shared<StoermelderPackOne::MidiScript::WorkerDomain>(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
		setDomain(ownedDomain.get());
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
	void dispatchBroadcast(const StoermelderPackOne::MidiScript::InboundBroadcast&) override { }

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
	void invokeContextMenuCallback(int, const StoermelderPackOne::MidiScript::ScriptMenuClick&) override {}
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
	ScriptLog::Entry t;
	while (rig.m->log.tryPop(t)) {
		if (std::get<2>(t) == "MIDI input queue full, message(s) dropped") lines++;
	}
	REQUIRE(lines == 1);
}


// ── midiOut.cancel() ────────────────────────────────────────────────────────
// Matching rules are in MidiKit.test.cancel.hpp; these go through the module:
// worker -> ring -> port queues -> recorder. Every case runs in both engines.

static const char* JS_CANCEL_HEAD = R"(/**
 * @engine QuickJs@v1
 */
function off(n) { let m = midi.create(); midi.setNoteOff(m, 1, n, 0); return m; }
)";

static const char* LUA_CANCEL_HEAD = R"(--[[
@engine minilua@v1
--]]
local function off(n) local m = midi.create(); midi.setNoteOff(m, 1, n, 0); return m end
)";

struct CancelScripts {
	std::string js;
	std::string lua;
	std::string get(bool isLua) const { return isLua ? lua : js; }
};

static CancelScripts cancelScripts(const std::string& js, const std::string& lua) {
	return { std::string(JS_CANCEL_HEAD) + js, std::string(LUA_CANCEL_HEAD) + lua };
}

// The note numbers of everything the recorder got, in send order.
static std::vector<int> sentNotes(const TimingRecorder& rec) {
	std::vector<int> notes;
	for (const TimingRecorder::Sent& s : rec.sent) notes.push_back(s.note);
	return notes;
}

// Frames to run so that a 10 ms delay scheduled at about frame 20 is long over.
static int64_t cancelRunUntil() {
	return 100 + 2 * int64_t(0.010 * Test::sampleRate());
}

TEST_CASE("Cancel: a message handle drops only the scheduled messages with its address", "[MidiKit][cancel][timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    midiOut.sendAfterMs(off(61), 10);
    midiOut.cancel(off(60));
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.sendAfterMs(off(61), 10)
    midiOut.cancel(off(60))
end
)");
	for (bool lua : { false, true }) {
		for (bool timing : { false, true }) {
			CATCH_INFO(std::string(lua ? "lua" : "js") + (timing ? " timing" : " legacy"));
			std::string script = timing ? withTiming(s.get(lua).c_str()) : s.get(lua);
			TimingRig rig(script.c_str());
			rig.inject(noteOn(0, 60, 100), 8);
			rig.run(cancelRunUntil());

			REQUIRE(sentNotes(rig.rec) == std::vector<int>{61});
			REQUIRE(rig.rec.sent[0].status == 0x8);
		}
	}
}

TEST_CASE("Cancel: a trigger-scheduled message is dropped before its edge", "[MidiKit][cancel][timing]") {
	CancelScripts s = cancelScripts(R"(
trig.enableIn(1);
midi.onMessage = function(port, msg) {
    midiOut.sendAfterTrigger(off(60), 1);
    midiOut.sendAfterTrigger(off(61), 1);
    midiOut.cancel(off(60));
};
)", R"(
trig.enableIn(1)
midi.onMessage = function(port, msg)
    midiOut.sendAfterTrigger(off(60), 1)
    midiOut.sendAfterTrigger(off(61), 1)
    midiOut.cancel(off(60))
end
)");
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		TimingRig rig(s.get(lua).c_str());
		rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

		rig.run(8);
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(45);
		REQUIRE(rig.rec.sent.empty());

		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		rig.step();
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		rig.run(80);

		REQUIRE(sentNotes(rig.rec) == std::vector<int>{61});
	}
}

TEST_CASE("Cancel: without an argument it clears the selected port only", "[MidiKit][cancel][timing]") {
	CancelScripts s = cancelScripts(R"(
midiOut.enablePorts(2);
midi.onMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.sendAfterMs(off(60), 10);
    midiOut.sendAfterMs(off(61), 10);
    midiOut.selectPort(2);
    midiOut.sendAfterMs(off(62), 10);
    midiOut.selectPort(1);
    midiOut.cancel();
};
)", R"(
midiOut.enablePorts(2)
midi.onMessage = function(port, msg)
    midiOut.selectPort(1)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.sendAfterMs(off(61), 10)
    midiOut.selectPort(2)
    midiOut.sendAfterMs(off(62), 10)
    midiOut.selectPort(1)
    midiOut.cancel()
end
)");
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		// Declared first: it must outlive the rig, whose teardown flushes through it.
		TimingRecorder rec2;
		TimingRig rig(s.get(lua).c_str());
		rig.m->midiOuts.ports[1].outputDevice = &rec2;
		rig.m->midiOuts.ports[1].channel = -1;
		rig.inject(noteOn(0, 60, 100), 8);
		rec2.now = 0;
		for (int64_t f = rig.frame; f < cancelRunUntil(); f++) {
			rec2.now = f;
			rig.step();
		}

		REQUIRE(rig.rec.sent.empty());
		REQUIRE(sentNotes(rec2) == std::vector<int>{62});
		rig.m->midiOuts.ports[1].outputDevice = nullptr;
	}
}

TEST_CASE("Cancel: a timing-mode send() in the same callback is not cancelled", "[MidiKit][cancel][timing]") {
	// In timing mode send() waits in the frame queue until the end of the pump,
	// next to the scheduled messages: only the `scheduled` flag tells them apart.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.send(off(60));
    midiOut.cancel();
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.send(off(60))
    midiOut.cancel()
end
)");
	for (bool lua : { false, true }) {
		for (bool timing : { false, true }) {
			CATCH_INFO(std::string(lua ? "lua" : "js") + (timing ? " timing" : " legacy"));
			std::string script = timing ? withTiming(s.get(lua).c_str()) : s.get(lua);
			TimingRig rig(script.c_str());
			rig.inject(noteOn(0, 60, 100), 8);
			rig.run(60);

			REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
		}
	}
}

TEST_CASE("Cancel: sendAtFrame with a negative frame is a plain send", "[MidiKit][cancel][timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAtFrame(off(60), -1);
    midiOut.cancel();
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAtFrame(off(60), -1)
    midiOut.cancel()
end
)");
	for (bool lua : { false, true }) {
		for (bool timing : { false, true }) {
			CATCH_INFO(std::string(lua ? "lua" : "js") + (timing ? " timing" : " legacy"));
			std::string script = timing ? withTiming(s.get(lua).c_str()) : s.get(lua);
			TimingRig rig(script.c_str());
			rig.inject(noteOn(0, 60, 100), 8);
			rig.run(60);

			REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
		}
	}
}

TEST_CASE("Cancel: it applies in call order", "[MidiKit][cancel][timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    midiOut.cancel();
    midiOut.sendAfterMs(off(61), 10);
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.cancel()
    midiOut.sendAfterMs(off(61), 10)
end
)");
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		TimingRig rig(s.get(lua).c_str());
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(cancelRunUntil());

		REQUIRE(sentNotes(rig.rec) == std::vector<int>{61});
	}
}

TEST_CASE("Cancel: a group is taken whole by its handle and never split by a member", "[MidiKit][cancel][timing]") {
	// The incoming note picks the cancel: 1 = a plain CC 99 (a member of the
	// scheduled NRPN), 2 = the NRPN handle itself, 3 = another NRPN number.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    let n = midi.createNRPN();
    midi.setNRPN(n, 1, 300, 1000);
    midiOut.sendAfterMs(n, 10);
    let k = midi.getNote(msg);
    if (k == 1) { let c = midi.create(); midi.setCc(c, 1, 99, 2); midiOut.cancel(c); }
    else if (k == 2) { midiOut.cancel(n); }
    else if (k == 3) { let o = midi.createNRPN(); midi.setNRPN(o, 1, 301, 0); midiOut.cancel(o); }
};
)", R"(
midi.onMessage = function(port, msg)
    local n = midi.createNRPN()
    midi.setNRPN(n, 1, 300, 1000)
    midiOut.sendAfterMs(n, 10)
    local k = midi.getNote(msg)
    if k == 1 then local c = midi.create(); midi.setCc(c, 1, 99, 2); midiOut.cancel(c)
    elseif k == 2 then midiOut.cancel(n)
    elseif k == 3 then local o = midi.createNRPN(); midi.setNRPN(o, 1, 301, 0); midiOut.cancel(o)
    end
end
)");
	struct Case { int note; std::vector<int> controllers; };
	// Number 300 = 2 * 128 + 44: CC 99 and 98 select it, CC 6 and 38 carry the value.
	std::vector<Case> cases = {
		{ 1, { 99, 98, 6, 38 } },
		{ 2, { } },
		{ 3, { 99, 98, 6, 38 } },
	};
	for (bool lua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(lua ? "lua" : "js") + " note " + std::to_string(c.note));
			TimingRig rig(s.get(lua).c_str());
			rig.inject(noteOn(0, c.note, 100), 8);
			rig.run(cancelRunUntil());

			REQUIRE(sentNotes(rig.rec) == c.controllers);
		}
	}
}

TEST_CASE("Cancel: a message already past its frame is not recalled", "[MidiKit][cancel][timing]") {
	// Note 60 schedules for 1 ms; note 61, long after, cancels what it was.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    if (midi.getNote(msg) == 60) midiOut.sendAfterMs(off(60), 1);
    else midiOut.cancel(off(60));
};
)", R"(
midi.onMessage = function(port, msg)
    if midi.getNote(msg) == 60 then midiOut.sendAfterMs(off(60), 1)
    else midiOut.cancel(off(60)) end
end
)");
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		TimingRig rig(s.get(lua).c_str());
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(300);
		REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});

		rig.inject(noteOn(0, 61, 100), 320);
		rig.run(400);
		REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
	}
}

TEST_CASE("Cancel: 40 cancels in one callback all apply, over several drains", "[MidiKit][cancel][timing]") {
	// A cancel weighs 8 of the drain's 128 units, so 40 of them (and the 41 sends
	// before them) need more than one drain. One handle is reused: a callback has
	// only a few message slots.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    let m = midi.create();
    for (let i = 0; i < 40; i++) { midi.setNoteOff(m, 1, i, 0); midiOut.sendAfterMs(m, 10); }
    midi.setNoteOff(m, 1, 100, 0);
    midiOut.sendAfterMs(m, 10);
    for (let i = 0; i < 40; i++) { midi.setNoteOff(m, 1, i, 0); midiOut.cancel(m); }
};
)", R"(
midi.onMessage = function(port, msg)
    local m = midi.create()
    for i = 0, 39 do midi.setNoteOff(m, 1, i, 0); midiOut.sendAfterMs(m, 10) end
    midi.setNoteOff(m, 1, 100, 0)
    midiOut.sendAfterMs(m, 10)
    for i = 0, 39 do midi.setNoteOff(m, 1, i, 0); midiOut.cancel(m) end
end
)");
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		TimingRig rig(s.get(lua).c_str());
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(cancelRunUntil());

		REQUIRE(sentNotes(rig.rec) == std::vector<int>{100});
	}
}

TEST_CASE("Cancel: bad arguments raise a script error and cancel nothing", "[MidiKit][cancel][timing]") {
	CancelScripts s = cancelScripts(R"(
function t(name, f) {
    try { f(); rack.log("ok " + name); }
    catch (e) { rack.log("err " + name); }
}
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    t("empty", function() { midiOut.cancel(midi.create()); });
    t("unsetNrpn", function() { midiOut.cancel(midi.createNRPN()); });
    t("twoArgs", function() { midiOut.cancel(off(60), off(61)); });
    t("notHandle", function() { midiOut.cancel("x"); });
    t("staleHandle", function() { midiOut.cancel(123456789); });
};
)", R"(
local function t(name, f)
    local ok = pcall(f)
    if ok then rack.log("ok " .. name) else rack.log("err " .. name) end
end
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    t("empty", function() midiOut.cancel(midi.create()) end)
    t("unsetNrpn", function() midiOut.cancel(midi.createNRPN()) end)
    t("twoArgs", function() midiOut.cancel(off(60), off(61)) end)
    t("notHandle", function() midiOut.cancel("x") end)
    t("staleHandle", function() midiOut.cancel(123456789) end)
end
)");
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		TimingRig rig(s.get(lua).c_str());
		drainLog(rig.m);
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(cancelRunUntil());

		std::string log = drainLog(rig.m);
		CATCH_INFO("log: " << log);
		for (const char* name : { "empty", "unsetNrpn", "twoArgs", "notHandle", "staleHandle" }) {
			CATCH_INFO(name);
			REQUIRE(log.find(std::string("err ") + name) != std::string::npos);
			REQUIRE(log.find(std::string("ok ") + name) == std::string::npos);
		}
		// The scheduled note was not touched by any of the failed calls.
		REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
	}
}


TEST_CASE("Cancel: without an argument it clears every tick queue", "[MidiKit][cancel][timing]") {
	// Messages on trigger input 1 and on input 2, channels 1 and 3. Note 2 skips
	// the cancel: the control that shows all three really are released.
	CancelScripts s = cancelScripts(R"(
trig.enableIn(1, 1);
trig.enableIn(2, 1);
trig.enableIn(2, 3);
midi.onMessage = function(port, msg) {
    midiOut.sendAfterTrigger(off(60), 1, 1, 1);
    midiOut.sendAfterTrigger(off(61), 1, 2, 3);
    midiOut.sendAfterTrigger(off(62), 1, 2, 1);
    if (midi.getNote(msg) == 1) midiOut.cancel();
};
)", R"(
trig.enableIn(1, 1)
trig.enableIn(2, 1)
trig.enableIn(2, 3)
midi.onMessage = function(port, msg)
    midiOut.sendAfterTrigger(off(60), 1, 1, 1)
    midiOut.sendAfterTrigger(off(61), 1, 2, 3)
    midiOut.sendAfterTrigger(off(62), 1, 2, 1)
    if midi.getNote(msg) == 1 then midiOut.cancel() end
end
)");
	struct Case { int note; std::vector<int> sent; };
	std::vector<Case> cases = { { 1, { } }, { 2, { 60, 61, 62 } } };
	for (bool lua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(lua ? "lua" : "js") + " note " + std::to_string(c.note));
			TimingRig rig(s.get(lua).c_str());
			rig.m->inputs[MidiKitModule::INPUT_TRIG + 0].channels = 1;
			rig.m->inputs[MidiKitModule::INPUT_TRIG + 1].channels = 3;

			rig.run(8);
			rig.inject(noteOn(0, c.note, 100), 8);
			rig.run(45);
			REQUIRE(rig.rec.sent.empty());

			rig.m->inputs[MidiKitModule::INPUT_TRIG + 0].setVoltage(10.f, 0);
			for (int ch = 0; ch < 3; ch++) rig.m->inputs[MidiKitModule::INPUT_TRIG + 1].setVoltage(10.f, ch);
			rig.step();
			rig.m->inputs[MidiKitModule::INPUT_TRIG + 0].setVoltage(0.f, 0);
			for (int ch = 0; ch < 3; ch++) rig.m->inputs[MidiKitModule::INPUT_TRIG + 1].setVoltage(0.f, ch);
			rig.run(80);

			std::vector<int> got = sentNotes(rig.rec);
			std::sort(got.begin(), got.end());
			REQUIRE(got == c.sent);
		}
	}
}

TEST_CASE("Cancel: 14-bit CC and RPN handles go through the bindings as groups", "[MidiKit][cancel][timing]") {
	// The incoming note picks the scenario (see `cases`). A scheduled message
	// is a 14-bit CC (MSB 5, so CC 5 and 37) or an RPN 300 (CC 101, 100, 6, 38).
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    let k = midi.getNote(msg);
    if (k <= 3) {
        let h = midi.createCc14bit(); midi.setCc14bit(h, 1, 5, 12864);
        midiOut.sendAfterMs(h, 10);
        if (k == 1) midiOut.cancel(h);
        else if (k == 2) { let c = midi.create(); midi.setCc(c, 1, 5, 1); midiOut.cancel(c); }
        else { let o = midi.createCc14bit(); midi.setCc14bit(o, 1, 6, 12864); midiOut.cancel(o); }
    } else {
        let h = midi.createRPN(); midi.setRPN(h, 1, 300, 1000);
        midiOut.sendAfterMs(h, 10);
        if (k == 4) { let n = midi.createNRPN(); midi.setNRPN(n, 1, 300, 1000); midiOut.cancel(n); }
        else { midiOut.cancel(h); }
    }
};
)", R"(
midi.onMessage = function(port, msg)
    local k = midi.getNote(msg)
    if k <= 3 then
        local h = midi.createCc14bit(); midi.setCc14bit(h, 1, 5, 12864)
        midiOut.sendAfterMs(h, 10)
        if k == 1 then midiOut.cancel(h)
        elseif k == 2 then local c = midi.create(); midi.setCc(c, 1, 5, 1); midiOut.cancel(c)
        else local o = midi.createCc14bit(); midi.setCc14bit(o, 1, 6, 12864); midiOut.cancel(o) end
    else
        local h = midi.createRPN(); midi.setRPN(h, 1, 300, 1000)
        midiOut.sendAfterMs(h, 10)
        if k == 4 then local n = midi.createNRPN(); midi.setNRPN(n, 1, 300, 1000); midiOut.cancel(n)
        else midiOut.cancel(h) end
    end
end
)");
	struct Case { int note; std::vector<int> controllers; };
	std::vector<Case> cases = {
		{ 1, { } },                      // the 14-bit handle removes the pair
		{ 2, { 5, 37 } },                // a plain CC 5 never splits it
		{ 3, { 5, 37 } },                // another MSB controller is another group
		{ 4, { 101, 100, 6, 38 } },      // an NRPN of the same number is not the RPN
		{ 5, { } },                      // the RPN handle removes the quad
	};
	for (bool lua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(lua ? "lua" : "js") + " note " + std::to_string(c.note));
			TimingRig rig(s.get(lua).c_str());
			rig.inject(noteOn(0, c.note, 100), 8);
			rig.run(cancelRunUntil());

			REQUIRE(sentNotes(rig.rec) == c.controllers);
		}
	}
}

TEST_CASE("Cancel: on a port that is not enabled it does nothing, silently", "[MidiKit][cancel][timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    midiOut.selectPort(2);
    midiOut.cancel();
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.selectPort(2)
    midiOut.cancel()
end
)");
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		TimingRig rig(s.get(lua).c_str());
		drainLog(rig.m);
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(cancelRunUntil());

		std::string log = drainLog(rig.m);
		CATCH_INFO("log: " << log);
		REQUIRE(log.find("rror") == std::string::npos);
		REQUIRE(log.find("not enabled") == std::string::npos);
		// Port 1's scheduled message is untouched.
		REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
	}
}


// ── Received NRPN / RPN / 14-bit CC as group handles ───────────────────────
// A received group is rebuilt from its decoded number and value, so what a
// script forwards, reschedules or cancels is the whole group on the wire.

struct GroupInput {
	const char* enable;   // the enable call for the input kind
	const char* hook;     // the callback it reaches
	std::vector<int> in;  // controller, value pairs, flattened, fed on one channel
	std::vector<int> out; // the controllers a forward must produce
};

static std::string groupScript(bool lua, const GroupInput& g, const std::string& body) {
	if (lua) {
		return std::string("--[[\n@engine minilua@v1\n--]]\n") + "midi." + g.enable + "(1)\nmidi." + g.hook + " = function(port, msg)\n" + body + "\nend\n";
	}
	return std::string("/**\n * @engine QuickJs@v1\n */\nmidi.") + g.enable + "(1);\nmidi." + g.hook + " = function(port, msg) {\n" + body + "\n};\n";
}

static void injectGroup(TimingRig& rig, const GroupInput& g, int channel, int64_t frame) {
	for (size_t i = 0; i + 1 < g.in.size(); i += 2) rig.inject(Test::makeMidiMessage(0xb, channel, g.in[i], g.in[i + 1]), frame);
}

static std::vector<GroupInput> groupInputs() {
	return {
		// NRPN 517 (4 * 128 + 5), value 2562 (20 * 128 + 2)
		{ "enableNrpnIn", "onNrpn", { 99, 4, 98, 5, 6, 20, 38, 2 }, { 99, 98, 6, 38 } },
		{ "enableRpnIn", "onRpn", { 101, 4, 100, 5, 6, 20, 38, 2 }, { 101, 100, 6, 38 } },
		// 14-bit CC on MSB controller 7, value 130
		{ "enableCc14bitIn", "onCc14bit", { 7, 1, 39, 2 }, { 7, 39 } },
	};
}

static std::vector<int> sentValues(const TimingRecorder& rec) {
	std::vector<int> v;
	for (const TimingRecorder::Sent& s : rec.sent) v.push_back(s.value);
	return v;
}

TEST_CASE("Received group: midiOut.send forwards the whole group, in wire order", "[MidiKit][cc][timing]") {
	for (bool lua : { false, true }) {
		for (const GroupInput& g : groupInputs()) {
			CATCH_INFO(std::string(lua ? "lua " : "js ") + g.hook);
			TimingRig rig(groupScript(lua, g, lua ? "    midiOut.send(msg)" : "    midiOut.send(msg);").c_str());
			injectGroup(rig, g, 2, 8);
			rig.run(100);

			REQUIRE(sentNotes(rig.rec) == g.out);
			std::vector<int> values;
			for (size_t i = 1; i < g.in.size(); i += 2) values.push_back(g.in[i]);
			REQUIRE(sentValues(rig.rec) == values);
			for (const TimingRecorder::Sent& s : rig.rec.sent) {
				REQUIRE(s.status == 0xb);
				REQUIRE(s.channel == 2);
			}
		}
	}
}

TEST_CASE("Received group: an LSB-only data entry forwards with CC 6 = 0", "[MidiKit][cc][timing]") {
	GroupInput g = { "enableNrpnIn", "onNrpn", { 99, 4, 98, 5, 38, 7 }, {} };
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		TimingRig rig(groupScript(lua, g, lua ? "    midiOut.send(msg)" : "    midiOut.send(msg);").c_str());
		injectGroup(rig, g, 0, 8);
		rig.run(100);

		REQUIRE(sentNotes(rig.rec) == std::vector<int>({ 99, 98, 6, 38 }));
		REQUIRE(sentValues(rig.rec) == std::vector<int>({ 4, 5, 0, 7 }));
	}
}

TEST_CASE("Received group: setChannel moves every message of the group", "[MidiKit][cc][timing]") {
	for (bool lua : { false, true }) {
		for (const GroupInput& g : groupInputs()) {
			CATCH_INFO(std::string(lua ? "lua " : "js ") + g.hook);
			TimingRig rig(groupScript(lua, g, lua ? "    midi.setChannel(msg, 5); midiOut.send(msg)" : "    midi.setChannel(msg, 5); midiOut.send(msg);").c_str());
			injectGroup(rig, g, 0, 8);
			rig.run(100);

			REQUIRE(sentNotes(rig.rec) == g.out);
			for (const TimingRecorder::Sent& s : rig.rec.sent) REQUIRE(s.channel == 4);
		}
	}
}

TEST_CASE("Received group: midiOut.cancel(msg) takes the scheduled group with that number", "[MidiKit][cancel][cc][timing]") {
	// A note schedules NRPN 300 on channel 1 (a created group); the received NRPN
	// then cancels by its own number: 300 removes it, 301 does not.
	static const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);
midi.onMessage = function(port, msg) {
    let n = midi.createNRPN();
    midi.setNRPN(n, 1, 300, 1000);
    midiOut.sendAfterMs(n, 10);
};
midi.onNrpn = function(port, msg) { midiOut.cancel(msg); };
)";
	static const char* lua = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)
midi.onMessage = function(port, msg)
    local n = midi.createNRPN()
    midi.setNRPN(n, 1, 300, 1000)
    midiOut.sendAfterMs(n, 10)
end
midi.onNrpn = function(port, msg) midiOut.cancel(msg) end
)";
	struct Case { int numberLsb; std::vector<int> controllers; };
	// 300 = 2 * 128 + 44
	std::vector<Case> cases = { { 44, { } }, { 45, { 99, 98, 6, 38 } } };
	for (bool isLua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(isLua ? "lua" : "js") + " number LSB " + std::to_string(c.numberLsb));
			TimingRig rig(isLua ? lua : js);
			rig.inject(noteOn(0, 60, 100), 8);
			rig.inject(Test::makeMidiMessage(0xb, 0, 99, 2), 16);
			rig.inject(Test::makeMidiMessage(0xb, 0, 98, c.numberLsb), 16);
			rig.inject(Test::makeMidiMessage(0xb, 0, 6, 1), 16);
			rig.inject(Test::makeMidiMessage(0xb, 0, 38, 1), 16);
			rig.run(cancelRunUntil());

			REQUIRE(sentNotes(rig.rec) == c.controllers);
		}
	}
}


// ── Round trip: what a script creates, a second MIDI-KIT receives ───────────
// Addendum B made received and created groups the same shape; this feeds a
// created group's wire bytes into a receiving module's decoder, so the two ends
// have to agree on the numbers. An empty `expected` means the receiver fires nothing.

struct RoundTrip {
	const char* name;
	const char* create;   // JS statements creating `h`; also valid Lua apart from `let`
	const char* enable;   // the receiver's enable call
	const char* hook;
	std::string expected; // control:value the receiver logs
};

static std::vector<std::string> roundTripProbes(const std::string& sendScript, const RoundTrip& rt, bool lua) {
	TimingRig sender(sendScript.c_str());
	sender.inject(noteOn(0, 60, 100), 8);
	sender.run(100);
	REQUIRE_FALSE(sender.rec.sent.empty());

	std::string recv = lua
		? std::string("--[[\n@engine minilua@v1\n--]]\nmidi.") + rt.enable + "(1)\nmidi." + rt.hook + " = function(port, msg) rack.log('P:' .. midi.getControl(msg) .. ':' .. midi.getValue(msg)) end\n"
		: std::string("/**\n * @engine QuickJs@v1\n */\nmidi.") + rt.enable + "(1);\nmidi." + rt.hook + " = function(port, msg) { rack.log('P:' + midi.getControl(msg) + ':' + midi.getValue(msg)); };\n";
	TimingRig receiver(recv.c_str());
	for (const TimingRecorder::Sent& s : sender.rec.sent) {
		receiver.inject(Test::makeMidiMessage(s.status, s.channel, s.note, s.value), 8);
	}
	receiver.run(100);

	std::vector<std::string> probes;
	std::string log = drainLog(receiver.m);
	size_t pos = 0;
	while (pos < log.size()) {
		size_t nl = log.find('\n', pos);
		if (nl == std::string::npos) break;
		if (log.compare(pos, 2, "P:") == 0) probes.push_back(log.substr(pos + 2, nl - pos - 2));
		pos = nl + 1;
	}
	return probes;
}

TEST_CASE("Round trip: a created group is received as the same number and value", "[MidiKit][MidiProcessor][timing]") {
	std::vector<RoundTrip> cases = {
		{ "NRPN", "const h = midi.createNRPN(); midi.setNRPN(h, 2, 300, 1000);", "enableNrpnIn", "onNrpn", "300:1000" },
		{ "NRPN value 0", "const h = midi.createNRPN(); midi.setNRPN(h, 2, 300, 0);", "enableNrpnIn", "onNrpn", "300:0" },
		{ "NRPN max", "const h = midi.createNRPN(); midi.setNRPN(h, 2, 16383, 16383);", "enableNrpnIn", "onNrpn", "16383:16383" },
		{ "RPN", "const h = midi.createRPN(); midi.setRPN(h, 2, 300, 1000);", "enableRpnIn", "onRpn", "300:1000" },
		// The RPN null is the spec's "no parameter": only the select pair is sent and a
		// receiver fires nothing. NRPN 16383 above is an ordinary parameter.
		{ "RPN 16383 (the null)", "const h = midi.createRPN(); midi.setRPN(h, 2, 16383, 1000);", "enableRpnIn", "onRpn", "" },
		{ "14-bit", "const h = midi.createCc14bit(); midi.setCc14bit(h, 2, 7, 1000);", "enableCc14bitIn", "onCc14bit", "7:1000" },
		{ "14-bit below 128", "const h = midi.createCc14bit(); midi.setCc14bit(h, 2, 7, 100);", "enableCc14bitIn", "onCc14bit", "7:100" },
		{ "14-bit zero", "const h = midi.createCc14bit(); midi.setCc14bit(h, 2, 7, 0);", "enableCc14bitIn", "onCc14bit", "7:0" },
	};
	for (const RoundTrip& rt : cases) {
		for (bool lua : { false, true }) {
			CATCH_INFO(std::string(rt.name) + (lua ? " lua" : " js"));
			std::string create = rt.create;
			if (lua) {
				// The same statements in Lua.
				size_t at;
				while ((at = create.find("const ")) != std::string::npos) create.replace(at, 6, "local ");
				while ((at = create.find(";")) != std::string::npos) create.replace(at, 1, "");
			}
			std::string script = lua
				? std::string("--[[\n@engine minilua@v1\n--]]\nmidi.onMessage = function(port, msg)\n" + create + "\nmidiOut.send(h)\nend\n")
				: std::string("/**\n * @engine QuickJs@v1\n */\nmidi.onMessage = function(port, msg) {\n" + create + "\nmidiOut.send(h);\n};\n");
			std::vector<std::string> expected;
			if (!rt.expected.empty()) expected.push_back(rt.expected);
			REQUIRE(roundTripProbes(script, rt, lua) == expected);
		}
	}
}


TEST_CASE("Received group: forwarding an MSB-only change sends the whole group with CC 38 = 0", "[MidiKit][cc][timing]") {
	// A 7-bit device (99, 98, 6) in msb mode. The forward carries what B.2 rebuilds:
	// a complete group, with the missing LSB as 0.
	for (bool lua : { false, true }) {
		for (const char* hook : { "onNrpn", "onRpn" }) {
			bool rpn = std::string(hook) == "onRpn";
			CATCH_INFO(std::string(lua ? "lua " : "js ") + hook);
			std::string enable = std::string(rpn ? "midi.enableRpnIn(1, null, \"msb\")" : "midi.enableNrpnIn(1, null, \"msb\")");
			std::string script;
			if (lua) {
				size_t at = enable.find("null");
				enable.replace(at, 4, "nil");
				script = "--[[\n@engine minilua@v1\n--]]\n" + enable + "\nmidi." + hook + " = function(port, msg) midiOut.send(msg) end\n";
			}
			else {
				script = "/**\n * @engine QuickJs@v1\n */\n" + enable + ";\nmidi." + hook + " = function(port, msg) { midiOut.send(msg); };\n";
			}
			TimingRig rig(script.c_str());
			int sel = rpn ? 101 : 99;
			rig.inject(Test::makeMidiMessage(0xb, 0, sel, 0), 8);
			rig.inject(Test::makeMidiMessage(0xb, 0, sel - 1, 5), 8);
			rig.inject(Test::makeMidiMessage(0xb, 0, 6, 64), 8);
			rig.run(100);

			REQUIRE(sentNotes(rig.rec) == std::vector<int>({ sel, sel - 1, 6, 38 }));
			REQUIRE(sentValues(rig.rec) == std::vector<int>({ 0, 5, 64, 0 }));
		}
	}
}


TEST_CASE("Received group: cancel by a received handle needs the same channel as the scheduled group", "[MidiKit][cancel][cc][timing]") {
	// groupOf() reads the channel from the handle's bytes and the number from its
	// decode fields; both have to agree between a received and a created group.
	// A created NRPN 300 is scheduled on `created` (1-based). A received NRPN 300
	// or 301 arrives on channel `received` (0-based) and cancels by its handle.
	std::string js = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);
midi.onMessage = function(port, msg) {
    let n = midi.createNRPN();
    midi.setNRPN(n, CREATED, 300, 1000);
    midiOut.sendAfterMs(n, 10);
};
midi.onNrpn = function(port, msg) { midiOut.cancel(msg); };
)";
	std::string lua = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)
midi.onMessage = function(port, msg)
    local n = midi.createNRPN()
    midi.setNRPN(n, CREATED, 300, 1000)
    midiOut.sendAfterMs(n, 10)
end
midi.onNrpn = function(port, msg) midiOut.cancel(msg) end
)";
	struct Case { int created; int received; int numberLsb; bool cancelled; };
	// 300 = 2 * 128 + 44
	std::vector<Case> cases = {
		{ 1, 0, 44, true },    // same channel and number
		{ 1, 1, 44, false },   // same number on another channel: not cancelled
		{ 2, 0, 44, false },   // created on channel 2, received on channel 1
		{ 2, 1, 44, true },    // channel 2 on both sides
		{ 16, 15, 44, true },  // the highest channel
		{ 1, 15, 44, false },
		{ 2, 1, 45, false },   // same channel, other number
	};
	for (bool isLua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(isLua ? "lua" : "js") + " created " + std::to_string(c.created) + " received " + std::to_string(c.received) + " lsb " + std::to_string(c.numberLsb));
			std::string script = isLua ? lua : js;
			size_t at = script.find("CREATED");
			script.replace(at, 7, std::to_string(c.created));
			TimingRig rig(script.c_str());
			rig.inject(noteOn(0, 60, 100), 8);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 99, 2), 16);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 98, c.numberLsb), 16);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 6, 1), 16);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 38, 1), 16);
			rig.run(cancelRunUntil());

			REQUIRE(sentNotes(rig.rec) == (c.cancelled ? std::vector<int>{} : std::vector<int>({ 99, 98, 6, 38 })));
			for (const TimingRecorder::Sent& sent : rig.rec.sent) REQUIRE(sent.channel == c.created - 1);
		}
	}
}


// ── Timing-mode presets, driven with real input frames ──────────────────────
// The preset tests in MidiKit.test.examples.hpp feed messages straight into the
// engine, with currentInFrame = -1, so the branch of the timing-mode presets
// that places a message on the frame of the event that caused it never runs
// there. In a real patch it always does. These run the shipped scripts through
// the module's real input stage and check the frames that reach the output.

static std::string presetSource(const std::string& relPath) {
	static const std::string suffix = "src/modules/midikit/";
	std::string f = __FILE__;
	size_t at = f.rfind(suffix);
	std::string root = at == std::string::npos ? "" : f.substr(0, at);
	while (root.size() > 1 && root.back() == '/') root.pop_back();
	if (root.empty()) root = ".";
	std::ifstream in(root + "/presets/MidiKit/" + relPath);
	CATCH_INFO("cannot open preset " << relPath);
	REQUIRE(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// Steps `edges` clock edges, `period` frames apart, on trigger input 1 (channel 1).
// Returns the frames the edges were applied on.
static std::vector<int64_t> clockEdges(TimingRig& rig, int edges, int64_t period) {
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	std::vector<int64_t> frames;
	for (int i = 0; i < edges; i++) {
		int64_t at = rig.frame;
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		rig.step();
		rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		frames.push_back(at);
		rig.run(at + period);
	}
	return frames;
}

TEST_CASE("Timing-mode preset 'Bouncing ball delay': echoes are placed from the note's frame", "[MidiKit][timing][preset]") {
	for (const char* path : { "JavaScript/creative/Bouncing ball delay.js", "Lua/creative/Bouncing ball delay.lua" }) {
		CATCH_INFO(path);
		std::string source = presetSource(path);
		TimingRig rig(source.c_str());

		// The note arrives on frame 100 but is dispatched on the next divider tick, so a
		// preset that placed its echoes from the process() frame would drift by up to 7.
		rig.run(100);
		rig.inject(noteOn(0, 60, 100), 100);
		rig.run(100 + 3 * int64_t(Test::sampleRate()));

		REQUIRE_FALSE(rig.rec.sent.empty());
		requireOrderedFrames(rig.rec);
		// The dry note first, on the frame it arrived on.
		REQUIRE(rig.rec.sent[0].status == 0x9);
		REQUIRE(rig.rec.sent[0].frameField == 100);
		// The first echo: initialInterval (250 ms) after the note's own frame.
		int64_t expected = 100 + int64_t(std::llround(0.250 * Test::sampleRate()));
		REQUIRE(rig.rec.sent.size() > 2);
		REQUIRE(std::llabs(rig.rec.sent[1].frameField - expected) <= 1);
		// The echoes continue and settle: every later echo is a Note-On with lower velocity.
		int lastVelocity = rig.rec.sent[0].value;
		for (const TimingRecorder::Sent& e : rig.rec.sent) {
			if (e.status != 0x9) continue;
			REQUIRE(e.value <= lastVelocity);
			lastVelocity = e.value;
		}
	}
}

TEST_CASE("Timing-mode preset 'Clock multiplier': one pulse on each edge, the rest spread evenly", "[MidiKit][timing][preset]") {
	for (const char* path : { "JavaScript/Clock multiplier.js", "Lua/Clock multiplier.lua" }) {
		CATCH_INFO(path);
		std::string source = presetSource(path);
		TimingRig rig(source.c_str());

		rig.run(100);
		std::vector<int64_t> edges = clockEdges(rig, 3, 240);
		rig.run(edges.back() + 240);

		REQUIRE(edges == std::vector<int64_t>({ 100, 340, 580 }));
		requireOrderedFrames(rig.rec);
		std::vector<int64_t> frames;
		for (const TimingRecorder::Sent& e : rig.rec.sent) {
			REQUIRE(e.status == 0xf);
			frames.push_back(e.frameField);
		}
		// Edge 1 has no period yet: its own pulse only. Edge 2 sends its pulse and
		// 23 more, 240 / 24 = 10 frames apart. Edge 3 sends its own pulse on its
		// frame, and 23 more.
		std::vector<int64_t> expected = { 100 };
		for (int k = 0; k < 24; k++) expected.push_back(340 + 10 * k);
		for (int k = 0; k < 24; k++) expected.push_back(580 + 10 * k);
		REQUIRE(frames == expected);
	}
}


TEST_CASE("Round trip: a script receives the releases another script creates", "[MidiKit][timing][Release]") {
	// setNoteOff() sends 0x80, setNoteOn(.., 0) a velocity-0 Note-On. Fed back into the
	// input of a second module, both are releases, and only the first is a Note-Off.
	for (bool sendLua : { false, true }) {
		for (bool recvLua : { false, true }) {
			CATCH_INFO(std::string("send ") + (sendLua ? "lua" : "js") + ", receive " + (recvLua ? "lua" : "js"));
			std::string send = sendLua
				? "--[[\n@engine minilua@v1\n--]]\nmidi.onMessage = function(port, msg)\n"
				  "local a = midi.create(); midi.setNoteOff(a, 1, 60, 64); midiOut.send(a)\n"
				  "local b = midi.create(); midi.setNoteOn(b, 1, 60, 0); midiOut.send(b)\n"
				  "local c = midi.create(); midi.setNoteOn(c, 1, 60, 100); midiOut.send(c)\nend\n"
				: "/**\n * @engine QuickJs@v1\n */\nmidi.onMessage = function(port, msg) {\n"
				  "let a = midi.create(); midi.setNoteOff(a, 1, 60, 64); midiOut.send(a);\n"
				  "let b = midi.create(); midi.setNoteOn(b, 1, 60, 0); midiOut.send(b);\n"
				  "let c = midi.create(); midi.setNoteOn(c, 1, 60, 100); midiOut.send(c);\n};\n";
			std::string recv = recvLua
				? "--[[\n@engine minilua@v1\n--]]\nmidi.onMessage = function(port, msg)\n"
				  "local function b(v) if v then return '1' else return '0' end end\n"
				  "rack.log('P:' .. b(midi.isNoteOn(msg)) .. b(midi.isNoteOff(msg)) .. b(midi.isNoteRelease(msg)))\nend\n"
				: "/**\n * @engine QuickJs@v1\n */\nmidi.onMessage = function(port, msg) {\n"
				  "rack.log('P:' + (midi.isNoteOn(msg) ? '1' : '0') + (midi.isNoteOff(msg) ? '1' : '0') + (midi.isNoteRelease(msg) ? '1' : '0'));\n};\n";

			TimingRig sender(send.c_str());
			sender.inject(noteOn(0, 60, 100), 8);
			sender.run(100);
			REQUIRE(sender.rec.sent.size() == 3);

			TimingRig receiver(recv.c_str());
			for (const TimingRecorder::Sent& e : sender.rec.sent) {
				receiver.inject(Test::makeMidiMessage(e.status, e.channel, e.note, e.value), 8);
			}
			receiver.run(100);

			std::vector<std::string> probes;
			std::string log = drainLog(receiver.m);
			size_t pos = 0;
			while (pos < log.size()) {
				size_t nl = log.find('\n', pos);
				if (nl == std::string::npos) break;
				if (log.compare(pos, 2, "P:") == 0) probes.push_back(log.substr(pos + 2, nl - pos - 2));
				pos = nl + 1;
			}
			// isNoteOn, isNoteOff, isNoteRelease: the Note-Off, the velocity-0 Note-On, a real Note-On.
			REQUIRE(probes == std::vector<std::string>({ "011", "101", "100" }));
		}
	}
}


TEST_CASE("Received group: a running parameter is forwarded with the select the device never sent", "[MidiKit][cc][timing]") {
	// The device selects NRPN 517 once, then sends data entry only (running parameter).
	// Every received change is a complete group, so each forward carries 99 and 98.
	for (bool lua : { false, true }) {
		for (bool rpn : { false, true }) {
			CATCH_INFO(std::string(lua ? "lua " : "js ") + (rpn ? "rpn" : "nrpn"));
			GroupInput g = rpn ? GroupInput{ "enableRpnIn", "onRpn", {}, {} } : GroupInput{ "enableNrpnIn", "onNrpn", {}, {} };
			TimingRig rig(groupScript(lua, g, lua ? "    midiOut.send(msg)" : "    midiOut.send(msg);").c_str());
			int sel = rpn ? 101 : 99;
			auto cc = [&](int num, int value) { rig.inject(Test::makeMidiMessage(0xb, 0, num, value), 8); };
			cc(sel, 4); cc(sel - 1, 5); cc(6, 20); cc(38, 2);   // a full write
			cc(6, 21); cc(38, 3);                              // only the data entry
			rig.run(100);

			REQUIRE(sentNotes(rig.rec) == std::vector<int>({ sel, sel - 1, 6, 38, sel, sel - 1, 6, 38 }));
			REQUIRE(sentValues(rig.rec) == std::vector<int>({ 4, 5, 20, 2, 4, 5, 21, 3 }));
		}
	}
}
