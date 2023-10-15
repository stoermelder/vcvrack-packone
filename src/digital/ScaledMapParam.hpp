#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {
    
template<typename T, typename PQ = ParamQuantity>
struct ScaledMapParam {
	PQ* paramQuantity = NULL;
	float limitMin;
	T limitMinT;
	float limitMax;
	T limitMaxT;
	T uninit;
	float min = 0.f;
	float max = 1.f;

	dsp::ExponentialSlewLimiter filter;
	bool filterInitialized;
	float filterSlew;
	T valueIn;
	float value;
	float valueOut;

	ScaledMapParam() {
		reset();
	}

	void setLimits(T min, T max, T uninit) {
		limitMin = float(min);
		limitMinT = min;
		limitMax = float(max);
		limitMaxT = max;
		this->uninit = uninit;
	}
	T getLimitMin() {
		return limitMinT;
	}
	T getLimitMax() {
		return limitMaxT;
	}

	virtual void reset(bool resetSettings = true) {
		paramQuantity = NULL;
		filter.reset();
		filterInitialized = false;
		valueIn = uninit;
		value = -1.f;
		valueOut = std::numeric_limits<float>::infinity();

		if (resetSettings) {
			filterSlew = 0.f;
			min = 0.f;
			max = 1.f;
		}
	}

	void resetFilter() {
		filter.reset();
		filterInitialized = false;
	}

	void setParamQuantity(PQ* pq) {
		paramQuantity = pq;
		if (paramQuantity && valueOut == std::numeric_limits<float>::infinity()) {
			valueOut = paramQuantity->getScaledValue();
		}
	}

	void setSlew(float slew) {
		filterSlew = slew;
		float s = (1.f / slew) * 10.f;
		filter.setRiseFall(s, s);
		if (filterSlew == 0.f) filterInitialized = false;
	}
	float getSlew() {
		return filterSlew;
	}

	void setMin(float v) {
		min = v;
		if (paramQuantity && valueIn != -1) setValue(valueIn);
	}
	float getMin() {
		return min;
	}

	void setMax(float v) {
		max = v;
		if (paramQuantity && valueIn != -1) setValue(valueIn);
	}
	float getMax() {
		return max;
	}

	virtual void setValue(T i) {
		float f = rescale(float(i), limitMin, limitMax, min, max);
		f = clamp(f, 0.f, 1.f);
		valueIn = i;
		value = f;
	}

	virtual void process(float sampleTime = -1.f, bool force = false) {
		if (valueOut == std::numeric_limits<float>::infinity()) return;
		// Set filter from param value if filter is uninitialized
		if (!filterInitialized) {
			filter.out = paramQuantity->getScaledValue();
			// If setValue has not been called yet use the parameter's current value
			if (value == -1.f) value = filter.out;
			filterInitialized = true;
		}
		float f = filterSlew > 0.f && sampleTime > 0.f ? filter.process(sampleTime, value) : value;
		if (valueOut != f || force) {
			Param* param = paramQuantity->getParam();
			if (param) {
				float vScaled = math::rescale(f, 0.f, 1.f, paramQuantity->getMinValue(), paramQuantity->getMaxValue());
				if (paramQuantity->snapEnabled) vScaled = std::round(vScaled);
				paramQuantity->getParam()->setValue(vScaled);
			}
			else {
				// Only used by "fake" paramQuantaties for CV-ports
				paramQuantity->setScaledValue(f);
			}
			valueOut = f;
		}
	}

	virtual T getValue() {
		float f = paramQuantity->getScaledValue();
		if (isNear(valueOut, f)) return valueIn;
		// Reset the internal values to the actual parameter's value in case 
		// getValue() is called before setValue() - for proper MIDI feedback
		if (valueOut == std::numeric_limits<float>::infinity()) value = valueOut = f;
		// If a parameter is snapped then the returned value of ParaQuantity can't be trusted
		// -> simply return the input value, assuming the value hasn't changed so much that
		// it changes the value that would be snapped to.
		if (paramQuantity->snapEnabled) {
			size_t numSnaps = static_cast<size_t>(paramQuantity->maxValue - paramQuantity->minValue + 1);
			size_t snapF = static_cast<size_t>(f * numSnaps + 0.5);
			size_t snapOut = static_cast<size_t>(valueOut * numSnaps + 0.5);
			if (snapF == snapOut) {
				f = valueOut;
			}
		}

		f = rescale(f, min, max, limitMin, limitMax);
		f = clamp(f, limitMin, limitMax);
		T i = T(f);
		if (valueIn == uninit) valueIn = i;
		return i;
	}

	float getLightBrightness() {
		if (!paramQuantity) return 0.f;
		return valueOut;
	}
}; // struct ScaledMapParam

} // namespace StoermelderPackOne
