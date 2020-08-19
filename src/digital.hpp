#pragma once

struct ClockMultiplier {
	uint32_t clock = 0;
	uint32_t lastTickSamples = 0;
	float division = 0.f;
	float divisionMult = 0.f;
	uint32_t currentDivision = 0;

	bool process() {
		lastTickSamples++;
		if (division > 0 && currentDivision > divisionMult) {
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


struct StoermelderSlewLimiter {
	// Minimum and maximum slopes in volts per second
	const float slewMin = 0.1;
	const float slewMax = 10000.f;
	// Amount of extra slew per voltage difference
	const float shapeScale = 1/10.f;

	float shape	= 0.5f;
	float rise = 0.0f;
	float fall = 0.0f;

	float out = 0.0;

	inline void reset() {
		out = 0.f;
	}
	inline void setShape(float shape) {
		this->shape = shape;
	}
	inline void setRise(float rise) {
		this->rise = rise;
	}
	inline void setFall(float fall) {
		this->fall = fall;
	}
	inline void setRiseFall(float rise, float fall) {
		this->rise = rise;
		this->fall = fall;
	}

	float process(float in, float sampleTime) {
		// Rise
		if (in > out) {
			float slew = slewMax * std::pow(slewMin / slewMax, rise);
			out += slew * crossfade(1.f, shapeScale * (in - out), shape) * sampleTime;
			if (out > in)
				out = in;
		}
		// Fall
		else if (in < out) {
			float slew = slewMax * std::pow(slewMin / slewMax, fall);
			out -= slew * crossfade(1.f, shapeScale * (out - in), shape) * sampleTime;
			if (out < in)
				out = in;
		}
		return out;
	}
};

struct StoermelderShapedSlewLimiter {
	enum RANGE {
		LOW,
		MED,
		HIGH
	};

	float rise = 0.f;
	float fall = 0.f;
	float shape	= 0.f;
	RANGE range = RANGE::MED;

	float out = 0.f;

	inline void setRange(RANGE range) {
		this->range = range;
	}
	inline void setShape(float shape) {
		this->shape = shape;
	}
	inline void setRise(float rise) {
		this->rise = rise;
	}
	inline void setFall(float fall) {
		this->fall = fall;
	}
	inline void setRiseFall(float rise, float fall) {
		this->rise = rise;
		this->fall = fall;
	}
	inline void reset(float out = 0.f) {
		this->out = out;
	}

	inline float shapeDelta(float delta, float tau) {
		float lin = sgn(delta) * 10.f / tau;
		if (shape < 0.f) {
			float log = sgn(delta) * 40.f / tau / (std::fabs(delta) + 1.f);
			return crossfade(lin, log, -shape * 0.95f);
		}
		else {
			float exp = M_E * delta / tau;
			return crossfade(lin, exp, shape * 0.90f);
		}
	}

	float process(float in, float sampleTime) {
		float delta = in - out;
		
		float minTime;
		switch (range) {
			case LOW: minTime = 1e-1; break;
			case MED: minTime = 1e-2; break;
			case HIGH: minTime = 1e-3; break;
		}

		bool rising = false;
		bool falling = false;

		if (delta > 0.f) {
			// Rise
			float riseCv = clamp(rise, 0.f, 1.f);
			float rise = minTime * std::pow(2.f, riseCv * 10.f);
			out += shapeDelta(delta, rise) * sampleTime;
			rising = (in - out > 1e-3f);
		}
		else {
			// Fall
			float fallCv = clamp(fall, 0.f, 1.f);
			float fall = minTime * std::pow(2.f, fallCv * 10.f);
			out += shapeDelta(delta, fall) * sampleTime;
			falling = (in - out < -1e-3f);
		}
		if (!rising && !falling) {
			out = in;
		}
		return out;
	}
};