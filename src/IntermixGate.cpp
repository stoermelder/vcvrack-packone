#include "plugin.hpp"
#include "IntermixBase.hpp"

namespace StoermelderPackOne {
namespace Intermix {

template<int PORTS>
struct IntermixGateModule : Module {
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

	IntermixGateModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void process(const ProcessArgs& args) override {
		// Expander
		Module* exp = leftExpander.module;
		if (!exp || (exp->model != modelIntermix && exp->model != modelIntermixGate && exp->model != modelIntermixEnv && exp->model != modelIntermixFade) || !exp->rightExpander.consumerMessage) return;
		IntermixBase<PORTS>* module = reinterpret_cast<IntermixBase<PORTS>*>(exp->rightExpander.consumerMessage);
		rightExpander.producerMessage = module;
		rightExpander.messageFlipRequested = true;

		// DSP
		auto currentMatrix = module->expGetCurrentMatrix();
		simd::float_4 out[PORTS / 4] = {};
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j += 4) {
				simd::float_4 v1 = simd::float_4::load(&currentMatrix[i][j]);
				out[j / 4] += v1;
			}
		}

		for (int i = 0; i < PORTS; i++) {
			outputs[OUTPUT + i].setVoltage(out[i / 4][i % 4] > 0.f ? 10.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};

struct IntermixGateWidget : ThemedModuleWidget<IntermixGateModule<8>> {
	const static int PORTS = 8;

	IntermixGateWidget(IntermixGateModule<PORTS>* module)
		: ThemedModuleWidget<IntermixGateModule<8>>(module, "IntermixGate") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float yMin = 53.0f;
		float yMax = 264.3f;
    
		for (int i = 0; i < PORTS; i++) {
			Vec vo1 = Vec(22.5f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addOutput(createOutputCentered<StoermelderPort>(vo1, module, IntermixGateModule<PORTS>::OUTPUT + i));
		}
	}
};

} // namespace Intermix
} // namespace StoermelderPackOne

Model* modelIntermixGate = createModel<StoermelderPackOne::Intermix::IntermixGateModule<8>, StoermelderPackOne::Intermix::IntermixGateWidget>("IntermixGate");