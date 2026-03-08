#pragma once
#include "../../plugin.hpp"
#include "AhabSim.hpp"
#include <vector>
#include <functional>
#include <random>

extern "C" {
	#include "../../../orca-c/field.h"
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
class AhabRandomizer {
public:
	AhabRandomizer() : rng(std::random_device{}()) {}
	
	/**
	 * Randomize a region of the field with connected musical patterns
	 * @param sim The simulation to modify
	 * @param startY Top row of selection (0-indexed)
	 * @param startX Left column of selection (0-indexed)
	 * @param height Number of rows in selection
	 * @param width Number of columns in selection
	 * @param density How densely to fill (0.0-1.0)
	 */
	void randomize(AhabSim* sim, Usz startY, Usz startX, Usz height, Usz width, float density = 0.3f);

private:
	std::mt19937 rng;
	
	// Helper functions for random values
	char randomBase36();
	char randomSmallDigit();  // 1-8 for rates/mods
	char randomNote();
	char randomOctave();
	char randomVarName();
	char randomChannel();
	
	// Pitch/scale helpers
	void fillNoteSequence(AhabSim* sim, Usz y, Usz x, Usz len, Usz fieldW);
	void fillScaleSequence(AhabSim* sim, Usz y, Usz x, Usz len, Usz fieldW, int scaleType);
	
	// High-level structure generators
	
	/**
	 * Generate a complete multi-voice arrangement
	 * Creates multiple independent voices with shared timing
	 */
	void generateArrangement(AhabSim* sim, Usz y, Usz x, Usz h, Usz w, float density);
	
	/**
	 * Generate a single voice (timing + sequencing + output)
	 * Returns width used
	 */
	Usz generateVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW, int voiceNum);
	
	/**
	 * Generate a simple standalone pattern (for small selections)
	 */
	Usz generateSimplePattern(AhabSim* sim, Usz y, Usz x, Usz maxY, Usz maxX);
	
	/**
	 * Generate a master clock section that sets shared variables
	 */
	Usz generateMasterClock(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW);
	
	/**
	 * Generate a rhythm/percussion voice using euclidean patterns
	 */
	Usz generateDrumVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW, int channel);
	
	/**
	 * Generate a melodic voice with sequencer
	 */
	Usz generateMelodicVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW, int channel, char varName);
	
	// Pattern building blocks - these are the fundamental ORCA idioms
	
	/** 
	 * Clock pattern: .rCm (rate, modulo) → outputs index below
	 * Used to drive sequencers and create timing divisions
	 */
	Usz placeClockPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char rate, char mod);
	
	/**
	 * Delay/Bang pattern: .rDm → outputs * below on trigger
	 * Used to create rhythmic patterns
	 */
	Usz placeDelayPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char rate, char mod);
	
	/**
	 * Track sequencer: .keyTlen_values → outputs value at key below
	 * Core melodic sequencer - reads from a sequence based on key
	 */
	Usz placeTrackPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char key, const std::string& values);
	
	/**
	 * Euclidean rhythm: .stepsUmax → outputs * on euclidean hits
	 * Great for polyrhythmic patterns
	 */
	Usz placeUclidPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char steps, char max);
	
	/**
	 * MIDI note output: *:channelOctaveNoteVelLen
	 * The bang (*) must be present for output
	 */
	Usz placeMidiPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char channel, char octave, char note, char vel, char len);
	
	/**
	 * Variable write: .nameVvalue → stores value in variable
	 */
	Usz placeVarWrite(AhabSim* sim, Usz y, Usz x, Usz maxW, char name, char value);
	
	/**
	 * Variable read: ..Vname → outputs variable value below
	 */
	Usz placeVarRead(AhabSim* sim, Usz y, Usz x, Usz maxW, char name);
	
	/**
	 * Konkat: .lenKvars → reads multiple variables
	 */
	Usz placeKonkatPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, const std::string& vars);
	
	/**
	 * Random: .minRmax → outputs random value in range below
	 */
	Usz placeRandomPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char min, char max);
	
	/**
	 * Increment: .stepImod → increments counter (below) each frame
	 */
	Usz placeIncrementPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char step, char mod);
	
	/**
	 * Add: .aAb → outputs (a+b) mod 36 below
	 */
	Usz placeAddPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char a, char b);
	
	/**
	 * Multiply: .aMb → outputs (a*b) mod 36 below
	 */
	Usz placeMultiplyPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char a, char b);
	
	/**
	 * If/conditional: .aFb → outputs * if a==b
	 */
	Usz placeIfPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char a, char b);
	
	/**
	 * Halt: H stops operators below until bang
	 */
	Usz placeHaltPattern(AhabSim* sim, Usz y, Usz x, Usz maxW);
	
	/**
	 * Offset read: .x.yO → reads from relative position (y,x+1)
	 */
	Usz placeOffsetPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char offX, char offY);
	
	// Compound patterns - combinations that create musical structures
	
	/**
	 * Clock driving a Track sequencer with variable output
	 * Classic melodic voice pattern
	 */
	Usz placeClockTrackVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW, 
							  char rate, const std::string& notes, char varName);
	
	/**
	 * Delay triggering MIDI output
	 * Basic rhythmic pattern
	 */
	Usz placeDelayMidiVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
							 char rate, char mod, char channel, char octave, char note);
	
	/**
	 * Euclidean rhythm with MIDI output
	 * Polyrhythmic pattern
	 */
	Usz placeUclidMidiVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
							 char steps, char max, char channel, char octave, char note);
	
	/**
	 * Arpeggio pattern: clock → track → variable → MIDI
	 * Example from examples: .gC4 / .14T1324 / .aV3
	 */
	Usz placeArpeggioVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
						   char channel, const std::string& notes);
	
	/**
	 * Chord pattern: multiple MIDI notes triggered together
	 * From chord.orca example
	 */
	Usz placeChordVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
						char channel, char octave, const std::vector<char>& notes);
	
	/**
	 * Random melody: random generator → variable → MIDI
	 */
	Usz placeRandomMelodyVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
							   char channel, char minNote, char maxNote);
	
	// Helper to safely place a glyph
	void trySet(AhabSim* sim, Usz y, Usz x, char g, Usz fieldH, Usz fieldW);
};

} // namespace Ahab
} // namespace StoermelderPackOne
