#pragma once
#include "MidiCat.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

/** The four-step press/release cycle behind the TOGGLE input modes.
 *
 *  A toggle needs two full press-release cycles to return to where it started: the first
 *  press switches the parameter on and the first release keeps it there, the second press
 *  switches it off and the second release keeps it off. Tracking that needs four states,
 *  because "value is zero" alone cannot distinguish "released after switching on" from
 *  "released after switching off".
 *
 *  Which value each transition emits is up to the caller -- the four input modes that use
 *  this ladder emit different ones (see ToggleValue).
 */
struct ToggleValueLadder {
	enum class STATE {
		IDLE,       // nothing pressed yet, or a full cycle just completed
		PRESSED,    // first press seen: parameter switched on
		RELEASED,   // first release seen: parameter holds
		REPRESSED   // second press seen: parameter switched off
	};

	/** Which step of the cycle a value just completed, or NONE if the input did not
	 *  advance the ladder (e.g. a repeated press while already pressed). */
	enum class STEP {
		NONE,
		PRESS,
		RELEASE,
		REPRESS,
		RERELEASE
	};

	STATE state = STATE::IDLE;

	void reset() {
		state = STATE::IDLE;
	}

	/** Advance the ladder with a new input value (0 = released, >0 = pressed). */
	STEP advance(int value) {
		if (value > 0 && state == STATE::IDLE) {
			state = STATE::PRESSED;
			return STEP::PRESS;
		}
		if (value == 0 && state == STATE::PRESSED) {
			state = STATE::RELEASED;
			return STEP::RELEASE;
		}
		if (value > 0 && state == STATE::RELEASED) {
			state = STATE::REPRESSED;
			return STEP::REPRESS;
		}
		if (value == 0 && state == STATE::REPRESSED) {
			state = STATE::IDLE;
			return STEP::RERELEASE;
		}
		return STEP::NONE;
	}
}; // struct ToggleValueLadder


/** Per-slot input tracking.
 *
 *  Replaces the single `lastValueIn` int, which used to carry three unrelated meanings at
 *  once: the last raw MIDI value (when >= 0), the toggle ladder's position (encoded as the
 *  sentinels -1/-2/-3/-4), and -- by being negative at all -- a flag telling the feedback
 *  path not to write the tracked value back to the parameter.
 */
struct InputTracker {
	/** Last raw MIDI value seen, or -1 if none. Only meaningful for the modes that track
	 *  a continuous value (DIRECT, PICKUP*, MOMENTARY*). */
	int lastValue = -1;
	/** Position in the press/release cycle, for the TOGGLE modes. */
	ToggleValueLadder toggle;
	/** True when the value being tracked is not the parameter's value -- the toggle modes
	 *  and the snapped modes both drive the parameter without tracking it. The feedback
	 *  path uses this to decide whether it may write the tracked value back. */
	bool detached = false;

	void reset() {
		lastValue = -1;
		toggle.reset();
		detached = false;
	}

	/** Enter a mode that drives the parameter without tracking its value. */
	void detach() {
		lastValue = -1;
		detached = true;
	}
}; // struct InputTracker


/** The four values a toggle emits, one per step of the cycle. The input modes differ only
 *  in these; the cycle itself is identical. Any of them may be -1 for "emit nothing". */
struct ToggleValue {
	int onPress;
	int onRelease;
	int onRepress;
	int onRerelease;

	int step(ToggleValueLadder::STEP step) const {
		switch (step) {
			case ToggleValueLadder::STEP::PRESS: return onPress;
			case ToggleValueLadder::STEP::RELEASE: return onRelease;
			case ToggleValueLadder::STEP::REPRESS: return onRepress;
			case ToggleValueLadder::STEP::RERELEASE: return onRerelease;
			case ToggleValueLadder::STEP::NONE: return -1;
		}
		return -1;
	}
}; // struct ToggleValue

} // namespace MidiCat
} // namespace StoermelderPackOne
