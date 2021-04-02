#include "plugin.hpp"

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

	dsp::RCFilter dcblock;
	dsp::TBiquadFilter<simd::float_4> biquad[UNITS / 4];

	PrismaModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_INPUT, 0.f, 2.f, 1.f, "Input level", "x");

		for (int i = 0; i < UNITS; i++) {
			configParam(PARAM_SHIFT_CV + i, 0.f, 1.f, 0.f, string::f("Shift unit %i CV attenuator", i + 1), "x");
			configParam(PARAM_SHIFT + i, 0.f, 1.f, 1.f / (UNITS + 1) * (i + 1.f), string::f("Shift unit %i shift", i + 1));
			configParam(PARAM_LEVEL + i, 0.f, 1.f, 0.5f, string::f("Shift unit %i sum level", i + 1));
		}
		for (int i = 0; i < UNITS / 4; i++) {
			biquad[i].setParameters(dsp::TBiquadFilter<simd::float_4>::Type::LOWPASS, 0.4f, 1.f, 0.f);
		}
		onSampleRateChange();
		onReset();
	}

	void onSampleRateChange() override {
		dcblock.setCutoffFreq(20.f / APP->engine->getSampleRate());
	}

	void process(const ProcessArgs& args) override {
		float in = inputs[INPUT].getVoltage();
		//in *= params[PARAM_INPUT].getValue();

		/*
		float out = in;
		float div = params[PARAM_INPUT].getValue();
		for (int i = 0; i < UNITS; i++) {
			float cv = clamp(inputs[INPUT_SHIFT_CV + i].getVoltage() * params[PARAM_SHIFT_CV + i].getValue() + params[PARAM_SHIFT + i].getValue() * 10.f, 0.f, 10.f);
			cv -= 5.f;
			float comp = in > cv ? -5.f : 5.f;
			float s = in + comp - cv;
			outputs[OUTPUT_POLY].setVoltage(s, i);
			float l = params[PARAM_LEVEL + i].getValue() * inputs[INPUT_LEVEL + i].getNormalVoltage(10.f) / 10.f;
			out += (s * l);
			div += l;
		}
		*/

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

		// Block DC in the signal
		dcblock.process(out);
		out = dcblock.highpass();

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
		
		addInput(createInputCentered<StoermelderPort>(Vec(20.1f, 72.8f), module, PrismaModule<4>::INPUT_SHIFT_CV + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(27.0f, 49.3f), module, PrismaModule<4>::PARAM_SHIFT_CV + 0));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(52.5f, 67.9f), module, PrismaModule<4>::PARAM_SHIFT + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(78.0f, 49.3f), module, PrismaModule<4>::PARAM_LEVEL + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(84.9f, 72.8f), module, PrismaModule<4>::INPUT_LEVEL + 0));

		addInput(createInputCentered<StoermelderPort>(Vec(20.1f, 106.8f), module, PrismaModule<4>::INPUT_SHIFT_CV + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(27.0f, 130.3f), module, PrismaModule<4>::PARAM_SHIFT_CV + 1));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(52.5f, 111.7f), module, PrismaModule<4>::PARAM_SHIFT + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(78.0f, 130.3f), module, PrismaModule<4>::PARAM_LEVEL + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(84.9f, 106.8f), module, PrismaModule<4>::INPUT_LEVEL + 1));

		addInput(createInputCentered<StoermelderPort>(Vec(20.1f, 272.8f), module, PrismaModule<4>::INPUT_SHIFT_CV + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(27.0f, 249.3f), module, PrismaModule<4>::PARAM_SHIFT_CV + 2));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(52.5f, 267.9f), module, PrismaModule<4>::PARAM_SHIFT + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(78.0f, 249.3f), module, PrismaModule<4>::PARAM_LEVEL + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(84.9f, 272.8f), module, PrismaModule<4>::INPUT_LEVEL + 2));

		addInput(createInputCentered<StoermelderPort>(Vec(20.1f, 306.8f), module, PrismaModule<4>::INPUT_SHIFT_CV + 3));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(27.0f, 330.3f), module, PrismaModule<4>::PARAM_SHIFT_CV + 3));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(52.5f, 311.7f), module, PrismaModule<4>::PARAM_SHIFT + 3));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(78.0f, 330.3f), module, PrismaModule<4>::PARAM_LEVEL + 3));
		addInput(createInputCentered<StoermelderPort>(Vec(84.9f, 306.8f), module, PrismaModule<4>::INPUT_LEVEL + 3));
	
		addInput(createInputCentered<StoermelderPort>(Vec(37.3f, 172.6f), module, PrismaModule<4>::INPUT));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(67.7f, 172.6f), module, PrismaModule<4>::PARAM_INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(37.3f, 216.6f), module, PrismaModule<4>::OUTPUT_POLY));
		addOutput(createOutputCentered<StoermelderPort>(Vec(67.7f, 216.6f), module, PrismaModule<4>::OUTPUT));
	}
};

} // namespace Prisma
} // namespace StoermelderPackOne

Model* modelPrisma = createModel<StoermelderPackOne::Prisma::PrismaModule<4>, StoermelderPackOne::Prisma::PrismaWidget>("Prisma");