#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {
namespace MidiCat {

/** MIDI output port which tracks the last value sent for every CC number and the
 *  last gate state for every note number, so that redundant messages are suppressed.
 *  Owned by the DSP thread.
 */
struct MidiCatOutput : midi::Output {
	int lastValues[128];
	bool lastGates[128];

	MidiCatOutput() {
		reset();
	}

	void reset() {
		for (int n = 0; n < 128; n++) {
			lastValues[n] = -1;
			lastGates[n] = false;
		}
	}

	void setValue(int value, int cc, bool force = false) {
		assert(cc >= 0 && cc < 128);
		if (value == lastValues[cc] && !force)
			return;
		lastValues[cc] = value;
		// CC
		midi::Message m;
		m.setStatus(0xb);
		m.setNote(cc);
		m.setValue(value);
		sendMessage(m);
	}

	void setGate(int vel, int note, bool noteOffVelocityZero, bool force = false) {
		assert(note >= 0 && note < 128);
		if (vel > 0) {
			// Note on
			if (!lastGates[note] || force) {
				midi::Message m;
				m.setStatus(0x9);
				m.setNote(note);
				m.setValue(vel);
				sendMessage(m);
			}
		}
		else if (vel == 0) {
			// Note off
			if (lastGates[note] || force) {
				midi::Message m;
				m.setStatus(noteOffVelocityZero ? 0x9 : 0x8);
				m.setNote(note);
				m.setValue(0);
				sendMessage(m);
			}
		}
		lastGates[note] = vel > 0;
	}
}; // struct MidiCatOutput

} // namespace MidiCat
} // namespace StoermelderPackOne
