#include "plugin.hpp"
#include "MapModuleBase.hpp"
#include <chrono>

namespace StoermelderPackOne {
namespace CVPam {

static const int MAX_CHANNELS = 32;

struct CVPamModule : MapModuleBase<MAX_CHANNELS> {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT1,
		POLY_OUTPUT2,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHANNEL_LIGHTS1, 16),
		ENUMS(CHANNEL_LIGHTS2, 16),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	bool bipolarOutput;
	/** [Stored to JSON] */
	bool audioRate;
	/** [Stored to JSON] */
	bool locked;
	
	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;

	CVPamModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configOutput(POLY_OUTPUT1, "Polyphonic");
		outputInfos[POLY_OUTPUT1]->description = "Slots 1-16";
		configOutput(POLY_OUTPUT2, "Polyphonic");
		outputInfos[POLY_OUTPUT2]->description = "Slots 17-32";
		this->mappingIndicatorColor = nvgRGB(0x40, 0xff, 0xff);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].text = string::f("CV-PAM Ch%02d", id + 1);
		}
		onReset();
		processDivider.setDivision(32);
		lightDivider.setDivision(1024);
	}

	void onReset() override {
		bipolarOutput = false;
		audioRate = true;
		locked = false;
		MapModuleBase<MAX_CHANNELS>::onReset();
	}

	void process(const ProcessArgs& args) override {
		if (audioRate || processDivider.process()) {
			int channelCount1 = 0;
			int channelCount2 = 0;

			// Step channels
			for (int i = 0; i < mapLen; i++) {
				ParamQuantity* paramQuantity = getParamQuantity(i);
				if (!paramQuantity) continue;

				if (i < 16)
					channelCount1 = i + 1;
				if (i >= 16)
					channelCount2 = i - 16 + 1;

				// Set voltage
				float v = paramQuantity->getScaledValue();
				v = valueFilters[i].process(args.sampleTime, v);
				v = rescale(v, 0.f, 1.f, 0.f, 10.f);
				if (bipolarOutput)
					v -= 5.f;
				if (i < 16) 
					outputs[POLY_OUTPUT1].setVoltage(v, i);
				else 
					outputs[POLY_OUTPUT2].setVoltage(v, i - 16);
			}
			
			outputs[POLY_OUTPUT1].setChannels(channelCount1);
			outputs[POLY_OUTPUT2].setChannels(channelCount2);
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < 16; c++) {
				bool active = (c < outputs[POLY_OUTPUT1].getChannels());
				lights[CHANNEL_LIGHTS1 + c].setBrightness(active);
			}
			for (int c = 0; c < 16; c++) {
				bool active = (c < outputs[POLY_OUTPUT2].getChannels());
				lights[CHANNEL_LIGHTS2 + c].setBrightness(active);
			}
		}

		MapModuleBase::process(args);
	}

	json_t* dataToJson() override {
		json_t* rootJ = MapModuleBase::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "bipolarOutput", json_boolean(bipolarOutput));
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));
		json_object_set_new(rootJ, "locked", json_boolean(locked));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		MapModuleBase::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* bipolarOutputJ = json_object_get(rootJ, "bipolarOutput");
		bipolarOutput = json_boolean_value(bipolarOutputJ);
		json_t* audioRateJ = json_object_get(rootJ, "audioRate");
		if (audioRateJ) audioRate = json_boolean_value(audioRateJ);
		json_t* lockedJ = json_object_get(rootJ, "locked");
		if (lockedJ) locked = json_boolean_value(lockedJ);
	}
};


struct CVPamWidget : ThemedModuleWidget<CVPamModule> {
	CVPamWidget(CVPamModule* module)
		: ThemedModuleWidget<CVPamModule>(module, "CVPam") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(createOutputCentered<StoermelderPort>(Vec(26.9f, 60.8f), module, CVPamModule::POLY_OUTPUT1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(123.1f, 60.8f), module, CVPamModule::POLY_OUTPUT2));

		PolyLedWidget<>* w0 = createWidgetCentered<PolyLedWidget<>>(Vec(54.2f, 60.8f));
		w0->setModule(module, CVPamModule::CHANNEL_LIGHTS1);
		addChild(w0);

		PolyLedWidget<>* w1 = createWidgetCentered<PolyLedWidget<>>(Vec(95.8f, 60.8f));
		w1->setModule(module, CVPamModule::CHANNEL_LIGHTS2);
		addChild(w1);

		typedef MapModuleDisplay<MAX_CHANNELS, CVPamModule> TMapDisplay;
		TMapDisplay* mapWidget = createWidget<TMapDisplay>(Vec(10.6f, 81.5f));
		mapWidget->box.size = Vec(128.9f, 261.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}


	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<CVPamModule>::appendContextMenu(menu);
		CVPamModule* module = dynamic_cast<CVPamModule*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());
		menu->addChild(createIndexPtrSubmenuItem("Signal output", {"0V..10V", "-5V..5V"}, &module->bipolarOutput));
		menu->addChild(createBoolPtrMenuItem("Audio rate processing", "", &module->audioRate));
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Text scrolling", "", &module->textScrolling));
		menu->addChild(createBoolPtrMenuItem("Hide mapping indicators", "", &module->mappingIndicatorHidden));
		menu->addChild(createBoolPtrMenuItem("Lock mapping slots", "", &module->locked));
	}
};

} // namespace CVPam
} // namespace StoermelderPackOne

Model* modelCVPam = createModel<StoermelderPackOne::CVPam::CVPamModule, StoermelderPackOne::CVPam::CVPamWidget>("CVPam");
