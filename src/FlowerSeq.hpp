#include "plugin.hpp"
#include "digital.hpp"
#include "Flower.hpp"
#include <bitset>
#include <random>

namespace StoermelderPackOne {
namespace Flower {

enum class SEQ_UI_STATE {
	DEFAULT,
	PROBABILITY,
	RATCHETS,
	SLEW,
	AUXILIARY,
	NUM_MODES
};


template< typename MODULE >
struct SeqStepParamQuantity : ParamQuantity {
	MODULE* mymodule;
	int i;
	float getDisplayValue() override {
		if (mymodule->seq.stepCvMode == SEQ_CV_MODE::ATTENUATE && mymodule->inputs[mymodule->INPUT_STEP + i].isConnected()) {
			switch (mymodule->seq.outCvMode) {
				case OUT_CV_MODE::BI_10V:
				case OUT_CV_MODE::BI_5V:
				case OUT_CV_MODE::BI_1V: return getSmoothValue() * 2.f - 1.f;
				case OUT_CV_MODE::UNI_10V:
				case OUT_CV_MODE::UNI_5V:
				case OUT_CV_MODE::UNI_3V:
				case OUT_CV_MODE::UNI_2V:
				case OUT_CV_MODE::UNI_1V:
				default: return getSmoothValue();
			}
		}
		else {
			switch (mymodule->seq.outCvMode) {
				case OUT_CV_MODE::BI_10V: return getSmoothValue() * 20.f - 10.f;
				case OUT_CV_MODE::BI_5V: return getSmoothValue() * 10.f - 5.f;
				case OUT_CV_MODE::BI_1V: return getSmoothValue() * 2.f - 1.f;
				case OUT_CV_MODE::UNI_10V: return getSmoothValue() * 10.f;
				case OUT_CV_MODE::UNI_5V: return getSmoothValue() * 5.f;
				case OUT_CV_MODE::UNI_3V: return getSmoothValue() * 3.f;
				case OUT_CV_MODE::UNI_2V: return getSmoothValue() * 2.f;
				case OUT_CV_MODE::UNI_1V: 
				default: return getSmoothValue();
			}
		}
	}
	void setDisplayValue(float displayValue) override {
		switch (mymodule->seq.outCvMode) {
			case OUT_CV_MODE::BI_10V: setValue((displayValue + 10.f) / 20.f); break;
			case OUT_CV_MODE::BI_5V: setValue((displayValue + 5.f) / 10.f); break;
			case OUT_CV_MODE::BI_1V: setValue((displayValue + 1.f) / 2.f); break;
			case OUT_CV_MODE::UNI_10V: setValue(displayValue / 10.f); break;
			case OUT_CV_MODE::UNI_5V: setValue(displayValue / 5.f); break;
			case OUT_CV_MODE::UNI_3V: setValue(displayValue / 3.f); break;
			case OUT_CV_MODE::UNI_2V: setValue(displayValue / 2.f); break;
			case OUT_CV_MODE::UNI_1V: setValue(displayValue); break;
		}
	}
	std::string getUnit() override {
		return mymodule->inputs[mymodule->INPUT_STEP + i].isConnected() && mymodule->seq.stepCvMode == SEQ_CV_MODE::ATTENUATE ? "x attenuate" : "V";
	}
}; // SeqStepParamQuantity

template< typename MODULE, int STEPS >
struct SeqStepButtonParamQuantity : ParamQuantity {
	MODULE* mymodule;
	int i;
	std::string getDisplayValueString() override {
		std::string s;
		switch (mymodule->seq.stepState) {
			default:
			case SEQ_UI_STATE::DEFAULT:
				return string::f("Step %i: %s\nAuxiliary voltage: %4.3fV\nProbability: %4.3f\nRatchets: %i\nSlew: %4.3f",
					i + 1, mymodule->seq.stepGet(i)->disabled ? "Off" : "On", mymodule->seq.stepGet(i)->auxiliary, mymodule->seq.stepGet(i)->probability, mymodule->seq.stepGet(i)->ratchets, mymodule->seq.stepGet(i)->slew);
			case SEQ_UI_STATE::AUXILIARY:
				return string::f("Step %i auxiliary voltage: %4.3fV\nShort press: select step %i\nLong press: set auxiliary voltage %4.3fV",
					i + 1, mymodule->seq.stepGet(i)->auxiliary, i + 1, float(i) / (STEPS - 1));
			case SEQ_UI_STATE::PROBABILITY:
				return string::f("Step %i probability: %4.3f\nShort press: select step %i\nLong press: set probability value %4.3f",
					i + 1, mymodule->seq.stepGet(i)->probability, i + 1, float(i) / (STEPS - 1));
			case SEQ_UI_STATE::RATCHETS:
				s = string::f("\nLong press: set ratchets %i", i + 1);
				return string::f("Step %i ratchets: %i\nShort press: select step %i",
					i + 1, mymodule->seq.stepGet(i)->ratchets, i + 1) + (i < 8 ? s : "");
			case SEQ_UI_STATE::SLEW:
				return string::f("Step %i slew: %4.3f\nShort press: select step %i\nLong press: set slew value %4.3f",
					i + 1, mymodule->seq.stepGet(i)->slew, i + 1, float(i) / (STEPS - 1));
		}
		return "";
	}
	std::string getLabel() override {
		return "";
	}
}; // SeqStepButtonParamQuantity

template< typename MODULE >
struct SeqStepModeParamQuantity : ParamQuantity {
	MODULE* mymodule;
	std::string getDisplayValueString() override {
		switch (mymodule->seq.stepState) {
			default:
			case SEQ_UI_STATE::DEFAULT: return "Edit step on/off";
			case SEQ_UI_STATE::AUXILIARY: return "Edit step auxiliary sequence";
			case SEQ_UI_STATE::PROBABILITY: return "Edit step probability value";
			case SEQ_UI_STATE::RATCHETS: return "Edit step ratchets";
			case SEQ_UI_STATE::SLEW: return "Edit step slew";
		}
		return "";
	}
}; // SeqStepModeParamQuantity

template< typename MODULE >
struct SeqFlowerKnobParamQuantity : ParamQuantity {
	MODULE* mymodule;
	std::string getDisplayValueString() override {
		int i = mymodule->seq.stepEditSelected;
		switch (mymodule->seq.stepState) {
			default:
			case SEQ_UI_STATE::DEFAULT:
				return "FLOWER control (use EDIT-button)";
			case SEQ_UI_STATE::AUXILIARY:
				return string::f("%4.3fV", mymodule->seq.stepGet(i)->auxiliary);
			case SEQ_UI_STATE::PROBABILITY:
				return string::f("%4.3f", mymodule->seq.stepGet(i)->probability);
			case SEQ_UI_STATE::RATCHETS:
				return string::f("%i", mymodule->seq.stepGet(i)->ratchets);
			case SEQ_UI_STATE::SLEW:
				return string::f("%4.3f", mymodule->seq.stepGet(i)->slew);
		}
		return "";
	}
	std::string getLabel() override {
		int i = mymodule->seq.stepEditSelected;
		switch (mymodule->seq.stepState) {
			default:
			case SEQ_UI_STATE::DEFAULT:
				return "";
			case SEQ_UI_STATE::AUXILIARY:
				return string::f("Step %i auxiliary voltage", i + 1);
			case SEQ_UI_STATE::PROBABILITY:
				return string::f("Step %i probability", i + 1);
			case SEQ_UI_STATE::RATCHETS:
				return string::f("Step %i ratchets", i + 1);
			case SEQ_UI_STATE::SLEW:
				return string::f("Step %i slew", i + 1);
		}
	}
}; // SeqFlowerKnobParamQuantity


template < typename MODULE, int STEPS >
struct FlowerSeq {
	MODULE* m;

    struct FlowerSeqStep {
        bool disabled;
        float auxiliary;
        float probability;
        int ratchets;
        float slew;
    };

	/** [Stored to Json] */
	OUT_CV_MODE outCvMode;
	/** [Stored to Json] */
	OUT_AUX_MODE outAuxMode;
	/** [Stored to Json] indicated if the CV-port should be clamped to the selected output range */
	bool outCvClamp;


	/** currently acitve step after pattern-transform */ 
	int stepOutIndex;
	/** [Stored to JSON] the steps */
	FlowerSeqStep steps[STEPS];
	/** [Stored to JSON] */
	SEQ_CV_MODE stepCvMode;

	/** currently selected ui-state for step-buttons/leds */
	SEQ_UI_STATE stepState;
	/** currently selected step if one of the edit-modes is active */
	int stepEditSelected;
	/** helper-variable for led-blinking */
	bool stepBlink = false;
	/** currently selected random step */
	uint32_t stepRandomIndex;
	/** random transpose of the sequence with range -1V..+1V */
	float stepRandomSeqTranpose;
	/** random probability for the currently selected step */
	float stepRandomProbability;
	/** */
	uint32_t stepRandomSeqAuxiliary;
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
	StoermelderSlewLimiter stepSlew;
	StoermelderSlewLimiter stepSlewTrigger;

	dsp::PulseGenerator trigPulseGenerator;

	dsp::ClockDivider paramDivider;
	dsp::ClockDivider lightDivider;
	dsp::ClockDivider lightBlinkDivider;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};

	FlowerSeq(MODULE* module) {
		m = module;
		paramDivider.setDivision(32);
		lightDivider.setDivision(512);
		reset();
	}

	void reset() {
		outCvMode = OUT_CV_MODE::UNI_3V;
		outAuxMode = OUT_AUX_MODE::TRIG;
		outCvClamp = false;
		clockMultiplier.reset();

		// Steps
		stepState = SEQ_UI_STATE::DEFAULT;
		stepEditSelected = -1;
		stepCenterValue = 0.f;
		stepCvMode = SEQ_CV_MODE::ATTENUATE;
		stepRandomIndex = 0;
		stepRandomSeqTranpose = 0.f;
		stepRandomSeqAuxiliary = 0;
		stepRandomProbability = 0.5f;
		stepSlew.setShape(0.975f);
		stepSlewTrigger.setShape(0.975f);

		for (int i = 0; i < STEPS; i++) {
			stepGet(i)->disabled = false;
			stepGet(i)->auxiliary = random::uniform();
			stepGet(i)->probability = 1.f;
			stepGet(i)->ratchets = 1;
			stepGet(i)->slew = 0.f;
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
			// Random variables for the next pattern
			stepRandomSeqTranpose = random::uniform() * 2.f - 1.f;
			stepRandomSeqAuxiliary = random::u32();
		}

		if (paramDivider.process()) {
			// StepMode-button
			if (stepModeTrigger.process(m->params[MODULE::PARAM_STEPMODE].getValue())) {
				stepState = (SEQ_UI_STATE)(((int)stepState + 1) % (int)SEQ_UI_STATE::NUM_MODES);
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
						if (stepState == SEQ_UI_STATE::DEFAULT) {
							stepGet(i)->disabled ^= true;
						}
						if (stepState == SEQ_UI_STATE::AUXILIARY) {
							if (stepEditSelected >= 0) stepGet(stepEditSelected)->auxiliary = float(i) / float(STEPS - 1);
						}
						if (stepState == SEQ_UI_STATE::PROBABILITY) {
							if (stepEditSelected >= 0) stepGet(stepEditSelected)->probability = float(i) / float(STEPS - 1);
						}
						if (stepState == SEQ_UI_STATE::RATCHETS) {
							if (stepEditSelected >= 0 && i < 8) stepGet(stepEditSelected)->ratchets = i + 1;
						}
						if (stepState == SEQ_UI_STATE::SLEW) {
							if (stepEditSelected >= 0) stepGet(stepEditSelected)->slew = float(i) / float(STEPS - 1);
						}
						break;
				}
			}

			// FlowerKnob-param
			if (m->params[MODULE::PARAM_STEP_CENTER].getValue() != stepCenterValue) {
				float v = m->params[MODULE::PARAM_STEP_CENTER].getValue();
				float delta =  v - stepCenterValue;

				if (stepState == SEQ_UI_STATE::AUXILIARY) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->auxiliary = clamp(stepGet(stepEditSelected)->auxiliary + delta / 10.f, 0.f, 1.f);
					stepCenterValue = v;
				}
				if (stepState == SEQ_UI_STATE::PROBABILITY) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->probability = clamp(stepGet(stepEditSelected)->probability + delta / 10.f, 0.f, 1.f);
					stepCenterValue = v;
				}
				if (stepState == SEQ_UI_STATE::RATCHETS && std::abs(delta) > 0.5f) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->ratchets = (int)clamp(std::round(stepGet(stepEditSelected)->ratchets + delta * 2.f), 0.f, 8.f);
					stepCenterValue = v;
				}
				if (stepState == SEQ_UI_STATE::SLEW) {
					if (stepEditSelected >= 0) stepGet(stepEditSelected)->slew = clamp(stepGet(stepEditSelected)->slew + delta / 10.f, 0.f, 1.f);
					stepCenterValue = v;
				}
			}
		}

		processOutput(args, clockPulse);
		if (lightDivider.process()) processLights(args);
	}

	void processOutput(const FlowerProcessArgs& args, bool clockPulse) {
		auto _clamp = [&](float v) {
			switch (outCvMode) {
				case OUT_CV_MODE::BI_10V:
					return clamp(v, -10.f, 10.f);
				case OUT_CV_MODE::BI_5V:
					return clamp(v, -5.f, 5.f);
				case OUT_CV_MODE::BI_1V:
					return clamp(v, -1.f, 1.f);
				case OUT_CV_MODE::UNI_10V:
					return clamp(v, 0.f, 10.f);
				case OUT_CV_MODE::UNI_5V:
					return clamp(v, 0.f, 5.f);
				case OUT_CV_MODE::UNI_3V:
					return clamp(v, 0.f, 3.f);
				case OUT_CV_MODE::UNI_2V:
					return clamp(v, 0.f, 2.f);
				case OUT_CV_MODE::UNI_1V:
					return clamp(v, 0.f, 1.f);
			}
			return v;
		};

		float v = 0.f;
		float stepProbability = 1.f;
		bool stepEnabled = true;

		switch (args.patternType) {
			default:
			case PATTERN_TYPE::SEQ_FWD: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::SEQ_REV: {
				stepOutIndex = (args.stepStart + args.stepLength - args.stepIndex - 1) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::SEQ_ADD_1V: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				v += 1.f;
				break;
			}
			case PATTERN_TYPE::SEQ_INV: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				float w = std::floor(v);
				float r = 1.f - (v - w);
				v = w + r;
				break;
			}
			case PATTERN_TYPE::ADD_2STEPS: {
				stepOutIndex = (args.stepStart + ((args.stepIndex + 2) % args.stepLength)) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::AUX_ADD: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				v += stepGet(args.stepIndex)->auxiliary;
				break;
			}
			case PATTERN_TYPE::SEQ_PROB_05: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				stepProbability = 0.5f;
				break;
			}
			case PATTERN_TYPE::AUX_SUB: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				v -= stepGet(args.stepIndex)->auxiliary;
				break;
			}
			case PATTERN_TYPE::SEQ_RAND: {
				stepOutIndex = (args.stepStart + (stepRandomIndex % args.stepLength)) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				break;
			}
			case PATTERN_TYPE::SEQ_OOD: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				if (stepOutIndex % 2 == 0)
					v = stepGetValueScaled(stepOutIndex);
				else
					stepEnabled = false;
				break;
			}
			case PATTERN_TYPE::SEQ_EVEN: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				if (stepOutIndex % 2 == 1)
					v = stepGetValueScaled(stepOutIndex);
				else
					stepEnabled = false;
				break;
			}
			case PATTERN_TYPE::SEQ_TRANSPOSE: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				v = stepGetValueScaled(stepOutIndex);
				v += stepRandomSeqTranpose;
				break;
			}
			case PATTERN_TYPE::AUX_RAND: {
				stepOutIndex = (args.stepStart + args.stepIndex) % STEPS;
				int sign = ((stepRandomSeqAuxiliary & (1 << stepOutIndex)) > 0) * 1 + ((stepRandomSeqAuxiliary & (1 << (stepRandomIndex + 16))) > 0) * -1;
				v = stepGetValueScaled(stepOutIndex);
				v += sign * stepGet(stepOutIndex)->auxiliary;
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
			trigPulseGenerator.trigger(2e-2f);
		}
		if (stepEnabled && args.running) {
			stepSlew.setRiseFall(stepGet(stepOutIndex)->slew, stepGet(stepOutIndex)->slew);
			v = stepSlew.process(v, args.sampleTime);
			if (outCvClamp) v = _clamp(v);
			m->outputs[MODULE::OUTPUT_CV].setVoltage(v);

			switch (outAuxMode) {
				case OUT_AUX_MODE::TRIG:
					m->outputs[MODULE::OUTPUT_AUX].setVoltage(trigPulseGenerator.process(args.sampleTime) ? 10.f : 0.f);
					break;
				case OUT_AUX_MODE::TRIG_SLEW:
					stepSlewTrigger.setRiseFall(-1.f, stepGet(stepOutIndex)->slew);
					m->outputs[MODULE::OUTPUT_AUX].setVoltage(stepSlewTrigger.process(trigPulseGenerator.process(args.sampleTime) ? 10.f : 0.f, args.sampleTime));
					break;
				case OUT_AUX_MODE::CLOCK:
					m->outputs[MODULE::OUTPUT_AUX].setVoltage(args.clock);
					break;
				case OUT_AUX_MODE::AUXILIARY:
					m->outputs[MODULE::OUTPUT_AUX].setVoltage(stepGet(stepOutIndex)->auxiliary);
					break;
			}
		} else {
			switch (outAuxMode) {
				default:
					m->outputs[MODULE::OUTPUT_AUX].setVoltage(0.f);
					break;
				case OUT_AUX_MODE::CLOCK:
					m->outputs[MODULE::OUTPUT_AUX].setVoltage(args.clock);
					break;
			}
		}
	}

	inline float stepGetValueScaled(int index, bool useCvInput = true) {
		float v = m->params[MODULE::PARAM_STEP + index].getValue();
		if (outCvMode == OUT_CV_MODE::BI_10V || outCvMode == OUT_CV_MODE::BI_5V || outCvMode == OUT_CV_MODE::BI_1V) {
			v -= 0.5f;
		}

		useCvInput = useCvInput && m->inputs[MODULE::INPUT_STEP + index].isConnected();
		if (useCvInput && stepCvMode == SEQ_CV_MODE::ATTENUATE) {
			v = (m->inputs[MODULE::INPUT_STEP + index].getVoltage() / 10.f) * v;
		}

		switch (outCvMode) {
			case OUT_CV_MODE::BI_10V:
				v = rescale(v, -0.5f, 0.5f, -10.f, 10.f); break;
			case OUT_CV_MODE::BI_5V:
				v = rescale(v, -0.5f, 0.5f, -5.f, 5.f); break;
			case OUT_CV_MODE::BI_1V:
				v = rescale(v, -0.5f, 0.5f, -1.f, 1.f); break;
			case OUT_CV_MODE::UNI_10V:
				v = rescale(v, 0.f, 1.f, 0.f, 10.f); break;
			case OUT_CV_MODE::UNI_5V:
				v = rescale(v, 0.f, 1.f, 0.f, 5.f); break;
			case OUT_CV_MODE::UNI_3V:
				v = rescale(v, 0.f, 1.f, 0.f, 3.f); break;
			case OUT_CV_MODE::UNI_2V:
				v = rescale(v, 0.f, 1.f, 0.f, 2.f); break;
			case OUT_CV_MODE::UNI_1V:
			default: 
				break;
		}

		if (useCvInput && stepCvMode == SEQ_CV_MODE::SUM) {
			v += m->inputs[MODULE::INPUT_STEP + index].getVoltage();
		}

		return v;
	}

	inline FlowerSeqStep* stepGet(int idx) {
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
			if (flags.test(FlowerProcessArgs::STEP_AUX))
				stepGet(i)->auxiliary = random::uniform();
			if (flags.test(FlowerProcessArgs::STEP_PROB))
				stepGet(i)->probability = random::uniform();
			if (flags.test(FlowerProcessArgs::STEP_RATCHETS))
				stepGet(i)->ratchets = 1 + std::min(7, stepGeoDist(randGen));
			if (flags.test(FlowerProcessArgs::STEP_SLEW)) 
				stepGet(i)->slew = stepGeoDist(randGen) / ((stepGeoDist(randGen) + 1) * (7.f + 3.f * random::uniform()));
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
			float r = (stepState == SEQ_UI_STATE::PROBABILITY) + (stepState == SEQ_UI_STATE::RATCHETS) + (stepState == SEQ_UI_STATE::SLEW);
			float g = (stepState == SEQ_UI_STATE::AUXILIARY) + (stepState == SEQ_UI_STATE::PROBABILITY) + (stepState == SEQ_UI_STATE::SLEW ? 0.55f : 0.f);
			float b = (stepState == SEQ_UI_STATE::DEFAULT ? editLightBrightness : 0.f);
			m->lights[MODULE::LIGHT_EDIT + i * 3 + 0].setBrightness(r);
			m->lights[MODULE::LIGHT_EDIT + i * 3 + 1].setBrightness(g);
			m->lights[MODULE::LIGHT_EDIT + i * 3 + 2].setSmoothBrightness(b, st);
		}

		// Step lights
		if (stepState == SEQ_UI_STATE::DEFAULT) {
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
		if (stepState == SEQ_UI_STATE::AUXILIARY) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->auxiliary >= float(i) / float(STEPS);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i));
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
		if (stepState == SEQ_UI_STATE::PROBABILITY) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->probability >= float(i) / float(STEPS);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
		if (stepState == SEQ_UI_STATE::RATCHETS) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->ratchets > i;
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i));
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
		if (stepState == SEQ_UI_STATE::SLEW) {
			for (int i = 0; i < STEPS; i++) {
				bool b = stepEditSelected >= 0 && stepGet(stepEditSelected)->slew >= float(i) / float(STEPS);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 0].setBrightness((stepBlink && stepEditSelected == i) + b);
				m->lights[MODULE::LIGHT_STEP + i * 3 + 1].setBrightness((stepBlink && stepEditSelected == i) + (b ? 0.55f : 0.f));
				m->lights[MODULE::LIGHT_STEP + i * 3 + 2].setBrightness((stepBlink && stepEditSelected == i));
			}
		}
	}


	void dataToJson(json_t* rootJ) {
		// Common
		json_object_set_new(rootJ, "outCvMode", json_integer((int)outCvMode));
		json_object_set_new(rootJ, "outAuxMode", json_integer((int)outAuxMode));
		json_object_set_new(rootJ, "outCvClamp", json_boolean(outCvClamp));

		// Steps
		json_object_set_new(rootJ, "stepCvMode", json_integer((int)stepCvMode));
		json_t* stepsJ = json_array();
		for (int i = 0; i < STEPS; i++) {
			json_t* stepJ = json_object();
			json_object_set_new(stepJ, "disabled", json_boolean(stepGet(i)->disabled));
			json_object_set_new(stepJ, "auxiliary", json_real(stepGet(i)->auxiliary));
			json_object_set_new(stepJ, "probability", json_real(stepGet(i)->probability));
			json_object_set_new(stepJ, "ratchets", json_integer(stepGet(i)->ratchets));
			json_object_set_new(stepJ, "slew", json_real(stepGet(i)->slew));
			json_array_append_new(stepsJ, stepJ);
		}
		json_object_set_new(rootJ, "step", stepsJ);
	}

	void dataFromJson(json_t* rootJ) {
		// Common
		outCvMode = (OUT_CV_MODE)json_integer_value(json_object_get(rootJ, "outCvMode"));
		outAuxMode = (OUT_AUX_MODE)json_integer_value(json_object_get(rootJ, "outAuxMode"));
		outCvClamp = json_is_true(json_object_get(rootJ, "outCvClamp"));

		// Steps
		stepState = SEQ_UI_STATE::DEFAULT;
		stepCvMode = (SEQ_CV_MODE)json_integer_value(json_object_get(rootJ, "stepCvMode"));
		json_t* stepsJ = json_object_get(rootJ, "step");
		for (int i = 0; i < STEPS; i++) {
			json_t* stepJ = json_array_get(stepsJ, i);
			stepGet(i)->disabled = json_boolean_value(json_object_get(stepJ, "disabled"));
			stepGet(i)->auxiliary = json_real_value(json_object_get(stepJ, "auxiliary"));
			stepGet(i)->probability = json_real_value(json_object_get(stepJ, "probability"));
			stepGet(i)->ratchets = json_integer_value(json_object_get(stepJ, "ratchets"));
			stepGet(i)->slew = json_real_value(json_object_get(stepJ, "slew"));
		}
	}
};

} // namespace Flower
} // namespace StoermelderPackOne