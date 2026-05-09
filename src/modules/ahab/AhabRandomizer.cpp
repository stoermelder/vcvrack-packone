#include "AhabRandomizer.hpp"

namespace StoermelderPackOne {
namespace Ahab {

// Helper functions
char AhabRandomizer::randomBase36() {
	static const char* b36 = "0123456789abcdefghijklmnopqrstuvwxyz";
	return b36[rng() % 36];
}

char AhabRandomizer::randomSmallDigit() {
	return '1' + (rng() % 8);
}

char AhabRandomizer::randomNote() {
	static const char* notes = "CDEFGAB";
	return notes[rng() % 7];
}

char AhabRandomizer::randomOctave() {
	return '2' + (rng() % 5); // octaves 2-6
}

char AhabRandomizer::randomVarName() {
	return 'a' + (rng() % 8); // a-h for variables
}

char AhabRandomizer::randomChannel() {
	return '0' + (rng() % 4); // channels 0-3
}

void AhabRandomizer::trySet(AhabSim* sim, Usz y, Usz x, char g, Usz fieldH, Usz fieldW) {
	if (y < fieldH && x < fieldW) {
		sim->setGlyphRequest(y, x, g, Mark_flag_none, false);
	}
}

void AhabRandomizer::fillNoteSequence(AhabSim* sim, Usz y, Usz x, Usz len, Usz fieldW) {
	// Mix of natural notes - creates more consonant melodies
	static const char* notes = "CDEFGABcdefgab";
	for (Usz i = 0; i < len && x + i < fieldW; ++i) {
		trySet(sim, y, x + i, notes[rng() % 14], sim->getFieldHeight(), fieldW);
	}
}

void AhabRandomizer::fillScaleSequence(AhabSim* sim, Usz y, Usz x, Usz len, Usz fieldW, int scaleType) {
	// Common musical scales
	static const char* majorScale = "CDEFGABcdefgab";
	static const char* minorScale = "CDdFGgaBcddfga";
	static const char* pentatonic = "CDFGACDFGacdfg";
	
	const char* scale = majorScale;
	switch (scaleType % 3) {
		case 1: scale = minorScale; break;
		case 2: scale = pentatonic; break;
	}
	
	Usz scaleLen = 14;
	for (Usz i = 0; i < len && x + i < fieldW; ++i) {
		trySet(sim, y, x + i, scale[rng() % scaleLen], sim->getFieldHeight(), fieldW);
	}
}

// Main randomize function
void AhabRandomizer::randomize(AhabSim* sim, Usz startY, Usz startX, Usz height, Usz width, float density) {
	if (!sim) return;
	
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	// Clamp selection to field bounds
	if (startY >= fieldH || startX >= fieldW) return;
	if (startY + height > fieldH) height = fieldH - startY;
	if (startX + width > fieldW) width = fieldW - startX;
	
	sim->pushUndo();
	
	// For small selections, just fill with simpler patterns
	if (height < 4 || width < 10) {
		// Simple fill mode - individual patterns
		for (Usz y = startY; y < startY + height; ) {
			for (Usz x = startX; x < startX + width; ) {
				if ((float)(rng() % 100) / 100.0f < density) {
					Usz used = generateSimplePattern(sim, y, x, startY + height, startX + width);
					x += used > 0 ? used + 1 : 1;
				} else {
					x++;
				}
			}
			y += 2;
		}
	} else {
		// Full arrangement mode
		generateArrangement(sim, startY, startX, height, width, density);
	}
	
	sim->notifyTick();
}

Usz AhabRandomizer::generateSimplePattern(AhabSim* sim, Usz y, Usz x, Usz maxY, Usz maxX) {
	Usz availW = maxX > x ? maxX - x : 0;
	Usz availH = maxY > y ? maxY - y : 0;
	
	if (availW < 4 || availH < 1) return 0;
	
	int patternType = rng() % 8;
	
	switch (patternType) {
		case 0: // Clock
			return placeClockPattern(sim, y, x, availW, randomSmallDigit(), randomBase36());
		case 1: // Delay
			return placeDelayPattern(sim, y, x, availW, randomSmallDigit(), randomSmallDigit());
		case 2: // Random
			return placeRandomPattern(sim, y, x, availW, '0', randomBase36());
		case 3: // Uclid
			return placeUclidPattern(sim, y, x, availW, randomSmallDigit(), '8');
		case 4: // Variable write
			return placeVarWrite(sim, y, x, availW, randomVarName(), randomBase36());
		case 5: // Add
			return placeAddPattern(sim, y, x, availW, randomBase36(), randomBase36());
		case 6: // If
			return placeIfPattern(sim, y, x, availW, randomVarName(), randomBase36());
		case 7: { // Track with short sequence
			std::string notes;
			for (int i = 0; i < 4; i++) notes += randomNote();
			return placeTrackPattern(sim, y, x, availW, randomBase36(), notes);
		}
	}
	return 0;
}

void AhabRandomizer::generateArrangement(AhabSim* sim, Usz y, Usz x, Usz h, Usz w, float density) {
	// Generate a grid-based arrangement with multiple patterns
	// Each cell in the grid can potentially contain a pattern based on density
	
	Usz currentY = y;
	
	// Place master clock at top if we have room
	if (h >= 3 && w >= 8 && density > 0.2f) {
		generateMasterClock(sim, y, x, std::min(h, (Usz)3), std::min(w, (Usz)8));
		currentY = y + 3;
	}
	
	// Fill the remaining space with patterns
	while (currentY < y + h) {
		Usz currentX = x;
		Usz remainingH = (y + h) - currentY;
		
		while (currentX < x + w) {
			Usz remainingW = (x + w) - currentX;
			
			// Check density threshold
			if ((float)(rng() % 100) / 100.0f < density) {
				// Determine pattern type based on available space
				Usz patternH = std::min(remainingH, (Usz)8);
				Usz patternW = std::min(remainingW, (Usz)16);
				
				if (patternH >= 3 && patternW >= 8) {
					// Place a voice (complete pattern chain)
					int voiceType = rng() % 5;
					Usz used = 0;
					
					switch (voiceType) {
						case 0: { // Arpeggio
							std::string notes;
							int numNotes = 4 + (rng() % 5);
							for (int i = 0; i < numNotes; i++) notes += randomNote();
							used = placeArpeggioVoice(sim, currentY, currentX, patternH, patternW, randomChannel(), notes);
							break;
						}
						case 1: // Drum
							used = generateDrumVoice(sim, currentY, currentX, patternH, patternW, rng() % 4);
							break;
						case 2: // Delay MIDI
							used = placeDelayMidiVoice(sim, currentY, currentX, patternH, patternW,
													   randomSmallDigit(), randomSmallDigit(),
													   randomChannel(), randomOctave(), randomNote());
							break;
						case 3: // Random melody
							used = placeRandomMelodyVoice(sim, currentY, currentX, patternH, patternW,
														  randomChannel(), '0', 'c');
							break;
						case 4: // Euclidean MIDI
							used = placeUclidMidiVoice(sim, currentY, currentX, patternH, patternW,
													   randomSmallDigit(), '8',
													   randomChannel(), randomOctave(), randomNote());
							break;
					}
					
					currentX += (used > 0 ? used + 1 : 8);
				} else if (patternH >= 2 && patternW >= 5) {
					// Place a simple pattern
					Usz used = generateSimplePattern(sim, currentY, currentX, currentY + patternH, currentX + patternW);
					currentX += (used > 0 ? used + 1 : 5);
				} else {
					currentX += 2;
				}
			} else {
				// Skip this position
				currentX += 2;
			}
		}
		
		// Move to next row, with varying spacing
		currentY += 3 + (rng() % 3);
	}
}

Usz AhabRandomizer::generateMasterClock(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Place a clock that stores to variable 'g' (global clock)
	// .4Cg / .gV. (clock mod g, store to variable g)
	char rate = randomSmallDigit();
	char mod = '4' + (rng() % 5); // 4-8
	
	// Row 0: Clock
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, rate, fieldH, fieldW);
	trySet(sim, y, x+2, 'C', fieldH, fieldW);
	trySet(sim, y, x+3, mod, fieldH, fieldW);
	
	// Row 1: Store to variable 'g'
	trySet(sim, y+1, x, '.', fieldH, fieldW);
	trySet(sim, y+1, x+1, 'g', fieldH, fieldW);
	trySet(sim, y+1, x+2, 'V', fieldH, fieldW);
	
	return 5;
}

Usz AhabRandomizer::generateVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW, int voiceNum) {
	if (maxH < 3 || maxW < 8) return 0;
	
	// Choose voice type based on voice number for variety
	int voiceType = (voiceNum + rng()) % 5;
	
	switch (voiceType) {
		case 0: // Melodic voice using arpeggio pattern
		{
			std::string notes;
			int numNotes = 4 + (rng() % 5);
			for (int i = 0; i < numNotes; i++) notes += randomNote();
			return placeArpeggioVoice(sim, y, x, maxH, maxW, randomChannel(), notes);
		}
		
		case 1: // Drum voice with euclidean rhythm
			return generateDrumVoice(sim, y, x, maxH, maxW, voiceNum);
		
		case 2: // Delay-based rhythmic voice
			return placeDelayMidiVoice(sim, y, x, maxH, maxW, 
									   randomSmallDigit(), randomSmallDigit(),
									   randomChannel(), randomOctave(), randomNote());
		
		case 3: // Random melody
			return placeRandomMelodyVoice(sim, y, x, maxH, maxW, 
										  randomChannel(), '0', 'c');
		
		case 4: // Euclidean MIDI
			return placeUclidMidiVoice(sim, y, x, maxH, maxW,
									   randomSmallDigit(), '8',
									   randomChannel(), randomOctave(), randomNote());
	}
	
	return 0;
}

Usz AhabRandomizer::generateDrumVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW, int channel) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Euclidean pattern for drums
	char steps = '2' + (rng() % 6); // 2-7 steps
	
	// Row 0: Uclid - .3U8
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, steps, fieldH, fieldW);
	trySet(sim, y, x+2, 'U', fieldH, fieldW);
	trySet(sim, y, x+3, '8', fieldH, fieldW);
	
	// Row 1: MIDI - :0xCg1 (drum note on MIDI channel 9/10)
	trySet(sim, y+1, x+2, ':', fieldH, fieldW);
	trySet(sim, y+1, x+3, '9', fieldH, fieldW);  // MIDI channel 10 (drums)
	trySet(sim, y+1, x+4, '2', fieldH, fieldW);  // Octave 2
	trySet(sim, y+1, x+5, "CDEG"[channel % 4], fieldH, fieldW); // Different drum hits
	trySet(sim, y+1, x+6, 'f', fieldH, fieldW);  // High velocity
	trySet(sim, y+1, x+7, '1', fieldH, fieldW);  // Short duration
	
	return 8;
}

Usz AhabRandomizer::generateMelodicVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW, int channel, char varName) {
	// Delegate to arpeggio voice
	std::string notes;
	int numNotes = 4 + (rng() % 5);
	for (int i = 0; i < numNotes; i++) notes += randomNote();
	return placeArpeggioVoice(sim, y, x, maxH, maxW, '0' + (channel % 10), notes);
}

// Pattern building blocks implementation

Usz AhabRandomizer::placeClockPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char rate, char mod) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .rCm format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, rate, fieldH, fieldW);
	trySet(sim, y, x+2, 'C', fieldH, fieldW);
	trySet(sim, y, x+3, mod, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeDelayPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char rate, char mod) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .rDm format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, rate, fieldH, fieldW);
	trySet(sim, y, x+2, 'D', fieldH, fieldW);
	trySet(sim, y, x+3, mod, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeTrackPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char key, const std::string& values) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	Usz len = values.size();
	if (maxW < 4 + len) return 0;
	
	// .keyTlen_values format: ..kT + values
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, key, fieldH, fieldW);
	char lenChar = (len < 10) ? '0' + len : 'a' + (len - 10);
	trySet(sim, y, x+2, lenChar, fieldH, fieldW);
	trySet(sim, y, x+3, 'T', fieldH, fieldW);
	
	for (Usz i = 0; i < len; ++i) {
		trySet(sim, y, x+4+i, values[i], fieldH, fieldW);
	}
	
	return 4 + len;
}

Usz AhabRandomizer::placeUclidPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char steps, char max) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .sUm format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, steps, fieldH, fieldW);
	trySet(sim, y, x+2, 'U', fieldH, fieldW);
	trySet(sim, y, x+3, max, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeMidiPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, 
									  char channel, char octave, char note, char vel, char len) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 6) return 0;
	
	// :cOnvl format (colon operator for MIDI)
	trySet(sim, y, x, ':', fieldH, fieldW);
	trySet(sim, y, x+1, channel, fieldH, fieldW);
	trySet(sim, y, x+2, octave, fieldH, fieldW);
	trySet(sim, y, x+3, note, fieldH, fieldW);
	trySet(sim, y, x+4, vel, fieldH, fieldW);
	trySet(sim, y, x+5, len, fieldH, fieldW);
	
	return 6;
}

Usz AhabRandomizer::placeVarWrite(AhabSim* sim, Usz y, Usz x, Usz maxW, char name, char value) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .nVv format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, name, fieldH, fieldW);
	trySet(sim, y, x+2, 'V', fieldH, fieldW);
	trySet(sim, y, x+3, value, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeVarRead(AhabSim* sim, Usz y, Usz x, Usz maxW, char name) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 3) return 0;
	
	// ..Vn format (read variable, output below)
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, '.', fieldH, fieldW);
	trySet(sim, y, x+2, 'V', fieldH, fieldW);
	trySet(sim, y, x+3, name, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeKonkatPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, const std::string& vars) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	Usz len = vars.size();
	if (maxW < 3 + len) return 0;
	
	// .lKvars format
	trySet(sim, y, x, '.', fieldH, fieldW);
	char lenChar = (len < 10) ? '0' + len : 'a' + (len - 10);
	trySet(sim, y, x+1, lenChar, fieldH, fieldW);
	trySet(sim, y, x+2, 'K', fieldH, fieldW);
	
	for (Usz i = 0; i < len; ++i) {
		trySet(sim, y, x+3+i, vars[i], fieldH, fieldW);
	}
	
	return 3 + len;
}

Usz AhabRandomizer::placeRandomPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char min, char max) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .mRx format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, min, fieldH, fieldW);
	trySet(sim, y, x+2, 'R', fieldH, fieldW);
	trySet(sim, y, x+3, max, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeIncrementPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char step, char mod) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .sIm format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, step, fieldH, fieldW);
	trySet(sim, y, x+2, 'I', fieldH, fieldW);
	trySet(sim, y, x+3, mod, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeAddPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char a, char b) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .aAb format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, a, fieldH, fieldW);
	trySet(sim, y, x+2, 'A', fieldH, fieldW);
	trySet(sim, y, x+3, b, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeMultiplyPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char a, char b) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .aMb format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, a, fieldH, fieldW);
	trySet(sim, y, x+2, 'M', fieldH, fieldW);
	trySet(sim, y, x+3, b, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeIfPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char a, char b) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .aFb format
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, a, fieldH, fieldW);
	trySet(sim, y, x+2, 'F', fieldH, fieldW);
	trySet(sim, y, x+3, b, fieldH, fieldW);
	
	return 4;
}

Usz AhabRandomizer::placeHaltPattern(AhabSim* sim, Usz y, Usz x, Usz maxW) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 1) return 0;
	
	trySet(sim, y, x, 'H', fieldH, fieldW);
	
	return 1;
}

Usz AhabRandomizer::placeOffsetPattern(AhabSim* sim, Usz y, Usz x, Usz maxW, char offX, char offY) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxW < 4) return 0;
	
	// .x.yO format (read from offset position)
	trySet(sim, y, x, '.', fieldH, fieldW);
	trySet(sim, y, x+1, offX, fieldH, fieldW);
	trySet(sim, y, x+2, offY, fieldH, fieldW);
	trySet(sim, y, x+3, 'O', fieldH, fieldW);
	
	return 4;
}

// Compound pattern implementations

Usz AhabRandomizer::placeClockTrackVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
										  char rate, const std::string& notes, char varName) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	Usz noteLen = notes.size();
	if (maxH < 3 || maxW < 5 + noteLen) return 0;
	
	char mod = (noteLen < 10) ? '0' + noteLen : 'a' + (noteLen - 10);
	
	// Row 0: Clock - .rCm
	placeClockPattern(sim, y, x, maxW, rate, mod);
	
	// Row 1: Track - ..mT + notes (clock output feeds into track key position)
	trySet(sim, y+1, x, '.', fieldH, fieldW);
	trySet(sim, y+1, x+1, mod, fieldH, fieldW);
	trySet(sim, y+1, x+2, mod, fieldH, fieldW);
	trySet(sim, y+1, x+3, 'T', fieldH, fieldW);
	for (Usz i = 0; i < noteLen; ++i) {
		trySet(sim, y+1, x+4+i, notes[i], fieldH, fieldW);
	}
	
	// Row 2: Store to variable
	placeVarWrite(sim, y+2, x, maxW, varName, '.'); // The track output will fill in the value
	
	return 4 + noteLen;
}

Usz AhabRandomizer::placeDelayMidiVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
										 char rate, char mod, char channel, char octave, char note) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Row 0: Delay - .rDm
	placeDelayPattern(sim, y, x, maxW, rate, mod);
	
	// Row 1: MIDI - the delay outputs * which triggers the MIDI
	// Position MIDI so the * lands on the bang position
	trySet(sim, y+1, x+2, ':', fieldH, fieldW);
	trySet(sim, y+1, x+3, channel, fieldH, fieldW);
	trySet(sim, y+1, x+4, octave, fieldH, fieldW);
	trySet(sim, y+1, x+5, note, fieldH, fieldW);
	trySet(sim, y+1, x+6, 'f', fieldH, fieldW); // velocity
	trySet(sim, y+1, x+7, '4', fieldH, fieldW); // length
	
	return 8;
}

Usz AhabRandomizer::placeUclidMidiVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
										 char steps, char max, char channel, char octave, char note) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Row 0: Uclid - .sUm
	placeUclidPattern(sim, y, x, maxW, steps, max);
	
	// Row 1: MIDI - the uclid outputs * which triggers the MIDI
	trySet(sim, y+1, x+2, ':', fieldH, fieldW);
	trySet(sim, y+1, x+3, channel, fieldH, fieldW);
	trySet(sim, y+1, x+4, octave, fieldH, fieldW);
	trySet(sim, y+1, x+5, note, fieldH, fieldW);
	trySet(sim, y+1, x+6, 'c', fieldH, fieldW); // velocity
	trySet(sim, y+1, x+7, '2', fieldH, fieldW); // length
	
	return 8;
}

Usz AhabRandomizer::placeArpeggioVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
									   char channel, const std::string& notes) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	Usz noteLen = notes.size();
	if (maxH < 4 || maxW < std::max((Usz)8, 5 + noteLen)) return 0;
	
	char mod = (noteLen < 10) ? '0' + noteLen : 'a' + (noteLen - 10);
	char rate = randomSmallDigit();
	
	// Based on arpeggio.orca example:
	// .gC4
	// .14T1324
	// .aV3
	// H.Va
	// *:02J
	
	// Row 0: Clock
	placeClockPattern(sim, y, x, maxW, rate, mod);
	
	// Row 1: Track with notes
	trySet(sim, y+1, x, '.', fieldH, fieldW);
	trySet(sim, y+1, x+1, '1', fieldH, fieldW);
	trySet(sim, y+1, x+2, mod, fieldH, fieldW);
	trySet(sim, y+1, x+3, 'T', fieldH, fieldW);
	for (Usz i = 0; i < noteLen && x+4+i < fieldW; ++i) {
		trySet(sim, y+1, x+4+i, notes[i], fieldH, fieldW);
	}
	
	// Row 2: Store to variable
	char varName = randomVarName();
	placeVarWrite(sim, y+2, x, maxW, varName, '.');
	
	// Row 3: Halt + Variable read + MIDI
	trySet(sim, y+3, x, 'H', fieldH, fieldW);
	trySet(sim, y+3, x+1, '.', fieldH, fieldW);
	trySet(sim, y+3, x+2, 'V', fieldH, fieldW);
	trySet(sim, y+3, x+3, varName, fieldH, fieldW);
	
	// Row 4: MIDI output (bang comes from delay or uclid above in a real patch)
	// Using * to show where the trigger should be
	trySet(sim, y+4, x, '*', fieldH, fieldW);
	trySet(sim, y+4, x+1, ':', fieldH, fieldW);
	trySet(sim, y+4, x+2, channel, fieldH, fieldW);
	trySet(sim, y+4, x+3, randomOctave(), fieldH, fieldW);
	// Note comes from the variable read above
	
	return std::max((Usz)8, 4 + noteLen);
}

Usz AhabRandomizer::placeChordVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
									char channel, char octave, const std::vector<char>& notes) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxH < 2 + notes.size() || maxW < 8) return 0;
	
	// Row 0: Delay for triggering
	placeDelayPattern(sim, y, x, maxW, randomSmallDigit(), '4');
	
	// Rows 1+: Multiple MIDI notes (chord)
	for (Usz i = 0; i < notes.size() && y+1+i < fieldH; ++i) {
		trySet(sim, y+1+i, x+2, ':', fieldH, fieldW);
		trySet(sim, y+1+i, x+3, channel, fieldH, fieldW);
		trySet(sim, y+1+i, x+4, octave, fieldH, fieldW);
		trySet(sim, y+1+i, x+5, notes[i], fieldH, fieldW);
		trySet(sim, y+1+i, x+6, 'c', fieldH, fieldW);
		trySet(sim, y+1+i, x+7, '8', fieldH, fieldW);
	}
	
	return 8;
}

Usz AhabRandomizer::placeRandomMelodyVoice(AhabSim* sim, Usz y, Usz x, Usz maxH, Usz maxW,
										   char channel, char minNote, char maxNote) {
	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();
	
	if (maxH < 3 || maxW < 8) return 0;
	
	char varName = randomVarName();
	
	// Row 0: Random - .0Rz
	placeRandomPattern(sim, y, x, maxW, minNote, maxNote);
	
	// Row 1: Store to variable
	placeVarWrite(sim, y+1, x, maxW, varName, '.');
	
	// Row 2: Delay for rhythm
	placeDelayPattern(sim, y+2, x, maxW, randomSmallDigit(), '4');
	
	// Row 3: Read variable + MIDI
	if (maxH >= 4) {
		trySet(sim, y+3, x, 'H', fieldH, fieldW);
		trySet(sim, y+3, x+1, '.', fieldH, fieldW);
		trySet(sim, y+3, x+2, 'V', fieldH, fieldW);
		trySet(sim, y+3, x+3, varName, fieldH, fieldW);
		
		// Row 4: MIDI
		if (maxH >= 5) {
			trySet(sim, y+4, x+2, ':', fieldH, fieldW);
			trySet(sim, y+4, x+3, channel, fieldH, fieldW);
			trySet(sim, y+4, x+4, randomOctave(), fieldH, fieldW);
			// Note placeholder - will be filled by variable
		}
	}
	
	return 8;
}

} // namespace Ahab
} // namespace StoermelderPackOne
