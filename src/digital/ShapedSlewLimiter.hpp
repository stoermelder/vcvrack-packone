#include "plugin.hpp"

namespace StoermelderPackOne {

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
		
		float minTime = 1e-2;
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
}; // struct StoermelderShapedSlewLimiter

} // namespace StoermelderPackOne