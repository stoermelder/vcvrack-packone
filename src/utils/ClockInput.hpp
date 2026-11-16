#pragma once
#include "../plugin.hpp"
#include "digital.hpp"

namespace StoermelderPackOne {

// The clock sources of a sequencer module, turned into ticks: external CLK edges through a
// divider or multiplier, and an internal BPM phase accumulator that runs while nothing is patched
// into CLK. DSP thread only; it touches nothing of the module. The module decides what a tick
// does, and calls processExternal() and processInternal() at the points of its own process() order.
struct ClockInput {
	// Restarts the internal clock's next tick a full period away. Call it on every run/stop change;
	// processExternal() does it itself on each external tick, so the two sources never double up.
	void resetPhase() {
		phase_ = 0.f;
	}

	float phase() const {
		return phase_;
	}

	void setPhase(float phase) {
		phase_ = phase;
	}

	// Forgets everything (phase, divider and multiplier state), as after a module reset.
	void reset() {
		phase_ = 0.f;
		divider_.reset();
		multiplier_.reset();
		ratioKey_ = -1;
	}

	// One sample of the external clock. The ratio is `multiply` / `divide`; at most one of them is
	// above 1 (both 1 passes every edge through). Returns how many ticks to apply, 0 while not
	// running (edges are not counted then).
	int processExternal(bool running, float voltage, int multiply, int divide) {
		if (!running) return 0;
		int ticks = 0;

		// Reconfigure the divider/multiplier only when the ratio changes.
		int key = divide > 1 ? -divide : multiply;
		if (key != ratioKey_) {
			ratioKey_ = key;
			if (divide > 1) divider_.setDivision((uint32_t) divide);
			else if (multiply > 1) multiplier_.reset();
		}

		if (trigger_.process(voltage)) {
			if (divide > 1) {
				if (divider_.process()) ticks++;
			}
			else if (multiply > 1) {
				multiplier_.tick();
				multiplier_.trigger((uint32_t) multiply);
			}
			else {
				ticks++;
			}
		}
		if (divide <= 1 && multiply > 1 && multiplier_.process()) ticks++;

		if (ticks > 0) resetPhase();
		return ticks;
	}

	// One sample of the internal clock at `bpm`, with `ticksPerQuarter` ticks per quarter note.
	// Returns how many ticks to apply. `active` is the caller's gate: running, and no cable in CLK.
	int processInternal(float sampleTime, bool active, float bpm, float ticksPerQuarter) {
		if (!active) return 0;
		phase_ += bpm / 60.f * ticksPerQuarter * sampleTime;
		if (phase_ < 1.f) return 0;
		phase_ -= 1.f;
		return 1;
	}

private:
	float phase_ = 0.f;
	dsp::SchmittTrigger trigger_;
	ClockDividerEx divider_;
	ClockMultiplier multiplier_;
	int ratioKey_ = -1;
};

} // namespace StoermelderPackOne
