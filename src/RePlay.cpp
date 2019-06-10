#include "plugin.hpp"
#include "MapModule.hpp"
#include <thread>


const int MAX_DATA = 48000 * 8;     // 8 seconds, full precision


struct RePlay : MapModule<1> {
    enum ParamIds {
        PLAY_PARAM,
        RESET_PARAM,
        REC_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        PLAY_INPUT,
        RESET_INPUT,
        POS_INPUT,  
        NUM_INPUTS
    };
    enum OutputIds {
        CV_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        REC_LIGHT,
        NUM_LIGHTS
    };

    float data[MAX_DATA];
    int dataLength;

    int precision = 1;

	dsp::ClockDivider lightDivider;

    RePlay() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); 

        paramHandles[0].color = nvgRGB(0x40, 0xff, 0xff);
        paramHandles[0].text = "RePlay";
        APP->engine->addParamHandle(&paramHandles[0]);

        onReset();
		lightDivider.setDivision(1024);
    }

    void process(const ProcessArgs &args) override { 

		// Set channel lights infrequently
		if (lightDivider.process()) {
        }
    }

    json_t *dataToJson() override {
		json_t *rootJ = MapModule::dataToJson();

		json_t *dataJ = json_array();
		for (int i = 0; i < dataLength; i++) {
			json_t *d = json_real(data[i]);
			json_array_append(dataJ, d);
		}
		json_object_set_new(rootJ, "data", dataJ);

        json_object_set_new(rootJ, "dataLength", json_integer(dataLength));
		json_object_set_new(rootJ, "precision", json_integer(precision));

		return rootJ;
	}

 	void dataFromJson(json_t *rootJ) override {
        MapModule::dataFromJson(rootJ);
		
    	json_t *dataLengthJ = json_object_get(rootJ, "dataLength");
		if (dataLengthJ) dataLength = json_integer_value(dataLengthJ);
    	json_t *precisionJ = json_object_get(rootJ, "precision");
		if (precisionJ) precision = json_integer_value(precisionJ);

        json_t *dataJ = json_object_get(rootJ, "data");
		if (dataJ) {
			json_t *d;
			size_t dataIndex;
			json_array_foreach(dataJ, dataIndex, d) {
                if (dataIndex >= MAX_DATA) continue;
                data[dataIndex] = json_real_value(d);
			}
		}
	}   
};


struct RecButton : SvgSwitch {
    RecButton() {
        momentary = true;
        addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RecButton.svg")));
    }
};

struct RecLight : RedLight {
	RecLight() {
		bgColor = nvgRGB(0x66, 0x66, 0x66);
		box.size = Vec(32.9f, 32.9f);
	}
};


struct RePlayWidget : ModuleWidget {
    RePlayWidget(RePlay *module) {	
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RePlay.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 124.7f), module, RePlay::PLAY_INPUT));
        addParam(createParamCentered<TL1105>(Vec(52.6f, 140.9f), module, RePlay::PLAY_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 176.f), module, RePlay::RESET_INPUT));
        addParam(createParamCentered<TL1105>(Vec(52.6f, 194.2f), module, RePlay::RESET_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 229.3f), module, RePlay::POS_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(37.6f, 320.9f), module, RePlay::CV_OUTPUT));

        addParam(createParamCentered<RecButton>(Vec(37.6f, 272.7f), module, RePlay::REC_PARAM));
        addChild(createLightCentered<RecLight>(Vec(37.6f, 272.7f), module, RePlay::REC_LIGHT));

		MapModuleDisplay<1> *mapWidget = createWidget<MapModuleDisplay<1>>(Vec(6.8f, 36.4f));
		mapWidget->box.size = Vec(61.5f, 25.6f);
		mapWidget->setModule(module);
		addChild(mapWidget);
    }

    void appendContextMenu(Menu *menu) override {
        RePlay *module = dynamic_cast<RePlay*>(this->module);
        assert(module);

        struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/RePlay.md");
                t.detach();
            }
        };

        menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
        menu->addChild(construct<MenuLabel>());

        struct PrecisionItem : MenuItem {
            RePlay *module;
            int precision;

            void onAction(const event::Action &e) override {
                module->precision = precision;
            }

            void step() override {
                rightText = (module->precision == precision) ? "✔" : "";
                MenuItem::step();
            }
        };

        menu->addChild(construct<MenuLabel>());
        menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Precision"));
        menu->addChild(construct<PrecisionItem>(&MenuItem::text, "Full", &PrecisionItem::module, module, &PrecisionItem::precision, 1));
        menu->addChild(construct<PrecisionItem>(&MenuItem::text, "Half", &PrecisionItem::module, module, &PrecisionItem::precision, 2));
        menu->addChild(construct<PrecisionItem>(&MenuItem::text, "Quarter", &PrecisionItem::module, module, &PrecisionItem::precision, 4));
        menu->addChild(construct<PrecisionItem>(&MenuItem::text, "8th", &PrecisionItem::module, module, &PrecisionItem::precision, 8));
    }
};


Model *modelRePlay = createModel<RePlay, RePlayWidget>("RePlay");