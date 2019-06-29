#include "plugin.hpp"
#include <thread>

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
            traverseDisable(false);
        }
	}

    void traverseDisable(bool val) {
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
  	}
};


Model *modelStrip = createModel<Strip, StripWidget>("Strip");