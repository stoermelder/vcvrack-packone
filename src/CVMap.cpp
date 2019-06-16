#include "plugin.hpp"
#include "widgets.hpp"
#include "MapModule.hpp"
#include <chrono>

static const int MAX_CHANNELS = 32;
static const float UINIT = 0;

struct CV_Map : MapModule<MAX_CHANNELS> {
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

	bool bipolarInput = false;

	/** Track last values */
	float lastValue[MAX_CHANNELS];
	/** [Saved to JSON] Allow manual changes of target parameters */
	bool lockParameterChanges = true;

	dsp::ClockDivider lightDivider;

	CV_Map() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].color = nvgRGB(0xff, 0x40, 0xff);
			paramHandles[id].text = string::f("CV-Map Ch%02d", id + 1);
		}
		onReset();
		lightDivider.setDivision(1024);
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
		
		MapModule::process(args);
	}

	json_t *dataToJson() override {
		json_t *rootJ = MapModule::dataToJson();
		json_object_set_new(rootJ, "lockParameterChanges", json_boolean(lockParameterChanges));
		json_object_set_new(rootJ, "bipolarInput", json_boolean(bipolarInput));

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		MapModule::dataFromJson(rootJ);

		json_t *lockParameterChangesJ = json_object_get(rootJ, "lockParameterChanges");
		lockParameterChanges = json_boolean_value(lockParameterChangesJ);

		json_t *bipolarInputJ = json_object_get(rootJ, "bipolarInput");
		bipolarInput = json_boolean_value(bipolarInputJ);
	}
};


struct CV_MapWidget : ModuleWidget {
	CV_MapWidget(CV_Map *module) {	
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV-Map.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = 9.f;
		float v = 13.5f;
		float d = 6.8f;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(o, 21.1)), module, CV_Map::POLY_INPUT1));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(o + d + v + 12.4f, 21.1)), module, CV_Map::POLY_INPUT2));

		PolyLedWidget *w0 = createWidget<PolyLedWidget>(mm2px(Vec(o + d, 17.975)));
		w0->setModule(module, CV_Map::CHANNEL_LIGHTS1);
		addChild(w0);

		PolyLedWidget *w1 = createWidget<PolyLedWidget>(mm2px(Vec(o + d + v, 17.975)));
		w1->setModule(module, CV_Map::CHANNEL_LIGHTS2);
		addChild(w1);

		MapModuleDisplay<MAX_CHANNELS> *mapWidget = createWidget<MapModuleDisplay<MAX_CHANNELS>>(mm2px(Vec(3.41891, 29.f)));
		mapWidget->box.size = mm2px(Vec(43.999, 91));
		mapWidget->setModule(module);
		addChild(mapWidget);
	}


	void appendContextMenu(Menu *menu) override {
		CV_Map *module = dynamic_cast<CV_Map*>(this->module);
		assert(module);

		struct LockItem : MenuItem {
			CV_Map *module;

			void onAction(const event::Action &e) override {
				module->lockParameterChanges ^= true;
			}

			void step() override {
				rightText = module->lockParameterChanges ? "Locked" : "Unlocked";
				MenuItem::step();
			}
		};

		struct UniBiItem : MenuItem {
			CV_Map *module;

			void onAction(const event::Action &e) override {
				module->bipolarInput ^= true;
			}

			void step() override {
				rightText = module->bipolarInput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		struct TextScrollItem : MenuItem {
			CV_Map *module;

			void onAction(const event::Action &e) override {
				module->textScrolling ^= true;
			}

			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LockItem>(&MenuItem::text, "Parameter changes", &LockItem::module, module));
		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Signal input", &UniBiItem::module, module));
	}
};


Model *modelCV_Map = createModel<CV_Map, CV_MapWidget>("CVMap");
