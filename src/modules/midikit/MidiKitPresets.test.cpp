#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"
#include <fstream>
#include <sstream>

using namespace StoermelderPackOne::MidiKit;
using StoermelderPackOne::MidiScript::MidiScriptEngine;

SYNC_MODEL(modelMidiKit, "MidiKit");
Test::TestContext<> testContext;

// Smoke test for the shipped example presets in presets/MidiKit/.
//
// These scripts are only ever exercised by loading them in Rack, so nothing
// else in the suite would notice if one stopped working — and both engines fail
// in ways that are easy to miss by reading: a script with an unsupported
// construct can load cleanly, report "Script loaded", and then error out on
// every single MIDI message. See project SCRIPTING.md ("Elk language
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

static MidiKitModule* createModule() {
	MidiKitModule* m = new MidiKitModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

static std::string readFile(const std::string& path) {
	std::ifstream f(path);
	CATCH_INFO("cannot open " << path);
	REQUIRE(f.good());
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// Drains the module log and returns it as one string.
static std::string drainLog(MidiKitModule* m) {
	std::string out;
	while (!m->midiLogMessages.empty()) {
		auto s = m->midiLogMessages.shift();
		out += std::get<2>(s) + "\n";
	}
	return out;
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
	"Rewrite Ch1 to Ch2"
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

// Loads one of the two Arpeggiator preset files and sets its four params by
// normalized 0..1 Param value - the same values a user would get from the
// panel knobs, so this exercises the same param.getValue() reads the script
// makes rather than reaching into script-internal state.
static MidiKitModule* loadArp(const std::string& relPath, float clockDivision, float octaveRange, float noteLength, float playmode) {
	MidiKitModule* m = createModule();
	m->loadScript(readFile(repoRoot() + "/" + relPath));

	std::string loadLog = drainLog(m);
	CATCH_INFO("preset: " << relPath);
	CATCH_INFO("load log:\n" << loadLog);
	REQUIRE(loadLog.find("rror") == std::string::npos);
	REQUIRE(loadLog.find("Script loaded") != std::string::npos);

	m->params[MidiKitModule::PARAM + 0].setValue(clockDivision);
	m->params[MidiKitModule::PARAM + 1].setValue(octaveRange);
	m->params[MidiKitModule::PARAM + 2].setValue(noteLength);
	m->params[MidiKitModule::PARAM + 3].setValue(playmode);
	return m;
}

// --- Up mode, 1 tick/step, 1 octave: plain ascending replay of the held chord ---
TEST_CASE("Arpeggiator Up mode steps the held chord in press order", "[MidiKit][Arpeggiator]") {
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
TEST_CASE("Arpeggiator Down mode steps the held chord in reverse", "[MidiKit][Arpeggiator]") {
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
TEST_CASE("Arpeggiator Up-Down mode does not repeat the end notes", "[MidiKit][Arpeggiator]") {
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
TEST_CASE("Arpeggiator octave range repeats the chord one octave higher", "[MidiKit][Arpeggiator]") {
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
TEST_CASE("Arpeggiator clock division holds the step across intermediate ticks", "[MidiKit][Arpeggiator]") {
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
TEST_CASE("Arpeggiator note length never overruns into the next step", "[MidiKit][Arpeggiator]") {
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
TEST_CASE("Arpeggiator stops stepping once every note is released", "[MidiKit][Arpeggiator]") {
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
TEST_CASE("Arpeggiator releases the sounding note on unload", "[MidiKit][Arpeggiator]") {
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
