#include "plugin.hpp"
#include "FlowerSeq.hpp"
#include <osdialog.h>

namespace Flower {

enum class PHRASE_CV_MODE {
	OFF = -1,
	TRIG_FWD = 0,
	VOLT = 8,
	C4 = 9,
	ARM = 10
};

enum class MUTATE_DISTRIBUTION {
	UNIFORM_FIXED_0 = 0,
	UNIFORM = 1,
	BINOMIAL_FIXED_0 = 2,
	BINOMIAL = 3
};


template < int STEPS, int PATTERNS, int PHRASES >
struct FlowerSeqModule : Module {
	enum ParamIds {
		PARAM_RUN,
		PARAM_RESET,
		PARAM_STEPLENGTH,
		PARAM_START,
		PARAM_RAND,
		PARAM_STEPMODE,
		ENUMS(PARAM_STEP, STEPS),
		ENUMS(PARAM_STEP_BUTTON, STEPS),
		ENUMS(PARAM_PHRASE_SELECT, PHRASES),
		ENUMS(PARAM_PATTERN_SELECT, PATTERNS),
		PARAM_STEP_CENTER,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_CLOCK,
		INPUT_RAND,
		INPUT_RUN,
		INPUT_RESET,
		INPUT_STEPCNT,
		INPUT_START,
		INPUT_PHRASE,
		INPUT_MUTATE,
		ENUMS(INPUT_STEP, STEPS),
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_CV,
		OUTPUT_AUX,
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_CLOCK,
		LIGHT_RAND,
		ENUMS(LIGHT_STEP, STEPS * 3),
		ENUMS(LIGHT_PATTERN_SELECT, PATTERNS * 3),
		ENUMS(LIGHT_PATTERN, PATTERNS * 4),
		ENUMS(LIGHT_PHRASE_SELECT, PHRASES * 3),
		ENUMS(LIGHT_EDIT, STEPS * 3),
		NUM_LIGHTS
	};

	typedef FlowerSeqModule<STEPS, PATTERNS, PHRASES> MODULE;

	struct PatternParamQuantity : ParamQuantity {
		MODULE* module;
		int i;
		std::string getDisplayValueString() override {
			std::string s = "";
			switch (module->phrases[module->phraseIndex].patterns[i].type) {
				case PATTERN_TYPE::SEQ_FWD: s = "Forward"; break;
				case PATTERN_TYPE::SEQ_REV: s = "Reverse"; break;
				case PATTERN_TYPE::SEQ_ADD_1V: s = "Add 1V"; break;
				case PATTERN_TYPE::SEQ_INV: s = "Inverse"; break;
				case PATTERN_TYPE::ADD_2STEPS: s = "Add 2 steps"; break;
				case PATTERN_TYPE::AUX_ADD: s = "Add auxiliary sequence"; break;
				case PATTERN_TYPE::SEQ_PROB_05: s = "Step probability 0.5"; break;
				case PATTERN_TYPE::AUX_SUB: s = "Subtract auxiliary sequence"; break;
				case PATTERN_TYPE::SEQ_RAND: s = "Random"; break;
				case PATTERN_TYPE::SEQ_OOD: s = "Odd steps"; break;
				case PATTERN_TYPE::SEQ_EVEN: s = "Even steps"; break;
				case PATTERN_TYPE::SEQ_TRANSPOSE: s = "Transpose"; break;
				case PATTERN_TYPE::AUX_RAND: s = "Random auxiliary sequence"; break;
				default: break;
			}
			if (module->phrases[module->phraseIndex].patterns[i].mult > 1) {
				s += string::f(" (%i times)", module->phrases[module->phraseIndex].patterns[i].mult);
			}
			s += "\nShort press: next pattern\nLong press: toggle number of repeats 1 -> 2 -> 3 -> 4";
			return s;
		}
	}; // PatternParamQuantity


	struct FlowerPattern {
		PATTERN_TYPE type;
		int mult;
	};

	struct FlowerPhrase {
		FlowerPattern patterns[PATTERNS];
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	
	/** [Stored to JSON] */
	typedef FlowerSeq<MODULE, STEPS> SEQ;
	SEQ seq{this};

	/** [Stored to JSON] flags which targets should be randomized */
	FlowerProcessArgs::RandomizeFlags randomizeFlags;
	/** [Stored to JSON] indicates if the sequencer is running */
	bool running = true;

	/** [Stored to JSON] currently active step before pattern-transform */
	int stepIndex;

	/** [Stored to JSON] currently selected pattern */
	int patternIndex = 0;
	/** [Stored to JSON] number of currently active patterns */
	int patternCount;
	/** remaining repeats of the currently active pattern */
	int patternMultCount;
	/** [Stored to JSON] type of the used random distribution for mutating patterns */
	MUTATE_DISTRIBUTION patternMutateDist;
	/** random distributions of mutating the pattern types */
	std::binomial_distribution<int> patternBinomialDist[PATTERNS - 1];

	/** [Stored to JSON] set of currently used pattern types */
	//std::set<PATTERN_TYPE> patternTypeSet;
	/** trivial locking-variable for thread-safe access on unsafe collections */
	bool patternSetInChange = false;

	/** [Stored to JSON] mode for PHRASE input */
	PHRASE_CV_MODE phraseCvMode = PHRASE_CV_MODE::TRIG_FWD;
	/** [Stored to JSON] currently selected phrase */
	int phraseIndex = 0;
	/** [Stored to JSON] number of currently active phrases */
	int phraseCount;
	/** holds the next phrase if in PHRASE_CV_MODE::ARM */
	int phraseNext;
	/** [Stored to JSON] the phrases */
	FlowerPhrase phrases[PHRASES];

	PatternList patternList;

	dsp::SchmittTrigger seqRandTrigger;
	dsp::SchmittTrigger runningTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger clockTrigger;
	dsp::Timer resetTimer;

	dsp::SchmittTrigger patternParamTrigger[PATTERNS];
	LongPressButton patternButtons[PATTERNS];
	dsp::SchmittTrigger patternMutateTrigger;

	dsp::SchmittTrigger phraseParamTrigger[PHRASES];
	dsp::SchmittTrigger phraseTrigger;

	dsp::ClockDivider paramDivider;
	dsp::ClockDivider lightDivider;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};

	FlowerProcessArgs argsProducer;
	FlowerProcessArgs argsConsumer;

	FlowerSeqModule() {
		leftExpander.consumerMessage = &argsConsumer;
		leftExpander.producerMessage = &argsProducer;
		rightExpander.consumerMessage = &argsConsumer;
		rightExpander.producerMessage = &argsProducer;

		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_RUN, 0.f, 1.f, 0.f, "Run");
		configParam<TriggerParamQuantity>(PARAM_RESET, 0.f, 1.f, 0.f, "Reset");
		configParam<TriggerParamQuantity>(PARAM_RAND, 0.f, 1.f, 0.f, "Randomize sequence");
		configParam(PARAM_STEPLENGTH, 1.f, STEPS, STEPS, "Sequence length");
		configParam(PARAM_START, 0.f, STEPS - 1, 0.f, "Sequence start", "", 0.f, 1.f, 1.f);

		configParam<SeqStepModeParamQuantity<MODULE>>(PARAM_STEPMODE, 0.f, 1.f, 0.f, "Mode");
		auto pq1 = dynamic_cast<SeqStepModeParamQuantity<MODULE>*>(paramQuantities[PARAM_STEPMODE]);
		pq1->module = this;

		configParam<SeqFlowerKnobParamQuantity<MODULE>>(PARAM_STEP_CENTER, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), 0.f);
		auto pq2 = dynamic_cast<SeqFlowerKnobParamQuantity<MODULE>*>(paramQuantities[PARAM_STEP_CENTER]);
		pq2->module = this;

		for (int i = 0; i < STEPS; i++) {
			configParam<SeqStepParamQuantity<MODULE>>(PARAM_STEP + i, 0.f, 1.f, 0.5f, string::f("Step %i", i + 1), "V");
			auto pq1 = dynamic_cast<SeqStepParamQuantity<MODULE>*>(paramQuantities[PARAM_STEP + i]);
			pq1->module = this;
			pq1->i = i;

			configParam<SeqStepButtonParamQuantity<MODULE, STEPS>>(PARAM_STEP_BUTTON + i, 0.f, 1.f, 0.f);
			auto pq2 = dynamic_cast<SeqStepButtonParamQuantity<MODULE, STEPS>*>(paramQuantities[PARAM_STEP_BUTTON + i]);
			pq2->module = this;
			pq2->i = i;
		}

		for (int i = 0; i < PHRASES; i++) {
			configParam<TriggerParamQuantity>(PARAM_PHRASE_SELECT + i, 0.f, 1.f, 0.f, string::f("Phrase %i", i + 1));
		}

		for (int i = 0; i < PATTERNS; i++) {
			configParam<PatternParamQuantity>(PARAM_PATTERN_SELECT + i, 0.f, 1.f, 0.f, string::f("Pattern %i", i + 1));
			PatternParamQuantity* pq = dynamic_cast<PatternParamQuantity*>(paramQuantities[PARAM_PATTERN_SELECT + i]);
			pq->module = this;
			pq->i = i;
			patternButtons[i].param = &params[PARAM_PATTERN_SELECT + i];
		}

		for (int i = 0; i < PATTERNS; i++) {
			if (i > 0) patternBinomialDist[i - 1] = std::binomial_distribution<int>(i + 1, 0.5);
		}

		paramDivider.setDivision(32);
		lightDivider.setDivision(512);

		patternList.setName((int)PATTERN_TYPE::SEQ_FWD,		"[oxxx] Forward");
		patternList.setName((int)PATTERN_TYPE::SEQ_REV,		"[xoxx] Reverse");
		patternList.setName((int)PATTERN_TYPE::SEQ_ADD_1V, 	"[ooxx] Add 1V");
		patternList.setName((int)PATTERN_TYPE::SEQ_INV,		"[xxox] Inverse");
		patternList.setName((int)PATTERN_TYPE::ADD_2STEPS,	"[oxox] Add 2 steps");
		patternList.setName((int)PATTERN_TYPE::AUX_ADD,		"[xoox] Add auxiliary sequence");
		patternList.setName((int)PATTERN_TYPE::SEQ_PROB_05,	"[ooox] Step probability 0.5");
		patternList.setName((int)PATTERN_TYPE::AUX_SUB,		"[xxxo] Subtract auxiliary sequence");
		patternList.setName((int)PATTERN_TYPE::SEQ_RAND,		"[oxxo] Random");
		patternList.setName((int)PATTERN_TYPE::SEQ_OOD,		"[xoxo] Odd steps only");
		patternList.setName((int)PATTERN_TYPE::SEQ_EVEN,		"[ooxo] Even steps only");
		patternList.setName((int)PATTERN_TYPE::SEQ_TRANSPOSE,"[xxoo] Transpose");
		patternList.setName((int)PATTERN_TYPE::AUX_RAND,		"[oxoo] Random auxiliary sequence");

		onReset();
	}

	void onReset() override {
		Module::onReset();
		randomizeFlags.reset();
		randomizeFlags.set(0);
		seq.reset();
		running = true;

		// Steps
		stepIndex = 0;

		// Patterns
		patternIndex = 0;
		patternCount = PATTERNS / 2;
		patternMultCount = 1;
		patternMutateDist = MUTATE_DISTRIBUTION::UNIFORM_FIXED_0;
		for (int i = 0; i < PHRASES; i++) {
			for (int j = 0; j < PATTERNS; j++) {
				phrases[i].patterns[j].type = (PATTERN_TYPE)(j % (int)PATTERN_TYPE::NUM);
				phrases[i].patterns[j].mult = 1;
			}
		}
		patternList.reset();
		//patternTypeSet.clear();
		//for (int i = 0; i < (int)PATTERN_TYPE::NUM; i++) {
		//	patternTypeSet.insert((PATTERN_TYPE)i);
		//}

		// Phrases
		phraseCvMode = PHRASE_CV_MODE::TRIG_FWD;
		phraseIndex = 0;
		phraseCount = PHRASES / 2;
	}

	void process(const ProcessArgs& args) override {
		if (paramDivider.process()) {
			// Pattern-buttons
			for (int i = 0; i < PATTERNS; i++) {
				switch (patternButtons[i].process(args.sampleTime, 0.8f / float(paramDivider.getDivision()))) {
					default:
					case LongPressButton::NO_PRESS:
						break;
					case LongPressButton::SHORT_PRESS:
						patternNext(i, 1);
						break;
					case LongPressButton::LONG_PRESS:
						phrases[phraseIndex].patterns[i].mult = (phrases[phraseIndex].patterns[i].mult) % 4 + 1;
						break;
				}
			}

			// Phrase-buttons
			for (int i = 0; i < PHRASES; i++) {
				if (phraseParamTrigger[i].process(params[PARAM_PHRASE_SELECT + i].getValue())) {
					if (phraseCvMode != PHRASE_CV_MODE::ARM)
						phraseSetIndex(i);
					else
						phraseNext = i;
				}
			}
		}

		// PHRASE-input
		if (inputs[INPUT_PHRASE].isConnected()) {
			switch (phraseCvMode) {
				case PHRASE_CV_MODE::OFF: {
					break;
				}
				case PHRASE_CV_MODE::TRIG_FWD: {
					if (phraseTrigger.process(inputs[INPUT_PHRASE].getVoltage())) {
						int s = (phraseIndex + 1) % phraseCount;
						phraseSetIndex(s);
					}
					break;
				}
				case PHRASE_CV_MODE::C4: {
					int s = std::round(clamp(inputs[INPUT_PHRASE].getVoltage() * 12.f, 0.f, phraseCount - 1.f));
					phraseSetIndex(s);
					break;
				}
				case PHRASE_CV_MODE::VOLT: {
					int s = std::floor(rescale(inputs[INPUT_PHRASE].getVoltage(), 0.f, 10.f, 0, phraseCount - 1e-3f));
					phraseSetIndex(s);
					break;
				}
				case PHRASE_CV_MODE::ARM: {
					if (phraseTrigger.process(inputs[INPUT_PHRASE].getVoltage())) {
						phraseSetIndex(phraseNext);
					}
					break;
				}
			}
		}

		// MUTATE-input
		if (patternMutateTrigger.process(inputs[INPUT_MUTATE].getVoltage())) {
			patternMutate();
		}

		auto seqArgs = reinterpret_cast<FlowerProcessArgs*>(rightExpander.producerMessage);
		seqArgs->reset();

		// RUN-input / RUN-button
		if (runningTrigger.process(inputs[INPUT_RUN].getVoltage() + params[PARAM_RUN].getValue())) {
			running = !running;
		}

		// RESET-input / RESET-button
		if (resetTrigger.process(inputs[INPUT_RESET].getVoltage() + params[PARAM_RESET].getValue())) {
			patternIndex = 0;
			seqArgs->patternTick = stepSetIndex(0);
			seqArgs->stepTick = true;
			resetTimer.reset();
		}

		// CLOCK-input
		if (running) {
			seqArgs->clock = inputs[INPUT_CLOCK].getVoltage();
			if (resetTimer.process(args.sampleTime) >= 1e-3f && clockTrigger.process(seqArgs->clock)) {
				seqArgs->patternTick = stepSetIndex(stepIndex + 1);
				seqArgs->stepTick = true;
				seqArgs->clockTick = true;
			}
		}

		// SEQ_RAND-input / SEQ_RAND-button
		if (seqRandTrigger.process(inputs[INPUT_RAND].getVoltage() + params[PARAM_RAND].getValue())) {
			doRandomize();
			seqArgs->randTick = true;
		}

		seqArgs->randomizeFlagsMaster = randomizeFlags;
		seqArgs->sampleTime = args.sampleTime;
		seqArgs->sampleRate = args.sampleRate;

		seqArgs->stepIndex = stepIndex;
		seqArgs->stepStart = stepGetSeqStart();
		seqArgs->stepLength = stepGetSeqLength();
		seqArgs->running = running;
		seqArgs->patternType = phrases[phraseIndex].patterns[patternIndex].type;
		seqArgs->patternMult = phrases[phraseIndex].patterns[patternIndex].mult;

		seq.process(*seqArgs);
		if (lightDivider.process()) processLights(args);

		leftExpander.messageFlipRequested = true;
		rightExpander.messageFlipRequested = true;
	}

	void processLights(const ProcessArgs& args) {
		float st = args.sampleTime * lightDivider.getDivision();

		// Pattern lights
		for (int i = 0; i < PATTERNS; i++) {
			float r = i == patternIndex ? 1.f : (i < patternCount && phrases[phraseIndex].patterns[i].mult >= 2 ? 1.0f : 0.f);
			float g = i == patternIndex ? 1.f : (i < patternCount && phrases[phraseIndex].patterns[i].mult >= 2 ? (-1.0f + phrases[phraseIndex].patterns[i].mult * 0.5f) : 0.f);
			float b = i == patternIndex ? 1.f : (i < patternCount && phrases[phraseIndex].patterns[i].mult == 1 ? 0.7f : 0.f);
			lights[LIGHT_PATTERN_SELECT + i * 3 + 0].setSmoothBrightness(r, st);
			lights[LIGHT_PATTERN_SELECT + i * 3 + 1].setSmoothBrightness(g, st);
			lights[LIGHT_PATTERN_SELECT + i * 3 + 2].setSmoothBrightness(b, st);
			lights[LIGHT_PATTERN + i * 4 + 0].setBrightness(i < patternCount && ((int)phrases[phraseIndex].patterns[i].type + 1) & 1);
			lights[LIGHT_PATTERN + i * 4 + 1].setBrightness(i < patternCount && ((int)phrases[phraseIndex].patterns[i].type + 1) & 2);
			lights[LIGHT_PATTERN + i * 4 + 2].setBrightness(i < patternCount && ((int)phrases[phraseIndex].patterns[i].type + 1) & 4);
			lights[LIGHT_PATTERN + i * 4 + 3].setBrightness(i < patternCount && ((int)phrases[phraseIndex].patterns[i].type + 1) & 8);
		}

		// Phrase lights
		for (int i = 0; i < PHRASES; i++) {
			float r = i == phraseIndex ? 1.f : (phraseCvMode == PHRASE_CV_MODE::ARM && i == phraseNext ? 1.f : 0.f);
			float g = i == phraseIndex ? 1.f : 0.f;
			float b = i == phraseIndex ? 1.f : (i < phraseCount && (phraseCvMode != PHRASE_CV_MODE::ARM || i != phraseNext) ? 0.7f : 0.f);
			lights[LIGHT_PHRASE_SELECT + i * 3 + 0].setSmoothBrightness(r, st);
			lights[LIGHT_PHRASE_SELECT + i * 3 + 1].setSmoothBrightness(g, st);
			lights[LIGHT_PHRASE_SELECT + i * 3 + 2].setSmoothBrightness(b, st);
		}
	}

	void doRandomize() {
		if (randomizeFlags.test(FlowerProcessArgs::SEQ_START))
			params[PARAM_START].setValue(random::u32() % STEPS);
		if (randomizeFlags.test(FlowerProcessArgs::SEQ_LENGTH))
			params[PARAM_STEPLENGTH].setValue((random::u32() % STEPS) + 1);
		if (randomizeFlags.test(FlowerProcessArgs::PATTERN_CNT))
			patternCount = (random::u32() % PATTERNS) + 1;
		for (int i = 0; i < PATTERNS; i++) {
			if (randomizeFlags.test(FlowerProcessArgs::PATTERN_RPT))
				phrases[phraseIndex].patterns[i].mult = (random::u32() % 4) + 1;
		}
	}

	void seqSetLength(int length) {
		params[PARAM_STEPLENGTH].setValue(length);
	}

	bool stepSetIndex(int index) {
		bool ret = false;
		int numSteps = stepGetSeqLength();

		if (index >= numSteps) {
			if (patternMultCount > 1) {
				patternMultCount--;
			}
			else {
				patternIndex = (patternIndex + 1) % patternCount;
				patternMultCount = phrases[phraseIndex].patterns[patternIndex].mult;
				ret = true;
			}
			stepIndex = 0;
		}
		else {
			stepIndex = index;
		}
		return ret;
	}

	inline int stepGetSeqStart() {
		return (int)clamp(std::round(params[PARAM_START].getValue() + inputs[INPUT_START].getVoltage()), 0.f, float(STEPS - 1));
	}

	inline int stepGetSeqLength() {
		return (int)clamp(std::round(params[PARAM_STEPLENGTH].getValue() + inputs[INPUT_STEPCNT].getVoltage()), 1.f, float(STEPS));
	}

	void patternNext(int p, int n) {
		/*
		if (patternSetInChange) return;
		auto it = patternTypeSet.find(phrases[phraseIndex].patterns[p].type);
		std::advance(it, n);
		if (it == patternTypeSet.end()) it = patternTypeSet.begin();
		else if (it == patternTypeSet.begin()) it = patternTypeSet.end()--;
		phrases[phraseIndex].patterns[p].type = *it;
		*/
		if (n == 1)  patternList.next();
		if (n == -1) patternList.prev();
		phrases[phraseIndex].patterns[p].type = (PATTERN_TYPE)patternList.current();
	}

	void patternCheck() {
		for (int i = 0; i < PHRASES; i++) {
			for (int j = 0; j < PATTERNS; j++) {
				while (!patternList.active(phrases[i].patterns[j].type))
					phrases[i].patterns[j].type = (PATTERN_TYPE)((int)phrases[i].patterns[j].type + 1);
			}
		}
	}

	void patternSetCount(int count) {
		patternCount = count;
		patternIndex = std::min(patternIndex, patternCount - 1);
	}

	void patternMutate() {
		switch (patternMutateDist) {
			case MUTATE_DISTRIBUTION::UNIFORM_FIXED_0: {
				if (patternCount == 1) break;
				int p = 1 + random::u32() % (patternCount - 1);
				patternNext(p, random::uniform() < 0.5f ? -1 : 1);
				break;
			}
			case MUTATE_DISTRIBUTION::UNIFORM: {
				int p = random::u32() % patternCount;
				patternNext(p, random::uniform() < 0.5f ? -1 : 1);
				break;
			}
			case MUTATE_DISTRIBUTION::BINOMIAL_FIXED_0: {
				switch (patternCount) {
					case 1:
						break;
					case 2:
						patternNext(1, random::uniform() < 0.5f ? -1 : 1); 
						break;
					default:
						int p = 1 + patternBinomialDist[patternCount - 3](randGen);
						patternNext(p, random::uniform() < 0.5f ? -1 : 1);
						break;
				}
				break;
			}
			case MUTATE_DISTRIBUTION::BINOMIAL: {
				switch (patternCount) {
					case 1:
						patternNext(0, random::uniform() < 0.5f ? -1 : 1);
						break;
					default:
						int p = patternBinomialDist[patternCount - 2](randGen);
						patternNext(p, random::uniform() < 0.5f ? -1 : 1);
						break;
				}
				break;
			}
		}
	}

	void phraseSetIndex(int phrase) {
		if (phraseIndex == phrase) return;
		if (phrase < 0) return;
		phraseIndex = phraseNext = std::min(phrase, phraseCount - 1);
	}

	void phraseSetCount(int count) {
		phraseCount = count;
		phraseIndex = std::min(phraseIndex, phraseCount - 1);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		seq.dataToJson(rootJ);
		json_object_set_new(rootJ, "randomizeFlags", json_string(randomizeFlags.to_string().c_str()));

		json_object_set_new(rootJ, "running", json_boolean(running));

		// Steps
		json_object_set_new(rootJ, "stepIndex", json_integer(stepIndex));

		// Patterns
		json_object_set_new(rootJ, "patternIndex", json_integer(patternIndex));
		json_object_set_new(rootJ, "patternCount", json_integer(patternCount));
		json_object_set_new(rootJ, "patternMutateDist", json_integer((int)patternMutateDist));
		json_t* patternListJ = json_object();
		patternList.toJson(patternListJ);
		json_object_set_new(rootJ, "patternList", patternListJ);

		/*
		json_t* patternTypeSetJ = json_array();
		for (PATTERN_TYPE p : patternTypeSet) {
			json_array_append_new(patternTypeSetJ, json_integer((int)p));
		}
		json_object_set_new(rootJ, "patternTypeSet", patternTypeSetJ);
		*/

		// Phrases
		json_object_set_new(rootJ, "phraseIndex", json_integer(phraseIndex));
		json_object_set_new(rootJ, "phraseCvMode", json_integer((int)phraseCvMode));
		json_object_set_new(rootJ, "phraseCount", json_integer(phraseCount));

		json_t* phrasesJ = json_array();
		for (int i = 0; i < PHRASES; i++) {
			json_t* phraseJ = json_object();
			json_t* patternsJ = json_array();
			for (int j = 0; j < PATTERNS; j++) {
				json_t* patternJ = json_object();
				json_object_set_new(patternJ, "type", json_integer((int)phrases[i].patterns[j].type));
				json_object_set_new(patternJ, "mult", json_integer(phrases[i].patterns[j].mult));
				json_array_append_new(patternsJ, patternJ);
			}
			json_object_set_new(phraseJ, "patterns", patternsJ);
			json_array_append_new(phrasesJ, phraseJ);
		}
		json_object_set_new(rootJ, "phrases", phrasesJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		seq.dataFromJson(rootJ);
		randomizeFlags = FlowerProcessArgs::RandomizeFlags(json_string_value(json_object_get(rootJ, "randomizeFlags")));

		running = json_is_true(json_object_get(rootJ, "running"));

		// Steps
		stepIndex = json_integer_value(json_object_get(rootJ, "stepIndex"));

		// Patterns
		patternIndex = json_integer_value(json_object_get(rootJ, "patternIndex"));
		patternCount = json_integer_value(json_object_get(rootJ, "patternCount"));
		patternMutateDist = (MUTATE_DISTRIBUTION)json_integer_value(json_object_get(rootJ, "patternMutateDist"));
		json_t* patternListJ = json_object_get(rootJ, "patternList");
		patternList.fromJson(patternListJ);

		/*
		patternTypeSet.clear();
		json_t* patternTypeSetJ = json_object_get(rootJ, "patternTypeSet");
		json_t* patternTypeJ;
		size_t patternTypeSetIndex;
		json_array_foreach(patternTypeSetJ, patternTypeSetIndex, patternTypeJ) {
			patternTypeSet.insert((PATTERN_TYPE)json_integer_value(patternTypeJ));
		}
		*/

		// Phrases
		phraseIndex = json_integer_value(json_object_get(rootJ, "phraseIndex"));
		phraseCvMode = (PHRASE_CV_MODE)json_integer_value(json_object_get(rootJ, "phraseCvMode"));
		phraseCount = json_integer_value(json_object_get(rootJ, "phraseCount"));

		json_t* phrasesJ = json_object_get(rootJ, "phrases");
		for (int i = 0; i < PHRASES; i++) {
			json_t* phraseJ = json_array_get(phrasesJ, i);
			json_t* patternsJ = json_object_get(phraseJ, "patterns");
			for (int j = 0; j < PATTERNS; j++) {
				json_t* patternJ = json_array_get(patternsJ, j);
				phrases[i].patterns[j].type = (PATTERN_TYPE)json_integer_value(json_object_get(patternJ, "type"));
				phrases[i].patterns[j].mult = json_integer_value(json_object_get(patternJ, "mult"));
			}
		}
	}
};



struct FlowerSeqWidget : ThemedModuleWidget<FlowerSeqModule<16, 8, 8>> {
	typedef FlowerSeqModule<16, 8, 8> MODULE;

	FlowerSeqWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "FlowerSeq") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<FlowerKnob>(Vec(195.0f, 190.0f), module, MODULE::PARAM_STEP_CENTER));

		addParam(createParamCentered<TL1105>(Vec(116.2, 53.4f), module, MODULE::PARAM_RUN));
		addInput(createInputCentered<StoermelderPort>(Vec(93.1f, 66.4f), module, MODULE::INPUT_RUN));
		addParam(createParamCentered<TL1105>(Vec(58.5f, 111.2f), module, MODULE::PARAM_RESET));
		addInput(createInputCentered<StoermelderPort>(Vec(71.4f, 88.1f), module, MODULE::INPUT_RESET));

		addParam(createParamCentered<StoermelderTrimpotSnap>(Vec(274.0f, 53.4f), module, MODULE::PARAM_STEPLENGTH));
		addInput(createInputCentered<StoermelderPort>(Vec(297.0f, 66.3f), module, MODULE::INPUT_STEPCNT));
		addInput(createInputCentered<StoermelderPort>(Vec(318.7f, 88.0f), module, MODULE::INPUT_START));
		addParam(createParamCentered<StoermelderTrimpotSnap>(Vec(331.7f, 112.2f), module, MODULE::PARAM_START));

		addParam(createParamCentered<TL1105>(Vec(116.2f, 326.7f), module, MODULE::PARAM_RAND));
		addInput(createInputCentered<StoermelderPort>(Vec(93.2f, 313.7f), module, MODULE::INPUT_RAND));
		addInput(createInputCentered<StoermelderPort>(Vec(71.5f, 292.0f), module, MODULE::INPUT_CLOCK));

		addOutput(createOutputCentered<StoermelderPort>(Vec(318.9f, 291.9f), module, MODULE::OUTPUT_CV));
		addOutput(createOutputCentered<StoermelderPort>(Vec(297.1f, 313.6f), module, MODULE::OUTPUT_AUX));
		addParam(createParamCentered<TL1105>(Vec(274.f, 326.7f), module, MODULE::PARAM_STEPMODE));

		// Edit leds
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(219.2f, 68.5f), module, MODULE::LIGHT_EDIT + 0 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(262.9f, 86.4f), module, MODULE::LIGHT_EDIT + 1 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(298.1f, 121.1f), module, MODULE::LIGHT_EDIT + 2 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(316.6f, 165.8f), module, MODULE::LIGHT_EDIT + 3 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(316.6f, 214.1f), module, MODULE::LIGHT_EDIT + 4 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(298.1f, 257.9f), module, MODULE::LIGHT_EDIT + 5 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(262.9f, 293.0f), module, MODULE::LIGHT_EDIT + 6 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(219.2f, 311.5f), module, MODULE::LIGHT_EDIT + 7 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(171.0f, 311.5f), module, MODULE::LIGHT_EDIT + 8 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(126.4f, 293.0f), module, MODULE::LIGHT_EDIT + 9 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(92.2f, 257.9f), module, MODULE::LIGHT_EDIT + 10 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(73.6f, 214.1f), module, MODULE::LIGHT_EDIT + 11 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(73.6f, 165.8f), module, MODULE::LIGHT_EDIT + 12 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(92.2f, 121.1f), module, MODULE::LIGHT_EDIT + 13 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(126.4f, 86.4f), module, MODULE::LIGHT_EDIT + 14 * 3));
		addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(Vec(171.0f, 68.5f), module, MODULE::LIGHT_EDIT + 15 * 3));

		// Steps
		addParam(createParamCentered<LEDButton>(Vec(195.0f, 137.9f), module, MODULE::PARAM_STEP_BUTTON + 0));
		addParam(createParamCentered<LEDButton>(Vec(214.9f, 141.9f), module, MODULE::PARAM_STEP_BUTTON + 1));
		addParam(createParamCentered<LEDButton>(Vec(231.8f, 153.2f), module, MODULE::PARAM_STEP_BUTTON + 2));
		addParam(createParamCentered<LEDButton>(Vec(243.0f, 169.7f), module, MODULE::PARAM_STEP_BUTTON + 3));
		addParam(createParamCentered<LEDButton>(Vec(247.1f, 190.0f), module, MODULE::PARAM_STEP_BUTTON + 4));
		addParam(createParamCentered<LEDButton>(Vec(243.1f, 209.9f), module, MODULE::PARAM_STEP_BUTTON + 5));
		addParam(createParamCentered<LEDButton>(Vec(231.8f, 226.8f), module, MODULE::PARAM_STEP_BUTTON + 6));
		addParam(createParamCentered<LEDButton>(Vec(214.9f, 238.1f), module, MODULE::PARAM_STEP_BUTTON + 7));
		addParam(createParamCentered<LEDButton>(Vec(195.0f, 242.1f), module, MODULE::PARAM_STEP_BUTTON + 8));
		addParam(createParamCentered<LEDButton>(Vec(175.1f, 238.1f), module, MODULE::PARAM_STEP_BUTTON + 9));
		addParam(createParamCentered<LEDButton>(Vec(158.2f, 226.8f), module, MODULE::PARAM_STEP_BUTTON + 10));
		addParam(createParamCentered<LEDButton>(Vec(146.9f, 209.9f), module, MODULE::PARAM_STEP_BUTTON + 11));
		addParam(createParamCentered<LEDButton>(Vec(142.9f, 190.0f), module, MODULE::PARAM_STEP_BUTTON + 12));
		addParam(createParamCentered<LEDButton>(Vec(146.9f, 169.7f), module, MODULE::PARAM_STEP_BUTTON + 13));
		addParam(createParamCentered<LEDButton>(Vec(158.2f, 153.2f), module, MODULE::PARAM_STEP_BUTTON + 14));
		addParam(createParamCentered<LEDButton>(Vec(175.1f, 141.9f), module, MODULE::PARAM_STEP_BUTTON + 15));

		addChild(createLightCentered<FlowerLight>(Vec(195.0f, 137.9f), module, MODULE::LIGHT_STEP + 0 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(214.9f, 141.9f), module, MODULE::LIGHT_STEP + 1 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(231.8f, 153.2f), module, MODULE::LIGHT_STEP + 2 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(243.0f, 169.7f), module, MODULE::LIGHT_STEP + 3 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(247.1f, 190.0f), module, MODULE::LIGHT_STEP + 4 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(243.1f, 209.9f), module, MODULE::LIGHT_STEP + 5 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(231.8f, 226.8f), module, MODULE::LIGHT_STEP + 6 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(214.9f, 238.1f), module, MODULE::LIGHT_STEP + 7 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(195.0f, 242.1f), module, MODULE::LIGHT_STEP + 8 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(175.1f, 238.1f), module, MODULE::LIGHT_STEP + 9 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(158.2f, 226.8f), module, MODULE::LIGHT_STEP + 10 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(146.9f, 209.9f), module, MODULE::LIGHT_STEP + 11 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(142.9f, 190.0f), module, MODULE::LIGHT_STEP + 12 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(146.9f, 169.7f), module, MODULE::LIGHT_STEP + 13 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(158.2f, 153.2f), module, MODULE::LIGHT_STEP + 14 * 3));
		addChild(createLightCentered<FlowerLight>(Vec(175.1f, 141.9f), module, MODULE::LIGHT_STEP + 15 * 3));

		addParam(createParamCentered<StoermelderSmallKnob>(Vec(195.0f, 107.2f), module, MODULE::PARAM_STEP + 0));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(230.2f, 104.9f), module, MODULE::PARAM_STEP + 1));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(253.6f, 131.4f), module, MODULE::PARAM_STEP + 2));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(280.1f, 154.9f), module, MODULE::PARAM_STEP + 3));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(277.8f, 190.0f), module, MODULE::PARAM_STEP + 4));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(280.1f, 225.2f), module, MODULE::PARAM_STEP + 5));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(253.6f, 248.6f), module, MODULE::PARAM_STEP + 6));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(230.2f, 275.1f), module, MODULE::PARAM_STEP + 7));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(195.0f, 272.8f), module, MODULE::PARAM_STEP + 8));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(159.8f, 275.1f), module, MODULE::PARAM_STEP + 9));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(136.4f, 248.6f), module, MODULE::PARAM_STEP + 10));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(109.9f, 225.2f), module, MODULE::PARAM_STEP + 11));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(112.2f, 190.0f), module, MODULE::PARAM_STEP + 12));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(109.9f, 154.9f), module, MODULE::PARAM_STEP + 13));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(136.4f, 131.4f), module, MODULE::PARAM_STEP + 14));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(159.8f, 104.9f), module, MODULE::PARAM_STEP + 15));

		addInput(createInputCentered<StoermelderPort>(Vec(195.0f, 66.2f), module, MODULE::INPUT_STEP + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(242.2f, 75.6f), module, MODULE::INPUT_STEP + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(282.6f, 102.4f), module, MODULE::INPUT_STEP + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(309.4f, 142.6f), module, MODULE::INPUT_STEP + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(318.8f, 190.0f), module, MODULE::INPUT_STEP + 4));
		addInput(createInputCentered<StoermelderPort>(Vec(309.4f, 237.4f), module, MODULE::INPUT_STEP + 5));
		addInput(createInputCentered<StoermelderPort>(Vec(282.6f, 277.6f), module, MODULE::INPUT_STEP + 6));
		addInput(createInputCentered<StoermelderPort>(Vec(242.2f, 304.4f), module, MODULE::INPUT_STEP + 7));
		addInput(createInputCentered<StoermelderPort>(Vec(195.0f, 313.8f), module, MODULE::INPUT_STEP + 8));
		addInput(createInputCentered<StoermelderPort>(Vec(147.6f, 304.4f), module, MODULE::INPUT_STEP + 9));
		addInput(createInputCentered<StoermelderPort>(Vec(107.4f, 277.6f), module, MODULE::INPUT_STEP + 10));
		addInput(createInputCentered<StoermelderPort>(Vec(80.6f, 237.4f), module, MODULE::INPUT_STEP + 11));
		addInput(createInputCentered<StoermelderPort>(Vec(71.2f, 190.0f), module, MODULE::INPUT_STEP + 12));
		addInput(createInputCentered<StoermelderPort>(Vec(80.6f, 142.6f), module, MODULE::INPUT_STEP + 13));
		addInput(createInputCentered<StoermelderPort>(Vec(107.4f, 102.4f), module, MODULE::INPUT_STEP + 14));
		addInput(createInputCentered<StoermelderPort>(Vec(147.6f, 75.6f), module, MODULE::INPUT_STEP + 15));

		// Phrases
		for (int i = 0; i < 8; i++) {
			addParam(createParamCentered<LEDButton>(Vec(23.8f, 73.3f + i * 30.f), module, MODULE::PARAM_PHRASE_SELECT + i));
			addChild(createLightCentered<FlowerLight>(Vec(23.8f, 73.3f + i * 30.f), module, MODULE::LIGHT_PHRASE_SELECT + i * 3));
		}
		addInput(createInputCentered<StoermelderPort>(Vec(23.8f, 325.8f), module, MODULE::INPUT_PHRASE));

		// Patterns
		for (int i = 0; i < 8; i++) {
			addParam(createParamCentered<LEDButton>(Vec(366.3f, 73.3f + i * 30.f), module, MODULE::PARAM_PATTERN_SELECT + i));
			addChild(createLightCentered<FlowerLight>(Vec(366.3f, 73.3f + i * 30.f), module, MODULE::LIGHT_PATTERN_SELECT + i * 3));

			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(357.1f, 63.7f + i * 30.f), module, MODULE::LIGHT_PATTERN + i * 4 + 0));
			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(363.2f, 60.5f + i * 30.f), module, MODULE::LIGHT_PATTERN + i * 4 + 1));
			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(369.3f, 60.5f + i * 30.f), module, MODULE::LIGHT_PATTERN + i * 4 + 2));
			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(375.4f, 63.7f + i * 30.f), module, MODULE::LIGHT_PATTERN + i * 4 + 3));
		}
		addInput(createInputCentered<StoermelderPort>(Vec(366.3f, 325.8f), module, MODULE::INPUT_MUTATE));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct PortableSequenceMenuItem : MenuItem {
			FlowerSeqWidget* mw;
			PortableSequenceMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct CopyItem : MenuItem {
					FlowerSeqWidget* mw;
					void onAction(const event::Action& e) override {
						mw->copyPortableSequence();
					}
				};
				struct PasteItem : MenuItem {
					FlowerSeqWidget* mw;
					void onAction(const event::Action& e) override {
						mw->pastePortableSequence();
					}
				};

				menu->addChild(construct<CopyItem>(&MenuItem::text, "Copy sequence", &CopyItem::mw, mw));
				menu->addChild(construct<PasteItem>(&MenuItem::text, "Paste sequence", &PasteItem::mw, mw));
				return menu;
			}
		}; // PortableSequenceMenuItem

		struct PhraseCvModeMenuItem : MenuItem {
			PhraseCvModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct PhraseCvModeItem : MenuItem {
				MODULE* module;
				PHRASE_CV_MODE phraseCvMode;
				void onAction(const event::Action& e) override {
					module->phraseCvMode = phraseCvMode;
				}
				void step() override {
					rightText = module->phraseCvMode == phraseCvMode ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<PhraseCvModeItem>(&MenuItem::text, "Off", &PhraseCvModeItem::module, module, &PhraseCvModeItem::phraseCvMode, PHRASE_CV_MODE::OFF));
				menu->addChild(construct<PhraseCvModeItem>(&MenuItem::text, "Trigger", &PhraseCvModeItem::module, module, &PhraseCvModeItem::phraseCvMode, PHRASE_CV_MODE::TRIG_FWD));
				menu->addChild(construct<PhraseCvModeItem>(&MenuItem::text, "0..10V", &PhraseCvModeItem::module, module, &PhraseCvModeItem::phraseCvMode, PHRASE_CV_MODE::VOLT));
				menu->addChild(construct<PhraseCvModeItem>(&MenuItem::text, "C4-G4", &PhraseCvModeItem::module, module, &PhraseCvModeItem::phraseCvMode, PHRASE_CV_MODE::C4));
				menu->addChild(construct<PhraseCvModeItem>(&MenuItem::text, "Arm", &PhraseCvModeItem::module, module, &PhraseCvModeItem::phraseCvMode, PHRASE_CV_MODE::ARM));
				return menu;
			}
		}; // PhraseCvModeMenuItem

		struct PhraseCountMenuItem : MenuItem {
			MODULE* module;
			PhraseCountMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct PhraseCountItem : MenuItem {
					MODULE* module;
					int count;
					void onAction(const event::Action& e) override {
						module->phraseSetCount(count);
					}
					void step() override {
						rightText = module->phraseCount == count ? "✔" : "";
						MenuItem::step();
					}
				};

				for (int i = 0; i < 8; i++) {
					menu->addChild(construct<PhraseCountItem>(&MenuItem::text, string::f("%2u", i + 1), &PhraseCountItem::module, module, &PhraseCountItem::count, i + 1));
				}

				return menu;
			}
		}; // PhraseCountMenuItem

		struct PatternCountMenuItem : MenuItem {
			MODULE* module;
			PatternCountMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct PatternCountItem : MenuItem {
					MODULE* module;
					int count;
					void onAction(const event::Action& e) override {
						module->patternSetCount(count);
					}
					void step() override {
						rightText = module->patternCount == count ? "✔" : "";
						MenuItem::step();
					}
				};

				for (int i = 0; i < 8; i++) {
					menu->addChild(construct<PatternCountItem>(&MenuItem::text, string::f("%2u", i + 1), &PatternCountItem::module, module, &PatternCountItem::count, i + 1));
				}

				return menu;
			}
		}; // PatternCountMenuItem

		struct PatternModeMenuItem : MenuItem {
			MODULE* module;
			PatternModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct PatternModeItem : MenuItem {
					MODULE* module;
					PATTERN_TYPE pm;
					void onAction(const event::Action& e) override {
						module->patternList.toggle(pm);
						if (!module->patternList.active(pm)) module->patternCheck();
					}
					void step() override {
						//rightText = module->patternTypeSet.find(pm) != module->patternTypeSet.end() ? "✔" : "";
						rightText = module->patternList.active(pm) ? "✔" : "";
						MenuItem::step();
					}
					Menu* createChildMenu() override {
						if (!module->patternList.active(pm)) return NULL;
						Menu* menu = new Menu;

						struct UpItem : MenuItem {
							MODULE* module;
							PATTERN_TYPE pm;
							void onAction(const event::Action& e) override {
								module->patternList.moveFwd(pm);
							}
						};
						struct DownItem : MenuItem {
							MODULE* module;
							PATTERN_TYPE pm;
							void onAction(const event::Action& e) override {
								module->patternList.moveBwd(pm);
							}
						};

						if (!module->patternList.isFirst(pm)) 
							menu->addChild(construct<UpItem>(&MenuItem::text, "Move up", &UpItem::module, module, &UpItem::pm, pm));
						if (!module->patternList.isLast(pm))
							menu->addChild(construct<DownItem>(&MenuItem::text, "Move down", &DownItem::module, module, &DownItem::pm, pm));
						return menu;
					}
				};

				struct PatternResetItem : MenuItem {
					MODULE* module;
					void onAction(const event::Action& e) override {
						module->patternList.reset();
					}
				};

				for (int i = 0; i < (int)PATTERN_TYPE::NUM; i++) {
					menu->addChild(construct<PatternModeItem>(&MenuItem::text, module->patternList.getNameAt(i), &PatternModeItem::module, module, &PatternModeItem::pm, module->patternList.at(i)));
				}
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<PatternResetItem>(&MenuItem::text, "Reset patterns", &PatternResetItem::module, module));

				/*
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[oxxx] Forward", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_FWD));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[xoxx] Reverse", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_REV));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[ooxx] Add 1V", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_ADD_1V));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[xxox] Inverse", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_INV));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[oxox] Add 2 steps", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::ADD_2STEPS));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[xoox] Add auxiliary sequence", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::AUX_ADD));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[ooox] Step probability 0.5", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_PROB_05));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[xxxo] Subtract auxiliary sequence", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::AUX_SUB));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[oxxo] Random", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_RAND));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[xoxo] Odd steps only", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_OOD));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[ooxo] Even steps only", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_EVEN));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[xxoo] Transpose", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::SEQ_TRANSPOSE));
				menu->addChild(construct<PatternModeItem>(&MenuItem::text, "[oxoo] Random auxiliary sequence", &PatternModeItem::module, module, &PatternModeItem::pm, PATTERN_TYPE::AUX_RAND));
				*/
				return menu;
			}
		}; // PatternModeMenuItem

		struct PatternMutateMenuItem : MenuItem {
			MODULE* module;
			PatternMutateMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct PatternMutateItem : MenuItem {
					MODULE* module;
					MUTATE_DISTRIBUTION d;
					void onAction(const event::Action& e) override {
						module->patternMutateDist = d;
					}
					void step() override {
						rightText = d == module->patternMutateDist ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<PatternMutateItem>(&MenuItem::text, "Uniform distribution (fixed pattern 1)", &PatternMutateItem::module, module, &PatternMutateItem::d, MUTATE_DISTRIBUTION::UNIFORM_FIXED_0));
				menu->addChild(construct<PatternMutateItem>(&MenuItem::text, "Uniform distribution", &PatternMutateItem::module, module, &PatternMutateItem::d, MUTATE_DISTRIBUTION::UNIFORM));
				menu->addChild(construct<PatternMutateItem>(&MenuItem::text, "Binomial distribution (fixed pattern 1)", &PatternMutateItem::module, module, &PatternMutateItem::d, MUTATE_DISTRIBUTION::BINOMIAL_FIXED_0));
				menu->addChild(construct<PatternMutateItem>(&MenuItem::text, "Binomial distribution", &PatternMutateItem::module, module, &PatternMutateItem::d, MUTATE_DISTRIBUTION::BINOMIAL));
				return menu;
			}
		}; // PatternMutateMenuItem

		struct StepCvModeMenuItem : MenuItem {
			MODULE* module;
			StepCvModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct StepCvModeItem : MenuItem {
					MODULE* module;
					SEQ_CV_MODE stepCvMode;
					void onAction(const event::Action& e) override {
						module->seq.stepCvMode = stepCvMode;
					}
					void step() override {
						rightText = module->seq.stepCvMode == stepCvMode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<StepCvModeItem>(&MenuItem::text, "Attenuate", &StepCvModeItem::module, module, &StepCvModeItem::stepCvMode, SEQ_CV_MODE::ATTENUATE));
				menu->addChild(construct<StepCvModeItem>(&MenuItem::text, "Sum", &StepCvModeItem::module, module, &StepCvModeItem::stepCvMode, SEQ_CV_MODE::SUM));
				return menu;
			}
		}; // StepCvModeMenuItem

		struct StepRandomizeMenuItem : MenuItem {
			MODULE* module;
			StepRandomizeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct StepRandomizeItem : MenuItem {
					MODULE* module;
					int idx;
					void onAction(const event::Action& e) override {
						module->randomizeFlags.flip(idx);
					}
					void step() override {
						rightText = module->randomizeFlags.test(idx) ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Steps"));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Value", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_VALUE));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Disabled", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_DISABLED));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Auxiliary value", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_AUX));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Probability", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_PROB));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Ratchets", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_RATCHETS));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Slew", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::STEP_SLEW));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Sequence"));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Start", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::SEQ_START));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Length", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::SEQ_LENGTH));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Pattern"));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Count", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::PATTERN_CNT));
				menu->addChild(construct<StepRandomizeItem>(&MenuItem::text, "Repeats", &StepRandomizeItem::module, module, &StepRandomizeItem::idx, FlowerProcessArgs::PATTERN_RPT));
				return menu;
			}
		}; // StepRandomizeMenuItem

		struct OutCvModeMenuItem : MenuItem {
			MODULE* module;
			OutCvModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct OutCvModeItem : MenuItem {
					MODULE* module;
					OUT_CV_MODE outCvMode;
					void onAction(const event::Action& e) override {
						module->seq.outCvMode = outCvMode;
					}
					void step() override {
						rightText = module->seq.outCvMode == outCvMode ? "✔" : "";
						MenuItem::step();
					}
				};

				struct OutCvClampItem : MenuItem {
					MODULE* module;
					void onAction(const event::Action& e) override {
						module->seq.outCvClamp ^= true;
					}
					void step() override {
						rightText = module->seq.outCvClamp ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "-10..10V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::BI_10V));
				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "-5..5V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::BI_5V));
				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "-1..1V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::BI_1V));
				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "0..10V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::UNI_10V));
				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "0..5V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::UNI_5V));
				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "0..3V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::UNI_3V));
				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "0..2V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::UNI_2V));
				menu->addChild(construct<OutCvModeItem>(&MenuItem::text, "0..1V", &OutCvModeItem::module, module, &OutCvModeItem::outCvMode, OUT_CV_MODE::UNI_1V));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<OutCvClampItem>(&MenuItem::text, "Clamp output", &OutCvClampItem::module, module));
				return menu;
			}
		}; // OutCvModeMenuItem

		struct OutAuxModeMenuItem : MenuItem {
			MODULE* module;
			OutAuxModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct OutAuxModeItem : MenuItem {
					MODULE* module;
					OUT_AUX_MODE outAuxMode;
					void onAction(const event::Action& e) override {
						module->seq.outAuxMode = outAuxMode;
					}
					void step() override {
						rightText = module->seq.outAuxMode == outAuxMode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<OutAuxModeItem>(&MenuItem::text, "Trigger", &OutAuxModeItem::module, module, &OutAuxModeItem::outAuxMode, OUT_AUX_MODE::TRIG));
				menu->addChild(construct<OutAuxModeItem>(&MenuItem::text, "Slewed trigger", &OutAuxModeItem::module, module, &OutAuxModeItem::outAuxMode, OUT_AUX_MODE::TRIG_SLEW));
				menu->addChild(construct<OutAuxModeItem>(&MenuItem::text, "Clock", &OutAuxModeItem::module, module, &OutAuxModeItem::outAuxMode, OUT_AUX_MODE::CLOCK));
				menu->addChild(construct<OutAuxModeItem>(&MenuItem::text, "Auxiliary sequence", &OutAuxModeItem::module, module, &OutAuxModeItem::outAuxMode, OUT_AUX_MODE::AUXILIARY));
				return menu;
			}
		}; // OutAuxModeMenuItem

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PortableSequenceMenuItem>(&MenuItem::text, "Portable sequence", &PortableSequenceMenuItem::mw, this));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PhraseCvModeMenuItem>(&MenuItem::text, "Phrase CV mode", &PhraseCvModeMenuItem::module, module));
		menu->addChild(construct<PhraseCountMenuItem>(&MenuItem::text, "Phrase count", &PhraseCountMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PatternModeMenuItem>(&MenuItem::text, "Patterns", &PatternModeMenuItem::module, module));
		menu->addChild(construct<PatternCountMenuItem>(&MenuItem::text, "Pattern count", &PatternCountMenuItem::module, module));
		menu->addChild(construct<PatternMutateMenuItem>(&MenuItem::text, "Pattern mutate", &PatternMutateMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<StepCvModeMenuItem>(&MenuItem::text, "Step CV knob mode", &StepCvModeMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<StepRandomizeMenuItem>(&MenuItem::text, "RAND-port targets", &StepRandomizeMenuItem::module, module));
		menu->addChild(construct<OutCvModeMenuItem>(&MenuItem::text, "CV-port range", &OutCvModeMenuItem::module, module));
		menu->addChild(construct<OutAuxModeMenuItem>(&MenuItem::text, "OUT-port mode", &OutAuxModeMenuItem::module, module));
	}

	void copyPortableSequence() {
		int start = module->stepGetSeqStart();
		int length = module->stepGetSeqLength();

		json_t* sequenceJ = json_object();
		json_object_set_new(sequenceJ, "length", json_real((float)length));

		json_t* notesJ = json_array();
		for (int i = 0; i < length; i++) {
			int i_ = (start + i) % 16;
			if (!module->seq.stepGet(i_)->disabled) {
				json_t* noteJ = json_object();
				json_object_set_new(noteJ, "type", json_string("note"));
				json_object_set_new(noteJ, "start", json_real((float)i));
				json_object_set_new(noteJ, "length", json_real(0.5f));
				json_object_set_new(noteJ, "pitch", json_real(module->seq.stepGetValueScaled(i_, false)));
				json_object_set_new(noteJ, "playProbability", json_real(module->seq.stepGet(i_)->probability));
				json_array_append_new(notesJ, noteJ);
			}
		}
		json_object_set_new(sequenceJ, "notes", notesJ);

		// Copy to clipboard
		json_t* clipboardJ = json_object();
		json_object_set_new(clipboardJ, "vcvrack-sequence", sequenceJ);
		DEFER({
			json_decref(clipboardJ);
		});
		char* clipboard = json_dumps(clipboardJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		DEFER({
			free(clipboard);
		});
		glfwSetClipboardString(APP->window->win, clipboard);
	}

	void pastePortableSequence() {
		const char* clipboard = glfwGetClipboardString(APP->window->win);
		if (!clipboard) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not get text from clipboard.");
			return;
		}

		json_error_t error;
		json_t* clipboardJ = json_loads(clipboard, 0, &error);
		if (!clipboardJ) {
			std::string message = string::f("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(clipboardJ);
		});

		json_t* sequenceJ = json_object_get(clipboardJ, "vcvrack-sequence");
		if (!sequenceJ) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not get sequence from clipboard.");
			return;
		}

		json_t* lengthJ = json_object_get(sequenceJ, "length");
		if (!lengthJ) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Wrong format of sequence in clipboard.");
			return;
		}

		json_t* notesJ = json_object_get(sequenceJ, "notes");
		if (!notesJ || !json_is_array(notesJ)) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Wrong format of sequence in clipboard.");
			return;
		}

		int start = module->stepGetSeqStart();
		int length = std::min((int)json_array_size(notesJ), 16);

		for (int i = 0; i < length; i++) {
			int i_ = (start + i) % 16;

			json_t* noteJ = json_array_get(notesJ, i);
			if (!noteJ) continue;

			json_t* pitchJ = json_object_get(noteJ, "pitch");
			if (!pitchJ) continue;
			module->seq.stepSetValue(i_, json_real_value(pitchJ));

			json_t* probJ = json_object_get(noteJ, "playProbability");
			module->seq.stepGet(i_)->probability = probJ ? json_real_value(probJ) : 1.0f;

			module->seq.stepGet(i_)->ratchets = 1;
			module->seq.stepGet(i_)->disabled = false;
		}

		module->seqSetLength(length);
	}
};

} // namespace Flower

Model* modelFlowerSeq = createModel<Flower::FlowerSeqModule<16, 8, 8>, Flower::FlowerSeqWidget>("FlowerSeq");