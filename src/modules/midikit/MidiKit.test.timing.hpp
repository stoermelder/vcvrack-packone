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
		m->midiOutput.outputDevice = &rec;
		m->midiOutput.channel = -1;
		m->loadScript(script);
	}

	~TimingRig() {
		// The recorder is a member and dies before `mods` destroys the module;
		// onRemove() flushes output through the device.
		m->midiOutput.outputDevice = nullptr;
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
		m->midiInput.onMessage(msg);
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
	REQUIRE(rig.m->midiOutput.frameQueue.size() == 1);

	rig.run(20 + delay + 40);
	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.m->midiOutput.frameQueue.empty());
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
	// status quo: sendAfterTrigger() never touches the
	// frame, so the message reaches the device carrying the stale arrival frame
	// of the input it was copied from (8) — not the edge's frame, and not -1.
	// Rack would treat that as long past and send immediately.
	REQUIRE(rig.rec.sent[0].frameField == 8);
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
			REQUIRE(rig.m->midiOutput.frameQueue.size() == 4);

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
			REQUIRE(rig.m->midiOutput.frameQueue.size() == 2);

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
static std::string withTiming(const char* script) {
	std::string s = script;
	size_t at = s.find("*/\n");
	if (at != std::string::npos) at += 3;
	else {
		at = s.find("--]]\n");
		REQUIRE(at != std::string::npos);
		at += 5;
	}
	bool lua = s.find("minilua") != std::string::npos;
	return s.substr(0, at) + (lua ? "midiOut.enableTiming()\n" : "midiOut.enableTiming();\n") + s.substr(at);
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
		REQUIRE(rig.m->timingEnabled.load());

		// A script without the call is back in legacy mode.
		rig.m->loadScript(script);
		REQUIRE_FALSE(rig.m->timingEnabled.load());

		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);
		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(rig.rec.sent[0].frameField == -1);
	}
}

TEST_CASE("Timing mode: a reply carries a frame instead of -1", "[MidiKit][timing]") {
	for (const char* script : TIMING_SCRIPTS) {
		CATCH_INFO(script);
		TimingRig rig(withTiming(script).c_str());

		rig.inject(noteOn(0, 60, 100), 20);
		rig.run(40);

		REQUIRE(rig.rec.sent.size() == 1);
		// Stamped with the hand-over frame, so Rack places it.
		REQUIRE(rig.rec.sent[0].frameField == rig.rec.sent[0].releasedAt);
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

TEST_CASE("Teardown flush is frame-less in legacy mode and stamped in timing mode", "[MidiKit][timing]") {
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
		rig.m->host.closeState();
		rig.m->flushOutput();

		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(rig.rec.sent[0].status == 0x8);
		if (timing) REQUIRE(rig.rec.sent[0].frameField >= 0);
		else REQUIRE(rig.rec.sent[0].frameField == -1);
	}
}
