#pragma once
#include "AhabSim.hpp"
#include "AhabScratchPad.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace StoermelderPackOne {
namespace Ahab {

// Automatic quality gate: a pure function that runs a generated ScratchPad
// through a real AhabSim for N ticks and reports measurable facts about what
// came out. Production code (not test-only) because the runtime gate needs it
// too.
//
// Deterministic: the same buffer always yields the same score — the sim's RNG
// is pinned so R-operator output cannot vary between runs.
struct PatternScore {
	// Sentinel for "never happened" (firstEventTick).
	static constexpr Usz kNever = std::numeric_limits<Usz>::max();

	Usz totalEvents = 0;   // any Oevent
	Usz noteEvents = 0;    // Oevent_type_midi_note only
	Usz ccEvents = 0;      // Oevent_type_midi_cc ('!' rows)
	Usz activeTicks = 0;   // ticks that produced >= 1 event
	Usz distinctPitches = 0;   // unique (octave * 12 + note)
	Usz distinctChannels = 0;
	// Max distinct values emitted on any single (channel, control) pair. A CC
	// stuck at one value is not modulation — the likeliest silent failure of a
	// '!' row is a value cell that never varies (a '.' value still emits, as a
	// constant 0), and danglingReads cannot see it when the variable IS written
	// but simply never changes.
	Usz distinctCcValues = 0;
	Usz firstEventTick = kNever; // how long until the pattern speaks
	Usz longestSilence = 0;      // longest run of silent ticks
	float density = 0.f;         // activeTicks / totalTicks

	// Variable-flow analysis, computed STATICALLY from the buffer
	// by mirroring orca_run's evaluation order (top-to-bottom, left-to-right):
	Usz varsWritten = 0;   // distinct variables the field writes
	Usz varsRead = 0;      // distinct variables the field reads
	Usz danglingReads = 0; // reads of a variable never written above/left -> BUG
	float pitchCorrelation = 0.f; // share of note-active ticks where >= 2
								  // channels emitted together (coupling proxy)

	bool silent() const { return noteEvents == 0; }
};

// Runs the pattern in a real sim. Deterministic: same buffer -> same score.
inline PatternScore scorePattern(ScratchPad const& buf, Usz ticks = 128, Usz simSeed = 1) {
	PatternScore s;

	// Static variable-flow scan: mirrors orca_run's evaluation order
	// (top-to-bottom, left-to-right). V semantics from sim.c: left != '.' ->
	// WRITE of index_of(left); left == '.' && right != '.' -> READ of
	// index_of(right). vars_slots are wiped every tick, so a read can only ever
	// see a write that is above it or on the same row to its left — anything
	// else yields '.' forever.
	auto varIndex = [](Glyph g) -> int {
		if (g >= '0' && g <= '9') return g - '0';
		if (g >= 'a' && g <= 'z') return g - 'a' + 10;
		if (g >= 'A' && g <= 'Z') return g - 'A' + 10;
		return -1; // '.', '*', operators, notes: not a variable name
	};
	bool written[36] = {false};
	bool read[36] = {false};
	for (Usz y = 0; y < buf.height(); ++y) {
		for (Usz x = 0; x < buf.width(); ++x) {
			if (buf.get(y, x) != 'V') continue;
			Glyph left = x > 0 ? buf.get(y, x - 1) : '.';
			Glyph right = buf.get(y, x + 1); // OOB get returns '.'
			int li = varIndex(left);
			int ri = varIndex(right);
			if (li >= 0) {
				written[li] = true; // write mode (even if the value is '.')
			}
			else if (right != '.' && ri >= 0) {
				read[ri] = true;
				if (!written[ri]) ++s.danglingReads;
			}
		}
	}
	for (int i = 0; i < 36; ++i) {
		s.varsWritten += written[i] ? 1 : 0;
		s.varsRead += read[i] ? 1 : 0;
	}

	AhabSim sim;
	sim.setFieldSizeRequest(buf.height(), buf.width(), false);
	sim.process();
	sim.setRandomSeed(simSeed);        // pin R-operator output

	Usz outH = 0, outW = 0;
	if (!sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, outH, outW, false)) return s;
	sim.process();

	std::set<int> pitches;
	std::set<int> channels;
	// CC values tracked PER (channel, control): two controls each stuck at
	// their own constant must not look like modulation.
	std::map<std::pair<int, int>, std::set<int>> ccValues;
	Usz silenceRun = 0;
	Usz noteActiveTicks = 0;
	Usz multiChannelTicks = 0;
	for (Usz t = 0; t < ticks; ++t) {
		sim.stepRequest();
		sim.process();

		// Read the events EVERY tick: the list is cleared per step.
		Oevent_list const* ev = sim.getEvents();
		Usz n = ev->count;
		s.totalEvents += n;

		Usz notesThisTick = 0;
		std::set<int> chansThisTick;
		for (Usz i = 0; i < n; ++i) {
			Oevent const& e = ev->buffer[i];
			if (e.any.oevent_type == Oevent_type_midi_cc) {
				++s.ccEvents;
				ccValues[std::make_pair(e.midi_cc.channel, e.midi_cc.control)]
					.insert(e.midi_cc.value);
				continue;
			}
			if (e.any.oevent_type != Oevent_type_midi_note) continue;
			++notesThisTick;
			pitches.insert(e.midi_note.octave * 12 + e.midi_note.note);
			channels.insert(e.midi_note.channel);
			chansThisTick.insert(e.midi_note.channel);
		}
		s.noteEvents += notesThisTick;

		if (notesThisTick > 0) {
			++s.activeTicks;
			++noteActiveTicks;
			if (chansThisTick.size() >= 2) ++multiChannelTicks;
			if (s.firstEventTick == PatternScore::kNever) s.firstEventTick = t;
			silenceRun = 0;
		}
		else {
			s.longestSilence = std::max(s.longestSilence, ++silenceRun);
		}
	}
	s.distinctPitches = pitches.size();
	s.distinctChannels = channels.size();
	for (auto const& kv : ccValues) {
		s.distinctCcValues = std::max(s.distinctCcValues, kv.second.size());
	}
	s.density = ticks ? (float)s.activeTicks / (float)ticks : 0.f;
	// Coupling proxy: do voices play TOGETHER rather than take turns? Share of
	// note-active ticks where two or more channels sounded.
	s.pitchCorrelation = noteActiveTicks ? (float)multiChannelTicks / (float)noteActiveTicks : 0.f;
	return s;
}

} // namespace Ahab
} // namespace StoermelderPackOne
