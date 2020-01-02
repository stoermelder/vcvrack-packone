#pragma once

struct ClockMultiplier {
	uint32_t clock = 0;
	uint32_t lastTickSamples = 0;
	float division = 0.f;
	float divisionMult = 0.f;
	uint32_t currentDivision = 0;

	bool process() {
		lastTickSamples++;
		if (division > 0 && currentDivision >= divisionMult) {
			currentDivision++;
			divisionMult += division;
			return true;
		}
		currentDivision++;
		return false;
	}

	void tick() {
		clock = lastTickSamples;
		lastTickSamples = 0;
		division = 0.f;
		divisionMult = 0.f;
		currentDivision = 0;
	}

	void trigger(uint32_t div) {
		if (clock == 0) return;
		division = 0.f;
		divisionMult = 0.f;
		if (div == 0) return;
		division = clock / float(div);
	}

	void reset() {
		clock = 0;
		lastTickSamples = 0;
		division = 0.f;
		divisionMult = 0.f;
		currentDivision = 0;
	}
};


struct LinearFade {
	float rise = 1.f;
	float fall = 1.f;
	float currentRise;
	float currentFall;
	float last = 0.f;

	void reset(float last) {
		currentRise = rise;
		currentFall = 0.f;
		this->last = last;
	}

	void triggerFadeIn() {
		currentRise = (fall > 0.f ? (currentFall / fall) : 0.f) * rise;
		currentFall = 0.f;
		last = 1.f;
	}

	void triggerFadeOut() {
		currentFall = (rise > 0.f ? (currentRise / rise) : 0.f) * fall;
		currentRise = rise;
		last = 0.f;
	}

	inline void setRiseFall(float rise, float fall) {
		if (currentRise == this->rise) currentRise = rise;
		currentFall = std::min(fall, currentFall);
		this->rise = rise;
		this->fall = fall;
	}

	inline float process(float deltaTime) {
		if (currentRise < rise) {
			currentRise += deltaTime;
			return (currentRise / rise);
		}
		else if (currentFall > 0.f) {
			currentFall = std::max(currentFall - deltaTime, 0.f);
			return (currentFall / fall);
		}
		else {
			return last;
		}
	}
};


struct LinearFade4 {
	float rise = 1.f;
	float fall = 1.f;
	simd::float_4 currentRise;
	simd::float_4 currentFall;
	simd::float_4 last = 0.f;

	void reset(int i, float last) {
		currentRise[i] = rise;
		currentFall[i] = 0.f;
		this->last[i] = last;
	}

	void triggerFadeIn(int i) {
		currentRise[i] = (currentFall[i] / fall) * rise;
		currentFall[i] = 0.f;
		last[i] = 1.f;
	}

	void triggerFadeOut(int i) {
		currentFall[i] = (currentRise[i] / rise) * fall;
		currentRise[i] = rise;
		last[i] = 0.f;
	}

	inline void setRiseFall(float rise, float fall) {
		currentRise = simd::ifelse(currentRise == this->rise, rise, currentRise);
		currentFall = simd::fmin(fall, currentFall);
		this->rise = rise;
		this->fall = fall;
	}

	inline simd::float_4 process(float deltaTime) {
		simd::float_4 r = last;

		r = simd::ifelse(currentRise < rise, currentRise / rise, r);
		currentRise = simd::ifelse(currentRise < rise, currentRise += deltaTime, currentRise);

		r = simd::ifelse(currentFall > 0.f, currentFall / fall, r);
		currentFall = simd::ifelse(currentFall > 0.f, simd::fmax(currentFall - deltaTime, 0.f), currentFall);

		return r;
	}
};