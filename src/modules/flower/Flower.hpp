#pragma once
#include "../../plugin.hpp"
#include "../../utils/digital.hpp"
#include "../../components/Knobs.hpp"
#include <bitset>
#include <random>

namespace StoermelderPackOne {
namespace Flower {

inline bool isFlowerSeqModel(Model* model) {
	return model == modelFlowerSeq || model == modelFlowerSeqEx;
}
inline bool isFlowerTrigModel(Model* model) {
	return model == modelFlowerSeqTrig;
}

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

// Values are intentionally out of declaration order: they are persisted in patches,
// so existing patches would silently change meaning if the numbering were "fixed".
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
	int stepLength = 1;

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
		pos = (pos - 1 + last) % last;
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
		json_t* lastJ = json_object_get(rootJ, "last");
		last = lastJ ? clamp((int)json_integer_value(lastJ), 1, SIZE) : SIZE;

		const char* dataJ = json_string_value(json_object_get(rootJ, "data"));
		if (!dataJ) {
			reset(last);
			return;
		}
		std::string s = dataJ;
		if ((int)s.size() < SIZE) {
			reset(last);
			return;
		}
		bool seen[SIZE] = {};
		for (int i = 0; i < SIZE; i++) {
			int t = s[i] - 97;
			if (t < 0 || t >= SIZE || seen[t]) {
				// Malformed data (out-of-range byte, or a type repeated so another is missing)
				// would otherwise leave slot[]/map[] as something other than a permutation of
				// every PATTERN_TYPE — fall back to a known-good state instead.
				reset(last);
				return;
			}
			seen[t] = true;
			slot[i] = (PATTERN_TYPE)t;
			map[t] = i;
		}
	}
};


// Widgets

// Menu item toggling a single bit of a FlowerProcessArgs::RandomizeFlags bitset.
inline ui::MenuItem* createRandomizeFlagMenuItem(std::string text, FlowerProcessArgs::RandomizeFlags* flags, int idx) {
	return createBoolMenuItem(text, "",
		[=]() { return flags->test(idx); },
		[=](bool b) { flags->set(idx, b); }
	);
}

// Shared context-menu items for FLOWER and OFFSPRING: both host a FlowerSeq<MODULE, STEPS>
// engine at `module->seq` with the same OUT_CV_MODE / OUT_AUX_MODE / SEQ_CV_MODE options.
template <typename MODULE>
void appendFlowerSeqMenu(Menu* menu, MODULE* module) {
	menu->addChild(new MenuSeparator());
	menu->addChild(createSubmenuItem("Step CV knob mode", "", [=](Menu* menu) {
		menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Attenuate", &module->seq.stepCvMode, SEQ_CV_MODE::ATTENUATE));
		menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Sum", &module->seq.stepCvMode, SEQ_CV_MODE::SUM));
	}));

	menu->addChild(new MenuSeparator());
	menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem<OUT_CV_MODE>("CV-port range",
		{
			{ OUT_CV_MODE::BI_10V, "-10..10V" },
			{ OUT_CV_MODE::BI_5V, "-5..5V" },
			{ OUT_CV_MODE::BI_1V, "-1..1V" },
			{ OUT_CV_MODE::UNI_10V, "0..10V" },
			{ OUT_CV_MODE::UNI_5V, "0..5V" },
			{ OUT_CV_MODE::UNI_3V, "0..3V" },
			{ OUT_CV_MODE::UNI_2V, "0..2V" },
			{ OUT_CV_MODE::UNI_1V, "0..1V" }
		},
		&module->seq.outCvMode
	));
	menu->addChild(createBoolPtrMenuItem("Clamp output", "", &module->seq.outCvClamp));
	menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem<OUT_AUX_MODE>("OUT-port mode",
		{
			{ OUT_AUX_MODE::TRIG, "Trigger" },
			{ OUT_AUX_MODE::TRIG_SLEW, "Slewed trigger" },
			{ OUT_AUX_MODE::CLOCK, "Clock" },
			{ OUT_AUX_MODE::AUXILIARY, "Auxiliary sequence" }
		},
		&module->seq.outAuxMode
	));
}

struct FlowerLight : RedGreenBlueLight {
	FlowerLight() {
		this->box.size = mm2px(math::Vec(4.6f, 4.6f));
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
} // namespace StoermelderPackOne