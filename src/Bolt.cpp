#include "plugin.hpp"
#include "components/Knobs.hpp"

namespace StoermelderPackOne {
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
		configInput(TRIG_INPUT, "Trigger");
		inputInfos[TRIG_INPUT]->description = "Optional, takes a new sample from the logic inputs.";
		configInput(OP_INPUT, "Operator trigger");
		inputInfos[OP_INPUT]->description = "Switches to the next logic mode.";
		configInput(IN + 0, "Logic 1");
		configInput(IN + 1, "Logic 2");
		configInput(IN + 2, "Logic 3");
		configInput(IN + 3, "Logic 4");
		configOutput(OUTPUT, "Logic");

		configParam<TriggerParamQuantity>(OP_PARAM, 0.0f, 1.0f, 0.0f, "Next operator");
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

	void process(const ProcessArgs& args) override {
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

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "op", json_integer(op));
		json_object_set_new(rootJ, "opCvMode", json_integer(opCvMode));
		json_object_set_new(rootJ, "outCvMode", json_integer(outCvMode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		json_t* opJ = json_object_get(rootJ, "op");
		op = json_integer_value(opJ);
		json_t* opCvModeJ = json_object_get(rootJ, "opCvMode");
		opCvMode = json_integer_value(opCvModeJ);
		json_t* outCvModeJ = json_object_get(rootJ, "outCvMode");
		outCvMode = json_integer_value(outCvModeJ);
	}
};


struct BoltWidget : ThemedModuleWidget<BoltModule> {
	BoltWidget(BoltModule* module)
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

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<BoltModule>::appendContextMenu(menu);
		BoltModule* module = dynamic_cast<BoltModule*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());
		menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem<int>("Port OP mode",
			{
				{ BOLT_OPCV_MODE_10V, "0..10V" },
				{ BOLT_OPCV_MODE_C4, "C4-E4" },
				{ BOLT_OPCV_MODE_TRIG, "Trigger" }
			},
			&module->opCvMode
		));
		menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem<int>("Output mode",
			{
				{ BOLT_OUTCV_MODE_GATE, "Gate" },
				{ BOLT_OUTCV_MODE_TRIG_HIGH, "Trigger on high" },
				{ BOLT_OUTCV_MODE_TRIG_CHANGE, "Trigger on change" }
			},
			&module->outCvMode
		));
	}
};

} // namespace Bolt
} // namespace StoermelderPackOne

Model* modelBolt = createModel<StoermelderPackOne::Bolt::BoltModule, StoermelderPackOne::Bolt::BoltWidget>("Bolt");