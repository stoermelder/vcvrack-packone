#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"
#include <fstream>
#include <sstream>

using namespace StoermelderPackOne::MidiKit;

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
