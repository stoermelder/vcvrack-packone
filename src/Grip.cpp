#include "plugin.hpp"
#include "MapModuleBase.hpp"

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
		ENUMS(LIGHT_BIND, 2),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;

	GripModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_BIND, 0.f, 1.f, 0.f, "Bind parameter");
		processDivider.setDivision(64);
		lightDivider.setDivision(1024);

		for (int i = 0; i < MAX_CHANNELS; i++) {
			paramHandles[i].text = "GRIP";
			paramHandles[i].color = mappingIndicatorColor;
		}

		onReset();
	}

	void process(const ProcessArgs& args) override {
		if (processDivider.process()) {
			// Step channels
			for (int i = 0; i < mapLen; i++) {
				ParamQuantity* paramQuantity = getParamQuantity(i);
				if (paramQuantity == NULL) continue;

				// Set ParamQuantity
				paramQuantity->setScaledValue(lastValue[i]);
			}
		}

		if (lightDivider.process()) {
			lights[LIGHT_BIND + 0].setBrightness(learningId == -1 && mapLen > 0 ? 1.f : 0.f);
			lights[LIGHT_BIND + 1].setBrightness(learningId >= 0 ? 1.f : 0.f);
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

		json_t* lastValuesJ = json_object_get(rootJ, "lastValues");
		for (int i = 0; i < MAX_CHANNELS; i++) {
			json_t* lastValueJ = json_array_get(lastValuesJ, i);
			lastValue[i] = json_real_value(lastValueJ);
		}
	}
};


struct MapButton : LEDBezel {
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
		if (!module) return;
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

template <typename BASE>
struct MapLight : BASE {
	MapLight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct GripWidget : ThemedModuleWidget<GripModule> {
	GripWidget(GripModule* module)
		: ThemedModuleWidget<GripModule>(module, "Grip") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MapButton* button = createParamCentered<MapButton>(Vec(15.0f, 60.3f), module, GripModule::PARAM_BIND);
		button->module = module;
		addParam(button);
		addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(15.f, 60.3f), module, GripModule::LIGHT_BIND));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<GripModule>::appendContextMenu(menu);
		GripModule* module = dynamic_cast<GripModule*>(this->module);

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

		if (module->mapLen > 0) {
			menu->addChild(new MenuSeparator());
			for (int i = 0; i < MAX_CHANNELS; i++) {
				if (module->paramHandles[i].moduleId >= 0) {
					menu->addChild(construct<UnmapItem>(&UnmapItem::module, module, &UnmapItem::id, i));
				}
			}
		}
	}
};

} // namespace Grip

Model* modelGrip = createModel<Grip::GripModule, Grip::GripWidget>("Grip");