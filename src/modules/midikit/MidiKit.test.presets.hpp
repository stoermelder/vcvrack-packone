using StoermelderPackOne::MidiScript::ScriptMenuItem;

// processInMessage only queues the message — process() is what actually runs
// midi.onMessage(), so both are needed or the script never executes at all.
static void feed(MidiKitModule* m, midi::Message msg) {
	m->host.getActiveEngine()->processInMessage(0, QueuedMessage(msg));
	m->host.getActiveEngine()->process();
}

static midi::Message cc(int ch, int num, int value) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	msg.setChannel(ch);
	msg.setNote(num);
	msg.setValue(value);
	return msg;
}

static midi::Message clockTick() {
	midi::Message msg;
	msg.setSize(1);
	msg.bytes[0] = 0xf8;
	return msg;
}

static midi::Message startMsg() {
	midi::Message msg;
	msg.setSize(1);
	msg.bytes[0] = 0xfa;
	return msg;
}

static midi::Message continueMsg() {
	midi::Message msg;
	msg.setSize(1);
	msg.bytes[0] = 0xfb;
	return msg;
}

static midi::Message stopMsg() {
	midi::Message msg;
	msg.setSize(1);
	msg.bytes[0] = 0xfc;
	return msg;
}

// A 14-bit pitch wheel message. The engine reads it back as
// (getValue() << 7) | getNote(), so the MSB goes in value and the LSB in note.
static midi::Message pitchWheel(int ch, int value) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xe);
	msg.setChannel(ch);
	msg.setNote(value & 0x7f);
	msg.setValue((value >> 7) & 0x7f);
	return msg;
}

// A 2-byte channel-pressure message; the pressure value lives in bytes[1],
// read back via getChanPressure()/getNote().
static midi::Message chanPressure(int ch, int value) {
	midi::Message msg;
	msg.setSize(2);
	msg.setStatus(0xd);
	msg.setChannel(ch);
	msg.setNote(value);
	return msg;
}

// Feeds a raw message through the module's MidiProcessor — the production
// decode path that assembles NRPN/RPN/14-bit CC — and dispatches it, without
// draining the out-queue (the smoke test reads the queue once at the end).
// The feed()/Kit::dispatch() helpers go straight to the engine and thus
// bypass MidiProcessor; scripts that opt in via midi.enableNrpnIn() and
// friends need this instead.
static void feedDecoded(MidiKitModule* m, midi::Message msg) {
	m->midiIns.ports[0].processor.processMessage(msg);
	m->host.getActiveEngine()->process();
}

// Both encodings of a key release: a Note-Off (0x80) and, from most keyboards, a
// Note-On with velocity 0. Presets that track held notes must treat them alike.
static std::vector<midi::Message> releasesOf(int ch, int note) {
	return { noteOff(ch, note), noteOn(ch, note, 0) };
}

// Looks up the PRESETS[] entry for a named preset; the name must match. A
// mistyped name fails loudly here rather than silently running the wrong
// script.
static const PresetInfo& requirePreset(const char* name) {
	for (const auto& p : PRESETS)
		if (std::strcmp(p.name, name) == 0) return p;
	FAIL("preset not found in PRESETS[]: " << name);
	return PRESETS[0]; // unreachable; silences the return-path warning
}

// Generator yielding the preset file path for every engine variant of the
// named preset (e.g. ".../JavaScript/Arpeggiator.js" and ".../Lua/Arpeggiator.lua").
// One GENERATE over this replaces the former engine + preset pair.
static auto presetPaths(const char* name) {
	return Catch::Generators::map(
		[name](const char* engine) { return presetPath(requirePreset(name), engine); },
		Catch::Generators::from_range(std::begin(ENGINES), std::end(ENGINES)));
}

static void checkPreset(const PresetInfo& p, const char* engine) {
	std::string relPath = presetPath(p, engine);
	CATCH_INFO("preset: " << relPath);
	std::string source = readFile(repoRoot() + "/" + relPath);

	// rack.onSave()/rack.onLoad(persisted) no longer exist — hard API break,
	// no migration shim. Assigning an unknown property on rack is silent in
	// both engines (it's a plain object), so a leftover rack.onSave = ...
	// would load without ever logging an error and this grep is the only
	// thing that would catch it.
	REQUIRE(source.find("onSave") == std::string::npos);

	Kit<> kit;
	MidiKitModule* m = kit.m;
	std::string loadLog = kit.loadRaw(source);
	CATCH_INFO("load log:\n" << loadLog);
	REQUIRE(loadLog.find("rror") == std::string::npos);
	REQUIRE(loadLog.find("not compatible") == std::string::npos);
	REQUIRE(loadLog.find("Script loaded") != std::string::npos);

// Representative traffic: notes on two channels (exercising channel filters
// and MPE member handling), a full release, and enough clock ticks to drive
// the clock-counting scripts past a step.
	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(2, 64, 40));
	feed(m, noteOff(1, 60));
	feed(m, noteOff(2, 64));
	for (int i = 0; i < 32; i++) feed(m, clockTick());

	// A complete NRPN write (parameter 1, which the NRPN preset maps) plus a
	// plain CC - note/clock traffic alone leaves the CC-driven presets silent.
	// Fed through MidiProcessor so scripts using the assembled-input API
	// (midi.enableNrpnIn and friends) see it as a parameter change too; the
	// engine-direct feed() above would bypass that assembly.
	feedDecoded(m, cc(1, 99, 0));    // parameter number MSB
	feedDecoded(m, cc(1, 98, 1));    // parameter number LSB
	feedDecoded(m, cc(1, 6, 64));    // data entry MSB
	feedDecoded(m, cc(1, 38, 0));    // data entry LSB
	feedDecoded(m, cc(1, 20, 100));

	std::string runLog = drainLog(m);
	CATCH_INFO("runtime log:\n" << runLog);
	REQUIRE(runLog.find("rror") == std::string::npos);

	// Every preset emits something for the traffic above (without this, a
	// script that never ran would pass the log checks). The Arpeggiator is
	// trigger-clocked and emits nothing for MIDI traffic, so it is exempt.
	if (p.midiDriven) {
		int outPort = 0, outTicks = 0;
		midi::Message outMsg;
		REQUIRE(processOutMessage(m, outPort, outMsg, outTicks));
	}
}

// GENERATE re-runs the body once per preset name, and each generated value is
// its own Catch2 leaf: a failure names the preset that broke and the others
// still run. Listing the names is unavoidable - a TEST_CASE is registered at
// static-init time, so it can't be produced per array element.
TEST_CASE("Preset loads and runs without errors, in both engines", "[MidiKit][Presets]") {
	const char* engine = GENERATE(from_range(std::begin(ENGINES), std::end(ENGINES)));
	PresetInfo p = GENERATE(from_range(std::begin(PRESETS), std::end(PRESETS)));
	CATCH_INFO("engine: " << engine);
	checkPreset(p, engine);
}

// Behavioural tests for the Arpeggiator preset. It is clocked by trig.onTrigger
// (the CV trigger input) rather than MIDI, and its four params are read live
// from the module's Param objects. Each case sets params directly, builds a
// held chord via Note-On/Off, steps the clock via dispatchTick(), and asserts on
// the decoded note sequence and timing.


// Loads one of the two Arpeggiator presets and sets its four params by
// normalized 0..1 Param value - the same reads a user's panel knobs produce,
// so this exercises the script's param.getValue() path.
static MidiKitModule* loadArp(Kit<>& kit, const std::string& relPath, float clockDivision, float octaveRange, float noteLength, float playmode) {
	MidiKitModule* m = kit.loadPreset(relPath).m;
	m->params[MidiKitModule::PARAM + 0].setValue(clockDivision);
	m->params[MidiKitModule::PARAM + 1].setValue(octaveRange);
	m->params[MidiKitModule::PARAM + 2].setValue(noteLength);
	m->params[MidiKitModule::PARAM + 3].setValue(playmode);
	return m;
}

// Up mode, 1 tick/step, 1 octave: plain ascending replay of the held chord
TEST_CASE("'Arpeggiator.js/.lua' Up mode steps the held chord in press order", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// clockDivision=0 -> DIVISIONS[0]=1 tick/step, octaveRange=0 -> 1 octave,
	// playmode=0 -> Up.
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 6; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	// Up mode over {60,64,67}: 60,64,67,60,64,67 - one step per tick, in
	// press order, cycling back to the start after the last note.
	std::vector<uint8_t> expected = {60, 64, 67, 60, 64, 67};
	REQUIRE(notesOn == expected);
}

// Down mode: exact reverse of press order
TEST_CASE("'Arpeggiator.js/.lua' Down mode steps the held chord in reverse", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// playmode=0.5 -> Down (index 1 of 3 in PLAYMODES).
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 0.5f, 0.5f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 3; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	std::vector<uint8_t> expected = {67, 64, 60};
	REQUIRE(notesOn == expected);
}

// Up-Down mode: ascends then descends without repeating the two end notes
TEST_CASE("'Arpeggiator.js/.lua' Up-Down mode does not repeat the end notes", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// playmode=0.99 -> last entry in PLAYMODES (Up-Down, index 2 of 3).
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 0.5f, 0.99f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 8; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	// {60,64,67} up-down, without repeating 67 or 60 at the turnarounds:
	// 60,64,67,64,60,64,67,64
	std::vector<uint8_t> expected = {60, 64, 67, 64, 60, 64, 67, 64};
	REQUIRE(notesOn == expected);
}

// Octave range doubles the pattern upward before it cycles
TEST_CASE("'Arpeggiator.js/.lua' octave range repeats the chord one octave higher", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// octaveRange=0.5 -> floor(0.5*4)+1 = 3 octaves, playmode=0 -> Up.
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.5f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 6; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	std::vector<uint8_t> expected = {60, 64, 72, 76, 84, 88};
	REQUIRE(notesOn == expected);
}

// Clock division: steps only advance every Nth trigger tick
TEST_CASE("'Arpeggiator.js/.lua' clock division holds the step across intermediate ticks", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// clockDivision index 3 -> DIVISIONS[3] = 4 ticks/step. 4 divisions span
	// indices [0.3, 0.4) of the 10-entry list, so 0.35 lands there reliably.
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.35f, 0.f, 0.9f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	drainLog(m);

	int noteOnCount = 0;
	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 8; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) {
			if (e.status == 0x9) {
				noteOnCount++;
				notesOn.push_back(e.note);
			}
		}
	}

	// 8 ticks at 4 ticks/step = exactly 2 steps, not 8.
	REQUIRE(noteOnCount == 2);
	REQUIRE(notesOn == std::vector<uint8_t>{60, 64});
}

// Note length: the scheduled Note-Off must always land before the next Note-On
TEST_CASE("'Arpeggiator.js/.lua' note length never overruns into the next step", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// noteLength=1.0 (full gate) at clockDivision index 0 -> 1 tick/step; the
	// script must clamp length to division-1 (>=1) rather than tying notes.
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 1.0f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	drainLog(m);

	// Walk enough ticks to see several steps; verify every Note-On for a note
	// is preceded, on some earlier tick, by a matching Note-Off releasing the
	// previously sounding note (i.e. the arp always cuts the last note before
	// or in the same tick as starting the next one - never after).
	int lastNote = -1;
	bool lastReleased = true;
	for (int i = 0; i < 6; i++) {
		auto events = kit.dispatchTick();
		bool releasedThisTick = false;
		int newNote = -1;
		for (auto& e : events) {
			if (e.status == 0x8 && e.note == lastNote) releasedThisTick = true;
			if (e.status == 0x9) newNote = e.note;
		}
		if (releasedThisTick) lastReleased = true;
		if (newNote >= 0) {
			REQUIRE(lastReleased);
			lastNote = newNote;
			lastReleased = false;
		}
	}
}

// Releasing all held notes stops the arp; no further Note-On is sent
TEST_CASE("'Arpeggiator.js/.lua' stops stepping once every note is released", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	drainLog(m);
	auto firstStep = kit.dispatchTick();
	bool sawNoteOn = false;
	for (auto& e : firstStep) if (e.status == 0x9) sawNoteOn = true;
	REQUIRE(sawNoteOn);

	feed(m, noteOff(1, 60));

	bool sawAnyNoteOnAfterRelease = false;
	for (int i = 0; i < 4; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) if (e.status == 0x9) sawAnyNoteOnAfterRelease = true;
	}
	REQUIRE_FALSE(sawAnyNoteOnAfterRelease);
}

// Presets that track held notes treat a velocity-0 Note-On like a Note-Off. Each
// case plays the same notes twice and releases them once as 0x80 and once as 0x90
// with velocity 0; what reaches the output must agree, except that a preset which
// forwards the message as it is leaves the velocity-0 Note-On as one.
static Out releaseNormalised(Out e) {
	if (e.status == 0x9 && e.value == 0) e.status = 0x8;
	return e;
}

TEST_CASE("Presets that track held notes treat a velocity-0 Note-On as a release", "[MidiKit][Release]") {
	struct Case {
		const char* preset;
		void (*setup)(MidiKitModule*);
		std::vector<midi::Message> ons;
		midi::Message release;
		bool arp;
	};
	const Case cases[] = {
		{ "Chord harmonizer", nullptr, { noteOn(1, 60, 100) }, noteOff(1, 60), false },
		{ "Micro scale", nullptr, { noteOn(1, 60, 100), noteOn(1, 64, 100) }, noteOff(1, 64), false },
		{ "Volca Sample", nullptr, { noteOn(15, 60, 100), noteOn(15, 64, 100) }, noteOff(15, 64), false },
		{ "Keyboard split", nullptr, { noteOn(1, 50, 100), noteOn(1, 84, 100) }, noteOff(1, 84), false },
		{ "Gravity well", [](MidiKitModule* m) {
			m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
			m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
		  }, { noteOn(1, 72, 40) }, noteOff(1, 72), false },
		{ "Arpeggiator", nullptr, { noteOn(1, 60, 100) }, noteOff(1, 60), true },
	};
	for (const Case& c : cases) {
		std::string path = GENERATE_COPY(presetPaths(c.preset));
		CATCH_INFO("preset: " << path);

		std::vector<Out> results[2];
		for (int encoding = 0; encoding < 2; encoding++) {
			Kit<> kit;
			MidiKitModule* m = c.arp ? loadArp(kit, path, 0.f, 0.f, 0.5f, 0.f) : kit.loadPreset(path).m;
			if (c.setup) c.setup(m);
			kit.drain();
			drainLog(m);
			for (const midi::Message& on : c.ons) kit.dispatch(on);

			midi::Message release = c.release;
			if (encoding == 1) release = noteOn(c.release.getChannel(), c.release.getNote(), 0);
			CATCH_INFO("encoding: " << (encoding == 0 ? "Note-Off" : "Note-On velocity 0"));

			std::vector<Out> events = kit.dispatch(release);
			// The arpeggiator's release shows in what it plays on the following ticks.
			if (c.arp) {
				for (int i = 0; i < 4; i++) {
					for (const Out& n : kit.dispatchTick()) events.push_back(Out(n.status, n.channel, n.note, n.value, 0));
				}
				for (const Out& e : events) REQUIRE(e.status != 0x9);
			}
			for (const Out& e : events) results[encoding].push_back(releaseNormalised(e));
		}
		if (!c.arp) REQUIRE_FALSE(results[0].empty());
		REQUIRE(results[1] == results[0]);
	}
}

// onUnload releases whatever note the arp is currently sustaining
TEST_CASE("'Arpeggiator.js/.lua' releases the sounding note on unload", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// clockDivision=0 -> 1 tick/step, so the first dispatchTick() lands on a step
	// boundary; noteLength=1.0 (clamped to division-1, >= 1 tick) keeps the
	// note sustained when onUnload fires.
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 1.0f, 0.f);

	feed(m, noteOn(1, 60, 100));
	drainLog(m);
	auto stepEvents = kit.dispatchTick();
	uint8_t soundingNote = 0;
	for (auto& e : stepEvents) if (e.status == 0x9) soundingNote = e.note;
	REQUIRE(soundingNote != 0);
	drainLog(m);

	// loadScript("") runs onUnload() synchronously and queues its output on
	// the module's out-queue, which survives the reload regardless of which
	// engine produced it or that activeEngine now points elsewhere.
	m->loadScript("");

	int port, ticks;
	midi::Message out;
	bool sawMatchingNoteOff = false;
	while (processOutMessage(m, port, out, ticks)) {
		if (out.getStatus() == 0x8 && out.getNote() == soundingNote) sawMatchingNoteOff = true;
	}
	REQUIRE(sawMatchingNoteOff);
}


// Dynamic chords: adding/removing a held note mid-arp rebuilds the pattern
// for the next step without corrupting the step position. rebuildPattern()
// only resets the step when it has run past the (possibly shrunken) pattern's
// end: adding keeps the current step and folds the note in at its press-order
// position; removing can clamp the step back to 0. These two cases pin the
// difference.
TEST_CASE("'Arpeggiator.js/.lua' folds a note added mid-arp into the pattern from the current step", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// 1 tick/step, 1 octave, Up mode.
	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	drainLog(m);

	// Step once: pattern [60,64] plays 60, step advances to 1.
	auto first = kit.dispatchTick();
	bool saw60 = false;
	for (auto& e : first) if (e.status == 0x9 && e.note == 60) saw60 = true;
	REQUIRE(saw60);
	drainLog(m);

	// Add 67 mid-arp: rebuildPattern() makes [60,64,67] but does not reset the
	// step (stays at 1), so 64 still plays before the newly added 67.
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 4; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) if (e.status == 0x9) notesOn.push_back(e.note);
	}

	// Continuing from step 1 of the rebuilt [60,64,67] pattern.
	std::vector<uint8_t> expected = {64, 67, 60, 64};
	REQUIRE(notesOn == expected);
}

TEST_CASE("'Arpeggiator.js/.lua' clamps the step when a note removed mid-arp shrinks the pattern", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	// Advance the step twice: plays 60 then 64, leaving step at 2.
	kit.dispatchTick();
	kit.dispatchTick();
	drainLog(m);

	// Remove 60 mid-arp: the pattern shrinks to [64,67] and step 2 is past its
	// end, so rebuildPattern() clamps it to 0 and restarts the pattern.
	feed(m, noteOff(1, 60));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 3; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) if (e.status == 0x9) notesOn.push_back(e.note);
	}

	std::vector<uint8_t> expected = {64, 67, 64};
	REQUIRE(notesOn == expected);
}


// Behavioural tests for the Euclidean rhythm generator preset. Clocked by the
// trigger input, rebuilding its pattern live from the four panel params:
// Steps (1-16), Fills (0..Steps), Note (0-127), Velocity (1-127). Each trigger
// tick advances one step and, on a hit, fires a Note-On that sustains until
// the next tick (a one-step gate). The pattern is the canonical Bjorklund
// distribution (4/2 -> [0,1,0,1]). MIDI IN passes through unchanged.


TEST_CASE("'Euclidean rhythm generator.js/.lua' fires a note on each Euclidean hit", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	// steps=4, fills=2 -> pattern [0,1,0,1] (hits on steps 1 and 3), note 64,
	// velocity 33, output channel 1.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	// Two bars (8 ticks): hits land on ticks 2,4,6,8 (steps 1,3,1,3). The rest
	// ticks emit no Note-On.
	std::vector<int> hitTicks;
	for (int i = 1; i <= 8; i++) {
		auto events = kit.dispatchTick();
		bool hit = false;
		for (auto& e : events) {
			if (e.status == 0x9) {
				hit = true;
				REQUIRE(e.channel == 0);
				REQUIRE(e.note == 64);
				REQUIRE(e.value == 33);
			}
		}
		if (hit) hitTicks.push_back(i);
	}
	std::vector<int> expected = {2, 4, 6, 8};
	REQUIRE(hitTicks == expected);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' passes MIDI in through unchanged", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// The script is a pure generator: whatever arrives on MIDI IN is forwarded
	// untouched on its own channel.
	auto ccEv = kit.dispatch(cc(1, 20, 100));
	REQUIRE(ccEv == std::vector<Out>{{0xb, 1, 20, 100, 0}});
	auto note = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(note == std::vector<Out>{{0x9, 1, 60, 100, 0}});
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' plays the canonical 5-in-8 pattern", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	// steps=8, fills=5 -> the canonical Bjorklund distribution
	// [1,0,1,1,0,1,1,0], hits on steps 0,2,3,5,6. This pins the algorithm
	// beyond the trivially symmetric 4-in-2 case.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(7.0f / 15.0f);  // 8 steps
	m->params[MidiKitModule::PARAM + 1].setValue(5.0f / 8.0f);   // 5 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);          // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);         // velocity 33
	drainLog(m);

	// One bar (8 ticks): hits on ticks 1,3,4,6,7.
	std::vector<int> hitTicks;
	for (int i = 1; i <= 8; i++) {
		auto events = kit.dispatchTick();
		bool hit = false;
		for (auto& e : events) if (e.status == 0x9) hit = true;
		if (hit) hitTicks.push_back(i);
	}
	std::vector<int> expected = {1, 3, 4, 6, 7};
	REQUIRE(hitTicks == expected);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' fills 0 silences and fills == steps fires every step", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	// fills = 0 -> the pattern is all rests: no Note-On across a full bar.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.0f);   // 0 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);
	for (int i = 0; i < 8; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) REQUIRE_FALSE(e.status == 0x9);
	}

	// fills = steps -> every step is a hit.
	Kit<> kit2;
	MidiKitModule* m2 = kit2.loadPreset(path).m;
	m2->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m2->params[MidiKitModule::PARAM + 1].setValue(1.0f);   // 4 fills
	m2->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m2->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m2);
	for (int i = 0; i < 4; i++) {
		auto events = kit2.dispatchTick();
		bool hit = false;
		for (auto& e : events) if (e.status == 0x9) hit = true;
		REQUIRE(hit);
	}
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' gates each hit for exactly one step", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	// steps=4, fills=2 -> [0,1,0,1]. A Note-On on a hit tick must be released
	// by the very next trigger tick (one-step gate) - the Note-Off comes out
	// even though the next step is a rest.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	// tick 1 (rest): nothing sounds, nothing to release.
	REQUIRE(kit.dispatchTick().empty());

	// tick 2 (hit): a Note-On goes out.
	auto t2 = kit.dispatchTick();
	bool on = false;
	for (auto& e : t2) if (e.status == 0x9) on = true;
	REQUIRE(on);

	// tick 3 (rest): the tick-2 note is released - a matching Note-Off.
	auto t3 = kit.dispatchTick();
	bool off = false;
	for (auto& e : t3) if (e.status == 0x8 && e.note == 64) off = true;
	REQUIRE(off);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' output channel menu changes the note channel", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33

	// "Output channel" option index 1 -> MIDI channel 2 (internal 1).
	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(specs.size() == 1);
	REQUIRE(specs[0].label == "MIDI channel (Output)");
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[0].callbackId, 1);
	drainLog(m);

	// The first hit (tick 2) goes out on the new channel.
	kit.dispatchTick();   // tick 1: rest
	auto t2 = kit.dispatchTick();
	bool onCh2 = false;
	for (auto& e : t2) if (e.status == 0x9 && e.channel == 1) onCh2 = true;
	REQUIRE(onCh2);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' releases the sounding note on unload", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	// fills = steps so the first trigger tick already fires a note, which is
	// still sounding when the script is unloaded immediately after.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);   // every step hits
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	kit.dispatchTick();
	drainLog(m);

	m->loadScript("");

	// onUnload releases the still-sounding note on output channel 1
	// (internal 0).
	auto ev = kit.drain();
	REQUIRE(ev == std::vector<Out>{{0x8, 0, 64, 0, 0}});
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' rebuilds the pattern live when a param changes", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills -> [0,1,0,1]
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	// First bar of [0,1,0,1]: hits on ticks 2 and 4.
	std::vector<int> firstBar;
	for (int i = 1; i <= 4; i++) {
		auto events = kit.dispatchTick();
		bool hit = false;
		for (auto& e : events) if (e.status == 0x9) hit = true;
		if (hit) firstBar.push_back(i);
	}
	REQUIRE(firstBar == std::vector<int>{2, 4});

	// Drop fills to 0 mid-run: the pattern is rebuilt from the live knob, so
	// from the very next tick nothing ever fires again (only releases).
	m->params[MidiKitModule::PARAM + 1].setValue(0.0f);
	drainLog(m);
	for (int i = 0; i < 6; i++) {
		auto events = kit.dispatchTick();
		for (auto& e : events) REQUIRE_FALSE(e.status == 0x9);
	}
}


// Behavioural tests for the Keyboard split preset. The shipped config defines
// three presets, each a split point plus two output channels, activated by a
// CC matching its `cc` with value > 0:
//   preset 1: CC 70, A=1, B=2, split 60  (active at load)
//   preset 2: CC 71, A=3, B=4, split 48
//   preset 3: CC 72, A=5, B=6, split 72
// Notes below the split go to A, at/above to B (Note-Offs likewise). The
// trigger CCs are consumed; other non-note messages pass through.


TEST_CASE("'Keyboard split.js/.lua' routes notes below and above the split to channels A and B", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(presetPaths("Keyboard split"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Default preset 1: A=1 (internal 0), B=2 (internal 1), split at 60.
	auto below = kit.dispatch(noteOn(1, 50, 100));   // below the split -> A
	REQUIRE(below == std::vector<Out>{{0x9, 0, 50, 100, 0}});
	auto at = kit.dispatch(noteOn(1, 60, 100));      // at the split -> B
	REQUIRE(at == std::vector<Out>{{0x9, 1, 60, 100, 0}});
	auto above = kit.dispatch(noteOn(1, 84, 100));   // above -> B
	REQUIRE(above == std::vector<Out>{{0x9, 1, 84, 100, 0}});

	// Note-Offs are rewritten by the same rule.
	auto offBelow = kit.dispatch(noteOff(1, 50));
	REQUIRE(offBelow == std::vector<Out>{{0x8, 0, 50, 0, 0}});
	auto offAbove = kit.dispatch(noteOff(1, 84));
	REQUIRE(offAbove == std::vector<Out>{{0x8, 1, 84, 0, 0}});
}

TEST_CASE("'Keyboard split.js/.lua' a trigger CC with value > 0 switches the active preset", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(presetPaths("Keyboard split"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// CC 71 (preset 2's trigger) with value 100 activates preset 2 and is
	// consumed - nothing is forwarded.
	REQUIRE(kit.dispatch(cc(1, 71, 100)).empty());

	// Preset 2: A=3 (internal 2), B=4 (internal 3), split at 48.
	auto below = kit.dispatch(noteOn(1, 40, 100));   // below 48 -> A
	REQUIRE(below == std::vector<Out>{{0x9, 2, 40, 100, 0}});
	auto at = kit.dispatch(noteOn(1, 48, 100));      // at 48 -> B
	REQUIRE(at == std::vector<Out>{{0x9, 3, 48, 100, 0}});
	auto above = kit.dispatch(noteOn(1, 60, 100));   // above -> B
	REQUIRE(above == std::vector<Out>{{0x9, 3, 60, 100, 0}});
}

TEST_CASE("'Keyboard split.js/.lua' a trigger CC with value 0 does not switch presets", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(presetPaths("Keyboard split"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// CC 71 with value 0 is a control CC (still consumed) but does not switch.
	REQUIRE(kit.dispatch(cc(1, 71, 0)).empty());

	// Preset 1 is still active: note 50 -> A=1 (internal 0).
	auto below = kit.dispatch(noteOn(1, 50, 100));
	REQUIRE(below == std::vector<Out>{{0x9, 0, 50, 100, 0}});
}

TEST_CASE("'Keyboard split.js/.lua' passes non-trigger messages through unchanged", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(presetPaths("Keyboard split"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// A plain CC (not a preset trigger) and a pitch wheel pass through on
	// their own channel.
	auto ccEv = kit.dispatch(cc(1, 7, 100));
	REQUIRE(ccEv == std::vector<Out>{{0xb, 1, 7, 100, 0}});
	auto pw = kit.dispatch(pitchWheel(1, 8192));
	REQUIRE(pw == std::vector<Out>{{0xe, 1, 0, 64, 0}});
}

TEST_CASE("'Keyboard split.js/.lua' the preset menu switches the active preset", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(presetPaths("Keyboard split"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(specs.size() == 1);
	REQUIRE(specs[0].label == "Preset");
	REQUIRE(specs[0].options.size() == 3);

	// Option index 2 -> preset 3 (A=5 internal 4, B=6 internal 5, split 72).
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[0].callbackId, 2);
	drainLog(m);

	auto below = kit.dispatch(noteOn(1, 70, 100));   // below 72 -> A
	REQUIRE(below == std::vector<Out>{{0x9, 4, 70, 100, 0}});
	auto above = kit.dispatch(noteOn(1, 84, 100));   // above -> B
	REQUIRE(above == std::vector<Out>{{0x9, 5, 84, 100, 0}});
}


// Behavioural tests for the Bouncing ball delay preset. Every Note-On passes
// through (dry) and spawns a pre-scheduled echo train: each echo's gap is the
// previous gap times (1 - Gravity) and its velocity the previous times
// Bounciness, until velocity drops below Min or the echo cap is hit. Params
// are read at the Note-On. sendAfterMs() echoes carry a positive `frame` (the
// dry note has frame -1), so the drain below sees the whole train at once.


TEST_CASE("'Bouncing ball delay.js/.lua' passes the note through and echoes it with decaying velocity", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(presetPaths("Bouncing ball delay"));
	CATCH_INFO("preset: " << path);

	// bounciness 0.5 (half the velocity survives each bounce), gravity 0,
	// min velocity 1.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.0f);  // gravity 0
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);  // bounciness 0.5
	m->params[MidiKitModule::PARAM + 2].setValue(0.0f);  // min velocity 1
	drainLog(m);

	// Dry note first, then one Note-On/Note-Off pair per echo. Velocity decays
	// 100, 50, 25, 13, 6, 3, 2 (round-half-up) and the train stops once the
	// float velocity drops below 1.
	auto ev = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<Out>{
		{0x9, 1, 60, 100, 0},                                          // dry
		{0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 0},                       // echo 1
		{0x9, 1, 60, 50, 0},  {0x8, 1, 60, 0, 0},                       // echo 2
		{0x9, 1, 60, 25, 0},  {0x8, 1, 60, 0, 0},                       // echo 3
		{0x9, 1, 60, 13, 0},  {0x8, 1, 60, 0, 0},                       // echo 4
		{0x9, 1, 60, 6, 0},   {0x8, 1, 60, 0, 0},                       // echo 5
		{0x9, 1, 60, 3, 0},   {0x8, 1, 60, 0, 0},                       // echo 6
		{0x9, 1, 60, 2, 0},   {0x8, 1, 60, 0, 0},                       // echo 7
	});
}

TEST_CASE("'Bouncing ball delay.js/.lua' settles once the velocity drops below the min threshold", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(presetPaths("Bouncing ball delay"));
	CATCH_INFO("preset: " << path);

	// min velocity 32: vel 100 -> 100, 50, then 25 < 32 -> settle.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.0f);
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);
	m->params[MidiKitModule::PARAM + 2].setValue(31.0f / 126.0f);  // min velocity 32
	drainLog(m);

	auto ev = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<Out>{
		{0x9, 1, 60, 100, 0},                                          // dry
		{0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 0},                       // echo 1
		{0x9, 1, 60, 50, 0},  {0x8, 1, 60, 0, 0},                       // echo 2
	});
}

TEST_CASE("'Bouncing ball delay.js/.lua' gravity shrinks the interval between echoes", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(presetPaths("Bouncing ball delay"));
	CATCH_INFO("preset: " << path);

	// gravity 0.2 (intervals shrink to 80% per bounce), bounciness 1.0 (no
	// velocity decay -> the train runs to the maxEchoes cap), min velocity 1.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.5f);   // gravity 0.2
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);   // bounciness 1.0
	m->params[MidiKitModule::PARAM + 2].setValue(0.0f);   // min velocity 1
	drainLog(m);

	auto ev = kit.dispatch(noteOn(1, 60, 100));

	// Dry note immediate (frame -1); all maxEchoes = 12 echoes scheduled.
	std::vector<int64_t> onFrames;
	for (auto& e : ev) if (e.status == 0x9 && e.frame != -1) onFrames.push_back(e.frame);
	REQUIRE(onFrames.size() == 12);

	// Every gap is positive and the gaps strictly shrink (gravity > 0).
	std::vector<int64_t> gaps;
	for (size_t i = 1; i < onFrames.size(); i++) {
		int64_t gap = onFrames[i] - onFrames[i - 1];
		REQUIRE(gap > 0);
		gaps.push_back(gap);
	}
	for (size_t i = 1; i < gaps.size(); i++) REQUIRE(gaps[i] < gaps[i - 1]);

	// The first gap shrinks by the retention factor (1 - gravity) = 0.8, up to
	// integer frame rounding.
	REQUIRE(std::abs(double(gaps[1]) - double(gaps[0]) * 0.8) <= 2.0);

	// gravity 0 -> uniform delay: consecutive gaps stay equal (within rounding).
	Kit<> kit2;
	MidiKitModule* m2 = kit2.loadPreset(path).m;
	m2->params[MidiKitModule::PARAM + 0].setValue(0.0f);   // gravity 0
	m2->params[MidiKitModule::PARAM + 1].setValue(1.0f);
	m2->params[MidiKitModule::PARAM + 2].setValue(0.0f);
	drainLog(m2);

	auto ev2 = kit2.dispatch(noteOn(1, 60, 100));
	std::vector<int64_t> gaps2;
	int64_t prev = -1;
	for (auto& e : ev2) if (e.status == 0x9 && e.frame != -1) {
		if (prev >= 0) gaps2.push_back(e.frame - prev);
		prev = e.frame;
	}
	REQUIRE(gaps2.size() == 11);
	for (size_t i = 1; i < gaps2.size(); i++) {
		REQUIRE(std::abs(gaps2[i] - gaps2[0]) <= 1);
	}
}

TEST_CASE("'Bouncing ball delay.js/.lua' passes Note-Offs and non-note messages through unchanged", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(presetPaths("Bouncing ball delay"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// A Note-Off is not a Note-On, so it passes through and spawns no echoes.
	auto off = kit.dispatch(noteOff(1, 60));
	REQUIRE(off == std::vector<Out>{{0x8, 1, 60, 0, 0}});

	// Non-note messages pass through untouched too.
	auto ccEv = kit.dispatch(cc(1, 7, 100));
	REQUIRE(ccEv == std::vector<Out>{{0xb, 1, 7, 100, 0}});
	auto pw = kit.dispatch(pitchWheel(1, 8192));
	REQUIRE(pw == std::vector<Out>{{0xe, 1, 0, 64, 0}});
}


// Behavioural tests for the Gravity well preset. Every Note-On is retuned
// toward Center (param 1) by round(distance * Strength * (1 - velocity/127)):
// the bend grows with distance and shrinks with velocity, so soft/distant
// notes fall deep into the well and loud/near-center notes stay put. Strength
// 0 disables it and a center note is never bent. Because the sent pitch
// depends on velocity, the Note-Off (played note) is redirected to the sent
// note. Non-note messages pass through.


TEST_CASE("'Gravity well.js/.lua' bends notes toward the center, more for soft and distant notes", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(presetPaths("Gravity well"));
	CATCH_INFO("preset: " << path);

	// center 60, strength 1.0.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	// note 72 (12 above center), vel 100 -> bent 3 down to 69.
	auto a = kit.dispatch(noteOn(1, 72, 100));
	REQUIRE(a == std::vector<Out>{{0x9, 1, 69, 100, 0}});

	// same note, vel 40 -> bent further (8) down to 64: softer falls deeper.
	auto b = kit.dispatch(noteOn(1, 72, 40));
	REQUIRE(b == std::vector<Out>{{0x9, 1, 64, 40, 0}});

	// note 84 (24 above center), vel 100 -> bent 5 to 79: farther falls more
	// than note 72's 3 at the same velocity.
	auto c = kit.dispatch(noteOn(1, 84, 100));
	REQUIRE(c == std::vector<Out>{{0x9, 1, 79, 100, 0}});

	// note 48 (12 below center), vel 100 -> bent up 3 to 51.
	auto d = kit.dispatch(noteOn(1, 48, 100));
	REQUIRE(d == std::vector<Out>{{0x9, 1, 51, 100, 0}});

	// a note on the center is never bent.
	auto e = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(e == std::vector<Out>{{0x9, 1, 60, 100, 0}});
}

TEST_CASE("'Gravity well.js/.lua' redirects the Note-Off to the bent note", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(presetPaths("Gravity well"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	// note 72, vel 40 -> sent as 64, so its Note-Off must release 64.
	auto on = kit.dispatch(noteOn(1, 72, 40));
	REQUIRE(on == std::vector<Out>{{0x9, 1, 64, 40, 0}});
	auto off = kit.dispatch(noteOff(1, 72));
	REQUIRE(off == std::vector<Out>{{0x8, 1, 64, 0, 0}});
}

TEST_CASE("'Gravity well.js/.lua' max velocity and zero strength leave notes unbent", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(presetPaths("Gravity well"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	// velocity 127 -> the well exerts no pull at all.
	auto loud = kit.dispatch(noteOn(1, 84, 127));
	REQUIRE(loud == std::vector<Out>{{0x9, 1, 84, 127, 0}});

	// strength 0 -> the well is off regardless of velocity.
	m->params[MidiKitModule::PARAM + 1].setValue(0.0f);
	auto weak = kit.dispatch(noteOn(1, 84, 40));
	REQUIRE(weak == std::vector<Out>{{0x9, 1, 84, 40, 0}});
}

TEST_CASE("'Gravity well.js/.lua' passes non-note messages through unchanged", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(presetPaths("Gravity well"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	auto ccEv = kit.dispatch(cc(1, 7, 100));
	REQUIRE(ccEv == std::vector<Out>{{0xb, 1, 7, 100, 0}});
	auto pw = kit.dispatch(pitchWheel(1, 8192));
	REQUIRE(pw == std::vector<Out>{{0xe, 1, 0, 64, 0}});
}

TEST_CASE("'Gravity well.js/.lua' releases the held bent note on unload", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(presetPaths("Gravity well"));
	CATCH_INFO("preset: " << path);

	// A bent note that is still held when the script is replaced must be
	// released at its sent pitch (64), not the played 72.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	kit.dispatch(noteOn(1, 72, 40));
	drainLog(m);

	m->loadScript("");

	// onUnload releases on the script channel the note was played on (channel
	// 2 = internal 1).
	auto ev = kit.drain();
	REQUIRE(ev == std::vector<Out>{{0x8, 1, 64, 0, 0}});
}


// Behavioural tests for the Chord harmonizer preset. It reads no params:
// config.intervals ([0,4,7]) and harmonyVelocity (0.8) are hardcoded. A note
// expands into three voices (0-offset at full velocity, harmony voices
// scaled); the Note-Off releases exactly the started voices, and overlapping
// voices must not be released twice - reference-counting is the point of the
// script.


// [0,4,7] triad with 0.8 harmony velocity
TEST_CASE("'Chord harmonizer.js/.lua' expands a single note into a scaled triad", "[MidiKit][ChordHarmonizer]") {
	std::string path = GENERATE(presetPaths("Chord harmonizer"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// C4 -> C4 (0-offset, full velocity), E4 (+4, floor(100*0.8+0.5)=80),
	// G4 (+7, 80).
	auto on = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(on == std::vector<Out>{{0x9, 1, 60, 100, 0}, {0x9, 1, 64, 80, 0}, {0x9, 1, 67, 80, 0}});

	// The Note-Off releases exactly those three voices, once each.
	auto off = kit.dispatch(noteOff(1, 60));
	REQUIRE(off == std::vector<Out>{{0x8, 1, 60, 0, 0}, {0x8, 1, 64, 0, 0}, {0x8, 1, 67, 0, 0}});
}

// reference-counting: two notes transposing onto the same target
TEST_CASE("'Chord harmonizer.js/.lua' releases a colliding voice exactly once", "[MidiKit][ChordHarmonizer]") {
	std::string path = GENERATE(presetPaths("Chord harmonizer"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// C4's voices are {60,64,67}; A3's voices are {57,61,64}. Note 64 (E4) is
	// shared - C4's +4 and A3's +7 both land on it.
	kit.dispatch(noteOn(1, 60, 100));   // 60, 64, 67 down
	auto second = kit.dispatch(noteOn(1, 57, 100));
	// 64 is already sounding, so no second Note-On for it - only 57 and 61.
	REQUIRE(second == std::vector<Out>{{0x9, 1, 57, 100, 0}, {0x9, 1, 61, 80, 0}});

	// Releasing C4 drops 60 and 67 but must leave 64 down (A3 still holds it).
	auto release60 = kit.dispatch(noteOff(1, 60));
	REQUIRE(release60 == std::vector<Out>{{0x8, 1, 60, 0, 0}, {0x8, 1, 67, 0, 0}});

	// Releasing A3 finally lets 64 go - exactly once.
	auto release57 = kit.dispatch(noteOff(1, 57));
	REQUIRE(release57 == std::vector<Out>{{0x8, 1, 57, 0, 0}, {0x8, 1, 61, 0, 0}, {0x8, 1, 64, 0, 0}});
}

// onUnload releases every still-sounding voice
TEST_CASE("'Chord harmonizer.js/.lua' releases all sounding voices on unload", "[MidiKit][ChordHarmonizer]") {
	std::string path = GENERATE(presetPaths("Chord harmonizer"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	kit.dispatch(noteOn(1, 60, 100));   // 60, 64, 67 all held

	m->loadScript("");

	// refCount 60/64/67 all > 0 -> released ascending. The script's onUnload
	// hard-codes the release to MIDI channel 1 (internal channel 0) because
	// refCount isn't channel-indexed - so this is not the 1-based channel the
	// Note-On went out on, but the fixed first channel.
	auto ev = kit.drain();
	REQUIRE(ev == std::vector<Out>{{0x8, 0, 60, 0, 0}, {0x8, 0, 64, 0, 0}, {0x8, 0, 67, 0, 0}});
}


// Behavioural tests for the Scale quantiser preset. Shipped default: C minor
// ({0,2,3,5,7,8,10}, root C) with preferUpward=false. Every out-of-scale note
// sits exactly halfway between two minor degrees, so it always snaps down a
// semitone - the uniform tie-break worth pinning. The Note-Off rewrite (the
// release arrives with the played note) and the onUnload release of the
// substituted note are the parts most likely to regress.


TEST_CASE("'Scale quantiser.js/.lua' passes in-scale notes through unchanged", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(presetPaths("Scale quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// C minor degrees all pass through note-for-note. Feeding all seven in one
	// run also crosses the engine's GC boundary (the 6th consecutive
	// quantise() call), which used to surface as a dropped message with
	// "onMessage error: ERROR: parse error".
	for (int note : {60, 62, 63, 65, 67, 68, 70}) {
		auto ev = kit.dispatch(noteOn(1, note, 100));
		REQUIRE(ev == std::vector<Out>{{0x9, 1, static_cast<uint8_t>(note), 100, 0}});
	}
}

TEST_CASE("'Scale quantiser.js/.lua' snaps off-scale notes to the nearest degree", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(presetPaths("Scale quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// C# E F# A B are each equidistant between two minor-scale degrees; the
	// default preferUpward=false rounds them all down by a semitone.
	std::vector<std::pair<int, int>> cases = {{61, 60}, {64, 63}, {66, 65}, {69, 68}, {71, 70}};
	for (auto& c : cases) {
		auto ev = kit.dispatch(noteOn(1, c.first, 100));
		REQUIRE(ev == std::vector<Out>{{0x9, 1, static_cast<uint8_t>(c.second), 100, 0}});
	}
}

TEST_CASE("'Scale quantiser.js/.lua' rewrites the Note-Off to the snapped note", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(presetPaths("Scale quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// E4 snaps to D#4...
	auto on = kit.dispatch(noteOn(1, 64, 100));
	REQUIRE(on == std::vector<Out>{{0x9, 1, 63, 100, 0}});

	// ...so the Note-Off that arrives as 64 must be rewritten to release 63.
	auto off = kit.dispatch(noteOff(1, 64));
	REQUIRE(off == std::vector<Out>{{0x8, 1, 63, 0, 0}});
}

TEST_CASE("'Scale quantiser.js/.lua' releases the played note in either encoding, even after the root moved", "[MidiKit][ScaleQuantiser][Release]") {
	std::string path = GENERATE(presetPaths("Scale quantiser"));
	CATCH_INFO("preset: " << path);

	for (const midi::Message& release : releasesOf(1, 64)) {
		CATCH_INFO("release status: " << int(release.getStatus()));
		Kit<> kit;
		MidiKitModule* m = kit.loadPreset(path).m;

		// E4 snaps to D#4 under the default root.
		REQUIRE(kit.dispatch(noteOn(1, 64, 100)) == std::vector<Out>{{0x9, 1, 63, 100, 0}});

		// The root moves while the key is held: the release must still end what was
		// played (63), not what the key would snap to now.
		m->inputs[MidiKitModule::INPUT].setVoltage(0.5f, 0);
		auto ev = kit.dispatch(release);
		REQUIRE(ev.size() == 1);
		REQUIRE(ev[0].status == release.getStatus());
		REQUIRE(ev[0].note == 63);
	}
}

TEST_CASE("'Scale quantiser.js/.lua' releases the substituted note on unload", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(presetPaths("Scale quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	kit.dispatch(noteOn(1, 64, 100));   // played as 63, still held

	m->loadScript("");

	// onUnload must release the *substituted* note (63), not the raw 64 -
	// releasing 64 would leave a hanging voice. As with Chord harmonizer, the
	// release goes out on the fixed MIDI channel 1 (internal channel 0).
	auto ev = kit.drain();
	REQUIRE(ev == std::vector<Out>{{0x8, 0, 63, 0, 0}});
}

TEST_CASE("'Scale quantiser.js/.lua' reads the root from CV input 1", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(presetPaths("Scale quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// 0.5V on input 1 puts the root at F# (pitch class 6) under the standard
	// 1V/oct pitch convention: the C minor scale transposes up 6 semitones.
	// In-scale notes still pass through unchanged and off-scale notes still
	// snap down a semitone under the default preferUpward=false.
	m->inputs[MidiKitModule::INPUT].setVoltage(0.5f, 0);

	// C#5 (pitch class 1) is in the F# minor scale -> unchanged.
	auto inScale = kit.dispatch(noteOn(1, 73, 100));
	REQUIRE(inScale == std::vector<Out>{{0x9, 1, 73, 100, 0}});

	// C5 (pitch class 0) sits midway between B (11) and C# (1) -> snaps down
	// to B (71).
	auto offScale = kit.dispatch(noteOn(1, 72, 100));
	REQUIRE(offScale == std::vector<Out>{{0x9, 1, 71, 100, 0}});
}


TEST_CASE("'Scale quantiser.js/.lua' config survives a save/reload round-trip", "[MidiKit][ScaleQuantiser][JSON]") {
	std::string path = GENERATE(presetPaths("Scale quantiser"));
	CATCH_INFO("preset: " << path);

	// First module: load the preset and change two settings via the
	// right-click context menus, exactly as a user would.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(specs.size() == 3);
	REQUIRE(specs[1].label == "MIDI channel");
	REQUIRE(specs[2].label == "Round up on ties");

	// "Channel" option index 1 selects MIDI channel 2 (internal channel 1).
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 1);
	// Switch "Round up on ties" on.
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[2].callbackId, 1);
	drainLog(m);

	// Save: the context-menu onChange handlers call rack.setConfig()
	// themselves, so the published config already reflects the user's changes
	// before any save happens — dataToJson() is a plain read of the last
	// published value, unlike the old rack.onSave() design where the save
	// itself had to refresh the config by re-entering the script.
	// Module::onSave() is exercised too, to confirm it does not need to run
	// first for the read to be current.
	rack::engine::Module::SaveEvent saveEvent;
	m->onSave(saveEvent);
	json_t* rootJ = m->dataToJson();

	json_t* configJ = json_object_get(rootJ, "scriptConfig");
	REQUIRE(configJ != NULL);
	REQUIRE(json_is_object(configJ));
	REQUIRE(json_integer_value(json_object_get(configJ, "channel")) == 1);
	REQUIRE(json_is_true(json_object_get(configJ, "preferUpward")));

	// Second module: reload the patch and confirm the config came back.
	Kit<> kit2;
	MidiKitModule* m2 = kit2.m;
	m2->dataFromJson(rootJ);
	json_decref(rootJ);

	// The reloaded module's config must match what the user changed.
	std::string restored = publishedConfigJson(m2->host.getActiveEngine());
	REQUIRE(configInt(restored, "channel") == 1);
	REQUIRE(configBool(restored, "preferUpward") == true);

	// The menu presentation state is read lazily from onGetValue, so after a
	// reload the rebuilt menus reflect the restored config — the regression
	// this fix targets (checked/selected used to be captured at script load
	// time, before onLoad() restored the persisted config).
	std::vector<ScriptMenuItem> restoredSpecs = kit2.menus();
	REQUIRE(restoredSpecs.size() == 3);
	REQUIRE(restoredSpecs[1].label == "MIDI channel");
	REQUIRE(restoredSpecs[2].label == "Round up on ties");
	REQUIRE(restoredSpecs[1].selected == 1);
	REQUIRE(restoredSpecs[2].checked == true);
}


// Behavioural tests for the Micro scale preset. onLoad parses the Scala .scl
// pasted into config.scl. The shipped default is a 5-limit just-intonation
// major scale (9/8, 5/4, ...) on baseNote 60 @ A440, bendDepth 2, output
// channels 1-8, alwaysSendBend=false. Each note is retuned to the nearest
// 12-EDO note plus a pitch-wheel for the residual cents, dispatched
// round-robin to its own channel (pitch bend is channel-global). The exact
// note/cent split and wheel bytes are noted inline in each test.

// Loads one of the two Micro scale presets with the pasted .scl swapped for a
// caller-provided scale, so the parser is exercised with arbitrary content.
static MidiKitModule* loadPresetWithScl(Kit<>& kit, const std::string& relPath, const std::string& scl) {
	std::string script = readFile(repoRoot() + "/" + relPath);

	// Anchor on the assignment (Lua "scl = [[...]]", JS "scl: `...`") - a
	// generic search would also hit the literal "[[ ]]"/"`" in comments/logs.
	size_t contentStart = std::string::npos;
	size_t contentEnd = std::string::npos;
	size_t anchor = script.find("scl = [[");
	if (anchor != std::string::npos) {
		contentStart = anchor + 8;                    // just after "[["
		contentEnd = script.find("]]", contentStart);
	}
	else {
		anchor = script.find("scl: `");
		REQUIRE(anchor != std::string::npos);
		contentStart = anchor + 6;                    // just after the backtick
		contentEnd = script.find("`", contentStart);
	}
	REQUIRE(contentStart != std::string::npos);
	REQUIRE(contentEnd != std::string::npos);
	script.replace(contentStart, contentEnd - contentStart, scl);

	CATCH_INFO("preset: " << relPath);
	return kit.load(script).m;
}



TEST_CASE("'Micro scale.js/.lua' retunes a note and bends the residual cents", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// D4 (note 62) is a 5/4 above C4 in just intonation: it sounds as 64 with
	// a -0.13686 st bend. The pitch wheel is sent before the Note-On, because
	// the bend must be in place when the voice starts.
	auto ev = kit.dispatch(noteOn(1, 62, 100));
	REQUIRE(ev == std::vector<Out>{{0xe, 0, 79, 59, 0}, {0x9, 0, 64, 100, 0}});

	// Its Note-Off arrives as the *played* note 62 but must release the sent
	// note 64 on the same channel.
	auto off = kit.dispatch(noteOff(1, 62));
	REQUIRE(off == std::vector<Out>{{0x8, 0, 64, 0, 0}});
}

TEST_CASE("'Micro scale.js/.lua' dispatches simultaneous notes to separate channels", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// C#4 (note 61) is a 9/8 above C4: it sounds as 62 with a +0.03910 st bend.
	// Both notes are held at once, so they must land on different channels
	// (1 then 2) - pitch bend is channel-global, so sharing a channel would
	// corrupt the other voice's tuning.
	auto c = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(c == std::vector<Out>{{0x9, 0, 60, 100, 0}});

	auto cs = kit.dispatch(noteOn(1, 61, 100));
	REQUIRE(cs == std::vector<Out>{{0xe, 1, 32, 65, 0}, {0x9, 1, 62, 100, 0}});

	// Each Note-Off releases exactly its own channel and sent note.
	auto cOff = kit.dispatch(noteOff(1, 60));
	REQUIRE(cOff == std::vector<Out>{{0x8, 0, 60, 0, 0}});

	auto csOff = kit.dispatch(noteOff(1, 61));
	REQUIRE(csOff == std::vector<Out>{{0x8, 1, 62, 0, 0}});
}

TEST_CASE("'Micro scale.js/.lua' sends no redundant bend for the tonic and releases it on unload", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// The tonic (note 60) is exactly on the centre bend 8192, so with
	// alwaysSendBend=false no pitch wheel precedes the Note-On.
	auto ev = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0x9, 0, 60, 100, 0}});

	// onUnload releases the still-held note.
	m->loadScript("");

	auto unload = kit.drain();
	REQUIRE(unload == std::vector<Out>{{0x8, 0, 60, 0, 0}});
}

TEST_CASE("'Micro scale.js/.lua' parses a pasted equal-temperament scl", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	// A hand-written 12-tone equal temperament .scl, pasted exactly as a user
	// would copy it from the Scala archive: cents values carry a decimal
	// point (Scala convention), the octave is given as a ratio. The parser
	// skips the "!" comments and is indifferent to the stated note count.
	std::string scl =
		"! 12edo.scl\n"
		"!\n"
		"12-tone equal temperament\n"
		" 12\n"
		"!\n"
		" 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n"
		" 2/1\n";
	Kit<> kit;
	loadPresetWithScl(kit, path, scl);

	// In 12-EDO every scale degree is a whole semitone, so no note needs
	// retuning: D4 (note 62) passes through as 62 with no pitch wheel.
	auto ev = kit.dispatch(noteOn(1, 62, 100));
	REQUIRE(ev == std::vector<Out>{{0x9, 0, 62, 100, 0}});
}

TEST_CASE("'Micro scale.js/.lua' parses a mixed scl with ratios, cents, comments and dropped entries", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	// A convoluted .scl exercising every parser rule at once: ratio and cents
	// notes mixed, comments at the top and between notes, a stated count that
	// doesn't match the lines (ignored), and entries that must be dropped -
	// the tonic "1/1", the octave "2/1"/"1200.0" (implicit), and the bare
	// integer "3" (a ratio outside the octave). The surviving 8 degrees are
	// 9/8, 193.0, 5/4, 4/3, 3/2, 5/3, 15/8.
	std::string scl =
		"! Mixed scale.scl\n"
		"!\n"
		"Ratios, cents and comments in one scale\n"
		" 8\n"
		"!\n"
		" 1/1\n"
		" 9/8\n"
		"! a mid-file comment must be skipped too\n"
		" 193.0\n"
		" 5/4\n"
		" 4/3\n"
		" 3/2\n"
		" 5/3\n"
		" 15/8\n"
		" 2/1\n"
		" 1200.0\n"
		" 3\n";
	Kit<> kit;
	loadPresetWithScl(kit, path, scl);

	// C#4 (note 61) lands on degree 1 = 9/8 (ratio branch): sent as 62 with a
	// +0.03910 st bend.
	auto cs = kit.dispatch(noteOn(1, 61, 100));
	REQUIRE(cs == std::vector<Out>{{0xe, 0, 32, 65, 0}, {0x9, 0, 62, 100, 0}});

	// D4 (note 62) lands on degree 2 = 193.0 cents (cents branch): sent as 62
	// with a -0.07 st bend -> wheel 7905 (LSB 97, MSB 61). Both held notes
	// land on separate channels (1 then 2).
	auto d = kit.dispatch(noteOn(1, 62, 100));
	REQUIRE(d == std::vector<Out>{{0xe, 1, 97, 61, 0}, {0x9, 1, 62, 100, 0}});

	// G#4 (note 68) is a full scale-octave up, wrapping to degree 0 of the next
	// octave - the tonic one octave higher, passing through as 72. Had the
	// implicit "2/1"/"1200.0" octave entries not been dropped, the indexing
	// would shift and this note wouldn't come out clean.
	auto gs = kit.dispatch(noteOn(1, 68, 100));
	REQUIRE(gs == std::vector<Out>{{0x9, 2, 72, 100, 0}});

	// Each Note-Off releases its own channel and sent note.
	auto csOff = kit.dispatch(noteOff(1, 61));
	REQUIRE(csOff == std::vector<Out>{{0x8, 0, 62, 0, 0}});
	auto dOff = kit.dispatch(noteOff(1, 62));
	REQUIRE(dOff == std::vector<Out>{{0x8, 1, 62, 0, 0}});
	auto gsOff = kit.dispatch(noteOff(1, 68));
	REQUIRE(gsOff == std::vector<Out>{{0x8, 2, 72, 0, 0}});
}

// A unison (the same note played twice) must release in press order, one
// voice per Note-Off - the queueOfNote FIFO is the part of the script most
// likely to regress. Round-robin sends the first voice to channel 1 and the
// second to channel 2; the two Note-Offs must then release channel 1 first and
// channel 2 second, not the other way round and not both at once.
TEST_CASE("'Micro scale.js/.lua' releases a unison's voices in press order", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// The tonic (note 60) has no residual bend, so each voice is a single
	// clean Note-On - the test focuses purely on the FIFO, not on tuning.
	auto first = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(first == std::vector<Out>{{0x9, 0, 60, 100, 0}});
	auto second = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(second == std::vector<Out>{{0x9, 1, 60, 100, 0}});

	// The two Note-Offs release in press order: channel 1 first, then channel
	// 2. A LIFO or a single combined release would break this.
	auto off1 = kit.dispatch(noteOff(1, 60));
	REQUIRE(off1 == std::vector<Out>{{0x8, 0, 60, 0, 0}});
	auto off2 = kit.dispatch(noteOff(1, 60));
	REQUIRE(off2 == std::vector<Out>{{0x8, 1, 60, 0, 0}});
}

// Voice stealing with the default 8 output channels: when every channel
// is busy the 9th note displaces the round-robin next channel, and the
// displaced note's later Note-Off must be dropped so it can't release the
// thief. An equal-temperament scale makes every note pass through unchanged,
// so the test observes channel allocation alone.
TEST_CASE("'Micro scale.js/.lua' steals a busy channel and drops the displaced note", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	// 12-EDO: every scale degree is exactly a semitone, so all notes pass
	// through with no pitch bend - allocation is the only variable.
	std::string scl =
		"! 12edo.scl\n"
		"!\n"
		"12-tone equal temperament\n"
		" 12\n"
		"!\n"
		" 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n"
		" 2/1\n";
	Kit<> kit;
	loadPresetWithScl(kit, path, scl);

	// 8 notes fill the 8 default channels in round-robin order (1..8).
	for (int n = 0; n < 8; n++) {
		auto ev = kit.dispatch(noteOn(1, 60 + n, 100));
		REQUIRE(ev == std::vector<Out>{{0x9, static_cast<uint8_t>(n), static_cast<uint8_t>(60 + n), 100, 0}});
	}

	// The 9th note finds every channel busy, so it steals the next round-robin
	// channel - channel 1 (internal 0) - displacing note 60.
	auto steal = kit.dispatch(noteOn(1, 68, 100));
	REQUIRE(steal == std::vector<Out>{{0x9, 0, 68, 100, 0}});

	// The displaced note's Note-Off arrives later and must be dropped: its
	// queue entry was removed at steal time, so releasing it would kill the
	// thief's voice.
	REQUIRE(kit.dispatch(noteOff(1, 60)).empty());

	// The thief's own Note-Off releases channel 1.
	auto rel = kit.dispatch(noteOff(1, 68));
	REQUIRE(rel == std::vector<Out>{{0x8, 0, 68, 0, 0}});

	// Channel 1 is free again; the next note lands there (round-robin wraps
	// back to the freed channel rather than skipping it).
	auto again = kit.dispatch(noteOn(1, 69, 100));
	REQUIRE(again == std::vector<Out>{{0x9, 0, 69, 100, 0}});
}

// The "Always send pitch bend" context-menu option: off (default) the
// tonic sits on the centre bend and no wheel is emitted; on, the centre bend
// is sent anyway. This is the script's only alwaysSendBend code path and
// nothing else in the suite exercises it.
TEST_CASE("'Micro scale.js/.lua' alwaysSendBend forces a bend even for the tonic", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(specs.size() == 4);
	REQUIRE(specs[0].label == "MIDI channel (Input)");
	REQUIRE(specs[1].label == "Always send pitch bend");
	REQUIRE(specs[2].label == "Load scale (.scl)...");
	REQUIRE(specs[3].label == "Default scale");

	// Switch the option on; the next tonic Note-On must be preceded by the
	// centre bend 8192 (LSB 0, MSB 64) even though it is unchanged.
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 1);
	drainLog(m);

	auto ev = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0xe, 0, 0, 64, 0}, {0x9, 0, 60, 100, 0}});
	kit.dispatch(noteOff(1, 60));

	// Switch it back off: the receiver still remembers the last bend per
	// channel, so the next tonic (on a fresh round-robin channel) goes out
	// bend-free again.
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 0);
	drainLog(m);

	auto again = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(again == std::vector<Out>{{0x9, 1, 60, 100, 0}});
}

// The "Input channel" context-menu option filters which input channel is
// retuned. Notes on the chosen channel are retuned; notes on other channels
// pass through untouched and untracked (their Note-Offs pass as the raw
// note). Non-note messages pass through on all channels.
TEST_CASE("'Micro scale.js/.lua' input-channel filter retunes only the chosen channel", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	std::vector<ScriptMenuItem> specs = kit.menus();
	// "Input channel" option index 1 selects script channel 1. The script's
	// channels are 1-based (midi.getChannel returns the Rack nibble + 1), so
	// the matching note is fed as noteOn(0, ...) and the non-matching one as
	// noteOn(1, ...).
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[0].callbackId, 1);
	drainLog(m);

	// Channel 1 is still retuned exactly as before.
	auto in = kit.dispatch(noteOn(0, 62, 100));
	REQUIRE(in == std::vector<Out>{{0xe, 0, 79, 59, 0}, {0x9, 0, 64, 100, 0}});

	// Channel 2 passes through untouched - not retuned, not tracked.
	auto out = kit.dispatch(noteOn(1, 62, 100));
	REQUIRE(out == std::vector<Out>{{0x9, 1, 62, 100, 0}});

	// Non-note messages pass through on both the matching and non-matching
	// channels.
	auto ccIn = kit.dispatch(cc(0, 20, 100));
	REQUIRE(ccIn == std::vector<Out>{{0xb, 0, 20, 100, 0}});
	auto ccOut = kit.dispatch(cc(1, 20, 100));
	REQUIRE(ccOut == std::vector<Out>{{0xb, 1, 20, 100, 0}});

	// The channel-1 Note-Off releases the retuned note 64; the channel-2
	// Note-Off is a passthrough of the raw note 62.
	auto offIn = kit.dispatch(noteOff(0, 62));
	REQUIRE(offIn == std::vector<Out>{{0x8, 0, 64, 0, 0}});
	auto offOut = kit.dispatch(noteOff(1, 62));
	REQUIRE(offOut == std::vector<Out>{{0x8, 1, 62, 0, 0}});
}


// Behavioural tests for the Note length quantiser preset. Shipped default:
// config.lengthTicks=12 counted on trigger input 1. Every Note-On is
// re-articulated immediately and a Note-Off scheduled exactly 12 ticks later
// via midiOut.sendAfterTrigger(); the incoming Note-Off is discarded.
// triggerTick only advances on a real trigger edge inside
// Module::process(), which these engine-level tests do not run, so the tests
// write the counter directly - also proving the scheduled tick is *relative*
// to the note-on's tick count, not a fixed absolute tick.


TEST_CASE("'Note length quantiser.js/.lua' schedules the Note-Off lengthTicks after the Note-On", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(presetPaths("Note length quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->triggerIns.triggerTick[0][0] = 40;

	// The note's old scheduled Note-Off is cancelled first (a no-op here: there
	// is none), then the Note-On passes through and its Note-Off is scheduled at
	// 40 + 12.
	auto ev = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0x8, 1, 60, 0, 0, true}, {0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 52}});
}

TEST_CASE("'Note length quantiser.js/.lua' drops the incoming Note-Off", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(presetPaths("Note length quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.dispatch(noteOn(1, 60, 100));

	// The player's own release is discarded - the scheduled one ends the note.
	auto ev = kit.dispatch(noteOff(1, 60));
	REQUIRE(ev.empty());
}

TEST_CASE("'Note length quantiser.js/.lua' drops the release in either encoding", "[MidiKit][NoteLength][Release]") {
	std::string path = GENERATE(presetPaths("Note length quantiser"));
	CATCH_INFO("preset: " << path);

	for (const midi::Message& release : releasesOf(1, 60)) {
		CATCH_INFO("release status: " << int(release.getStatus()));
		Kit<> kit;
		MidiKitModule* m = kit.loadPreset(path).m;
		m->triggerIns.triggerTick[0][0] = 40;
		kit.dispatch(noteOn(1, 60, 100));

		// Neither encoding cuts the note, schedules a Note-Off or reaches the output.
		REQUIRE(kit.dispatch(release).empty());
	}
}

TEST_CASE("'Note length quantiser.js/.lua' cuts a retriggered note before re-articulating", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(presetPaths("Note length quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->triggerIns.triggerTick[0][0] = 40;
	kit.dispatch(noteOn(1, 60, 100));   // drains [on, off@52]; sounding[60] stays true

	// Retriggering 60 while it's still sounding cancels the old scheduled
	// Note-Off (the cancel entry carries its address), cuts the old note
	// immediately (Note-Off, tick 0), then sends the fresh Note-On and its
	// scheduled Note-Off. The engine queues in call order: cancel, cut, Note-On,
	// scheduled Note-Off.
	auto ev = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0x8, 1, 60, 0, 0, true}, {0x8, 1, 60, 0, 0}, {0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 52}});
}

TEST_CASE("'Note length quantiser.js/.lua' a retriggered note keeps its full new length", "[MidiKit][NoteLength]") {
	// The regression: the old note's scheduled Note-Off used to stay queued and
	// fire at its own tick, cutting the retriggered note short. Runs the real
	// process() with trigger edges, so the tick queues really release.
	std::string path = GENERATE(presetPaths("Note length quantiser"));
	// The player lets go of the key before retriggering: the usual way to play a
	// fixed length, and the case where the preset's own "sounding" bookkeeping
	// used to hide the pending Note-Off from the retrigger.
	bool released = GENERATE(false, true);
	CATCH_INFO("preset: " << path << ", released before retrigger: " << released);

	DeviceKit<> kit;
	Device& rec = kit.dev[0];
	MidiKitModule* m = kit.m;
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	m->loadScript(readFile(repoRoot() + "/" + path));

	int64_t frame = 0;
	auto step = [&]() { m->process(Test::makeProcessArgs(frame++)); };
	auto run = [&](int n) { for (int i = 0; i < n; i++) step(); };
	auto play = [&](int note) {
		midi::Message msg = noteOn(1, note, 100);
		msg.frame = frame;
		m->midiIns.ports[0].processor.getInput().onMessage(msg);
		run(16);   // past the callback and the next drain
	};
	auto edges = [&](int n) {
		for (int i = 0; i < n; i++) {
			m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
			step();
			m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
			step();
		}
		run(16);
	};

	run(16);          // loaded, trigger input primed low
	REQUIRE(rec.sent.empty());

	play(60);         // tick 0: Note-Off scheduled for tick 12
	edges(2);
	if (released) {
		midi::Message up = noteOff(1, 60);
		up.frame = frame;
		m->midiIns.ports[0].processor.getInput().onMessage(up);
		run(16);      // the preset drops it; the scheduled one still ends the note
		REQUIRE(rec.count(0x8) == 0);
	}
	edges(3);
	play(60);         // tick 5: cut, re-articulation, Note-Off scheduled for tick 17
	REQUIRE(rec.count(0x9) == 2);
	REQUIRE(rec.count(0x8) == 1);   // the cut

	edges(7);         // tick 12: where the first note's release used to fire
	REQUIRE(rec.count(0x8) == 1);

	edges(5);         // tick 17
	REQUIRE(rec.count(0x8) == 2);
}

TEST_CASE("'Note length quantiser.js/.lua' releases the sounding note on unload", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(presetPaths("Note length quantiser"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	kit.dispatch(noteOn(1, 60, 100));   // scheduled Note-Off not yet due

	m->loadScript("");

	// onUnload releases the still-sounding note on the fixed MIDI channel 1
	// (internal channel 0), same best-effort choice as the other presets.
	auto ev = kit.drain();
	REQUIRE(ev == std::vector<Out>{{0x8, 0, 60, 0, 0}});
}


// Behavioural tests for the Clock divider preset. Shipped default
// config.divisor=6 with passthrough of non-clock messages. MIDI clock (0xF8)
// arrives on the MIDI input, so these tests feed it via dispatch() and
// assert on the realtime messages that come back: the division (every 6th
// tick), the Start phase reset, and passthrough. (The CV trigger output is
// not asserted - it only surfaces through Module::process(), which these
// engine-level tests do not run.)


TEST_CASE("'Clock divider.js/.lua' forwards only every divisor-th tick", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(presetPaths("Clock divider"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Realtime messages decode to status nibble 0xf. The first five clock
	// ticks are swallowed...
	for (int i = 0; i < 5; i++) {
		auto ev = kit.dispatch(clockTick());
		bool forwarded = false;
		for (auto& e : ev) if (e.status == 0xf) forwarded = true;
		REQUIRE_FALSE(forwarded);
	}

	// ...and only the sixth is forwarded.
	auto ev = kit.dispatch(clockTick());
	int forwarded = 0;
	for (auto& e : ev) if (e.status == 0xf) forwarded++;
	REQUIRE(forwarded == 1);
}

TEST_CASE("'Clock divider.js/.lua' resets the phase on Start", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(presetPaths("Clock divider"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// 3 ticks before Start are swallowed, leaving tickCount = 3.
	for (int i = 0; i < 3; i++) kit.dispatch(clockTick());

	// Start itself is forwarded and resets the count back to 0.
	auto start = kit.dispatch(startMsg());
	bool startFwd = false;
	for (auto& e : start) if (e.status == 0xf) startFwd = true;
	REQUIRE(startFwd);

	// Without the reset the next tick would already reach the divisor; with
	// it, the phase restarts and the 6th tick after Start is the first one out.
	for (int i = 0; i < 5; i++) {
		auto ev = kit.dispatch(clockTick());
		bool fwd = false;
		for (auto& e : ev) if (e.status == 0xf) fwd = true;
		REQUIRE_FALSE(fwd);
	}
	auto ev = kit.dispatch(clockTick());
	int fwd = 0;
	for (auto& e : ev) if (e.status == 0xf) fwd++;
	REQUIRE(fwd == 1);
}

TEST_CASE("'Clock divider.js/.lua' forwards Stop and does not reset the phase", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(presetPaths("Clock divider"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// 3 ticks are swallowed, leaving tickCount = 3 (3 < divisor 6).
	for (int i = 0; i < 3; i++) kit.dispatch(clockTick());

	// Stop is always forwarded untouched - it is a realtime message handled
	// before the passThroughOther check, so it goes out even mid-count.
	auto stop = kit.dispatch(stopMsg());
	bool stopFwd = false;
	for (auto& e : stop) if (e.status == 0xf) stopFwd = true;
	REQUIRE(stopFwd);

	// Unlike Start, Stop does not reset the phase: the next forwarded tick is
	// still the one that completes the count from before the Stop (3 + 3 = 6),
	// not 6 ticks after the Stop.
	for (int i = 0; i < 2; i++) {
		auto ev = kit.dispatch(clockTick());
		bool fwd = false;
		for (auto& e : ev) if (e.status == 0xf) fwd = true;
		REQUIRE_FALSE(fwd);
	}
	auto ev = kit.dispatch(clockTick());
	int fwd = 0;
	for (auto& e : ev) if (e.status == 0xf) fwd++;
	REQUIRE(fwd == 1);
}

TEST_CASE("'Clock divider.js/.lua' passes non-clock messages through unchanged", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(presetPaths("Clock divider"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// passThroughOther: a CC is forwarded untouched - only 0xF8 is thinned.
	auto ev = kit.dispatch(cc(1, 20, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 1, 20, 100, 0}});
}

// Behavioural tests for the Clock multiplier preset. Unlike the divider, it is
// clocked by the trigger input and enables sample-accurate timing, so these run
// the module's process() sample by sample and read what reaches the output
// device: the frame each clock pulse carries.

// Steps the module from frame `from` to `until`, driving trigger input 1 with a
// one-frame pulse on each of `edges`.
static void driveClockEdges(MidiKitModule* m, const std::vector<int64_t>& edges, int64_t from, int64_t until) {
	size_t next = 0;
	for (int64_t f = from; f < until; f++) {
		bool high = next < edges.size() && edges[next] == f;
		if (high) next++;
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(high ? 10.f : 0.f);
		m->process(Test::makeProcessArgs(f));
	}
}

TEST_CASE("'Clock multiplier.js/.lua' spaces its pulses over the previous period", "[MidiKit][ClockMultiplier]") {
	std::string path = GENERATE(presetPaths("Clock multiplier"));
	CATCH_INFO("preset: " << path);

	DeviceKit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	Device& rec = kit.dev[0];
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Starts past 2^24, where a float would no longer hold every frame exactly.
	const int64_t base = (int64_t(1) << 25) + 1;

	// Default multiplier 24, input period 480: three edges.
	driveClockEdges(m, { base + 100, base + 580, base + 1060 }, base, base + 1600);

	// The first edge sends its own pulse only, the period is not known yet. Each
	// later edge sends one on its own frame and 23 more, 20 frames apart.
	std::vector<int64_t> expected = { base + 100 };
	for (int64_t edge : { base + 580, base + 1060 }) {
		expected.push_back(edge);
		for (int k = 1; k < 24; k++) expected.push_back(edge + k * 20);
	}
	REQUIRE(rec.frames == expected);
	for (int status : rec.statuses) REQUIRE(status == 0xf);
}

TEST_CASE("'Clock multiplier.js/.lua' treats a long gap as a restart", "[MidiKit][ClockMultiplier]") {
	std::string path = GENERATE(presetPaths("Clock multiplier"));
	CATCH_INFO("preset: " << path);

	DeviceKit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	Device& rec = kit.dev[0];
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// The third edge is 8 periods after the second: the clock was stopped.
	driveClockEdges(m, { 100, 580, 4420, 4900 }, 0, 6000);

	// Only the pulse for the restart edge itself follows it, none of the pulses a
	// slow tempo would have scattered over the next 3800 frames.
	auto at = std::find(rec.frames.begin(), rec.frames.end(), 4420);
	REQUIRE(at != rec.frames.end());
	REQUIRE(*(at + 1) == 4900);

	// The edge after it measures the new period (480) and subdivides again.
	for (int k = 1; k < 24; k++) {
		REQUIRE(std::find(rec.frames.begin(), rec.frames.end(), 4900 + k * 20) != rec.frames.end());
	}
}


// Behavioural tests for the MPE to single channel preset. Shipped default is
// a Lower Zone: channel 1 is the master (passes through), channels 2-16 are
// members folded onto config.outChannel=1. Per-note pitch bend is quantised
// to semitones and folded into the note number (a semitone crossing
// re-articulates), and channel pressure / CC 74 are forwarded only for the
// member channel holding the most recently played note. None of this is
// exercised by the generic smoke check.


TEST_CASE("'MPE to single channel.js/.lua' rewrites member-channel notes to the output channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(presetPaths("MPE to single channel"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Note-On on member channel 2 is recreated on outChannel 1 (internal 0).
	auto on = kit.dispatch(noteOn(2, 60, 100));
	REQUIRE(on == std::vector<Out>{{0x9, 0, 60, 100, 0}});

	// The Note-Off releases the same folded note on the output channel.
	auto off = kit.dispatch(noteOff(2, 60));
	REQUIRE(off == std::vector<Out>{{0x8, 0, 60, 0, 0}});
}

TEST_CASE("'MPE to single channel.js/.lua' releases a member note in either encoding", "[MidiKit][MPE][Release]") {
	std::string path = GENERATE(presetPaths("MPE to single channel"));
	CATCH_INFO("preset: " << path);

	for (const midi::Message& release : releasesOf(2, 60)) {
		CATCH_INFO("release status: " << int(release.getStatus()));
		Kit<> kit;
		kit.loadPreset(path);
		kit.dispatch(noteOn(2, 60, 100));

		// A release, not a new note: a Note-Off on the output channel.
		REQUIRE(kit.dispatch(release) == std::vector<Out>{{0x8, 0, 60, 0, 0}});

		// The note is gone: a later bend on that channel must not play it again.
		auto bend = kit.dispatch(pitchWheel(2, 8320));
		for (const Out& e : bend) REQUIRE(e.status != 0x9);
	}
}

TEST_CASE("'MPE to single channel.js/.lua' passes the master channel through untouched", "[MidiKit][MPE]") {
	std::string path = GENERATE(presetPaths("MPE to single channel"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Channel 1 (internal 0) is the master channel - not a member channel - so
	// it is sent through as-is on its own channel (internal 0), not folded.
	auto on = kit.dispatch(noteOn(0, 60, 100));
	REQUIRE(on == std::vector<Out>{{0x9, 0, 60, 100, 0}});
}

TEST_CASE("'MPE to single channel.js/.lua' folds a semitone pitch bend into the note number", "[MidiKit][MPE]") {
	std::string path = GENERATE(presetPaths("MPE to single channel"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.dispatch(noteOn(2, 60, 100));   // 60 sounding on member channel 2

	// A bend of +0.75 semitones (pitch wheel 8320, centre 8192, range 48)
	// rounds to +1 step, so the receiver re-articulates 60 as 61: release the
	// old note, play the new one at the script's fixed velocity 100.
	auto ev = kit.dispatch(pitchWheel(2, 8320));
	REQUIRE(ev == std::vector<Out>{{0x8, 0, 60, 0, 0}, {0x9, 0, 61, 100, 0}});
}

TEST_CASE("'MPE to single channel.js/.lua' forwards channel pressure only for the active channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(presetPaths("MPE to single channel"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Channel 2 is the most recently played member channel, so its pressure
	// is forwarded on the output channel.
	kit.dispatch(noteOn(2, 60, 100));
	auto p1 = kit.dispatch(chanPressure(2, 50));
	REQUIRE(p1 == std::vector<Out>{{0xd, 0, 50, 0, 0}});

	// Playing a note on channel 3 makes it the active channel; pressure on the
	// now-inactive channel 2 is dropped, while channel 3's is forwarded.
	kit.dispatch(noteOn(3, 64, 100));
	REQUIRE(kit.dispatch(chanPressure(2, 60)).empty());
	auto p3 = kit.dispatch(chanPressure(3, 70));
	REQUIRE(p3 == std::vector<Out>{{0xd, 0, 70, 0, 0}});
}

TEST_CASE("'MPE to single channel.js/.lua' forwards CC 74 only for the active channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(presetPaths("MPE to single channel"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	kit.dispatch(noteOn(2, 60, 100));   // active channel 2
	auto c1 = kit.dispatch(cc(2, 74, 40));
	REQUIRE(c1 == std::vector<Out>{{0xb, 0, 74, 40, 0}});

	// Channel 3 becomes active; CC 74 on channel 2 is dropped, on 3 forwarded.
	kit.dispatch(noteOn(3, 64, 100));
	REQUIRE(kit.dispatch(cc(2, 74, 30)).empty());
	auto c3 = kit.dispatch(cc(3, 74, 60));
	REQUIRE(c3 == std::vector<Out>{{0xb, 0, 74, 60, 0}});
}

TEST_CASE("'MPE to single channel.js/.lua' forwards other CCs on a member channel to the output channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(presetPaths("MPE to single channel"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// A non-74 CC on a member channel is forwarded on the output channel.
	auto ev = kit.dispatch(cc(2, 20, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 0, 20, 100, 0}});
}


// Behavioural tests for the Velocity curve preset. Shipped default
// config.minVelocity=1, maxVelocity=127, curveAmount=2, channel=0, with the
// curve shape read live from panel param 1. At knob 0.5 the curve is linear
// (pass-through); velocity 0 (running-status Note-Off) is always left alone;
// non-note messages pass through untouched.


TEST_CASE("'Velocity curve.js/.lua' passes velocity through unchanged at the linear knob", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(presetPaths("Velocity curve"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	// knob 0.5 -> curve 0 -> identity mapping.
	m->params[MidiKitModule::PARAM + 0].setValue(0.5f);

	// The script passes the message through on its own channel (internal 1).
	auto ev = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0x9, 1, 60, 100, 0}});
}

TEST_CASE("'Velocity curve.js/.lua' leaves velocity 0 untouched even off-linear", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(presetPaths("Velocity curve"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	// A strongly non-linear knob would reshape any real velocity, but a
	// Note-On with velocity 0 is a Note-Off in disguise and must pass through.
	m->params[MidiKitModule::PARAM + 0].setValue(1.0f);

	auto ev = kit.dispatch(noteOn(1, 60, 0));
	REQUIRE(ev == std::vector<Out>{{0x9, 1, 60, 0, 0}});
}

TEST_CASE("'Velocity curve.js/.lua' passes non-note messages through unchanged", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(presetPaths("Velocity curve"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	auto ev = kit.dispatch(cc(1, 20, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 1, 20, 100, 0}});
}

// The knob extremes are the whole point of the script: curveAmount=2 turns
// knob 0.0 into curve +2 (exponential) and knob 1.0 into curve -2
// (logarithmic), the only non-linear settings number.rescale() ever sees at
// runtime. Both engines run Rack's rack::math::rescale() with a
// dsp::exp2_taylor5() exponent. A POSITIVE curve squashes soft notes toward
// the floor; a NEGATIVE one lifts light touches toward the ceiling. (The
// curve sign was inverted in the presets until 2026-08-03.) Expected values
// are exact, computed from those same functions.
static int shapedVelocity(Kit<>& kit, int vel) {
	auto ev = kit.dispatch(noteOn(1, 60, vel));
	REQUIRE_FALSE(ev.empty());
	return ev[0].value;
}

TEST_CASE("'Velocity curve.js/.lua' exponential knob (0.0) reshapes velocities", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(presetPaths("Velocity curve"));
	CATCH_INFO("preset: " << path);

	// knob 0.0 -> curve +2: soft notes are squashed toward the floor - only
	// hard hits open up the range - and the endpoints stay pinned at 1 and 127.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.0f);

	std::vector<std::pair<int, int>> cases = {{1, 1}, {2, 1}, {8, 1}, {32, 2}, {64, 13}, {96, 46}, {120, 102}, {127, 127}};
	for (auto& c : cases) {
		REQUIRE(shapedVelocity(kit, c.first) == c.second);
	}
}

TEST_CASE("'Velocity curve.js/.lua' logarithmic knob (1.0) reshapes velocities", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(presetPaths("Velocity curve"));
	CATCH_INFO("preset: " << path);

	// knob 1.0 -> curve -2: light touches are lifted sharply toward the
	// ceiling (useful for stiff keybeds), and the endpoints stay pinned.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(1.0f);

	std::vector<std::pair<int, int>> cases = {{1, 1}, {2, 31}, {8, 55}, {32, 86}, {64, 106}, {96, 118}, {120, 125}, {127, 127}};
	for (auto& c : cases) {
		REQUIRE(shapedVelocity(kit, c.first) == c.second);
	}
}

TEST_CASE("'Velocity curve.js/.lua' keeps every output within the 1..127 window", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(presetPaths("Velocity curve"));
	CATCH_INFO("preset: " << path);

	// rescale() works in floats and can land a hair outside the configured
	// window, so the script clamps to [minVelocity, maxVelocity] = [1, 127].
	// Sweep the whole input range at both knob extremes: a valid output byte
	// must never drop to 0 (would read as a Note-Off) nor exceed 127.
	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	for (float knob : {0.0f, 1.0f}) {
		m->params[MidiKitModule::PARAM + 0].setValue(knob);
		for (int vel = 1; vel <= 127; vel++) {
			int out = shapedVelocity(kit, vel);
			REQUIRE(out >= 1);
			REQUIRE(out <= 127);
		}
	}
}


// Behavioural tests for the NRPN to CC preset. Shipped config.map maps NRPN
// 0->CC 0, 1->CC 1, 2->CC 2 on ccChannel 1. A full NRPN write is four CCs
// (99/98 number MSB/LSB, 6/38 value MSB/LSB); once all four arrive the 14-bit
// value is emitted as a 14-bit CC pair (CC n = MSB, CC n+32 = LSB). Unmapped
// numbers are ignored, and nothing is emitted until all four bytes arrive.


TEST_CASE("'NRPN to CC.js/.lua' converts a mapped NRPN to a 14-bit CC pair", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// NRPN 1 (number MSB 0, LSB 1) with value 8192 (value MSB 64, LSB 0).
	// Mapped to CC 1: CC 1 = MSB 64, CC 33 = LSB 0, both on channel 1 (int 0).
	kit.dispatch(cc(1, 99, 0));
	kit.dispatch(cc(1, 98, 1));
	kit.dispatch(cc(1, 6, 64));
	auto ev = kit.dispatch(cc(1, 38, 0));
	REQUIRE(ev == std::vector<Out>{{0xb, 0, 1, 64, 0}, {0xb, 0, 33, 0, 0}});
}

TEST_CASE("'NRPN to CC.js/.lua' ignores unmapped NRPN numbers", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// NRPN 5 is not in config.map, so the complete write produces nothing.
	kit.dispatch(cc(1, 99, 0));
	kit.dispatch(cc(1, 98, 5));
	kit.dispatch(cc(1, 6, 64));
	auto ev = kit.dispatch(cc(1, 38, 0));
	REQUIRE(ev.empty());
}

TEST_CASE("'NRPN to CC.js/.lua' emits nothing until all four bytes arrive", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Only three of the four bytes - the value LSB is missing.
	kit.dispatch(cc(1, 99, 0));
	kit.dispatch(cc(1, 98, 1));
	auto ev = kit.dispatch(cc(1, 6, 64));
	REQUIRE(ev.empty());
}

// The script speaks only NRPN: non-CC messages hit an early return and
// non-NRPN CC numbers fall through to an else/return, so a plain CC is
// silently swallowed. That is the intended design; these tests pin it and
// prove the swallowed messages never corrupt the NRPN state machine.
TEST_CASE("'NRPN to CC.js/.lua' drops non-NRPN CCs and non-CC messages silently", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Regular CCs fall through to the else/return branch and are swallowed.
	auto cc7 = kit.dispatch(cc(1, 7, 100));
	REQUIRE(cc7.empty());
	auto cc10 = kit.dispatch(cc(1, 10, 64));
	REQUIRE(cc10.empty());

	// Non-CC messages (a Note-On here) hit the early return and are dropped too.
	auto note = kit.dispatch(noteOn(1, 60, 100));
	REQUIRE(note.empty());

	// None of the dropped messages touched the NRPN state machine: a complete
	// NRPN write that follows still converts to its CC pair as usual.
	kit.dispatch(cc(1, 99, 0));
	kit.dispatch(cc(1, 98, 1));
	kit.dispatch(cc(1, 6, 64));
	auto ev = kit.dispatch(cc(1, 38, 0));
	REQUIRE(ev == std::vector<Out>{{0xb, 0, 1, 64, 0}, {0xb, 0, 33, 0, 0}});
}


// Behavioural tests for the "NRPN to CC (assembled)" preset — the same
// converter as above, but via MIDI-KIT's assembled-input API
// (midi.enableNrpnIn + midi.onNrpn) instead of the hand-rolled state machine.
// Assembly happens in the module's MidiProcessor, so these tests feed through
// it (dispatchDecoded) rather than the engine-direct dispatch the manual
// version uses.
TEST_CASE("'NRPN to CC (assembled).js/.lua' converts a mapped NRPN to a 14-bit CC pair", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC (assembled)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// NRPN 1 (number MSB 0, LSB 1) with value 8192 (value MSB 64, LSB 0).
	// Mapped to CC 1: CC 1 = MSB 64, CC 33 = LSB 0, both on channel 1 (int 0).
	feedDecoded(m, cc(1, 99, 0));
	feedDecoded(m, cc(1, 98, 1));
	feedDecoded(m, cc(1, 6, 64));
	auto ev = kit.dispatchDecoded(cc(1, 38, 0));
	REQUIRE(ev == std::vector<Out>{{0xb, 0, 1, 64, 0}, {0xb, 0, 33, 0, 0}});
}

TEST_CASE("'NRPN to CC (assembled).js/.lua' switches the data entry mode from its menu", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC (assembled)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	auto& ext = m->midiIns.ports[0].extendedCc;

	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(specs.size() == 2);
	REQUIRE(specs[0].label == "MIDI channel (CC)");
	REQUIRE(specs[1].label == "Device sends 7-bit NRPN");

	// Off by default: NRPN is enabled in "lsb" mode, on every channel.
	REQUIRE(m->midiIns.isNrpnEnabled(0, false));
	REQUIRE(ext.msbDataEntryNrpnMask.load() == 0);

	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 1);
	drainLog(m);
	REQUIRE(m->midiIns.isNrpnEnabled(0, false));
	REQUIRE(ext.msbDataEntryNrpnMask.load() == 0xFFFF);

	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 0);
	drainLog(m);
	REQUIRE(ext.msbDataEntryNrpnMask.load() == 0);
}

// End to end through the input stage, for both settings of the menu item. NRPN 1
// (MSB 0, LSB 1) is mapped to CC 1; the value 8192 is CC 6 = 64, CC 38 = 0.
TEST_CASE("'NRPN to CC (assembled).js/.lua' converts a 7-bit NRPN only with the menu item on", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC (assembled)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(specs.size() == 2);
	std::vector<Out> pair = {{0xb, 0, 1, 64, 0}, {0xb, 0, 33, 0, 0}};

	// Off (the default, "lsb"): 99, 98, 6 is not a complete change, CC 6 alone fires nothing.
	REQUIRE(kit.dispatchPumped(cc(1, 99, 0)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 98, 1)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 6, 64)).empty());
	// A new select and a full data entry convert on CC 38 only.
	REQUIRE(kit.dispatchPumped(cc(1, 99, 0)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 98, 1)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 6, 64)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 38, 0)) == pair);

	// On ("msb"): the same 7-bit write converts on CC 6.
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 1);
	drainLog(m);
	REQUIRE(kit.dispatchPumped(cc(1, 99, 0)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 98, 1)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 6, 64)) == pair);
	// The value changes with CC 6 alone, on the selected parameter.
	REQUIRE(kit.dispatchPumped(cc(1, 6, 65)) == (std::vector<Out>{{0xb, 0, 1, 65, 0}, {0xb, 0, 33, 0, 0}}));
	// A full write fires twice: the coarse value, then the real one.
	REQUIRE(kit.dispatchPumped(cc(1, 6, 64)) == pair);
	REQUIRE(kit.dispatchPumped(cc(1, 38, 3)) == (std::vector<Out>{{0xb, 0, 1, 64, 0}, {0xb, 0, 33, 3, 0}}));

	// Off again: CC 6 alone fires nothing.
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 0);
	drainLog(m);
	REQUIRE(kit.dispatchPumped(cc(1, 6, 70)).empty());
	REQUIRE(kit.dispatchPumped(cc(1, 38, 0)) == (std::vector<Out>{{0xb, 0, 1, 70, 0}, {0xb, 0, 33, 0, 0}}));
}

TEST_CASE("'NRPN to CC (assembled).js/.lua' ignores unmapped NRPN numbers", "[MidiKit][NRPN]") {
	std::string path = GENERATE(presetPaths("NRPN to CC (assembled)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// NRPN 5 is not in config.map, so the complete write produces nothing.
	feedDecoded(m, cc(1, 99, 0));
	feedDecoded(m, cc(1, 98, 5));
	feedDecoded(m, cc(1, 6, 64));
	auto ev = kit.dispatchDecoded(cc(1, 38, 0));
	REQUIRE(ev.empty());
}


// Behavioural tests for the NRPN Generator preset (companion to "NRPN to CC"):
// it sweeps a 14-bit value up and down, emitting a spec-compliant NRPN once
// per ticksPerStep MIDI clock ticks. A send is the 4-CC wire form
// (CC 99/98 number MSB/LSB, CC 6/38 value MSB/LSB, see nrpnQuad). Shipped
// config: channel 1, nrpnNumber 0, ticksPerStep 8, stepSize 16, maxValue
// 16383. Only Channel and Ticks per step are in the context menu; the other
// fields are set by rewriting the config block before load (loadNrpnGen).


// The 4-CC wire form midi.setNRPN() expands an NRPN into, in send() order:
// CC 99 = number MSB, CC 98 = number LSB, CC 6 = value MSB, CC 38 = value LSB,
// all on the script's 1-based channel ch (Out channels are 0-based).
static std::vector<Out> nrpnQuad(int ch, int number, int value) {
	return {
		{0xb, static_cast<uint8_t>(ch - 1), 99, static_cast<uint8_t>((number >> 7) & 0x7f), 0},
		{0xb, static_cast<uint8_t>(ch - 1), 98, static_cast<uint8_t>(number & 0x7f), 0},
		{0xb, static_cast<uint8_t>(ch - 1), 6, static_cast<uint8_t>((value >> 7) & 0x7f), 0},
		{0xb, static_cast<uint8_t>(ch - 1), 38, static_cast<uint8_t>(value & 0x7f), 0},
	};
};

// Feeds n consecutive MIDI clock ticks and returns everything emitted across
// them (only the Nth - a step boundary - yields the NRPN quad).
static std::vector<Out> feedTicks(Kit<>& kit, int n) {
	std::vector<Out> all;
	for (int i = 0; i < n; i++) {
		auto ev = kit.dispatch(clockTick());
		all.insert(all.end(), ev.begin(), ev.end());
	}
	return all;
}

// Loads the NRPN Generator preset with an arbitrary config by rewriting the
// shipped defaults in the script text, so the sweep can be driven with a
// handful of steps instead of ~1000. The engines differ only in assignment
// syntax (JS "field: value", Lua "field = value"). Each replacement is
// asserted found so a preset edit can't silently skip a field.
static MidiKitModule* loadNrpnGen(Kit<>& kit, const std::string& relPath, int channel, int nrpnNumber, int ticksPerStep, int stepSize, int maxValue) {
	std::string script = readFile(repoRoot() + "/" + relPath);
	const char* sep = (relPath.find("Lua/") != std::string::npos) ? " = " : ": ";
	auto set = [&script, sep](const char* name, int oldVal, int newVal) {
		// Settings behind a context menu are persisted, so the shipped default
		// sits inside rack.getConfig("name", default).
		std::string wrapped = std::string("rack.getConfig(\"") + name + "\", ";
		std::string from = std::string(name) + sep + std::to_string(oldVal);
		size_t at = script.find(wrapped + std::to_string(oldVal));
		if (at != std::string::npos) {
			script.replace(at, wrapped.size() + std::to_string(oldVal).size(), wrapped + std::to_string(newVal));
			return;
		}
		at = script.find(from);
		REQUIRE(at != std::string::npos);
		script.replace(at, from.size(), std::string(name) + sep + std::to_string(newVal));
	};
	set("channel", 1, channel);
	set("nrpnNumber", 0, nrpnNumber);
	set("ticksPerStep", 8, ticksPerStep);
	set("stepSize", 16, stepSize);
	set("maxValue", 16383, maxValue);

	CATCH_INFO("preset: " << relPath);
	return kit.load(script).m;
}

TEST_CASE("'NRPN Generator.js/.lua' sweeps the value once per ticksPerStep ticks", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(presetPaths("NRPN Generator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Default ticksPerStep=8: the 8th tick is the first step boundary. The
	// value starts at 0 and advanceValue() runs before sendNrpn(), so the
	// first emitted value is 0 + 16 = 16, not 0.
	auto first = feedTicks(kit, 8);
	REQUIRE(first == nrpnQuad(1, 0, 16));

	// The next 8 ticks advance the sweep to 32.
	auto second = feedTicks(kit, 8);
	REQUIRE(second == nrpnQuad(1, 0, 32));
}

TEST_CASE("'NRPN Generator.js/.lua' emits nothing until the first full step", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(presetPaths("NRPN Generator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// 7 ticks leave tickCount at 7 - below the 8-tick step - so no NRPN yet.
	REQUIRE(feedTicks(kit, 7).empty());

	// The 8th tick completes the first step.
	REQUIRE(feedTicks(kit, 1) == nrpnQuad(1, 0, 16));
}

TEST_CASE("'NRPN Generator.js/.lua' resets the phase on Start and Continue", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(presetPaths("NRPN Generator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Partial ticks before Start: tickCount reaches 3, below the step.
	feedTicks(kit, 3);
	REQUIRE(kit.dispatch(startMsg()).empty());

	// Start reset the count to 0, so the next NRPN is not on tick 5 (the
	// remainder of the abandoned step) but on the 8th tick after Start.
	REQUIRE(feedTicks(kit, 7).empty());
	REQUIRE(feedTicks(kit, 1) == nrpnQuad(1, 0, 16));

	// Same for Continue after a partial run.
	feedTicks(kit, 4);
	REQUIRE(kit.dispatch(continueMsg()).empty());
	REQUIRE(feedTicks(kit, 7).empty());
	REQUIRE(feedTicks(kit, 1) == nrpnQuad(1, 0, 32));
}

TEST_CASE("'NRPN Generator.js/.lua' context menu changes ticks per step and channel", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(presetPaths("NRPN Generator"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(specs.size() == 2);
	REQUIRE(specs[0].label == "MIDI channel");
	REQUIRE(specs[1].label == "Ticks per step");

	// "Ticks per step" option index 1 -> TICKS_PER_STEP[1] = 2 ticks/step.
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[1].callbackId, 1);
	// "Channel" option index 1 -> MIDI channel 2 (internal channel 1).
	m->host.getActiveEngine()->invokeContextMenuCallback(specs[0].callbackId, 1);
	drainLog(m);

	// 2 ticks now complete a step; the quad goes out on the new channel.
	REQUIRE(feedTicks(kit, 2) == nrpnQuad(2, 0, 16));

	// The menus report the new selections (read back through onGetValue).
	std::vector<ScriptMenuItem> after = kit.menus();
	REQUIRE(after[0].selected == 1);
	REQUIRE(after[1].selected == 1);
}

TEST_CASE("'NRPN Generator.js/.lua' encodes the NRPN number as 14-bit MSB/LSB", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(presetPaths("NRPN Generator"));
	CATCH_INFO("preset: " << path);

	// nrpnNumber 257 = 0b100000001: CC 99 carries MSB 2, CC 98 carries LSB 1.
	// Everything else stays at the default, so the first step is value 16.
	Kit<> kit;
	loadNrpnGen(kit, path, 1, 257, 8, 16, 16383);

	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 257, 16));
}

TEST_CASE("'NRPN Generator.js/.lua' clamps at maxValue and reverses the sweep", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(presetPaths("NRPN Generator"));
	CATCH_INFO("preset: " << path);

	// A tiny sweep (stepSize 100, maxValue 300) reaches both ends in a handful
	// of steps instead of the ~1000 the shipped config needs, and every value
	// is > 127 so the 14-bit MSB/LSB split of CC 6/38 is exercised too (200 ->
	// CC 6 = 1, CC 38 = 72; 300 -> CC 6 = 2, CC 38 = 44).
	Kit<> kit;
	loadNrpnGen(kit, path, 1, 0, 8, 100, 300);

	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 0, 100));
	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 0, 200));
	// 300 hits maxValue: the value is clamped there, not overshot to 400...
	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 0, 300));
	// ...and the direction flips.
	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 0, 200));
	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 0, 100));
	// 0 hits the bottom and flips back up.
	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 0, 0));
	REQUIRE(feedTicks(kit, 8) == nrpnQuad(1, 0, 100));
}


// Behavioural tests for the Copy Ch1 CC to Ch2 preset. It duplicates every CC
// on channel 1 onto channel 2 (the copy is sent first, then the original),
// and leaves everything else untouched.


TEST_CASE("'Copy Ch1 CC to Ch2.js/.lua' duplicates a channel-1 CC onto channel 2", "[MidiKit][CopyCC]") {
	std::string path = GENERATE(presetPaths("Copy Ch1 CC to Ch2"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// A CC on MIDI channel 1 (internal 0) is copied to channel 2 (internal 1).
	// The script calls send(copy) before send(original), and the engine now
	// sends in send() order, so the copy on channel 2 comes out first, then
	// the original on channel 1.
	auto ev = kit.dispatch(cc(0, 20, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 1, 20, 100, 0}, {0xb, 0, 20, 100, 0}});
}

TEST_CASE("'Copy Ch1 CC to Ch2.js/.lua' does not duplicate CCs on other channels", "[MidiKit][CopyCC]") {
	std::string path = GENERATE(presetPaths("Copy Ch1 CC to Ch2"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// A CC on MIDI channel 2 (internal 1) is only forwarded on its own channel
	// - no copy.
	auto ev = kit.dispatch(cc(1, 20, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 1, 20, 100, 0}});
}

TEST_CASE("'Copy Ch1 CC to Ch2.js/.lua' does not duplicate non-CC messages on channel 1", "[MidiKit][CopyCC]") {
	std::string path = GENERATE(presetPaths("Copy Ch1 CC to Ch2"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// A Note-On on MIDI channel 1 (internal 0) is not a CC, so it is only
	// forwarded once on its own channel.
	auto ev = kit.dispatch(noteOn(0, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0x9, 0, 60, 100, 0}});
}


// Behavioural tests for the Rewrite Ch1 to Ch2 preset. It rewrites every
// message on channel 1 to channel 2 and leaves all other channels alone.


TEST_CASE("'Rewrite Ch1 to Ch2.js/.lua' rewrites channel-1 messages to channel 2", "[MidiKit][Rewrite]") {
	std::string path = GENERATE(presetPaths("Rewrite Ch1 to Ch2"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Note-On and CC on MIDI channel 1 (internal 0) both come out on channel 2
	// (internal 1).
	auto note = kit.dispatch(noteOn(0, 60, 100));
	REQUIRE(note == std::vector<Out>{{0x9, 1, 60, 100, 0}});
	auto ccEv = kit.dispatch(cc(0, 20, 100));
	REQUIRE(ccEv == std::vector<Out>{{0xb, 1, 20, 100, 0}});
}

TEST_CASE("'Rewrite Ch1 to Ch2.js/.lua' leaves other channels unchanged", "[MidiKit][Rewrite]") {
	std::string path = GENERATE(presetPaths("Rewrite Ch1 to Ch2"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Channel 3 (internal 2) and channel 2 (internal 1) are untouched.
	auto note = kit.dispatch(noteOn(3, 60, 100));
	REQUIRE(note == std::vector<Out>{{0x9, 3, 60, 100, 0}});
	auto ccEv = kit.dispatch(cc(2, 20, 100));
	REQUIRE(ccEv == std::vector<Out>{{0xb, 2, 20, 100, 0}});
}



// Behavioural tests for the Volca Sample preset. Shipped default converts
// MIDI notes on channels 1-10 to CC 43 speed + Note-On 60 for the Volca
// Sample's chromatic playback. Channel 16 (poly channel) uses notes 0-9 for
// part selection and 36-84 for chromatic play with 4-voice round-robin
// allocation (channels 7-10). Pitch bend → CC 44 with configurable range
// mapping. Non-note messages and notes outside the chromatic range pass
// through unchanged.

// Speed for note 60 (C4, index 24 in the 0-based speed table).
#define KVS_SPEED_C4 64

// Note 60 on a part channel (MIDI ch 1 → internal 0) produces CC 43 (speed)
// then Note-On 60 on the same channel. The 20 init CCs from onLoad must be
// drained first.
TEST_CASE("'Volca Sample.js/.lua' multi-channel note maps to CC43 speed + Note-On 60", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.drain();  // drain the 10×2 init CCs sent by onLoad

	auto ev = kit.dispatch(noteOn(0, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 0, 43, KVS_SPEED_C4, 0}, {0x9, 0, 60, 100, 0}});
}

// A note outside the chromatic range (35 < 36) passes through unchanged.
TEST_CASE("'Volca Sample.js/.lua' out-of-range note passes through", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.drain();  // drain init CCs

	auto ev = kit.dispatch(noteOn(0, 35, 100));
	REQUIRE(ev == std::vector<Out>{{0x9, 0, 35, 100, 0}});
}

// Notes 0-9 on the poly channel select the sample part and are consumed.
TEST_CASE("'Volca Sample.js/.lua' poly-channel note 0-9 selects part", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.drain();  // drain init CCs

	// note 3 on poly channel (internal 15) → selects part channel 4, consumed.
	REQUIRE(kit.dispatch(noteOn(15, 3, 100)).empty());

	// Next chromatic note on poly channel goes to part channel 4 (internal 3).
	auto ev = kit.dispatch(noteOn(15, 60, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 3, 43, KVS_SPEED_C4, 0}, {0x9, 3, 60, 100, 0}});
}

// Chromatic notes on the poly channel cycle through 4 voice channels.
TEST_CASE("'Volca Sample.js/.lua' poly mode cycles voices round-robin", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.drain();  // drain init CCs

	// Four successive notes on poly channel → channels 7, 8, 9, 10.
	auto n1 = kit.dispatch(noteOn(15, 60, 100));
	REQUIRE(n1 == std::vector<Out>{{0xb, 6, 43, KVS_SPEED_C4, 0}, {0x9, 6, 60, 100, 0}});

	auto n2 = kit.dispatch(noteOn(15, 62, 100));
	// Speed for note 62: index 26 → 69
	REQUIRE(n2 == std::vector<Out>{{0xb, 7, 43, 69, 0}, {0x9, 7, 60, 100, 0}});

	auto n3 = kit.dispatch(noteOn(15, 64, 100));
	// Speed for note 64: index 28 → 75
	REQUIRE(n3 == std::vector<Out>{{0xb, 8, 43, 75, 0}, {0x9, 8, 60, 100, 0}});

	auto n4 = kit.dispatch(noteOn(15, 67, 100));
	// Speed for note 67: index 31 → 83
	REQUIRE(n4 == std::vector<Out>{{0xb, 9, 43, 83, 0}, {0x9, 9, 60, 100, 0}});

	// Wraps back to channel 7.
	auto n5 = kit.dispatch(noteOn(15, 72, 100));
	// Speed for note 72: index 72-36=36 → SPEED_TABLE[36] = 96
	REQUIRE(n5 == std::vector<Out>{{0xb, 6, 43, 96, 0}, {0x9, 6, 60, 100, 0}});
}

// Note-Off releases the correct voice channel in poly mode.
TEST_CASE("'Volca Sample.js/.lua' Note-Off routes to the correct voice channel", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	kit.drain();  // drain init CCs

	// Two notes on poly channel → channels 7 and 8.
	kit.dispatch(noteOn(15, 60, 100));
	kit.dispatch(noteOn(15, 64, 100));
	drainLog(m);

	// Releasing note 64 → voice channel 8 (internal 7).
	auto off = kit.dispatch(noteOff(15, 64));
	REQUIRE(off == std::vector<Out>{{0x8, 7, 60, 0, 0}});

	// Releasing note 60 → voice channel 7 (internal 6).
	auto off2 = kit.dispatch(noteOff(15, 60));
	REQUIRE(off2 == std::vector<Out>{{0x8, 6, 60, 0, 0}});
}

// Pitch bend → CC 44 with the configured range mapping.
TEST_CASE("'Volca Sample.js/.lua' pitch bend maps to CC44", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.drain();  // drain init CCs

	// pitchWheel(1, 8192 + 64*128): MSB=64, which sits at 0.5 in the
	// [32,96] input range → rescales to 65 in [60,70] output → clamped 65.
	// (64 - 32) / (96 - 32) = 0.5; 0.5 * (70 - 60) + 60 = 65.
	auto ev = kit.dispatch(pitchWheel(0, 64 * 128));
	REQUIRE(ev == std::vector<Out>{{0xb, 0, 44, 65, 0}});
}

// Non-note messages pass through unchanged.
TEST_CASE("'Volca Sample.js/.lua' non-note messages pass through", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	kit.drain();  // drain init CCs

	auto ev = kit.dispatch(cc(0, 20, 100));
	REQUIRE(ev == std::vector<Out>{{0xb, 0, 20, 100, 0}});
}

// Active notes are released on unload.
TEST_CASE("'Volca Sample.js/.lua' releases active notes on unload", "[MidiKit][VolcaSample]") {
	std::string path = GENERATE(presetPaths("Volca Sample"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	kit.drain();  // drain init CCs

	// Hold two notes: one on multi-channel 1, one on poly channel.
	kit.dispatch(noteOn(0, 60, 100));   // multi-channel
	kit.dispatch(noteOn(15, 64, 100));  // poly, voice channel 7
	drainLog(m);

	m->loadScript("");

	// onUnload releases both active notes on their respective channels.
	// Lua table iteration order differs from JS, so assert on the set.
	auto ev = kit.drain();
	REQUIRE(ev.size() == 2);
	bool hasCh0 = (ev[0].channel == 0 && ev[0].note == 60) || (ev[1].channel == 0 && ev[1].note == 60);
	bool hasCh6 = (ev[0].channel == 6 && ev[0].note == 60) || (ev[1].channel == 6 && ev[1].note == 60);
	REQUIRE(hasCh0);
	REQUIRE(hasCh6);
}

TEST_CASE("'Program Change Trigger.js/.lua' each of the 16 trigger channels sends its own program", "[MidiKit][TriggerProgramChange]") {
	std::string path = GENERATE(presetPaths("Program Change Trigger"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// Default config: channels 1..16 -> programs 0..15 on MIDI channel 1.
	for (int ch = 1; ch <= 16; ch++) {
		auto ev = kit.dispatchTick(0, ch - 1);
		REQUIRE(ev.size() == 1);
		REQUIRE(ev[0].status == 0xc);
		REQUIRE(ev[0].channel == 0);
		REQUIRE(ev[0].note == ch - 1);
		REQUIRE(ev[0].ticks == 0);
	}

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Program Change Trigger.js/.lua' passes MIDI in through unchanged", "[MidiKit][TriggerProgramChange]") {
	std::string path = GENERATE(presetPaths("Program Change Trigger"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	auto ev = kit.dispatch(noteOn(3, 60, 100));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0] == (Out{0x9, 3, 60, 100, 0}));
}

// Program Change CV: CV input 1 read as V/Oct (semitone = 1/12 V) picks the
// program, sampled on each trigger-channel-1 tick.
static std::vector<Out> feedCvTrigger(Kit<>& kit, float volts) {
	kit.m->inputs[MidiKitModule::INPUT].setVoltage(volts, 0);
	return kit.dispatchTick(0, 0);
}

TEST_CASE("'Program Change CV.js/.lua' V/Oct on input 1 selects the program", "[MidiKit][ProgramChangeCv]") {
	std::string path = GENERATE(presetPaths("Program Change CV"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	struct Case { float volts; int program; };
	const Case cases[] = {
		{0.f, 0},            // C0
		{1.f / 12.f, 1},     // C#0
		{1.f, 12},           // C1
		{5.f, 60},
		{127.f / 12.f, 127},
	};
	for (const Case& c : cases) {
		auto ev = feedCvTrigger(kit, c.volts);
		REQUIRE(ev.size() == 1);
		REQUIRE(ev[0].status == 0xc);
		REQUIRE(ev[0].channel == 0);
		REQUIRE(ev[0].note == c.program);
	}

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Program Change CV.js/.lua' clamps out-of-range voltages", "[MidiKit][ProgramChangeCv]") {
	std::string path = GENERATE(presetPaths("Program Change CV"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	auto low = feedCvTrigger(kit, -3.f);
	REQUIRE(low.size() == 1);
	REQUIRE(low[0].note == 0);

	auto high = feedCvTrigger(kit, 10.f);
	REQUIRE(high.size() == 1);
	REQUIRE(high[0].note == 120);

	auto over = feedCvTrigger(kit, 20.f);
	REQUIRE(over.size() == 1);
	REQUIRE(over[0].note == 127);
}

// Bank Select (e.g. Arturia Microfreak): knob 1 = bank, knob 2 = preset in the bank (0-127). A
// trigger on channel 1 sends Bank Select (CC 0 = MSB, CC 32 = LSB) followed
// by the Program Change.
static std::vector<Out> feedMicrofreak(Kit<>& kit, float bankKnob, float presetKnob) {
	kit.param(0, bankKnob).param(1, presetKnob);
	return kit.dispatchTick(0, 0);
}

static void requireMicrofreakMessages(const std::vector<Out>& ev, int bank, int program) {
	REQUIRE(ev.size() == 3);
	REQUIRE(ev[0].status == 0xb);
	REQUIRE(ev[0].channel == 0);
	REQUIRE(ev[0].note == 0);           // Bank Select MSB
	REQUIRE(ev[0].value == bank);
	REQUIRE(ev[1].status == 0xb);
	REQUIRE(ev[1].channel == 0);
	REQUIRE(ev[1].note == 32);          // Bank Select LSB
	REQUIRE(ev[1].value == 0);
	REQUIRE(ev[2].status == 0xc);
	REQUIRE(ev[2].channel == 0);
	REQUIRE(ev[2].note == program);
}

TEST_CASE("'Bank Select (param).js/.lua' trigger sends Bank Select CC 0/32 then Program Change", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (param)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// Default config: 4 banks. Knob quarters map to banks 0-3; the preset
	// knob spans 0-127.
	struct Case { float bankKnob; int bank; float presetKnob; int program; };
	const Case cases[] = {
		{0.f,   0, 0.f,   0},
		{0.3f,  1, 0.5f,  64},
		{0.6f,  2, 0.25f, 32},
		{1.f,   3, 1.f,   127},
	};
	for (const Case& c : cases)
		requireMicrofreakMessages(feedMicrofreak(kit, c.bankKnob, c.presetKnob), c.bank, c.program);

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Bank Select (param).js/.lua' bank boundaries and preset extremes", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (param)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	// Just below / above each quarter boundary of the bank knob.
	const std::pair<float, int> banks[] = {
		{0.24f, 0}, {0.26f, 1}, {0.49f, 1}, {0.51f, 2}, {0.74f, 2}, {0.76f, 3}
	};
	for (const auto& b : banks)
		requireMicrofreakMessages(feedMicrofreak(kit, b.first, 0.f), b.second, 0);

	// The full preset sweep is 0..127 and never exceeds a 7-bit program.
	requireMicrofreakMessages(feedMicrofreak(kit, 0.f, 0.f), 0, 0);
	requireMicrofreakMessages(feedMicrofreak(kit, 0.f, 1.f), 0, 127);
}

TEST_CASE("'Bank Select (param).js/.lua' number of banks comes from the config", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (param)"));
	CATCH_INFO("preset: " << path);

	// JS "banks: 4," / Lua "banks = 4,"
	std::string script = readFile(repoRoot() + "/" + path);
	size_t pos = script.find("banks: 4,");
	size_t len = 9;
	std::string repl = "banks: 8,";
	if (pos == std::string::npos) {
		pos = script.find("banks = 4,");
		len = 10;
		repl = "banks = 8,";
	}
	REQUIRE(pos != std::string::npos);
	script.replace(pos, len, repl);

	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	REQUIRE(drainLog(m).find("rror") == std::string::npos);

	// 8 banks: each eighth of the knob is one bank.
	requireMicrofreakMessages(feedMicrofreak(kit, 0.f, 0.f), 0, 0);
	requireMicrofreakMessages(feedMicrofreak(kit, 0.3f, 0.f), 2, 0);
	requireMicrofreakMessages(feedMicrofreak(kit, 0.6f, 0.f), 4, 0);
	requireMicrofreakMessages(feedMicrofreak(kit, 1.f, 0.f), 7, 0);
}

TEST_CASE("'Bank Select (param).js/.lua' trigger 1 logs the preset change", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (param)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// Bank 1 (knob 0.3), program 64 => preset 192
	feedMicrofreak(kit, 0.3f, 0.5f);
	REQUIRE(drainLog(m).find("Preset 192 (bank 1, program 64") != std::string::npos);
}

// Bank Select (menu): the context menu is the only input. Items in
// registration order: Channel (id 1), Bank (2), Program group (3), Program (4).
TEST_CASE("'Bank Select (menu).js/.lua' sends from the context menu only", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (menu)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	auto click = [&](int id, int value) {
		m->host.getActiveEngine()->invokeContextMenuCallback(id, value);
		m->host.getActiveEngine()->process();
		return kit.drain();
	};

	// Nothing is sent on load.
	REQUIRE(kit.drain().empty());

	// Bank 2 with program 0
	requireMicrofreakMessages(click(2, 2), 2, 0);
	// Changing the group sends nothing; it only re-lists the Program item.
	REQUIRE(click(3, 3).empty());
	// The Program item now lists group 3 (48-63): entry 5 is program 53
	requireMicrofreakMessages(click(4, 5), 2, 53);
	REQUIRE(drainLog(m).find("Preset 309 (bank 2, program 53") != std::string::npos);

	// Another group keeps the position within it (5), still without sending
	REQUIRE(click(3, 7).empty());
	requireMicrofreakMessages(click(4, 5), 2, 117);

	// Channel change sends nothing; the next send uses it.
	REQUIRE(click(1, 4).empty());
	auto ev = click(2, 1);
	REQUIRE(ev.size() == 3);
	REQUIRE(ev[0].channel == 4);
	REQUIRE(ev[2].channel == 4);

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

// Config numbers come back as floats after a reload; the Program labels must
// still be plain integers ("53", not "53.0").
TEST_CASE("'Bank Select (menu).js/.lua' restores its selection with integer labels", "[MidiKit][Microfreak][JSON]") {
	std::string path = GENERATE(presetPaths("Bank Select (menu)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->host.getActiveEngine()->invokeContextMenuCallback(2, 3);
	m->host.getActiveEngine()->invokeContextMenuCallback(3, 3);
	m->host.getActiveEngine()->invokeContextMenuCallback(4, 5);
	drainLog(m);

	rack::engine::Module::SaveEvent saveEvent;
	m->onSave(saveEvent);
	json_t* rootJ = m->dataToJson();
	Kit<> kit2;
	MidiKitModule* m2 = kit2.m;
	m2->dataFromJson(rootJ);
	json_decref(rootJ);

	std::vector<ScriptMenuItem> specs = kit2.menus();
	REQUIRE(specs.size() == 4);
	REQUIRE(specs[1].selected == 3);                       // Bank
	REQUIRE(specs[2].selected == 3);                       // Program group
	REQUIRE(specs[3].selected == 5);                       // Program
	REQUIRE(specs[3].options.size() == 16);
	REQUIRE(specs[3].options[0] == "48");
	REQUIRE(specs[3].options[15] == "63");
	REQUIRE(kit2.drain().empty());                         // nothing sent on load
}

TEST_CASE("'Bank Select (param).js/.lua' trigger 2 steps to the next preset", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (param)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	// Before anything was sent, "next" continues from the knobs (bank 0,
	// program 5) and does not send anything for the knobs themselves.
	m->params[MidiKitModule::PARAM + 0].setValue(0.f);
	m->params[MidiKitModule::PARAM + 1].setValue(5.f / 128.f);
	requireMicrofreakMessages(kit.dispatchTick(1), 0, 6);
	REQUIRE(drainLog(m).find("Preset 6 (bank 0, program 6") != std::string::npos);

	requireMicrofreakMessages(kit.dispatchTick(1), 0, 7);

	// Trigger 1 re-syncs from the knobs: last program of bank 0 ...
	requireMicrofreakMessages(feedMicrofreak(kit, 0.f, 1.f), 0, 127);
	// ... next rolls over into bank 1, program 0
	requireMicrofreakMessages(kit.dispatchTick(1), 1, 0);
	REQUIRE(drainLog(m).find("Preset 128 (bank 1, program 0") != std::string::npos);

	// Last preset of the last bank wraps around to bank 0, program 0
	requireMicrofreakMessages(feedMicrofreak(kit, 1.f, 1.f), 3, 127);
	requireMicrofreakMessages(kit.dispatchTick(1), 0, 0);

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Bank Select (param).js/.lua' turning the knobs alone sends nothing", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (param)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	m->params[MidiKitModule::PARAM + 0].setValue(0.8f);
	m->params[MidiKitModule::PARAM + 1].setValue(0.4f);
	m->host.getActiveEngine()->process();
	REQUIRE(kit.drain().empty());
}

TEST_CASE("'Bank Select (param).js/.lua' passes MIDI in through unchanged", "[MidiKit][Microfreak]") {
	std::string path = GENERATE(presetPaths("Bank Select (param)"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	auto ev = kit.dispatch(noteOn(3, 60, 100));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0] == (Out{0x9, 3, 60, 100, 0}));
}


// Channel router: input 1 -> up to four outputs, by MIDI channel. Default config:
// ch 1,2 -> out 1, ch 3,5 -> out 2, ch 4 -> out 3, out 4 unused.
// Channels below are the script's 1-based ones; the message helpers take Rack's
// 0-based channel, hence the `- 1` / `ch - 1` when feeding.

// Loads the router with `from` replaced by the matching JS or Lua text of
// `to`, so a test can vary the config without a second copy of the script.
static MidiKitModule* loadRouter(Kit<>& kit, const std::string& path, const std::string& jsFrom, const std::string& jsTo,
                                 const std::string& luaFrom, const std::string& luaTo) {
	std::string script = readFile(repoRoot() + "/" + path);
	bool js = path.find(".js") != std::string::npos;
	const std::string& from = js ? jsFrom : luaFrom;
	const std::string& to = js ? jsTo : luaTo;
	size_t pos = script.find(from);
	CATCH_INFO("config text not found: " << from);
	REQUIRE(pos != std::string::npos);
	script.replace(pos, from.size(), to);

	REQUIRE(kit.loadRaw(script).find("rror") == std::string::npos);
	return kit.m;
}

TEST_CASE("'Channel router.js/.lua' routes each channel to the output configured for it", "[MidiKit][Router]") {
	std::string path = GENERATE(presetPaths("Channel router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	struct Case { int ch; int port; };
	const Case cases[] = { {1, 0}, {2, 0}, {3, 1}, {5, 1}, {4, 2} };
	for (const Case& c : cases) {
		auto ev = kit.dispatch(noteOn(c.ch - 1, 60, 100));
		CATCH_INFO("channel " << c.ch);
		REQUIRE(ev.size() == 1);
		REQUIRE(ev[0] == Out::want(0x9, uint8_t(c.ch - 1), 60, 100, 0, c.port));
	}

	// Channels in no route are dropped, and nothing ever reaches output 4.
	REQUIRE(kit.dispatch(noteOn(5, 60, 100)).empty());
	REQUIRE(kit.dispatch(noteOn(15, 60, 100)).empty());

	// Every message type follows its channel, not just notes.
	auto ev = kit.dispatch(cc(3, 7, 99));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 2);
}

TEST_CASE("'Channel router.js/.lua' enables exactly the configured outputs", "[MidiKit][Router]") {
	std::string path = GENERATE(presetPaths("Channel router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	REQUIRE(m->midiOuts.enabledCount() == 4);
	REQUIRE(m->midiIns.enabledCount() == 1);

	// Two entries in routes -> two outputs.
	Kit<> kit2;
	m = loadRouter(kit2, path,
		"        [4],      // output 3\n        []        // output 4\n", "",
		"        { 4 },      -- output 3\n        {}          -- output 4\n", "");
	REQUIRE(m->midiOuts.enabledCount() == 2);
	REQUIRE(m->midiIns.enabledCount() == 1);
}

TEST_CASE("'Channel router.js/.lua' sends channel-less messages to every output", "[MidiKit][Router]") {
	std::string path = GENERATE(presetPaths("Channel router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);
	auto ev = kit.dispatch(clockTick());
	REQUIRE(ev.size() == 4);
	for (int i = 0; i < 4; i++) {
		REQUIRE(ev[i].port == i);
		REQUIRE(ev[i].status == 0xf);
	}

	Kit<> kit2;
	loadRouter(kit2, path, "systemToAll: true", "systemToAll: false", "systemToAll = true", "systemToAll = false");
	REQUIRE(kit2.dispatch(clockTick()).empty());
}

TEST_CASE("'Channel router.js/.lua' copies a channel that is in several routes", "[MidiKit][Router]") {
	std::string path = GENERATE(presetPaths("Channel router"));
	CATCH_INFO("preset: " << path);

	// Output 2 now takes channels 1 and 3, so channel 1 goes to outputs 1 and 2.
	Kit<> kit;
	loadRouter(kit, path, "[3, 5],", "[1, 3],", "{ 3, 5 },", "{ 1, 3 },");
	auto ev = kit.dispatch(noteOn(0, 64, 90));
	REQUIRE(ev.size() == 2);
	REQUIRE(ev[0] == Out::want(0x9, 0, 64, 90, 0, 0));
	REQUIRE(ev[1] == Out::want(0x9, 0, 64, 90, 0, 1));   // the same message on the second output

	// Channel 5 lost its route.
	REQUIRE(kit.dispatch(noteOn(4, 64, 90)).empty());
}

TEST_CASE("'Channel router.js/.lua' sends unrouted channels to the fallback output", "[MidiKit][Router]") {
	std::string path = GENERATE(presetPaths("Channel router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	loadRouter(kit, path, "fallbackOutput: 0", "fallbackOutput: 4", "fallbackOutput = 0", "fallbackOutput = 4");
	auto ev = kit.dispatch(noteOn(5, 60, 100));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 3);
	// Routed channels are unaffected.
	ev = kit.dispatch(noteOn(2, 60, 100));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 1);
}

TEST_CASE("'Channel router.js/.lua' ignores invalid channels in the config", "[MidiKit][Router]") {
	std::string path = GENERATE(presetPaths("Channel router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = loadRouter(kit, path, "[4],      // output 3", "[4, 17],      // output 3", "{ 4 },      -- output 3", "{ 4, 17 },      -- output 3");
	std::string log = drainLog(m);
	auto ev = kit.dispatch(noteOn(3, 60, 100));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 2);
}


TEST_CASE("'Smart merge.js/.lua' forwards only the active input", "[MidiKit][SmartMerge]") {
	std::string path = GENERATE(presetPaths("Smart merge"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	auto active = kit.dispatch(noteOn(1, 60, 100), 0);
	REQUIRE(active == std::vector<Out>{{0x9, 1, 60, 100, 0}});
	auto inactive = kit.dispatch(noteOn(1, 64, 90), 1);
	REQUIRE(inactive.empty());

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Smart merge.js/.lua' switch releases the old notes and replays the new state in order", "[MidiKit][SmartMerge]") {
	std::string path = GENERATE(presetPaths("Smart merge"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	kit.dispatch(noteOn(1, 60, 100), 0);
	kit.dispatch(noteOn(1, 62, 100), 0);
	kit.dispatch(noteOff(1, 60), 0);                 // only 62 is still held on input 1

	// Input 2 sends state while inactive: CC 7 first, then CC 10, CC 7 again
	// (the value updates, the position stays), and two notes.
	kit.dispatch(cc(2, 7, 10), 1);
	kit.dispatch(cc(2, 10, 64), 1);
	kit.dispatch(cc(2, 7, 99), 1);
	kit.dispatch(noteOn(3, 70, 80), 1);
	kit.dispatch(noteOn(3, 72, 81), 1);

	auto ev = kit.dispatchTick();
	REQUIRE(ev == std::vector<Out>{
		{0x8, 1, 62, 0, 0},                          // old input's held note
		{0xb, 2, 7, 99, 0}, {0xb, 2, 10, 64, 0},     // CCs, first-seen order
		{0x9, 3, 70, 80, 0}, {0x9, 3, 72, 81, 0}});  // held notes, press order

	// Input 2 is active now, input 1 is not forwarded any more.
	REQUIRE(kit.dispatch(noteOn(1, 50, 100), 0).empty());
	REQUIRE(kit.dispatch(noteOff(3, 70), 1) == std::vector<Out>{{0x8, 3, 70, 0, 0}});

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Smart merge.js/.lua' the trigger wraps around after the last input", "[MidiKit][SmartMerge]") {
	std::string path = GENERATE(presetPaths("Smart merge"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	kit.loadPreset(path);

	kit.dispatchTick();                                        // input 2
	REQUIRE(kit.dispatch(noteOn(1, 60, 100), 1).size() == 1);
	kit.dispatchTick();                                        // back to input 1
	// The note held on input 2 is released on the way out.
	REQUIRE(kit.dispatch(noteOn(1, 61, 100), 0) == std::vector<Out>{{0x9, 1, 61, 100, 0}});
}

// The script has no bound of its own: a state of more than 128 messages (one
// drain's budget) arrives whole, over several drains.
TEST_CASE("'Smart merge.js/.lua' a switch of more than 128 messages arrives whole", "[MidiKit][SmartMerge]") {
	std::string path = GENERATE(presetPaths("Smart merge"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	for (int c = 0; c < 160; c++) kit.dispatch(cc(1 + c / 128, c % 128, 1), 1);
	auto ev = kit.dispatchTick();
	REQUIRE(ev.size() == 160);
	REQUIRE_FALSE(m->midiOuts.overflow.load());
}

TEST_CASE("'Smart merge.js/.lua' the Active input menu follows the number of inputs", "[MidiKit][SmartMerge]") {
	std::string path = GENERATE(presetPaths("Smart merge"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	auto menus = kit.menus();
	REQUIRE(menus.size() == 2);
	REQUIRE(menus[0].label == "Number of inputs");
	REQUIRE(menus[1].label == "Active input");
	REQUIRE(menus[1].options.size() == 2);

	// "Number of inputs" -> 4: the Active input list grows in place, no third item.
	m->host.getActiveEngine()->invokeContextMenuCallback(menus[0].callbackId, 2);
	m->host.getActiveEngine()->process();
	menus = kit.menus();
	REQUIRE(menus.size() == 2);
	REQUIRE(menus[1].label == "Active input");
	REQUIRE(menus[1].options.size() == 4);

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}


// Port router: input 1 -> exactly one of the first n outputs (2 by
// default); the trigger on trigger input 1 steps to the next output.
TEST_CASE("'Port router.js/.lua' sends everything to the active output only", "[MidiKit][PortRouter]") {
	std::string path = GENERATE(presetPaths("Port router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	REQUIRE(m->midiOuts.enabledCount() == 2);
	REQUIRE(m->midiIns.enabledCount() == 1);

	// Any channel, any message type - nothing is filtered, all go to output 1.
	const midi::Message msgs[] = { noteOn(0, 60, 100), noteOn(9, 36, 90), cc(3, 7, 99), clockTick() };
	for (const midi::Message& msg : msgs) {
		auto ev = kit.dispatch(msg);
		REQUIRE(ev.size() == 1);
		REQUIRE(ev[0].port == 0);
	}
	auto ev = kit.dispatch(cc(2, 10, 64));
	REQUIRE(ev[0] == (Out{0xb, 2, 10, 64, 0}));

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Port router.js/.lua' a trigger releases held notes and steps to the next output", "[MidiKit][PortRouter]") {
	std::string path = GENERATE(presetPaths("Port router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	kit.dispatch(noteOn(2, 60, 100));
	kit.dispatch(noteOn(9, 62, 100));
	kit.dispatch(noteOn(2, 64, 100));
	kit.dispatch(noteOff(2, 64));   // released by the player - not released again

	auto off = kit.dispatchTick();
	REQUIRE(off == std::vector<Out>{{0x8, 2, 60, 0, 0}, {0x8, 9, 62, 0, 0}});

	auto ev = kit.dispatch(noteOn(0, 64, 90));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 1);

	// Wraps around after the last output.
	REQUIRE(kit.dispatchTick() == std::vector<Out>{Out::want(0x8, 0, 64, 0, 0, 1)});   // the release goes to output 2
	ev = kit.dispatch(noteOn(0, 65, 90));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 0);

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}

TEST_CASE("'Port router.js/.lua' the Active output menu follows the number of outputs", "[MidiKit][PortRouter]") {
	std::string path = GENERATE(presetPaths("Port router"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	auto menus = kit.menus();
	REQUIRE(menus.size() == 2);
	REQUIRE(menus[0].label == "Number of outputs");
	REQUIRE(menus[1].label == "Active output");
	REQUIRE(menus[1].options.size() == 2);

	// "Number of outputs" -> 4: the list grows in place, no third item.
	m->host.getActiveEngine()->invokeContextMenuCallback(menus[0].callbackId, 2);
	m->host.getActiveEngine()->process();
	menus = kit.menus();
	REQUIRE(menus.size() == 2);
	REQUIRE(menus[1].options.size() == 4);
	REQUIRE(m->midiOuts.enabledCount() == 4);

	// Active output -> 4: only that output receives messages.
	m->host.getActiveEngine()->invokeContextMenuCallback(menus[1].callbackId, 3);
	m->host.getActiveEngine()->process();
	kit.drain();
	auto ev = kit.dispatch(noteOn(0, 60, 100));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 3);

	// "Number of outputs" -> 2 drops the active output 4: back to output 2.
	menus = kit.menus();
	m->host.getActiveEngine()->invokeContextMenuCallback(menus[0].callbackId, 0);
	m->host.getActiveEngine()->process();
	kit.drain();
	ev = kit.dispatch(noteOn(0, 61, 100));
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].port == 1);

	REQUIRE(drainLog(m).find("rror") == std::string::npos);
}


// Behavioural tests for the presets that switch on sample-accurate timing
// (Bouncing ball delay, Arpeggiator, Euclidean rhythm generator). They run the
// module's process() and read the frame each message carries at the output
// device, which is where timing is decided: with timing on, a note sits on the
// frame of the clock edge or of the note that caused it, instead of leaving at
// whatever block boundary came next.

static void sendInputAt(MidiKitModule* m, midi::Message msg, int64_t frame) {
	msg.frame = frame;
	m->midiIns.ports[0].processor.getInput().onMessage(msg);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' places its notes on the clock edges", "[MidiKit][EuclidRhythm][Timing]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	CATCH_INFO("preset: " << path);

	// 4 steps, 2 fills: hits on ticks 2, 4, 6 and 8.
	DeviceKit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);
	drainLog(m);
	Device& rec = kit.dev[0];
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	std::vector<int64_t> edges;
	for (int i = 0; i < 8; i++) edges.push_back(1000 + 480 * i);
	driveClockEdges(m, edges, 0, 5000);

	// A hit on tick i cuts the previous note and starts the next one on the same
	// edge, so the note-on follows the note-off one sample later.
	std::vector<int64_t> onFrames;
	for (size_t i = 0; i < rec.frames.size(); i++) {
		REQUIRE(rec.frames[i] >= 0);
		if (rec.statuses[i] == 0x9) onFrames.push_back(rec.frames[i]);
	}
	REQUIRE(onFrames.size() == 4);
	REQUIRE(onFrames[0] == edges[1]);
	for (int k = 0; k < 4; k++) {
		int64_t late = onFrames[k] - edges[1 + 2 * k];
		REQUIRE(late >= 0);
		REQUIRE(late <= 1);
	}
}

TEST_CASE("'Arpeggiator.js/.lua' places its notes and note-offs on the clock edges", "[MidiKit][Arpeggiator][Timing]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// 4 ticks per step, half-length notes (2 ticks), one octave, Up.
	DeviceKit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.35f, 0.f, 0.5f, 0.f);
	Device& rec = kit.dev[0];
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	sendInputAt(m, noteOn(1, 60, 100), 100);
	std::vector<int64_t> edges;
	for (int i = 0; i < 9; i++) edges.push_back(1000 + 480 * i);
	driveClockEdges(m, edges, 0, 6000);

	// The first note starts on the 4th edge and ends on the frame of the 6th, two
	// ticks later. The next step, on the 8th edge, has nothing left to release and
	// starts the next note on its own frame: one note-off per note.
	REQUIRE(rec.statuses == std::vector<int>{0x9, 0x8, 0x9});
	REQUIRE(rec.frames[0] == edges[3]);
	REQUIRE(rec.frames[1] == edges[5]);
	REQUIRE(rec.frames[2] == edges[7]);
}

TEST_CASE("'Arpeggiator.js/.lua' sends one Note-Off per note at every clock division and length", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	CATCH_INFO("preset: " << path);

	// Clock division (param 1) and note length (param 3), from 1 tick per step up.
	struct Setting { float division; float length; };
	for (const Setting& setting : { Setting{0.f, 0.5f}, Setting{0.2f, 0.5f}, Setting{0.35f, 0.5f}, Setting{0.35f, 1.f}, Setting{0.35f, 0.f}, Setting{0.6f, 0.5f} }) {
		CATCH_INFO("division " << setting.division << " length " << setting.length);
		Kit<> kit;
		MidiKitModule* m = loadArp(kit, path, setting.division, 0.f, setting.length, 0.f);
		feed(m, noteOn(1, 60, 100));
		feed(m, noteOn(1, 64, 100));
		drainLog(m);

		std::vector<Out> all;
		for (int i = 0; i < 64; i++) {
			for (const Out& e : kit.dispatchTick()) all.push_back(e);
		}
		REQUIRE_FALSE(all.empty());

		// Every Note-Off ends a note that is sounding: it never repeats, and never
		// comes before the Note-On it belongs to.
		int sounding = -1;
		int ons = 0, offs = 0;
		for (const Out& e : all) {
			if (e.status == 0x9) {
				ons++;
				sounding = e.note;
			}
			else if (e.status == 0x8) {
				offs++;
				REQUIRE(sounding == e.note);
				sounding = -1;
			}
		}
		REQUIRE(offs <= ons);
		REQUIRE(offs >= ons - 1);   // the last note may still be sounding

	}
}

TEST_CASE("'Bouncing ball delay.js/.lua' places its echoes on exact frames from the note", "[MidiKit][BouncingBall][Timing]") {
	std::string path = GENERATE(presetPaths("Bouncing ball delay"));
	CATCH_INFO("preset: " << path);

	// Gravity 0 (even gaps of 250 ms), no velocity decay: the 12-echo cap.
	DeviceKit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.f);
	m->params[MidiKitModule::PARAM + 1].setValue(1.f);
	m->params[MidiKitModule::PARAM + 2].setValue(0.f);
	drainLog(m);
	Device& rec = kit.dev[0];

	const int64_t arrival = 5000;
	double sr = m->sampleRate.load();
	auto after = [&](double ms) { return arrival + int64_t(ms / 1000.0 * sr); };

	sendInputAt(m, noteOn(1, 60, 100), arrival);
	driveClockEdges(m, {}, 0, after(12 * 250 + 40) + 200);

	// The dry note is on the frame of its input; echo k starts 250 ms * k later
	// and its 40 ms gate ends after that.
	std::vector<int64_t> expectedOn = { arrival };
	std::vector<int64_t> expectedOff;
	for (int k = 1; k <= 12; k++) {
		expectedOn.push_back(after(250.0 * k));
		expectedOff.push_back(after(250.0 * k + 40.0));
	}
	std::vector<int64_t> on, off;
	for (size_t i = 0; i < rec.frames.size(); i++) {
		(rec.statuses[i] == 0x9 ? on : off).push_back(rec.frames[i]);
	}
	REQUIRE(on == expectedOn);
	REQUIRE(off == expectedOff);
}

// On unload the generators send their note-off with midiOut.send(), which the
// module holds behind Rack's output queue. A note-on handed to Rack just before
// may still be waiting in it, and an immediate note-off would overtake it. The
// wait is two blocks and a frame, so it follows the engine's block size.

// EngineScope (MidiKit.test.hpp) makes the engine report a frame and a block size. loadScript()
// seeds the published frame from the frame, process() publishes the block size.

// Unloads the script at `unloadFrame` and returns the frame, relative to it, on
// which the first note-off after the unload reaches the device (-1 if none).
static int64_t unloadNoteOffDelay(MidiKitModule* m, Device& rec, EngineScope& engine, int64_t unloadFrame) {
	size_t before = rec.statuses.size();
	engine.mock.frame = unloadFrame;
	m->loadScript("");
	for (int64_t f = unloadFrame; f < unloadFrame + 10000; f++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		m->process(Test::makeProcessArgs(f));
		for (size_t i = before; i < rec.statuses.size(); i++) {
			if (rec.statuses[i] == 0x8) return f - unloadFrame;
		}
	}
	return -1;
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' releases its note two blocks after unloading", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(presetPaths("Euclidean rhythm generator"));
	int64_t block = GENERATE(256, 2048);
	CATCH_INFO("preset: " << path);
	CATCH_INFO("block: " << block);

	EngineScope engine;
	engine.mock.blockFrames = block;
	DeviceKit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps, 2 fills: hits on ticks 2, 4, ...
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);
	drainLog(m);
	Device& rec = kit.dev[0];
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	driveClockEdges(m, { 1000, 1480 }, 0, 1600);   // the hit on tick 2 sounds a note
	REQUIRE(std::count(rec.statuses.begin(), rec.statuses.end(), 0x9) == 1);
	REQUIRE(std::count(rec.statuses.begin(), rec.statuses.end(), 0x8) == 0);

	// Two blocks and a frame, released on the next divider tick (up to 8 later).
	int64_t delay = unloadNoteOffDelay(m, rec, engine, 1600);
	REQUIRE(delay >= 2 * block + 1);
	REQUIRE(delay <= 2 * block + 1 + 8);
}

TEST_CASE("'Arpeggiator.js/.lua' releases its note two blocks after unloading", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(presetPaths("Arpeggiator"));
	int64_t block = GENERATE(256, 2048);
	CATCH_INFO("preset: " << path);
	CATCH_INFO("block: " << block);

	// 4 ticks per step: the first note starts on the 4th edge.
	EngineScope engine;
	engine.mock.blockFrames = block;
	DeviceKit<> kit;
	MidiKitModule* m = loadArp(kit, path, 0.35f, 0.f, 0.5f, 0.f);
	Device& rec = kit.dev[0];
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	sendInputAt(m, noteOn(1, 60, 100), 100);
	driveClockEdges(m, { 1000, 1480, 1960, 2440 }, 0, 2600);
	REQUIRE(std::count(rec.statuses.begin(), rec.statuses.end(), 0x9) == 1);
	REQUIRE(std::count(rec.statuses.begin(), rec.statuses.end(), 0x8) == 0);

	int64_t delay = unloadNoteOffDelay(m, rec, engine, 2600);
	REQUIRE(delay >= 2 * block + 1);
	REQUIRE(delay <= 2 * block + 1 + 8);
}


// Every context-menu setting of these presets must survive a save/reload: flip
// each menu item, save, load the patch into a fresh module and compare what the
// rebuilt menus report. A preset that never calls rack.setConfig() for an item
// forgets it here.
TEST_CASE("Preset context-menu settings survive a save/reload round-trip", "[MidiKit][Presets][JSON]") {
	const char* name = GENERATE(
		"Arpeggiator", "Chord harmonizer", "Clock divider", "Clock multiplier",
		"MPE to single channel", "Micro scale", "NRPN Generator", "NRPN to CC",
		"NRPN to CC (assembled)", "Note length quantiser", "Velocity curve",
		"Euclidean rhythm generator", "Keyboard split");
	const char* engine = GENERATE("JavaScript", "Lua");
	std::string path = presetPath(requirePreset(name), engine);
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;

	std::vector<ScriptMenuItem> specs = kit.menus();
	REQUIRE(!specs.empty());

	// Move every item to a different value than the one it loaded with.
	std::vector<int> expected;
	// Actions and file items have no state to keep (the Micro scale file is covered
	// by its own test).
	auto hasValue = [](const ScriptMenuItem& item) {
		return item.type == ScriptMenuItem::Type::Boolean || item.type == ScriptMenuItem::Type::Options;
	};
	for (const ScriptMenuItem& item : specs) {
		int next = -1;
		if (!hasValue(item)) { expected.push_back(next); continue; }
		if (item.type == ScriptMenuItem::Type::Boolean) next = item.checked ? 0 : 1;
		else next = (item.selected + 1) % (int)item.options.size();
		expected.push_back(next);
		m->host.getActiveEngine()->invokeContextMenuCallback(item.callbackId, next);
	}
	drainLog(m);

	rack::engine::Module::SaveEvent saveEvent;
	m->onSave(saveEvent);
	json_t* rootJ = m->dataToJson();

	Kit<> kit2;
	MidiKitModule* m2 = kit2.m;
	m2->dataFromJson(rootJ);
	json_decref(rootJ);

	std::vector<ScriptMenuItem> restored = kit2.menus();
	REQUIRE(restored.size() == specs.size());
	for (size_t i = 0; i < specs.size(); i++) {
		CATCH_INFO("menu item: " << specs[i].label);
		if (!hasValue(specs[i])) continue;
		int got = specs[i].type == ScriptMenuItem::Type::Boolean ? (restored[i].checked ? 1 : 0) : restored[i].selected;
		REQUIRE(got == expected[i]);
	}
}


// Broadcaster and follower on two modules sharing a bus, in every engine pairing.
// Output is realtime MIDI, whose status nibble is 0xf and whose channel field is
// the low nibble of the status byte: F8 clock = 8, FA start = 10, FC stop = 12.
TEST_CASE("'Transport broadcaster' and 'Transport follower' relay clock, start and stop", "[MidiKit][Broadcast][TransportRelay]") {
	for (const char* broadcasterEngine : ENGINES) {
		for (const char* followerEngine : ENGINES) {
			CATCH_INFO("broadcaster " << broadcasterEngine << ", follower " << followerEngine);
			auto bus = std::make_shared<StoermelderPackOne::MidiScript::BroadcastBus>();
			auto worker = std::make_shared<StoermelderPackOne::SyncTaskWorker>();
			Kit<> followerKit(worker, bus);
			Kit<> broadcasterKit(worker, bus);
			MidiKitModule* follower = followerKit.m;
			MidiKitModule* broadcaster = broadcasterKit.m;
			follower->loadScript(readFile(repoRoot() + "/" + presetPath(requirePreset("Transport follower"), followerEngine)));
			drainLog(follower);

			broadcaster->loadScript(readFile(repoRoot() + "/" + presetPath(requirePreset("Transport broadcaster"), broadcasterEngine)));
			REQUIRE(drainLog(broadcaster).find("rror") == std::string::npos);

			auto pump = [&]() {
				follower->host.getActiveEngine()->process();
				return followerKit.drain();
			};

			// Nothing is sent until the "Running" menu item is switched on.
			REQUIRE(pump().empty());
			broadcasterKit.click("Running", 1);
			REQUIRE(drainLog(broadcaster).find("Transport start sent to 1 module(s)") != std::string::npos);
			std::vector<Out> out = pump();
			REQUIRE(out.size() == 1);
			REQUIRE(out[0].status == 0xf);
			REQUIRE(out[0].channel == 10);
			REQUIRE(drainLog(follower).find("Transport start") != std::string::npos);

			// A trigger pulse is a clock.
			broadcaster->host.getActiveEngine()->processInTick(0, 0);
			broadcaster->host.getActiveEngine()->process();
			out = pump();
			REQUIRE(out.size() == 1);
			REQUIRE(out[0].channel == 8);

			// Clearing the script runs onUnload(), which stops a running transport.
			broadcaster->loadScript("");
			out = pump();
			REQUIRE(out.size() == 1);
			REQUIRE(out[0].channel == 12);
			std::string log = drainLog(follower);
			REQUIRE(log.find("Transport stop") != std::string::npos);
			REQUIRE(log.find("rror") == std::string::npos);
		}
	}
}


// "Load scale" / "Default scale" menu items of the Micro scale preset

// The notes go round-robin over the output channels, so compare without the channel.
static std::vector<Out> onChannel0(std::vector<Out> events) {
	for (Out& e : events) e.channel = 0;
	return events;
}

// 12-EDO as a file saved on Windows. In it no note needs a bend, unlike the
// embedded just-intonation default, where D4 sounds as 64 with a bend.
static const char* MICRO_SCALE_12EDO_FILE =
	"! 12edo.scl\r\n!\r\n12-tone equal temperament\r\n 12\r\n!\r\n"
	" 100.0\r\n 200.0\r\n 300.0\r\n 400.0\r\n 500.0\r\n 600.0\r\n 700.0\r\n 800.0\r\n 900.0\r\n 1000.0\r\n 1100.0\r\n 2/1\r\n";

TEST_CASE("'Micro scale.js/.lua' loads a scale file from the context menu and can return to the default", "[MidiKit][MicroScale][ContextMenu]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	std::vector<Out> justD4 = {{0xe, 0, 79, 59, 0}, {0x9, 0, 64, 100, 0}};
	std::vector<Out> edoD4 = {{0x9, 0, 62, 100, 0}};

	// The embedded scale is the default.
	REQUIRE(onChannel0(kit.dispatch(noteOn(1, 62, 100))) == justD4);
	kit.dispatch(noteOff(1, 62));

	auto* e = m->host.getActiveEngine();
	e->invokeContextMenuCallback(kit.menuId("Load scale (.scl)..."),
		ScriptMenuClick::file(MICRO_SCALE_12EDO_FILE, "12edo.scl"));
	std::string log = drainLog(m);
	CATCH_INFO(log);
	REQUIRE(log.find("Scale loaded from 12edo.scl: 12 degrees per octave") != std::string::npos);
	REQUIRE(onChannel0(kit.dispatch(noteOn(1, 62, 100))) == edoD4);
	kit.dispatch(noteOff(1, 62));

	// A file without scale notes changes nothing.
	e->invokeContextMenuCallback(kit.menuId("Load scale (.scl)..."), ScriptMenuClick::file("just some text\n", "notes.txt"));
	REQUIRE(drainLog(m).find("No scale notes found in notes.txt") != std::string::npos);
	REQUIRE(onChannel0(kit.dispatch(noteOn(1, 62, 100))) == edoD4);
	kit.dispatch(noteOff(1, 62));

	// Back to the embedded scale.
	e->invokeContextMenuCallback(kit.menuId("Default scale"), ScriptMenuClick());
	REQUIRE(drainLog(m).find("Default scale: 7 degrees per octave") != std::string::npos);
	REQUIRE(onChannel0(kit.dispatch(noteOn(1, 62, 100))) == justD4);
}

TEST_CASE("'Micro scale.js/.lua' keeps a loaded scale in the patch until the default is chosen again", "[MidiKit][MicroScale][JSON]") {
	std::string path = GENERATE(presetPaths("Micro scale"));
	CATCH_INFO("preset: " << path);

	std::vector<Out> justD4 = {{0xe, 0, 79, 59, 0}, {0x9, 0, 64, 100, 0}};
	std::vector<Out> edoD4 = {{0x9, 0, 62, 100, 0}};

	Kit<> kit;
	MidiKitModule* m = kit.loadPreset(path).m;
	m->host.getActiveEngine()->invokeContextMenuCallback(kit.menuId("Load scale (.scl)..."),
		ScriptMenuClick::file(MICRO_SCALE_12EDO_FILE, "12edo.scl"));
	drainLog(m);

	// Saved and restored into a new module: the file's scale, not the embedded one.
	json_t* rootJ = m->dataToJson();
	Kit<> kit2;
	MidiKitModule* m2 = kit2.m;
	m2->dataFromJson(rootJ);
	drainLog(m2);
	REQUIRE(onChannel0(kit2.dispatch(noteOn(1, 62, 100))) == edoD4);

	// After "Default scale" nothing of the file is stored any more.
	m2->host.getActiveEngine()->invokeContextMenuCallback(kit2.menuId("Default scale"), ScriptMenuClick());
	drainLog(m2);
	json_decref(rootJ);
	rootJ = m2->dataToJson();
	Kit<> kit3;
	MidiKitModule* m3 = kit3.m;
	m3->dataFromJson(rootJ);
	json_decref(rootJ);
	drainLog(m3);
	REQUIRE(onChannel0(kit3.dispatch(noteOn(1, 62, 100))) == justD4);
}


// ── Timing-mode presets, driven with real input frames ──────────────────────
// The preset tests above feed messages straight into the
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

TEST_CASE("Timing-mode preset 'Bouncing ball delay': echoes are placed from the note's frame", "[MidiKit][Timing][preset]") {
	FOR_EACH_LANG;
	const char* path = Pair{"JavaScript/creative/Bouncing ball delay.js", "Lua/creative/Bouncing ball delay.lua"}.get(lang);
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
	REQUIRE(rig.rec.sent[0].frame == 100);
	// The first echo: initialInterval (250 ms) after the note's own frame.
	int64_t expected = 100 + int64_t(std::llround(0.250 * Test::sampleRate()));
	REQUIRE(rig.rec.sent.size() > 2);
	REQUIRE(std::llabs(rig.rec.sent[1].frame - expected) <= 1);
	// The echoes continue and settle: every later echo is a Note-On with lower velocity.
	int lastVelocity = rig.rec.sent[0].value;
	for (const Out& e : rig.rec.sent) {
		if (e.status != 0x9) continue;
		REQUIRE(e.value <= lastVelocity);
		lastVelocity = e.value;
	}
}

TEST_CASE("Timing-mode preset 'Clock multiplier': one pulse on each edge, the rest spread evenly", "[MidiKit][Timing][preset]") {
	FOR_EACH_LANG;
	const char* path = Pair{"JavaScript/Clock multiplier.js", "Lua/Clock multiplier.lua"}.get(lang);
	CATCH_INFO(path);
	std::string source = presetSource(path);
	TimingRig rig(source.c_str());

	rig.run(100);
	std::vector<int64_t> edges = clockEdges(rig, 3, 240);
	rig.run(edges.back() + 240);

	REQUIRE(edges == std::vector<int64_t>({ 100, 340, 580 }));
	requireOrderedFrames(rig.rec);
	std::vector<int64_t> frames;
	for (const Out& e : rig.rec.sent) {
		REQUIRE(e.status == 0xf);
		frames.push_back(e.frame);
	}
	// Edge 1 has no period yet: its own pulse only. Edge 2 sends its pulse and
	// 23 more, 240 / 24 = 10 frames apart. Edge 3 sends its own pulse on its
	// frame, and 23 more.
	std::vector<int64_t> expected = { 100 };
	for (int k = 0; k < 24; k++) expected.push_back(340 + 10 * k);
	for (int k = 0; k < 24; k++) expected.push_back(580 + 10 * k);
	REQUIRE(frames == expected);
}


// The basic delay demos delay the release with the note, in every encoding of it,
// so a keyboard that sends Note-Offs does not leave notes hanging.
TEST_CASE("The basic delay presets delay Note-Offs and velocity-0 releases with their notes", "[MidiKit][Release]") {
	const char* name = GENERATE("Delay NoteOn Ch1 for two 1500ms", "Delay NoteOn Ch1 for two clock ticks");
	std::string path = GENERATE_COPY(presetPaths(name));
	bool byTicks = std::strstr(name, "ticks") != nullptr;
	CATCH_INFO("preset: " << path);

	std::vector<midi::Message> delayed = { noteOn(0, 60, 100), noteOff(0, 60), noteOn(0, 60, 0) };
	for (const midi::Message& in : delayed) {
		CATCH_INFO("status " << int(in.getStatus()) << " value " << int(in.getValue()));
		Kit<> kit;
		kit.loadPreset(path);
		std::vector<Out> sent = kit.dispatch(in);
		REQUIRE(sent.size() == 1);
		REQUIRE(sent[0].note == 60);
		// Held back, not sent at once.
		if (byTicks) REQUIRE(sent[0].ticks == 2);
		else REQUIRE(sent[0].frame == int64_t(1.5 * Test::sampleRate()));
	}

	// Other channels are not delayed and not passed through.
	Kit<> kit;
	kit.loadPreset(path);
	REQUIRE(kit.dispatch(noteOn(1, 60, 100)).empty());
	REQUIRE(kit.dispatch(noteOff(1, 60)).empty());
}
