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

void AhabRandomizer::trySet(ScratchPad& buf, Usz y, Usz x, char g) {
	buf.set(y, x, g);
}

void AhabRandomizer::fillNoteSequence(ScratchPad& buf, Usz y, Usz x, Usz len) {
	// Mix of natural notes - creates more consonant melodies
	static const char* notes = "CDEFGABcdefgab";
	for (Usz i = 0; i < len && x + i < buf.width(); ++i) {
		buf.set(y, x + i, notes[rng() % 14]);
	}
}

void AhabRandomizer::fillScaleSequence(ScratchPad& buf, Usz y, Usz x, Usz len, int scaleType) {
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
	for (Usz i = 0; i < len && x + i < buf.width(); ++i) {
		buf.set(y, x + i, scale[rng() % scaleLen]);
	}
}

// Pure core: decide what glyphs to place. No sim, no threads — deterministic
// given the instance seed (or the cfg.seed override). The returned buffer is
// selection-local: (0,0) is the top-left of the generated region.
ScratchPad AhabRandomizer::generate(Usz height, Usz width, Config const& cfg) {
	if (cfg.seed != 0) {
		seed_ = cfg.seed;
		rng.seed(seed_);
	}

	ScratchPad buf(height, width);

	// For small selections, just fill with simpler patterns
	if (height < 4 || width < 10) {
		// Simple fill mode - individual patterns
		for (Usz y = 0; y < height; ) {
			for (Usz x = 0; x < width; ) {
				if ((float)(rng() % 100) / 100.0f < cfg.density) {
					Usz used = generateSimplePattern(buf, y, x, height, width);
					x += used > 0 ? used + 1 : 1;
				} else {
					x++;
				}
			}
			y += 2;
		}
	} else {
		// Full arrangement mode
		generateArrangement(buf, 0, 0, height, width, cfg.density);
	}

	return buf;
}

// Sim adapter: generates into a buffer, then commits it in ONE queued command.
bool AhabRandomizer::randomize(AhabSim* sim, Usz startY, Usz startX,
							   Usz height, Usz width, Config const& cfg) {
	if (!sim) return false;

	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();

	// Clamp selection to field bounds
	if (startY >= fieldH || startX >= fieldW) return false;
	if (startY + height > fieldH) height = fieldH - startY;
	if (startX + width > fieldW) width = fieldW - startX;
	if (height == 0 || width == 0) return false;

	ScratchPad buf = generate(height, width, cfg);

	// Nothing generated -> no undo entry, no notify, no queue traffic.
	if (!buf.dirty()) return false;

	// One command, not one per glyph. loadRectFromOrcaRequest already does
	// pushUndo() + notifyTick() internally, so we must NOT do either here.
	// Untouched cells are '.', so the full-rect paste also clears prior content
	// in the rect (cfg.clearFirst); overlay mode would need the existing field
	// contents and is not supported yet.
	Usz outH = 0, outW = 0;
	return sim->loadRectFromOrcaRequest(buf.toOrca(), startY, startX, outH, outW, /*replace_field=*/false);
}

Usz AhabRandomizer::generateSimplePattern(ScratchPad& buf, Usz y, Usz x, Usz maxY, Usz maxX) {
	Usz availW = maxX > x ? maxX - x : 0;
	Usz availH = maxY > y ? maxY - y : 0;
	
	if (availW < 4 || availH < 1) return 0;
	
	int patternType = rng() % 8;
	
	switch (patternType) {
		case 0: // Clock
			return placeClockPattern(buf, y, x, availW, randomSmallDigit(), randomBase36());
		case 1: // Delay
			return placeDelayPattern(buf, y, x, availW, randomSmallDigit(), randomSmallDigit());
		case 2: // Random
			return placeRandomPattern(buf, y, x, availW, '0', randomBase36());
		case 3: // Uclid
			return placeUclidPattern(buf, y, x, availW, randomSmallDigit(), '8');
		case 4: // Variable write
			return placeVarWrite(buf, y, x, availW, randomVarName(), randomBase36());
		case 5: // Add
			return placeAddPattern(buf, y, x, availW, randomBase36(), randomBase36());
		case 6: // If
			return placeIfPattern(buf, y, x, availW, randomVarName(), randomBase36());
		case 7: { // Track with short sequence
			std::string notes;
			for (int i = 0; i < 4; i++) notes += randomNote();
			return placeTrackPattern(buf, y, x, availW, randomBase36(), notes);
		}
	}
	return 0;
}

void AhabRandomizer::generateArrangement(ScratchPad& buf, Usz y, Usz x, Usz h, Usz w, float density) {
	// Generate a grid-based arrangement with multiple patterns
	// Each cell in the grid can potentially contain a pattern based on density
	
	Usz currentY = y;
	
	// Place master clock at top if we have room
	if (h >= 3 && w >= 8 && density > 0.2f) {
		generateMasterClock(buf, y, x, std::min(h, (Usz)3), std::min(w, (Usz)8));
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
							used = placeArpeggioVoice(buf, currentY, currentX, patternH, patternW, randomChannel(), notes);
							break;
						}
						case 1: // Drum
							used = generateDrumVoice(buf, currentY, currentX, patternH, patternW, rng() % 4);
							break;
						case 2: // Delay MIDI
							used = placeDelayMidiVoice(buf, currentY, currentX, patternH, patternW,
													   randomSmallDigit(), randomSmallDigit(),
													   randomChannel(), randomOctave(), randomNote());
							break;
						case 3: // Random melody
							used = placeRandomMelodyVoice(buf, currentY, currentX, patternH, patternW,
														  randomChannel(), '0', 'c');
							break;
						case 4: // Euclidean MIDI
							used = placeUclidMidiVoice(buf, currentY, currentX, patternH, patternW,
													   randomSmallDigit(), '8',
													   randomChannel(), randomOctave(), randomNote());
							break;
					}
					
					currentX += (used > 0 ? used + 1 : 8);
				} else if (patternH >= 2 && patternW >= 5) {
					// Place a simple pattern
					Usz used = generateSimplePattern(buf, currentY, currentX, currentY + patternH, currentX + patternW);
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

Usz AhabRandomizer::generateMasterClock(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW) {
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Place a clock that stores to variable 'g' (global clock)
	// .4Cg / .gV. (clock mod g, store to variable g)
	char rate = randomSmallDigit();
	char mod = '4' + (rng() % 5); // 4-8
	
	// Row 0: Clock
	buf.set(y, x, '.');
	buf.set(y, x+1, rate);
	buf.set(y, x+2, 'C');
	buf.set(y, x+3, mod);
	
	// Row 1: Store to variable 'g'
	buf.set(y+1, x, '.');
	buf.set(y+1, x+1, 'g');
	buf.set(y+1, x+2, 'V');
	
	return 5;
}

Usz AhabRandomizer::generateVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW, int voiceNum) {
	if (maxH < 3 || maxW < 8) return 0;
	
	// Choose voice type based on voice number for variety
	int voiceType = (voiceNum + rng()) % 5;
	
	switch (voiceType) {
		case 0: // Melodic voice using arpeggio pattern
		{
			std::string notes;
			int numNotes = 4 + (rng() % 5);
			for (int i = 0; i < numNotes; i++) notes += randomNote();
			return placeArpeggioVoice(buf, y, x, maxH, maxW, randomChannel(), notes);
		}
		
		case 1: // Drum voice with euclidean rhythm
			return generateDrumVoice(buf, y, x, maxH, maxW, voiceNum);
		
		case 2: // Delay-based rhythmic voice
			return placeDelayMidiVoice(buf, y, x, maxH, maxW, 
									   randomSmallDigit(), randomSmallDigit(),
									   randomChannel(), randomOctave(), randomNote());
		
		case 3: // Random melody
			return placeRandomMelodyVoice(buf, y, x, maxH, maxW, 
										  randomChannel(), '0', 'c');
		
		case 4: // Euclidean MIDI
			return placeUclidMidiVoice(buf, y, x, maxH, maxW,
									   randomSmallDigit(), '8',
									   randomChannel(), randomOctave(), randomNote());
	}
	
	return 0;
}

Usz AhabRandomizer::generateDrumVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW, int channel) {
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Euclidean pattern for drums
	char steps = '2' + (rng() % 6); // 2-7 steps
	
	// Row 0: Uclid - .3U8
	buf.set(y, x, '.');
	buf.set(y, x+1, steps);
	buf.set(y, x+2, 'U');
	buf.set(y, x+3, '8');
	
	// Row 1: MIDI - :0xCg1 (drum note on MIDI channel 9/10)
	buf.set(y+1, x+2, ':');
	buf.set(y+1, x+3, '9');  // MIDI channel 10 (drums)
	buf.set(y+1, x+4, '2');  // Octave 2
	buf.set(y+1, x+5, "CDEG"[channel % 4]); // Different drum hits
	buf.set(y+1, x+6, 'f');  // High velocity
	buf.set(y+1, x+7, '1');  // Short duration
	
	return 8;
}

Usz AhabRandomizer::generateMelodicVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW, int channel, char varName) {
	// Delegate to arpeggio voice
	std::string notes;
	int numNotes = 4 + (rng() % 5);
	for (int i = 0; i < numNotes; i++) notes += randomNote();
	return placeArpeggioVoice(buf, y, x, maxH, maxW, '0' + (channel % 10), notes);
}

// Pattern building blocks implementation

Usz AhabRandomizer::placeClockPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod) {
	
	if (maxW < 4) return 0;
	
	// .rCm format
	buf.set(y, x, '.');
	buf.set(y, x+1, rate);
	buf.set(y, x+2, 'C');
	buf.set(y, x+3, mod);
	
	return 4;
}

Usz AhabRandomizer::placeDelayPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod) {
	
	if (maxW < 4) return 0;
	
	// .rDm format
	buf.set(y, x, '.');
	buf.set(y, x+1, rate);
	buf.set(y, x+2, 'D');
	buf.set(y, x+3, mod);
	
	return 4;
}

Usz AhabRandomizer::placeTrackPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char key, const std::string& values) {
	
	Usz len = values.size();
	if (maxW < 4 + len) return 0;
	
	// .keyTlen_values format: ..kT + values
	buf.set(y, x, '.');
	buf.set(y, x+1, key);
	char lenChar = (len < 10) ? '0' + len : 'a' + (len - 10);
	buf.set(y, x+2, lenChar);
	buf.set(y, x+3, 'T');
	
	for (Usz i = 0; i < len; ++i) {
		buf.set(y, x+4+i, values[i]);
	}
	
	return 4 + len;
}

Usz AhabRandomizer::placeUclidPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char steps, char max) {
	
	if (maxW < 4) return 0;
	
	// .sUm format
	buf.set(y, x, '.');
	buf.set(y, x+1, steps);
	buf.set(y, x+2, 'U');
	buf.set(y, x+3, max);
	
	return 4;
}

Usz AhabRandomizer::placeMidiPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, 
									  char channel, char octave, char note, char vel, char len) {
	
	if (maxW < 6) return 0;
	
	// :cOnvl format (colon operator for MIDI)
	buf.set(y, x, ':');
	buf.set(y, x+1, channel);
	buf.set(y, x+2, octave);
	buf.set(y, x+3, note);
	buf.set(y, x+4, vel);
	buf.set(y, x+5, len);
	
	return 6;
}

Usz AhabRandomizer::placeVarWrite(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name, char value) {
	
	if (maxW < 4) return 0;
	
	// .nVv format
	buf.set(y, x, '.');
	buf.set(y, x+1, name);
	buf.set(y, x+2, 'V');
	buf.set(y, x+3, value);
	
	return 4;
}

Usz AhabRandomizer::placeVarRead(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name) {
	
	if (maxW < 3) return 0;
	
	// ..Vn format (read variable, output below)
	buf.set(y, x, '.');
	buf.set(y, x+1, '.');
	buf.set(y, x+2, 'V');
	buf.set(y, x+3, name);
	
	return 4;
}

Usz AhabRandomizer::placeKonkatPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, const std::string& vars) {
	
	Usz len = vars.size();
	if (maxW < 3 + len) return 0;
	
	// .lKvars format
	buf.set(y, x, '.');
	char lenChar = (len < 10) ? '0' + len : 'a' + (len - 10);
	buf.set(y, x+1, lenChar);
	buf.set(y, x+2, 'K');
	
	for (Usz i = 0; i < len; ++i) {
		buf.set(y, x+3+i, vars[i]);
	}
	
	return 3 + len;
}

Usz AhabRandomizer::placeRandomPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char min, char max) {
	
	if (maxW < 4) return 0;
	
	// .mRx format
	buf.set(y, x, '.');
	buf.set(y, x+1, min);
	buf.set(y, x+2, 'R');
	buf.set(y, x+3, max);
	
	return 4;
}

Usz AhabRandomizer::placeIncrementPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char step, char mod) {
	
	if (maxW < 4) return 0;
	
	// .sIm format
	buf.set(y, x, '.');
	buf.set(y, x+1, step);
	buf.set(y, x+2, 'I');
	buf.set(y, x+3, mod);
	
	return 4;
}

Usz AhabRandomizer::placeAddPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b) {
	
	if (maxW < 4) return 0;
	
	// .aAb format
	buf.set(y, x, '.');
	buf.set(y, x+1, a);
	buf.set(y, x+2, 'A');
	buf.set(y, x+3, b);
	
	return 4;
}

Usz AhabRandomizer::placeMultiplyPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b) {
	
	if (maxW < 4) return 0;
	
	// .aMb format
	buf.set(y, x, '.');
	buf.set(y, x+1, a);
	buf.set(y, x+2, 'M');
	buf.set(y, x+3, b);
	
	return 4;
}

Usz AhabRandomizer::placeIfPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b) {
	
	if (maxW < 4) return 0;
	
	// .aFb format
	buf.set(y, x, '.');
	buf.set(y, x+1, a);
	buf.set(y, x+2, 'F');
	buf.set(y, x+3, b);
	
	return 4;
}

Usz AhabRandomizer::placeHaltPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW) {
	
	if (maxW < 1) return 0;
	
	buf.set(y, x, 'H');
	
	return 1;
}

Usz AhabRandomizer::placeOffsetPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char offX, char offY) {
	
	if (maxW < 4) return 0;
	
	// .x.yO format (read from offset position)
	buf.set(y, x, '.');
	buf.set(y, x+1, offX);
	buf.set(y, x+2, offY);
	buf.set(y, x+3, 'O');
	
	return 4;
}

// Compound pattern implementations

Usz AhabRandomizer::placeClockTrackVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
										  char rate, const std::string& notes, char varName) {
	
	Usz noteLen = notes.size();
	if (maxH < 3 || maxW < 5 + noteLen) return 0;
	
	char mod = (noteLen < 10) ? '0' + noteLen : 'a' + (noteLen - 10);
	
	// Row 0: Clock - .rCm
	placeClockPattern(buf, y, x, maxW, rate, mod);
	
	// Row 1: Track - ..mT + notes (clock output feeds into track key position)
	buf.set(y+1, x, '.');
	buf.set(y+1, x+1, mod);
	buf.set(y+1, x+2, mod);
	buf.set(y+1, x+3, 'T');
	for (Usz i = 0; i < noteLen; ++i) {
		buf.set(y+1, x+4+i, notes[i]);
	}
	
	// Row 2: Store to variable
	placeVarWrite(buf, y+2, x, maxW, varName, '.'); // The track output will fill in the value
	
	return 4 + noteLen;
}

Usz AhabRandomizer::placeDelayMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
										 char rate, char mod, char channel, char octave, char note) {
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Row 0: Delay - .rDm
	placeDelayPattern(buf, y, x, maxW, rate, mod);
	
	// Row 1: MIDI - the delay outputs * which triggers the MIDI
	// Position MIDI so the * lands on the bang position
	buf.set(y+1, x+2, ':');
	buf.set(y+1, x+3, channel);
	buf.set(y+1, x+4, octave);
	buf.set(y+1, x+5, note);
	buf.set(y+1, x+6, 'f'); // velocity
	buf.set(y+1, x+7, '4'); // length
	
	return 8;
}

Usz AhabRandomizer::placeUclidMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
										 char steps, char max, char channel, char octave, char note) {
	
	if (maxH < 2 || maxW < 8) return 0;
	
	// Row 0: Uclid - .sUm
	placeUclidPattern(buf, y, x, maxW, steps, max);
	
	// Row 1: MIDI - the uclid outputs * which triggers the MIDI
	buf.set(y+1, x+2, ':');
	buf.set(y+1, x+3, channel);
	buf.set(y+1, x+4, octave);
	buf.set(y+1, x+5, note);
	buf.set(y+1, x+6, 'c'); // velocity
	buf.set(y+1, x+7, '2'); // length
	
	return 8;
}

Usz AhabRandomizer::placeArpeggioVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
									   char channel, const std::string& notes) {
	
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
	placeClockPattern(buf, y, x, maxW, rate, mod);
	
	// Row 1: Track with notes
	buf.set(y+1, x, '.');
	buf.set(y+1, x+1, '1');
	buf.set(y+1, x+2, mod);
	buf.set(y+1, x+3, 'T');
	for (Usz i = 0; i < noteLen && x+4+i < buf.width(); ++i) {
		buf.set(y+1, x+4+i, notes[i]);
	}
	
	// Row 2: Store to variable
	char varName = randomVarName();
	placeVarWrite(buf, y+2, x, maxW, varName, '.');
	
	// Row 3: Halt + Variable read + MIDI
	buf.set(y+3, x, 'H');
	buf.set(y+3, x+1, '.');
	buf.set(y+3, x+2, 'V');
	buf.set(y+3, x+3, varName);
	
	// Row 4: MIDI output (bang comes from delay or uclid above in a real patch)
	// Using * to show where the trigger should be
	buf.set(y+4, x, '*');
	buf.set(y+4, x+1, ':');
	buf.set(y+4, x+2, channel);
	buf.set(y+4, x+3, randomOctave());
	// Note comes from the variable read above
	
	return std::max((Usz)8, 4 + noteLen);
}

Usz AhabRandomizer::placeChordVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
									char channel, char octave, const std::vector<char>& notes) {
	
	if (maxH < 2 + notes.size() || maxW < 8) return 0;
	
	// Row 0: Delay for triggering
	placeDelayPattern(buf, y, x, maxW, randomSmallDigit(), '4');
	
	// Rows 1+: Multiple MIDI notes (chord)
	for (Usz i = 0; i < notes.size() && y+1+i < buf.height(); ++i) {
		buf.set(y+1+i, x+2, ':');
		buf.set(y+1+i, x+3, channel);
		buf.set(y+1+i, x+4, octave);
		buf.set(y+1+i, x+5, notes[i]);
		buf.set(y+1+i, x+6, 'c');
		buf.set(y+1+i, x+7, '8');
	}
	
	return 8;
}

Usz AhabRandomizer::placeRandomMelodyVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
										   char channel, char minNote, char maxNote) {
	
	if (maxH < 3 || maxW < 8) return 0;
	
	char varName = randomVarName();
	
	// Row 0: Random - .0Rz
	placeRandomPattern(buf, y, x, maxW, minNote, maxNote);
	
	// Row 1: Store to variable
	placeVarWrite(buf, y+1, x, maxW, varName, '.');
	
	// Row 2: Delay for rhythm
	placeDelayPattern(buf, y+2, x, maxW, randomSmallDigit(), '4');
	
	// Row 3: Read variable + MIDI
	if (maxH >= 4) {
		buf.set(y+3, x, 'H');
		buf.set(y+3, x+1, '.');
		buf.set(y+3, x+2, 'V');
		buf.set(y+3, x+3, varName);
		
		// Row 4: MIDI
		if (maxH >= 5) {
			buf.set(y+4, x+2, ':');
			buf.set(y+4, x+3, channel);
			buf.set(y+4, x+4, randomOctave());
			// Note placeholder - will be filled by variable
		}
	}
	
	return 8;
}

} // namespace Ahab
} // namespace StoermelderPackOne
