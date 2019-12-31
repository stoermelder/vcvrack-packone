#include "plugin.hpp"
#include <thread>

namespace Sipo {

static const int MAX_DATA = 4096;
static const int MAX_DATA_32 = MAX_DATA / 32;
static const int MAX_DATA_32_16 = MAX_DATA_32 / PORT_MAX_CHANNELS;

struct SipoModule : Module {
	enum ParamIds {
		SKIP_PARAM,
		INCR_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		SRC_INPUT,
		TRIG_INPUT,
		SKIP_INPUT,
		INCR_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHANNEL_LIGHTS, PORT_MAX_CHANNELS * 2),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	float* data;
	/** [Stored to JSON] */
	int dataPtr = 0;
	int dataUsed = 0;

	dsp::SchmittTrigger clockTrigger;
	dsp::ClockDivider lightDivider;

	SipoModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SKIP_PARAM, 0.f, MAX_DATA_32 - 1, 0.f, "Trigger-skip to the current value, 0 acts as a standard shift register");
		configParam(INCR_PARAM, 0.f, MAX_DATA_32_16, 0.f, "Inrement between used cells, 0 acts as standard shift register");
		data = new float[MAX_DATA];

		onReset();
		lightDivider.setDivision(512);
	}

	~SipoModule() {
		delete[] data;
	}

	void onReset() override {
		dataPtr = 0;
		dataUsed = 0;
		for (int i = 0; i < MAX_DATA; i++) {
			data[i] = 0.f;
		}
	}

	void process(const ProcessArgs &args) override {
		outputs[POLY_OUTPUT].setChannels(PORT_MAX_CHANNELS);

		if (clockTrigger.process(inputs[TRIG_INPUT].getVoltage())) {
			dataPtr = (dataPtr + 1) % MAX_DATA;
			dataUsed = std::min(dataUsed + 1, MAX_DATA);
			data[dataPtr] = inputs[SRC_INPUT].getVoltage();
		}

		int skipCv = std::round(rescale(inputs[SKIP_INPUT].getVoltage(), 0.f, 10.f, 0, MAX_DATA_32 - 1));
		int skip = 1 + (int)clamp((int)params[SKIP_PARAM].getValue() + skipCv, 0, MAX_DATA_32 - 1);
		int incrCv = std::round(rescale(inputs[INCR_INPUT].getVoltage(), 0.f, 10.f, 0, MAX_DATA_32_16));
		int incr = (int)clamp((int)params[INCR_PARAM].getValue() + incrCv, 0, MAX_DATA_32_16);

		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			outputs[POLY_OUTPUT].setVoltage(data[(dataPtr - (skip + incr * c) * c + MAX_DATA) % MAX_DATA], c);
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
				float f = outputs[POLY_OUTPUT].getVoltage(c);
				lights[CHANNEL_LIGHTS + 2 * c + 0].setBrightness(f <= 0.f ? 0 : rescale(f, 0.f, 5.f, 0.f, 1.f));
				lights[CHANNEL_LIGHTS + 2 * c + 1].setBrightness(f >= 0.f ? 0 : rescale(f, -5.f, 0.f, 0.f, 1.f));
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* dataJ = json_array();
		for (int i = 0; i < dataUsed; i++) {
			json_t* d = json_real(data[i]);
			json_array_append(dataJ, d);
		}
		json_object_set_new(rootJ, "data", dataJ);

		json_object_set_new(rootJ, "dataPtr", json_integer(dataPtr));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* dataJ = json_object_get(rootJ, "data");
		if (dataJ) {
			json_t *d;
			size_t dataIndex;
			json_array_foreach(dataJ, dataIndex, d) {
				data[dataIndex] = json_real_value(d);
			}
			dataUsed = dataIndex;
		}

		dataPtr = json_integer_value(json_object_get(rootJ, "dataPtr"));
	}
};


struct SipoWidget : ThemedModuleWidget<SipoModule> {
	SipoWidget(SipoModule* module)
		: ThemedModuleWidget<SipoModule>(module, "Sipo") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 60.9f), module, SipoModule::TRIG_INPUT));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 109.5f), module, SipoModule::SKIP_INPUT));
		StoermelderTrimpot* tp1 = createParamCentered<StoermelderTrimpot>(Vec(22.5f, 134.1f), module, SipoModule::SKIP_PARAM);
		tp1->snap = true;
		addParam(tp1);

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 181.4f), module, SipoModule::INCR_INPUT));
		StoermelderTrimpot* tp2 = createParamCentered<StoermelderTrimpot>(Vec(22.5f, 206.2f), module, SipoModule::INCR_PARAM);
		tp2->snap = true;
		addParam(tp2);

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 252.2f), module, SipoModule::SRC_INPUT));

		PolyLedWidget<GreenRedLight, 2>* w = createWidgetCentered<PolyLedWidget<GreenRedLight, 2>>(Vec(22.5f, 299.8f));
		w->setModule(module, SipoModule::CHANNEL_LIGHTS);
		addChild(w);
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, SipoModule::POLY_OUTPUT));
	}
};

} // namespace Sipo

Model* modelSipo = createModel<Sipo::SipoModule, Sipo::SipoWidget>("Sipo");