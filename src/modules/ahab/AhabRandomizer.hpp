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

/**
 * AhabRandomizer - Generates musically meaningful random ORCA patterns
 * 
 * Based on analysis of ORCA examples and documentation, this class generates
 * connected patterns that work together. Key principles:
 * 
 * 1. Vertical data flow: Operators output below and read from sides
 * 2. Variable sharing: Variables (a-h) connect distant parts of the field
 * 3. Timing chains: Clocks → Delays → Triggers → MIDI
 * 4. Modular voices: Each voice has a complete signal chain
 */
struct AhabRandomizer {
	struct Config {
		float density = 0.3f;
		uint32_t seed = 0;       // 0 = keep the seed given to the constructor
		bool clearFirst = true;  // paste overwrites the whole rect; overlay mode not yet supported
	};

	// seed == 0 draws a nondeterministic seed from std::random_device and
	// stores it, so getSeed() always reports the seed that produced the output.
	explicit AhabRandomizer(uint32_t seed = 0) : seed_(seed ? seed : std::random_device{}()), rng(seed_) {}

	/**
	 * Pure core: decide what glyphs to place. No sim, no threads —
	 * deterministic given a seed. This is what the tests drive.
	 * @param height Number of rows to generate
	 * @param width Number of columns to generate
	 * @param cfg Density / seed / clear options
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

private:
	uint32_t seed_;
	std::mt19937 rng;
	
	// Helper functions for random values
	char randomBase36();
	char randomSmallDigit();  // 1-8 for rates/mods
	char randomNote();
	char randomOctave();
	char randomVarName();
	char randomChannel();
	
	// Pitch/scale helpers
	void fillNoteSequence(ScratchPad& buf, Usz y, Usz x, Usz len);
	void fillScaleSequence(ScratchPad& buf, Usz y, Usz x, Usz len, int scaleType);
	
	// High-level structure generators
	
	/**
	 * Generate a complete multi-voice arrangement
	 * Creates multiple independent voices with shared timing
	 */
	void generateArrangement(ScratchPad& buf, Usz y, Usz x, Usz h, Usz w, float density);
	
	/**
	 * Generate a single voice (timing + sequencing + output)
	 * Returns width used
	 */
	Usz generateVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW, int voiceNum);
	
	/**
	 * Generate a simple standalone pattern (for small selections)
	 */
	Usz generateSimplePattern(ScratchPad& buf, Usz y, Usz x, Usz maxY, Usz maxX);
	
	/**
	 * Generate a master clock section that sets shared variables
	 */
	Usz generateMasterClock(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW);
	
	/**
	 * Generate a rhythm/percussion voice using euclidean patterns
	 */
	Usz generateDrumVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW, int channel);
	
	/**
	 * Generate a melodic voice with sequencer
	 */
	Usz generateMelodicVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW, int channel, char varName);
	
	// Pattern building blocks - these are the fundamental ORCA idioms
	
	/** 
	 * Clock pattern: .rCm (rate, modulo) → outputs index below
	 * Used to drive sequencers and create timing divisions
	 */
	Usz placeClockPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod);
	
	/**
	 * Delay/Bang pattern: .rDm → outputs * below on trigger
	 * Used to create rhythmic patterns
	 */
	Usz placeDelayPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod);
	
	/**
	 * Track sequencer: .keyTlen_values → outputs value at key below
	 * Core melodic sequencer - reads from a sequence based on key
	 */
	Usz placeTrackPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char key, const std::string& values);
	
	/**
	 * Euclidean rhythm: .stepsUmax → outputs * on euclidean hits
	 * Great for polyrhythmic patterns
	 */
	Usz placeUclidPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char steps, char max);
	
	/**
	 * MIDI note output: *:channelOctaveNoteVelLen
	 * The bang (*) must be present for output
	 */
	Usz placeMidiPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char channel, char octave, char note, char vel, char len);
	
	/**
	 * Variable write: .nameVvalue → stores value in variable
	 */
	Usz placeVarWrite(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name, char value);
	
	/**
	 * Variable read: ..Vname → outputs variable value below
	 */
	Usz placeVarRead(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name);
	
	/**
	 * Konkat: .lenKvars → reads multiple variables
	 */
	Usz placeKonkatPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, const std::string& vars);
	
	/**
	 * Random: .minRmax → outputs random value in range below
	 */
	Usz placeRandomPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char min, char max);
	
	/**
	 * Increment: .stepImod → increments counter (below) each frame
	 */
	Usz placeIncrementPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char step, char mod);
	
	/**
	 * Add: .aAb → outputs (a+b) mod 36 below
	 */
	Usz placeAddPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b);
	
	/**
	 * Multiply: .aMb → outputs (a*b) mod 36 below
	 */
	Usz placeMultiplyPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b);
	
	/**
	 * If/conditional: .aFb → outputs * if a==b
	 */
	Usz placeIfPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b);
	
	/**
	 * Halt: H stops operators below until bang
	 */
	Usz placeHaltPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW);
	
	/**
	 * Offset read: .x.yO → reads from relative position (y,x+1)
	 */
	Usz placeOffsetPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char offX, char offY);
	
	// Compound patterns - combinations that create musical structures
	
	/**
	 * Clock driving a Track sequencer with variable output
	 * Classic melodic voice pattern
	 */
	Usz placeClockTrackVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW, 
							  char rate, const std::string& notes, char varName);
	
	/**
	 * Delay triggering MIDI output
	 * Basic rhythmic pattern
	 */
	Usz placeDelayMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
							 char rate, char mod, char channel, char octave, char note);
	
	/**
	 * Euclidean rhythm with MIDI output
	 * Polyrhythmic pattern
	 */
	Usz placeUclidMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
							 char steps, char max, char channel, char octave, char note);
	
	/**
	 * Arpeggio pattern: clock → track → variable → MIDI
	 * Example from examples: .gC4 / .14T1324 / .aV3
	 */
	Usz placeArpeggioVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						   char channel, const std::string& notes);
	
	/**
	 * Chord pattern: multiple MIDI notes triggered together
	 * From chord.orca example
	 */
	Usz placeChordVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
						char channel, char octave, const std::vector<char>& notes);
	
	/**
	 * Random melody: random generator → variable → MIDI
	 */
	Usz placeRandomMelodyVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
							   char channel, char minNote, char maxNote);
	
	// Helper to safely place a glyph (bounds-checked by the buffer)
	void trySet(ScratchPad& buf, Usz y, Usz x, char g);
};

} // namespace Ahab
} // namespace StoermelderPackOne
