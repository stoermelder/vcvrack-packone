#include "plugin.hpp"
#include "components/Knobs.hpp"

namespace StoermelderPackOne {
namespace Prisma {

template <int UNITS = 4>
struct PrismaModule : Module {
	enum ParamIds {
		PARAM_INPUT,
		ENUMS(PARAM_SHIFT_CV, UNITS),
		ENUMS(PARAM_SHIFT, UNITS),
		ENUMS(PARAM_LEVEL, UNITS),
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		ENUMS(INPUT_SHIFT_CV, UNITS),
		ENUMS(INPUT_LEVEL, UNITS),
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		OUTPUT_POLY,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	simd::float_4 input_cv[UNITS / 4];
	simd::float_4 param_cv[UNITS / 4];
	simd::float_4 param_shift[UNITS / 4];
	simd::float_4 input_level[UNITS / 4];
	simd::float_4 param_level[UNITS / 4];

	dsp::RCFilter dcblock[UNITS];
	dsp::TBiquadFilter<float> biquad[UNITS];

	dsp::MinBlepGenerator<16, 32> minBlep[UNITS];

	PrismaModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_INPUT, 0.f, 2.f, 1.f, "Input level", "x");

		for (int i = 0; i < UNITS; i++) {
			configParam(PARAM_SHIFT_CV + i, 0.f, 1.f, 0.f, string::f("Shift unit %i CV attenuator", i + 1), "x");
			configParam(PARAM_SHIFT + i, 0.f, 1.f, 1.f / (UNITS + 1) * (i + 1.f), string::f("Shift unit %i shift", i + 1));
			configParam(PARAM_LEVEL + i, 0.f, 1.f, 0.5f, string::f("Shift unit %i sum level", i + 1));
		}
		for (int i = 0; i < UNITS; i++) {
			biquad[i].setParameters(dsp::TBiquadFilter<float>::Type::LOWPASS, 0.4f, 1.f, 0.f);
		}
		onSampleRateChange();
		onReset();
	}

	void onSampleRateChange() override {
		for (int i = 0; i < UNITS; i++) {
			dcblock[i].setCutoffFreq(40.f / APP->engine->getSampleRate());
		}
	}

	float compPrev[UNITS] = {0.f};
	float inPrev = 0.f;

	void process(const ProcessArgs& args) override {
		float in = inputs[INPUT].getVoltage();
		in *= params[PARAM_INPUT].getValue();

		float out = 0.f;
		float div = params[PARAM_INPUT].getValue();
		for (int i = 0; i < UNITS; i++) {
			float cv = clamp(inputs[INPUT_SHIFT_CV + i].getVoltage() * params[PARAM_SHIFT_CV + i].getValue() + params[PARAM_SHIFT + i].getValue() * 10.f, 0.f, 10.f);
			cv -= 5.f;
			float comp = in > cv ? -5.f : 5.f;

			if (comp != compPrev[i]) {
				// discontinuity
				float m1 = in - cv;
				float m2 = inPrev - cv;
				float d = m1 / (m1 - m2);
				minBlep[i].insertDiscontinuity(-d, comp - compPrev[i]);
			}
			
			compPrev[i] = comp;

			float s = in + comp - cv;
			s += minBlep[i].process();

			// Filter at 0.4 * samplerate
			s = biquad[i].process(s);

			// Block DC in the signal
			dcblock[i].process(s);
			s = dcblock[i].highpass();

			outputs[OUTPUT_POLY].setVoltage(s, i);
			float l = params[PARAM_LEVEL + i].getValue() * inputs[INPUT_LEVEL + i].getNormalVoltage(10.f) / 10.f;
			out += (s * l);
			div += l;
		}

		inPrev = in;

		/*
		float out = 0.f;
		out += in * params[PARAM_INPUT].getValue();

		for (int i = 0; i < UNITS; i++) {
			input_cv[i / 4][i % 4] = inputs[INPUT_SHIFT_CV + i].getVoltage();
			param_cv[i / 4][i % 4] = params[PARAM_SHIFT_CV + i].getValue();
			param_shift[i / 4][i % 4] = params[PARAM_SHIFT + i].getValue();
			input_level[i / 4][i % 4] = inputs[INPUT_LEVEL + i].getNormalVoltage(10.f);
			param_level[i / 4][i % 4] = params[PARAM_LEVEL + i].getValue();
		}
		
		for (int c = 0; c < UNITS; c += 4) {
			simd::float_4 cv = input_cv[c / 4] * param_cv[c / 4] + param_shift[c / 4] * 10.f;
			cv = simd::clamp(cv, 0.f, 10.f);
			cv -= 5.f;
			simd::float_4 comp = simd::ifelse(simd::float_4(in) >= cv, -5.f, 5.f);
			simd::float_4 s = simd::float_4(in) + comp - cv;

			// Filter at 0.4 * samplerate
			s = biquad[c / 4].process(s);

			outputs[OUTPUT_POLY].setVoltageSimd(s, c);
			simd::float_4 l = param_level[c / 4] * input_level[c / 4] / 10.f;
			s = s * l;

			__m128 _s = s.v;
			_s = _mm_hadd_ps(_s, _s);
			_s = _mm_hadd_ps(_s, _s);
			out += _s[0];
			//__m128 _l = l.v;
			//_l = _mm_hadd_ps(_l, _l);
			//_l = _mm_hadd_ps(_l, _l);
			//div += _l[0];
		}
		*/

		outputs[OUTPUT_POLY].setChannels(UNITS);
		outputs[OUTPUT].setVoltage(out);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};


struct PrismaWidget : ThemedModuleWidget<PrismaModule<4>> {
	PrismaWidget(PrismaModule<4>* module)
		: ThemedModuleWidget<PrismaModule<4>>(module, "Prisma") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 72.8f), module, PrismaModule<4>::INPUT_SHIFT_CV + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(33.6f, 47.9f), module, PrismaModule<4>::PARAM_SHIFT_CV + 0));
		addParam(createParamCentered<StoermelderLargeKnob>(Vec(67.5f, 65.9f), module, PrismaModule<4>::PARAM_SHIFT + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(101.4f, 47.9f), module, PrismaModule<4>::PARAM_LEVEL + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(112.5f, 72.8f), module, PrismaModule<4>::INPUT_LEVEL + 0));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 129.f), module, PrismaModule<4>::INPUT_SHIFT_CV + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(33.6f, 104.1f), module, PrismaModule<4>::PARAM_SHIFT_CV + 1));
		addParam(createParamCentered<StoermelderLargeKnob>(Vec(67.5f, 122.1f), module, PrismaModule<4>::PARAM_SHIFT + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(101.4f, 104.1f), module, PrismaModule<4>::PARAM_LEVEL + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(112.5f, 129.f), module, PrismaModule<4>::INPUT_LEVEL + 1));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 185.2f), module, PrismaModule<4>::INPUT_SHIFT_CV + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(33.6f, 160.3f), module, PrismaModule<4>::PARAM_SHIFT_CV + 2));
		addParam(createParamCentered<StoermelderLargeKnob>(Vec(67.5f, 178.3f), module, PrismaModule<4>::PARAM_SHIFT + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(101.4f, 160.3f), module, PrismaModule<4>::PARAM_LEVEL + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(112.5f, 185.2f), module, PrismaModule<4>::INPUT_LEVEL + 2));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 241.4f), module, PrismaModule<4>::INPUT_SHIFT_CV + 3));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(33.6f, 216.5f), module, PrismaModule<4>::PARAM_SHIFT_CV + 3));
		addParam(createParamCentered<StoermelderLargeKnob>(Vec(67.5f, 234.5f), module, PrismaModule<4>::PARAM_SHIFT + 3));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(101.4f, 216.5f), module, PrismaModule<4>::PARAM_LEVEL + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(112.5f, 241.4f), module, PrismaModule<4>::INPUT_LEVEL + 3));
	
		addInput(createInputCentered<StoermelderPort>(Vec(52.3f, 285.f), module, PrismaModule<4>::INPUT));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(82.7f, 285.f), module, PrismaModule<4>::PARAM_INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(52.3f, 327.9f), module, PrismaModule<4>::OUTPUT_POLY));
		addOutput(createOutputCentered<StoermelderPort>(Vec(82.7f, 327.9f), module, PrismaModule<4>::OUTPUT));
	}
};

} // namespace Prisma
} // namespace StoermelderPackOne

Model* modelPrisma = createModel<StoermelderPackOne::Prisma::PrismaModule<4>, StoermelderPackOne::Prisma::PrismaWidget>("Prisma");