#include "plugin.hpp"
#include "widgets.hpp"
#include "CVMapModule.hpp"
#include <chrono>
#include <thread>

static const int MAX_CHANNELS = 32;
static const float UINIT = 0;

struct CVMap : CVMapModule<MAX_CHANNELS> {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		POLY_INPUT1,
		POLY_INPUT2,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHANNEL_LIGHTS1, 16),
		ENUMS(CHANNEL_LIGHTS2, 16),
		NUM_LIGHTS
	};

	CVMap() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			MapModule<MAX_CHANNELS>::paramHandles[id].text = string::f("CV-MAP Ch%02d", id + 1);
		}
	}

	void process(const ProcessArgs &args) override {
		// Step channels
		for (int id = 0; id < mapLen; id++) {
			if (id < 16) {
				// Skip unused channels on INPUT1
				if (inputs[POLY_INPUT1].getChannels() == id) {
					id = 15;
					continue;
				}
			} else {
				// Skip unused channels on INPUT2
				if (inputs[POLY_INPUT2].getChannels() == id - 16) {
					break;
				}
			}

			ParamQuantity *paramQuantity = getParamQuantity(id);
			if (paramQuantity == NULL) continue;
			// Set ParamQuantity
			float v = id < 16 ? inputs[POLY_INPUT1].getVoltage(id) : inputs[POLY_INPUT2].getVoltage(id - 16);
			if (bipolarInput)
				v += 5.f;
			v = rescale(v, 0.f, 10.f, 0.f, 1.f);
			v = valueFilters[id].process(args.sampleTime, v);

			// If lastValue is unitialized set it to its current value, only executed once
			if (lastValue[id] == UINIT) {
				lastValue[id] = v;
			}

			if (lockParameterChanges || lastValue[id] != v) {
				paramQuantity->setScaledValue(v);
				lastValue[id] = v;					
			}
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < 16; c++) {
				bool active = (c < inputs[POLY_INPUT1].getChannels());
				lights[CHANNEL_LIGHTS1 + c].setBrightness(active);
			}
			for (int c = 0; c < 16; c++) {
				bool active = (c < inputs[POLY_INPUT2].getChannels());
				lights[CHANNEL_LIGHTS2 + c].setBrightness(active);
			}
		}
		
		CVMapModule<MAX_CHANNELS>::process(args);
	}
};


struct CVMapWidget : ModuleWidget {
	CVMapWidget(CVMap *module) {	
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV-Map.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = 9.f;
		float v = 13.5f;
		float d = 6.8f;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(o, 21.1)), module, CVMap::POLY_INPUT1));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(o + d + v + 12.4f, 21.1)), module, CVMap::POLY_INPUT2));

		PolyLedWidget *w0 = createWidget<PolyLedWidget>(mm2px(Vec(o + d, 17.975)));
		w0->setModule(module, CVMap::CHANNEL_LIGHTS1);
		addChild(w0);

		PolyLedWidget *w1 = createWidget<PolyLedWidget>(mm2px(Vec(o + d + v, 17.975)));
		w1->setModule(module, CVMap::CHANNEL_LIGHTS2);
		addChild(w1);

		MapModuleDisplay<MAX_CHANNELS> *mapWidget = createWidget<MapModuleDisplay<MAX_CHANNELS>>(mm2px(Vec(3.41891, 29.f)));
		mapWidget->box.size = mm2px(Vec(43.999, 91));
		mapWidget->setModule(module);
		addChild(mapWidget);
	}


	void appendContextMenu(Menu *menu) override {
		CVMap *module = dynamic_cast<CVMap*>(this->module);
		assert(module);

        struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/CVMap.md");
                t.detach();
            }
        };

        menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
        menu->addChild(new MenuSeparator());

		struct LockItem : MenuItem {
			CVMap *module;

			void onAction(const event::Action &e) override {
				module->lockParameterChanges ^= true;
			}

			void step() override {
				rightText = module->lockParameterChanges ? "Locked" : "Unlocked";
				MenuItem::step();
			}
		};

		struct UniBiItem : MenuItem {
			CVMap *module;

			void onAction(const event::Action &e) override {
				module->bipolarInput ^= true;
			}

			void step() override {
				rightText = module->bipolarInput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		struct TextScrollItem : MenuItem {
			CVMap *module;

			void onAction(const event::Action &e) override {
				module->textScrolling ^= true;
			}

			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<LockItem>(&MenuItem::text, "Parameter changes", &LockItem::module, module));
		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Signal input", &UniBiItem::module, module));
	}
};


Model *modelCVMap = createModel<CVMap, CVMapWidget>("CVMap");
