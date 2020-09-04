#include "plugin.hpp"

namespace StoermelderPackOne {
    
template<typename T, typename PQ = ParamQuantity>
struct ScaledMapParam {
	PQ* paramQuantity = NULL;
	T absoluteMin;
	T absoluteMax;
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

	void setAbsolutes(T min, T max, T uninit) {
		absoluteMin = min;
		absoluteMax = max;
		this->uninit = uninit;
	}

	void reset() {
		filter.reset();
		filterInitialized = false;
		filterSlew = 0.f;
		valueIn = uninit;
		value = -1.f;
		valueOut = std::numeric_limits<float>::infinity();
		min = 0.f;
		max = 1.f;
	}

	void setParamQuantity(PQ* pq) {
		paramQuantity = pq;
		if (paramQuantity && valueOut == std::numeric_limits<float>::infinity()) {
			float f = paramQuantity->getValue();
			value = valueOut = f;
		}
	}

	void setSlew(float slew) {
		filterSlew = slew;
		float s = (1.f / slew) * 10.f;
		filter.setRiseFall(s, s);
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

	void setValue(T i) {
		float f = rescale(float(i), float(absoluteMin), float(absoluteMax), min, max);
		f = clamp(f, 0.f, 1.f);
		f = rescale(f, 0.f, 1.f, paramQuantity->getMinValue(), paramQuantity->getMaxValue());
		valueIn = i;
		value = f;
	}

	void process(float sampleTime = -1.f) {
		if (valueOut == std::numeric_limits<float>::infinity()) return;
		// Set filter from param value if filter is uninitialized
		if (!filterInitialized) {
			filter.out = value = paramQuantity->getValue();
			filterInitialized = true;
		}
		float f = filterSlew > 0.f && sampleTime > 0.f ? filter.process(sampleTime, value) : value;
		if (valueOut != f) {
			paramQuantity->setValue(f);
			valueOut = f;
		}
	}

	T getValue() {
		float f = paramQuantity->getValue();
		if (isNear(valueOut, f)) return valueIn;
		if (valueOut == std::numeric_limits<float>::infinity()) value = valueOut = f;
		f = rescale(f, paramQuantity->getMinValue(), paramQuantity->getMaxValue(), 0.f, 1.f);
		f = rescale(f, min, max, float(absoluteMin), float(absoluteMax));
		f = clamp(f, 0.f, 127.f);
		T i = T(f);
		if (valueIn == uninit) valueIn = i;
		return i;
	}
}; // struct ScaledMapParam

} // namespace StoermelderPackOne