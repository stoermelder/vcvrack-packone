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

TEST_CASE("Timing: a pass-through reply carries no frame", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
	TimingRig rig(script);

	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	REQUIRE(rig.rec.sent.size() == 1);
	// status quo: send() clears the frame, so Rack's
	// output thread is never asked to schedule anything.
	REQUIRE(rig.rec.sent[0].frame == -1);
	REQUIRE(rig.rec.sent[0].note == 60);
}

TEST_CASE("Timing: a reply leaves on the first divider tick after the input frame", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
	TimingRig rig(script);

	// Input due at 20 → popped on the divider tick at 23.
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	REQUIRE(rig.rec.sent.size() == 1);
	// status quo: up to DIVIDER-1 samples of avoidable lag.
	REQUIRE(rig.rec.sent[0].releasedAt == nextDividerTick(20));
	REQUIRE(rig.rec.sent[0].releasedAt == 23);
}

TEST_CASE("Timing: a message due exactly on a divider tick is not delayed", "[MidiKit][Timing]") {
	TimingRig rig(JS_PASS_THROUGH);

	rig.inject(noteOn(0, 60, 100), 23);
	rig.run(40);

	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].releasedAt == 23);
}

TEST_CASE("Timing: a message is never released before its input frame", "[MidiKit][Timing]") {
	TimingRig rig(JS_PASS_THROUGH);

	rig.inject(noteOn(0, 60, 100), 100);
	rig.run(99);
	REQUIRE(rig.rec.sent.empty());

	rig.run(120);
	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].releasedAt >= 100);
}

TEST_CASE("Timing: sendAfterMs holds the message and strips its frame", "[MidiKit][Timing]") {
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
	REQUIRE(rig.rec.sent[0].frame == -1);
}

TEST_CASE("Timing: sendAfterTrigger releases on the exact edge sample", "[MidiKit][Timing]") {
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
	REQUIRE(rig.rec.sent[0].frame == -1);
}

TEST_CASE("Timing: release jitter over a metronomic stream is the divider quantisation", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
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
		REQUIRE(rig.rec.sent[i].frame == -1);
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

TEST_CASE("Timing: messages sharing an input frame leave in arrival order", "[MidiKit][Timing]") {
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

TEST_CASE("Timing: a group sent with sendAfterMs is held and released whole, in order", "[MidiKit][Timing]") {
	const int64_t delay = int64_t(0.010 * Test::sampleRate());

	Pair scripts{JS_GROUP_AFTER_MS, LUA_GROUP_AFTER_MS};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
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

TEST_CASE("Timing: a group sent with sendAfterTrigger is released in order", "[MidiKit][Timing]") {
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

TEST_CASE("Timing mode: the opt-in is per script and forgotten on reload", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
	TimingRig rig(withTiming(script).c_str());
	REQUIRE(rig.m->midiOuts.isTimingEnabled());

	// A script without the call is back in legacy mode.
	rig.m->loadScript(script);
	REQUIRE_FALSE(rig.m->midiOuts.isTimingEnabled());

	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);
	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].frame == -1);
}

TEST_CASE("Timing mode: a reply carries the frame of the message it answers", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
	TimingRig rig(withTiming(script).c_str());

	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	REQUIRE(rig.rec.sent.size() == 1);
	// The arrival frame, not the later frame the script ran on.
	REQUIRE(rig.rec.sent[0].frame == 20);
	REQUIRE(rig.rec.sent[0].releasedAt == 23);
	REQUIRE(rig.rec.sent[0].note == 60);
}

TEST_CASE("Timing mode: sendAfterMs hands its scheduled frame to the device", "[MidiKit][Timing]") {
	TimingRig rig(withTiming(JS_AFTER_MS).c_str());
	const int64_t delay = int64_t(0.010 * Test::sampleRate());

	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(20 + delay + 40);

	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].frame > 0);
	REQUIRE(rig.rec.sent[0].frame <= rig.rec.sent[0].releasedAt);
}

TEST_CASE("Timing mode: messages sharing a frame get strictly increasing frames in send order", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
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
	for (int i = 0; i < 4; i++) REQUIRE(rig.rec.sent[i].frame == 20 + i);
}

TEST_CASE("Timing mode: a group keeps its order and gets one frame per message", "[MidiKit][Timing]") {
	const int64_t delay = int64_t(0.010 * Test::sampleRate());

	Pair scripts{JS_GROUP_AFTER_MS, LUA_GROUP_AFTER_MS};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(withTiming(script).c_str());
	rig.inject(noteOn(0, 1, 100), 20);
	rig.run(20 + delay + 40);

	REQUIRE(controllers(rig.rec) == std::vector<int>{99, 98, 6, 38});
	requireOrderedFrames(rig.rec);
}

TEST_CASE("Timing mode: scheduled and immediate messages never go out of order", "[MidiKit][Timing]") {
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

TEST_CASE("Timing mode: a sendAfterTrigger message is stamped with the edge's frame", "[MidiKit][Timing]") {
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
	REQUIRE(rig.rec.sent[0].frame == 45);
	REQUIRE(rig.rec.sent[0].releasedAt == 45);
}

TEST_CASE("Teardown flush is sent immediately in both modes", "[MidiKit][Timing]") {
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

		// As the destructor does.
		rig.m->host.unload();
		rig.m->flushMidiOut();

		REQUIRE(rig.rec.sent.size() == 1);
		REQUIRE(rig.rec.sent[0].status == 0x8);
		// Frame-less even in timing mode: a framed message would sit in Rack's output
		// queue, which goes away with the device when this module releases it.
		REQUIRE(rig.rec.sent[0].frame == -1);
	}
}

// Frames carried to the script
// Every dispatch runs with currentInFrame set to the frame of the event that
// caused it. Nothing reads it yet; these pin the plumbing.

using StoermelderPackOne::MidiScript::QueuedMessage;
using StoermelderPackOne::MidiScript::TipsyMessage;

// Dispatches through the engine's real queues and records the frame each
// callback runs under.
struct FrameProbeEngine : StubEngine {
	struct Seen {
		const char* kind;
		int64_t frame;
	};
	std::vector<Seen> seen;

	explicit FrameProbeEngine(MidiKitModule* module) : StubEngine(module) {}

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
};

struct ProbeRig : Kit<> {
	AttachedEngine<FrameProbeEngine> attached;
	FrameProbeEngine& eng;

	ProbeRig() : attached(m), eng(attached.eng) {}

	void run(int64_t untilFrame) { runUntil(untilFrame); }
};

TEST_CASE("Frames: a message dispatches under its arrival frame", "[MidiKit][Timing]") {
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

TEST_CASE("Frames: an assembled NRPN carries its last component's frame", "[MidiKit][Timing]") {
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

TEST_CASE("Frames: a trigger dispatches under the frame of its edge", "[MidiKit][Timing]") {
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

TEST_CASE("Frames: a Tipsy message dispatches under the frame it completed on", "[MidiKit][Timing]") {
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

TEST_CASE("Frames: sendAfterMs counts from the frame the script was dispatched on", "[MidiKit][Timing]") {
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

TEST_CASE("sendAtFrame holds a message until its frame; legacy strips it, timing keeps it", "[MidiKit][Timing]") {
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
			REQUIRE(rig.rec.sent[0].frame == -1);
		}

		SECTION("timing") {
			TimingRig rig(withTiming(sc.first).c_str());
			rig.inject(noteOn(0, 60, 100), 20);
			rig.run(220);

			REQUIRE(rig.rec.sent.size() == 1);
			REQUIRE(rig.rec.sent[0].releasedAt >= 200);
			REQUIRE(rig.rec.sent[0].frame == 200);
		}
	}
}

TEST_CASE("sendAtFrame treats a negative frame as 'now'", "[MidiKit][Timing]") {
	Pair scripts{JS_AT_NEGATIVE, LUA_AT_NEGATIVE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(script);
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	// Sent at once, not parked in the frame queue ahead of everything else.
	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.empty());
	REQUIRE(rig.rec.sent[0].frame == -1);
}

TEST_CASE("sendAtFrame with a negative frame keeps send order", "[MidiKit][Timing]") {
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(script);
	rig.inject(noteOn(0, 1, 100), 20);
	rig.run(40);
	REQUIRE(controllers(rig.rec) == std::vector<int>{60, 61});
}

TEST_CASE("rack.getEventFrame() is the frame of the event being handled", "[MidiKit][Timing]") {
	Pair scripts{JS_EVENT_FRAME, LUA_EVENT_FRAME};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
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

TEST_CASE("rack.getEventFrame() is -1 outside an event", "[MidiKit][Timing]") {
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(script);
	rig.run(20);
	// -1 + 500
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 499);
}

TEST_CASE("Timing mode: sendAfterMs counts from the causing event", "[MidiKit][Timing]") {
	TimingRig rig(withTiming(JS_AFTER_MS).c_str());
	int64_t delay = int64_t(10.0 / 1000.0 * rig.m->sampleRate.load());

	// Input at 20 is dispatched on the tick at 23; the delay counts from 20.
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 20 + delay);
}

TEST_CASE("Timing mode: sendAfterMs with no event counts from the published frame", "[MidiKit][Timing]") {
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
	EngineScope engine;
	engine.mock.frame = 1000;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	int64_t delay = int64_t(10.0 / 1000.0 * m->sampleRate.load());

	m->process(Test::makeProcessArgs(1000));
	for (int64_t f = 1001; f < 1010; f++) m->process(Test::makeProcessArgs(f));

	REQUIRE(m->midiOuts.ports[0].frameQueue.size() == 1);
	REQUIRE(m->midiOuts.ports[0].frameQueue.top().msg.frame == 1000 + delay);
}

TEST_CASE("Timing mode: send() takes the frame of the event it runs in", "[MidiKit][Timing]") {
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(script);
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	rig.run(30);
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	rig.step();   // the edge is on frame 30
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	rig.run(50);

	REQUIRE(rig.rec.sent.size() == 1);
	REQUIRE(rig.rec.sent[0].frame == 30);
}

TEST_CASE("Timing mode: send() outside an event goes out stamped with the current frame", "[MidiKit][Timing]") {
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
	REQUIRE(rig.rec.sent[0].frame == rig.rec.sent[0].releasedAt);
}

// Tick-scheduled messages

TEST_CASE("Timing mode: a tick-scheduled group leaves in order from the edge's frame", "[MidiKit][Timing]") {
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
	for (int i = 0; i < 4; i++) REQUIRE(rig.rec.sent[i].frame == 45 + i);
}

// On a sample where a trigger edge and a divider tick coincide, the tick queue
// drains before the script's out-queue, so a tick-scheduled message reaches the
// device ahead of an immediate one produced on that same sample. The two are
// unrelated, so this is fine; it is pinned so that a change to process() cannot
// swap it unnoticed.
TEST_CASE("Tick-scheduled messages are released before the immediate messages of the same sample", "[MidiKit][Timing]") {
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
// and size are zero headless, so an EngineScope supplies them.

TEST_CASE("Timing report: a message handed over a block past its frame is logged", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
	// The block being processed started at 1000; the reply is for frame 20.
	EngineScope blocks;
	blocks.mock.blockFrame = 1000;
	blocks.mock.blockFrames = 256;
	TimingRig rig(withTiming(script, "true").c_str());
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	std::string log = drainLog(rig.m);
	REQUIRE(countOf(log, "reached the output too late") == 1);
}

TEST_CASE("Timing report: a message with time left is not logged", "[MidiKit][Timing]") {
	EngineScope blocks;
	blocks.mock.blockFrame = 100;
	blocks.mock.blockFrames = 256;
	TimingRig rig(withTiming(JS_PASS_THROUGH, "true").c_str());
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	REQUIRE(countOf(drainLog(rig.m), "reached the output too late") == 0);
}

TEST_CASE("Timing report: nothing is logged unless the script asked for it", "[MidiKit][Timing]") {
	EngineScope blocks;
	blocks.mock.blockFrame = 1000;
	blocks.mock.blockFrames = 256;

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

TEST_CASE("Timing report: a script that keeps falling behind is logged once per second", "[MidiKit][Timing]") {
	EngineScope blocks;
	blocks.mock.blockFrame = 1000000;
	blocks.mock.blockFrames = 256;
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

TEST_CASE("Timing mode: a frame-less message does not overtake earlier messages of the same callback", "[MidiKit][Timing]") {
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(script);
	rig.inject(noteOn(0, 1, 100), 20);
	rig.run(40);

	REQUIRE(controllers(rig.rec) == std::vector<int>{60, 61});
	requireOrderedFrames(rig.rec);
}

TEST_CASE("sendAfterMs from rack.onLoad counts from the engine frame when no process() has run yet", "[MidiKit][Timing]") {
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	for (bool timing : { false, true }) {
		CATCH_INFO(script);
		CATCH_INFO(timing);
		EngineScope engine;
		engine.mock.frame = 1000000;
		TimingRig rig(timing ? withTiming(script).c_str() : script);
		int64_t delay = int64_t(10.0 / 1000.0 * rig.m->sampleRate.load());

		rig.frame = 1000000;
		rig.run(1000010);

		REQUIRE(rig.rec.sent.empty());
		REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
		REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 1000000 + delay);
	}
}

TEST_CASE("sendAfterMs keeps full precision in both engines", "[MidiKit][Timing]") {
	// 7199999 ms is the last whole millisecond under the 2 hour limit; both
	// engines must land on the same frame, exactly.
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, 7199999);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
midiOut.enableTiming()
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(msg, 7199999)
end
)";
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(script);
	double sr = rig.m->sampleRate.load();
	rig.inject(noteOn(0, 60, 100), 20);
	rig.run(40);

	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.size() == 1);
	REQUIRE(rig.m->midiOuts.ports[0].frameQueue.top().msg.frame == 20 + int64_t(7199999.0 / 1000.0 * sr));
}

TEST_CASE("Each send call on a handle schedules its own copy, in both engines", "[MidiKit][Timing]") {
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
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

TEST_CASE("sendAfterMs with -1 waits two blocks and a frame after the base frame", "[MidiKit][Timing]") {
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	for (int64_t block : { 64, 256, 2048 }) {
		for (bool timing : { false, true }) {
			CATCH_INFO(script);
			CATCH_INFO(block);
			CATCH_INFO(timing);
			EngineScope blocks;
			blocks.mock.blockFrame = 0;
			blocks.mock.blockFrames = block;
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

TEST_CASE("Timing: a trigger written in an event is applied on its stamped frame", "[MidiKit][Timing]") {
	// Headless the block size is 0, which would make a stamp indistinguishable
	// from the divider tick the script runs on; a 64-frame block separates them.
	EngineScope blocks;
	blocks.mock.blockFrame = 0;
	blocks.mock.blockFrames = 64;
	Pair scripts{JS_TRIG_STAMP, LUA_TRIG_STAMP};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	TimingRig rig(script);

	// Input due at 20 → the script runs on the divider tick at 23, but the
	// write is stamped 20 + one block, whatever frame the worker was at.
	rig.inject(noteOn(0, 60, 100), 20);
	REQUIRE(firstHighFrame(rig, 0, 2000) == 20 + 64);
}

TEST_CASE("Timing: without enableTiming a trigger is written when the script runs", "[MidiKit][Timing]") {
	TimingRig rig(JS_TRIG_NO_TIMING);

	rig.inject(noteOn(0, 60, 100), 20);
	// status quo for unstamped writes: the divider tick that dispatched it.
	REQUIRE(firstHighFrame(rig, 0, 200) == nextDividerTick(20));
}

TEST_CASE("Timing: stamped trigger writes are applied in frame order", "[MidiKit][Timing]") {
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

TEST_CASE("Timing: stamped trigger writes for the same frame keep the order written", "[MidiKit][Timing]") {
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

TEST_CASE("Timing: a script reload drops stamped trigger writes that are still pending", "[MidiKit][Timing]") {
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


TEST_CASE("Round trip: a script receives the releases another script creates", "[MidiKit][Timing][Release]") {
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
			for (const Out& e : sender.rec.sent) {
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
