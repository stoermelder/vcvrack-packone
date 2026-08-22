#pragma once
#include "MidiCat.hpp"
#include "MidiCat.output.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

/** The raw last-seen MIDI state, shared by all mapping slots: the current value of every
 *  CC number and every note number, each stamped with the tick it last changed on. Slots
 *  compare their own `lastTs` against these stamps to notice new input.
 *
 *  The timestamp is a plain counter incremented once per process() call, not a wall clock;
 *  `tick()` advances it.
 *  Owned by the DSP thread.
 */
struct MidiInputState {
	/** The value of each CC number */
	int valuesCc[128];
	uint64_t valuesCcTs[128];
	/** The value of each note number */
	int valuesNote[128];
	uint64_t valuesNoteTs[128];

	uint64_t ts = 0;

	MidiInputState() {
		reset();
	}

	void reset() {
		for (int i = 0; i < 128; i++) {
			valuesCc[i] = -1;
			valuesCcTs[i] = 0;
			valuesNote[i] = -1;
			valuesNoteTs[i] = 0;
		}
	}

	void tick() {
		ts++;
	}

	/** Record a CC value. Returns true if it actually changed. */
	bool setCc(int cc, int value) {
		bool changed = valuesCc[cc] != value;
		valuesCc[cc] = value;
		valuesCcTs[cc] = ts;
		return changed;
	}

	/** Record a note value (velocity, or 0 for note-off). Returns true if it changed. */
	bool setNote(int note, int value) {
		bool changed = valuesNote[note] != value;
		valuesNote[note] = value;
		valuesNoteTs[note] = ts;
		return changed;
	}

	int getCc(int cc) const {
		return valuesCc[cc];
	}
	int getNote(int note) const {
		return valuesNote[note];
	}
}; // struct MidiInputState


/** The CC binding of one mapping slot: which CC number it listens to, how incoming values
 *  are interpreted, and the last value seen.
 *
 *  In 14-bit mode the slot listens to a pair of CCs -- `cc` carries the high 7 bits and
 *  `cc + 32` the low 7 -- and only reports a change once both halves have arrived.
 *  Owned by the DSP thread.
 */
struct CcSource {
	int current = -1;
	uint64_t lastTs = 0;
	uint64_t diffTs = 0;

	/** [Stored to Json] */
	int cc;
	/** [Stored to Json] */
	CCMODE ccMode;
	/** [Stored to Json] */
	bool cc14bit = false;

	/** Pick up any new value from the shared input state.
	 *  Returns true if a new, different value arrived. */
	bool process(const MidiInputState& in) {
		int previous = current;
		if (cc14bit) {
			if (in.valuesCcTs[cc] > lastTs && in.valuesCcTs[cc + 32] > lastTs) {
				current = in.valuesCc[cc] * 128 + in.valuesCc[cc + 32];
				diffTs = in.ts - lastTs;
				lastTs = in.ts;
			}
		}
		else {
			if (in.valuesCcTs[cc] > lastTs) {
				current = in.valuesCc[cc];
				diffTs = in.ts - lastTs;
				lastTs = in.ts;
			}
		}
		return current >= 0 && current != previous;
	}

	int getValue() {
		return current;
	}

	/** Send `value` as MIDI feedback. `sendOnly` suppresses updating the tracked value,
	 *  used when the feedback is deliberately detached from the parameter. */
	void setValue(MidiCatOutput& out, int value, bool sendOnly) {
		if (cc == -1) return;
		if (cc14bit) {
			out.setValue(value / 128, cc, true);
			out.setValue(value % 128, cc + 32, true);
		}
		else {
			out.setValue(value, cc, current == -1 || sendOnly);
		}
		if (!sendOnly) current = value;
	}

	void reset() {
		cc = -1;
		current = -1;
	}

	void resetValue() {
		current = -1;
	}

	int getCc() {
		return cc;
	}

	bool get14bit() {
		return cc14bit;
	}

	/** Set the CC number. Returns true if 14-bit mode had to be switched off as a result
	 *  (only CCs 0-31 have a matching low-order partner), so the caller can update the
	 *  value limits that go with it. */
	bool setCc(int cc) {
		this->cc = cc;
		bool cleared14bit = false;
		if (cc == -1 || cc > 32) {
			cleared14bit = cc14bit;
			cc14bit = false;
		}
		current = -1;
		return cleared14bit;
	}

	/** Set 14-bit mode. The caller is responsible for applying the matching value limits
	 *  -- see MappingSlotBase::setCc14bit(), which keeps the two in step. */
	void set14bit(bool value) {
		cc14bit = value;
		current = -1;
	}
}; // struct CcSource


/** The note binding of one mapping slot: which note number it listens to, how incoming
 *  note on/off is interpreted, and the last velocity seen.
 *  Owned by the DSP thread.
 */
struct NoteSource {
	int current = -1;
	uint64_t lastTs = 0;
	uint64_t diffTs = 0;

	/** [Stored to Json] */
	int note;
	/** [Stored to Json] Use the velocity value of each channel when notes are used */
	NOTEMODE noteMode;

	/** Pick up any new value from the shared input state.
	 *  Returns true if a new, different value arrived. */
	bool process(const MidiInputState& in) {
		int previous = current;
		if (in.valuesNoteTs[note] > lastTs) {
			current = in.valuesNote[note];
			diffTs = in.ts - lastTs;
			lastTs = in.ts;
		}
		return current >= 0 && current != previous;
	}

	int getValue() {
		return current;
	}

	/** Send `value` as MIDI feedback. `noteOffVelocityZero` selects whether note-off is
	 *  sent as an actual note-off or as note-on with velocity 0. */
	void setValue(MidiCatOutput& out, int value, bool noteOffVelocityZero, bool sendOnly) {
		if (note == -1) return;
		out.setGate(value, note, noteOffVelocityZero, current == -1 || sendOnly);
		if (!sendOnly) current = value;
	}

	void reset() {
		note = -1;
		current = -1;
	}

	void resetValue() {
		current = -1;
	}

	int getNote() {
		return note;
	}

	void setNote(int note) {
		this->note = note;
		current = -1;
	}
}; // struct NoteSource

} // namespace MidiCat
} // namespace StoermelderPackOne
