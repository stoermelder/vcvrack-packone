#include "plugin.hpp"
#include "widgets.hpp"

namespace Sipo {

static const int MAX_DATA = 4096;
static const int MAX_DATA_32 = MAX_DATA / 32;
static const int MAX_DATA_32_16 = MAX_DATA_32 / 16;

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
		TRIG_LIGHT,
		ENUMS(CHANNEL_LIGHTS, 32),
		NUM_LIGHTS
	};

	dsp::SchmittTrigger clockTrigger;

	float* data;
	int dataPtr = 0;

	dsp::ClockDivider lightDivider;

	SipoModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(OFFSET_PARAM, 1.f, MAX_DATA_32, 1.f, "Offset to the current value, 1 acts as a standard shift register");
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
		for (int i = 0; i < MAX_DATA; i++) {
			data[i] = 0.f;
		}
	}

	void process(const ProcessArgs &args) override {
		outputs[POLY_OUTPUT].setChannels(16);

		if (clockTrigger.process(inputs[TRIG_INPUT].getVoltage())) {
			dataPtr = (dataPtr + 1) % MAX_DATA;
			data[dataPtr] = inputs[SRC_INPUT].getVoltage();
		}

		int offsetCv = std::round(rescale(inputs[OFFSET_INPUT].getVoltage(), 0.f, 10.f, 0, MAX_DATA_32));
		int offset = (int) clamp((int)params[OFFSET_PARAM].getValue() + offsetCv, 1, MAX_DATA_32);
		int incrCv = std::round(rescale(inputs[INCR_INPUT].getVoltage(), 0.f, 10.f, 0, MAX_DATA_32_16));
		int incr = (int) clamp((int)params[INCR_PARAM].getValue() + incrCv, 0, MAX_DATA_32_16);

		for (int c = 0; c < 16; c++) {
			outputs[POLY_OUTPUT].setVoltage(data[(dataPtr - (offset + incr * c) * c + MAX_DATA) % MAX_DATA], c);
		}

		lights[TRIG_LIGHT].setSmoothBrightness(clockTrigger.isHigh(), args.sampleTime);

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < 16; c++) {
				float f = outputs[POLY_OUTPUT].getVoltage(c);
				lights[CHANNEL_LIGHTS + 2 * c + 0].setBrightness(f < 0.f ? 0 : rescale(f, 0.f, 5.f, 0.f, 1.f));
				lights[CHANNEL_LIGHTS + 2 * c + 1].setBrightness(f > 0.f ? 0 : rescale(f, -5.f, 0.f, 0.f, 1.f));
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_t* dataJ = json_array();
		for (int i = 0; i < MAX_DATA; i++) {
			json_t* d = json_real(data[i]);
			json_array_append(dataJ, d);
		}
		json_object_set_new(rootJ, "data", dataJ);

		json_t* dataPtrJ = json_integer(dataPtr);
		json_object_set_new(rootJ, "dataPtr", dataPtrJ);

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
		}

		json_t* dataPtrJ = json_object_get(rootJ, "dataPtr");
		if (dataPtrJ) dataPtr = json_integer_value(dataPtrJ);
	}
};


struct SipoWidget : ModuleWidget {
	SipoWidget(SipoModule* module) {	
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Sipo.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 60.6f), module, SipoModule::TRIG_INPUT));
		addChild(createLightCentered<SmallLight<GreenLight>>(Vec(32.3f, 76.3f), module, SipoModule::TRIG_LIGHT));

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 112.1f), module, SipoModule::OFFSET_INPUT));
		Trimpot* tp1 = createParamCentered<Trimpot>(Vec(22.5f, 140.5f), module, SipoModule::OFFSET_PARAM);
		tp1->snap = true;
		addParam(tp1);

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 183.3f), module, SipoModule::INCR_INPUT));
		Trimpot* tp2 = createParamCentered<Trimpot>(Vec(22.5f, 211.8f), module, SipoModule::INCR_PARAM);
		tp2->snap = true;
		addParam(tp2);

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 254.f), module, SipoModule::SRC_INPUT));

		PolyLedWidget<GreenRedLight, 2>* w = createWidget<PolyLedWidget<GreenRedLight, 2>>(Vec(14.f, 288.2f));
		w->setModule(module, SipoModule::CHANNEL_LIGHTS);
		addChild(w);
		addOutput(createOutputCentered<PJ301MPort>(Vec(22.5f, 324.1f), module, SipoModule::POLY_OUTPUT));
	}
};

} // namespace Sipo

Model* modelSipo = createModel<Sipo::SipoModule, Sipo::SipoWidget>("Sipo");