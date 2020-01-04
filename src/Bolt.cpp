#include "plugin.hpp"


namespace Bolt {

const int BOLT_OP_AND = 0;
const int BOLT_OP_NOR = 1;
const int BOLT_OP_XOR = 2;
const int BOLT_OP_OR = 3;
const int BOLT_OP_NAND = 4;

const int BOLT_OPCV_MODE_10V = 0;
const int BOLT_OPCV_MODE_C4 = 1;
const int BOLT_OPCV_MODE_TRIG = 2;

const int BOLT_OUTCV_MODE_GATE = 0;
const int BOLT_OUTCV_MODE_TRIG_HIGH = 1;
const int BOLT_OUTCV_MODE_TRIG_CHANGE = 2;


struct BoltModule : Module {
	enum ParamIds {
		OP_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TRIG_INPUT,
		OP_INPUT,
		ENUMS(IN, 4),
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(OP_LIGHTS, 5),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	int op = 0;
	int opCvMode = BOLT_OPCV_MODE_10V;
	int outCvMode = BOLT_OUTCV_MODE_GATE;

	bool out[16];

	dsp::SchmittTrigger trigTrigger[16];
	dsp::SchmittTrigger opButtonTrigger;
	dsp::SchmittTrigger opCvTrigger;
	dsp::PulseGenerator outPulseGenerator[16];

    dsp::ClockDivider lightDivider;

    BoltModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();

		configParam(OP_PARAM, 0.0f, 1.0f, 0.0f, "Next operator");
		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		op = 0;
		for (int c = 0; c < 16; c++) {
			out[c] = false;
			outPulseGenerator[c].reset();
		}
	}

	void process(const ProcessArgs &args) override {
		// OP-button
		if (opButtonTrigger.process(params[OP_PARAM].getValue())) {
			op = (op + 1) % 5;
		}

		// OP-input, monophonic
		if (inputs[OP_INPUT].isConnected()) {
            switch (opCvMode) {
                case BOLT_OPCV_MODE_10V: 
                    op = std::min((int)clamp(inputs[OP_INPUT].getVoltage(), 0.f, 10.f) / 2, 4);
                    break;
                case BOLT_OPCV_MODE_C4:
                    op = round(clamp(inputs[OP_INPUT].getVoltage() * 12.f, 0.f, 4.f));
                    break;
                case BOLT_OPCV_MODE_TRIG:
                    if (opCvTrigger.process(inputs[OP_INPUT].getVoltage()))
                        op = (op + 1) % 5;
                    break;
            }
        }

        if (outputs[OUTPUT].isConnected()) {
            int maxChannels = 0;
            // Get the maximum number of channels on any input port to set the output port correctly
            for (int i = 0; i < 4; i++) {
                maxChannels = std::max(maxChannels, inputs[IN + i].getChannels());
            }
            outputs[OUTPUT].setChannels(maxChannels);

            for (int c = 0; c < maxChannels; c++) {
                bool b = out[c];
                if (inputs[TRIG_INPUT].getChannels() > c) {
                    // if trigger-channel is connected update voltage on trigger
                    if (trigTrigger[c].process(inputs[TRIG_INPUT].getVoltage(c))) {
                        b = getOutValue(c);
                    }
                }
                else {
                    b = getOutValue(c);
                }
                switch (outCvMode) {
                    case BOLT_OUTCV_MODE_GATE:
                        out[c] = b;
                        outputs[OUTPUT].setVoltage(out[c] ? 10.f : 0.f, c);
                        break;
                    case BOLT_OUTCV_MODE_TRIG_HIGH:
                        if (b && !out[c]) outPulseGenerator[c].trigger();
                        out[c] = b;
                        outputs[OUTPUT].setVoltage(outPulseGenerator[c].process(args.sampleTime) ? 10.f : 0.f, c);
                        break;
                    case BOLT_OUTCV_MODE_TRIG_CHANGE:
                        if (b != out[c]) outPulseGenerator[c].trigger();
                        out[c] = b;
                        outputs[OUTPUT].setVoltage(outPulseGenerator[c].process(args.sampleTime) ? 10.f : 0.f, c);
                        break;
                }            
            }
        }

        if (lightDivider.process()) {
			for (int c = 0; c < 5; c++) {
				lights[OP_LIGHTS + c].setBrightness(op == c);
			}
		}
    }

    bool getOutValue(int c) {
        int h = 0;
        bool o = false;
        switch (op) {
            case BOLT_OP_AND:
                o = true;
                for (int i = 0; i < 4; i++) {
                    if (inputs[IN + i].getChannels() > c)
                        o = o && (inputs[IN + i].getVoltage(c) >= 1.0f);
                }
                break;

            case BOLT_OP_NOR:
                o = false;
                for (int i = 0; i < 4; i++) {
                    if (inputs[IN + i].getChannels() > c)
                        o = o || (inputs[IN + i].getVoltage(c) >= 1.0f);
                }
                o = !o;
                break;

            case BOLT_OP_XOR:
                for (int i = 0; i < 4; i++) {
                    if (inputs[IN + i].getChannels() > c)
                        h += (inputs[IN + i].getVoltage(c) >= 1.0f);
                }
                o = h % 2 == 1;
                break;

            case BOLT_OP_OR:
                o = false;
                for (int i = 0; i < 4; i++) {
                    if (inputs[IN + i].getChannels() > c)
                        o = o || (inputs[IN + i].getVoltage(c) >= 1.0f);
                }
                break;

            case BOLT_OP_NAND:
                o = true;
                for (int i = 0; i < 4; i++) {
                    if (inputs[IN + i].getChannels() > c)
                        o = o && (inputs[IN + i].getVoltage(c) >= 1.0f);
                }
                o = !o;
                break;
        }

        return o;
    }

    json_t *dataToJson() override {
        json_t *rootJ = json_object();
        json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "op", json_integer(op));
        json_object_set_new(rootJ, "opCvMode", json_integer(opCvMode));
        json_object_set_new(rootJ, "outCvMode", json_integer(outCvMode));
        return rootJ;
    }

    void dataFromJson(json_t *rootJ) override {
        panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
        json_t *opJ = json_object_get(rootJ, "op");
		op = json_integer_value(opJ);
        json_t *opCvModeJ = json_object_get(rootJ, "opCvMode");
		opCvMode = json_integer_value(opCvModeJ);
        json_t *outCvModeJ = json_object_get(rootJ, "outCvMode");
		outCvMode = json_integer_value(outCvModeJ);
    }
};


struct BoltOpCvModeMenuItem : MenuItem {
    struct BoltOpCvModeItem : MenuItem {
        BoltModule *module;
        int opCvMode;

        void onAction(const event::Action &e) override {
            module->opCvMode = opCvMode;
        }

        void step() override {
            rightText = module->opCvMode == opCvMode ? "✔" : "";
            MenuItem::step();
        }
    };
    
    BoltModule *module;
    Menu *createChildMenu() override {
        Menu *menu = new Menu;
        menu->addChild(construct<BoltOpCvModeItem>(&MenuItem::text, "0..10V", &BoltOpCvModeItem::module, module, &BoltOpCvModeItem::opCvMode, BOLT_OPCV_MODE_10V));
        menu->addChild(construct<BoltOpCvModeItem>(&MenuItem::text, "C4-E4", &BoltOpCvModeItem::module, module, &BoltOpCvModeItem::opCvMode, BOLT_OPCV_MODE_C4));
        menu->addChild(construct<BoltOpCvModeItem>(&MenuItem::text, "Trigger", &BoltOpCvModeItem::module, module, &BoltOpCvModeItem::opCvMode, BOLT_OPCV_MODE_TRIG));
        return menu;
    }
};

struct BoltOutCvModeMenuItem : MenuItem {
    struct BoltOutCvModeItem : MenuItem {
        BoltModule *module;
        int outCvMode;

        void onAction(const event::Action &e) override {
            module->outCvMode = outCvMode;
        }

        void step() override {
            rightText = module->outCvMode == outCvMode ? "✔" : "";
            MenuItem::step();
        }
    };
    
    BoltModule *module;
    Menu *createChildMenu() override {
        Menu *menu = new Menu;
        menu->addChild(construct<BoltOutCvModeItem>(&MenuItem::text, "Gate", &BoltOutCvModeItem::module, module, &BoltOutCvModeItem::outCvMode, BOLT_OUTCV_MODE_GATE));
        menu->addChild(construct<BoltOutCvModeItem>(&MenuItem::text, "Trigger on high", &BoltOutCvModeItem::module, module, &BoltOutCvModeItem::outCvMode, BOLT_OUTCV_MODE_TRIG_HIGH));
        menu->addChild(construct<BoltOutCvModeItem>(&MenuItem::text, "Trigger on change", &BoltOutCvModeItem::module, module, &BoltOutCvModeItem::outCvMode, BOLT_OUTCV_MODE_TRIG_CHANGE));
        return menu;
    }
};


struct BoltWidget : ThemedModuleWidget<BoltModule> {
	BoltWidget(BoltModule *module)
        : ThemedModuleWidget<BoltModule>(module, "Bolt") {
		setModule(module);

        addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 60.3f), module, BoltModule::TRIG_INPUT));
        addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 102.1f), module, BoltModule::OP_INPUT));
        addParam(createParamCentered<TL1105>(Vec(22.5f, 125.4f), module, BoltModule::OP_PARAM));

        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(11.9f, 146.3f), module, BoltModule::OP_LIGHTS + 0));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(11.9f, 156.0f), module, BoltModule::OP_LIGHTS + 1));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(11.9f, 165.7f), module, BoltModule::OP_LIGHTS + 2));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(11.9f, 175.3f), module, BoltModule::OP_LIGHTS + 3));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(11.9f, 185.0f), module, BoltModule::OP_LIGHTS + 4));

        addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 209.1f), module, BoltModule::IN + 0));
        addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 236.7f), module, BoltModule::IN + 1));
        addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 264.2f), module, BoltModule::IN + 2));
        addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 291.7f), module, BoltModule::IN + 3));
        addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, BoltModule::OUTPUT));
    }

    void appendContextMenu(Menu *menu) override {
        ThemedModuleWidget<BoltModule>::appendContextMenu(menu);
        BoltModule *module = dynamic_cast<BoltModule*>(this->module);
        assert(module);

        menu->addChild(new MenuSeparator());

        BoltOpCvModeMenuItem *opCvModeMenuItem = construct<BoltOpCvModeMenuItem>(&MenuItem::text, "Port OP mode", &BoltOpCvModeMenuItem::module, module);
        opCvModeMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(opCvModeMenuItem);

        BoltOutCvModeMenuItem *outCvModeMenuItem = construct<BoltOutCvModeMenuItem>(&MenuItem::text, "Output mode", &BoltOutCvModeMenuItem::module, module);
        outCvModeMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(outCvModeMenuItem);
    }
};

} // namespace Bolt

Model *modelBolt = createModel<Bolt::BoltModule, Bolt::BoltWidget>("Bolt");