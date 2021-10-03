#include "plugin.hpp"
#include "IntermixBase.hpp"
#include "components/Knobs.hpp"

namespace StoermelderPackOne {
namespace Intermix {

enum class FADE {
	INOUT = 0,
	IN = 1,
	OUT = 2
};

template<int PORTS>
struct IntermixFadeModule : Module {
	enum ParamIds {
		ENUMS(PARAM_FADE, PORTS),
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_IN,
		LIGHT_OUT,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	int input;
	/** [Stored to JSON] */
	FADE fade;

	dsp::ClockDivider sceneDivider;
	dsp::ClockDivider lightDivider;

	IntermixFadeModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < PORTS; i++) {
			configParam(PARAM_FADE + i, 0.f, 15.f, 1.f, "Fade", "s");
		}
		onReset();
		sceneDivider.setDivision(64);
		lightDivider.setDivision(512);
	}

	void onReset() override {
		input = 0;
		fade = FADE::INOUT;
	}

	void process(const ProcessArgs& args) override {
		// Expander
		Module* exp = leftExpander.module;
		if (!exp || (exp->model != modelIntermix && exp->model != modelIntermixGate && exp->model != modelIntermixEnv && exp->model != modelIntermixFade) || !exp->rightExpander.consumerMessage) return;
		IntermixBase<PORTS>* module = reinterpret_cast<IntermixBase<PORTS>*>(exp->rightExpander.consumerMessage);
		rightExpander.producerMessage = module;
		rightExpander.messageFlipRequested = true;

		// DSP
		if (sceneDivider.process()) {
			float v[PORTS];
			for (int i = 0; i < PORTS; i++) {
				v[i] = params[PARAM_FADE + i].getValue();
			}
			module->expSetFade(input, fade == FADE::IN || fade == FADE::INOUT ? v : NULL, fade == FADE::OUT || fade == FADE::INOUT ? v : NULL);
		}

		// Lights
		if (lightDivider.process()) {
			lights[LIGHT_IN].setBrightness(fade == FADE::IN || fade == FADE::INOUT);
			lights[LIGHT_OUT].setBrightness(fade == FADE::OUT || fade == FADE::INOUT);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "input", json_integer(input));
		json_object_set_new(rootJ, "fade", json_integer((int)fade));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		input = json_integer_value(json_object_get(rootJ, "input"));
		fade = (FADE)json_integer_value(json_object_get(rootJ, "fade"));
	}
};


template<int PORTS>
struct InputLedDisplay : StoermelderPackOne::StoermelderLedDisplay {
	IntermixFadeModule<PORTS>* module;

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
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(string::f("%02u", i + 1), &module->input, i));
		}
	}
};


struct IntermixFadeWidget : ThemedModuleWidget<IntermixFadeModule<8>> {
	const static int PORTS = 8;

	IntermixFadeWidget(IntermixFadeModule<PORTS>* module)
		: ThemedModuleWidget<IntermixFadeModule<8>>(module, "IntermixFade") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float yMin = 53.0f;
		float yMax = 264.3f;
    
		for (int i = 0; i < PORTS; i++) {
			Vec vo1 = Vec(22.5f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addParam(createParamCentered<StoermelderTrimpot>(vo1, module, IntermixFadeModule<PORTS>::PARAM_FADE + i));
		}

		InputLedDisplay<PORTS>* ledDisplay = createWidgetCentered<InputLedDisplay<PORTS>>(Vec(29.1f, 294.1f));
		ledDisplay->module = module;
		addChild(ledDisplay);

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(29.7f, 315.5f), module, IntermixFadeModule<PORTS>::LIGHT_IN));
		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(29.7f, 332.9f), module, IntermixFadeModule<PORTS>::LIGHT_OUT));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<IntermixFadeModule<PORTS>>::appendContextMenu(menu);
		IntermixFadeModule<PORTS>* module = dynamic_cast<IntermixFadeModule<PORTS>*>(this->module);

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Mode"));
		menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("In & Out", &module->fade, FADE::INOUT));
		menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("In", &module->fade, FADE::IN));
		menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Out", &module->fade, FADE::OUT));
	};
};

} // namespace Intermix
} // namespace StoermelderPackOne

Model* modelIntermixFade = createModel<StoermelderPackOne::Intermix::IntermixFadeModule<8>, StoermelderPackOne::Intermix::IntermixFadeWidget>("IntermixFade");