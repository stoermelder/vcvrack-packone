#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include "../plugin.hpp"

namespace StoermelderPackOne {

// What the slot / bank CV input of a preset module does. The values are what the modules save in
// their patches (Transit, EightFace, EightFaceMk2, Signls), so they must not change. A module uses
// the subset it supports; ARM and PHASE are handled by the module itself.
enum class SlotCvMode : int {
	OFF = -1,
	VOLT = 0,
	C4 = 1,
	TRIG_FWD = 2,
	ARM = 3,
	TRIG_REV = 4,
	TRIG_PINGPONG = 5,
	TRIG_RANDOM = 6,
	TRIG_RANDOM_WO_REPEAT = 7,
	TRIG_RANDOM_WALK = 8,
	TRIG_ALT = 9,
	TRIG_SHUFFLE = 10,
	PHASE = 11,
};

// Which slot the slot CV input selects. Pure policy: it knows the mode, the current slot and the
// range [first, last) of selectable slots, and keeps the little state the pingpong, alternating and
// shuffle modes need. It loads nothing; the module loads the index it returns.
struct SlotSelector {
	// Where randomness comes from; tests replace it with a scripted source.
	uint32_t (*random)() = &rack::random::u32;

	// Trigger modes advance on a rising edge; the level modes follow the input's voltage.
	static bool isTriggerMode(SlotCvMode mode) {
		switch (mode) {
			case SlotCvMode::TRIG_FWD: case SlotCvMode::TRIG_REV: case SlotCvMode::TRIG_PINGPONG:
			case SlotCvMode::TRIG_RANDOM: case SlotCvMode::TRIG_RANDOM_WO_REPEAT: case SlotCvMode::TRIG_RANDOM_WALK:
			case SlotCvMode::TRIG_ALT: case SlotCvMode::TRIG_SHUFFLE:
				return true;
			default:
				return false;
		}
	}

	static bool isLevelMode(SlotCvMode mode) {
		return mode == SlotCvMode::VOLT || mode == SlotCvMode::C4;
	}

	// The slot in [first, last) for the input's level (0..10 V spread over the range, or 1 V per
	// semitone from C4), or -1 if `mode` is not a level mode.
	static int forLevel(SlotCvMode mode, float volts, int first, int last) {
		switch (mode) {
			case SlotCvMode::VOLT: {
				float clamped = rack::math::clamp(volts, 0.f, 10.f - 1e-6f);
				int s = first + (int) std::floor(clamped / 10.f * (float) (last - first));
				return clampIndex(s, first, last);
			}
			case SlotCvMode::C4:
				return (int) std::round(rack::math::clamp(volts * 12.f, (float) first, (float) (last - 1)));
			default:
				return -1;
		}
	}

	// The slot one rising edge selects, relative to `current`. `current` itself if `mode` is not a
	// trigger mode.
	int next(SlotCvMode mode, int current, int first, int last) {
		switch (mode) {
			case SlotCvMode::TRIG_FWD:
				return current + 1 >= last ? first : current + 1;
			case SlotCvMode::TRIG_REV:
				return current - 1 < first ? last - 1 : current - 1;
			case SlotCvMode::TRIG_PINGPONG: {
				int n = current + dir_;
				if (n >= last - 1) dir_ = -1;
				if (n <= first) dir_ = 1;
				return clampIndex(n, first, last);
			}
			case SlotCvMode::TRIG_ALT: {
				// first, first+1, first, first+2, ... up to the last slot, then back down.
				if (current != first) return first;
				int n = std::max(alt_, first) + dir_;
				if (n >= last - 1) dir_ = -1;
				if (n <= first + 1) dir_ = 1;
				n = clampIndex(n, first, last);
				alt_ = n;
				return n;
			}
			case SlotCvMode::TRIG_RANDOM:
				return first + (int) (random() % (uint32_t) (last - first));
			case SlotCvMode::TRIG_RANDOM_WO_REPEAT: {
				int s = first + (int) (random() % (uint32_t) (last - first - 1));
				if (s >= current) s++;
				return s;
			}
			case SlotCvMode::TRIG_RANDOM_WALK:
				return clampIndex(current + (random() % 2 == 0 ? -1 : 1), first, last);
			case SlotCvMode::TRIG_SHUFFLE: {
				if (deck_.empty()) {
					for (int i = first; i < last; i++) deck_.push_back(i);
					// Separate rng so slot selection doesn't consume the module's own rng seed.
					std::mt19937 shuffleRng(random());
					std::shuffle(deck_.begin(), deck_.end(), shuffleRng);
				}
				int s = clampIndex(deck_.back(), first, last);
				deck_.pop_back();
				return s;
			}
			default:
				return current;
		}
	}

	// Back to the state of a fresh module (module reset, or the RESET input).
	void reset() {
		dir_ = 1;
		alt_ = 0;
		deck_.clear();
	}

	int direction() const { return dir_; }           // pingpong / alternating: +1 up, -1 down
	void setDirection(int dir) { dir_ = dir; }
	int alt() const { return alt_; }                 // alternating: the advancing slot
	void setAlt(int alt) { alt_ = alt; }
	bool deckEmpty() const { return deck_.empty(); } // shuffle: the next trigger reshuffles
	void setDeck(const std::vector<int>& deck) { deck_ = deck; }

private:
	static int clampIndex(int n, int first, int last) {
		return std::min(std::max(n, first), last - 1);
	}

	int dir_ = 1;
	int alt_ = 0;
	std::vector<int> deck_;
};

} // namespace StoermelderPackOne
