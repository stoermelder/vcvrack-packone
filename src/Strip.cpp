#include "plugin.hpp"
#include <thread>


const int STRIP_ONMODE_DEFAULT = 0;
const int STRIP_ONMODE_TOGGLE = 1;

struct Strip : Module {
	enum ParamIds {
        ON_PARAM,
        OFF_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
        ON_INPUT,
        OFF_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

    /** [Stored to JSON] */

    int onMode = STRIP_ONMODE_DEFAULT;
    bool lastState = false;

	dsp::SchmittTrigger onTrigger;
    dsp::SchmittTrigger offPTrigger;

	Strip() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void process(const ProcessArgs &args) override {
        if (offPTrigger.process(params[OFF_PARAM].getValue() + inputs[OFF_INPUT].getVoltage())) {
            traverseDisable(true);
        }
        if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage())) {
            traverseDisable(onMode == STRIP_ONMODE_DEFAULT ? false : !lastState);
        }
	}

    void traverseDisable(bool val) {
        lastState = val;
        Module *m = this;
        while (m) {
            if (m->rightExpander.moduleId < 0) break;
            m->rightExpander.module->bypass = val;
            m = m->rightExpander.module;
        }
        m = this;
        while (m) {
            if (m->leftExpander.moduleId < 0) break;
            m->leftExpander.module->bypass = val;
            m = m->leftExpander.module;
        }
    }

    json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "onMode", json_boolean(onMode));

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *onModeJ = json_object_get(rootJ, "onMode");
		onMode = json_boolean_value(onModeJ);
	}
};


struct StripWidget : ModuleWidget {
	StripWidget(Strip *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Strip.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 59.3f), module, Strip::ON_INPUT));
        addParam(createParamCentered<TL1105>(Vec(22.5f, 82.6f), module, Strip::ON_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 126.1f), module, Strip::OFF_INPUT));
        addParam(createParamCentered<TL1105>(Vec(22.5f, 149.4f), module, Strip::OFF_PARAM));
	}

	
	void appendContextMenu(Menu *menu) override {
		Strip *module = dynamic_cast<Strip*>(this->module);
		assert(module);

        struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Strip.md");
                t.detach();
            }
        };

        menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
        menu->addChild(new MenuSeparator());

		struct OnModeMenuItem : MenuItem {
			Strip *module;

			void onAction(const event::Action &e) override {
				module->onMode ^= true;
			}

			void step() override {
				rightText = module->onMode == STRIP_ONMODE_DEFAULT ? "Default" : "Toggle";
				MenuItem::step();
			}
		};

        OnModeMenuItem *onModeMenuItem = construct<OnModeMenuItem>(&MenuItem::text, "ON mode", &OnModeMenuItem::module, module);
        menu->addChild(onModeMenuItem);
  	}
};


Model *modelStrip = createModel<Strip, StripWidget>("Strip");