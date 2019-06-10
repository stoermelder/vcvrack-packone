#include "plugin.hpp"
#include <thread>


const int MAX_DATA = 48000 * 8;     // 8 seconds, full precision


struct RePlay : Module {
    enum ParamIds {
        NUM_PARAMS
    };
    enum InputIds {
        PLAY_INPUT,
        RESET_INPUT,
        POS_INPUT,  
        NUM_INPUTS
    };
    enum OutputIds {
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    float data[MAX_DATA];
    int precision = 1;

    RePlay() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); 
    }

    void process(const ProcessArgs &args) override { 

    }

    json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_t *dataJ = json_array();
		for (int i = 0; i < MAX_DATA; i++) {
			json_t *d = json_real(data[i]);
			json_array_append(dataJ, d);
		}
		json_object_set_new(rootJ, "data", dataJ);

		json_t *precisionJ = json_integer(precision);
		json_object_set_new(rootJ, "precision", precisionJ);

		return rootJ;
	}

 	void dataFromJson(json_t *rootJ) override {
		json_t *dataJ = json_object_get(rootJ, "data");
		if (dataJ) {
			json_t *d;
			size_t dataIndex;
			json_array_foreach(dataJ, dataIndex, d) {
                if (dataIndex >= MAX_DATA) continue;
                data[dataIndex] = json_real_value(d);
			}
		}

    	json_t *precisionJ = json_object_get(rootJ, "precision");
		if (precisionJ) precision = json_integer_value(precisionJ);
	}   
};


struct RePlayWidget : ModuleWidget {
    RePlayWidget(RePlay *module) {	
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RePlay.svg")));
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
        menu->addChild(construct<PrecisionItem>(&MenuItem::text, "Eighth", &PrecisionItem::module, module, &PrecisionItem::precision, 8));
    }
};


Model *modelRePlay = createModel<RePlay, RePlayWidget>("RePlay");