#pragma once
#include "../../plugin.hpp"
#include "AhabSim.hpp"
#include "AhabScratchPad.hpp"
#include <cstdint>
#include <vector>
#include <functional>
#include <random>

extern "C" {
	#include <orca-c/field.h>
}

namespace StoermelderPackOne {
namespace Ahab {

// An inbound coupling edge: the consumer reads from VoiceNode `from` per
// `kind`. NB: explicit constructors instead of NSDMI-only — the plugin builds
// as C++11, where NSDMIs disable aggregate brace-initialization (see Extent).
struct Edge {
	size_t from;        // index of the producing VoiceNode
	enum Kind {
		Clock,          // drives the consumer's track key (today: bus division)
		Pitch,          // consumer transposes/derives from the producer's pitch
		Trigger,        // consumer fires only when the producer's pitch matches
		// The consumer maps the producer's published pitch onto a MIDI CC ('!'
		// operator) on the CONSUMER's channel. A tail on an existing voice, not
		// a new voice: it consumes no plan capacity and no channel, only a
		// control number (param).
		Modulation,
		// An OPERAND CELL of the consumer's ':' row is fed from the producer's
		// published pitch via a K (konkat) row — no new event, one extra live
		// field in the note the voice already fires. One K row mirrors several
		// Operand edges; `param` names the target cell (OpCell constants).
		// Kept separate from Modulation: different destination (operand cell vs
		// CC event) and risk class — the midi operator returns early on
		// velocity 0 (a note-off), so an Operand edge can silence where
		// Modulation cannot. Builders keep a non-zero literal floor in every
		// K-fed cell; sources are pitch publishers only (glyph index >= 12).
		// Velocity is non-linear (idx*8-1, clamped at 127 from index 16).
		Operand
	} kind;
	char param;         // Clock: division index; Pitch: interval (Counter:
						// reflection pivot); Trigger: note;
						// Modulation: CC control number (kept low: midiCcOffset,
						// default 64, is added downstream and clamped to 127);
						// Operand: which ':' cell the K row feeds (OpCell)
	// Operand cell selectors — chars so plans stay readable in a debugger.
	enum OpCell {
		kOpOctave  = 'o',
		kOpNote    = 'n',
		kOpVelocity = 'v',
		kOpLength  = 'l'
	};
	Edge() : from(0), kind(Clock), param(0) {}
	Edge(size_t from_, Kind kind_, char param_) : from(from_), kind(kind_), param(param_) {}
};

// One planned voice in an arrangement. The planner produces these without
// touching a ScratchPad; layout fills publishedVar as each voice is placed.
// Tests can assert on graph shape directly.
struct VoiceNode {
	enum Role { Bus, Bass, Lead, Harmony, Gate, Drums, Delay, Uclid, Chord,
		Counter, RoleCount };
	Role role;

	char channel = '0';   // kRoleChannel entry for reserved roles; a free-region draw otherwise
	std::string notes;    // Lead sequence / Chord tones (patch scale)
	char param = '2';     // Harmony: transpose steps; Gate: trigger note

	// Set by layout once placed: the variable this voice publishes its pitch
	// into, or 0 if none. Consumers read this.
	char publishedVar = 0;

	// Inbound edges. Empty => a root voice (its own clock, no dependency).
	std::vector<Edge> inputs;
};

// Channel policy: which MIDI channel carries which role.
//
// RESERVED entries give every primary role a fixed channel (ch0 is always the
// bass) so a rack can be patched once and re-rolled freely. Channels 0-3 are
// the four with CV jacks, so Bass/Lead/Harmony/Chord (the harmonic core) take
// them; Gate is reserved but MIDI-only. Rows follow VoiceNode::Role order.
//
// FREE ('a'-'f' = base36, MIDI channels 10-15): no fixed meaning. Texture
// voices and anything without a reserved entry draw from here, so the reserved
// mapping is never disturbed. A 0 entry means "draw from the free region".
static char const kRoleChannel[] = {
	/* Bus     */ 0,    // places no notes
	/* Bass    */ '0',
	/* Lead    */ '1',
	/* Harmony */ '2',
	/* Gate    */ '5',
	/* Drums   */ '9',  // GM convention; the drum builder hardcodes it too
	/* Delay   */ 0,    // free region
	/* Uclid   */ 0,    // free region
	/* Chord   */ '3',
	/* Counter */ '4',  // reflected counter-line
};
static char const kFreeChannels[] = "abcdef"; // base36: MIDI channels 10-15
static_assert(sizeof(kRoleChannel) == static_cast<size_t>(VoiceNode::RoleCount),
	"one channel-policy row per VoiceNode::Role — decide reserved-or-free for any new role");

// How many voices may sound at all: at least a solo lead; at most the reserved
// table plus the free region with slack (beyond 12 the free region wraps).
static int const kMinChannelBudget = 1;
static int const kMaxChannelBudget = 16;

/**
 * AhabRandomizer - Generates musically meaningful random ORCA patterns
 *
 * Based on analysis of ORCA examples and documentation, this class generates
 * connected patterns that work together. Key principles:
 *
 * 1. Vertical data flow: Operators output below and read from sides
 * 2. Variable sharing: Variables connect distant parts of the field
 * 3. Timing chains: Clocks → Delays → Triggers → MIDI
 * 4. Modular voices: Each voice has a complete signal chain
 */
struct AhabGenerator {
	// Occupied size of a placed pattern/voice. Voices report their real height
	// so the arrangement advances past the tallest voice in a row (a fixed 3
	// rows made 5-row voices overlap). NB: explicit constructors (C++11 NSDMI
	// disables aggregate brace-initialization).
	struct Extent {
		Usz h;
		Usz w;
		Extent() : h(0), w(0) {}
		Extent(Usz h_, Usz w_) : h(h_), w(w_) {}
		bool empty() const { return h == 0 || w == 0; }
	};

	struct Config {
		float density = 0.3f;
		uint32_t seed = 0;       // 0 = keep the seed given to the constructor
		bool clearFirst = true;  // paste overwrites the whole rect; overlay mode not yet supported
		// Channel budget: how many DISTINCT channels may sound. Density still
		// drives voice count; this caps the channel set only, and matches the
		// patch to the rig - default 4 so every reserved role lands on a CV
		// channel. Texture voices share channels once the set is full.
		int channels = 4;
		// Generate-and-reject: when on (default), generate() scores each attempt
		// with scorePattern() and re-rolls deterministically, keeping the best.
		// Turn OFF when measuring the raw generator (the CI gate does exactly that).
		bool qualityGate = true;

		// JSON round-trip for everything that survives patch save/load. The
		// module persists its generator settings as a "generator" subobject; new
		// steerable fields go here. clearFirst is excluded (overlay mode is
		// unimplemented and not user-facing).
		static json_t* toJson(Config const& cfg) {
			json_t* j = json_object();
			json_object_set_new(j, "density", json_real(cfg.density));
			json_object_set_new(j, "seed", json_integer((int)cfg.seed));
			json_object_set_new(j, "qualityGate", json_boolean(cfg.qualityGate));
			json_object_set_new(j, "channels", json_integer(cfg.channels));
			return j;
		}
		static Config fromJson(json_t* j) {
			Config cfg;
			if (!j || !json_is_object(j)) return cfg;
			json_t* densityJ = json_object_get(j, "density");
			if (densityJ && json_is_number(densityJ)) cfg.density = (float)json_number_value(densityJ);
			json_t* seedJ = json_object_get(j, "seed");
			if (seedJ && json_is_integer(seedJ)) cfg.seed = (uint32_t)json_integer_value(seedJ);
			json_t* gateJ = json_object_get(j, "qualityGate");
			if (gateJ) cfg.qualityGate = json_boolean_value(gateJ);
			json_t* channelsJ = json_object_get(j, "channels");
			if (channelsJ && json_is_number(channelsJ)) cfg.channels = (int)json_number_value(channelsJ);
			if (cfg.channels < kMinChannelBudget) cfg.channels = kMinChannelBudget;
			if (cfg.channels > kMaxChannelBudget) cfg.channels = kMaxChannelBudget;
			return cfg;
		}
	};

	// seed == 0 draws a nondeterministic seed from std::random_device and
	// stores it, so getSeed() always reports the seed that produced the output.
	explicit AhabGenerator(uint32_t seed = 0) : seed_(seed ? seed : std::random_device{}()), rng(seed_) {}

	/**
	 * Pure core: decide what glyphs to place — deterministic given a seed.
	 * With cfg.qualityGate on, runs a generate-and-reject loop: each
	 * attempt is scored in a throwaway AhabSim, which ALLOCATES — therefore
	 * UI-thread only (simRandomize). Never call from the DSP thread.
	 * @param height Number of rows to generate
	 * @param width Number of columns to generate
	 * @param cfg Density / seed / clear / quality-gate options
	 * @return Selection-local buffer: (0,0) is the top-left of the region
	 */
	ScratchPad generate(Usz height, Usz width, Config const& cfg);

	/**
	 * Sim adapter: clamps the rect against the field, generates into a buffer,
	 * then commits it as ONE paste command (no per-glyph queue traffic).
	 * @return false when nothing was generated or the command queue was full
	 *         (both previously silent failures)
	 */
	bool randomize(AhabSim* sim, Usz startY, Usz startX, Usz height, Usz width, Config const& cfg);

	uint32_t getSeed() const { return seed_; }

	// Shared rhythmic identity, valid after generate(): every clock/delay/uclid
	// modulus in the patch divides this bar length.
	Usz getBarMod() const { return barMod_; }
	// Clock bus: the variables the top-of-field bus published this call (a
	// subset of "qwer"), in placement order. Voices read these instead of
	// carrying their own clocks.
	std::string const& getBusVars() const { return busVars_; }
	// Number of derived voices placed by the last generate().
	int getDerivedPlaced() const { return derivedPlaced_; }

	/**
	 * Legend text: one entry per PLACED role, in plan order, duplicate roles
	 * collapsed — "N ABBR" where N is the RAW 0-based MIDI channel (matching
	 * the ':' glyph the user looks at next) and ABBR names the role. Pure: no
	 * rng, no ScratchPad, no placement. Empty output when nothing placed.
	 *
	 * Abbreviations contain NO 'V': scorePattern's static variable-flow scan
	 * reads a 'V' as a variable operation even inside a comment span, so
	 * legend text must stay invisible to it.
	 */
	static std::vector<std::string> composeChannelLegend(
		std::vector<VoiceNode> const& plan, std::vector<bool> const& placed);

	/**
	 * Free-rectangle search: true and outY/outX set when an all-'.' needH x
	 * needW rectangle fits inside the selection at (y, x, h, w); false otherwise.
	 * Deterministic — scans row-major, so the FIRST fitting anchor (lowest y,
	 * then lowest x) wins.
	 *
	 * The skyline only short-circuits: cells above sky[c] are packed, so a
	 * rectangle overlapping them fails the buffer scan anyway — skipping the
	 * reads is purely an optimisation. The BUFFER is the sole authority:
	 * skyline and reality can disagree (inflated separators, builders smaller
	 * than their reserved footprint, the known footprint-overlap bug), making
	 * writes over live cells non-recoverable.
	 */
	static bool findFreeRect(ScratchPad const& buf, std::vector<Usz> const& sky,
		Usz y, Usz x, Usz h, Usz w, Usz needH, Usz needW, Usz& outY, Usz& outX);

	/**
	 * Writes each line as one '#'-'#' delimited row anchored at (y, x), all
	 * rows padded to equal width (widest line + 2) — the same marker shape as
	 * the widget's toggleCommentBlock. Returns the extent actually written,
	 * {0, 0} when nothing was written.
	 *
	 * Precondition: the caller verified the target rectangle is free
	 * (findFreeRect) and wide enough for the widest line — this writes
	 * unconditionally. All-or-nothing: if any line contains '#' (which would
	 * terminate the comment early and unlock everything right of it), NOTHING
	 * is written.
	 */
	static Extent writeLegendBlock(ScratchPad& buf, Usz y, Usz x,
		std::vector<std::string> const& lines);

	// One key + scale per patch: root semitone 0..11 and scale index
	// 0=major 1=naturalMinor 2=pentatonic 3=dorian.
	int getPatchRoot() const { return patchRoot_; }
	int getPatchScaleIndex() const { return patchScaleIdx_; }

	/**
	 * Fill notes with a random walk over a random scale (major / natural minor
	 * / pentatonic / dorian). Scales are stored as semitone offsets and
	 * converted to glyphs with sim.c's mapping (from_midi_note_number:
	 * lowercase = sharp), so every emitted glyph is a note the ':' operator
	 * accepts. Static and rng-parameterised so tests can drive it directly.
	 */
	static void fillScaleWalk(std::string& notes, Usz len, std::mt19937& rng);

	/**
	 * Core of the above with an explicit patch key: root semitone 0..11 and
	 * scale index (0=major 1=naturalMinor 2=pentatonic 3=dorian).
	 */
	static void fillScaleWalkWith(std::string& notes, Usz len, std::mt19937& rng, int root, int scaleIdx);

	/**
	 * Bass line: root and fifth of the patch scale only — a foundation, not a
	 * melody. Static and rng-parameterised so tests can drive it directly.
	 */
	static void fillBassWalkWith(std::string& notes, Usz len, std::mt19937& rng, int root, int scaleIdx);

	/**
	 * Chord pattern: multiple MIDI notes triggered together (from chord.orca).
	 * N stacked delay+MIDI pairs with IDENTICAL rate/mod: D fires when
	 * Tick%(rate*mod)==0 regardless of position, so every note's bang lands on
	 * the same tick — a true simultaneous chord. (The old single-delay fan-out
	 * left notes 2..N without a bang neighbour and permanently silent.)
	 */
	Extent placeChordVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						   char rate, char mod, char channel, char octave, const std::vector<char>& notes,
						   char velocityVar = 0);
	/**
	 * Derived voice: reads another voice's published pitch variable (which must
	 * be WRITTEN ABOVE this voice — vars are within-tick only) and derives from
	 * it:
	 *   gate == false: transposes by param scale-degree steps ('A' add) -> a
	 *                  harmony line locked to the lead.
	 *   gate == true:  fires ONLY when the lead pitch equals param ('F' if) ->
	 *                  call-and-response gating. With sourceVarB != 0 the voice
	 *                  additionally requires the second publisher's pitch to
	 *                  equal paramB: an exact AND-merge (F/J/L/F chain, Extent
	 *                  7x12) with every cell refreshed each tick, so no stale
	 *                  bang. Public so tests can drive it against a hand-placed
	 *                  writer.
	 */
	Extent placeDerivedVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						char channel, char sourceVar, bool gate, char param,
						char* allocatedVar = nullptr,
						char sourceVarB = 0, char paramB = 0,
						char arithGlyph = 'A');
	/**
	 * Modulation tail: maps a producer's published pitch variable onto a MIDI
	 * CC on channel/control. Two rows hung BELOW the voice it expresses; the
	 * producer's V-write must sit above (vars are within-tick). Public so tests
	 * can drive it against a hand-placed writer.
	 */
	Extent placeModulationTail(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						char channel, char control, char sourceVar);
	/**
	 * Plan half of the arrangement: returns the cast and their couplings. No
	 * glyphs placed, no ScratchPad touched — deterministic given the instance
	 * seed, so tests can assert on plan shape directly. Returns an empty vector
	 * when the area/density budget is exhausted.
	 * @param nBus number of clock-bus divisions already placed above
	 * @param budget sounding-voice cap; the bus node does not count.
	 * Default 64 = capacity's own hard cap, i.e. no budget beyond capacity —
	 * direct planner calls (tests) stay unbounded; the module passes
	 * Config::channels.
	 */
	std::vector<VoiceNode> planArrangement(Usz h, Usz w, float density, size_t nBus,
		size_t budget = 64);

	/**
	 * Arpeggio pattern: clock (or the shared clock bus) → track → variable →
	 * MIDI. Example: .gC4 / .14T1324 / .aV3. When sharedVar is a bus variable
	 * (q/w/e/r), the voice reads that bus clock instead of its own C row; 0 =
	 * own clock. If allocatedVar is non-null it receives the variable the voice
	 * publishes its current pitch into — the hook derived voices follow.
	 */
	Extent placeArpeggioVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
												char channel, const std::string& notes, char sharedVar = 0, char* allocatedVar = nullptr,
												char velocityVar = 0, char octave = 0);
	// octave: 0 = draw the LEAD register band (the only role relying on
	// the random draw), '2'..'4' = pinned low register — the Bass role
	// passes a fixed low octave so the foundation never depends on a draw.

private:
	/**
	 * Names the channel each placed role sounds on, as a single '#'-'#' strip
	 * in spare space. Decorative and optional — skipped silently when nothing
	 * placed or no free rectangle fits (normal on small selections, 16x32 and
	 * below). Draws NOTHING from rng: fully deterministic given (plan, placed),
	 * so seeded streams and snapshots are untouched.
	 */
	void placeChannelLegend(ScratchPad& buf, Usz y, Usz x, Usz h, Usz w,
		std::vector<VoiceNode> const& plan, std::vector<bool> const& placed,
		std::vector<Usz> const& sky);

	uint32_t seed_;
	std::mt19937 rng;

	// Variable names available for the current generate() call:
	// shuffled once per call, handed out uniquely, 'g' reserved for the master
	// clock. Empty pool => voices fall back to "no variable".
	std::string varPool_;

	// Shared rhythmic identity, decided once per generate() call:
	Usz barMod_ = 8;        // every modulus in the patch divides this
	int patchRoot_ = 0;     // patch key, semitone 0..11
	int patchScaleIdx_ = 0; // patch scale, see fillScaleWalkWith
	std::string freeChanPool_; // shuffled kFreeChannels: unique texture channel per call
	Usz freeWrap_ = 0;         // round-robin once the free pool runs dry
	std::string ccPool_;    // shuffled "0123": unique CC control number per
						// Modulation edge. Two tails on one (channel, control)
						// pair fight; low numbers also survive midiCcOffset's
						// 127 clamp.
	std::string busVars_;   // clock-bus variables placed this call:
	int derivedPlaced_ = 0; // derived voices placed this call.
	                        // generate() captures it on adoption so it always
	                        // describes the RETURNED attempt, not a later
	                        // rejected retry
	// Helper functions for random values
	char randomBase36();
	char randomSmallDigit();  // 1-8 for rates/mods
	char randomNote();
	// Register band per ROLE: Bass < Chord < Harmony < Lead with ±1 jitter;
	// gates and texture sit mid. Replaces the old channel-latched draw and its
	// evaluation-order hazard.
	char randomOctaveForRole(VoiceNode::Role role);
	// Channel policy lookup: the role's reserved entry, or a unique
	// draw from the free region when it has none.
	char channelForRole(VoiceNode::Role role);
	char allocateFreeChannel();
	// Picks a euclidean pair whose modulus DIVIDES the patch bar:
	// with steps in 2..max-1.
	void randomEuclid(char& steps, char& max);
	// A base36 modulus glyph that divides the patch bar.
	char barDivisorMod();
	// fillScaleWalk over the patch's own key + scale.
	void fillPatchWalk(std::string& notes, Usz len);
	// fillBassWalkWith over the patch's own key + scale.
	void fillBassWalk(std::string& notes, Usz len);
	// Unique variable name from this call's pool, or 0 when exhausted.
	char allocateVarName();
	// Unique CC control number from this call's pool, or 0 when exhausted.
	char allocateCcNumber();

	// RNG hygiene: unbiased draws instead of modulo.
	int randInt(int lo, int hi) {
		std::uniform_int_distribution<int> d(lo, hi);
		return d(rng);
	}
	bool chance(float p) {
		if (p <= 0.0f) return false;
		if (p >= 1.0f) return true;
		std::uniform_real_distribution<float> d(0.0f, 1.0f);
		return d(rng) < p;
	}

	// High-level structure generators

	/**
	 * Generate a complete multi-voice arrangement: multiple independent voices
	 * with shared timing.
	 */
	void generateArrangement(ScratchPad& buf, Usz y, Usz x, Usz h, Usz w, float density,
		size_t budget);

	/**
	 * One raw generation pass, no retry loop (the body of the old generate()).
	 * Seeding is managed by generate(); uses whatever rng state it finds.
	 */
	ScratchPad generateOnce(Usz height, Usz width, Config const& cfg);

	/**
	 * Generate a simple standalone pattern (for small selections)
	 */
	Extent generateSimplePattern(ScratchPad& buf, Usz y, Usz x, Usz maxY, Usz maxX);

	/**
	 * Clock bus: a row of clock divisions at the very top of the arrangement,
	 * each publishing its counter into a reserved variable (q, w, e, r ...).
	 * Writers sit ABOVE their readers (vars are wiped every tick and only
	 * visible below/right). Placed vars are recorded in busVars_.
	 */
	Extent generateClockBus(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW);

	/**
	 * Generate a rhythm/percussion voice using euclidean patterns
	 */
	Extent generateDrumVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW);

	// Pattern building blocks - these are the fundamental ORCA idioms

	/**
	 * Clock pattern: .rCm (rate, modulo) → outputs index below. Used to drive
	 * sequencers and create timing divisions.
	 */
	Extent placeClockPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod);

	/**
	 * Delay/Bang pattern: .rDm → outputs * below on trigger. Used to create
	 * rhythmic patterns.
	 */
	Extent placeDelayPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod);

	/**
	 * Track sequencer: .keyTlen_values → outputs value at key below. The key
	 * cell may be poked externally (e.g. by a clock above) to walk the sequence;
	 * a constant key reads one fixed slot.
	 */
	Extent placeTrackPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char key, const std::string& values);

	/**
	 * Euclidean rhythm: .stepsUmax → outputs * on euclidean hits. Great for
	 * polyrhythmic patterns.
	 */
	Extent placeUclidPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char steps, char max);

	/**
	 * MIDI note output: :channelOctaveNoteVelLen. Needs a bang in a neighbouring
	 * cell to fire.
	 */
	Extent placeMidiPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char channel, char octave, char note, char vel, char len);

	/**
	 * Variable write: .nameVvalue → stores value in variable
	 */
	Extent placeVarWrite(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name, char value);

	/**
	 * Variable read: ..Vname → outputs variable value below
	 */
	Extent placeVarRead(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name);

	/**
	 * Random: .minRmax → outputs random value in range below
	 */
	Extent placeRandomPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char min, char max);

	/**
	 * Increment: .stepImod → increments counter (below) each frame
	 */
	Extent placeIncrementPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char step, char mod);

	/**
	 * Add: .aAb → outputs (a+b) mod 36 below
	 */
	Extent placeAddPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b);

	/**
	 * Multiply: .aMb → outputs (a*b) mod 36 below
	 */
	Extent placeMultiplyPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b);

	/**
	 * If/conditional: .aFb → outputs * if a==b
	 */
	Extent placeIfPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b);

	/**
	 * Halt: H stops operators below until bang
	 */
	Extent placeHaltPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW);

	/**
	 * Offset read: .y x O → O reads x from (0,-1) and y from (0,-2)
	 */
	Extent placeOffsetPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char offX, char offY);

	// Compound patterns - combinations that create musical structures

	/**
	 * Delay triggering MIDI output. Basic rhythmic pattern.
	 */
	Extent placeDelayMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
							 char rate, char mod, char channel, char octave, char note, char velocityVar = 0);

	/**
	 * Euclidean rhythm with MIDI output. Polyrhythmic pattern.
	 */
	Extent placeUclidMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
							 char steps, char max, char channel, char octave, char note, char velocityVar = 0);

};

} // namespace Ahab
} // namespace StoermelderPackOne
