#include "../../plugin.hpp"
#include "DirtGenerators.hpp"
#include "DirtProcessors.hpp"
#include <random>

namespace StoermelderPackOne {
namespace Dirt {

struct DirtModule : Module {
	enum ParamIds {
		PARAM_NOISE,
		PARAM_CROSSTALK,
		PARAM_CRACKE,
		PARAM_PITCH,
		PARAM_CRUSH,
		PARAM_DROPOUT,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_NOISE,
		LIGHT_CROSSTALK,
		LIGHT_CRACKE,
		LIGHT_PITCH,
		LIGHT_CRUSH,
		LIGHT_DROPOUT,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	WhiteNoiseGenerator noise[PORT_MAX_CHANNELS];
	CrosstalkGenerator crosstalk;
	CrackleGenerator crackle;
	CrushDefectProcessor crushDefect;
	DropoutDefectProcessor dropoutDefect;
	PitchDefectProcessor pitchDefect;

	ClockDividerEx lightDivider;

	DirtModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(PARAM_NOISE, 0.f, 1.f, 1.f, "White noise", {"Off", "On"});
		paramQuantities[PARAM_NOISE]->description = "Adds low-level white noise to the signal.";
		configSwitch(PARAM_CROSSTALK, 0.f, 1.f, 1.f, "Crosstalk between channels of a polyphonic cable", {"Off", "On"});
		paramQuantities[PARAM_CROSSTALK]->description = "Mixes a small amount of each channel into its neighbors, emulating crosstalk in analog cables.";
		configSwitch(PARAM_CRACKE, 0.f, 1.f, 1.f, "Crackle", {"Off", "On"});
		paramQuantities[PARAM_CRACKE]->description = "Adds random pops and crackles to the signal.";
		configSwitch(PARAM_PITCH, 0.f, 1.f, 1.f, "Pitch defects", {"Off", "On"});
		paramQuantities[PARAM_PITCH]->description = "Introduces occasional pitch wobble.";
		configSwitch(PARAM_CRUSH, 0.f, 1.f, 1.f, "Crush defects", {"Off", "On"});
		paramQuantities[PARAM_CRUSH]->description = "Introduces occasional bit-depth / sample-rate reduction artifacts.";
		configSwitch(PARAM_DROPOUT, 0.f, 1.f, 1.f, "Dropout defects", {"Off", "On"});
		paramQuantities[PARAM_DROPOUT]->description = "Introduces occasional dropouts (silenced samples).";
		configInput(INPUT, "Polyphonic");
		inputInfos[INPUT]->description = "Polyphonic audio to which the lo-fi defects are applied.";
		configOutput(OUTPUT, "Polyphonic");
		outputInfos[OUTPUT]->description = "Polyphonic audio with the selected defects applied, same number of channels as the input.";
		for (size_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			noise[i].reset();
 		}
		crosstalk.reset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		crushDefect.setRateChange(e.sampleRate);
		dropoutDefect.setRateChange(e.sampleRate);
		pitchDefect.setRateChange(e.sampleRate);
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void process(const ProcessArgs& args) override {
		if (lightDivider.process()) {
			lights[LIGHT_NOISE].setBrightness(params[PARAM_NOISE].getValue() > 0.f);
			lights[LIGHT_CROSSTALK].setBrightness(params[PARAM_CROSSTALK].getValue() > 0.f);
			lights[LIGHT_CRACKE].setBrightness(params[PARAM_CRACKE].getValue() > 0.f);
			lights[LIGHT_PITCH].setBrightness(params[PARAM_PITCH].getValue() > 0.f);
			lights[LIGHT_CRUSH].setBrightness(params[PARAM_CRUSH].getValue() > 0.f);
			lights[LIGHT_DROPOUT].setBrightness(params[PARAM_DROPOUT].getValue() > 0.f);
		}

		int channels = inputs[INPUT].getChannels();
		if (!inputs[INPUT].isConnected()) return;

		float in[channels];
		inputs[INPUT].readVoltages(in);

		if (params[PARAM_PITCH].getValue() > 0.f) {
			pitchDefect.process(in, channels, args.sampleTime);
		}

		if (params[PARAM_CRUSH].getValue() > 0.f) {
			crushDefect.process(in, channels, args.sampleTime);
		}

		if (params[PARAM_DROPOUT].getValue() > 0.f) {
			dropoutDefect.process(in, channels, args.sampleTime);
		}

		if (params[PARAM_NOISE].getValue() > 0.f) {
			for (int i = 0; i < channels; i++) {
				in[i] += noise[i].process();
			}
		}

		if (params[PARAM_CROSSTALK].getValue() > 0.f) {
			crosstalk.process(in, channels);
		}

		if (params[PARAM_CRACKE].getValue() > 0.f) {
			crackle.process(in, channels);
		}

		outputs[OUTPUT].setChannels(channels);
		outputs[OUTPUT].writeVoltages(in);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* channelsJ = json_array();
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) {
			json_t* channelJ = json_object();
			json_object_set_new(channelJ, "noiseRatio", json_real(noise[i].ratio));
			json_object_set_new(channelJ, "crosstalkRatio", json_real(crosstalk.ratio[i]));
			json_object_set_new(channelJ, "crackleRatio", json_real(crackle.ratio[i]));
			json_array_append_new(channelsJ, channelJ);
		}
		json_object_set_new(rootJ, "channels", channelsJ);

		json_object_set_new(rootJ, "pitchDefect", pitchDefect.dataToJson());
		json_object_set_new(rootJ, "crushDefect", crushDefect.dataToJson());
		json_object_set_new(rootJ, "dropoutDefect", dropoutDefect.dataToJson());

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* channelsJ = json_object_get(rootJ, "channels");
		json_t* channelJ;
		size_t i;
		json_array_foreach(channelsJ, i, channelJ) {
			json_t* noiseRatioJ = json_object_get(channelJ, "noiseRatio");
			if (noiseRatioJ) noise[i].ratio = json_real_value(noiseRatioJ);
			json_t* crosstalkRatioJ = json_object_get(channelJ, "crosstalkRatio");
			if (crosstalkRatioJ) crosstalk.ratio[i] = json_real_value(crosstalkRatioJ);
			json_t* crackleRatioJ = json_object_get(channelJ, "crackleRatio");
			if (crackleRatioJ) crackle.ratio[i] = json_real_value(crackleRatioJ);
		}

		json_t* pitchDefectJ = json_object_get(rootJ, "pitchDefect");
		if (pitchDefectJ) pitchDefect.dataFromJson(pitchDefectJ);
		else params[PARAM_PITCH].setValue(0.f);

		json_t* crushDefectJ = json_object_get(rootJ, "crushDefect");
		if (crushDefectJ) crushDefect.dataFromJson(crushDefectJ);
		else params[PARAM_CRUSH].setValue(0.f);

		json_t* dropoutDefectJ = json_object_get(rootJ, "dropoutDefect");
		if (dropoutDefectJ) dropoutDefect.dataFromJson(dropoutDefectJ);
		else params[PARAM_DROPOUT].setValue(0.f);
	}
};

struct DirtWidget : ThemedModuleWidget<DirtModule> {
	DirtWidget(DirtModule* module)
		: ThemedModuleWidget<DirtModule>(module, "Dirt") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(22.5f, 61.0f), module, DirtModule::PARAM_NOISE, DirtModule::LIGHT_NOISE));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(22.5f, 98.0f), module, DirtModule::PARAM_CROSSTALK, DirtModule::LIGHT_CROSSTALK));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(22.5f, 134.1f), module, DirtModule::PARAM_CRACKE, DirtModule::LIGHT_CRACKE));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(22.5f, 170.6f), module, DirtModule::PARAM_PITCH, DirtModule::LIGHT_PITCH));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(22.5f, 207.2f), module, DirtModule::PARAM_CRUSH, DirtModule::LIGHT_CRUSH));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(22.5f, 243.7f), module, DirtModule::PARAM_DROPOUT, DirtModule::LIGHT_DROPOUT));
		
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 291.1f), module, DirtModule::INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, DirtModule::OUTPUT));
	}
};

} // namespace Dirt
} // namespace StoermelderPackOne

Model* modelDirt = createModel<StoermelderPackOne::Dirt::DirtModule, StoermelderPackOne::Dirt::DirtWidget>("Dirt");