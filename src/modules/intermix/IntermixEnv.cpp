#include "../../plugin.hpp"
#include "IntermixBase.hpp"

namespace StoermelderPackOne {
namespace Intermix {

template<int PORTS>
struct IntermixEnvModule : IntermixChainModule {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	int input;

	IntermixEnvModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < PORTS; i++) {
			configOutput(OUTPUT + i, string::f("Envelope %i", i + 1));
			outputInfos[OUTPUT + i]->description = "Scaled 0..10V envelope of the selected input row's mix levels into the matching output column.";
		}

		ResetEvent re;
		onReset(re);
	}

	void onReset(const ResetEvent& e) override {
		input = 0;
		Module::onReset(e);
	}

	void resetOutputs() override {
		for (int i = 0; i < PORTS; i++) {
			outputs[OUTPUT + i].setVoltage(0.f);
		}
	}

	void process(const ProcessArgs& args) override {
		// A chain sibling was removed: drop forwarded messages, outputs go low
		if (consumeSiblingRemoved()) {
			resetOutputs();
			return;
		}

		// Expander
		Module* exp = leftExpander.module;
		if (!exp || (exp->model != modelIntermix && exp->model != modelIntermixGate && exp->model != modelIntermixEnv && exp->model != modelIntermixFade) || !exp->rightExpander.consumerMessage) {
			// Disconnected from the chain: outputs go low
			resetOutputs();
			return;
		}
		IntermixBase<PORTS>* module = reinterpret_cast<IntermixBase<PORTS>*>(exp->rightExpander.consumerMessage);
		rightExpander.producerMessage = module;
		rightExpander.messageFlipRequested = true;
		
		// DSP
		auto currentMatrix = module->expGetCurrentMatrix();
		for (int i = 0; i < PORTS; i++) {
			float v = currentMatrix[input][i];
			outputs[OUTPUT + i].setVoltage(v * 10.f);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "input", json_integer(input));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);
		json_t* inputJ = json_object_get(rootJ, "input");
		if (inputJ) input = json_integer_value(inputJ);
	}
};


template<int PORTS>
struct InputLedDisplay : StoermelderPackOne::StoermelderLedDisplay {
	IntermixEnvModule<PORTS>* module;

	void step() override {
		if (module) {
			text = string::f("%02d", module->input + 1);
		} 
		else {
			text = "";
		}
		StoermelderLedDisplay::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		StoermelderLedDisplay::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("Input"));
		for (int i = 0; i < PORTS; i++) {
			menu->addChild(createCheckMenuItem(string::f("%02u", i + 1), "",
				[=]() { return module->input == i; },
				[=]() { module->input = i; }
			));
		};
	}
};


struct IntermixEnvWidget : ThemedModuleWidget<IntermixEnvModule<8>> {
	const static int PORTS = 8;

	IntermixEnvWidget(IntermixEnvModule<PORTS>* module)
		: ThemedModuleWidget<IntermixEnvModule<8>>(module, "IntermixEnv") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float yMin = 53.0f;
		float yMax = 264.3f;
    
		for (int i = 0; i < PORTS; i++) {
			Vec vo1 = Vec(22.5f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addOutput(createOutputCentered<StoermelderPort>(vo1, module, IntermixEnvModule<PORTS>::OUTPUT + i));
		}

		InputLedDisplay<PORTS>* ledDisplay = createWidgetCentered<InputLedDisplay<PORTS>>(Vec(29.7f, 294.1f));
		ledDisplay->module = module;
		addChild(ledDisplay);
	}
};

} // namespace Intermix
} // namespace StoermelderPackOne

Model* modelIntermixEnv = createModel<StoermelderPackOne::Intermix::IntermixEnvModule<8>, StoermelderPackOne::Intermix::IntermixEnvWidget>("IntermixEnv");