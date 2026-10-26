#pragma once
// Test cases for the UDP command input: AhabCommander (driven directly, no
// socket), the AhabUdpInput settings/transport seam (scripted fake), module
// wiring (one datagram per process() call) and an optional loopback socket
// smoke test. Included by Ahab.test.cpp, which brings Ahab.cpp into the TU
// first so AhabModule is fully defined.

#include <chrono>
#include <deque>
#include <thread>
#include <vector>
#ifndef _WIN32
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif
#include "../../test/test_mock.hpp"
#include "Ahab.test.hpp"

using Commander = AhabCommander<AhabModule>;

static bool exec(AhabModule* m, const char* cmd) {
	return Commander::execute(*m, cmd, strlen(cmd));
}

static Glyph glyphAt(AhabModule* m, Usz y, Usz x) {
	return m->sim->getFieldBuffer()[y * m->sim->getFieldWidth() + x];
}

static Datagram makeDatagram(const char* text) {
	Datagram d;
	d.len = (uint16_t)strlen(text);
	d.session = 0;
	memcpy(d.data, text, d.len);
	return d;
}

// Stands in for the socket receiver: serves scripted datagrams and never opens
// a socket or starts a thread, so settings can change freely. `changes` counts
// effective settings changes (the points where the real receiver would rebind).
struct ScriptedUdpInput : AhabUdpInput {
	std::deque<Datagram> script;
	int changes = 0;

	void onSettingsChanged() override { changes++; }
	void stop() override {}
	bool tryPop(Datagram& d) override {
		if (script.empty()) return false;
		d = script.front();
		script.pop_front();
		return true;
	}
};

// Replaces the module's receiver with a scripted one wired to the commander.
static ScriptedUdpInput* injectScriptedInput(AhabModule* m) {
	ScriptedUdpInput* fake = new ScriptedUdpInput;
	fake->setHandler([m](const Datagram& d) { Commander::execute(*m, d.data, d.len); });
	m->udpInput.reset(fake);
	return fake;
}


// --- Commander: parsing and dispatch ---

TEST_CASE("Commander ignores unknown commands", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	m->sim->setTickNumber(5);
	const char* cases[] = { "", ":", ":5", "bogus:1", "f:1", "fra:1", "framed:1", "frameframeframeframe:1" };
	for (const char* cmd : cases) {
		REQUIRE_FALSE(exec(m, cmd));
		REQUIRE(m->sim->getTickNumber() == 5);
	}

	Test::unregisterModule(m);
}

TEST_CASE("Commander resolves two-letter aliases, case and whitespace", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	const char* cases[] = { "fr:7", "FRAME:7", "  frame :7", "Fr:7" };
	for (const char* cmd : cases) {
		m->sim->setTickNumber(0);
		REQUIRE(exec(m, cmd));
		REQUIRE(m->sim->getTickNumber() == 7);
	}

	// Aliases of the other commands reach the right handler.
	m->setRunning(true);
	REQUIRE(exec(m, "st"));
	REQUIRE_FALSE(m->simRunning);
	REQUIRE(exec(m, "pl"));
	REQUIRE(m->simRunning);
	m->sim->setTickNumber(10);
	REQUIRE(exec(m, "sk:5"));
	REQUIRE(m->sim->getTickNumber() == 15);
	REQUIRE(exec(m, "re:5"));
	REQUIRE(m->sim->getTickNumber() == 10);
	m->setRunning(false);
	REQUIRE(exec(m, "ru"));
	REQUIRE(m->sim->getTickNumber() == 11);
	REQUIRE(exec(m, "wr:A;0;0"));
	REQUIRE(glyphAt(m, 0, 0) == 'A');
	REQUIRE(exec(m, "cc:32"));
	REQUIRE(m->midiCcOffset == 32);

	Test::unregisterModule(m);
}

TEST_CASE("Commander uses only the parts a command needs", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "frame:3;9;9"));
	REQUIRE(m->sim->getTickNumber() == 3);

	Test::unregisterModule(m);
}

// --- Commander: individual commands ---

TEST_CASE("Commander frame sets the tick number", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "frame:42"));
	REQUIRE(m->sim->getTickNumber() == 42);
	REQUIRE(exec(m, "frame:0"));
	REQUIRE(m->sim->getTickNumber() == 0);

	// Negative values clamp to 0.
	m->sim->setTickNumber(10);
	REQUIRE(exec(m, "frame:-5"));
	REQUIRE(m->sim->getTickNumber() == 0);

	Test::unregisterModule(m);
}

TEST_CASE("Commander ignores a missing or non-numeric number", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	const char* names[] = { "frame", "skip", "rewind", "cc" };
	const char* values[] = { "", ":", ":abc", ":12x", ": ", ":99999999999" };
	for (const char* name : names) {
		for (const char* value : values) {
			std::string cmd = std::string(name) + value;
			m->sim->setTickNumber(5);
			m->midiCcOffset = 64;
			// Recognised (the handler ran) but changes nothing.
			REQUIRE(exec(m, cmd.c_str()));
			REQUIRE(m->sim->getTickNumber() == 5);
			REQUIRE(m->midiCcOffset == 64);
		}
	}

	Test::unregisterModule(m);
}

TEST_CASE("Commander play and stop use the RUN logic and are idempotent", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(m->simRunning);
	m->clockPhase = 0.5f;
	REQUIRE(exec(m, "play"));
	REQUIRE(m->simRunning);
	REQUIRE(m->clockPhase == 0.5f);   // already running: nothing is touched

	REQUIRE(exec(m, "stop"));
	REQUIRE_FALSE(m->simRunning);
	REQUIRE(m->clockPhase == 0.0f);
	REQUIRE(exec(m, "stop"));
	REQUIRE_FALSE(m->simRunning);

	REQUIRE(exec(m, "play"));
	REQUIRE(m->simRunning);

	Test::unregisterModule(m);
}

TEST_CASE("Commander run steps once, also while stopped", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	m->setRunning(false);
	Usz before = m->sim->getTickNumber();
	REQUIRE(exec(m, "run"));
	REQUIRE(m->sim->getTickNumber() == before + 1);
	REQUIRE(exec(m, "run"));
	REQUIRE(m->sim->getTickNumber() == before + 2);
	REQUIRE_FALSE(m->simRunning);

	Test::unregisterModule(m);
}

TEST_CASE("Commander skip and rewind move the tick, saturating at 0", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	m->sim->setTickNumber(10);
	REQUIRE(exec(m, "skip:5"));
	REQUIRE(m->sim->getTickNumber() == 15);
	REQUIRE(exec(m, "rewind:3"));
	REQUIRE(m->sim->getTickNumber() == 12);
	REQUIRE(exec(m, "rewind:100"));
	REQUIRE(m->sim->getTickNumber() == 0);

	// A negative amount goes the other way.
	m->sim->setTickNumber(10);
	REQUIRE(exec(m, "skip:-4"));
	REQUIRE(m->sim->getTickNumber() == 6);
	REQUIRE(exec(m, "rewind:-4"));
	REQUIRE(m->sim->getTickNumber() == 10);
	REQUIRE(exec(m, "skip:-100"));
	REQUIRE(m->sim->getTickNumber() == 0);

	Test::unregisterModule(m);
}

TEST_CASE("Commander write puts text into the grid at x;y", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	Usz undoBefore = m->sim->getUndoCount();
	REQUIRE(exec(m, "write:HELLO;2;3"));
	const char* hello = "HELLO";
	for (Usz i = 0; i < 5; i++) REQUIRE(glyphAt(m, 3, 2 + i) == hello[i]);
	// Neighbours stay untouched.
	REQUIRE(glyphAt(m, 3, 1) == '.');
	REQUIRE(glyphAt(m, 3, 7) == '.');
	REQUIRE(glyphAt(m, 2, 2) == '.');
	REQUIRE(glyphAt(m, 4, 2) == '.');
	// Remote edits are not undoable.
	REQUIRE(m->sim->getUndoCount() == undoBefore);

	// The value is split at the first ':' only, so the text may contain one.
	REQUIRE(exec(m, "write:a:b;0;0"));
	REQUIRE(glyphAt(m, 0, 0) == 'a');
	REQUIRE(glyphAt(m, 0, 1) == ':');
	REQUIRE(glyphAt(m, 0, 2) == 'b');

	// A space is not a valid glyph: it becomes an empty cell.
	REQUIRE(exec(m, "write:A B;0;1"));
	REQUIRE(glyphAt(m, 1, 0) == 'A');
	REQUIRE(glyphAt(m, 1, 1) == '.');
	REQUIRE(glyphAt(m, 1, 2) == 'B');

	// Coordinates are used as given, 0 included (no cursor fallback).
	REQUIRE(exec(m, "write:Z;0;5"));
	REQUIRE(glyphAt(m, 5, 0) == 'Z');

	Test::unregisterModule(m);
}

TEST_CASE("Commander write clips at the field edge", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	Usz w = m->sim->getFieldWidth();
	Usz h = m->sim->getFieldHeight();
	std::string cmd = "write:ABCDE;" + std::to_string(w - 2) + ";0";
	REQUIRE(exec(m, cmd.c_str()));
	REQUIRE(glyphAt(m, 0, w - 2) == 'A');
	REQUIRE(glyphAt(m, 0, w - 1) == 'B');
	// Nothing wraps into the next row.
	REQUIRE(glyphAt(m, 1, 0) == '.');

	// Entirely outside: no effect.
	cmd = "write:Q;" + std::to_string(w) + ";0";
	REQUIRE(exec(m, cmd.c_str()));
	cmd = "write:Q;0;" + std::to_string(h);
	REQUIRE(exec(m, cmd.c_str()));
	for (Usz x = 0; x < w; x++) REQUIRE(glyphAt(m, 0, x) != 'Q');
	REQUIRE(glyphAt(m, h - 1, 0) != 'Q');

	Test::unregisterModule(m);
}

TEST_CASE("Commander writes wraps text inside the selection", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	// 3 wide, 2 high at column 4, row 2.
	REQUIRE(exec(m, "select:4;2;3;2"));
	REQUIRE(exec(m, "writes:ABCDE"));
	const char* row0 = "ABC";
	for (Usz i = 0; i < 3; i++) REQUIRE(glyphAt(m, 2, 4 + i) == row0[i]);
	REQUIRE(glyphAt(m, 3, 4) == 'D');
	REQUIRE(glyphAt(m, 3, 5) == 'E');
	// Cells the text does not reach and the surroundings stay empty.
	REQUIRE(glyphAt(m, 3, 6) == '.');
	REQUIRE(glyphAt(m, 2, 3) == '.');
	REQUIRE(glyphAt(m, 2, 7) == '.');
	REQUIRE(glyphAt(m, 1, 4) == '.');
	REQUIRE(glyphAt(m, 4, 4) == '.');

	// Longer than the selection: cut off, nothing leaks below it.
	REQUIRE(exec(m, "writes:1234567890"));
	REQUIRE(glyphAt(m, 2, 4) == '1');
	REQUIRE(glyphAt(m, 3, 6) == '6');
	REQUIRE(glyphAt(m, 4, 4) == '.');

	// Spaces become empty cells; a 1x1 selection takes the first character.
	REQUIRE(exec(m, "select:0;0"));
	REQUIRE(exec(m, "writes:Q R"));
	REQUIRE(glyphAt(m, 0, 0) == 'Q');
	REQUIRE(glyphAt(m, 0, 1) == '.');

	// Rejected input leaves the grid alone.
	REQUIRE(exec(m, "writes"));
	REQUIRE(exec(m, "writes:"));
	REQUIRE(glyphAt(m, 0, 0) == 'Q');

	Test::unregisterModule(m);
}

TEST_CASE("Commander write rejects incomplete or unsafe input", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	auto gridIsEmpty = [&]() {
		for (Usz y = 0; y < m->sim->getFieldHeight(); y++)
			for (Usz x = 0; x < m->sim->getFieldWidth(); x++)
				if (glyphAt(m, y, x) != '.') return false;
		return true;
	};

	const char* cases[] = {
		"write", "write:", "write:A;1", "write:A;x;1", "write:A;1;y",
		"write:;1;1",            // empty text
		"write:A;-1;0",          // negative coordinates
		"write:A;0;-1",
	};
	for (const char* cmd : cases) {
		REQUIRE(exec(m, cmd));
		REQUIRE(gridIsEmpty());
	}

	// Control characters are rejected as a whole, not written partially.
	const char ctrl[] = "write:AB\x01" "C;0;0";
	REQUIRE(Commander::execute(*m, ctrl, sizeof(ctrl) - 1));
	REQUIRE(gridIsEmpty());
	const char tab[] = "write:A\tB;0;0";
	REQUIRE(Commander::execute(*m, tab, sizeof(tab) - 1));
	REQUIRE(gridIsEmpty());

	// Longer than the datagram payload limit.
	std::string longCmd = "write:" + std::string(300, 'A') + ";0;0";
	REQUIRE(exec(m, longCmd.c_str()));
	REQUIRE(gridIsEmpty());

	Test::unregisterModule(m);
}

// --- Commander: cursor, selection and clipboard commands ---

static void setEditor(AhabModule* m, Usz cy, Usz cx, Usz sy, Usz sx, Usz sh, Usz sw) {
	m->editor.update([&](AhabEditorState& s) {
		s.setSelection(sy, sx, sh, sw);
		s.setCursor(cy, cx);
		s.clampCursorToSelection();
	});
}

TEST_CASE("Commander write without coordinates writes at the cursor", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	setEditor(m, 4, 7, 4, 7, 1, 1);
	REQUIRE(exec(m, "write:HI"));
	REQUIRE(glyphAt(m, 4, 7) == 'H');
	REQUIRE(glyphAt(m, 4, 8) == 'I');
	REQUIRE(m->editor.load().cursor_x == 7);   // the cursor stays put

	// Explicit coordinates still win; a lone x is rejected.
	REQUIRE(exec(m, "write:Z;0;0"));
	REQUIRE(glyphAt(m, 0, 0) == 'Z');
	REQUIRE(exec(m, "write:Q;1"));
	REQUIRE(glyphAt(m, 0, 1) == '.');
	REQUIRE(glyphAt(m, 4, 7) == 'H');

	Test::unregisterModule(m);
}

TEST_CASE("Commander select sets cursor and selection, clamped to the field", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "select:5;3;4;2"));
	AhabEditorState s = m->editor.load();
	REQUIRE(s.cursor_x == 5); REQUIRE(s.cursor_y == 3);
	REQUIRE(s.sel_x == 5); REQUIRE(s.sel_y == 3); REQUIRE(s.sel_w == 4); REQUIRE(s.sel_h == 2);

	// w and h default to 1; the alias works.
	REQUIRE(exec(m, "se:2;1"));
	s = m->editor.load();
	REQUIRE(s.sel_x == 2); REQUIRE(s.sel_y == 1); REQUIRE(s.sel_w == 1); REQUIRE(s.sel_h == 1);

	// Beyond the field: shifted back inside, cursor stays in the selection.
	Usz fh = m->sim->getFieldHeight(), fw = m->sim->getFieldWidth();
	REQUIRE(exec(m, "select:9999;9999;3;3"));
	s = m->editor.load();
	REQUIRE(s.sel_y + s.sel_h <= fh); REQUIRE(s.sel_x + s.sel_w <= fw);
	REQUIRE(s.cursor_y >= s.sel_y); REQUIRE(s.cursor_y < s.sel_y + s.sel_h);
	REQUIRE(s.cursor_x >= s.sel_x); REQUIRE(s.cursor_x < s.sel_x + s.sel_w);

	// Rejected input changes nothing.
	AhabEditorState before = m->editor.load();
	for (const char* cmd : { "select", "select:1", "select:a;b", "select:-1;0", "select:1;1;0;1", "select:1;1;x" }) {
		REQUIRE(exec(m, cmd));
		AhabEditorState now = m->editor.load();
		REQUIRE(now.sel_y == before.sel_y); REQUIRE(now.sel_x == before.sel_x);
		REQUIRE(now.sel_h == before.sel_h); REQUIRE(now.sel_w == before.sel_w);
	}

	Test::unregisterModule(m);
}

TEST_CASE("Commander find selects the first row-major match", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "write:XYZ;3;2"));
	REQUIRE(exec(m, "write:XYZ;1;5"));
	REQUIRE(exec(m, "find:YZ"));
	AhabEditorState s = m->editor.load();
	REQUIRE(s.sel_y == 2); REQUIRE(s.sel_x == 4); REQUIRE(s.sel_h == 1); REQUIRE(s.sel_w == 2);
	REQUIRE(s.cursor_y == 2); REQUIRE(s.cursor_x == 4);

	// A match at the right edge of the field.
	Usz fw = m->sim->getFieldWidth();
	REQUIRE(exec(m, ("write:EDGE;" + std::to_string(fw - 4) + ";8").c_str()));
	REQUIRE(exec(m, "fi:EDGE"));
	s = m->editor.load();
	REQUIRE(s.sel_y == 8); REQUIRE(s.sel_x == fw - 4); REQUIRE(s.sel_w == 4);

	// No match, or text that can't be in the grid: the selection stays.
	REQUIRE(exec(m, "find:NOPE"));
	REQUIRE(exec(m, "find:"));
	REQUIRE(exec(m, "find"));
	s = m->editor.load();
	REQUIRE(s.sel_y == 8); REQUIRE(s.sel_x == fw - 4);

	Test::unregisterModule(m);
}

TEST_CASE("Commander erase clears the selection without undo", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "write:ABCD;0;0"));
	REQUIRE(exec(m, "write:EFGH;0;1"));
	Usz undoBefore = m->sim->getUndoCount();
	REQUIRE(exec(m, "select:1;0;2;2"));
	REQUIRE(exec(m, "erase"));
	REQUIRE(glyphAt(m, 0, 0) == 'A'); REQUIRE(glyphAt(m, 0, 1) == '.');
	REQUIRE(glyphAt(m, 0, 2) == '.'); REQUIRE(glyphAt(m, 0, 3) == 'D');
	REQUIRE(glyphAt(m, 1, 0) == 'E'); REQUIRE(glyphAt(m, 1, 1) == '.');
	REQUIRE(glyphAt(m, 1, 3) == 'H');
	REQUIRE(m->sim->getUndoCount() == undoBefore);

	Test::unregisterModule(m);
}

TEST_CASE("Commander copy and paste use the module clipboard", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	Mock mock;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "write:AB;0;0"));
	REQUIRE(exec(m, "write:CD;0;1"));
	REQUIRE(exec(m, "select:0;0;2;2"));
	REQUIRE(exec(m, "copy"));
	REQUIRE(exec(m, "select:5;6"));
	REQUIRE(exec(m, "paste"));
	REQUIRE(glyphAt(m, 6, 5) == 'A'); REQUIRE(glyphAt(m, 6, 6) == 'B');
	REQUIRE(glyphAt(m, 7, 5) == 'C'); REQUIRE(glyphAt(m, 7, 6) == 'D');
	REQUIRE(m->sim->getUndoCount() == 0);

	// `pa` is paste; `co` is not copy (it is ORCA's color) and does nothing.
	REQUIRE(exec(m, "select:0;3"));
	REQUIRE(exec(m, "pa"));
	REQUIRE(glyphAt(m, 3, 0) == 'A');
	REQUIRE_FALSE(exec(m, "co"));

	// Paste clips at the field edge.
	Usz fw = m->sim->getFieldWidth();
	REQUIRE(exec(m, ("select:" + std::to_string(fw - 1) + ";0").c_str()));
	REQUIRE(exec(m, "paste"));
	REQUIRE(glyphAt(m, 0, fw - 1) == 'A');
	REQUIRE(glyphAt(m, 1, fw - 1) == 'C');

	// The OS clipboard is untouched.
	REQUIRE(mock.ui.clipboardWrites.empty());

	Test::unregisterModule(m);
}

TEST_CASE("Commander paste uses the selection's top left and resizes the selection", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	Mock mock;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "write:AB;0;0"));
	REQUIRE(exec(m, "write:CD;0;1"));
	REQUIRE(exec(m, "select:0;0;2;2"));
	REQUIRE(exec(m, "copy"));

	// A 4x4 selection with the cursor on its bottom right corner: the paste still starts
	// at the top left (5,6), and the selection shrinks to the pasted 2x2 block.
	REQUIRE(exec(m, "select:5;6;4;4"));
	m->editor.update([](AhabEditorState& s) { s.setCursor(9, 8); });
	REQUIRE(exec(m, "paste"));
	REQUIRE(glyphAt(m, 6, 5) == 'A'); REQUIRE(glyphAt(m, 6, 6) == 'B');
	REQUIRE(glyphAt(m, 7, 5) == 'C'); REQUIRE(glyphAt(m, 7, 6) == 'D');
	AhabEditorState s = m->editor.load();
	REQUIRE(s.sel_y == 6); REQUIRE(s.sel_x == 5);
	REQUIRE(s.sel_h == 2); REQUIRE(s.sel_w == 2);

	Test::unregisterModule(m);
}

TEST_CASE("Commander paste with an empty clipboard is a no-op", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "paste"));
	REQUIRE(glyphAt(m, 0, 0) == '.');

	Test::unregisterModule(m);
}

TEST_CASE("Commander cc sets the CC offset, clamped to 0..127", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "cc:32"));
	REQUIRE(m->midiCcOffset == 32);
	REQUIRE(exec(m, "cc:200"));
	REQUIRE(m->midiCcOffset == 127);
	REQUIRE(exec(m, "cc:-5"));
	REQUIRE(m->midiCcOffset == 0);

	Test::unregisterModule(m);
}

TEST_CASE("Commander bpm sets the tempo, clamped to 30..300", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "bpm:140"));
	REQUIRE(m->params[AhabModule::BPM_PARAM].getValue() == 140.f);
	REQUIRE(exec(m, "bp:1000"));
	REQUIRE(m->params[AhabModule::BPM_PARAM].getValue() == 300.f);
	REQUIRE(exec(m, "bpm:-4"));
	REQUIRE(m->params[AhabModule::BPM_PARAM].getValue() == 30.f);
	REQUIRE(exec(m, "bpm:abc"));
	REQUIRE(m->params[AhabModule::BPM_PARAM].getValue() == 30.f);

	Test::unregisterModule(m);
}

TEST_CASE("Commander apm ramps 1 BPM per step and bpm cancels it", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	auto bpm = [&]() { return m->params[AhabModule::BPM_PARAM].getValue(); };

	REQUIRE(exec(m, "bpm:120"));
	REQUIRE(exec(m, "ap:123"));
	REQUIRE(bpm() == 120.f);   // nothing happens until the clock steps
	m->stepBpmRamp();
	REQUIRE(bpm() == 121.f);
	m->stepBpmRamp();
	m->stepBpmRamp();
	REQUIRE(bpm() == 123.f);
	m->stepBpmRamp();          // target reached, ramp is over
	REQUIRE(bpm() == 123.f);

	// Downwards, and cancelled by bpm.
	REQUIRE(exec(m, "apm:100"));
	m->stepBpmRamp();
	REQUIRE(bpm() == 122.f);
	REQUIRE(exec(m, "bpm:150"));
	m->stepBpmRamp();
	REQUIRE(bpm() == 150.f);

	// Clamped target.
	REQUIRE(exec(m, "apm:5"));
	REQUIRE(m->bpmTarget == 30);

	Test::unregisterModule(m);
}

TEST_CASE("Commander udp sets the UDP output and input ports", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	ScriptedUdpInput* in = injectScriptedInput(m);

	REQUIRE(exec(m, "udp:5000;6000"));
	REQUIRE(m->udpOutput->getPort() == "5000");
	REQUIRE(m->udpOutput->getAddress() == "127.0.0.1");
	REQUIRE(in->getPort() == "6000");
	REQUIRE(in->changes == 1);
	REQUIRE(m->oscOutput->getPort() == "49162");

	// A single part leaves the input alone; the alias works.
	REQUIRE(exec(m, "ud:5001"));
	REQUIRE(m->udpOutput->getPort() == "5001");
	REQUIRE(in->getPort() == "6000");
	REQUIRE(in->changes == 1);

	// Invalid ports change nothing.
	REQUIRE(exec(m, "udp:0;70000"));
	REQUIRE(exec(m, "udp:abc;-1"));
	REQUIRE(m->udpOutput->getPort() == "5001");
	REQUIRE(in->getPort() == "6000");

	Test::unregisterModule(m);
}

TEST_CASE("Commander osc sets the OSC output port", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "osc:7000"));
	REQUIRE(m->oscOutput->getPort() == "7000");
	REQUIRE(m->oscOutput->getAddress() == "127.0.0.1");
	REQUIRE(m->udpOutput->getPort() == "49161");

	REQUIRE(exec(m, "os:7001"));
	REQUIRE(m->oscOutput->getPort() == "7001");
	REQUIRE(exec(m, "osc:99999"));
	REQUIRE(exec(m, "osc:"));
	REQUIRE(m->oscOutput->getPort() == "7001");

	Test::unregisterModule(m);
}

TEST_CASE("Commander ip sets the address of both outputs", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);

	REQUIRE(exec(m, "ip:192.168.1.5"));
	REQUIRE(m->udpOutput->getAddress() == "192.168.1.5");
	REQUIRE(m->oscOutput->getAddress() == "192.168.1.5");
	REQUIRE(m->udpOutput->getPort() == "49161");
	REQUIRE(m->oscOutput->getPort() == "49162");

	// Empty and malformed addresses are rejected.
	REQUIRE(exec(m, "ip:"));
	REQUIRE(exec(m, "ip:a b"));
	REQUIRE(exec(m, "ip"));
	REQUIRE(m->udpOutput->getAddress() == "192.168.1.5");
	REQUIRE(m->oscOutput->getAddress() == "192.168.1.5");

	Test::unregisterModule(m);
}

TEST_CASE("Commander pg sends bank select and program change", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	MockMidiOutputDevice* dev = setupMockMidiOutput(m);
	m->midiOutEnabled = true;

	size_t base = dev->getMessageCount();
	REQUIRE(exec(m, "pg:3;5;7;9"));
	REQUIRE(dev->getMessageCount() == base + 3);

	const auto& bankMsb = dev->getMessage(base);
	REQUIRE(bankMsb.getSize() == 3);
	REQUIRE(bankMsb.bytes[0] == 0xB3);
	REQUIRE(bankMsb.bytes[1] == 0);
	REQUIRE(bankMsb.bytes[2] == 5);

	const auto& bankLsb = dev->getMessage(base + 1);
	REQUIRE(bankLsb.bytes[0] == 0xB3);
	REQUIRE(bankLsb.bytes[1] == 32);
	REQUIRE(bankLsb.bytes[2] == 7);

	const auto& program = dev->getMessage(base + 2);
	REQUIRE(program.getSize() == 2);
	REQUIRE(program.bytes[0] == 0xC3);
	REQUIRE(program.bytes[1] == 9);

	cleanupMockMidiOutput(m, dev);
	Test::unregisterModule(m);
}

TEST_CASE("Commander pg sends only the parts that are present", "[Ahab][Commander]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	MockMidiOutputDevice* dev = setupMockMidiOutput(m);
	m->midiOutEnabled = true;

	// Program only.
	size_t base = dev->getMessageCount();
	REQUIRE(exec(m, "pg:3;;;12"));
	REQUIRE(dev->getMessageCount() == base + 1);
	REQUIRE(dev->getMessage(base).bytes[0] == 0xC3);
	REQUIRE(dev->getMessage(base).bytes[1] == 12);

	// Bank MSB only; channel and values clamp.
	base = dev->getMessageCount();
	REQUIRE(exec(m, "pg:99;300"));
	REQUIRE(dev->getMessageCount() == base + 1);
	REQUIRE(dev->getMessage(base).bytes[0] == 0xBF);
	REQUIRE(dev->getMessage(base).bytes[2] == 127);

	base = dev->getMessageCount();
	REQUIRE(exec(m, "pg:0;;;-4"));
	REQUIRE(dev->getMessageCount() == base + 1);
	REQUIRE(dev->getMessage(base).bytes[1] == 0);

	// No channel: nothing is sent.
	base = dev->getMessageCount();
	REQUIRE(exec(m, "pg"));
	REQUIRE(exec(m, "pg:abc;1;2;3"));
	REQUIRE(exec(m, "pg:;1;2;3"));
	REQUIRE(dev->getMessageCount() == base);

	// Channel alone: nothing to send.
	REQUIRE(exec(m, "pg:5"));
	REQUIRE(dev->getMessageCount() == base);

	cleanupMockMidiOutput(m, dev);
	Test::unregisterModule(m);
}

// --- Module wiring: datagrams reach the commander, one per process() call ---

TEST_CASE("UDP input runs at most one datagram per process call", "[Ahab][Udp]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->setRunning(false);   // no automatic steps disturbing the tick
	ScriptedUdpInput* fake = injectScriptedInput(m);
	fake->script.push_back(makeDatagram("frame:5"));
	fake->script.push_back(makeDatagram("frame:9"));

	m->process({});
	REQUIRE(m->sim->getTickNumber() == 5);
	REQUIRE(fake->script.size() == 1);
	m->process({});
	REQUIRE(m->sim->getTickNumber() == 9);
	REQUIRE(fake->script.empty());

	// Nothing queued: nothing changes.
	m->process({});
	REQUIRE(m->sim->getTickNumber() == 9);

	Test::unregisterModule(m);
}

TEST_CASE("UDP input executes in arrival order", "[Ahab][Udp]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->setRunning(false);
	m->sim->setTickNumber(10);
	ScriptedUdpInput* fake = injectScriptedInput(m);
	fake->script.push_back(makeDatagram("write:A;0;0"));
	fake->script.push_back(makeDatagram("run"));
	fake->script.push_back(makeDatagram("frame:0"));

	m->process({});
	REQUIRE(glyphAt(m, 0, 0) == 'A');
	m->process({});
	REQUIRE(m->sim->getTickNumber() == 11);
	m->process({});
	REQUIRE(m->sim->getTickNumber() == 0);

	Test::unregisterModule(m);
}

TEST_CASE("UDP input ignores unknown commands without blocking the queue", "[Ahab][Udp]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->setRunning(false);
	ScriptedUdpInput* fake = injectScriptedInput(m);
	fake->script.push_back(makeDatagram("bogus:1"));
	fake->script.push_back(makeDatagram("frame:7"));

	m->process({});
	REQUIRE(m->sim->getTickNumber() == 0);
	m->process({});
	REQUIRE(m->sim->getTickNumber() == 7);

	Test::unregisterModule(m);
}

// --- AhabUdpInput settings ---

TEST_CASE("UDP input settings default to off", "[Ahab][Udp]") {
	AhabUdpInput in;
	REQUIRE_FALSE(in.isEnabled());
	REQUIRE(in.getPort() == "49160");
	REQUIRE_FALSE(in.isBindAll());
	REQUIRE(in.getStatusText() == "Off");
}

TEST_CASE("UDP input setPort validates and trims", "[Ahab][Udp]") {
	ScriptedUdpInput in;
	REQUIRE(in.setPort(" 50000 "));
	REQUIRE(in.getPort() == "50000");
	const char* invalid[] = { "", "  ", "abc", "0", "70000", "-1", "12 34" };
	for (const char* port : invalid) {
		REQUIRE_FALSE(in.setPort(port));
		REQUIRE(in.getPort() == "50000");
	}
}

TEST_CASE("UDP input setters notify only on an effective change", "[Ahab][Udp]") {
	ScriptedUdpInput in;
	in.setEnabled(true);
	REQUIRE(in.changes == 1);
	in.setBindAll(true);
	REQUIRE(in.changes == 2);
	in.setPort("50001");
	REQUIRE(in.changes == 3);
	in.setEnabled(false);
	REQUIRE(in.changes == 4);

	// Setting what is already set does nothing.
	in.setEnabled(false);
	in.setBindAll(true);
	in.setPort("50001");
	in.setPort(" 50001 ");
	REQUIRE(in.changes == 4);

	// So does an invalid port.
	in.setPort("abc");
	REQUIRE(in.changes == 4);
}

TEST_CASE("UDP input reset and load don't notify when nothing changes", "[Ahab][Udp]") {
	ScriptedUdpInput in;
	in.reset();
	in.fromJson(nullptr);
	REQUIRE(in.changes == 0);

	in.setEnabled(true);
	REQUIRE(in.changes == 1);
	json_t* j = in.toJson();
	in.fromJson(j);   // same settings as stored
	json_decref(j);
	REQUIRE(in.changes == 1);

	in.reset();       // back to the defaults is one change
	REQUIRE(in.changes == 2);
	REQUIRE_FALSE(in.isEnabled());
}

TEST_CASE("UDP input JSON round-trip", "[Ahab][Udp]") {
	ScriptedUdpInput a;
	a.setEnabled(true);
	a.setPort("50002");
	a.setBindAll(true);
	json_t* j = a.toJson();
	REQUIRE(json_boolean_value(json_object_get(j, "enabled")));
	REQUIRE(std::string(json_string_value(json_object_get(j, "port"))) == "50002");
	REQUIRE(json_boolean_value(json_object_get(j, "bindAll")));

	ScriptedUdpInput b;
	b.fromJson(j);
	json_decref(j);
	REQUIRE(b.isEnabled());
	REQUIRE(b.getPort() == "50002");
	REQUIRE(b.isBindAll());
	REQUIRE(b.changes == 1);   // one notification for the whole load
}

TEST_CASE("UDP input JSON falls back to defaults for missing or bad data", "[Ahab][Udp]") {
	ScriptedUdpInput in;
	in.setEnabled(true);
	in.setPort("50003");

	in.fromJson(nullptr);
	REQUIRE_FALSE(in.isEnabled());
	REQUIRE(in.getPort() == "49160");

	json_t* j = json_pack("{s:b, s:s}", "enabled", 1, "port", "not-a-port");
	in.fromJson(j);
	json_decref(j);
	REQUIRE(in.isEnabled());
	REQUIRE(in.getPort() == "49160");   // invalid port is dropped, not applied

	j = json_pack("{s:i}", "port", 50004);   // wrong type
	in.fromJson(j);
	json_decref(j);
	REQUIRE_FALSE(in.isEnabled());
	REQUIRE(in.getPort() == "49160");
}

TEST_CASE("Module stores UDP input settings and loads old patches with input off", "[Ahab][Udp]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	injectScriptedInput(m);

	m->udpInput->setPort("50005");
	m->udpInput->setEnabled(true);
	json_t* rootJ = m->dataToJson();
	json_t* inputJ = json_object_get(rootJ, "udpInput");
	REQUIRE(inputJ != nullptr);
	REQUIRE(json_boolean_value(json_object_get(inputJ, "enabled")));

	// A patch saved before this feature has no "udpInput" key.
	json_object_del(rootJ, "udpInput");
	m->dataFromJson(rootJ);
	json_decref(rootJ);
	REQUIRE_FALSE(m->udpInput->isEnabled());
	REQUIRE(m->udpInput->getPort() == "49160");

	Test::unregisterModule(m);
}

TEST_CASE("Module reset switches UDP input off", "[Ahab][Udp]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	injectScriptedInput(m);

	m->udpInput->setPort("50006");
	m->udpInput->setEnabled(true);
	Module::ResetEvent e;
	m->onReset(e);
	REQUIRE_FALSE(m->udpInput->isEnabled());
	REQUIRE(m->udpInput->getPort() == "49160");

	Test::unregisterModule(m);
}

// --- Loopback socket tests (skipped where local binding is blocked) ---

#ifndef _WIN32
// Loopback sender for the socket tests.
struct UdpSender {
	int fd;
	sockaddr_in to;

	UdpSender() {
		fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		memset(&to, 0, sizeof(to));
		to.sin_family = AF_INET;
		to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	}
	~UdpSender() { if (fd >= 0) ::close(fd); }
	void send(uint16_t port, const std::string& text) {
		to.sin_port = htons(port);
		::sendto(fd, text.data(), text.size(), 0, (sockaddr*)&to, sizeof(to));
	}
};

// Waits (up to ~2 s) until `done` holds, calling `step` between checks.
template <typename Done, typename Step>
static bool waitFor(Done done, Step step) {
	for (int i = 0; i < 200 && !done(); i++) {
		step();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return done();
}

// The receiver binds on its own thread; wait for the outcome.
static bool waitSettled(AhabUdpInput& in) {
	return waitFor([&]() { return in.getStatusText() != "Starting"; }, []() {});
}

static bool bindFailed(const AhabUdpInput& in) {
	return in.getStatusText().find("Bind failed") == 0;
}

// Asks the OS for `n` distinct free loopback ports (all held at once, so they
// differ), instead of hard-coding ports that may be taken. Empty if the OS
// refuses; the caller skips then. Small race: a port may be taken before use,
// which the callers also treat as "cannot bind" and skip.
static std::vector<uint16_t> freeUdpPorts(int n) {
	std::vector<uint16_t> ports;
	std::vector<int> fds;
	for (int i = 0; i < n; i++) {
		int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (fd < 0) break;
		fds.push_back(fd);
		sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = 0;
		socklen_t len = sizeof(addr);
		if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 || ::getsockname(fd, (sockaddr*)&addr, &len) != 0) break;
		ports.push_back(ntohs(addr.sin_port));
	}
	for (int fd : fds) ::close(fd);
	if ((int)ports.size() != n) ports.clear();
	return ports;
}

TEST_CASE("UDP input receives over a loopback socket", "[Ahab][Udp][Socket]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->setRunning(false);

	std::vector<uint16_t> ports = freeUdpPorts(1);
	if (ports.empty()) {
		Test::unregisterModule(m);
		SKIP("Cannot open a UDP socket");
	}
	const uint16_t port = ports[0];
	REQUIRE(m->udpInput->setPort(std::to_string(port)));
	m->udpInput->setEnabled(true);
	REQUIRE(waitSettled(*m->udpInput));
	if (bindFailed(*m->udpInput)) {
		std::string why = m->udpInput->getStatusText();
		Test::unregisterModule(m);
		SKIP("Cannot bind UDP port " << port << ": " << why);
	}

	UdpSender tx;
	REQUIRE(tx.fd >= 0);
	auto pump = [&](std::function<bool()> done) {
		return waitFor(done, [&]() { m->process({}); });
	};

	// An oversized datagram is dropped, a CRLF-terminated one is trimmed and runs.
	tx.send(port, "frame:" + std::string(300, '1'));
	tx.send(port, "frame:12\r\n");
	REQUIRE(pump([&]() { return m->sim->getTickNumber() == 12; }));
	REQUIRE(m->udpInput->getStatusText() == "Listening (1 received, 1 dropped)");

	// A NUL (a C string sent with its terminator) ends the message like a line break,
	// so the last part is not rejected as containing a non-printable character.
	tx.send(port, std::string("write:Z\0", 8));
	REQUIRE(pump([&]() { return glyphAt(m, 0, 0) == 'Z'; }));

	// The first \n, \r or NUL ends the message; what follows is ignored.
	tx.send(port, "frame:21\nframe:99");
	REQUIRE(pump([&]() { return m->sim->getTickNumber() == 21; }));
	tx.send(port, std::string("frame:22\0frame:99", 17));
	REQUIRE(pump([&]() { return m->sim->getTickNumber() == 22; }));
	tx.send(port, "frame:23\rframe:99");
	REQUIRE(pump([&]() { return m->sim->getTickNumber() == 23; }));
	// Nothing before the terminator: nothing to run.
	tx.send(port, "\nframe:50");
	tx.send(port, "frame:24");
	REQUIRE(pump([&]() { return m->sim->getTickNumber() == 24; }));

	// A second listener on the same port fails visibly.
	{
		AhabUdpInput second;
		second.setPort(std::to_string(port));
		second.setEnabled(true);
		REQUIRE(waitSettled(second));
		REQUIRE(second.getStatusText() == "Bind failed: port in use");
	}

	// Switched off: later datagrams have no effect.
	m->udpInput->setEnabled(false);
	REQUIRE(waitFor([&]() { return m->udpInput->getStatusText() == "Off"; }, []() {}));
	tx.send(port, "frame:99");
	for (int i = 0; i < 20; i++) {
		m->process({});
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(m->sim->getTickNumber() == 24);

	Test::unregisterModule(m);
}

TEST_CASE("UDP input rebinds on a settings change and discards stale datagrams", "[Ahab][Udp][Socket]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->setRunning(false);

	std::vector<uint16_t> ports = freeUdpPorts(2);
	if (ports.empty()) {
		Test::unregisterModule(m);
		SKIP("Cannot open a UDP socket");
	}
	const uint16_t portA = ports[0], portB = ports[1];
	m->udpInput->setPort(std::to_string(portA));
	m->udpInput->setEnabled(true);
	REQUIRE(waitSettled(*m->udpInput));
	if (bindFailed(*m->udpInput)) {
		std::string why = m->udpInput->getStatusText();
		Test::unregisterModule(m);
		SKIP("Cannot bind UDP port " << portA << ": " << why);
	}

	UdpSender tx;
	// Queue a command, but don't process it: it belongs to the old binding.
	tx.send(portA, "frame:5");
	REQUIRE(waitFor([&]() { return m->udpInput->getStatusText().find("1 received") != std::string::npos; }, []() {}));

	m->udpInput->setPort(std::to_string(portB));
	REQUIRE(waitFor([&]() { return m->udpInput->getStatusText() == "Listening (0 received, 0 dropped)"; }, []() {}));
	for (int i = 0; i < 5; i++) m->process({});
	REQUIRE(m->sim->getTickNumber() == 0);   // the stale "frame:5" never runs

	// The new port works.
	tx.send(portB, "frame:8");
	REQUIRE(waitFor([&]() { return m->sim->getTickNumber() == 8; }, [&]() { m->process({}); }));

	Test::unregisterModule(m);
}

TEST_CASE("UDP input settings changes never wait for the receiver", "[Ahab][Udp][Socket]") {
	std::vector<uint16_t> ports = freeUdpPorts(12);
	if (ports.empty()) SKIP("Cannot open a UDP socket");

	AhabUdpInput in;
	in.setPort(std::to_string(ports[0]));
	in.setEnabled(true);
	REQUIRE(waitSettled(in));
	// Without a bound socket the receiver sleeps instead of blocking in recv(),
	// which would make the timing below meaningless.
	if (bindFailed(in)) SKIP("Cannot bind UDP port " << ports[0] << ": " << in.getStatusText());

	// Rack applies settings with the engine lock held, so these must not join the
	// receiver (which is blocked in recv() and only wakes every POLL_MS).
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < 10; i++) {
		in.setPort(std::to_string(ports[1 + i]));
		in.setBindAll(i % 2 == 0);
	}
	in.reset();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	REQUIRE(ms < AhabUdpInput::POLL_MS / 2);
}

TEST_CASE("Destroying a module with UDP input running and traffic in flight", "[Ahab][Udp][Socket]") {
	std::vector<uint16_t> ports = freeUdpPorts(1);
	if (ports.empty()) SKIP("Cannot open a UDP socket");
	const uint16_t port = ports[0];
	{
		Test::ModuleScaffold<AhabModule> mods;
		AhabModule* m = mods.create("Ahab");
		Test::registerModule(m);
		m->udpInput->setPort(std::to_string(port));
		m->udpInput->setEnabled(true);
		REQUIRE(waitSettled(*m->udpInput));
		if (bindFailed(*m->udpInput)) {
			std::string why = m->udpInput->getStatusText();
			Test::unregisterModule(m);
			SKIP("Cannot bind UDP port " << port << ": " << why);
		}

		UdpSender tx;
		REQUIRE(tx.fd >= 0);
		// Make sure traffic really is flowing before the module goes away.
		tx.send(port, "frame:1");
		REQUIRE(waitFor([&]() { return m->udpInput->getStatusText().find("(0 received") == std::string::npos; }, []() {}));
		for (int i = 0; i < 100; i++) tx.send(port, "frame:1");
		Test::unregisterModule(m);
	}   // the scaffold destroys the module: must join the receiver without hanging
	SUCCEED();
}
#endif
