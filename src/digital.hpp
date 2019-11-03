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