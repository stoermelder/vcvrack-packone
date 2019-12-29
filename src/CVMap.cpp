#include "plugin.hpp"
#include "MapModule.hpp"
#include <chrono>
#include <thread>

namespace CVMap {

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

	/** [Stored to Json] */
	bool audioRate;

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;

	CVMap() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < MAX_CHANNELS; i++) {
			MapModule<MAX_CHANNELS>::paramHandles[i].text = string::f("CV-MAP Ch%02d", i + 1);
		}
		processDivider.setDivision(32);
		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		audioRate = true;
		CVMapModule<MAX_CHANNELS>::onReset();
	}

	void process(const ProcessArgs& args) override {
		if (audioRate || processDivider.process()) {
			// Step channels
			for (int i = 0; i < mapLen; i++) {
				if (i < 16) {
					// Skip unused channels on INPUT1
					if (inputs[POLY_INPUT1].getChannels() == i) {
						i = 15;
						continue;
					}
				} else {
					// Skip unused channels on INPUT2
					if (inputs[POLY_INPUT2].getChannels() == i - 16) {
						break;
					}
				}

				ParamQuantity *paramQuantity = getParamQuantity(i);
				if (paramQuantity == NULL) continue;
				// Set ParamQuantity
				float v = i < 16 ? inputs[POLY_INPUT1].getVoltage(i) : inputs[POLY_INPUT2].getVoltage(i - 16);
				if (bipolarInput)
					v += 5.f;
				v = rescale(v, 0.f, 10.f, 0.f, 1.f);

				// If lastValue is unitialized set it to its current value, only executed once
				if (lastValue[i] == UINIT) {
					lastValue[i] = v;
				}

				if (lockParameterChanges || lastValue[i] != v) {
					paramQuantity->setScaledValue(v);
					lastValue[i] = v;
				}
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

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModule<MAX_CHANNELS>::dataToJson();
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModule<MAX_CHANNELS>::dataFromJson(rootJ);
		json_t* audioRateJ = json_object_get(rootJ, "audioRate");
		if (audioRateJ) audioRate = json_boolean_value(audioRateJ);
	}
};


struct CVMapWidget : ModuleWidget {
	CVMapWidget(CVMap* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CVMap.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(26.9f, 60.8f), module, CVMap::POLY_INPUT1));
		addInput(createInputCentered<StoermelderPort>(Vec(123.1f, 60.8f), module, CVMap::POLY_INPUT2));

		PolyLedWidget<>* w0 = createWidgetCentered<PolyLedWidget<>>(Vec(54.2f, 60.8f));
		w0->setModule(module, CVMap::CHANNEL_LIGHTS1);
		addChild(w0);

		PolyLedWidget<>* w1 = createWidgetCentered<PolyLedWidget<>>(Vec(95.8f, 60.8f));
		w1->setModule(module, CVMap::CHANNEL_LIGHTS2);
		addChild(w1);

		typedef MapModuleDisplay<MAX_CHANNELS, CVMap> TMapDisplay;
		TMapDisplay* mapWidget = createWidget<TMapDisplay>(Vec(10.6f, 81.5f));
		mapWidget->box.size = Vec(128.9f, 261.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}


	void appendContextMenu(Menu* menu) override {
		CVMap* module = dynamic_cast<CVMap*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/CVMap.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());

		struct LockItem : MenuItem {
			CVMap* module;

			void onAction(const event::Action& e) override {
				module->lockParameterChanges ^= true;
			}

			void step() override {
				rightText = module->lockParameterChanges ? "Locked" : "Unlocked";
				MenuItem::step();
			}
		};

		struct UniBiItem : MenuItem {
			CVMap* module;

			void onAction(const event::Action& e) override {
				module->bipolarInput ^= true;
			}

			void step() override {
				rightText = module->bipolarInput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		struct AudioRateItem : MenuItem {
			CVMap* module;

			void onAction(const event::Action& e) override {
				module->audioRate ^= true;
			}

			void step() override {
				rightText = module->audioRate ? "✔" : "";
				MenuItem::step();
			}
		};

		struct TextScrollItem : MenuItem {
			CVMap* module;

			void onAction(const event::Action& e) override {
				module->textScrolling ^= true;
			}

			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		struct MappingIndicatorHiddenItem : MenuItem {
			CVMap* module;

			void onAction(const event::Action& e) override {
				module->mappingIndicatorHidden ^= true;
			}

			void step() override {
				rightText = module->mappingIndicatorHidden ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<LockItem>(&MenuItem::text, "Parameter changes", &LockItem::module, module));
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Signal input", &UniBiItem::module, module));
		menu->addChild(construct<AudioRateItem>(&MenuItem::text, "Audio rate processing", &AudioRateItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
	}
};

} // namespace CVMap

Model* modelCVMap = createModel<CVMap::CVMap, CVMap::CVMapWidget>("CVMap");