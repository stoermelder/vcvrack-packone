#include "plugin.hpp"
#include "widgets.hpp"

struct RotorA : Module {
	enum ParamIds {
        CHANNELS_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		MOD_INPUT,
		CAR_INPUT,
        BASE_INPUT,        
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(INPUT_LIGHTS, 16),
		ENUMS(OUTPUT_LIGHTS, 16),
		NUM_LIGHTS
	};

    dsp::ClockDivider lightDivider;
    float temp[16];

    RotorA() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(CHANNELS_PARAM, 1, 16, 16, "Number of output channels");
      
        onReset();
	}

    void process(const ProcessArgs &args) override {
        int chan = ceil(params[CHANNELS_PARAM].getValue());
        outputs[POLY_OUTPUT].setChannels(chan);

        float car = inputs[CAR_INPUT].isConnected() ? clamp(inputs[CAR_INPUT].getVoltage(), 0.f, 10.f) : 10.f;

        if (chan > 1) {
            for (int c = 0; c < 16; c++) {
                temp[c] = 0;
            }

            float split = 10.f / (float)(chan - 1);      
            
            float mod = clamp(inputs[MOD_INPUT].getVoltage(), 0.f, 10.f);
            float mod_p = mod / split;
            int mod_c = floor(mod_p);
            float mod_p2 = mod_p - (float)mod_c;
            float mod_p1 = 1.f - mod_p2;

            temp[mod_c] = mod_p1 * car;
            temp[mod_c + 1] = mod_p2 * car;
            
            for (int c = 0; c < chan; c++) {
                float v = (c < inputs[BASE_INPUT].getChannels()) ? rescale(inputs[BASE_INPUT].getVoltage(c), 0.f, 10.f, 0.f, 1.f) : 1.f; 
                outputs[POLY_OUTPUT].setVoltage(v * temp[c], c);
            }
        } else {
            float v = (inputs[BASE_INPUT].getChannels() > 0) ? rescale(inputs[BASE_INPUT].getVoltage(), 0.f, 10.f, 0.f, 1.f) : 1.f; 
            outputs[POLY_OUTPUT].setVoltage(v * car, 0);
        }

        // Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < 16; c++) {
				bool active = (c < inputs[BASE_INPUT].getChannels());
				lights[INPUT_LIGHTS + c].setBrightness(active);
			}
			for (int c = 0; c < 16; c++) {
				bool active = (c < chan);
				lights[OUTPUT_LIGHTS + c].setBrightness(active);
			}
		}
    }
};

struct RotorAWidget : ModuleWidget {
	RotorAWidget(RotorA *module) {	
		setModule(module);
    	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RotorA.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RoundBlackSnapKnob>(Vec(37.6f, 239.3f), module, RotorA::CHANNELS_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 62.4f), module, RotorA::MOD_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 106.7f), module, RotorA::CAR_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 154.0f), module, RotorA::BASE_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(37.6f, 291.9f), module, RotorA::POLY_OUTPUT));

        PolyLedWidget *w0 = createWidgetCentered<PolyLedWidget>(Vec(37.6, 185.0f));
        w0->setModule(module, RotorA::INPUT_LIGHTS);
        addChild(w0);

        PolyLedWidget *w1 = createWidgetCentered<PolyLedWidget>(Vec(37.6, 322.1f));
        w1->setModule(module, RotorA::OUTPUT_LIGHTS);
        addChild(w1);
    }
};

Model *modelRotorA = createModel<RotorA, RotorAWidget>("RotorA");