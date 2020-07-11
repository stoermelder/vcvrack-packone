#pragma once
#include "plugin.hpp"
#include "digital.hpp"
#include <bitset>
#include <random>


namespace Flower {

enum class OUT_CV_MODE {
	BI_1V = 0,
	BI_5V = 1,
	BI_10V = 2,
	UNI_10V = 3,
	UNI_5V = 4,
	UNI_3V = 5,
	UNI_2V = 6,
	UNI_1V = 7
};

enum class OUT_AUX_MODE {
	TRIG = 0,
	TRIG_SLEW = 3,
	CLOCK = 1,
	AUXILIARY = 2
};

enum class SEQ_CV_MODE {
	ATTENUATE = 0,
	SUM = 1
};

enum class PATTERN_TYPE {
	SEQ_FWD = 0,
	SEQ_REV = 1,
	SEQ_ADD_1V = 2,
	SEQ_INV = 3,
	ADD_2STEPS = 4,
	AUX_ADD = 5,
	SEQ_PROB_05 = 6,
	AUX_SUB = 7,
	SEQ_RAND = 8,
	SEQ_OOD = 9,
	SEQ_EVEN = 10,
	SEQ_TRANSPOSE = 11,
	AUX_RAND = 12,
	NUM = 13
};


struct FlowerProcessArgs {
	const static int STEP_VALUE = 0;
	const static int STEP_DISABLED = 1;
	const static int STEP_AUX = 2;
	const static int STEP_PROB = 3;
	const static int STEP_RATCHETS = 4;
	const static int STEP_SLEW = 5;
	const static int STEP_ATTACK = 6;
	const static int STEP_DECAY = 7;
	const static int SEQ_START = 12;
	const static int SEQ_LENGTH = 13;
	const static int PATTERN_CNT = 16;
	const static int PATTERN_RPT = 17;
	typedef std::bitset<24> RandomizeFlags;
	RandomizeFlags randomizeFlagsMaster;
	RandomizeFlags randomizeFlagsSlave;

	float sampleTime;
	float sampleRate;

	bool running;
	bool clockTick = false;
	float clock;
	bool stepTick = false;
	bool randTick = false;

	int stepIndex;
	int stepStart;
	int stepLength;

	bool patternTick = false;
	PATTERN_TYPE patternType;
	int patternMult;

	void reset() {
		clockTick = false;
		stepTick = false;
		randTick = false;
		patternTick = false;
	}
};


struct PatternList {
	static const int SIZE = (int)PATTERN_TYPE::NUM;
	int map[SIZE];
	PATTERN_TYPE slot[SIZE];
	std::string name[SIZE];

	int last = 0;
	int pos = 0;

	void reset(int s = SIZE) {
		for (int i = 0; i < SIZE; i++) {
			slot[i] = (PATTERN_TYPE)i;
			map[i] = i;
		}
		last = s;
		pos = 0;
	}
	void setName(int i, std::string s) {
		name[i] = s;
	}
	std::string getNameAt(int idx) {
		return name[(int)at(idx)];
	}
	bool active(PATTERN_TYPE i) {
		return map[(int)i] < last;
	}
	void enable(PATTERN_TYPE i) {
		int p = map[(int)i];
		if (p < last) return;
		for (int j = p; j > last; j--) { slot[j] = slot[j - 1]; map[(int)slot[j]] = j; }
		slot[last] = (PATTERN_TYPE)i;
		map[(int)i] = last;
		last++;
	}
	void disable(PATTERN_TYPE i) {
		int p = map[(int)i];
		if (p >= last) return;
		if (last == 1) return;
		for (int j = p; j < last - 1; j++) { slot[j] = slot[j + 1]; map[(int)slot[j]] = j; }
		slot[last - 1] = (PATTERN_TYPE)i;
		map[(int)i] = last - 1;
		last--;
	}
	void toggle(PATTERN_TYPE i) {
		if (active(i)) disable(i); else enable(i);
	}
	void moveFwd(PATTERN_TYPE i) {
		int p = map[(int)i];
		if (p == 0) return;
		slot[p] = slot[p - 1];
		slot[p - 1] = i;
		map[(int)slot[p]] = p;
		map[(int)slot[p - 1]] = p - 1;
	}
	void moveBwd(PATTERN_TYPE i) {
		int p = map[(int)i];
		if (p == last - 1) return;
		slot[p] = slot[p + 1];
		slot[p + 1] = i;
		map[(int)slot[p]] = p;
		map[(int)slot[p + 1]] = p + 1;
	}
	PATTERN_TYPE at(int idx) {
		return slot[idx];
	}
	bool isFirst(PATTERN_TYPE i) {
		return map[(int)i] == 0;
	}
	bool isLast(PATTERN_TYPE i) {
		return map[(int)i] == last - 1;
	}
	void setPos(int idx) {
		pos = idx;
	}
	void next() {
		pos = (pos + 1) % last;
	}
	void prev() {
		pos = (pos - 1 + SIZE) % last;
	}
	PATTERN_TYPE current() {
		return slot[pos];
	}
	void toJson(json_t* rootJ) {
		std::string s(SIZE, '0');
		for (int i = 0; i < SIZE; i++) s[i] = 97 + (int)slot[i];
		json_object_set_new(rootJ, "last", json_integer(last));
		json_object_set_new(rootJ, "data", json_string(s.c_str()));
	}
	void fromJson(json_t* rootJ) {
		last = json_integer_value(json_object_get(rootJ, "last"));
		std::string s = json_string_value(json_object_get(rootJ, "data"));
		for (int i = 0; i < SIZE; i++) { slot[i] = (PATTERN_TYPE)(s[i] - 97); map[(int)slot[i]] = i; }
	}
};


// Widgets

struct FlowerLight : RedGreenBlueLight {
	FlowerLight() {
		this->box.size = app::mm2px(math::Vec(4.6f, 4.6f));
	}
	void drawHalo(const DrawArgs& args) override {
		float radius = std::min(box.size.x, box.size.y) / 2.0;
		float oradius = 2.4 * radius;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, radius - oradius, radius - oradius, 2 * oradius, 2 * oradius);

		NVGpaint paint;
		NVGcolor icol = color::mult(color, 0.10);
		NVGcolor ocol = nvgRGB(0, 0, 0);
		paint = nvgRadialGradient(args.vg, radius, radius, radius, oradius, icol, ocol);
		nvgFillPaint(args.vg, paint);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFill(args.vg);
	}
};

struct FlowerKnob : app::SvgKnob {
	FlowerKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/FlowerKnob.svg")));
		fb->removeChild(shadow);
		delete shadow;
	}
};

} // namespace Flower