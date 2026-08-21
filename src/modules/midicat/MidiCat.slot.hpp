#pragma once
#include "MidiCat.hpp"
#include "MidiCat.input.hpp"
#include "MidiCat.modes.hpp"
#include "MidiCat.param.hpp"
#include <algorithm>
#include <string>

namespace StoermelderPackOne {
namespace MidiCat {

/** Note number as scientific pitch notation, e.g. 60 -> "C4".
 *  `padded` left-pads the natural notes to two characters (" C" instead of "C") so that
 *  columns of note names line up in the fixed-width mapping list.
 */
inline std::string noteName(int note, bool padded = false) {
	static const char* noteNames[] = {
		"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};
	static const char* noteNamesPadded[] = {
		" C", "C#", " D", "D#", " E", " F", "F#", " G", "G#", " A", "A#", " B"
	};
	int oct = note / 12 - 1;
	int semi = note % 12;
	return rack::string::f("%s%d", (padded ? noteNamesPadded : noteNames)[semi], oct);
}

/** Everything that describes one of the MAX_CHANNELS mapping slots, apart from the
 *  ParamHandle: that stays a module-level array because the shared MapModuleChoice /
 *  MapModuleDisplay widget templates (used by CVMap and others) address it as
 *  `module->paramHandles[id]`.
 *
 *  Touched from both the DSP thread (the value pipeline) and the UI thread (learning,
 *  the context menus and the MEM-expander) -- that sharing is pre-existing.
 */
struct MappingSlot {
	/** [Stored to Json] The mapped CC number and its input mode */
	CcSource cc;
	/** [Stored to Json] The mapped note number and its input mode */
	NoteSource note;
	/** [Stored to Json] Bitfield, see MIDIOPTION_*_BIT */
	int midiOptions = 0;

	/** [Stored to Json] Scaling, slew, curve, clock-quantization and LED binding */
	MidiCatParam param;

	/** [Stored to Json] Custom label shown in the mapping list */
	std::string label;

	/** Input tracking: last raw value, toggle-cycle position, detached flag */
	InputTracker tracker;
	/** Last value sent as MIDI feedback */
	int lastValueOut = -1;
	/** Last value seen while in MIDIMODE_LOCATE */
	int lastValueIndicate = -1;

	/** True if this slot listens to any MIDI source.
	 *  Not const because the adapters' getters are not const-qualified. */
	bool isBound() {
		return cc.getCc() >= 0 || note.getNote() >= 0;
	}

	/** Set the slot's CC number, keeping the value limits in step: dropping out of 14-bit
	 *  mode (because the new CC has no low-order partner) narrows the range back to 7 bits.
	 *  The limits live on `param` and the mode lives on `cc`, so only the slot -- which
	 *  owns both -- can keep the two consistent. */
	void setCc(int ccNumber) {
		if (cc.setCc(ccNumber)) {
			applyCcLimits();
		}
	}

	/** Set 14-bit mode and the matching value limits together. */
	void setCc14bit(bool value) {
		cc.set14bit(value);
		applyCcLimits();
	}

	/** Widen or narrow the parameter's input range to match the current CC resolution. */
	void applyCcLimits() {
		if (cc.get14bit()) {
			param.setLimits(0, 128 * 128 - 1, -1);
		}
		else {
			param.setLimits(0, 127, -1);
		}
	}

	void setNote(int noteNumber) {
		note.setNote(noteNumber);
	}

	/** Bind the slot to a CC number only, dropping any note binding. A slot listens to
	 *  one kind of MIDI source at a time, so learning a CC clears the note and vice
	 *  versa -- see bindNote(). */
	void bindCc(int ccNumber) {
		setCc(ccNumber);
		note.setNote(-1);
	}

	/** Bind the slot to a note number only, dropping any CC binding. */
	void bindNote(int noteNumber) {
		setCc(-1);
		note.setNote(noteNumber);
	}

	/** Restore both bindings at once, as stored in a preset or the MEM-expander. Unlike
	 *  bindCc()/bindNote() this does not treat them as mutually exclusive: it replays
	 *  exactly what was saved. */
	void setBinding(int ccNumber, int noteNumber) {
		setCc(ccNumber);
		note.setNote(noteNumber);
	}

	/** True if the slot holds anything at all -- a MIDI binding, a mapped parameter, or
	 *  both. The ParamHandle is passed in rather than held: it lives in a module-level
	 *  array because the shared MapModuleChoice widget template addresses it there.
	 *  An "in use" slot is one that must be kept when trimming the mapping list. */
	bool isUsed(const ParamHandle& handle) {
		return isBound() || handle.moduleId >= 0;
	}

	/** True if the slot is only half-configured: it has a MIDI binding without a mapped
	 *  parameter, or a mapped parameter without a MIDI binding. Learning walks the slots
	 *  looking for the next one of these to complete. */
	bool isIncomplete(const ParamHandle& handle) {
		return !isBound() || handle.moduleId < 0;
	}

	/** True if the slot is entirely empty -- no MIDI binding and no mapped parameter. */
	bool isEmpty(const ParamHandle& handle) {
		return !isBound() && handle.moduleId < 0;
	}

	/** The MIDI binding as a suffix for the ParamHandle tooltip, e.g. " cc07 note C4".
	 *  Empty when the slot is unbound. */
	std::string bindingText() {
		std::string text;
		if (cc.getCc() >= 0) {
			text += string::f(" cc%02d", cc.getCc());
		}
		if (note.getNote() >= 0) {
			text += string::f(" note %s", noteName(note.getNote()).c_str());
		}
		return text;
	}

	/** The binding as a menu entry, e.g. "MIDI CC 07" / "MIDI note C4". Prefers the
	 *  custom label when one is set. Empty when the slot is unbound and unlabelled. */
	std::string menuLabel() {
		if (label != "") return label;
		if (cc.getCc() >= 0) return string::f("MIDI CC %02d", cc.getCc());
		if (note.getNote() >= 0) return string::f("MIDI note %s", noteName(note.getNote()).c_str());
		return "";
	}

	/** Reset the slot to its default state: default input modes, no label, no options,
	 *  no tracking state. Does not touch the MIDI binding (cc/note numbers) -- callers
	 *  that want those cleared too go through clearMidi(). */
	void reset() {
		cc.ccMode = CCMODE::DIRECT;
		note.noteMode = NOTEMODE::MOMENTARY;
		label = "";
		midiOptions = 0;
		param.reset();
		resetValues();
	}

	/** Clear the MIDI binding and all per-slot settings, but not the parameter mapping. */
	void clearMidi() {
		cc.reset();
		note.reset();
		midiOptions = 0;
		param.reset();
	}

	/** Reset the runtime tracking state, leaving the binding and settings alone. */
	void resetValues() {
		tracker.reset();
		lastValueOut = -1;
	}

	/** The result of resolving one MIDI input event against an input mode: the target
	 *  value for the parameter, or -1 for "no change", plus a fractional offset used by
	 *  the FINE-expander's relative adjustment. */
	struct ModeResult {
		int value = -1;
		float fine = 0.f;
	};

	/** Run the toggle cycle and pick the value for whichever step just fired. */
	ModeResult processToggle(int inputValue, const ToggleValue& value) {
		ModeResult r;
		r.value = value.step(tracker.toggle.advance(inputValue));
		tracker.detached = true;
		return r;
	}

	/** Step to the next/previous snapped parameter value. `shortLong` selects the
	 *  short-press/long-press variant, which steps backwards on a long press. */
	ModeResult processSnapped(int inputValue, bool shortLong, uint64_t pressDuration,
	                           uint64_t longPressDuration) {
		ModeResult r;
		if (!shortLong) {
			r.value = inputValue > 0 ? param.getNextSnappedValue() : param.getValue();
		}
		else {
			if (inputValue == 0) {
				r.value = pressDuration < longPressDuration
					? param.getNextSnappedValue()
					: param.getPrevSnappedValue();
			}
			else {
				r.value = param.getValue();
			}
		}
		tracker.detach();
		return r;
	}

	/** Resolve a new CC value against this slot's CC input mode. */
	ModeResult processCcMode(CCMODE mode, bool fineMode, uint64_t longPressDuration) {
		ModeResult r;
		int in = cc.getValue();
		switch (mode) {
			case CCMODE::DIRECT:
				if (tracker.lastValue != in) {
					tracker.lastValue = in;
					tracker.detached = false;
					r.value = in;
				}
				break;
			case CCMODE::PICKUP1:
			case CCMODE::PICKUP2:
				if (tracker.lastValue != in) {
					if (!fineMode) {
						// PICKUP1 only requires the parameter to be near the last value;
						// PICKUP2 additionally requires the incoming value to be near it.
						bool near = mode == CCMODE::PICKUP1
							? param.isNear(tracker.lastValue)
							: param.isNear(tracker.lastValue, in);
						if (near) {
							param.resetFilter();
							r.value = in;
						}
					}
					else {
						r.value = 0;
						r.fine = param.precProcessor.process(in);
					}
					tracker.lastValue = in;
					tracker.detached = false;
				}
				break;
			case CCMODE::TOGGLE:
				return processToggle(in, { param.getLimitMax(), param.getLimitMax(),
				                            param.getLimitMin(), param.getLimitMin() });
			case CCMODE::TOGGLE_VALUE:
				return processToggle(in, { in, param.getValue(),
				                            param.getLimitMin(), param.getLimitMin() });
			case CCMODE::SNAPPED:
				return processSnapped(in, false, 0, longPressDuration);
			case CCMODE::SNAPPED_SL:
				return processSnapped(in, true, cc.diffTs, longPressDuration);
		}
		return r;
	}

	/** Resolve a new note value (velocity) against this slot's note input mode. */
	ModeResult processNoteMode(uint64_t longPressDuration) {
		ModeResult r;
		int in = note.getValue();
		switch (note.noteMode) {
			case NOTEMODE::MOMENTARY:
				if (tracker.lastValue != in) {
					r.value = in > 0 ? 127 : in;
					tracker.lastValue = in;
					tracker.detached = false;
				}
				break;
			case NOTEMODE::MOMENTARY_VEL:
				if (tracker.lastValue != in) {
					r.value = in;
					tracker.lastValue = in;
					tracker.detached = false;
				}
				break;
			case NOTEMODE::TOGGLE:
				return processToggle(in, { 127, 127, 0, 0 });
			case NOTEMODE::TOGGLE_VEL:
				return processToggle(in, { in, param.getValue(), 0, 0 });
			case NOTEMODE::SNAPPED:
				return processSnapped(in, false, 0, longPressDuration);
			case NOTEMODE::SNAPPED_SL:
				return processSnapped(in, true, note.diffTs, longPressDuration);
		}
		return r;
	}

	/** Resolve the slot's mapped parameter, or null if it is not currently mappable --
	 *  unmapped, pointing at a module that has gone away, out of range, or unbounded
	 *  (an unbounded parameter has no range to scale a MIDI value into). */
	static ParamQuantity* resolveTarget(const ParamHandle& handle) {
		Module* module = handle.module;
		if (!module) return NULL;
		if (handle.paramId >= (int)module->paramQuantities.size()) return NULL;
		ParamQuantity* pq = module->paramQuantities[handle.paramId];
		if (!pq) return NULL;
		if (!pq->isBounded()) return NULL;
		return pq;
	}

	/** Resolve any new MIDI input against the slot's input modes.
	 *  Returns the target value for the parameter, or -1 if nothing changed. */
	ModeResult processInput(const MidiInputState& in, bool ccModeOverride, bool fineMode, uint64_t longPressDuration) {
		ModeResult r;
		if (cc.getCc() >= 0 && cc.process(in)) {
			CCMODE mode = ccModeOverride ? CCMODE::DIRECT : cc.ccMode;
			r = processCcMode(mode, fineMode, longPressDuration);
		}
		if (note.getNote() >= 0 && note.process(in)) {
			r = processNoteMode(longPressDuration);
		}
		return r;
	}

	/** Write the resolved value to the parameter and send the matching MIDI feedback.
	 *  `sampleTime` is the time since the last call, for slew-limiting. */
	void processOutput(MidiCatOutput& out, ParamQuantity* pq, float sampleTime) {
		// Apply value on the mapped parameter (respecting slew and scale)
		param.process(sampleTime);

		// Retrieve the current value of the parameter (ignoring slew and scale)
		int v = param.getValue();

		// MIDI feedback is detached from the actual parameter value when the tracked
		// value is not the parameter's: the toggle and snapped modes drive the parameter
		// without tracking it, feedback bound to a light reads the light instead, and a
		// snapped parameter's actual value may differ from the tracked one due to
		// rounding. In those cases we send feedback but must not write the value back.
		bool sendOnlyFeedback = tracker.detached || param.hasLight() || pq->snapEnabled;

		if (lastValueOut == v) return;

		// A slot driven directly by a CC re-attaches to the parameter's value, so that
		// manual parameter changes are picked up as the new tracking baseline.
		if (cc.getCc() >= 0 && cc.ccMode == CCMODE::DIRECT) {
			tracker.lastValue = v;
			tracker.detached = false;
		}
		if (!sendOnlyFeedback) param.setValue(v);
		cc.setValue(out, v, sendOnlyFeedback);
		note.setValue(out, v, (midiOptions >> MIDIOPTION_VELZERO_BIT) & 1U, sendOnlyFeedback);
		lastValueOut = v;
	}

	/** Seed the locate-mode baseline from the last value actually received, so that
	 *  entering MIDIMODE_LOCATE does not immediately report a spurious change.
	 *  Clamped at 0 because the tracker reports -1 when nothing has been received yet. */
	void primeIndicate() {
		lastValueIndicate = std::max(0, tracker.lastValue);
	}

	/** Check whether new MIDI arrived for this slot while in MIDIMODE_LOCATE, updating
	 *  the baseline. Returns true if the mapped parameter should be indicated. */
	bool pollIndicate(const MidiInputState& in) {
		bool indicate = false;
		if (cc.getCc() >= 0 && in.getCc(cc.getCc()) >= 0 && lastValueIndicate != in.getCc(cc.getCc())) {
			lastValueIndicate = in.getCc(cc.getCc());
			indicate = true;
		}
		if (note.getNote() >= 0 && in.getNote(note.getNote()) >= 0 && lastValueIndicate != in.getNote(note.getNote())) {
			lastValueIndicate = in.getNote(note.getNote());
			indicate = true;
		}
		return indicate;
	}

	MidiCatParam::CLOCKMODE getClockMode() {
		return param.clockMode;
	}

	/** Enabling clock-quantization is incompatible with the input modes that track a
	 *  continuous value, and with slew: a quantized slot has to hold a discrete value
	 *  until the clock releases it. Switching it on therefore migrates those modes to
	 *  their nearest discrete equivalent and clears the slew. */
	void setClockMode(MidiCatParam::CLOCKMODE mode) {
		if (mode != MidiCatParam::CLOCKMODE::OFF) {
			if (cc.getCc() >= 0 && (cc.ccMode == CCMODE::PICKUP1 || cc.ccMode == CCMODE::PICKUP2)) {
				cc.ccMode = CCMODE::DIRECT;
			}
			if (note.getNote() >= 0 && (note.noteMode == NOTEMODE::MOMENTARY || note.noteMode == NOTEMODE::MOMENTARY_VEL)) {
				note.noteMode = NOTEMODE::TOGGLE;
			}
			param.setSlew(0.f);
		}
		param.clockMode = mode;
	}

	/** Drop clock-quantization entirely, including the chosen clock source. Used when the
	 *  CLK-expander goes away: the slot would otherwise hold a pending value forever with
	 *  no clock left to release it. Unlike setClockMode(OFF) this also resets the source,
	 *  so a later re-attach starts from the default rather than a stale selection. */
	void resetClockMode() {
		param.clockMode = MidiCatParam::CLOCKMODE::OFF;
		param.clockSource = 0;
	}

	/** Prepare the slot's fine-adjustment reference point. Returns false if the slot has
	 *  no mapped parameter to adjust. */
	bool initFineMode() {
		if (!param.paramQuantity) return false;
		param.precProcessor.init(param.getLimitMin(), param.getLimitMax());
		return true;
	}

	/** Set the fine-adjustment precision, optionally re-anchoring the reference point to
	 *  the slot's current CC value. Returns false if the slot has no mapped parameter. */
	bool setFinePrecision(float precision, bool updateRefPoint) {
		if (!param.paramQuantity) return false;
		param.precProcessor.setPrecision(precision, param.getRawValue(), updateRefPoint ? cc.getValue() : -1);
		return true;
	}

	/** Capture this slot's binding and settings for storage in the MEM-expander.
	 *  `paramId` comes from the ParamHandle, which the slot does not own. */
	MemParam* toMemParam(int paramId) {
		MemParam* p = new MemParam;
		p->paramId = paramId;
		p->cc = cc.getCc();
		p->ccMode = cc.ccMode;
		p->cc14bit = cc.get14bit();
		p->note = note.getNote();
		p->noteMode = note.noteMode;
		p->label = label;
		p->midiOptions = midiOptions;
		p->slew = param.getSlew();
		p->min = param.getMin();
		p->max = param.getMax();
		p->curve = param.getCurve();
		p->lightFirstId = param.lightFirstId;
		p->lightNumColors = param.lightNumColors;
		return p;
	}

	/** Restore a binding and its settings from the MEM-expander. The parameter mapping
	 *  itself is restored separately, via the ParamHandle. */
	void fromMemParam(const MemParam& p) {
		setBinding(p.cc, p.note);
		cc.ccMode = p.ccMode;
		setCc14bit(p.cc14bit);
		note.noteMode = p.noteMode;
		label = p.label;
		midiOptions = p.midiOptions;
		param.setSlew(p.slew);
		param.setMin(p.min);
		param.setMax(p.max);
		param.setCurve(p.curve);
		param.lightFirstId = p.lightFirstId;
		param.lightNumColors = p.lightNumColors;
	}

	/** Copy the MIDI-behaviour settings -- not the binding, not the label -- from another
	 *  slot. Used when a newly learned slot inherits the previous slot's settings.
	 *  `src` is non-const because ScaledMapParam's getters are not const-qualified. */
	void copySettingsFrom(MappingSlot& src, bool copy14bit) {
		cc.ccMode = src.cc.ccMode;
		setCc14bit(copy14bit);
		note.noteMode = src.note.noteMode;
		midiOptions = src.midiOptions;
		param.setSlew(src.param.getSlew());
		param.setMin(src.param.getMin());
		param.setMax(src.param.getMax());
		param.setCurve(src.param.getCurve());
		param.clockMode = src.param.clockMode;
		param.clockSource = src.param.clockSource;
	}
}; // struct MappingSlotBase

} // namespace MidiCat
} // namespace StoermelderPackOne
