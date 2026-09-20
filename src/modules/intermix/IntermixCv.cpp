#include "../../plugin.hpp"
#include "IntermixBase.hpp"

namespace StoermelderPackOne {
namespace Intermix {

template<int PORTS>
struct IntermixCvModule : IntermixChainModule, IntermixCvBase<PORTS> {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT_CV, PORTS),
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	int input = 0;

	// Detects a row change in process(), so IntermixModule's row-indexed
	// cache (cvExpanderByRow) can be notified.
	int lastInput = 0;

	IntermixCvModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < PORTS; i++) {
			configInput(INPUT_CV + i, string::f("Pad %i CV", i + 1));
			inputInfos[INPUT_CV + i]->description = "Overrides the matrix pad at the selected row for this column while connected.\n0..10V maps to a pad value of 0..1.";
		}
	}

	void onReset(const ResetEvent& e) override {
		input = 0;
		Module::onReset(e);
	}

	// Pulled by IntermixModule every sample; not throttled, since CV is audio-rate.
	int getInput() override {
		return input;
	}

	bool isConnected(int j) override {
		return inputs[INPUT_CV + j].isConnected();
	}

	float getValue(int j) override {
		return clamp(inputs[INPUT_CV + j].getVoltage() / 10.f, 0.f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		// A chain sibling was removed: drop forwarded messages, skip this sample
		if (consumeSiblingRemoved()) return;

		if (input != lastInput) {
			lastInput = input;
			// Row changed: tell IntermixModule to rebuild its row cache.
			notifyModuleListeners("Intermix");
		}

		// Forward the chain head's IntermixBase* so any expander further
		// right (e.g. Intermix -> IntermixCv -> IntermixGate) still finds it.
		Module* exp = leftExpander.module;
		if (!exp || !isIntermixModel(exp->model) || !exp->rightExpander.consumerMessage) return;
		IntermixBase<PORTS>* module = reinterpret_cast<IntermixBase<PORTS>*>(exp->rightExpander.consumerMessage);
		rightExpander.producerMessage = module;
		rightExpander.messageFlipRequested = true;
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
		if (inputJ) input = clamp((int)json_integer_value(inputJ), 0, PORTS - 1);
	}
};


struct IntermixCvWidget : ThemedModuleWidget<IntermixCvModule<8>> {
	const static int PORTS = 8;

	IntermixCvWidget(IntermixCvModule<PORTS>* module)
		: ThemedModuleWidget<IntermixCvModule<8>>(module, "IntermixCv") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float yMin = 53.0f;
		float yMax = 264.3f;

		for (int i = 0; i < PORTS; i++) {
			Vec vi1 = Vec(22.5f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addInput(createInputCentered<StoermelderPort>(vi1, module, IntermixCvModule<PORTS>::INPUT_CV + i));
		}

		auto* ledDisplay = createWidgetCentered<InputLedDisplay<IntermixCvModule<PORTS>, PORTS>>(Vec(29.1f, 294.1f));
		ledDisplay->module = module;
		addChild(ledDisplay);
	}
};

} // namespace Intermix
} // namespace StoermelderPackOne

Model* modelIntermixCv = createModel<StoermelderPackOne::Intermix::IntermixCvModule<8>, StoermelderPackOne::Intermix::IntermixCvWidget>("IntermixCv");
