#include "../../plugin.hpp"
#include "../../components/Knobs.hpp"
#include "IntermixBase.hpp"

namespace StoermelderPackOne {
namespace Intermix {

enum class FADE {
	INOUT = 0,
	IN = 1,
	OUT = 2
};

template<int PORTS>
struct IntermixFadeModule : IntermixChainModule {
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
	int input = 0;
	/** [Stored to JSON] */
	FADE fade = FADE::INOUT;
	/** [Stored to JSON] */
	FADE_LENGTH fadeLengthMode = FADE_LENGTH_15S;

	ClockDividerEx sceneDivider;
	ClockDividerEx lightDivider;

	IntermixFadeModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < PORTS; i++) {
			auto pq = configParam<FadeLengthParamQuantity<IntermixFadeModule<PORTS>>>(PARAM_FADE + i, 0.f, 15.f, 1.f, "Fade", "s");
			pq->module = this;
			pq->description = string::f("Crossfade time applied to the signal on output %i when scenes change.", i + 1);
		}

		ResetEvent re;
		onReset(re);

		sceneDivider.setDivision(64);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onReset(const ResetEvent& e) override {
		input = 0;
		fade = FADE::INOUT;
		Module::onReset(e);
	}

	void process(const ProcessArgs& args) override {
		// A chain sibling was removed: drop forwarded messages, skip this sample
		if (consumeSiblingRemoved()) return;

		// Expander
		Module* exp = leftExpander.module;
		if (!exp || !isIntermixModel(exp->model) || !exp->rightExpander.consumerMessage) return;
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
		json_object_set_new(rootJ, "fadeLengthMode", json_integer(fadeLengthMode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);
		json_t* inputJ = json_object_get(rootJ, "input");
		if (inputJ) input = clamp((int)json_integer_value(inputJ), 0, PORTS - 1);
		json_t* fadeJ = json_object_get(rootJ, "fade");
		if (fadeJ) fade = (FADE)json_integer_value(fadeJ);
		json_t* fadeLengthModeJ = json_object_get(rootJ, "fadeLengthMode");
		if (fadeLengthModeJ) fadeLengthMode = (FADE_LENGTH)json_integer_value(fadeLengthModeJ);
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

		auto* ledDisplay = createWidgetCentered<InputLedDisplay<IntermixFadeModule<PORTS>, PORTS>>(Vec(29.1f, 294.1f));
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
		menu->addChild(new MenuSeparator);
		menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<FADE_LENGTH>("Fade length",
			{
				{ FADE_LENGTH::FADE_LENGTH_4S, "4s" },
				{ FADE_LENGTH::FADE_LENGTH_15S, "15s" },
				{ FADE_LENGTH::FADE_LENGTH_60S, "60s" }
			},
			[=]() { return module->fadeLengthMode; },
			[=](FADE_LENGTH m) { module->fadeLengthMode = m; }
		));
	};
};

} // namespace Intermix
} // namespace StoermelderPackOne

Model* modelIntermixFade = createModel<StoermelderPackOne::Intermix::IntermixFadeModule<8>, StoermelderPackOne::Intermix::IntermixFadeWidget>("IntermixFade");