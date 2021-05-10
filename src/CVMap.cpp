#include "plugin.hpp"
#include "MapModuleBase.hpp"
#include "components/ParamWidgetContextExtender.hpp"
#include <chrono>

namespace StoermelderPackOne {
namespace CVMap {

static const int MAX_CHANNELS = 32;

struct CVMapModule : CVMapModuleBase<MAX_CHANNELS> {
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

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	bool audioRate;
	/** [Stored to JSON] */
	bool locked;

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;

	ScaledMapParam<float> mapParam[MAX_CHANNELS];

	CVMapModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < MAX_CHANNELS; i++) {
			paramHandles[i].text = string::f("CV-MAP Slot %02d", i + 1);
			mapParam[i].setLimits(0.f, 1.f, std::numeric_limits<float>::infinity());
		}
		processDivider.setDivision(32);
		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		CVMapModuleBase<MAX_CHANNELS>::onReset();
		audioRate = false;
		locked = false;
		for (size_t i = 0; i < MAX_CHANNELS; i++) {
			mapParam[i].reset();
		}
	}

	void process(const ProcessArgs& args) override {
		if (audioRate || processDivider.process()) {
			float deltaTime = args.sampleTime * (audioRate ? 1.f : float(processDivider.getDivision()));

			// Step channels
			for (int i = 0; i < mapLen; i++) {
				ParamQuantity* paramQuantity = getParamQuantity(i);
				if (paramQuantity == NULL) continue;
				mapParam[i].setParamQuantity(paramQuantity);

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

				float t = (i < 16 ? inputs[POLY_INPUT1].getVoltage(i) : inputs[POLY_INPUT2].getVoltage(i - 16));
				if (bipolarInput) t += 5.f;
				t /= 10.f;

				// Set a new value for the mapped parameter
				mapParam[i].setValue(t);

				// Apply value on the mapped parameter (respecting slew and scale)
				mapParam[i].process(deltaTime, lockParameterChanges);
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
		
		CVMapModuleBase<MAX_CHANNELS>::process(args);
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<MAX_CHANNELS>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));
		json_object_set_new(rootJ, "locked", json_boolean(locked));
		return rootJ;
	}

	void dataToJsonMap(json_t* mapJ, int index) override {
		json_object_set_new(mapJ, "slew", json_real(mapParam[index].getSlew()));
		json_object_set_new(mapJ, "min", json_real(mapParam[index].getMin()));
		json_object_set_new(mapJ, "max", json_real(mapParam[index].getMax()));
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<MAX_CHANNELS>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* audioRateJ = json_object_get(rootJ, "audioRate");
		if (audioRateJ) audioRate = json_boolean_value(audioRateJ);
		json_t* lockedJ = json_object_get(rootJ, "locked");
		if (lockedJ) locked = json_boolean_value(lockedJ);
	}

	void dataFromJsonMap(json_t* mapJ, int index) override {
		json_t* slewJ = json_object_get(mapJ, "slew");
		json_t* minJ = json_object_get(mapJ, "min");
		json_t* maxJ = json_object_get(mapJ, "max");
		if (slewJ) mapParam[index].setSlew(json_real_value(slewJ));
		if (minJ) mapParam[index].setMin(json_real_value(minJ));
		if (maxJ) mapParam[index].setMax(json_real_value(maxJ));
	}
};


struct CvMapChoice : MapModuleChoice<MAX_CHANNELS, CVMapModule> {
	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(new MapSlewSlider<>(&module->mapParam[id]));
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
		menu->addChild(construct<MapScalingInputLabel<>>(&MenuLabel::text, "Input", &MapScalingInputLabel<>::p, &module->mapParam[id]));
		menu->addChild(construct<MapScalingOutputLabel<>>(&MenuLabel::text, "Parameter range", &MapScalingOutputLabel<>::p, &module->mapParam[id]));
		menu->addChild(new MapMinSlider<>(&module->mapParam[id]));
		menu->addChild(new MapMaxSlider<>(&module->mapParam[id]));
		menu->addChild(construct<MapPresetMenuItem<>>(&MenuItem::text, "Presets", &MapPresetMenuItem<>::p, &module->mapParam[id]));
	}
};


struct CVMapWidget : ThemedModuleWidget<CVMapModule>, ParamWidgetContextExtender {
	CVMapWidget(CVMapModule* module)
		: ThemedModuleWidget<CVMapModule>(module, "CVMap") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(26.9f, 60.8f), module, CVMapModule::POLY_INPUT1));
		addInput(createInputCentered<StoermelderPort>(Vec(123.1f, 60.8f), module, CVMapModule::POLY_INPUT2));

		PolyLedWidget<>* w0 = createWidgetCentered<PolyLedWidget<>>(Vec(54.2f, 60.8f));
		w0->setModule(module, CVMapModule::CHANNEL_LIGHTS1);
		addChild(w0);

		PolyLedWidget<>* w1 = createWidgetCentered<PolyLedWidget<>>(Vec(95.8f, 60.8f));
		w1->setModule(module, CVMapModule::CHANNEL_LIGHTS2);
		addChild(w1);

		typedef MapModuleDisplay<MAX_CHANNELS, CVMapModule, CvMapChoice> TMapDisplay;
		TMapDisplay* mapWidget = createWidget<TMapDisplay>(Vec(10.6f, 81.5f));
		mapWidget->box.size = Vec(128.9f, 261.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}

	void step() override {
		ParamWidgetContextExtender::step();
		ThemedModuleWidget<CVMapModule>::step();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<CVMapModule>::appendContextMenu(menu);
		CVMapModule* module = dynamic_cast<CVMapModule*>(this->module);
		assert(module);

		struct LockItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->lockParameterChanges ^= true;
			}
			void step() override {
				rightText = module->lockParameterChanges ? "Locked" : "Unlocked";
				MenuItem::step();
			}
		};

		struct UniBiItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->bipolarInput ^= true;
			}
			void step() override {
				rightText = module->bipolarInput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		struct AudioRateItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->audioRate ^= true;
			}
			void step() override {
				rightText = module->audioRate ? "✔" : "";
				MenuItem::step();
			}
		};

		struct TextScrollItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->textScrolling ^= true;
			}
			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		struct MappingIndicatorHiddenItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->mappingIndicatorHidden ^= true;
			}
			void step() override {
				rightText = module->mappingIndicatorHidden ? "✔" : "";
				MenuItem::step();
			}
		};

		struct LockedItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->locked ^= true;
			}
			void step() override {
				rightText = module->locked ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LockItem>(&MenuItem::text, "Parameter changes", &LockItem::module, module));
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Signal input", &UniBiItem::module, module));
		menu->addChild(construct<AudioRateItem>(&MenuItem::text, "Audio rate processing", &AudioRateItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(construct<LockedItem>(&MenuItem::text, "Lock mapping slots", &LockedItem::module, module));
	}

	void extendParamWidgetContextMenu(ParamWidget* pw, Menu* menu) override {
		ParamQuantity* pq = pw->paramQuantity;
		if (!pq) return;

		for (int id = 0; id < module->mapLen; id++) {
			if (module->paramHandles[0].moduleId == pq->module->id && module->paramHandles[0].paramId == pq->paramId) {
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "CV-MAP"));
				menu->addChild(construct<CenterModuleItem>(&MenuItem::text, "Center mapping module", &CenterModuleItem::mw, this));
				menu->addChild(new MapSlewSlider<>(&module->mapParam[id]));
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
				menu->addChild(construct<MapScalingInputLabel<>>(&MenuLabel::text, "Input", &MapScalingInputLabel<>::p, &module->mapParam[id]));
				menu->addChild(construct<MapScalingOutputLabel<>>(&MenuLabel::text, "Parameter range", &MapScalingOutputLabel<>::p, &module->mapParam[id]));
				menu->addChild(new MapMinSlider<>(&module->mapParam[id]));
				menu->addChild(new MapMaxSlider<>(&module->mapParam[id]));
				return;
			}
		}
	}
};

} // namespace CVMap
} // namespace StoermelderPackOne

Model* modelCVMap = createModel<StoermelderPackOne::CVMap::CVMapModule, StoermelderPackOne::CVMap::CVMapWidget>("CVMap");