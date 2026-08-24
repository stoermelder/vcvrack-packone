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

// An inbound coupling edge: the consumer
// reads from VoiceNode `from` according to `kind`.
// NB: explicit constructors instead of NSDMI-only — the plugin builds as
// C++11, where NSDMIs disable aggregate brace-initialization (see Extent).
struct Edge {
	size_t from;        // index of the producing VoiceNode
	enum Kind {
		Clock,          // drives the consumer's track key (today: bus division)
		Pitch,          // consumer transposes/derives from the producer's pitch
		Trigger,        // consumer fires only when the producer's pitch matches
		// the consumer maps the producer's published
		// pitch onto a MIDI CC ('!' operator) on the CONSUMER's channel.
		// A tail on an existing voice, not a new voice: it consumes no plan
		// capacity and no channel, only a control number (param).
		Modulation,
		// An OPERAND CELL of the consumer's
		// ':' row is fed from the producer's published pitch through a K
		// (konkat) row — no new event, one more live field in the note the
		// voice already fires. One K row mirrors several Operand edges on the
		// same node; `param` names the target cell (OpCell constants).
		// Deliberately NOT merged into Modulation: different destination
		// (operand cell vs separate CC event), and a different risk class —
		// the midi operator returns early on velocity 0 (it IS a note-off),
		// so an Operand edge can cause silence where Modulation cannot.
		// Builders keep a non-zero literal floor in every K-fed cell and
		// sources are pitch publishers only (glyph indices >= 12). Velocity
		// is also non-linear (idx*8-1, clamped at 127 from index 16).
		Operand
	} kind;
	char param;         // Clock: division index; Pitch: interval; Trigger: note;
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

// One planned voice in an arrangement. The
// planner produces these WITHOUT touching a ScratchPad; layout consumes them,
// filling publishedVar as each voice is placed. Part of the tested surface:
// tests can assert on graph shape directly.
struct VoiceNode {
	enum Role { Bus, Bass, Lead, Harmony, Gate, Drums, Delay, Uclid, Chord };
	Role role;

	char channel = '0';   // melodic voices draw unique channels from the pool
	std::string notes;    // Lead sequence / Chord tones (patch scale)
	char param = '2';     // Harmony: transpose steps; Gate: trigger note

	// Set by layout once the voice is placed: the variable it publishes its
	// pitch into, or 0 if it publishes nothing. Consumers read this.
	char publishedVar = 0;

	// Inbound edges. Empty => a root voice (its own clock, no dependency).
	std::vector<Edge> inputs;
};

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
	// so the arrangement advances past the tallest voice in a row instead of a
	// fixed 3 rows that made 5-row voices overlap each other.
	// NB: explicit constructors instead of NSDMI-only — the plugin builds as
	// C++11, where NSDMIs disable aggregate brace-initialization.
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
		// 6.3 runtime gate: generate-and-reject. When on (default), generate()
		// scores each attempt with scorePattern() and re-rolls deterministically,
		// keeping the best. Turn OFF when measuring the raw generator (the CI
		// gate in Ahab.test.Randomizer.hpp does exactly that).
		bool qualityGate = true;

		// JSON round-trip for everything that should survive patch save/load.
		// The module persists its "settings that produced the current result"
		// as a "generator" subobject; vocabulary plan §5 will add key/scale/
		// feel/coupling fields to Config, and persistence comes along in this
		// one place. clearFirst is deliberately excluded: overlay mode is not
		// implemented and not user-facing.
		static json_t* toJson(Config const& cfg) {
			json_t* j = json_object();
			json_object_set_new(j, "density", json_real(cfg.density));
			json_object_set_new(j, "seed", json_integer((int)cfg.seed));
			json_object_set_new(j, "qualityGate", json_boolean(cfg.qualityGate));
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
			return cfg;
		}
	};

	// seed == 0 draws a nondeterministic seed from std::random_device and
	// stores it, so getSeed() always reports the seed that produced the output.
	explicit AhabGenerator(uint32_t seed = 0) : seed_(seed ? seed : std::random_device{}()), rng(seed_) {}

	/**
	 * Pure core: decide what glyphs to place — deterministic given a seed.
	 * With cfg.qualityGate on, runs the generate-and-reject loop (6.3): each
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

	// Shared rhythmic identity, valid after generate(): every
	// clock/delay/uclid modulus in the patch divides this bar length.
	Usz getBarMod() const { return barMod_; }
	// Clock bus: the variables the top-of-field bus published this
	// call (a subset of "qwer"), in placement order. Voices read these
	// instead of carrying their own clocks.
	std::string const& getBusVars() const { return busVars_; }
	// number of derived voices placed by the last generate().
	int getDerivedPlaced() const { return derivedPlaced_; }
	// One key + scale per patch: root semitone 0..11 and scale index
	// 0=major 1=naturalMinor 2=pentatonic 3=dorian.
	int getPatchRoot() const { return patchRoot_; }
	int getPatchScaleIndex() const { return patchScaleIdx_; }

	/**
	 * Fill notes with a random walk over a random scale (major / natural
	 * minor / pentatonic / dorian). Scales are stored as semitone offsets and
	 * converted to glyphs with the same mapping sim.c uses
	 * (from_midi_note_number: lowercase = sharp), so every emitted glyph is a
	 * note the ':' operator accepts. Static and rng-parameterised so tests can
	 * drive it directly.
	 */
	static void fillScaleWalk(std::string& notes, Usz len, std::mt19937& rng);

	/**
	 * Core of the above with an explicit patch key: root semitone 0..11 and
	 * scale index (0=major 1=naturalMinor 2=pentatonic 3=dorian).
	 */
	static void fillScaleWalkWith(std::string& notes, Usz len, std::mt19937& rng, int root, int scaleIdx);

	/**
	 * Bass line: root and fifth of the patch scale only (vocabulary plan
	 * Step 3) — a foundation, not a melody. Static and rng-parameterised
	 * so tests can drive it directly.
	 */
	static void fillBassWalkWith(std::string& notes, Usz len, std::mt19937& rng, int root, int scaleIdx);

	/**
	 * Chord pattern: multiple MIDI notes triggered together
	 * From chord.orca example. N stacked delay+MIDI pairs with IDENTICAL
	 * rate/mod: D fires when Tick%(rate*mod)==0 regardless of position, so
	 * every note's bang lands on the same tick — a true simultaneous chord.
	 * (The old single-delay fan-out left notes 2..N without a bang neighbour
	 * and permanently silent.)
	 */
	Extent placeChordVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						   char rate, char mod, char channel, char octave, const std::vector<char>& notes,
						   char velocityVar = 0);
	/**
	 * Derived voice: reads another voice's published pitch variable
	 * (which must be WRITTEN ABOVE this voice — vars are within-tick only,
	 * and derives from it:
	 *   gate == false: transposes by param scale-degree steps ('A' add)
	 *                  -> a harmony line locked to the lead.
	 *   gate == true:  fires ONLY when the lead pitch equals param ('F' if)
	 *                  -> call-and-response gating. With sourceVarB != 0 the
	 *                  voice additionally requires the second publisher's
	 *                  pitch to equal paramB: an
	 *                  exact AND-merge (F/J/L/F chain, Extent 7x12) with
	 *                  every cell refreshed each tick, so no stale bang.
	 * Public so tests can drive it against a hand-placed writer.
	 */
	Extent placeDerivedVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						char channel, char sourceVar, bool gate, char param,
						char* allocatedVar = nullptr,
						char sourceVarB = 0, char paramB = 0);
	/**
	 * Modulation tail: maps a producer's published
	 * pitch variable onto a MIDI CC on channel/control. Two rows hung BELOW
	 * the voice it expresses; the producer's V-write must sit above (vars
	 * are within-tick). Public so tests can drive it against a hand-placed
	 * writer.
	 */
	Extent placeModulationTail(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						char channel, char control, char sourceVar);
	/**
	 * Plan half of the arrangement: returns the cast and
	 * their couplings. No glyphs placed, no ScratchPad touched — deterministic
	 * given the instance seed, so tests can assert on plan shape directly.
	 * Returns an empty vector when the area/density budget is exhausted.
	 * @param nBus number of clock-bus divisions already placed above
	 */
	std::vector<VoiceNode> planArrangement(Usz h, Usz w, float density, size_t nBus);

	/**
	 * Arpeggio pattern: clock (or the shared clock bus) → track → variable → MIDI
	 * Example from examples: .gC4 / .14T1324 / .aV3
	 * When sharedVar is a bus variable (q/w/e/r), the voice reads that bus
	 * clock instead of carrying its own C row; 0 = own clock.
	 * If allocatedVar is non-null it receives the variable the voice publishes
	 * its current pitch into — the hook derived voices follow.
	 */
	Extent placeArpeggioVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
												char channel, const std::string& notes, char sharedVar = 0, char* allocatedVar = nullptr,
												char velocityVar = 0, char octave = 0);
	// octave: 0 = random (channel-correlated band), '2'..'4' = pinned low
	// register — the Bass role passes a fixed low octave so the foundation
	// never depends on the channel shuffle (vocabulary plan Step 3).

private:
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
	std::string chanPool_;  // shuffled "0123": unique melodic channel per voice
	int lastChanIdx_ = 0;   // channel index of the most recently planned voice
	std::string ccPool_;    // shuffled "0123": unique CC control number per
							// Modulation edge. Two
							// tails on one (channel, control) pair fight; low
							// numbers also survive midiCcOffset's 127 clamp.
	std::string busVars_;   // clock-bus variables placed this call:
	int derivedPlaced_ = 0; // derived voices placed this call.
	                        // generate() captures it on adoption so it always
	                        // describes the RETURNED attempt, not a later
	                        // rejected retry
	// Helper functions for random values
	char randomBase36();
	char randomSmallDigit();  // 1-8 for rates/mods
	char randomNote();
	char randomOctave();
	char randomChannel();
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
	 * Generate a complete multi-voice arrangement
	 * Creates multiple independent voices with shared timing
	 */
	void generateArrangement(ScratchPad& buf, Usz y, Usz x, Usz h, Usz w, float density);

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
	 * Clock bus: a row of clock divisions at the very top of the
	 * arrangement, each publishing its counter into a reserved variable
	 * (q, w, e, r ...). Writers sit ABOVE their readers (vars are wiped every
	 * tick and only visible below/right — review §7.1). Placed vars are
	 * recorded in busVars_.
	 */
	Extent generateClockBus(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW);

	/**
	 * Generate a rhythm/percussion voice using euclidean patterns
	 */
	Extent generateDrumVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW);

	// Pattern building blocks - these are the fundamental ORCA idioms

	/**
	 * Clock pattern: .rCm (rate, modulo) → outputs index below
	 * Used to drive sequencers and create timing divisions
	 */
	Extent placeClockPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod);

	/**
	 * Delay/Bang pattern: .rDm → outputs * below on trigger
	 * Used to create rhythmic patterns
	 */
	Extent placeDelayPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod);

	/**
	 * Track sequencer: .keyTlen_values → outputs value at key below
	 * The key cell may be poked externally (e.g. by a clock above) to walk
	 * the sequence; a constant key reads one fixed slot.
	 */
	Extent placeTrackPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char key, const std::string& values);

	/**
	 * Euclidean rhythm: .stepsUmax → outputs * on euclidean hits
	 * Great for polyrhythmic patterns
	 */
	Extent placeUclidPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char steps, char max);

	/**
	 * MIDI note output: :channelOctaveNoteVelLen
	 * Needs a bang in a neighbouring cell to fire
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
	 * Delay triggering MIDI output
	 * Basic rhythmic pattern
	 */
	Extent placeDelayMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
							 char rate, char mod, char channel, char octave, char note, char velocityVar = 0);

	/**
	 * Euclidean rhythm with MIDI output
	 * Polyrhythmic pattern
	 */
	Extent placeUclidMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
							 char steps, char max, char channel, char octave, char note, char velocityVar = 0);

};

} // namespace Ahab
} // namespace StoermelderPackOne
