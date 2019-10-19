#include "plugin.hpp"
#include "widgets.hpp"

namespace Sipo {

static const int MAX_DATA = 4096;
static const int MAX_DATA_32 = MAX_DATA / 32;
static const int MAX_DATA_32_16 = MAX_DATA_32 / PORT_MAX_CHANNELS;

struct SipoModule : Module {
	enum ParamIds {
		OFFSET_PARAM,
		INCR_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		SRC_INPUT,
		TRIG_INPUT,
		OFFSET_INPUT,
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

	dsp::SchmittTrigger clockTrigger;

	float* data;
	int dataPtr = 0;
	int dataUsed = 0;

	dsp::ClockDivider lightDivider;

	SipoModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(OFFSET_PARAM, 1.f, MAX_DATA_32, 1.f, "Trigger-count to the current value, 1 acts as a standard shift register");
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

		int offsetCv = std::round(rescale(inputs[OFFSET_INPUT].getVoltage(), 0.f, 10.f, 0, MAX_DATA_32));
		int offset = (int) clamp((int)params[OFFSET_PARAM].getValue() + offsetCv, 1, MAX_DATA_32);
		int incrCv = std::round(rescale(inputs[INCR_INPUT].getVoltage(), 0.f, 10.f, 0, MAX_DATA_32_16));
		int incr = (int) clamp((int)params[INCR_PARAM].getValue() + incrCv, 0, MAX_DATA_32_16);

		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			outputs[POLY_OUTPUT].setVoltage(data[(dataPtr - (offset + incr * c) * c + MAX_DATA) % MAX_DATA], c);
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
				float f = outputs[POLY_OUTPUT].getVoltage(c);
				lights[CHANNEL_LIGHTS + 2 * c + 0].setBrightness(f < 0.f ? 0 : rescale(f, 0.f, 5.f, 0.f, 1.f));
				lights[CHANNEL_LIGHTS + 2 * c + 1].setBrightness(f > 0.f ? 0 : rescale(f, -5.f, 0.f, 0.f, 1.f));
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

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


struct SipoWidget : ModuleWidget {
	SipoWidget(SipoModule* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Sipo.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 83.4f), module, SipoModule::TRIG_INPUT));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 126.9f), module, SipoModule::OFFSET_INPUT));
		StoermelderTrimpot* tp1 = createParamCentered<StoermelderTrimpot>(Vec(22.5f, 151.5f), module, SipoModule::OFFSET_PARAM);
		tp1->snap = true;
		addParam(tp1);

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 193.4f), module, SipoModule::INCR_INPUT));
		StoermelderTrimpot* tp2 = createParamCentered<StoermelderTrimpot>(Vec(22.5f, 218.1f), module, SipoModule::INCR_PARAM);
		tp2->snap = true;
		addParam(tp2);

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 258.7f), module, SipoModule::SRC_INPUT));

		PolyLedWidget<GreenRedLight, 2>* w = createWidget<PolyLedWidget<GreenRedLight, 2>>(Vec(14.f, 287.1f));
		w->setModule(module, SipoModule::CHANNEL_LIGHTS);
		addChild(w);
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 323.5f), module, SipoModule::POLY_OUTPUT));
	}
};

} // namespace Sipo

Model* modelSipo = createModel<Sipo::SipoModule, Sipo::SipoWidget>("Sipo");