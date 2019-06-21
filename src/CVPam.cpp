#include "plugin.hpp"
#include "widgets.hpp"
#include "MapModule.hpp"
#include <chrono>
#include <thread>

static const int MAX_CHANNELS = 32;


struct CVPam : MapModule<MAX_CHANNELS> {
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

  	bool bipolarOutput = false;

	dsp::ClockDivider lightDivider;

	CVPam() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].color = nvgRGB(0x40, 0xff, 0xff);
			paramHandles[id].text = string::f("CV-PAM Ch%02d", id + 1);
		}
		onReset();
		lightDivider.setDivision(1024);
	}

	void process(const ProcessArgs &args) override {		
		int lastChannel_out1 = -1;
		int lastChannel_out2 = -1;

		// Step channels
		for (int id = 0; id < mapLen; id++) {
			lastChannel_out1 = id < 16 ? id : lastChannel_out1;
			lastChannel_out2 = id >= 16 ? id - 16 : lastChannel_out2;
			
			ParamQuantity *paramQuantity = getParamQuantity(id);
			if (paramQuantity == NULL) continue;
			// set voltage
			float v = paramQuantity->getScaledValue();
			v = valueFilters[id].process(args.sampleTime, v);
			v = rescale(v, 0.f, 1.f, 0.f, 10.f);
			if (bipolarOutput)
	  			v -= 5.f;
			if (id < 16) 
				outputs[POLY_OUTPUT1].setVoltage(v, id); 
			else 
				outputs[POLY_OUTPUT2].setVoltage(v, id - 16);			
		}
		
		outputs[POLY_OUTPUT1].setChannels(lastChannel_out1 + 1);
		outputs[POLY_OUTPUT2].setChannels(lastChannel_out2 + 1);

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

		MapModule::process(args);
	}

	json_t *dataToJson() override {
		json_t *rootJ = MapModule::dataToJson();
		json_object_set_new(rootJ, "bipolarOutput", json_boolean(bipolarOutput));

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		MapModule::dataFromJson(rootJ);

		json_t *bipolarOutputJ = json_object_get(rootJ, "bipolarOutput");
		bipolarOutput = json_boolean_value(bipolarOutputJ);
	}
};


struct CVPamWidget : ModuleWidget {
	CVPamWidget(CVPam *module) {	
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV-Pam.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = 9.f;
		float v = 13.5f;
		float d = 6.8f;
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(o, 21.1)), module, CVPam::POLY_OUTPUT1));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(o + d + v + 12.4f, 21.1)), module, CVPam::POLY_OUTPUT2));

		PolyLedWidget *w0 = createWidget<PolyLedWidget>(mm2px(Vec(o + d, 17.975)));
		w0->setModule(module, CVPam::CHANNEL_LIGHTS1);
		addChild(w0);

		PolyLedWidget *w1 = createWidget<PolyLedWidget>(mm2px(Vec(o + d + v, 17.975)));
		w1->setModule(module, CVPam::CHANNEL_LIGHTS2);
		addChild(w1);

		MapModuleDisplay<MAX_CHANNELS> *mapWidget = createWidget<MapModuleDisplay<MAX_CHANNELS>>(mm2px(Vec(3.41891, 29.f)));
		mapWidget->box.size = mm2px(Vec(43.999, 91));
		mapWidget->setModule(module);
		addChild(mapWidget);
	}


	void appendContextMenu(Menu *menu) override {
		CVPam *module = dynamic_cast<CVPam*>(this->module);
		assert(module);

        struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/CVPam.md");
                t.detach();
            }
        };

        menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());

		struct UniBiItem : MenuItem {
			CVPam *module;

			void onAction(const event::Action &e) override {
				module->bipolarOutput ^= true;
			}

			void step() override {
				rightText = module->bipolarOutput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		struct TextScrollItem : MenuItem {
			CVPam *module;

			void onAction(const event::Action &e) override {
				module->textScrolling ^= true;
			}

			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Signal output", &UniBiItem::module, module));
		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
  	}
};


Model *modelCVPam = createModel<CVPam, CVPamWidget>("CVPam");
