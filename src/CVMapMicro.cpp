#include "plugin.hpp"
#include "MapModuleBase.hpp"
#include <chrono>


namespace CVMapMicro {

static const float UINIT = 0;


template < typename MODULE >
struct MapParamQuantity : ParamQuantity {
	MODULE* module;
	std::string getParamName() {
		if (!module)
			return "";
		ParamHandle* paramHandle = &module->paramHandles[0];
		if (paramHandle->moduleId < 0)
			return "";
		ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return "";
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module* m = mw->module;
		if (!m)
			return "";
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return "";
		ParamQuantity* paramQuantity = m->paramQuantities[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += paramQuantity->label;
		return s;
	}

	std::string getDisplayValueString() override {
		std::string name = getParamName();
		return name != "" ? "Mapped to \"" + name + "\"" : "Unmapped";
	}
};


struct CVMapMicroModule : CVMapModuleBase<1> {
	enum ParamIds {
		MAP_PARAM,
		OFFSET_PARAM,
		SCALE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		OFFSET_INPUT,
		SCALE_INPUT,
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(MAP_LIGHT, 2),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to Json] */
	bool invertedOutput = false;

	dsp::ClockDivider lightDivider;

	CVMapMicroModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<MapParamQuantity<CVMapMicroModule>>(MAP_PARAM, 0.f, 1.f, 0.f, "µMAP");
		MapParamQuantity<CVMapMicroModule>* pq = dynamic_cast<MapParamQuantity<CVMapMicroModule>*>(paramQuantities[MAP_PARAM]);
		pq->module = this;

		configParam(OFFSET_PARAM, 0.f, 1.f, 0.f, "Input-offset", "%", 0.f, 100.f);
		configParam(SCALE_PARAM, -2.f, 2.f, 1.f, "Input-scaling", "x");

		this->paramHandles[0].text = "µMAP";
		lightDivider.setDivision(1024);
		onReset();
	}

	void process(const ProcessArgs& args) override {
		if (inputs[INPUT].isConnected()) {
			ParamQuantity* paramQuantity = getParamQuantity(0);
			if (paramQuantity) {
				// Set ParamQuantity
				float v = inputs[INPUT].getVoltage();
				if (bipolarInput)
					v += 5.f;
				v = rescale(v, 0.f, 10.f, 0.f, 1.f);

				float o = inputs[OFFSET_INPUT].isConnected() ?
					clamp(rescale(inputs[OFFSET_INPUT].getVoltage(), 0.f, 10.f, 0.f, 1.f), 0.f, 1.f) :
					params[OFFSET_PARAM].getValue();

				float s = inputs[SCALE_INPUT].isConnected() ?
					clamp(rescale(inputs[SCALE_INPUT].getVoltage(), -10.f, 10.f, -2.f, 2.f), -2.f, 2.f) :
					params[SCALE_PARAM].getValue();

				v = o + v * s;
				v = clamp(v, 0.f, 1.f);

				// If lastValue is unitialized set it to its current value, only executed once
				if (lastValue[0] == UINIT)
					lastValue[0] = v;

				if (lockParameterChanges || lastValue[0] != v) {
					paramQuantity->setScaledValue(v);
					lastValue[0] = v;

					if (outputs[OUTPUT].isConnected()) {
						if (invertedOutput)
							v = 1 - v;
						if (bipolarInput)
							v = rescale(v, 0.f, 1.f, -5.f, 5.f);
						else
							v = rescale(v, 0.f, 1.f, 0.f, 10.f);
						outputs[OUTPUT].setVoltage(v);
					}
				}
			}
		}

		if (lightDivider.process()) {
			lights[MAP_LIGHT + 0].setBrightness(paramHandles[0].moduleId >= 0 && learningId != 0 ? 1.f : 0.f);
			lights[MAP_LIGHT + 1].setBrightness(learningId == 0 ? 1.f : 0.f);
		}

		CVMapModuleBase<1>::process(args);
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<1>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "invertedOutput", json_boolean(invertedOutput));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<1>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		json_t* invertedOutputJ = json_object_get(rootJ, "invertedOutput");
		if (invertedOutputJ) invertedOutput = json_boolean_value(invertedOutputJ);
	}
};


struct MapButton : LEDBezel {
	CVMapMicroModule* module;
	int id = 0;

	void setModule(CVMapMicroModule* module) {
		this->module = module;
	}

	void onButton(const event::Button& e) override {
		e.stopPropagating();
		if (!module)
			return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			e.consume(this);

			if (module->paramHandles[id].moduleId >= 0) {
				ui::Menu* menu = createMenu();
				std::string header = "Parameter \"" + getParamName() + "\"";
				menu->addChild(createMenuLabel(header));

				struct UnmapItem : MenuItem {
					CVMapMicroModule* module;
					int id;
					void onAction(const event::Action& e) override {
						module->clearMap(0);
					}
				};
				menu->addChild(construct<UnmapItem>(&MenuItem::text, "Unmap", &UnmapItem::module, module, &UnmapItem::id, id));

				struct IndicateItem : MenuItem {
					CVMapMicroModule* module;
					int id;
					void onAction(const event::Action& e) override {
						ParamHandle* paramHandle = &module->paramHandles[id];
						ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
						module->paramHandleIndicator[id].indicate(mw);
					}
				};
				menu->addChild(construct<IndicateItem>(&MenuItem::text, "Locate and indicate", &IndicateItem::module, module, &IndicateItem::id, id));
			} 
			else {
				module->clearMap(id);
			}
		}
	}

	void onSelect(const event::Select& e) override {
		if (!module)
			return;

		// Reset touchedParam
		APP->scene->rack->touchedParam = NULL;
		module->enableLearn(id);

		GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		glfwSetCursor(APP->window->win, cursor);
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module)
			return;
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
		glfwSetCursor(APP->window->win, NULL);
	}

	std::string getParamName() {
		if (!module)
			return "";
		if (id >= module->mapLen)
			return "<ERROR>";
		ParamHandle* paramHandle = &module->paramHandles[id];
		if (paramHandle->moduleId < 0)
			return "<ERROR>";
		ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return "<ERROR>";
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module* m = mw->module;
		if (!m)
			return "<ERROR>";
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return "<ERROR>";
		ParamQuantity* paramQuantity = m->paramQuantities[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += paramQuantity->label;
		return s;
	}
};


template <typename BASE>
struct MapLight : BASE {
	MapLight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct CVMapMicroWidget : ThemedModuleWidget<CVMapMicroModule> {
	CVMapMicroWidget(CVMapMicroModule* module)
		: ThemedModuleWidget<CVMapMicroModule>(module, "CVMapMicro") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MapButton* button = createParamCentered<MapButton>(Vec(22.5f, 60.3f), module, CVMapMicroModule::MAP_PARAM);
		button->setModule(module);
		addParam(button);
		addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(22.5f, 60.3f), module, CVMapMicroModule::MAP_LIGHT));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 142.0f), module, CVMapMicroModule::INPUT));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 187.1f), module, CVMapMicroModule::OFFSET_INPUT));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 211.9f), module, CVMapMicroModule::OFFSET_PARAM));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 255.9f), module, CVMapMicroModule::SCALE_INPUT));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 280.4f), module, CVMapMicroModule::SCALE_PARAM));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.4f), module, CVMapMicroModule::OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<CVMapMicroModule>::appendContextMenu(menu);
		CVMapMicroModule* module = dynamic_cast<CVMapMicroModule*>(this->module);
		assert(module);

		struct LockItem : MenuItem {
			CVMapMicroModule* module;

			void onAction(const event::Action& e) override {
				module->lockParameterChanges ^= true;
			}

			void step() override {
				rightText = module->lockParameterChanges ? "Locked" : "Unlocked";
				MenuItem::step();
			}
		};

		struct UniBiItem : MenuItem {
			CVMapMicroModule* module;

			void onAction(const event::Action& e) override {
				module->bipolarInput ^= true;
			}

			void step() override {
				rightText = module->bipolarInput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		struct SignalOutputItem : MenuItem {
			CVMapMicroModule* module;

			void onAction(const event::Action& e) override {
				module->invertedOutput ^= true;
			}

			void step() override {
				rightText = module->invertedOutput ? "Inverted" : "Default";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LockItem>(&MenuItem::text, "Parameter changes", &LockItem::module, module));
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Voltage range", &UniBiItem::module, module));
		menu->addChild(construct<SignalOutputItem>(&MenuItem::text, "OUT-port", &SignalOutputItem::module, module));
	}
};

} // namespace CVMapMicro

Model* modelCVMapMicro = createModel<CVMapMicro::CVMapMicroModule, CVMapMicro::CVMapMicroWidget>("CVMapMicro");