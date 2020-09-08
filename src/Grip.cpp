#include "plugin.hpp"
#include "MapModuleBase.hpp"

namespace StoermelderPackOne {
namespace Grip {

static const int MAX_CHANNELS = 32;

struct GripModule : CVMapModuleBase<MAX_CHANNELS> {
	enum ParamIds {
		PARAM_BIND,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_BIND,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	bool audioRate;

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;

	GripModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_BIND, 0.f, 1.f, 0.f, "Bind new parameter");
		processDivider.setDivision(64);
		lightDivider.setDivision(1024);

		for (int i = 0; i < MAX_CHANNELS; i++) {
			paramHandles[i].text = "stoermelder GRIP";
			paramHandles[i].color = color::fromHexString("#CD5C5C");
		}

		onReset();
	}

	void onReset() override {
		audioRate = false;
		CVMapModuleBase<MAX_CHANNELS>::onReset();
	}

	void process(const ProcessArgs& args) override {
		if (audioRate || processDivider.process()) {
			// Step channels
			for (int i = 0; i < mapLen; i++) {
				ParamQuantity* paramQuantity = getParamQuantity(i);
				if (paramQuantity == NULL) continue;

				// Set ParamQuantity
				paramQuantity->setScaledValue(lastValue[i]);
			}
		}

		if (lightDivider.process()) {
			lights[LIGHT_BIND].setBrightness(learningId >= 0 ? 1.f : 0.f);
		}
	}

	void commitLearn() override {
		int i = learningId;
		CVMapModuleBase<MAX_CHANNELS>::commitLearn();

		if (i >= 0) {
			ParamQuantity* paramQuantity = getParamQuantity(i);
			if (paramQuantity) lastValue[i] = paramQuantity->getScaledValue();
		}
		learningId = -1;
	}

	void clearMap(int id) override {
		CVMapModuleBase<MAX_CHANNELS>::clearMap(id);
		lastValue[id] = -1;
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<MAX_CHANNELS>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));

		json_t* lastValuesJ = json_array();
		for (int i = 0; i < MAX_CHANNELS; i++) {
			json_t* lastValueJ = json_real(lastValue[i]);
			json_array_append(lastValuesJ, lastValueJ);
		}
		json_object_set_new(rootJ, "lastValues", lastValuesJ);

 		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<MAX_CHANNELS>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		audioRate = json_boolean_value(json_object_get(rootJ, "audioRate"));

		json_t* lastValuesJ = json_object_get(rootJ, "lastValues");
		for (int i = 0; i < MAX_CHANNELS; i++) {
			json_t* lastValueJ = json_array_get(lastValuesJ, i);
			lastValue[i] = json_real_value(lastValueJ);
		}
	}
};


struct MapButton : TL1105 {
	GripModule* module;
	int id;

	void onSelect(const event::Select& e) override {
		if (!module) return;

		id = -1;
		// Find last nonempty map
		for (int i = 0; i < MAX_CHANNELS; i++) {
			if (module->paramHandles[i].moduleId < 0) {
				id = i;
				break;
			}
		}
		// No more empty slots
		if (id == -1) return;

		// Reset touchedParam
		APP->scene->rack->touchedParam = NULL;
		module->enableLearn(id);
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module || module->learningId < 0) return;
		// Check if a ParamWidget was touched
		ParamWidget* touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam && touchedParam->paramQuantity->module != module) {
			APP->scene->rack->touchedParam = NULL;
			int moduleId = touchedParam->paramQuantity->module->id;
			int paramId = touchedParam->paramQuantity->paramId;
			module->learnParam(id, moduleId, paramId);
		} 
		else {
			module->disableLearn(id);
		}
	}
};

struct GripWidget : ThemedModuleWidget<GripModule> {
	GripWidget(GripModule* module)
		: ThemedModuleWidget<GripModule>(module, "Grip") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 291.3f), module, GripModule::LIGHT_BIND));
		MapButton* button = createParamCentered<MapButton>(Vec(15.0f, 306.7f), module, GripModule::PARAM_BIND);
		button->module = module;
		addParam(button);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<GripModule>::appendContextMenu(menu);
		GripModule* module = dynamic_cast<GripModule*>(this->module);

		struct AudioRateItem : MenuItem {
			GripModule* module;
			void onAction(const event::Action& e) override {
				module->audioRate ^= true;
			}
			void step() override {
				rightText = module->audioRate ? "✔" : "";
				MenuItem::step();
			}
		};

		struct UnmapItem : MenuItem {
			GripModule* module;
			int id;
			void onAction(const event::Action& e) override {
				module->clearMap(id);
			}
			void step() override {
				text = getParamName();
				MenuItem::step();
			}

			std::string getParamName() {
				ParamHandle* paramHandle = &module->paramHandles[id];
				if (paramHandle->moduleId < 0) return "<ERROR>";
				ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
				if (!mw) return "<ERROR>";
				Module* m = mw->module;
				if (!m) return "<ERROR>";
				int paramId = paramHandle->paramId;
				if (paramId >= (int) m->params.size()) return "<ERROR>";
				ParamQuantity* paramQuantity = m->paramQuantities[paramId];
				std::string s;
				s += mw->model->name;
				s += " ";
				s += paramQuantity->label;
				return s;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<AudioRateItem>(&MenuItem::text, "Audio rate processing", &AudioRateItem::module, module));

		if (module->mapLen > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Locked parameters"));
			for (int i = 0; i < MAX_CHANNELS; i++) {
				if (module->paramHandles[i].moduleId >= 0) {
					menu->addChild(construct<UnmapItem>(&UnmapItem::module, module, &UnmapItem::id, i));
				}
			}
		}
	}
};

} // namespace Grip
} // namespace StoermelderPackOne

Model* modelGrip = createModel<StoermelderPackOne::Grip::GripModule, StoermelderPackOne::Grip::GripWidget>("Grip");