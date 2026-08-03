#include "MidiKit.test.hpp"
#include <fstream>
#include <sstream>

// Smoke test for the shipped example presets in presets/MidiKit/.
//
// These scripts are only ever exercised by loading them in Rack, so nothing
// else in the suite would notice if one stopped working — and both engines fail
// in ways that are easy to miss by reading: a script with an unsupported
// construct can load cleanly, report "Script loaded", and then error out on
// every single MIDI message. See project SCRIPTING.md ("QuickJs language
// limitations"); the boolean === / !== entry there was found by this test.
//
// Each preset is loaded into a real module and fed representative traffic, then
// checked for three things: nothing error-shaped in the load log, nothing
// error-shaped in the per-message log, and at least one MIDI message actually
// produced. The last check matters — without it a script whose onMidiMessage
// never runs at all passes both error checks trivially.

// Presets are read from disk, so the paths must not depend on the working
// directory the test binary happens to be started from.
//
// __FILE__ is whatever path the compiler was handed, and the test Makefile rule
// passes "$<" — a path relative to the repo root, not an absolute one. So this
// strips the known "src/modules/midikit/<file>" suffix rather than counting
// slashes: counting four levels up from a relative __FILE__ overshoots and
// yields "src", which is how this used to look for src/presets/MidiKit/...
// An empty result means __FILE__ was already repo-root-relative, in which case
// "." (the directory make runs from) is correct.
static std::string repoRoot() {
	static const std::string suffix = "src/modules/midikit/";
	std::string f = __FILE__;
	size_t at = f.rfind(suffix);
	std::string root = (at == std::string::npos) ? "" : f.substr(0, at);
	while (root.size() > 1 && root.back() == '/') root.pop_back();
	return root.empty() ? "." : root;
}

static std::string readFile(const std::string& path) {
	std::ifstream f(path);
	CATCH_INFO("cannot open " << path);
	REQUIRE(f.good());
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// processInMessage only queues the message — process() is what actually runs
// onMidiMessage(), so both are needed or the script never executes at all.
static void feed(MidiKitModule* m, midi::Message msg) {
	m->activeEngine->processInMessage(0, msg);
	m->activeEngine->process();
}

static midi::Message noteOn(int ch, int note, int vel) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(vel);
	return msg;
}

static midi::Message noteOff(int ch, int note) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x8);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(0);
	return msg;
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

// One trigger-input tick, draining whatever the callback sent into a flat
// list of decoded (status-nibble, channel, note, value) events, in order.
struct NoteEvent {
	uint8_t status;  // 0x9 = Note-On, 0x8 = Note-Off
	uint8_t channel; // 1-based
	uint8_t note;
	uint8_t value;
};

static std::vector<NoteEvent> feedTick(MidiKitModule* m) {
	m->activeEngine->processInTick(0);
	m->activeEngine->process();

	std::vector<NoteEvent> events;
	int port, ticks;
	midi::Message out;
	while (m->activeEngine->processOutMessage(port, out, ticks)) {
		events.push_back({out.getStatus(), out.getChannel(), out.getNote(), out.getValue()});
	}
	return events;
}

// Like NoteEvent but also carries the engine's tick-scheduling value for the
// message: 0 = send immediately, N = send once the trigger-input tick counter
// reaches N. The note-length quantiser schedules its Note-Offs through
// midiOut.sendAfterTrigger(), so its tests need this to assert the note is
// cut exactly config.lengthTicks ticks after the Note-On - the tick value the
// engine stamps on the message is the only way to observe the scheduling.
struct OutEvent {
	uint8_t status;  // status nibble: 0x9 = Note-On, 0x8 = Note-Off, 0xb = CC, 0xf = realtime
	uint8_t channel; // Rack's internal 0-based channel; the script's 1-based channel is this + 1
	uint8_t note;
	uint8_t value;
	int ticks;
	bool operator==(const OutEvent& o) const {
		return status == o.status && channel == o.channel && note == o.note && value == o.value && ticks == o.ticks;
	}
};

// Let Catch2 print OutEvent contents in assertion expansions (otherwise the
// "with expansion" line just shows { {?} } and hides the actual message).
namespace Catch {
	template<> struct StringMaker<OutEvent> {
		static std::string convert(OutEvent const& e) {
			std::ostringstream os;
			os << "0x" << std::hex << (int)e.status << std::dec
			   << " ch=" << (int)e.channel
			   << " n=" << (int)e.note
			   << " v=" << (int)e.value
			   << " t=" << e.ticks;
			return os.str();
		}
	};
}

// Drains an engine's out-queue into OutEvents. Also used after loadScript("")
// to read the messages onUnload() queued into the engine that was active
// before the reload.
static std::vector<OutEvent> drainOut(MidiScriptEngine* engine) {
	std::vector<OutEvent> events;
	int port, ticks;
	midi::Message out;
	while (engine->processOutMessage(port, out, ticks)) {
		events.push_back({out.getStatus(), out.getChannel(), out.getNote(), out.getValue(), ticks});
	}
	return events;
}

// feed() plus a drain of the engine's out-queue, decoded into OutEvents. The
// behavioural preset tests below need the actual outgoing messages (and their
// tick scheduling), not just "the script ran without erroring".
static std::vector<OutEvent> feedCollect(MidiKitModule* m, midi::Message msg) {
	m->activeEngine->processInMessage(0, msg);
	m->activeEngine->process();
	return drainOut(m->activeEngine);
}


static void checkPreset(const std::string& relPath) {
	CATCH_INFO("preset: " << relPath);
	MidiKitModule* m = createModule();
	m->loadScript(readFile(repoRoot() + "/" + relPath));

	std::string loadLog = drainLog(m);
	CATCH_INFO("load log:\n" << loadLog);
	REQUIRE(loadLog.find("rror") == std::string::npos);
	REQUIRE(loadLog.find("not compatible") == std::string::npos);
	REQUIRE(loadLog.find("Script loaded") != std::string::npos);

	// Representative traffic: notes on two channels (so channel filters and
	// MPE member-channel handling both see something), a full release, and
	// enough clock ticks to drive the clock-counting scripts past a step.
	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(2, 64, 40));
	feed(m, noteOff(1, 60));
	feed(m, noteOff(2, 64));
	for (int i = 0; i < 32; i++) feed(m, clockTick());

	// A complete NRPN write (parameter 1, an entry the NRPN preset maps) plus a
	// plain CC. Note/clock traffic alone leaves the CC-driven presets silent, so
	// without this the "produced output" check below cannot pass for them.
	feed(m, cc(1, 99, 0));    // parameter number MSB
	feed(m, cc(1, 98, 1));    // parameter number LSB
	feed(m, cc(1, 6, 64));    // data entry MSB
	feed(m, cc(1, 38, 0));    // data entry LSB
	feed(m, cc(1, 20, 100));

	std::string runLog = drainLog(m);
	CATCH_INFO("runtime log:\n" << runLog);
	REQUIRE(runLog.find("rror") == std::string::npos);

	// Every one of these presets emits something for the traffic above.
	// Without this the log checks would also pass for a script that never ran.
	int outPort = 0, outTicks = 0;
	midi::Message outMsg;
	REQUIRE(m->activeEngine->processOutMessage(outPort, outMsg, outTicks));

	Test::destroyModule(m);
}

// Presets carrying real logic. The trivial ones (PassThrough, Filter Ch2, ...)
// are deliberately not listed: they are single-branch scripts with nothing to
// regress, and some drop everything by design, so the "produced output" check
// would not apply to them.
static const char* PRESETS[] = {
	"MPE to single channel",
	"Clock divider",
	"Note length quantiser",
	"Velocity curve",
	"Scale quantiser",
	"Chord harmonizer",
	"NRPN to CC",
	"Copy Ch1 CC to Ch2",
	"Rewrite Ch1 to Ch2",
	"Micro scale"
};

// GENERATE re-runs the body once per preset name, and Catch2 treats every
// generated value as its own leaf: a failure names the preset that broke and
// the others still run, instead of the whole engine stopping at the first bad
// script. Listing the names in the TEST_CASE is unavoidable — a TEST_CASE is
// registered at static-init time, so it cannot be produced per array element.
TEST_CASE("JavaScript preset loads and runs without errors", "[MidiKit][Presets]") {
	std::string name = GENERATE(from_range(std::begin(PRESETS), std::end(PRESETS)));
	checkPreset("presets/MidiKit/JavaScript/" + name + ".js");
}

TEST_CASE("Lua preset loads and runs without errors", "[MidiKit][Presets]") {
	std::string name = GENERATE(from_range(std::begin(PRESETS), std::end(PRESETS)));
	checkPreset("presets/MidiKit/Lua/" + name + ".lua");
}

// Behavioural tests for the Arpeggiator preset, beyond the generic smoke
// check above. The arp is clocked by onTrigger (the CV trigger input) rather
// than by incoming MIDI, and its four params (clock division, octave range,
// note length, playmode) are read live from the module's Param objects — so
// each case here sets params directly on the module, builds a held chord via
// Note-On/Note-Off, steps the clock via feedTick(), and asserts on the actual
// decoded note sequence and timing produced, rather than only "it ran without
// erroring".
static const char* ARPEGGIATOR_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Arpeggiator.js",
	"presets/MidiKit/Lua/Arpeggiator.lua"
};

// Loads a preset file into a fresh module and asserts it loaded cleanly (no
// error lines, and the "Script loaded" confirmation). Shared by all the
// behavioural preset tests.
static MidiKitModule* loadPreset(const std::string& relPath) {
	MidiKitModule* m = createModule();
	m->loadScript(readFile(repoRoot() + "/" + relPath));

	std::string loadLog = drainLog(m);
	CATCH_INFO("preset: " << relPath);
	CATCH_INFO("load log:\n" << loadLog);
	REQUIRE(loadLog.find("rror") == std::string::npos);
	REQUIRE(loadLog.find("Script loaded") != std::string::npos);
	return m;
}

// Loads one of the two Arpeggiator preset files and sets its four params by
// normalized 0..1 Param value - the same values a user would get from the
// panel knobs, so this exercises the same param.getValue() reads the script
// makes rather than reaching into script-internal state.
static MidiKitModule* loadArp(const std::string& relPath, float clockDivision, float octaveRange, float noteLength, float playmode) {
	MidiKitModule* m = loadPreset(relPath);
	m->params[MidiKitModule::PARAM + 0].setValue(clockDivision);
	m->params[MidiKitModule::PARAM + 1].setValue(octaveRange);
	m->params[MidiKitModule::PARAM + 2].setValue(noteLength);
	m->params[MidiKitModule::PARAM + 3].setValue(playmode);
	return m;
}

// --- Up mode, 1 tick/step, 1 octave: plain ascending replay of the held chord ---
TEST_CASE("'Arpeggiator.js/.lua' Up mode steps the held chord in press order", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// clockDivision=0 -> DIVISIONS[0]=1 tick/step, octaveRange=0 -> 1 octave,
	// playmode=0 -> Up.
	MidiKitModule* m = loadArp(path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 6; i++) {
		auto events = feedTick(m);
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	// Up mode over {60,64,67}: 60,64,67,60,64,67 - one step per tick, in
	// press order, cycling back to the start after the last note.
	std::vector<uint8_t> expected = {60, 64, 67, 60, 64, 67};
	REQUIRE(notesOn == expected);

	Test::destroyModule(m);
}

// --- Down mode: exact reverse of press order ---
TEST_CASE("'Arpeggiator.js/.lua' Down mode steps the held chord in reverse", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// playmode=0.99 -> last entry in PLAYMODES (Down is index 1 of 3, so 0.5).
	MidiKitModule* m = loadArp(path, 0.f, 0.f, 0.5f, 0.5f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 3; i++) {
		auto events = feedTick(m);
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	std::vector<uint8_t> expected = {67, 64, 60};
	REQUIRE(notesOn == expected);

	Test::destroyModule(m);
}

// --- Up-Down mode: ascends then descends without repeating the two end notes ---
TEST_CASE("'Arpeggiator.js/.lua' Up-Down mode does not repeat the end notes", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// playmode=0.99 -> last entry in PLAYMODES (Up-Down, index 2 of 3).
	MidiKitModule* m = loadArp(path, 0.f, 0.f, 0.5f, 0.99f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 8; i++) {
		auto events = feedTick(m);
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	// {60,64,67} up-down, without repeating 67 or 60 at the turnarounds:
	// 60,64,67,64,60,64,67,64
	std::vector<uint8_t> expected = {60, 64, 67, 64, 60, 64, 67, 64};
	REQUIRE(notesOn == expected);

	Test::destroyModule(m);
}

// --- Octave range doubles the pattern upward before it cycles ---
TEST_CASE("'Arpeggiator.js/.lua' octave range repeats the chord one octave higher", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// octaveRange=0.5 -> floor(0.5*4)+1 = 3 octaves, playmode=0 -> Up.
	MidiKitModule* m = loadArp(path, 0.f, 0.5f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 6; i++) {
		auto events = feedTick(m);
		for (auto& e : events) {
			if (e.status == 0x9) notesOn.push_back(e.note);
		}
	}

	std::vector<uint8_t> expected = {60, 64, 72, 76, 84, 88};
	REQUIRE(notesOn == expected);

	Test::destroyModule(m);
}

// --- Clock division: steps only advance every Nth trigger tick ---
TEST_CASE("'Arpeggiator.js/.lua' clock division holds the step across intermediate ticks", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// clockDivision index 3 -> DIVISIONS[3] = 4 ticks/step. 4 divisions span
	// indices [0.3, 0.4) of the 10-entry list, so 0.35 lands there reliably.
	MidiKitModule* m = loadArp(path, 0.35f, 0.f, 0.9f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	drainLog(m);

	int noteOnCount = 0;
	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 8; i++) {
		auto events = feedTick(m);
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

	Test::destroyModule(m);
}

// --- Note length: the scheduled Note-Off must always land before the next Note-On ---
TEST_CASE("'Arpeggiator.js/.lua' note length never overruns into the next step", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// noteLength=1.0 (full gate) at clockDivision index 0 -> 1 tick/step; the
	// script must clamp length to division-1 (>=1) rather than tying notes.
	MidiKitModule* m = loadArp(path, 0.f, 0.f, 1.0f, 0.f);

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
		auto events = feedTick(m);
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

	Test::destroyModule(m);
}

// --- Releasing all held notes stops the arp; no further Note-On is sent ---
TEST_CASE("'Arpeggiator.js/.lua' stops stepping once every note is released", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadArp(path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	drainLog(m);
	auto firstStep = feedTick(m);
	bool sawNoteOn = false;
	for (auto& e : firstStep) if (e.status == 0x9) sawNoteOn = true;
	REQUIRE(sawNoteOn);

	feed(m, noteOff(1, 60));

	bool sawAnyNoteOnAfterRelease = false;
	for (int i = 0; i < 4; i++) {
		auto events = feedTick(m);
		for (auto& e : events) if (e.status == 0x9) sawAnyNoteOnAfterRelease = true;
	}
	REQUIRE_FALSE(sawAnyNoteOnAfterRelease);

	Test::destroyModule(m);
}

// --- onUnload releases whatever note the arp is currently sustaining ---
TEST_CASE("'Arpeggiator.js/.lua' releases the sounding note on unload", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// clockDivision=0 -> 1 tick/step, so the first feedTick() already lands on
	// a step boundary; noteLength=1.0 (clamped to division-1, i.e. at least 1
	// tick) keeps the note sustained, not yet auto-released, when onUnload
	// fires immediately after.
	MidiKitModule* m = loadArp(path, 0.f, 0.f, 1.0f, 0.f);

	feed(m, noteOn(1, 60, 100));
	drainLog(m);
	auto stepEvents = feedTick(m);
	uint8_t soundingNote = 0;
	for (auto& e : stepEvents) if (e.status == 0x9) soundingNote = e.note;
	REQUIRE(soundingNote != 0);
	drainLog(m);

	// loadScript("") replaces the active engine's script, which synchronously
	// runs onUnload() on that same engine object and queues its output into
	// that engine's own midiOutQueue - so it must be drained from the engine
	// that was active *before* the reload, not from m->activeEngine (which by
	// this point may already point at a different engine, e.g. after a
	// language switch elsewhere in the suite; here it is the same object,
	// but reading it before reload keeps the test correct either way).
	MidiScriptEngine* engineBeforeUnload = m->activeEngine;
	m->loadScript("");

	int port, ticks;
	midi::Message out;
	bool sawMatchingNoteOff = false;
	while (engineBeforeUnload->processOutMessage(port, out, ticks)) {
		if (out.getStatus() == 0x8 && out.getNote() == soundingNote) sawMatchingNoteOff = true;
	}
	REQUIRE(sawMatchingNoteOff);

	Test::destroyModule(m);
}


// Behavioural tests for the Chord harmonizer preset. It reads no module
// params - config.intervals (the major triad [0,4,7]) and harmonyVelocity
// (0.8) are hardcoded in the shipped script - so these tests assert against
// those defaults. A single note must expand into three voices with the
// 0-offset at full velocity and the harmony voices scaled, the Note-Off must
// release exactly the voices that were started, and overlapping voices must
// not be released twice - the reference-counting is the whole point of the
// script and the part most likely to regress.
static const char* CHORD_HARMONIZER_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Chord harmonizer.js",
	"presets/MidiKit/Lua/Chord harmonizer.lua"
};

// --- [0,4,7] triad with 0.8 harmony velocity ---
TEST_CASE("'Chord harmonizer.js/.lua' expands a single note into a scaled triad", "[MidiKit][ChordHarmonizer]") {
	std::string path = GENERATE(from_range(std::begin(CHORD_HARMONIZER_PRESET_PATHS), std::end(CHORD_HARMONIZER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// C4 -> C4 (0-offset, full velocity), E4 (+4, floor(100*0.8+0.5)=80),
	// G4 (+7, 80).
	auto on = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(on == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}, {0x9, 1, 64, 80, 0}, {0x9, 1, 67, 80, 0}});

	// The Note-Off releases exactly those three voices, once each.
	auto off = feedCollect(m, noteOff(1, 60));
	REQUIRE(off == std::vector<OutEvent>{{0x8, 1, 60, 0, 0}, {0x8, 1, 64, 0, 0}, {0x8, 1, 67, 0, 0}});

	Test::destroyModule(m);
}

// --- reference-counting: two notes transposing onto the same target ---
TEST_CASE("'Chord harmonizer.js/.lua' releases a colliding voice exactly once", "[MidiKit][ChordHarmonizer]") {
	std::string path = GENERATE(from_range(std::begin(CHORD_HARMONIZER_PRESET_PATHS), std::end(CHORD_HARMONIZER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// C4's voices are {60,64,67}; A3's voices are {57,61,64}. Note 64 (E4) is
	// shared - C4's +4 and A3's +7 both land on it.
	feedCollect(m, noteOn(1, 60, 100));   // 60, 64, 67 down
	auto second = feedCollect(m, noteOn(1, 57, 100));
	// 64 is already sounding, so no second Note-On for it - only 57 and 61.
	REQUIRE(second == std::vector<OutEvent>{{0x9, 1, 57, 100, 0}, {0x9, 1, 61, 80, 0}});

	// Releasing C4 drops 60 and 67 but must leave 64 down (A3 still holds it).
	auto release60 = feedCollect(m, noteOff(1, 60));
	REQUIRE(release60 == std::vector<OutEvent>{{0x8, 1, 60, 0, 0}, {0x8, 1, 67, 0, 0}});

	// Releasing A3 finally lets 64 go - exactly once.
	auto release57 = feedCollect(m, noteOff(1, 57));
	REQUIRE(release57 == std::vector<OutEvent>{{0x8, 1, 57, 0, 0}, {0x8, 1, 61, 0, 0}, {0x8, 1, 64, 0, 0}});

	Test::destroyModule(m);
}

// --- onUnload releases every still-sounding voice ---
TEST_CASE("'Chord harmonizer.js/.lua' releases all sounding voices on unload", "[MidiKit][ChordHarmonizer]") {
	std::string path = GENERATE(from_range(std::begin(CHORD_HARMONIZER_PRESET_PATHS), std::end(CHORD_HARMONIZER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	feedCollect(m, noteOn(1, 60, 100));   // 60, 64, 67 all held

	MidiScriptEngine* engineBeforeUnload = m->activeEngine;
	m->loadScript("");

	// refCount 60/64/67 all > 0 -> released ascending. The script's onUnload
	// hard-codes the release to MIDI channel 1 (internal channel 0) because
	// refCount isn't channel-indexed - so this is not the 1-based channel the
	// Note-On went out on, but the fixed first channel.
	auto ev = drainOut(engineBeforeUnload);
	REQUIRE(ev == std::vector<OutEvent>{{0x8, 0, 60, 0, 0}, {0x8, 0, 64, 0, 0}, {0x8, 0, 67, 0, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Scale quantiser preset. The shipped default is C
// minor ({0,2,3,5,7,8,10} with root C) and preferUpward=false. Every
// out-of-scale note in a minor scale sits exactly halfway between two scale
// degrees, so with the default it always snaps down by a semitone - that
// uniform tie-break is exactly the behaviour worth pinning. The Note-Off
// rewrite (the release arrives with the *played* note, not the snapped one)
// and the onUnload release of the substituted note are the parts of the
// script most likely to regress.
static const char* SCALE_QUANTISER_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Scale quantiser.js",
	"presets/MidiKit/Lua/Scale quantiser.lua"
};

TEST_CASE("'Scale quantiser.js/.lua' passes in-scale notes through unchanged", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(from_range(std::begin(SCALE_QUANTISER_PRESET_PATHS), std::end(SCALE_QUANTISER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// C minor degrees: C D D# F G G# A# - each passes through note-for-note.
	// All seven are fed to one module in a single run: that also covers the
	// engine's GC boundary, since the 6th consecutive message through
	// quantise() is roughly where the engine's memory/GC limit first surfaces
	// mid-call. That used to surface as a dropped message with
	// "onMidiMessage error: ERROR: parse error".
	for (int note : {60, 62, 63, 65, 67, 68, 70}) {
		auto ev = feedCollect(m, noteOn(1, note, 100));
		REQUIRE(ev == std::vector<OutEvent>{{0x9, 1, static_cast<uint8_t>(note), 100, 0}});
	}

	Test::destroyModule(m);
}

TEST_CASE("'Scale quantiser.js/.lua' snaps off-scale notes to the nearest degree", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(from_range(std::begin(SCALE_QUANTISER_PRESET_PATHS), std::end(SCALE_QUANTISER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// C# E F# A B are each equidistant between two minor-scale degrees; the
	// default preferUpward=false rounds them all down by a semitone.
	std::vector<std::pair<int, int>> cases = {{61, 60}, {64, 63}, {66, 65}, {69, 68}, {71, 70}};
	for (auto& c : cases) {
		auto ev = feedCollect(m, noteOn(1, c.first, 100));
		REQUIRE(ev == std::vector<OutEvent>{{0x9, 1, static_cast<uint8_t>(c.second), 100, 0}});
	}

	Test::destroyModule(m);
}

TEST_CASE("'Scale quantiser.js/.lua' rewrites the Note-Off to the snapped note", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(from_range(std::begin(SCALE_QUANTISER_PRESET_PATHS), std::end(SCALE_QUANTISER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// E4 snaps to D#4...
	auto on = feedCollect(m, noteOn(1, 64, 100));
	REQUIRE(on == std::vector<OutEvent>{{0x9, 1, 63, 100, 0}});

	// ...so the Note-Off that arrives as 64 must be rewritten to release 63.
	auto off = feedCollect(m, noteOff(1, 64));
	REQUIRE(off == std::vector<OutEvent>{{0x8, 1, 63, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Scale quantiser.js/.lua' releases the substituted note on unload", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(from_range(std::begin(SCALE_QUANTISER_PRESET_PATHS), std::end(SCALE_QUANTISER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	feedCollect(m, noteOn(1, 64, 100));   // played as 63, still held

	MidiScriptEngine* engineBeforeUnload = m->activeEngine;
	m->loadScript("");

	// onUnload must release the *substituted* note (63), not the raw 64 -
	// releasing 64 would leave a hanging voice. As with Chord harmonizer, the
	// release goes out on the fixed MIDI channel 1 (internal channel 0).
	auto ev = drainOut(engineBeforeUnload);
	REQUIRE(ev == std::vector<OutEvent>{{0x8, 0, 63, 0, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Micro scale preset. The scale is not hardcoded:
// onLoad parses the Scala .scl pasted into config.scl (a "config.scl = ..."
// multi-line string in each preset). The shipped default is the 5-limit
// just-intonation major scale given as ratios (9/8, 5/4, 4/3, 3/2, 5/3, 15/8,
// 2/1) on baseNote 60 @ 261.625565 Hz (A440), bendDepth 2 semitones, output
// channels 1-8, alwaysSendBend=false. Each incoming note is retuned to its
// exact scale pitch, expressed as the nearest 12-EDO note number plus a pitch
// wheel for the residual cents, dispatched to its own output channel
// (round-robin) because pitch bend is channel-global. The expected values
// below are exact: for note 62 the scale says D sits at 386.3137... cents,
// i.e. the tuned frequency is exactly a 5/4 above the base (327.03 Hz); that
// is semis 63.86314, so the note sent is 64 and the residual -0.13686 st maps
// to pitch wheel 7631 (LSB 79, MSB 59). For note 61 it is a 9/8 above base:
// semis 62.03910 -> sent 62, residual +0.03910 st -> wheel 8352 (LSB 32, MSB
// 65). The tonic (note 60, 0 cents) sits exactly on the centre bend, so with
// alwaysSendBend=false no bend is emitted at all.

// Loads one of the two Micro scale presets but with the pasted .scl swapped
// for a caller-provided scale, so the scl parser is exercised with arbitrary
// content rather than only the shipped default. The .scl lives in a
// multi-line string assigned to "config.scl" (a backtick template literal in
// the JS preset, a [[ ]] long-bracket string in the Lua one); the content
// between the delimiters is replaced with `scl`.
static MidiKitModule* loadPresetWithScl(const std::string& relPath, const std::string& scl) {
	std::string script = readFile(repoRoot() + "/" + relPath);

	// Anchor on the assignment itself - "config.scl" also appears in comments
	// and log strings, and the Lua preset's config comment mentions literal
	// "[[ ... ]]" brackets, so a generic search would splice the scl into the
	// wrong place. Lua assigns with "scl = [[ ... ]]", JS with "scl: ` ... `".
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

	MidiKitModule* m = createModule();
	m->loadScript(script);

	std::string loadLog = drainLog(m);
	CATCH_INFO("preset: " << relPath);
	CATCH_INFO("load log:\n" << loadLog);
	REQUIRE(loadLog.find("rror") == std::string::npos);
	REQUIRE(loadLog.find("Script loaded") != std::string::npos);
	return m;
}

static const char* MICRO_SCALE_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Micro scale.js",
	"presets/MidiKit/Lua/Micro scale.lua"
};

TEST_CASE("'Micro scale.js/.lua' retunes a note and bends the residual cents", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// D4 (note 62) is a 5/4 above C4 in just intonation: it sounds as 64 with
	// a -0.13686 st bend. The pitch wheel is sent before the Note-On, because
	// the bend must be in place when the voice starts.
	auto ev = feedCollect(m, noteOn(1, 62, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0xe, 0, 79, 59, 0}, {0x9, 0, 64, 100, 0}});

	// Its Note-Off arrives as the *played* note 62 but must release the sent
	// note 64 on the same channel.
	auto off = feedCollect(m, noteOff(1, 62));
	REQUIRE(off == std::vector<OutEvent>{{0x8, 0, 64, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Micro scale.js/.lua' dispatches simultaneous notes to separate channels", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// C#4 (note 61) is a 9/8 above C4: it sounds as 62 with a +0.03910 st bend.
	// Both notes are held at once, so they must land on different channels
	// (1 then 2) - pitch bend is channel-global, so sharing a channel would
	// corrupt the other voice's tuning.
	auto c = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(c == std::vector<OutEvent>{{0x9, 0, 60, 100, 0}});

	auto cs = feedCollect(m, noteOn(1, 61, 100));
	REQUIRE(cs == std::vector<OutEvent>{{0xe, 1, 32, 65, 0}, {0x9, 1, 62, 100, 0}});

	// Each Note-Off releases exactly its own channel and sent note.
	auto cOff = feedCollect(m, noteOff(1, 60));
	REQUIRE(cOff == std::vector<OutEvent>{{0x8, 0, 60, 0, 0}});

	auto csOff = feedCollect(m, noteOff(1, 61));
	REQUIRE(csOff == std::vector<OutEvent>{{0x8, 1, 62, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Micro scale.js/.lua' sends no redundant bend for the tonic and releases it on unload", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// The tonic (note 60) is exactly on the centre bend 8192, so with
	// alwaysSendBend=false no pitch wheel precedes the Note-On.
	auto ev = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0x9, 0, 60, 100, 0}});

	// onUnload releases the still-held note.
	MidiScriptEngine* engineBeforeUnload = m->activeEngine;
	m->loadScript("");

	auto unload = drainOut(engineBeforeUnload);
	REQUIRE(unload == std::vector<OutEvent>{{0x8, 0, 60, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Micro scale.js/.lua' parses a pasted equal-temperament scl", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
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
	MidiKitModule* m = loadPresetWithScl(path, scl);

	// In 12-EDO every scale degree is a whole semitone, so no note needs
	// retuning: D4 (note 62) passes through as 62 with no pitch wheel.
	auto ev = feedCollect(m, noteOn(1, 62, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0x9, 0, 62, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Micro scale.js/.lua' parses a mixed scl with ratios, cents, comments and dropped entries", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// A deliberately convoluted .scl exercising every parser rule at once:
	// ratio notes and cents notes mixed, comment lines at the top AND between
	// notes, a stated note count that does not match the lines that follow
	// (the count is ignored), and entries that must be dropped - the tonic
	// "1/1" (0 cents, implicit), the octave "2/1" and "1200.0" (1200 cents,
	// implied by the scale repeating) and the bare integer "3" (a ratio of 3,
	// i.e. 1901.96 cents, outside the octave). The surviving scale has 8
	// degrees: 9/8, 193.0, 5/4, 4/3, 3/2, 5/3, 15/8.
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
	MidiKitModule* m = loadPresetWithScl(path, scl);

	// C#4 (note 61) lands on degree 1 = 9/8 (ratio branch): sent as 62 with a
	// +0.03910 st bend.
	auto cs = feedCollect(m, noteOn(1, 61, 100));
	REQUIRE(cs == std::vector<OutEvent>{{0xe, 0, 32, 65, 0}, {0x9, 0, 62, 100, 0}});

	// D4 (note 62) lands on degree 2 = 193.0 cents (cents branch): sent as 62
	// with a -0.07 st bend -> wheel 7905 (LSB 97, MSB 61). Both held notes
	// land on separate channels (1 then 2).
	auto d = feedCollect(m, noteOn(1, 62, 100));
	REQUIRE(d == std::vector<OutEvent>{{0xe, 1, 97, 61, 0}, {0x9, 1, 62, 100, 0}});

	// G#4 (note 68) is a full scale-octave up, so it wraps to degree 0 of the
	// next octave - exactly the tonic one octave higher, passing through as
	// 72 with no bend. Had the "2/1"/"1200.0" octave entries not been dropped
	// (they are implicit), the degree indexing would shift and this note would
	// not come out clean.
	auto gs = feedCollect(m, noteOn(1, 68, 100));
	REQUIRE(gs == std::vector<OutEvent>{{0x9, 2, 72, 100, 0}});

	// Each Note-Off releases its own channel and sent note.
	auto csOff = feedCollect(m, noteOff(1, 61));
	REQUIRE(csOff == std::vector<OutEvent>{{0x8, 0, 62, 0, 0}});
	auto dOff = feedCollect(m, noteOff(1, 62));
	REQUIRE(dOff == std::vector<OutEvent>{{0x8, 1, 62, 0, 0}});
	auto gsOff = feedCollect(m, noteOff(1, 68));
	REQUIRE(gsOff == std::vector<OutEvent>{{0x8, 2, 72, 0, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Note length quantiser preset. The shipped default
// is config.lengthTicks=12 counted on trigger input 1. Every Note-On is
// re-articulated immediately and a Note-Off is scheduled exactly 12 ticks
// later via midiOut.sendAfterTrigger(); the incoming Note-Off is discarded.
// inputTriggerTick only advances on a real trigger edge inside
// Module::process(), which these engine-level tests do not run, so the tests
// write the counter directly to simulate a clock that has already advanced -
// this also proves the scheduled tick is *relative* to the note-on's tick
// count rather than a fixed absolute tick.
static const char* NOTE_LENGTH_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Note length quantiser.js",
	"presets/MidiKit/Lua/Note length quantiser.lua"
};

TEST_CASE("'Note length quantiser.js/.lua' schedules the Note-Off lengthTicks after the Note-On", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(from_range(std::begin(NOTE_LENGTH_PRESET_PATHS), std::end(NOTE_LENGTH_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	m->inputTriggerTick = 40;

	// Note-On passes through, and its Note-Off is scheduled at 40 + 12.
	auto ev = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 52}});

	Test::destroyModule(m);
}

TEST_CASE("'Note length quantiser.js/.lua' drops the incoming Note-Off", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(from_range(std::begin(NOTE_LENGTH_PRESET_PATHS), std::end(NOTE_LENGTH_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	feedCollect(m, noteOn(1, 60, 100));

	// The player's own release is discarded - the scheduled one ends the note.
	auto ev = feedCollect(m, noteOff(1, 60));
	REQUIRE(ev.empty());

	Test::destroyModule(m);
}

TEST_CASE("'Note length quantiser.js/.lua' cuts a retriggered note before re-articulating", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(from_range(std::begin(NOTE_LENGTH_PRESET_PATHS), std::end(NOTE_LENGTH_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	m->inputTriggerTick = 40;
	feedCollect(m, noteOn(1, 60, 100));   // drains [on, off@52]; sounding[60] stays true

	// Retriggering 60 while it is still sounding cuts the old note immediately
	// (Note-Off, tick 0) so the re-articulation is clean, then sends the fresh
	// Note-On and its scheduled Note-Off. The engine flushes in send() order,
	// so the cut (sent first) comes out before the incoming Note-On (sent
	// second) and the scheduled Note-Off (sent last).
	auto ev = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0x8, 1, 60, 0, 0}, {0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 52}});

	Test::destroyModule(m);
}

TEST_CASE("'Note length quantiser.js/.lua' releases the sounding note on unload", "[MidiKit][NoteLength]") {
	std::string path = GENERATE(from_range(std::begin(NOTE_LENGTH_PRESET_PATHS), std::end(NOTE_LENGTH_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	feedCollect(m, noteOn(1, 60, 100));   // scheduled Note-Off not yet due

	MidiScriptEngine* engineBeforeUnload = m->activeEngine;
	m->loadScript("");

	// onUnload releases the still-sounding note on the fixed MIDI channel 1
	// (internal channel 0), same best-effort choice as the other presets.
	auto ev = drainOut(engineBeforeUnload);
	REQUIRE(ev == std::vector<OutEvent>{{0x8, 0, 60, 0, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Clock divider preset. The shipped default is
// config.divisor=6 with passthrough of non-clock messages. MIDI clock (0xF8)
// arrives on the MIDI input, not the trigger input, so these tests feed it
// via feedCollect() and assert on which realtime messages come back out: the
// division (only every 6th tick forwarded), the Start phase reset (so the
// divided clock always lands on the downbeat rather than wherever the last
// run left off), and the passthrough of unrelated messages. (The CV trigger
// output is not asserted here - it is a side effect on the module's pulse
// generator that only surfaces through Module::process(), which these
// engine-level tests do not run.)
static const char* CLOCK_DIVIDER_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Clock divider.js",
	"presets/MidiKit/Lua/Clock divider.lua"
};

TEST_CASE("'Clock divider.js/.lua' forwards only every divisor-th tick", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(from_range(std::begin(CLOCK_DIVIDER_PRESET_PATHS), std::end(CLOCK_DIVIDER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Realtime messages decode to status nibble 0xf. The first five clock
	// ticks are swallowed...
	for (int i = 0; i < 5; i++) {
		auto ev = feedCollect(m, clockTick());
		bool forwarded = false;
		for (auto& e : ev) if (e.status == 0xf) forwarded = true;
		REQUIRE_FALSE(forwarded);
	}

	// ...and only the sixth is forwarded.
	auto ev = feedCollect(m, clockTick());
	int forwarded = 0;
	for (auto& e : ev) if (e.status == 0xf) forwarded++;
	REQUIRE(forwarded == 1);

	Test::destroyModule(m);
}

TEST_CASE("'Clock divider.js/.lua' resets the phase on Start", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(from_range(std::begin(CLOCK_DIVIDER_PRESET_PATHS), std::end(CLOCK_DIVIDER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// 3 ticks before Start are swallowed, leaving tickCount = 3.
	for (int i = 0; i < 3; i++) feedCollect(m, clockTick());

	// Start itself is forwarded and resets the count back to 0.
	auto start = feedCollect(m, startMsg());
	bool startFwd = false;
	for (auto& e : start) if (e.status == 0xf) startFwd = true;
	REQUIRE(startFwd);

	// Without the reset the next tick would already reach the divisor; with
	// it, the phase restarts and the 6th tick after Start is the first one out.
	for (int i = 0; i < 5; i++) {
		auto ev = feedCollect(m, clockTick());
		bool fwd = false;
		for (auto& e : ev) if (e.status == 0xf) fwd = true;
		REQUIRE_FALSE(fwd);
	}
	auto ev = feedCollect(m, clockTick());
	int fwd = 0;
	for (auto& e : ev) if (e.status == 0xf) fwd++;
	REQUIRE(fwd == 1);

	Test::destroyModule(m);
}

TEST_CASE("'Clock divider.js/.lua' passes non-clock messages through unchanged", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(from_range(std::begin(CLOCK_DIVIDER_PRESET_PATHS), std::end(CLOCK_DIVIDER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// passThroughOther: a CC is forwarded untouched - only 0xF8 is thinned.
	auto ev = feedCollect(m, cc(1, 20, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0xb, 1, 20, 100, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the MPE to single channel preset. The shipped default
// is a Lower Zone: channel 1 is the master channel (passes through untouched),
// channels 2-16 are member channels, and everything member-channel is folded
// onto config.outChannel=1. Per-note pitch bend is quantised to semitones and
// folded into the note number (a semitone crossing re-articulates the note),
// and channel pressure / CC 74 are forwarded only for the member channel
// holding the most recently played note. These are the parts of the script
// most likely to regress, and none of them are exercised by the generic smoke
// check (which only asserts "it ran without erroring").
static const char* MPE_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/MPE to single channel.js",
	"presets/MidiKit/Lua/MPE to single channel.lua"
};

TEST_CASE("'MPE to single channel.js/.lua' rewrites member-channel notes to the output channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(from_range(std::begin(MPE_PRESET_PATHS), std::end(MPE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Note-On on member channel 2 is recreated on outChannel 1 (internal 0).
	auto on = feedCollect(m, noteOn(2, 60, 100));
	REQUIRE(on == std::vector<OutEvent>{{0x9, 0, 60, 100, 0}});

	// The Note-Off releases the same folded note on the output channel.
	auto off = feedCollect(m, noteOff(2, 60));
	REQUIRE(off == std::vector<OutEvent>{{0x8, 0, 60, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'MPE to single channel.js/.lua' passes the master channel through untouched", "[MidiKit][MPE]") {
	std::string path = GENERATE(from_range(std::begin(MPE_PRESET_PATHS), std::end(MPE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Channel 1 (internal 0) is the master channel - not a member channel - so
	// it is sent through as-is on its own channel (internal 0), not folded.
	auto on = feedCollect(m, noteOn(0, 60, 100));
	REQUIRE(on == std::vector<OutEvent>{{0x9, 0, 60, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'MPE to single channel.js/.lua' folds a semitone pitch bend into the note number", "[MidiKit][MPE]") {
	std::string path = GENERATE(from_range(std::begin(MPE_PRESET_PATHS), std::end(MPE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	feedCollect(m, noteOn(2, 60, 100));   // 60 sounding on member channel 2

	// A bend of +0.75 semitones (pitch wheel 8320, centre 8192, range 48)
	// rounds to +1 step, so the receiver re-articulates 60 as 61: release the
	// old note, play the new one at the script's fixed velocity 100.
	auto ev = feedCollect(m, pitchWheel(2, 8320));
	REQUIRE(ev == std::vector<OutEvent>{{0x8, 0, 60, 0, 0}, {0x9, 0, 61, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'MPE to single channel.js/.lua' forwards channel pressure only for the active channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(from_range(std::begin(MPE_PRESET_PATHS), std::end(MPE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Channel 2 is the most recently played member channel, so its pressure
	// is forwarded on the output channel.
	feedCollect(m, noteOn(2, 60, 100));
	auto p1 = feedCollect(m, chanPressure(2, 50));
	REQUIRE(p1 == std::vector<OutEvent>{{0xd, 0, 50, 0, 0}});

	// Playing a note on channel 3 makes it the active channel; pressure on the
	// now-inactive channel 2 is dropped, while channel 3's is forwarded.
	feedCollect(m, noteOn(3, 64, 100));
	REQUIRE(feedCollect(m, chanPressure(2, 60)).empty());
	auto p3 = feedCollect(m, chanPressure(3, 70));
	REQUIRE(p3 == std::vector<OutEvent>{{0xd, 0, 70, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'MPE to single channel.js/.lua' forwards CC 74 only for the active channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(from_range(std::begin(MPE_PRESET_PATHS), std::end(MPE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	feedCollect(m, noteOn(2, 60, 100));   // active channel 2
	auto c1 = feedCollect(m, cc(2, 74, 40));
	REQUIRE(c1 == std::vector<OutEvent>{{0xb, 0, 74, 40, 0}});

	// Channel 3 becomes active; CC 74 on channel 2 is dropped, on 3 forwarded.
	feedCollect(m, noteOn(3, 64, 100));
	REQUIRE(feedCollect(m, cc(2, 74, 30)).empty());
	auto c3 = feedCollect(m, cc(3, 74, 60));
	REQUIRE(c3 == std::vector<OutEvent>{{0xb, 0, 74, 60, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'MPE to single channel.js/.lua' forwards other CCs on a member channel to the output channel", "[MidiKit][MPE]") {
	std::string path = GENERATE(from_range(std::begin(MPE_PRESET_PATHS), std::end(MPE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// A non-74 CC on a member channel is forwarded on the output channel.
	auto ev = feedCollect(m, cc(2, 20, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0xb, 0, 20, 100, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Velocity curve preset. The shipped default is
// config.minVelocity=1, maxVelocity=127, curveAmount=2, channel=0 (every
// channel), with the curve shape read live from panel param 1. At knob 0.5
// the curve is linear, so velocity passes through unchanged; velocity 0 (the
// running-status spelling of a Note-Off) must always be left alone; and
// non-note messages must pass through untouched.
static const char* VELOCITY_CURVE_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Velocity curve.js",
	"presets/MidiKit/Lua/Velocity curve.lua"
};

TEST_CASE("'Velocity curve.js/.lua' passes velocity through unchanged at the linear knob", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(from_range(std::begin(VELOCITY_CURVE_PRESET_PATHS), std::end(VELOCITY_CURVE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	// knob 0.5 -> curve 0 -> identity mapping.
	m->params[MidiKitModule::PARAM + 0].setValue(0.5f);

	// The script passes the message through on its own channel (internal 1).
	auto ev = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Velocity curve.js/.lua' leaves velocity 0 untouched even off-linear", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(from_range(std::begin(VELOCITY_CURVE_PRESET_PATHS), std::end(VELOCITY_CURVE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	// A strongly non-linear knob would reshape any real velocity, but a
	// Note-On with velocity 0 is a Note-Off in disguise and must pass through.
	m->params[MidiKitModule::PARAM + 0].setValue(1.0f);

	auto ev = feedCollect(m, noteOn(1, 60, 0));
	REQUIRE(ev == std::vector<OutEvent>{{0x9, 1, 60, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Velocity curve.js/.lua' passes non-note messages through unchanged", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(from_range(std::begin(VELOCITY_CURVE_PRESET_PATHS), std::end(VELOCITY_CURVE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	auto ev = feedCollect(m, cc(1, 20, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0xb, 1, 20, 100, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the NRPN to CC preset. The shipped config.map maps
// NRPN 0->CC 0, 1->CC 1, 2->CC 2 on ccChannel 1. A full NRPN write is four CCs
// (99/98 number MSB/LSB, 6/38 value MSB/LSB); once all four arrive the 14-bit
// value is emitted as a 14-bit CC pair (CC n = MSB, CC n+32 = LSB). Unmapped
// NRPN numbers are ignored, and nothing is emitted until all four bytes are
// present.
static const char* NRPN_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/NRPN to CC.js",
	"presets/MidiKit/Lua/NRPN to CC.lua"
};

TEST_CASE("'NRPN to CC.js/.lua' converts a mapped NRPN to a 14-bit CC pair", "[MidiKit][NRPN]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_PRESET_PATHS), std::end(NRPN_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// NRPN 1 (number MSB 0, LSB 1) with value 8192 (value MSB 64, LSB 0).
	// Mapped to CC 1: CC 1 = MSB 64, CC 33 = LSB 0, both on channel 1 (int 0).
	feedCollect(m, cc(1, 99, 0));
	feedCollect(m, cc(1, 98, 1));
	feedCollect(m, cc(1, 6, 64));
	auto ev = feedCollect(m, cc(1, 38, 0));
	REQUIRE(ev == std::vector<OutEvent>{{0xb, 0, 1, 64, 0}, {0xb, 0, 33, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'NRPN to CC.js/.lua' ignores unmapped NRPN numbers", "[MidiKit][NRPN]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_PRESET_PATHS), std::end(NRPN_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// NRPN 5 is not in config.map, so the complete write produces nothing.
	feedCollect(m, cc(1, 99, 0));
	feedCollect(m, cc(1, 98, 5));
	feedCollect(m, cc(1, 6, 64));
	auto ev = feedCollect(m, cc(1, 38, 0));
	REQUIRE(ev.empty());

	Test::destroyModule(m);
}

TEST_CASE("'NRPN to CC.js/.lua' emits nothing until all four bytes arrive", "[MidiKit][NRPN]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_PRESET_PATHS), std::end(NRPN_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Only three of the four bytes - the value LSB is missing.
	feedCollect(m, cc(1, 99, 0));
	feedCollect(m, cc(1, 98, 1));
	auto ev = feedCollect(m, cc(1, 6, 64));
	REQUIRE(ev.empty());

	Test::destroyModule(m);
}


// Behavioural tests for the Copy Ch1 CC to Ch2 preset. It duplicates every CC
// on channel 1 onto channel 2 (the copy is sent first, then the original),
// and leaves everything else untouched.
static const char* COPY_CC_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Copy Ch1 CC to Ch2.js",
	"presets/MidiKit/Lua/Copy Ch1 CC to Ch2.lua"
};

TEST_CASE("'Copy Ch1 CC to Ch2.js/.lua' duplicates a channel-1 CC onto channel 2", "[MidiKit][CopyCC]") {
	std::string path = GENERATE(from_range(std::begin(COPY_CC_PRESET_PATHS), std::end(COPY_CC_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// A CC on MIDI channel 1 (internal 0) is copied to channel 2 (internal 1).
	// The script calls send(copy) before send(original), and the engine now
	// flushes in send() order, so the copy on channel 2 comes out first, then
	// the original on channel 1.
	auto ev = feedCollect(m, cc(0, 20, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0xb, 1, 20, 100, 0}, {0xb, 0, 20, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Copy Ch1 CC to Ch2.js/.lua' does not duplicate CCs on other channels", "[MidiKit][CopyCC]") {
	std::string path = GENERATE(from_range(std::begin(COPY_CC_PRESET_PATHS), std::end(COPY_CC_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// A CC on MIDI channel 2 (internal 1) is only forwarded on its own channel
	// - no copy.
	auto ev = feedCollect(m, cc(1, 20, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0xb, 1, 20, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Copy Ch1 CC to Ch2.js/.lua' does not duplicate non-CC messages on channel 1", "[MidiKit][CopyCC]") {
	std::string path = GENERATE(from_range(std::begin(COPY_CC_PRESET_PATHS), std::end(COPY_CC_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// A Note-On on MIDI channel 1 (internal 0) is not a CC, so it is only
	// forwarded once on its own channel.
	auto ev = feedCollect(m, noteOn(0, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0x9, 0, 60, 100, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Rewrite Ch1 to Ch2 preset. It rewrites every
// message on channel 1 to channel 2 and leaves all other channels alone.
static const char* REWRITE_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Rewrite Ch1 to Ch2.js",
	"presets/MidiKit/Lua/Rewrite Ch1 to Ch2.lua"
};

TEST_CASE("'Rewrite Ch1 to Ch2.js/.lua' rewrites channel-1 messages to channel 2", "[MidiKit][Rewrite]") {
	std::string path = GENERATE(from_range(std::begin(REWRITE_PRESET_PATHS), std::end(REWRITE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Note-On and CC on MIDI channel 1 (internal 0) both come out on channel 2
	// (internal 1).
	auto note = feedCollect(m, noteOn(0, 60, 100));
	REQUIRE(note == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}});
	auto ccEv = feedCollect(m, cc(0, 20, 100));
	REQUIRE(ccEv == std::vector<OutEvent>{{0xb, 1, 20, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Rewrite Ch1 to Ch2.js/.lua' leaves other channels unchanged", "[MidiKit][Rewrite]") {
	std::string path = GENERATE(from_range(std::begin(REWRITE_PRESET_PATHS), std::end(REWRITE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Channel 3 (internal 2) and channel 2 (internal 1) are untouched.
	auto note = feedCollect(m, noteOn(3, 60, 100));
	REQUIRE(note == std::vector<OutEvent>{{0x9, 3, 60, 100, 0}});
	auto ccEv = feedCollect(m, cc(2, 20, 100));
	REQUIRE(ccEv == std::vector<OutEvent>{{0xb, 2, 20, 100, 0}});

	Test::destroyModule(m);
}
