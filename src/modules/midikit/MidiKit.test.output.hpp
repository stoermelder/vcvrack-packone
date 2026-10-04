TEST_CASE("processTick sends a message on its exact tick", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();

	out.send(msg, 0, 5);
	REQUIRE(out.tickQueue[0].size() == 1);

	out.processTick(0, 4);
	REQUIRE(out.tickQueue[0].size() == 1);  // not due yet

	out.processTick(0, 5);
	REQUIRE(out.tickQueue[0].size() == 0);
}

TEST_CASE("processTick sends a message whose tick has already passed", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();

	// process() calls processTick() before draining the engine out-queue, so a
	// script can schedule for a tick the counter has already consumed.
	out.send(msg, 0, 5);
	REQUIRE(out.tickQueue[0].size() == 1);

	out.processTick(0, 6);
	REQUIRE(out.tickQueue[0].size() == 0);  // with "==" this stayed queued forever
}

TEST_CASE("processTick drains every due message in one call", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();

	out.send(msg, 0, 3);
	out.send(msg, 0, 5);
	out.send(msg, 0, 7);
	REQUIRE(out.tickQueue[0].size() == 3);

	out.processTick(0, 5);
	REQUIRE(out.tickQueue[0].size() == 1);        // 3 and 5 sent, 7 still pending
	REQUIRE(out.tickQueue[0].top().tick == 7);

	out.processTick(0, 7);
	REQUIRE(out.tickQueue[0].size() == 0);
}

TEST_CASE("processTick: a stale entry does not block later messages", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();

	// tickQueue is ordered smallest-tick-first, so the stale entry sits at the
	// head. With "==" it was never popped and blocked everything behind it.
	out.send(msg, 0, 2);   // stale — this tick is already in the past
	out.send(msg, 0, 7);   // legitimately scheduled for later
	REQUIRE(out.tickQueue[0].size() == 2);

	out.processTick(0, 7);
	REQUIRE(out.tickQueue[0].size() == 0);  // both drained, not stuck at the head
}

TEST_CASE("processTick leaves not-yet-due messages queued", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();

	out.send(msg, 0, 10);

	for (uint64_t t = 0; t < 10; t++) {
		out.processTick(0, t);
		REQUIRE(out.tickQueue[0].size() == 1);
	}

	out.processTick(0, 10);
	REQUIRE(out.tickQueue[0].size() == 0);
}

TEST_CASE("processFrame sends a message on its exact frame", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();
	msg.frame = 5;

	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 1);

	out.processFrame(4);
	REQUIRE(out.frameQueue.size() == 1);  // not due yet

	out.processFrame(5);
	REQUIRE(out.frameQueue.size() == 0);  // with ">" this stayed queued one call longer
}

TEST_CASE("processFrame sends a message whose frame has already passed", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();
	msg.frame = 5;

	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 1);

	out.processFrame(6);
	REQUIRE(out.frameQueue.size() == 0);
}

TEST_CASE("processFrame drains every due message in one call", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();

	msg.frame = 3;
	out.send(msg, 0, 0);
	msg.frame = 5;
	out.send(msg, 0, 0);
	msg.frame = 7;
	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 3);

	out.processFrame(5);
	REQUIRE(out.frameQueue.size() == 1);         // 3 and 5 sent, 7 still pending
	REQUIRE(out.frameQueue.top().msg.frame == 7);

	out.processFrame(7);
	REQUIRE(out.frameQueue.size() == 0);
}

TEST_CASE("processFrame leaves not-yet-due messages queued", "[MidiKit]") {
	MidiOutput<1> out;
	midi::Message msg = makeCc();
	msg.frame = 10;

	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 1);

	for (int64_t f = 0; f < 10; f++) {
		out.processFrame(f);
		REQUIRE(out.frameQueue.size() == 1);
	}

	out.processFrame(10);
	REQUIRE(out.frameQueue.size() == 0);
}

static void patchTrigger(MidiKitModule* m) {
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
}

TEST_CASE("process() runs the engine only on divider ticks", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	AttachedEngine<RecordingEngine> attached(m);
	RecordingEngine& eng = attached.eng;

	// processDivider is set to a division of 8. dsp::ClockDivider increments
	// before comparing, so it fires on every 8th call — call indices 7, 15, 23
	// — not on the first one.
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.processCalls == 0);

	step(m, 0.f, 7);
	REQUIRE(eng.processCalls == 1);

	for (int64_t f = 8; f < 24; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.processCalls == 3);
}

TEST_CASE("process() drains the engine out-queue on a divider tick", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	AttachedEngine<RecordingEngine> attached(m);
	RecordingEngine& eng = attached.eng;

	// Three messages scheduled for tick 1; all must be pulled in a single
	// divider tick, not one per call.
	eng.pending = {1, 1, 1};

	// Nothing is drained until the divider actually fires on the 8th call.
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.pending.size() == 3);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);

	step(m, 0.f, 7);

	REQUIRE(eng.pending.empty());
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 3);
}

TEST_CASE("process() consumes the tick before the engine schedules on it", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	AttachedEngine<RecordingEngine> attached(m);
	RecordingEngine& eng = attached.eng;
	patchTrigger(m);
	// The module only processes triggers on enabled channels — as the script's
	// trig.enableIn(1) would do.
	m->enableTrigger(0, 0);

	// Prime the SchmittTrigger LOW and advance to one call short of a divider
	// tick, without letting the engine emit anything.
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.processCalls == 0);

	// A trigger and a divider tick coincide on this sample: processTick() runs
	// first and consumes tick 1, then the engine emits a message scheduled for
	// tick 1 — a tick already gone. This is the ordering that makes a stale
	// entry reachable at all, and it is why processTick() must use ">=".
	eng.pending = {1};
	step(m, 10.f, 7);
	REQUIRE(eng.processCalls == 1);

	REQUIRE(m->triggerIns.triggerTick[0][0] == 1);
	REQUIRE(eng.tickAtEmit.size() == 1);
	REQUIRE(eng.tickAtEmit[0] == 1);       // emitted after the tick was consumed
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].top().tick == 1);

	// The next trigger drains it rather than stranding it behind the counter.
	// The entry sits at tick 1 while the counter moves to 2, so only ">=" can
	// pop it — "==" strands it here permanently.
	step(m, 0.f, 8);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);   // falling edge: no tick
	step(m, 10.f, 9);

	REQUIRE(m->triggerIns.triggerTick[0][0] == 2);
	REQUIRE(eng.tickAtEmit.size() == 1);           // engine emitted only once
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
}

TEST_CASE("process() handles triggers arriving between divider ticks", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	AttachedEngine<RecordingEngine> attached(m);
	RecordingEngine& eng = attached.eng;
	patchTrigger(m);
	// The module only processes triggers on enabled channels — as the script's
	// trig.enableIn(1) would do.
	m->enableTrigger(0, 0);

	// Schedule for two ticks ahead on the first divider tick (call index 7).
	eng.pending = {2};
	for (int64_t f = 0; f < 8; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);

	// Triggers are handled every sample, independent of the divider. These land
	// between divider boundaries and must not send the tick-2 message early.
	step(m, 10.f, 8);
	REQUIRE(m->triggerIns.triggerTick[0][0] == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);

	step(m, 0.f, 9);
	step(m, 10.f, 10);
	REQUIRE(m->triggerIns.triggerTick[0][0] == 2);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
}

TEST_CASE("process() sends frame-scheduled messages on divider ticks only", "[MidiKit]") {
	// The synchronous worker: the test depends on the load having landed.
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard.
	m->loadScript(QUICKJS_SCRIPT);
	// The audio thread's half of the load, which would clear the message below;
	// without a process() call, so the divider phase stays where it is.
	m->syncScriptGen();

	// ticks == 0 with a set frame routes to frameQueue rather than tickQueue.
	midi::Message msg = makeCc();
	msg.frame = 9;
	m->midiOuts.ports[0].send(msg, 0, 0);
	REQUIRE(m->midiOuts.ports[0].frameQueue.size() == 1);

	// Divider ticks land on call indices 7 and 15, and processFrame() is only
	// reached inside that branch. The frame-9 message is therefore still queued
	// at frame 14, four samples after it came due — this is the one-divider-
	// period latency.
	for (int64_t f = 0; f <= 14; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(m->midiOuts.ports[0].frameQueue.size() == 1);

	// The next divider tick drains it.
	step(m, 0.f, 15);
	REQUIRE(m->midiOuts.ports[0].frameQueue.size() == 0);

}

TEST_CASE("process() orders trigger, inbound, and outbound effects in one call", "[MidiKit]") {
	Kit<> kit;
	// One divider-tick process() call that has a trigger
	// edge, a pending inbound message, and a queued outbound message, asserting
	// the observable order of effects. This is the ordering the extractions
	// most risk breaking, and nothing else pins it down:
	//   1. the trigger edge is consumed first (tick queued to the engine);
	//   2. the inbound message is decoded and queued to the engine;
	//   3. the engine runs, so it can see the just-queued inbound and emit.
	MidiKitModule* m = kit.m;
	AttachedEngine<RecordingEngine> attached(m);
	RecordingEngine& eng = attached.eng;
	patchTrigger(m);
	m->enableTrigger(0, 0);

	// Prime the SchmittTrigger LOW and advance to one call short of a divider
	// tick (the divider fires on call index 7).
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.events.empty());

	// One call with all three at once: a rising edge on channel 0, an inbound
	// CC queued for this sample (frame -1 processes immediately), and an
	// outbound message the engine emits during its pump, scheduled for the tick
	// just consumed.
	midi::Message in = makeCc();
	m->midiIns.ports[0].processor.getInput().onMessage(in);
	eng.pending = {1};
	step(m, 10.f, 7);

	// The engine was pumped exactly once, on this divider tick.
	REQUIRE(eng.processCalls == 1);

	// The observable order of effects within that single call:
	// trigger edge → inbound decode → engine pump.
	REQUIRE(eng.events.size() == 3);
	REQUIRE(eng.events[0] == RecordingEngine::TICK);
	REQUIRE(eng.events[1] == RecordingEngine::MESSAGE);
	REQUIRE(eng.events[2] == RecordingEngine::PROCESS);

	// The side effects that order produces: the edge was consumed, the inbound
	// reached the engine, and the engine's outbound landed after the tick was
	// consumed (so it stays queued until the next trigger).
	REQUIRE(m->triggerIns.triggerTick[0][0] == 1);
	REQUIRE(eng.received.size() == 1);
	REQUIRE(eng.received[0].type == StoermelderPackOne::MessageEx::Type::CC);
	REQUIRE(eng.tickAtEmit.size() == 1);
	REQUIRE(eng.tickAtEmit[0] == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].top().tick == 1);
}

TEST_CASE("Trigger input drains tick-scheduled messages via process()", "[MidiKit]") {
	// The synchronous worker: the test depends on the load having landed.
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard. The trigger is enabled directly here
	// (as the script's trig.enableIn(1) would do) so the module drains ticks.
	m->loadScript(QUICKJS_SCRIPT);
	m->enableTrigger(0, 0);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));

	// Schedule for tick 2 but also queue a stale tick-1 entry ahead of it, as
	// happens when a script schedules for a tick the counter already consumed.
	// The stale entry sorts to the head, so with "==" it blocks both forever.
	midi::Message msg = makeCc();
	m->midiOuts.ports[0].send(msg, 0, 2);
	m->midiOuts.ports[0].tickQueue[0].push(std::remove_reference<decltype(m->midiOuts.ports[0])>::type::TickSchedule{msg, 0, 0, OutGroup()});
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 2);

	int64_t frame = 1;
	for (int pulse = 0; pulse < 3; pulse++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		m->process(Test::makeProcessArgs(frame++));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		m->process(Test::makeProcessArgs(frame++));
	}

	REQUIRE(m->triggerIns.triggerTick[0][0] == 3);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
}

TEST_CASE("sendAfterTrigger on one channel is only drained by that channel's clock", "[MidiKit]") {
	// The synchronous worker: the test depends on the load having landed.
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard. Enable both trigger channels (as the
	// script's trig.enableIn(1, 1)/trig.enableIn(1, 2) would do) so the module
	// processes ticks on each.
	m->loadScript(QUICKJS_SCRIPT);
	m->enableTrigger(0, 0);
	m->enableTrigger(0, 1);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;

	// Prime both SchmittTriggers LOW. This first process() also carries out the
	// script load's request to drop older scheduled messages, so schedule after.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
	m->process(Test::makeProcessArgs(0));

	// Schedule a message against channel 2's clock at tick 2.
	midi::Message msg = makeCc();
	m->midiOuts.ports[0].send(msg, 1, 2);   // channel index 1 = script channel 2

	// Two pulses on channel 1 must NOT drain channel 2's queue.
	for (int pulse = 0; pulse < 2; pulse++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
		m->process(Test::makeProcessArgs(pulse * 2 + 1));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
		m->process(Test::makeProcessArgs(pulse * 2 + 2));
	}
	REQUIRE(m->triggerIns.triggerTick[0][0] == 2);
	REQUIRE(m->triggerIns.triggerTick[0][1] == 0);
	REQUIRE(m->midiOuts.ports[0].tickQueue[1].size() == 1);   // still queued

	// Two pulses on channel 2 drain it (tick 2 reached).
	for (int pulse = 0; pulse < 2; pulse++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
		m->process(Test::makeProcessArgs(pulse * 2 + 10));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
		m->process(Test::makeProcessArgs(pulse * 2 + 11));
	}
	REQUIRE(m->triggerIns.triggerTick[0][1] == 2);
	REQUIRE(m->midiOuts.ports[0].tickQueue[1].size() == 0);   // drained
}


// process() drains the module queue
// Runs process() over one full divider period (division 8), so the drain block
// inside `if (processDivider.process())` is reached exactly once.
static void processOneDividerPeriod(MidiKitModule* m, int64_t startFrame = 0) {
	for (int64_t f = 0; f < 8; f++) {
		m->process(Test::makeProcessArgs(startFrame + f));
	}
}

TEST_CASE("process() drains the out-queue after the script is cleared", "[MidiKit][CrossEngine]") {
	// The drain in process() sits ABOVE the activeEngine null check, on purpose.
	// clearScript() runs onUnload() (queuing its message) and leaves
	// activeEngine null; if the drain were still gated on activeEngine, that
	// message would sit in the queue forever — the script's all-notes-off never
	// reaching the device. Asserting via process() rather than reading the queue
	// directly is what makes this cover the hoist.
	auto check = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);

		m->clearScript();
		REQUIRE(m->host.getActiveEngine() == nullptr);
		REQUIRE_FALSE(m->midiOuts.queue.empty());   // onUnload()'s message is queued

		processOneDividerPeriod(m);

		// process() moved it out of the module queue even with no active engine.
		REQUIRE(m->midiOuts.queue.empty());
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("process() drains a tick-scheduled message into midiOutput", "[MidiKit]") {
	// End-to-end for the drain: a message the engine queued with a non-zero tick
	// must reach out.ports[0]'s tick queue, not merely leave the module queue.
	// midi::Output::sendMessage() no-ops without a subscribed device, so
	// out.ports[0]'s scheduling queues are the observable endpoint.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	midi::Message msg = noteOn(1, 60, 100);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));   // tick 5: lands in tickQueue
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);

	processOneDividerPeriod(m);

	REQUIRE(m->midiOuts.queue.empty());
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].top().tick == 5);
}



TEST_CASE("An NRPN group is queued whole and in order", "[MidiKit]") {
	// The atomicity contract has two halves: dropped whole when short on room
	// (below), and — here — queued as four consecutive messages in the order
	// given, with no interleaving from a message queued after it.
	Kit<> kit;
	MidiKitModule* m = kit.m;

	midi::Message group[4] = {noteOn(1, 60, 100), noteOn(1, 61, 100), noteOn(1, 62, 100), noteOn(1, 63, 100)};
	midi::Message after = noteOn(1, 70, 100);
	REQUIRE(m->sendMidi(0, group, 4, 0, 0));
	REQUIRE(m->sendMidi(0, &after, 1, 0, 0));

	int port, ticks;
	midi::Message out;
	for (int i = 0; i < 4; i++) {
		REQUIRE(processOutMessage(m, port, out, ticks));
		REQUIRE(out.getNote() == 60 + i);
	}
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getNote() == 70);
}


TEST_CASE("onRemove() flushes due output immediately and drops what is scheduled", "[MidiKit]") {
	// out.flush() sets frame = -1 and calls out.ports[0].sendMessage() directly
	// rather than out.ports[0].send(): the frame and tick queues are drained only
	// by process(), which will never run again. A tick-scheduled message belongs
	// to the script being removed and is dropped with it, not parked in a queue.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));   // would be tick-scheduled via send()

	Module::RemoveEvent eRemove;
	m->onRemove(eRemove);

	REQUIRE(m->midiOuts.queue.empty());
	// Dropped instead of being parked in a queue nothing will drain.
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
	REQUIRE(m->midiOuts.ports[0].frameQueue.size() == 0);

	delete m;
}


TEST_CASE("MIDI output overflow drops without corrupting the queue", "[MidiKit]") {
	// dsp::RingBuffer::push() has no overflow check: on a full buffer it
	// overwrites unread entries and leaves size() > capacity, and
	// empty()/full() go incoherent from there. sendMidi() adds the check the
	// container lacks — this pins that the queue's invariants survive being
	// pushed past capacity.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	midi::Message msg = noteOn(1, 60, 100);

	size_t capacity = m->midiOuts.queue.capacity();
	for (size_t i = 0; i < capacity; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	REQUIRE(m->midiOuts.queue.full());

	// One more push has no room: dropped, not overwritten.
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE(m->midiOuts.queue.full());
	REQUIRE(m->midiOuts.queue.size() == capacity);
	REQUIRE_FALSE(m->midiOuts.queue.empty());
}


TEST_CASE("MIDI output overflow is reported once per episode, not once per drop", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	midi::Message msg = noteOn(1, 60, 100);

	size_t capacity = m->midiOuts.queue.capacity();
	for (size_t i = 0; i < capacity; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	// Several drops in the same episode — only one log line should result once
	// process() next runs and consumes the rising edge of out.overflow.
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	for (int64_t f = 0; f < 8; f++) {
		m->process(Test::makeProcessArgs(f));
	}
	auto entries = drainLogEntries(m);
	size_t count = 0;
	for (auto& e : entries) {
		if (std::get<1>(e).find("dropped") != std::string::npos) count++;
	}
	REQUIRE(count == 1);
}


TEST_CASE("MIDI output overflow is reported again after the queue recovers", "[MidiKit]") {
	// The flag is edge-triggered via exchange(false), so reporting once per
	// episode must not mean once per module lifetime: a later, separate
	// saturation has to log again. Pins that process() CLEARS the flag rather
	// than latching it.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	midi::Message msg = noteOn(1, 60, 100);

	auto fillAndOverflow = [&]() {
		while (m->midiOuts.queue.capacity() > 0) {
			REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
		}
		REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	};
	auto countDropLines = [&]() {
		size_t count = 0;
		for (auto& e : drainLogEntries(m)) {
			if (std::get<1>(e).find("dropped") != std::string::npos) count++;
		}
		return count;
	};

	// One drain hands the ports at most DRAIN_BUDGET entries, so emptying a full
	// ring takes several divider periods.
	int64_t frame = 0;
	auto drainAll = [&]() {
		while (!m->midiOuts.queue.empty()) {
			processOneDividerPeriod(m, frame);
			frame += 8;
		}
	};

	fillAndOverflow();
	drainAll();                               // logs once, at the first drain
	REQUIRE(countDropLines() == 1);
	REQUIRE(m->midiOuts.queue.empty());

	// A quiet period with no drops must log nothing. This is what pins the
	// CLEARING of the flag: a latched flag would keep reporting here.
	processOneDividerPeriod(m, frame);
	frame += 8;
	REQUIRE(countDropLines() == 0);

	// Second, independent episode: reports again rather than staying silent
	// after the first — the flag re-arms.
	fillAndOverflow();
	drainAll();
	REQUIRE(countDropLines() == 1);
}


TEST_CASE("The output ring takes 2048 entries and drops a group that does not fit whole, never truncated", "[MidiKit][Timing]") {
	// The all-or-nothing contract in sendMidi(): an NRPN is 4 messages sharing
	// one parameter change, and a partial group is a malformed parameter
	// change, worse than dropping it outright.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	auto& outs = m->midiOuts;
	midi::Message msg = noteOn(1, 60, 100);

	// Leave exactly 3 free slots — one short of the 4-message group.
	size_t capacity = outs.queue.capacity();
	REQUIRE(capacity == 2048);
	for (size_t i = 0; i < capacity - 3; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	REQUIRE(outs.queue.capacity() == 3);
	REQUIRE_FALSE(outs.overflow.load());

	midi::Message group[4] = {msg, msg, msg, msg};
	REQUIRE_FALSE(m->sendMidi(0, group, 4, 0, 0));
	// Rejected as a whole: the 3 free slots are still free, not partially
	// consumed by the first 3 messages of the group, and the drop is flagged.
	REQUIRE(outs.queue.capacity() == 3);
	REQUIRE(outs.overflow.load());

	// The slots stay usable to the last one.
	for (int i = 0; i < 3; i++) REQUIRE(outs.enqueue(0, &msg, 1, 0, 0));
	REQUIRE(outs.queue.full());
}


// The scheduling queues are reserved up front and bounded, so scheduling itself
// never allocates on the audio thread (the one copy left is Rack's own
// Output::sendMessage(), on its side). A message that doesn't fit goes out at once
// instead of being dropped (a dropped Note-Off would leave a note stuck), and
// the log says so once.

TEST_CASE("A full frame-scheduling queue sends the overflow at once and logs it once", "[MidiKit][Timing]") {
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

TEST_CASE("A full trigger-tick queue sends the overflow at once and logs it once", "[MidiKit][Timing]") {
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

TEST_CASE("A script swap re-arms the schedule-queue-full log line", "[MidiKit][Timing]") {
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

TEST_CASE("A burst of 1000 sends in one callback arrives whole and in order", "[MidiKit][Timing]") {
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

TEST_CASE("One drain hands the ports at most DRAIN_BUDGET entries", "[MidiKit][Timing]") {
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

TEST_CASE("A replaced script's leftover burst is discarded; its onUnload output and the new script's messages arrive", "[MidiKit][Timing]") {
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

TEST_CASE("Timing: frames arriving out of order are released by frame", "[MidiKit][Timing]") {
	FOR_EACH_LANG;
	const char* script = TIMING_SCRIPT.get(lang);
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

TEST_CASE("Input queue overflow raises one notice per saturation", "[MidiKit][Timing]") {
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
