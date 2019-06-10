#include "plugin.hpp"


struct RePlay : Module {
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

    RePlay() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); 
    }

    void process(const ProcessArgs &args) override { 

    }
};


struct RePlayWidget : ModuleWidget {
    RePlayWidget(RePlay *module) {	

    }
};


Model *modelRePlay = createModel<RePlay, RePlayWidget>("RePlay");