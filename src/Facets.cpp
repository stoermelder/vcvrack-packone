#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Facets {

template <int UNITS = 4>
struct FacetsModule : Module {
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

	FacetsModule() {
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


struct FacetsWidget : ThemedModuleWidget<FacetsModule<4>> {
	FacetsWidget(FacetsModule<4>* module)
		: ThemedModuleWidget<FacetsModule<4>>(module, "Facets") {
		setModule(module);



		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < 4; i++) {
			float o = i * 57.3f;
			addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.3f, 52.3f + o), module, FacetsModule<4>::PARAM_SHIFT + i));
			addInput(createInputCentered<StoermelderPort>(Vec(49.5f, 52.3f + o), module, FacetsModule<4>::INPUT_SHIFT_CV + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(49.5f, 76.3f + o), module, FacetsModule<4>::PARAM_SHIFT_CV + i));
			addInput(createInputCentered<StoermelderPort>(Vec(83.0f, 52.3f + o), module, FacetsModule<4>::INPUT_LEVEL + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(83.0f, 76.3f + o), module, FacetsModule<4>::PARAM_LEVEL + i));
		}

		addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.3f, 292.5f), module, FacetsModule<4>::PARAM_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(22.3f, 327.9f), module, FacetsModule<4>::INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(83.0f, 292.5f), module, FacetsModule<4>::OUTPUT_POLY));
		addOutput(createOutputCentered<StoermelderPort>(Vec(83.0f, 327.9f), module, FacetsModule<4>::OUTPUT));
	}
};

} // namespace Facets
} // namespace StoermelderPackOne

Model* modelFacets = createModel<StoermelderPackOne::Facets::FacetsModule<4>, StoermelderPackOne::Facets::FacetsWidget>("Facets");