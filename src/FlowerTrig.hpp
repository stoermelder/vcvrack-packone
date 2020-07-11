#include "plugin.hpp"
#include "digital.hpp"
#include "Flower.hpp"
#include <bitset>
#include <random>

namespace Flower {

enum class TRIG_UI_STATE {
	DEFAULT,
	PROBABILITY,
	RATCHETS,
	ATTACK,
	DECAY,
	NUM_MODES
};


template< typename MODULE, int STEPS >
struct TrigStepButtonParamQuantity : ParamQuantity {
	MODULE* module;
	int i;
	std::string getDisplayValueString() override {
		std::string s;
		switch (module->seq.stepState) {
			default:
			case TRIG_UI_STATE::DEFAULT:
				return string::f("Step %i: %s\nProbability: %4.3f\nRatchets: %i\nAttack: %4.3f\nDecay: %4.3f",
					i + 1, module->seq.stepGet(i)->disabled ? "Off" : "On", module->seq.stepGet(i)->probability, module->seq.stepGet(i)->ratchets, module->seq.stepGet(i)->attack, module->seq.stepGet(i)->decay);
			case TRIG_UI_STATE::PROBABILITY:
				return string::f("Step %i probability: %4.3f\nShort press: select step %i\nLong press: set probability value %4.3f",
					i + 1, module->seq.stepGet(i)->probability, i + 1, float(i) / (STEPS - 1));
			case TRIG_UI_STATE::RATCHETS:
				s = string::f("\nLong press: set ratchets %i", i + 1);
				return string::f("Step %i ratchets: %i\nShort press: select step %i",
					i + 1, module->seq.stepGet(i)->ratchets, i + 1) + (i < 8 ? s : "");
			case TRIG_UI_STATE::ATTACK:
				return string::f("Step %i attack: %4.3f\nShort press: select step %i\nLong press: set attack value %4.3f",
					i + 1, module->seq.stepGet(i)->attack, i + 1, float(i) / (STEPS - 1));
			case TRIG_UI_STATE::DECAY:
				return string::f("Step %i decay: %4.3f\nShort press: select step %i\nLong press: set decay value %4.3f",
					i + 1, module->seq.stepGet(i)->decay, i + 1, float(i) / (STEPS - 1));
		}
		return "";
	}
	std::string getLabel() override {
		return "";
	}
}; // SeqStepButtonParamQuantity


template< typename MODULE >
struct TrigStepModeParamQuantity : ParamQuantity {
	MODULE* module;
	std::string getDisplayValueString() override {
		switch (module->seq.stepState) {
			default:
			case TRIG_UI_STATE::DEFAULT: return "Edit step on/off";
			case TRIG_UI_STATE::PROBABILITY: return "Edit step probability value";
			case TRIG_UI_STATE::RATCHETS: return "Edit step ratchets";
			case TRIG_UI_STATE::ATTACK: return "Edit step attack";
			case TRIG_UI_STATE::DECAY: return "Edit step decay";
		}
		return "";
	}
}; // SeqStepModeParamQuantity


template< typename MODULE >
struct TrigFlowerKnobParamQuantity : ParamQuantity {
	MODULE* module;
	std::string getDisplayValueString() override {
		int i = module->seq.stepEditSelected;
		switch (module->seq.stepState) {
			default:
			case TRIG_UI_STATE::DEFAULT:
				return "SEEDS control (use EDIT-button)";
			case TRIG_UI_STATE::PROBABILITY:
				return string::f("%4.3f", module->seq.stepGet(i)->probability);
			case TRIG_UI_STATE::RATCHETS:
				return string::f("%i", module->seq.stepGet(i)->ratchets);
			case TRIG_UI_STATE::ATTACK:
				return string::f("%4.3f", module->seq.stepGet(i)->attack);
			case TRIG_UI_STATE::DECAY:
				return string::f("%4.3f", module->seq.stepGet(i)->decay);
		}
		return "";
	}
	std::string getLabel() override {
		int i = module->seq.stepEditSelected;
		switch (module->seq.stepState) {
			default:
			case TRIG_UI_STATE::DEFAULT:
				return "";
			case TRIG_UI_STATE::PROBABILITY:
				return string::f("Step %i probability", i + 1);
			case TRIG_UI_STATE::RATCHETS:
				return string::f("Step %i ratchets", i + 1);
			case TRIG_UI_STATE::ATTACK:
				return string::f("Step %i attack", i + 1);
			case TRIG_UI_STATE::DECAY:
				return string::f("Step %i decay", i + 1);
		}
	}
}; // SeqFlowerKnobParamQuantity


template < typename MODULE, int STEPS >
struct FlowerTrig {
	MODULE* m;

	struct FlowerTrigStep {
		bool disabled;
		float probability;
		int ratchets;
		float attack;
		float decay;
	};

	/** currently acitve step after pattern-transform */ 
	int stepOutIndex;
	/** [Stored to JSON] the steps */
	FlowerTrigStep steps[STEPS];
	
	/** currently selected ui-state for step-buttons/leds */
	TRIG_UI_STATE stepState;
	/** currently selected step if one of the edit-modes is active */
	int stepEditSelected;
	/** helper-variable for led-blinking */
	bool stepBlink = false;
	/** currently selected random step */
	uint32_t stepRandomIndex;
	/** random probability for the currently selected step */
	float stepRandomProbability;
	/** helper-variable for center-param delta-calculation */
	float stepCenterValue;
	/** random distribution used for randomization */
	std::geometric_distribution<int> stepGeoDist{0.65};


	float editLightBrightness = 0.f;
	int editLightAdd = 1;

	dsp::SchmittTrigger randTrigger;
	ClockMultiplier clockMultiplier;

	LongPressButton stepButtons[STEPS];
	dsp::BooleanTrigger stepModeTrigger;
	StoermelderSlewLimiter stepGateEnv;
	StoermelderSlewLimiter stepGateDecay;
	StoermelderSlewLimiter stepEnv[STEPS];
	StoermelderSlewLimiter stepDecay[STEPS];

	dsp::PulseGenerator gatePulseGenerator;
	dsp::PulseGenerator trigPulseGenerator;

	dsp::ClockDivider paramDivider;
	dsp::ClockDivider lightDivider;
	dsp::ClockDivider lightBlinkDivider;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};

	FlowerTrig(MODULE* module) {
		m = module;
		paramDivider.setDivision(32);
		lightDivider.setDivision(512);
		reset();
	}

	void reset() {
		clockMultiplier.reset();

		// Steps
		stepState = TRIG_UI_STATE::DEFAULT;
		stepEditSelected = -1;
		stepCenterValue = 0.f;
		stepRandomIndex = 0;
		stepGateDecay.setShape(0.975f);

		for (int i = 0; i < STEPS; i++) {
			stepGet(i)->disabled = false;
			stepGet(i)->probability = 1.f;
			stepGet(i)->ratchets = 1;
			stepGet(i)->attack = 0.f;
			stepGet(i)->decay = 0.f;
			stepDecay[i].setShape(0.975f);
		}

		for (int i = 0; i < STEPS; i++) {
			stepButtons[i].param = &m->params[MODULE::PARAM_STEP_BUTTON + i];
		}
	}

	void process(const FlowerProcessArgs& args) {
		bool clockPulse = false;

		// CLOCK-input
		if (args.running && args.clockTick) {
			clockPulse = true;
			clockMultiplier.tick();
		}

		// SEQ_RAND-input / SEQ_RAND-button
		if (randTrigger.process(m->inputs[MODULE::INPUT_RAND].getVoltage() + m->params[MODULE::PARAM_RAND].getValue())) {
			doRandomize(args.randomizeFlagsSlave);
		}
		// SEQ_RAND-input / SEQ_RAND-button from master-module
		if (args.randTick) {
			doRandomize(args.randomizeFlagsMaster);
		}

		if (args.stepTick) {
			// Random variables for the next step
			stepRandomIndex = random::u32();
			stepRandomProbability = random::uniform();
		}

		if (args.patternTick) {
			
		}

		if (paramDivider.process()) {
			// StepMode-button
			if (stepModeTrigger.process(m->params[MODULE::PARAM_STEPMODE].getValue())) {
				stepState = (TRIG_UI_STATE)(((int)stepState + 1) % (int)TRIG_UI_STATE::NUM_MODES);
				stepEditSelected = 0;
			}

			// Step-buttons
			for (int i = 0; i < STEPS; i++) {
				switch (stepButtons[i].process(args.sampleTime, 0.8f / float(paramDivider.getDivision()))) {
					default:
					case LongPressButton::NO_PRESS:
						break;
					case LongPressButton::SHORT_PRESS:
						stepEditSelected = i;
						stepBlink = true;
						lightBlinkDivider.reset();
						break;
					case LongPressButton::LONG_PRESS:
						if (stepState == TRIG_UI_STATE::DEFAULT) {
							stepGet(i)->disabled ^= true;
						}
						if (stepState == TRIG_UI_STATE::PROBABILITY) {
							if (stepEditSelected >= 0) stepGet(stepEditSelected)->probability = float(i) / float(STEPS - 1);
						}
						if (stepState == TRIG_UI_STATE::RATCHETS) {
							if (stepEditSelected >= 0 && i < 8) stepGet(stepEditSelected)->ratchets = i + 1;
						}
						if (stepState == TRIG_UI_STATE::ATTACK) {
							if (stepEditSelected >= 0) stepGet(stepEditSelected)->attack = float(i) / float(STEPS - 1);
						}
						if (stepState == TRIG_UI_STATE::DECAY) {
							if (stepEditSelected >= 0) stepGet(stepEditSelected)->decay = float(i) / float(STEPS - 1);
						}
						break;
				}
			}

			// FlowerKnob-param
			if (m->params[MODULE::PARAM_STEP_CENTER].getValue() != stepCenterValue) {
				float v = m->params[MODULE::PARAM_STEP_CENTER].getValue();
				float delta =  v - stepCenterValue;

				if (stepState == TRIG_UI_STATE::PROBABILITY) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->probability = clamp(stepGet(stepEditSelected)->probability + delta / 10.f, 0.f, 1.f);
					stepCenterValue = v;
				}
				if (stepState == TRIG_UI_STATE::RATCHETS && std::abs(delta) > 0.5f) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->ratchets = (int)clamp(std::round(stepGet(stepEditSelected)->ratchets + delta * 2.f), 0.f, 8.f);
					stepCenterValue = v;
				}
				if (stepState == TRIG_UI_STATE::ATTACK) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->attack = clamp(stepGet(stepEditSelected)->attack + delta / 10.f, 0.f, 1.f);
					stepCenterValue = v;
				}
				if (stepState == TRIG_UI_STATE::DECAY) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->decay = clamp(stepGet(stepEditSelected)->decay + delta / 10.f, 0.f, 1.f);
					stepCenterValue = v;
				}
			}
		}

		processOutput(args, clockPulse);
		if (lightDivider.process()) processLights(args);
	}

	void processOutput(const FlowerProcessArgs& args, bool clockPulse) {
		float v = 0.f;
		float stepProbability = 1.f;
		bool stepEnabled = true;

		switch (args.patternType) {
			default:
			case PATTERN_TYPE::SEQ_FWD:
			case PATTERN_TYPE::SEQ_ADD_1V:
			case PATTERN_TYPE::SEQ_INV:
			case PATTERN_TYPE::AUX_ADD:
			case PATTERN_TYPE::AUX_SUB:
			case PATTERN_TYPE::SEQ_TRANSPOSE:
			case PATTERN_TYPE::AUX_RAND: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValue(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::SEQ_REV: {
				stepOutIndex = (args.stepStart + args.stepLength - args.stepIndex - 1) % STEPS;
				v = stepGetValue(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::ADD_2STEPS: {
				stepOutIndex = (args.stepStart + ((args.stepIndex + 2) % args.stepLength)) % STEPS;
				v = stepGetValue(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::SEQ_PROB_05: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValue(stepOutIndex);
				stepProbability = 0.5f;
				break;
			}
			case PATTERN_TYPE::SEQ_RAND: {
				stepOutIndex = (args.stepStart + (stepRandomIndex % args.stepLength)) % STEPS;
				v = stepGetValue(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::SEQ_OOD: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				if (stepOutIndex % 2 == 0)
					v = stepGetValue(stepOutIndex);
				else
					stepEnabled = false;
				break;
			}
			case PATTERN_TYPE::SEQ_EVEN: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				if (stepOutIndex % 2 == 1)
					v = stepGetValue(stepOutIndex);
				else
					stepEnabled = false;
				break;
			}
		}

		stepEnabled = stepEnabled && !stepGet(stepOutIndex)->disabled;
		stepProbability *= stepGet(stepOutIndex)->probability;
		if (stepEnabled && stepProbability < 1.f) {
			stepEnabled = stepRandomProbability <= stepProbability;
		}

		if (stepEnabled && clockPulse) {
			clockMultiplier.trigger(stepGet(stepOutIndex)->ratchets);
		}
		if (clockMultiplier.process()) {
			gatePulseGenerator.trigger(v);
			trigPulseGenerator.trigger(2e-2f);
		}

		if (args.running) {
			float t = 0.f;
			if (stepEnabled) {
				stepGateEnv.setRise(stepGet(stepOutIndex)->attack);
				stepGateEnv.setFall(stepGet(stepOutIndex)->decay);
				stepEnv[stepOutIndex].setRise(stepGet(stepOutIndex)->attack);
				stepEnv[stepOutIndex].setFall(stepGet(stepOutIndex)->decay);
				t = gatePulseGenerator.process(args.sampleTime) ? 10.f : 0.f;
			}

			float d;
			d = stepGateEnv.process(t, args.sampleTime);
			m->outputs[MODULE::OUTPUT_GATE].setVoltage(d);

			for (int i = 0; i < STEPS; i++) {
				d = i == stepOutIndex ? t : 0.f;
				d = stepEnv[i].process(d, args.sampleTime);
				m->outputs[MODULE::OUTPUT_STEP + i].setVoltage(d);
			}
		}
		if (stepEnabled && args.running) {
			m->outputs[MODULE::OUTPUT_TRIG].setVoltage(trigPulseGenerator.process(args.sampleTime) ? 10.f : 0.f);
		}
		else {
			m->outputs[MODULE::OUTPUT_TRIG].setVoltage(0.f);
		}
	}

	inline float stepGetValue(int index) {
		float v = m->params[MODULE::PARAM_STEP + index].getValue();
		return v;
	}

	inline FlowerTrigStep* stepGet(int idx) {
		return &steps[idx];
	}

	inline void stepSetValue(int index, float value) {
		m->params[MODULE::PARAM_STEP + index].setValue(value);
	}

	void doRandomize(const FlowerProcessArgs::RandomizeFlags& flags) {
		for (int i = 0; i < STEPS; i++) {
			if (flags.test(FlowerProcessArgs::STEP_VALUE)) 
				stepSetValue(i, random::uniform());
			if (flags.test(FlowerProcessArgs::STEP_DISABLED)) 
				stepGet(i)->disabled = random::uniform() > 0.5f;
			if (flags.test(FlowerProcessArgs::STEP_PROB)) 
				stepGet(i)->probability = random::uniform();
			if (flags.test(FlowerProcessArgs::STEP_RATCHETS)) 
				stepGet(i)->ratchets = 1 + std::min(7, stepGeoDist(randGen));
			if (flags.test(FlowerProcessArgs::STEP_ATTACK)) 
				stepGet(i)->attack = stepGeoDist(randGen) / ((stepGeoDist(randGen) + 1) * (7.f + 3.f * random::uniform()));
			if (flags.test(FlowerProcessArgs::STEP_DECAY)) 
				stepGet(i)->decay = stepGeoDist(randGen) / ((stepGeoDist(randGen) + 1) * (7.f + 3.f * random::uniform()));
		}
	}

	void processLights(const FlowerProcessArgs& args) {
		float st = args.sampleTime * lightDivider.getDivision();

		lightBlinkDivider.setDivision(args.sampleRate / lightDivider.getDivision() / 6);
		if (lightBlinkDivider.process()) {
			stepBlink ^= true;
			if (editLightBrightness == 0.9f) editLightAdd = -1;
			if (editLightBrightness == 0.f) editLightAdd = 1;
			editLightBrightness = clamp(editLightBrightness + editLightAdd * 0.02f, 0.f, 0.9f);
		}

		for (int i = 0; i < STEPS; i++) {
			float r = (stepState == TRIG_UI_STATE::PROBABILITY) + (stepState == TRIG_UI_STATE::RATCHETS) + (stepState == TRIG_UI_STATE::DECAY);
			float g = (stepState == TRIG_UI_STATE::PROBABILITY) + (stepState == TRIG_UI_STATE::DECAY ? 0.55f : 0.f);
			float b = (stepState == TRIG_UI_STATE::DEFAULT ? editLightBrightness : 0.f);
			m->lights[MODULE::LIGHT_EDIT + i * 3 + 0].setBrightness(r);
			m->lights[MODULE::LIGHT_EDIT + i * 3 + 1].setBrightness(g);
			m->lights[MODULE::LIGHT_EDIT + i * 3 + 2].setSmoothBrightness(b, st);
		}

		// Step lights
		if (stepState == TRIG_UI_STATE::DEFAULT) {
			int start = args.stepStart;
			int length = args.stepLength;
			for (int i = 0; i < STEPS; i++) {
				bool a = (i >= start && i < start + length) || (i + STEPS >= start && i + STEPS < start + length);
				float r = stepOutIndex == i ? 1.f : 0.f + stepGet(i)->disabled;
				float g = stepOutIndex == i ? 1.f : 0.f;
				float b = stepOutIndex == i ? 1.f : ((a && !stepGet(i)->disabled) ? 0.7f : 0.f);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setSmoothBrightness(r, st);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setSmoothBrightness(g, st);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setSmoothBrightness(b, st);
			}
		}
		if (stepState == TRIG_UI_STATE::PROBABILITY) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->probability >= float(i) / float(STEPS);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
		if (stepState == TRIG_UI_STATE::RATCHETS) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->ratchets > i;
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i));
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
		if (stepState == TRIG_UI_STATE::ATTACK) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->attack >= float(i) / float(STEPS);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i) + (b ? 0.55f : 0.f));
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
		if (stepState == TRIG_UI_STATE::DECAY) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->decay >= float(i) / float(STEPS);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i) + (b ? 0.55f : 0.f));
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
	}

	void dataToJson(json_t* rootJ) {
		// Steps
		json_t* stepsJ = json_array();
		for (int i = 0; i < STEPS; i++) {
			json_t* stepJ = json_object();
			json_object_set_new(stepJ, "disabled", json_boolean(stepGet(i)->disabled));
			json_object_set_new(stepJ, "probability", json_real(stepGet(i)->probability));
			json_object_set_new(stepJ, "ratchets", json_integer(stepGet(i)->ratchets));
			json_object_set_new(stepJ, "attack", json_real(stepGet(i)->attack));
			json_object_set_new(stepJ, "decay", json_real(stepGet(i)->decay));
			json_array_append_new(stepsJ, stepJ);
		}
		json_object_set_new(rootJ, "step", stepsJ);
	}

	void dataFromJson(json_t* rootJ) {
		// Steps
		stepState = TRIG_UI_STATE::DEFAULT;
		json_t* stepsJ = json_object_get(rootJ, "step");
		for (int i = 0; i < STEPS; i++) {
			json_t* stepJ = json_array_get(stepsJ, i);
			stepGet(i)->disabled = json_boolean_value(json_object_get(stepJ, "disabled"));
			stepGet(i)->probability = json_real_value(json_object_get(stepJ, "probability"));
			stepGet(i)->ratchets = json_integer_value(json_object_get(stepJ, "ratchets"));
			stepGet(i)->attack = json_real_value(json_object_get(stepJ, "attack"));
			stepGet(i)->decay = json_real_value(json_object_get(stepJ, "decay"));
		}
	}
};

} // namespace Flower