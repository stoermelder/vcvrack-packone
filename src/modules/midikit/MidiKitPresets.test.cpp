#include "MidiKit.test.hpp"
#include <fstream>
#include <sstream>

using StoermelderPackOne::MidiScript::ContextMenuSpec;

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
	"NRPN Generator",
	"Copy Ch1 CC to Ch2",
	"Rewrite Ch1 to Ch2",
	"Micro scale",
	"Euclidean rhythm generator",
	"Keyboard split",
	"Bouncing ball delay",
	"Gravity well"
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


// --- Dynamic chords: adding or removing a held note mid-arp must rebuild the
// pattern so the change takes effect on the next step, without corrupting the
// step position. Note that rebuildPattern() only resets state.step when it has
// run past the end of the (possibly shrunken) pattern: adding a note keeps the
// current step and folds the new note in at its press-order position, while
// removing a note can clamp the step back to 0. These two cases pin the
// difference. ---
TEST_CASE("'Arpeggiator.js/.lua' folds a note added mid-arp into the pattern from the current step", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// 1 tick/step, 1 octave, Up mode.
	MidiKitModule* m = loadArp(path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	drainLog(m);

	// Step once: pattern [60,64] plays 60, step advances to 1.
	auto first = feedTick(m);
	bool saw60 = false;
	for (auto& e : first) if (e.status == 0x9 && e.note == 60) saw60 = true;
	REQUIRE(saw60);
	drainLog(m);

	// Add 67 mid-arp. rebuildPattern() makes the pattern [60,64,67] but does
	// NOT reset the step - it stays at 1 (within the new length), so 64 still
	// plays before the newly added 67, which joins at its press-order position.
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 4; i++) {
		auto events = feedTick(m);
		for (auto& e : events) if (e.status == 0x9) notesOn.push_back(e.note);
	}

	// Continuing from step 1 of the rebuilt [60,64,67] pattern.
	std::vector<uint8_t> expected = {64, 67, 60, 64};
	REQUIRE(notesOn == expected);

	Test::destroyModule(m);
}

TEST_CASE("'Arpeggiator.js/.lua' clamps the step when a note removed mid-arp shrinks the pattern", "[MidiKit][Arpeggiator]") {
	std::string path = GENERATE(from_range(std::begin(ARPEGGIATOR_PRESET_PATHS), std::end(ARPEGGIATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadArp(path, 0.f, 0.f, 0.5f, 0.f);

	feed(m, noteOn(1, 60, 100));
	feed(m, noteOn(1, 64, 100));
	feed(m, noteOn(1, 67, 100));
	drainLog(m);

	// Advance the step twice: plays 60 then 64, leaving step at 2.
	feedTick(m);
	feedTick(m);
	drainLog(m);

	// Remove 60 mid-arp: held becomes [64,67], so the rebuilt pattern is
	// [64,67] and step 2 is now past its end - rebuildPattern() clamps it to 0
	// and the next step starts the shrunken pattern from the beginning.
	feed(m, noteOff(1, 60));
	drainLog(m);

	std::vector<uint8_t> notesOn;
	for (int i = 0; i < 3; i++) {
		auto events = feedTick(m);
		for (auto& e : events) if (e.status == 0x9) notesOn.push_back(e.note);
	}

	std::vector<uint8_t> expected = {64, 67, 64};
	REQUIRE(notesOn == expected);

	Test::destroyModule(m);
}


// Behavioural tests for the Euclidean rhythm generator preset. It is clocked
// by the trigger input (not by MIDI), rebuilding its pattern live from the
// four panel params: Steps (1-16), Fills (0..Steps), Note (0-127), Velocity
// (1-127). Each trigger tick advances one step and, on a hit, fires a Note-On
// that sustains until the next trigger tick (a one-step gate). The pattern is
// the canonical Bjorklund distribution - 4 steps / 2 fills is [0,1,0,1], so
// hits land on steps 1 and 3 of every bar. Everything arriving on MIDI IN is
// passed through unchanged.
static const char* EUCLID_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Euclidean rhythm generator.js",
	"presets/MidiKit/Lua/Euclidean rhythm generator.lua"
};

TEST_CASE("'Euclidean rhythm generator.js/.lua' fires a note on each Euclidean hit", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// steps=4, fills=2 -> pattern [0,1,0,1] (hits on steps 1 and 3), note 64,
	// velocity 33, output channel 1.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	// Two bars (8 ticks): hits land on ticks 2,4,6,8 (steps 1,3,1,3). The rest
	// ticks emit no Note-On.
	std::vector<int> hitTicks;
	for (int i = 1; i <= 8; i++) {
		auto events = feedTick(m);
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

	Test::destroyModule(m);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' passes MIDI in through unchanged", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// The script is a pure generator: whatever arrives on MIDI IN is forwarded
	// untouched on its own channel.
	auto ccEv = feedCollect(m, cc(1, 20, 100));
	REQUIRE(ccEv == std::vector<OutEvent>{{0xb, 1, 20, 100, 0}});
	auto note = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(note == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' plays the canonical 5-in-8 pattern", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// steps=8, fills=5 -> the canonical Bjorklund distribution
	// [1,0,1,1,0,1,1,0], hits on steps 0,2,3,5,6. This pins the algorithm
	// beyond the trivially symmetric 4-in-2 case.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(7.0f / 15.0f);  // 8 steps
	m->params[MidiKitModule::PARAM + 1].setValue(5.0f / 8.0f);   // 5 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);          // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);         // velocity 33
	drainLog(m);

	// One bar (8 ticks): hits on ticks 1,3,4,6,7.
	std::vector<int> hitTicks;
	for (int i = 1; i <= 8; i++) {
		auto events = feedTick(m);
		bool hit = false;
		for (auto& e : events) if (e.status == 0x9) hit = true;
		if (hit) hitTicks.push_back(i);
	}
	std::vector<int> expected = {1, 3, 4, 6, 7};
	REQUIRE(hitTicks == expected);

	Test::destroyModule(m);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' fills 0 silences and fills == steps fires every step", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// fills = 0 -> the pattern is all rests: no Note-On across a full bar.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.0f);   // 0 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);
	for (int i = 0; i < 8; i++) {
		auto events = feedTick(m);
		for (auto& e : events) REQUIRE_FALSE(e.status == 0x9);
	}
	Test::destroyModule(m);

	// fills = steps -> every step is a hit.
	MidiKitModule* m2 = loadPreset(path);
	m2->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m2->params[MidiKitModule::PARAM + 1].setValue(1.0f);   // 4 fills
	m2->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m2->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m2);
	for (int i = 0; i < 4; i++) {
		auto events = feedTick(m2);
		bool hit = false;
		for (auto& e : events) if (e.status == 0x9) hit = true;
		REQUIRE(hit);
	}
	Test::destroyModule(m2);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' gates each hit for exactly one step", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// steps=4, fills=2 -> [0,1,0,1]. A Note-On on a hit tick must be released
	// by the very next trigger tick (one-step gate) - the Note-Off comes out
	// even though the next step is a rest.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	// tick 1 (rest): nothing sounds, nothing to release.
	REQUIRE(feedTick(m).empty());

	// tick 2 (hit): a Note-On goes out.
	auto t2 = feedTick(m);
	bool on = false;
	for (auto& e : t2) if (e.status == 0x9) on = true;
	REQUIRE(on);

	// tick 3 (rest): the tick-2 note is released - a matching Note-Off.
	auto t3 = feedTick(m);
	bool off = false;
	for (auto& e : t3) if (e.status == 0x8 && e.note == 64) off = true;
	REQUIRE(off);

	Test::destroyModule(m);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' output channel menu changes the note channel", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33

	// "Output channel" option index 1 -> MIDI channel 2 (internal 1).
	std::vector<ContextMenuSpec> specs;
	m->activeEngine->getContextMenus([&specs](const std::vector<ContextMenuSpec>& s) { specs = s; });
	REQUIRE(specs.size() == 1);
	REQUIRE(specs[0].label == "Output channel");
	m->activeEngine->invokeContextMenuCallback(specs[0].callbackId, 1);
	drainLog(m);

	// The first hit (tick 2) goes out on the new channel.
	feedTick(m);   // tick 1: rest
	auto t2 = feedTick(m);
	bool onCh2 = false;
	for (auto& e : t2) if (e.status == 0x9 && e.channel == 1) onCh2 = true;
	REQUIRE(onCh2);

	Test::destroyModule(m);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' releases the sounding note on unload", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// fills = steps so the first trigger tick already fires a note, which is
	// still sounding when the script is unloaded immediately after.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);   // every step hits
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	feedTick(m);
	drainLog(m);

	MidiScriptEngine* engineBeforeUnload = m->activeEngine;
	m->loadScript("");

	// onUnload releases the still-sounding note on output channel 1
	// (internal 0).
	auto ev = drainOut(engineBeforeUnload);
	REQUIRE(ev == std::vector<OutEvent>{{0x8, 0, 64, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Euclidean rhythm generator.js/.lua' rebuilds the pattern live when a param changes", "[MidiKit][EuclidRhythm]") {
	std::string path = GENERATE(from_range(std::begin(EUCLID_PRESET_PATHS), std::end(EUCLID_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.2f);   // 4 steps
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);   // 2 fills -> [0,1,0,1]
	m->params[MidiKitModule::PARAM + 2].setValue(0.5f);   // note 64
	m->params[MidiKitModule::PARAM + 3].setValue(0.25f);  // velocity 33
	drainLog(m);

	// First bar of [0,1,0,1]: hits on ticks 2 and 4.
	std::vector<int> firstBar;
	for (int i = 1; i <= 4; i++) {
		auto events = feedTick(m);
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
		auto events = feedTick(m);
		for (auto& e : events) REQUIRE_FALSE(e.status == 0x9);
	}

	Test::destroyModule(m);
}


// Behavioural tests for the Keyboard split preset. The shipped config defines
// three presets, each a split point plus two output channels, activated by a
// CC whose number matches the preset's `cc` with any value > 0:
//   preset 1: CC 70, A=1, B=2, split 60  (active at load)
//   preset 2: CC 71, A=3, B=4, split 48
//   preset 3: CC 72, A=5, B=6, split 72
// Notes below the split go to channel A, at or above it to channel B; Note-Offs
// follow the same rule. The trigger CCs are consumed, and every other non-note
// message passes through unchanged.
static const char* KEYBOARD_SPLIT_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Keyboard split.js",
	"presets/MidiKit/Lua/Keyboard split.lua"
};

TEST_CASE("'Keyboard split.js/.lua' routes notes below and above the split to channels A and B", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(from_range(std::begin(KEYBOARD_SPLIT_PRESET_PATHS), std::end(KEYBOARD_SPLIT_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Default preset 1: A=1 (internal 0), B=2 (internal 1), split at 60.
	auto below = feedCollect(m, noteOn(1, 50, 100));   // below the split -> A
	REQUIRE(below == std::vector<OutEvent>{{0x9, 0, 50, 100, 0}});
	auto at = feedCollect(m, noteOn(1, 60, 100));      // at the split -> B
	REQUIRE(at == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}});
	auto above = feedCollect(m, noteOn(1, 84, 100));   // above -> B
	REQUIRE(above == std::vector<OutEvent>{{0x9, 1, 84, 100, 0}});

	// Note-Offs are rewritten by the same rule.
	auto offBelow = feedCollect(m, noteOff(1, 50));
	REQUIRE(offBelow == std::vector<OutEvent>{{0x8, 0, 50, 0, 0}});
	auto offAbove = feedCollect(m, noteOff(1, 84));
	REQUIRE(offAbove == std::vector<OutEvent>{{0x8, 1, 84, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Keyboard split.js/.lua' a trigger CC with value > 0 switches the active preset", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(from_range(std::begin(KEYBOARD_SPLIT_PRESET_PATHS), std::end(KEYBOARD_SPLIT_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// CC 71 (preset 2's trigger) with value 100 activates preset 2 and is
	// consumed - nothing is forwarded.
	REQUIRE(feedCollect(m, cc(1, 71, 100)).empty());

	// Preset 2: A=3 (internal 2), B=4 (internal 3), split at 48.
	auto below = feedCollect(m, noteOn(1, 40, 100));   // below 48 -> A
	REQUIRE(below == std::vector<OutEvent>{{0x9, 2, 40, 100, 0}});
	auto at = feedCollect(m, noteOn(1, 48, 100));      // at 48 -> B
	REQUIRE(at == std::vector<OutEvent>{{0x9, 3, 48, 100, 0}});
	auto above = feedCollect(m, noteOn(1, 60, 100));   // above -> B
	REQUIRE(above == std::vector<OutEvent>{{0x9, 3, 60, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Keyboard split.js/.lua' a trigger CC with value 0 does not switch presets", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(from_range(std::begin(KEYBOARD_SPLIT_PRESET_PATHS), std::end(KEYBOARD_SPLIT_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// CC 71 with value 0 is a control CC (still consumed) but does not switch.
	REQUIRE(feedCollect(m, cc(1, 71, 0)).empty());

	// Preset 1 is still active: note 50 -> A=1 (internal 0).
	auto below = feedCollect(m, noteOn(1, 50, 100));
	REQUIRE(below == std::vector<OutEvent>{{0x9, 0, 50, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Keyboard split.js/.lua' passes non-trigger messages through unchanged", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(from_range(std::begin(KEYBOARD_SPLIT_PRESET_PATHS), std::end(KEYBOARD_SPLIT_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// A plain CC (not a preset trigger) and a pitch wheel pass through on
	// their own channel.
	auto ccEv = feedCollect(m, cc(1, 7, 100));
	REQUIRE(ccEv == std::vector<OutEvent>{{0xb, 1, 7, 100, 0}});
	auto pw = feedCollect(m, pitchWheel(1, 8192));
	REQUIRE(pw == std::vector<OutEvent>{{0xe, 1, 0, 64, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Keyboard split.js/.lua' the preset menu switches the active preset", "[MidiKit][KeyboardSplit]") {
	std::string path = GENERATE(from_range(std::begin(KEYBOARD_SPLIT_PRESET_PATHS), std::end(KEYBOARD_SPLIT_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	std::vector<ContextMenuSpec> specs;
	m->activeEngine->getContextMenus([&specs](const std::vector<ContextMenuSpec>& s) { specs = s; });
	REQUIRE(specs.size() == 1);
	REQUIRE(specs[0].label == "Preset");
	REQUIRE(specs[0].options.size() == 3);

	// Option index 2 -> preset 3 (A=5 internal 4, B=6 internal 5, split 72).
	m->activeEngine->invokeContextMenuCallback(specs[0].callbackId, 2);
	drainLog(m);

	auto below = feedCollect(m, noteOn(1, 70, 100));   // below 72 -> A
	REQUIRE(below == std::vector<OutEvent>{{0x9, 4, 70, 100, 0}});
	auto above = feedCollect(m, noteOn(1, 84, 100));   // above -> B
	REQUIRE(above == std::vector<OutEvent>{{0x9, 5, 84, 100, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Bouncing ball delay preset. Every Note-On passes
// through (the dry note) and additionally spawns a pre-scheduled echo train:
// each echo's gap is the previous gap times (1 - Gravity) and its velocity is
// the previous velocity times Bounciness, until the velocity drops below Min
// velocity (the "settle" point) or the echo cap is hit. Params are read at the
// moment the Note-On arrives; every non-note message and every Note-Off passes
// through unchanged. Scheduling is done with midiOut.sendAfterMs(), which the
// engine emits immediately with a positive `frame` (the dry/immediate note has
// frame -1), so the drain below sees the whole train at once.
static const char* BOUNCING_BALL_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Bouncing ball delay.js",
	"presets/MidiKit/Lua/Bouncing ball delay.lua"
};

TEST_CASE("'Bouncing ball delay.js/.lua' passes the note through and echoes it with decaying velocity", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(from_range(std::begin(BOUNCING_BALL_PRESET_PATHS), std::end(BOUNCING_BALL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// bounciness 0.5 (half the velocity survives each bounce), gravity 0,
	// min velocity 1.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.0f);  // gravity 0
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);  // bounciness 0.5
	m->params[MidiKitModule::PARAM + 2].setValue(0.0f);  // min velocity 1
	drainLog(m);

	// Dry note first, then one Note-On/Note-Off pair per echo. Velocity decays
	// 100, 50, 25, 13, 6, 3, 2 (round-half-up) and the train stops once the
	// float velocity drops below 1.
	auto ev = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{
		{0x9, 1, 60, 100, 0},                                          // dry
		{0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 0},                       // echo 1
		{0x9, 1, 60, 50, 0},  {0x8, 1, 60, 0, 0},                       // echo 2
		{0x9, 1, 60, 25, 0},  {0x8, 1, 60, 0, 0},                       // echo 3
		{0x9, 1, 60, 13, 0},  {0x8, 1, 60, 0, 0},                       // echo 4
		{0x9, 1, 60, 6, 0},   {0x8, 1, 60, 0, 0},                       // echo 5
		{0x9, 1, 60, 3, 0},   {0x8, 1, 60, 0, 0},                       // echo 6
		{0x9, 1, 60, 2, 0},   {0x8, 1, 60, 0, 0},                       // echo 7
	});

	Test::destroyModule(m);
}

TEST_CASE("'Bouncing ball delay.js/.lua' settles once the velocity drops below the min threshold", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(from_range(std::begin(BOUNCING_BALL_PRESET_PATHS), std::end(BOUNCING_BALL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// min velocity 32: vel 100 -> 100, 50, then 25 < 32 -> settle.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.0f);
	m->params[MidiKitModule::PARAM + 1].setValue(0.5f);
	m->params[MidiKitModule::PARAM + 2].setValue(31.0f / 126.0f);  // min velocity 32
	drainLog(m);

	auto ev = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{
		{0x9, 1, 60, 100, 0},                                          // dry
		{0x9, 1, 60, 100, 0}, {0x8, 1, 60, 0, 0},                       // echo 1
		{0x9, 1, 60, 50, 0},  {0x8, 1, 60, 0, 0},                       // echo 2
	});

	Test::destroyModule(m);
}

// Drains one feed, capturing each message's scheduled frame so the interval
// shrink (gravity) is observable. Immediate sends carry frame -1; the
// sendAfterMs() echoes carry a positive future frame.
struct FrameEvent {
	uint8_t status;
	uint8_t channel;
	uint8_t note;
	uint8_t value;
	int64_t frame;
};

static std::vector<FrameEvent> feedFrames(MidiKitModule* m, midi::Message msg) {
	m->activeEngine->processInMessage(0, msg);
	m->activeEngine->process();
	std::vector<FrameEvent> events;
	int port, ticks;
	midi::Message out;
	while (m->activeEngine->processOutMessage(port, out, ticks)) {
		events.push_back({out.getStatus(), out.getChannel(), out.getNote(), out.getValue(), out.frame});
	}
	return events;
}

TEST_CASE("'Bouncing ball delay.js/.lua' gravity shrinks the interval between echoes", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(from_range(std::begin(BOUNCING_BALL_PRESET_PATHS), std::end(BOUNCING_BALL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// gravity 0.2 (intervals shrink to 80% per bounce), bounciness 1.0 (no
	// velocity decay -> the train runs to the maxEchoes cap), min velocity 1.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.5f);   // gravity 0.2
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);   // bounciness 1.0
	m->params[MidiKitModule::PARAM + 2].setValue(0.0f);   // min velocity 1
	drainLog(m);

	auto ev = feedFrames(m, noteOn(1, 60, 100));

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
	Test::destroyModule(m);

	// gravity 0 -> uniform delay: consecutive gaps stay equal (within rounding).
	MidiKitModule* m2 = loadPreset(path);
	m2->params[MidiKitModule::PARAM + 0].setValue(0.0f);   // gravity 0
	m2->params[MidiKitModule::PARAM + 1].setValue(1.0f);
	m2->params[MidiKitModule::PARAM + 2].setValue(0.0f);
	drainLog(m2);

	auto ev2 = feedFrames(m2, noteOn(1, 60, 100));
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
	Test::destroyModule(m2);
}

TEST_CASE("'Bouncing ball delay.js/.lua' passes Note-Offs and non-note messages through unchanged", "[MidiKit][BouncingBall]") {
	std::string path = GENERATE(from_range(std::begin(BOUNCING_BALL_PRESET_PATHS), std::end(BOUNCING_BALL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// A Note-Off is not a Note-On, so it passes through and spawns no echoes.
	auto off = feedCollect(m, noteOff(1, 60));
	REQUIRE(off == std::vector<OutEvent>{{0x8, 1, 60, 0, 0}});

	// Non-note messages pass through untouched too.
	auto ccEv = feedCollect(m, cc(1, 7, 100));
	REQUIRE(ccEv == std::vector<OutEvent>{{0xb, 1, 7, 100, 0}});
	auto pw = feedCollect(m, pitchWheel(1, 8192));
	REQUIRE(pw == std::vector<OutEvent>{{0xe, 1, 0, 64, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the Gravity well preset. Every Note-On is retuned
// toward the configurable Center (param 1, 0-127) by
//   round(distance * Strength * (1 - velocity / 127))
// so the bend grows with distance from the center and shrinks with velocity:
// soft and distant notes fall deep into the well (tension), loud and
// near-center notes stay put (release). Strength 0 disables the effect, and a
// note on the center is never bent. Because the sent pitch depends on
// velocity, the Note-Off arrives with the *played* note and is redirected to
// the note that was actually sent. Non-note messages pass through unchanged.
static const char* GRAVITY_WELL_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/Gravity well.js",
	"presets/MidiKit/Lua/Gravity well.lua"
};

TEST_CASE("'Gravity well.js/.lua' bends notes toward the center, more for soft and distant notes", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(from_range(std::begin(GRAVITY_WELL_PRESET_PATHS), std::end(GRAVITY_WELL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// center 60, strength 1.0.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	// note 72 (12 above center), vel 100 -> bent 3 down to 69.
	auto a = feedCollect(m, noteOn(1, 72, 100));
	REQUIRE(a == std::vector<OutEvent>{{0x9, 1, 69, 100, 0}});

	// same note, vel 40 -> bent further (8) down to 64: softer falls deeper.
	auto b = feedCollect(m, noteOn(1, 72, 40));
	REQUIRE(b == std::vector<OutEvent>{{0x9, 1, 64, 40, 0}});

	// note 84 (24 above center), vel 100 -> bent 5 to 79: farther falls more
	// than note 72's 3 at the same velocity.
	auto c = feedCollect(m, noteOn(1, 84, 100));
	REQUIRE(c == std::vector<OutEvent>{{0x9, 1, 79, 100, 0}});

	// note 48 (12 below center), vel 100 -> bent up 3 to 51.
	auto d = feedCollect(m, noteOn(1, 48, 100));
	REQUIRE(d == std::vector<OutEvent>{{0x9, 1, 51, 100, 0}});

	// a note on the center is never bent.
	auto e = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(e == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Gravity well.js/.lua' redirects the Note-Off to the bent note", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(from_range(std::begin(GRAVITY_WELL_PRESET_PATHS), std::end(GRAVITY_WELL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	// note 72, vel 40 -> sent as 64, so its Note-Off must release 64.
	auto on = feedCollect(m, noteOn(1, 72, 40));
	REQUIRE(on == std::vector<OutEvent>{{0x9, 1, 64, 40, 0}});
	auto off = feedCollect(m, noteOff(1, 72));
	REQUIRE(off == std::vector<OutEvent>{{0x8, 1, 64, 0, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Gravity well.js/.lua' max velocity and zero strength leave notes unbent", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(from_range(std::begin(GRAVITY_WELL_PRESET_PATHS), std::end(GRAVITY_WELL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	// velocity 127 -> the well exerts no pull at all.
	auto loud = feedCollect(m, noteOn(1, 84, 127));
	REQUIRE(loud == std::vector<OutEvent>{{0x9, 1, 84, 127, 0}});

	// strength 0 -> the well is off regardless of velocity.
	m->params[MidiKitModule::PARAM + 1].setValue(0.0f);
	auto weak = feedCollect(m, noteOn(1, 84, 40));
	REQUIRE(weak == std::vector<OutEvent>{{0x9, 1, 84, 40, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Gravity well.js/.lua' passes non-note messages through unchanged", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(from_range(std::begin(GRAVITY_WELL_PRESET_PATHS), std::end(GRAVITY_WELL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	auto ccEv = feedCollect(m, cc(1, 7, 100));
	REQUIRE(ccEv == std::vector<OutEvent>{{0xb, 1, 7, 100, 0}});
	auto pw = feedCollect(m, pitchWheel(1, 8192));
	REQUIRE(pw == std::vector<OutEvent>{{0xe, 1, 0, 64, 0}});

	Test::destroyModule(m);
}

TEST_CASE("'Gravity well.js/.lua' releases the held bent note on unload", "[MidiKit][GravityWell]") {
	std::string path = GENERATE(from_range(std::begin(GRAVITY_WELL_PRESET_PATHS), std::end(GRAVITY_WELL_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// A bent note that is still held when the script is replaced must be
	// released at its sent pitch (64), not the played 72.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(60.0f / 127.0f);  // center 60
	m->params[MidiKitModule::PARAM + 1].setValue(1.0f);            // strength 1
	drainLog(m);

	feedCollect(m, noteOn(1, 72, 40));
	drainLog(m);

	MidiScriptEngine* engineBeforeUnload = m->activeEngine;
	m->loadScript("");

	// onUnload releases on the script channel the note was played on (channel
	// 2 = internal 1).
	auto ev = drainOut(engineBeforeUnload);
	REQUIRE(ev == std::vector<OutEvent>{{0x8, 1, 64, 0, 0}});

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

TEST_CASE("'Scale quantiser.js/.lua' reads the root from CV input 1", "[MidiKit][ScaleQuantiser]") {
	std::string path = GENERATE(from_range(std::begin(SCALE_QUANTISER_PRESET_PATHS), std::end(SCALE_QUANTISER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// 0.5V on input 1 puts the root at F# (pitch class 6) under the standard
	// 1V/oct pitch convention: the C minor scale transposes up 6 semitones.
	// In-scale notes still pass through unchanged and off-scale notes still
	// snap down a semitone under the default preferUpward=false.
	m->inputs[MidiKitModule::INPUT].setVoltage(0.5f, 0);

	// C#5 (pitch class 1) is in the F# minor scale -> unchanged.
	auto inScale = feedCollect(m, noteOn(1, 73, 100));
	REQUIRE(inScale == std::vector<OutEvent>{{0x9, 1, 73, 100, 0}});

	// C5 (pitch class 0) sits midway between B (11) and C# (1) -> snaps down
	// to B (71).
	auto offScale = feedCollect(m, noteOn(1, 72, 100));
	REQUIRE(offScale == std::vector<OutEvent>{{0x9, 1, 71, 100, 0}});

	Test::destroyModule(m);
}


// Parses a single value out of the compact JSON returned by captureConfig().
static json_int_t configInt(const std::string& json, const char* key) {
	json_t* root = json_loads(json.c_str(), 0, NULL);
	if (!root) return 0;
	json_int_t v = json_integer_value(json_object_get(root, key));
	json_decref(root);
	return v;
}

static bool configBool(const std::string& json, const char* key) {
	json_t* root = json_loads(json.c_str(), 0, NULL);
	if (!root) return false;
	bool v = json_is_true(json_object_get(root, key));
	json_decref(root);
	return v;
}

TEST_CASE("'Scale quantiser.js/.lua' config survives a save/reload round-trip", "[MidiKit][ScaleQuantiser][JSON]") {
	std::string path = GENERATE(from_range(std::begin(SCALE_QUANTISER_PRESET_PATHS), std::end(SCALE_QUANTISER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// First module: load the preset and change two settings via the
	// right-click context menus, exactly as a user would.
	MidiKitModule* m = loadPreset(path);

	std::vector<ContextMenuSpec> specs;
	m->activeEngine->getContextMenus([&specs](const std::vector<ContextMenuSpec>& s) { specs = s; });
	REQUIRE(specs.size() == 3);
	REQUIRE(specs[1].label == "Channel");
	REQUIRE(specs[2].label == "Round up on ties");

	// "Channel" option index 1 selects MIDI channel 2 (internal channel 1).
	m->activeEngine->invokeContextMenuCallback(specs[1].callbackId, 1);
	// Switch "Round up on ties" on.
	m->activeEngine->invokeContextMenuCallback(specs[2].callbackId, 1);
	drainLog(m);

	// Save: Rack calls onSave() before dataToJson(), and onSave() asks the
	// active script for its current config (via onUnload) so the user's
	// context-menu changes are what get persisted as "scriptConfig".
	rack::engine::Module::SaveEvent saveEvent;
	m->onSave(saveEvent);
	json_t* rootJ = m->dataToJson();
	Test::destroyModule(m);

	json_t* configJ = json_object_get(rootJ, "scriptConfig");
	REQUIRE(configJ != NULL);
	REQUIRE(json_is_object(configJ));
	REQUIRE(json_integer_value(json_object_get(configJ, "channel")) == 1);
	REQUIRE(json_is_true(json_object_get(configJ, "preferUpward")));

	// Second module: reload the patch and confirm the config came back.
	MidiKitModule* m2 = createModule();
	m2->dataFromJson(rootJ);
	json_decref(rootJ);

	// The reloaded module's config must match what the user changed.
	std::string restored = m2->activeEngine->captureConfig();
	REQUIRE(configInt(restored, "channel") == 1);
	REQUIRE(configBool(restored, "preferUpward") == true);

	// The menu presentation state is read lazily from onGetValue, so after a
	// reload the rebuilt menus reflect the restored config — the regression
	// this fix targets (checked/selected used to be captured at script load
	// time, before onLoad() restored the persisted config).
	std::vector<ContextMenuSpec> restoredSpecs;
	m2->activeEngine->getContextMenus([&restoredSpecs](const std::vector<ContextMenuSpec>& s) { restoredSpecs = s; });
	REQUIRE(restoredSpecs.size() == 3);
	REQUIRE(restoredSpecs[1].label == "Channel");
	REQUIRE(restoredSpecs[2].label == "Round up on ties");
	REQUIRE(restoredSpecs[1].selected == 1);
	REQUIRE(restoredSpecs[2].checked == true);

	Test::destroyModule(m2);
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

// --- A unison (the same note played twice) must release in press order, one
// voice per Note-Off - the queueOfNote FIFO is the part of the script most
// likely to regress. Round-robin sends the first voice to channel 1 and the
// second to channel 2; the two Note-Offs must then release channel 1 first and
// channel 2 second, not the other way round and not both at once. ---
TEST_CASE("'Micro scale.js/.lua' releases a unison's voices in press order", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// The tonic (note 60) has no residual bend, so each voice is a single
	// clean Note-On - the test focuses purely on the FIFO, not on tuning.
	auto first = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(first == std::vector<OutEvent>{{0x9, 0, 60, 100, 0}});
	auto second = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(second == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}});

	// The two Note-Offs release in press order: channel 1 first, then channel
	// 2. A LIFO or a single combined release would break this.
	auto off1 = feedCollect(m, noteOff(1, 60));
	REQUIRE(off1 == std::vector<OutEvent>{{0x8, 0, 60, 0, 0}});
	auto off2 = feedCollect(m, noteOff(1, 60));
	REQUIRE(off2 == std::vector<OutEvent>{{0x8, 1, 60, 0, 0}});

	Test::destroyModule(m);
}

// --- Voice stealing with the default 8 output channels: once every channel is
// busy the 9th note displaces the round-robin next channel, and the displaced
// note's later Note-Off must be dropped so it cannot release the thief. An
// equal-temperament scale is used so every note passes through unchanged and
// the test observes channel allocation alone. ---
TEST_CASE("'Micro scale.js/.lua' steals a busy channel and drops the displaced note", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
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
	MidiKitModule* m = loadPresetWithScl(path, scl);

	// 8 notes fill the 8 default channels in round-robin order (1..8).
	for (int n = 0; n < 8; n++) {
		auto ev = feedCollect(m, noteOn(1, 60 + n, 100));
		REQUIRE(ev == std::vector<OutEvent>{{0x9, static_cast<uint8_t>(n), static_cast<uint8_t>(60 + n), 100, 0}});
	}

	// The 9th note finds every channel busy, so it steals the next round-robin
	// channel - channel 1 (internal 0) - displacing note 60.
	auto steal = feedCollect(m, noteOn(1, 68, 100));
	REQUIRE(steal == std::vector<OutEvent>{{0x9, 0, 68, 100, 0}});

	// The displaced note's Note-Off arrives later and must be dropped: its
	// queue entry was removed at steal time, so releasing it would kill the
	// thief's voice.
	REQUIRE(feedCollect(m, noteOff(1, 60)).empty());

	// The thief's own Note-Off releases channel 1.
	auto rel = feedCollect(m, noteOff(1, 68));
	REQUIRE(rel == std::vector<OutEvent>{{0x8, 0, 68, 0, 0}});

	// Channel 1 is free again; the next note lands there (round-robin wraps
	// back to the freed channel rather than skipping it).
	auto again = feedCollect(m, noteOn(1, 69, 100));
	REQUIRE(again == std::vector<OutEvent>{{0x9, 0, 69, 100, 0}});

	Test::destroyModule(m);
}

// --- The "Always send pitch bend" context-menu option: with it off (the
// default) the tonic sits exactly on the centre bend and no pitch wheel is
// emitted; with it on, the centre bend is sent anyway. This is the script's
// only alwaysSendBend code path and nothing else in the suite exercises it
// (the default preset keeps it off). ---
TEST_CASE("'Micro scale.js/.lua' alwaysSendBend forces a bend even for the tonic", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	std::vector<ContextMenuSpec> specs;
	m->activeEngine->getContextMenus([&specs](const std::vector<ContextMenuSpec>& s) { specs = s; });
	REQUIRE(specs.size() == 2);
	REQUIRE(specs[0].label == "Input channel");
	REQUIRE(specs[1].label == "Always send pitch bend");

	// Switch the option on; the next tonic Note-On must be preceded by the
	// centre bend 8192 (LSB 0, MSB 64) even though it is unchanged.
	m->activeEngine->invokeContextMenuCallback(specs[1].callbackId, 1);
	drainLog(m);

	auto ev = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(ev == std::vector<OutEvent>{{0xe, 0, 0, 64, 0}, {0x9, 0, 60, 100, 0}});
	feedCollect(m, noteOff(1, 60));

	// Switch it back off: the receiver still remembers the last bend per
	// channel, so the next tonic (on a fresh round-robin channel) goes out
	// bend-free again.
	m->activeEngine->invokeContextMenuCallback(specs[1].callbackId, 0);
	drainLog(m);

	auto again = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(again == std::vector<OutEvent>{{0x9, 1, 60, 100, 0}});

	Test::destroyModule(m);
}

// --- The "Input channel" context-menu option filters which input channel is
// retuned. Notes on the filtered channel go through the normal retune path;
// notes on every other channel pass through untouched - and, crucially, are
// not tracked, so their Note-Offs pass through as the raw note rather than
// being redirected to a retuned note. Non-note messages pass through on all
// channels. ---
TEST_CASE("'Micro scale.js/.lua' input-channel filter retunes only the chosen channel", "[MidiKit][MicroScale]") {
	std::string path = GENERATE(from_range(std::begin(MICRO_SCALE_PRESET_PATHS), std::end(MICRO_SCALE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	std::vector<ContextMenuSpec> specs;
	m->activeEngine->getContextMenus([&specs](const std::vector<ContextMenuSpec>& s) { specs = s; });
	// "Input channel" option index 1 selects script channel 1. The script's
	// channels are 1-based (midi.getChannel returns the Rack nibble + 1), so
	// the matching note is fed as noteOn(0, ...) and the non-matching one as
	// noteOn(1, ...).
	m->activeEngine->invokeContextMenuCallback(specs[0].callbackId, 1);
	drainLog(m);

	// Channel 1 is still retuned exactly as before.
	auto in = feedCollect(m, noteOn(0, 62, 100));
	REQUIRE(in == std::vector<OutEvent>{{0xe, 0, 79, 59, 0}, {0x9, 0, 64, 100, 0}});

	// Channel 2 passes through untouched - not retuned, not tracked.
	auto out = feedCollect(m, noteOn(1, 62, 100));
	REQUIRE(out == std::vector<OutEvent>{{0x9, 1, 62, 100, 0}});

	// Non-note messages pass through on both the matching and non-matching
	// channels.
	auto ccIn = feedCollect(m, cc(0, 20, 100));
	REQUIRE(ccIn == std::vector<OutEvent>{{0xb, 0, 20, 100, 0}});
	auto ccOut = feedCollect(m, cc(1, 20, 100));
	REQUIRE(ccOut == std::vector<OutEvent>{{0xb, 1, 20, 100, 0}});

	// The channel-1 Note-Off releases the retuned note 64; the channel-2
	// Note-Off is a passthrough of the raw note 62.
	auto offIn = feedCollect(m, noteOff(0, 62));
	REQUIRE(offIn == std::vector<OutEvent>{{0x8, 0, 64, 0, 0}});
	auto offOut = feedCollect(m, noteOff(1, 62));
	REQUIRE(offOut == std::vector<OutEvent>{{0x8, 1, 62, 0, 0}});

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

TEST_CASE("'Clock divider.js/.lua' forwards Stop and does not reset the phase", "[MidiKit][ClockDivider]") {
	std::string path = GENERATE(from_range(std::begin(CLOCK_DIVIDER_PRESET_PATHS), std::end(CLOCK_DIVIDER_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// 3 ticks are swallowed, leaving tickCount = 3 (3 < divisor 6).
	for (int i = 0; i < 3; i++) feedCollect(m, clockTick());

	// Stop is always forwarded untouched - it is a realtime message handled
	// before the passThroughOther check, so it goes out even mid-count.
	auto stop = feedCollect(m, stopMsg());
	bool stopFwd = false;
	for (auto& e : stop) if (e.status == 0xf) stopFwd = true;
	REQUIRE(stopFwd);

	// Unlike Start, Stop does not reset the phase: the next forwarded tick is
	// still the one that completes the count from before the Stop (3 pre-Stop
	// ticks + 3 more = 6), not 6 ticks after the Stop. Had Stop called
	// resetPhase(), the 3rd tick after it would still be swallowed (count 3).
	for (int i = 0; i < 2; i++) {
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

// The knob extremes are the whole point of the script - curveAmount=2 turns
// knob 0.0 into curve +2 and knob 1.0 into curve -2, the only two non-linear
// settings number.rescale() ever sees at runtime. Both engines run the same
// formula as Rack's rack::math::rescale() with a dsp::exp2_taylor5() exponent:
// first the velocity is rescaled linearly onto [1, e], raised to the power
// 2^curve, then rescaled back onto [1, 127] and rounded. Note the direction:
// a POSITIVE curve (knob 0.0, exponential) squashes soft notes toward the
// floor - the full range only opens up at the top of the key travel - while a
// NEGATIVE curve (knob 1.0, logarithmic) lifts light touches toward the
// ceiling. (The curve sign in the presets was inverted until 2026-08-03,
// making knob 0.0 boost instead of squash.) The expected values below are
// exact, computed from those same functions.
static int shapedVelocity(MidiKitModule* m, int vel) {
	auto ev = feedCollect(m, noteOn(1, 60, vel));
	REQUIRE_FALSE(ev.empty());
	return ev[0].value;
}

TEST_CASE("'Velocity curve.js/.lua' exponential knob (0.0) reshapes velocities", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(from_range(std::begin(VELOCITY_CURVE_PRESET_PATHS), std::end(VELOCITY_CURVE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// knob 0.0 -> curve +2: soft notes are squashed toward the floor - only
	// hard hits open up the range - and the endpoints stay pinned at 1 and 127.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(0.0f);

	std::vector<std::pair<int, int>> cases = {{1, 1}, {2, 1}, {8, 1}, {32, 2}, {64, 13}, {96, 46}, {120, 102}, {127, 127}};
	for (auto& c : cases) {
		REQUIRE(shapedVelocity(m, c.first) == c.second);
	}

	Test::destroyModule(m);
}

TEST_CASE("'Velocity curve.js/.lua' logarithmic knob (1.0) reshapes velocities", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(from_range(std::begin(VELOCITY_CURVE_PRESET_PATHS), std::end(VELOCITY_CURVE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// knob 1.0 -> curve -2: light touches are lifted sharply toward the
	// ceiling (useful for stiff keybeds), and the endpoints stay pinned.
	MidiKitModule* m = loadPreset(path);
	m->params[MidiKitModule::PARAM + 0].setValue(1.0f);

	std::vector<std::pair<int, int>> cases = {{1, 1}, {2, 31}, {8, 55}, {32, 86}, {64, 106}, {96, 118}, {120, 125}, {127, 127}};
	for (auto& c : cases) {
		REQUIRE(shapedVelocity(m, c.first) == c.second);
	}

	Test::destroyModule(m);
}

TEST_CASE("'Velocity curve.js/.lua' keeps every output within the 1..127 window", "[MidiKit][VelocityCurve]") {
	std::string path = GENERATE(from_range(std::begin(VELOCITY_CURVE_PRESET_PATHS), std::end(VELOCITY_CURVE_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// The script's rescale() works in floats and can land a hair outside the
	// configured window, so shapeVelocity() must clamp to [minVelocity,
	// maxVelocity] = [1, 127]. Sweep the whole input range at both knob
	// extremes: a valid output byte must never drop to 0 (that would read as a
	// Note-Off) nor exceed 127.
	MidiKitModule* m = loadPreset(path);
	for (float knob : {0.0f, 1.0f}) {
		m->params[MidiKitModule::PARAM + 0].setValue(knob);
		for (int vel = 1; vel <= 127; vel++) {
			int out = shapedVelocity(m, vel);
			REQUIRE(out >= 1);
			REQUIRE(out <= 127);
		}
	}

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

// The script speaks only NRPN: non-CC messages are dropped by an early return,
// and non-NRPN CC numbers (anything but 98/99/6/38) fall through to an
// else/return branch. So a plain CC - CC 7 volume, CC 10 pan, ... - is silently
// swallowed rather than passed on. That is the intended design (the script
// converts NRPN and nothing else), so these tests pin it, and also prove the
// swallowed messages never corrupt the NRPN state machine that follows.
TEST_CASE("'NRPN to CC.js/.lua' drops non-NRPN CCs and non-CC messages silently", "[MidiKit][NRPN]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_PRESET_PATHS), std::end(NRPN_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Regular CCs fall through to the else/return branch and are swallowed.
	auto cc7 = feedCollect(m, cc(1, 7, 100));
	REQUIRE(cc7.empty());
	auto cc10 = feedCollect(m, cc(1, 10, 64));
	REQUIRE(cc10.empty());

	// Non-CC messages (a Note-On here) hit the early return and are dropped too.
	auto note = feedCollect(m, noteOn(1, 60, 100));
	REQUIRE(note.empty());

	// None of the dropped messages touched the NRPN state machine: a complete
	// NRPN write that follows still converts to its CC pair as usual.
	feedCollect(m, cc(1, 99, 0));
	feedCollect(m, cc(1, 98, 1));
	feedCollect(m, cc(1, 6, 64));
	auto ev = feedCollect(m, cc(1, 38, 0));
	REQUIRE(ev == std::vector<OutEvent>{{0xb, 0, 1, 64, 0}, {0xb, 0, 33, 0, 0}});

	Test::destroyModule(m);
}


// Behavioural tests for the NRPN Generator preset, the companion to "NRPN to
// CC": it sweeps a 14-bit value up and down and emits it as a spec-compliant
// NRPN once per ticksPerStep MIDI clock ticks. A send is the 4-CC wire form
// built by midi.createNRPN()/midi.setNRPN() - CC 99/98 carry the parameter
// number MSB/LSB and CC 6/38 the value MSB/LSB (see nrpnQuad below). The
// shipped config is channel 1, nrpnNumber 0, ticksPerStep 8, stepSize 16,
// maxValue 16383; only Channel and Ticks per step are exposed in the context
// menu, so the other fields are set by rewriting the config block before load
// (loadNrpnGen) when a test needs a non-default sweep.
static const char* NRPN_GENERATOR_PRESET_PATHS[] = {
	"presets/MidiKit/JavaScript/NRPN Generator.js",
	"presets/MidiKit/Lua/NRPN Generator.lua"
};

// The 4-CC wire form midi.setNRPN() expands an NRPN into, in send() order:
// CC 99 = number MSB, CC 98 = number LSB, CC 6 = value MSB, CC 38 = value LSB,
// all on the script's 1-based channel ch (OutEvent channels are 0-based).
static std::vector<OutEvent> nrpnQuad(int ch, int number, int value) {
	return {
		{0xb, static_cast<uint8_t>(ch - 1), 99, static_cast<uint8_t>((number >> 7) & 0x7f), 0},
		{0xb, static_cast<uint8_t>(ch - 1), 98, static_cast<uint8_t>(number & 0x7f), 0},
		{0xb, static_cast<uint8_t>(ch - 1), 6, static_cast<uint8_t>((value >> 7) & 0x7f), 0},
		{0xb, static_cast<uint8_t>(ch - 1), 38, static_cast<uint8_t>(value & 0x7f), 0},
	};
};

// Feeds n consecutive MIDI clock ticks and returns everything the generator
// emitted across them. Intermediate ticks produce nothing; only the Nth tick
// (a step boundary) yields the NRPN quad, so this is just that tick's output.
static std::vector<OutEvent> feedTicks(MidiKitModule* m, int n) {
	std::vector<OutEvent> all;
	for (int i = 0; i < n; i++) {
		auto ev = feedCollect(m, clockTick());
		all.insert(all.end(), ev.begin(), ev.end());
	}
	return all;
}

// Loads the NRPN Generator preset with an arbitrary config. The shipped
// defaults are rewritten in the script text before loading, so the sweep
// arithmetic (stepSize/maxValue/nrpnNumber) can be driven with a handful of
// steps instead of the ~1000 the shipped stepSize=16/maxValue=16383 need.
// The two engines spell the config the same way apart from the assignment
// operator: JS "field: value", Lua "field = value". Each replacement is
// asserted to have been found so a preset edit cannot silently skip a field.
static MidiKitModule* loadNrpnGen(const std::string& relPath, int channel, int nrpnNumber, int ticksPerStep, int stepSize, int maxValue) {
	std::string script = readFile(repoRoot() + "/" + relPath);
	const char* sep = (relPath.find("Lua/") != std::string::npos) ? " = " : ": ";
	auto set = [&script, sep](const char* name, int oldVal, int newVal) {
		std::string from = std::string(name) + sep + std::to_string(oldVal);
		size_t at = script.find(from);
		REQUIRE(at != std::string::npos);
		script.replace(at, from.size(), std::string(name) + sep + std::to_string(newVal));
	};
	set("channel", 1, channel);
	set("nrpnNumber", 0, nrpnNumber);
	set("ticksPerStep", 8, ticksPerStep);
	set("stepSize", 16, stepSize);
	set("maxValue", 16383, maxValue);

	MidiKitModule* m = createModule();
	m->loadScript(script);
	std::string loadLog = drainLog(m);
	CATCH_INFO("preset: " << relPath);
	CATCH_INFO("load log:\n" << loadLog);
	REQUIRE(loadLog.find("rror") == std::string::npos);
	REQUIRE(loadLog.find("Script loaded") != std::string::npos);
	return m;
}

TEST_CASE("'NRPN Generator.js/.lua' sweeps the value once per ticksPerStep ticks", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_GENERATOR_PRESET_PATHS), std::end(NRPN_GENERATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Default ticksPerStep=8: the 8th tick is the first step boundary. The
	// value starts at 0 and advanceValue() runs before sendNrpn(), so the
	// first emitted value is 0 + 16 = 16, not 0.
	auto first = feedTicks(m, 8);
	REQUIRE(first == nrpnQuad(1, 0, 16));

	// The next 8 ticks advance the sweep to 32.
	auto second = feedTicks(m, 8);
	REQUIRE(second == nrpnQuad(1, 0, 32));

	Test::destroyModule(m);
}

TEST_CASE("'NRPN Generator.js/.lua' emits nothing until the first full step", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_GENERATOR_PRESET_PATHS), std::end(NRPN_GENERATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// 7 ticks leave tickCount at 7 - below the 8-tick step - so no NRPN yet.
	REQUIRE(feedTicks(m, 7).empty());

	// The 8th tick completes the first step.
	REQUIRE(feedTicks(m, 1) == nrpnQuad(1, 0, 16));

	Test::destroyModule(m);
}

TEST_CASE("'NRPN Generator.js/.lua' resets the phase on Start and Continue", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_GENERATOR_PRESET_PATHS), std::end(NRPN_GENERATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	// Partial ticks before Start: tickCount reaches 3, below the step.
	feedTicks(m, 3);
	REQUIRE(feedCollect(m, startMsg()).empty());

	// Start reset the count to 0, so the next NRPN is not on tick 5 (the
	// remainder of the abandoned step) but on the 8th tick after Start.
	REQUIRE(feedTicks(m, 7).empty());
	REQUIRE(feedTicks(m, 1) == nrpnQuad(1, 0, 16));

	// Same for Continue after a partial run.
	feedTicks(m, 4);
	REQUIRE(feedCollect(m, continueMsg()).empty());
	REQUIRE(feedTicks(m, 7).empty());
	REQUIRE(feedTicks(m, 1) == nrpnQuad(1, 0, 32));

	Test::destroyModule(m);
}

TEST_CASE("'NRPN Generator.js/.lua' context menu changes ticks per step and channel", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_GENERATOR_PRESET_PATHS), std::end(NRPN_GENERATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	MidiKitModule* m = loadPreset(path);

	std::vector<ContextMenuSpec> specs;
	m->activeEngine->getContextMenus([&specs](const std::vector<ContextMenuSpec>& s) { specs = s; });
	REQUIRE(specs.size() == 2);
	REQUIRE(specs[0].label == "Channel");
	REQUIRE(specs[1].label == "Ticks per step");

	// "Ticks per step" option index 1 -> TICKS_PER_STEP[1] = 2 ticks/step.
	m->activeEngine->invokeContextMenuCallback(specs[1].callbackId, 1);
	// "Channel" option index 1 -> MIDI channel 2 (internal channel 1).
	m->activeEngine->invokeContextMenuCallback(specs[0].callbackId, 1);
	drainLog(m);

	// 2 ticks now complete a step; the quad goes out on the new channel.
	REQUIRE(feedTicks(m, 2) == nrpnQuad(2, 0, 16));

	// The menus report the new selections (read back through onGetValue).
	std::vector<ContextMenuSpec> after;
	m->activeEngine->getContextMenus([&after](const std::vector<ContextMenuSpec>& s) { after = s; });
	REQUIRE(after[0].selected == 1);
	REQUIRE(after[1].selected == 1);

	Test::destroyModule(m);
}

TEST_CASE("'NRPN Generator.js/.lua' encodes the NRPN number as 14-bit MSB/LSB", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_GENERATOR_PRESET_PATHS), std::end(NRPN_GENERATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// nrpnNumber 257 = 0b100000001: CC 99 carries MSB 2, CC 98 carries LSB 1.
	// Everything else stays at the default, so the first step is value 16.
	MidiKitModule* m = loadNrpnGen(path, 1, 257, 8, 16, 16383);

	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 257, 16));

	Test::destroyModule(m);
}

TEST_CASE("'NRPN Generator.js/.lua' clamps at maxValue and reverses the sweep", "[MidiKit][NRPNGenerator]") {
	std::string path = GENERATE(from_range(std::begin(NRPN_GENERATOR_PRESET_PATHS), std::end(NRPN_GENERATOR_PRESET_PATHS)));
	CATCH_INFO("preset: " << path);

	// A tiny sweep (stepSize 100, maxValue 300) reaches both ends in a handful
	// of steps instead of the ~1000 the shipped config needs, and every value
	// is > 127 so the 14-bit MSB/LSB split of CC 6/38 is exercised too (200 ->
	// CC 6 = 1, CC 38 = 72; 300 -> CC 6 = 2, CC 38 = 44).
	MidiKitModule* m = loadNrpnGen(path, 1, 0, 8, 100, 300);

	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 0, 100));
	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 0, 200));
	// 300 hits maxValue: the value is clamped there, not overshot to 400...
	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 0, 300));
	// ...and the direction flips.
	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 0, 200));
	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 0, 100));
	// 0 hits the bottom and flips back up.
	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 0, 0));
	REQUIRE(feedTicks(m, 8) == nrpnQuad(1, 0, 100));

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
