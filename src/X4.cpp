#include "plugin.hpp"
#include "MapModuleBase.hpp"

namespace StoermelderPackOne {
namespace X4 {

struct X4Module : CVMapModuleBase<2> {
	enum ParamIds {
		ENUMS(PARAM_MAP_A, 5),
		ENUMS(PARAM_MAP_B, 5),
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_MAP_A, 2),
		ENUMS(LIGHT_MAP_B, 2),
		ENUMS(LIGHT_RX_A, 5),
		ENUMS(LIGHT_TX_A, 5),
		ENUMS(LIGHT_RX_B, 5),
		ENUMS(LIGHT_TX_B, 5),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	float lastA[5];
	float lastB[5];
	int lightArx[5];
	int lightBrx[5];
	int lightAtx[5];
	int lightBtx[5];

	dsp::ClockDivider lightDivider;

	X4Module() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_MAP_A, 0.f, 1.f, 0.f, "Map A");
		configParam(PARAM_MAP_A + 1, 0.f, 1.f, 0.f, "Param A-1");
		configParam(PARAM_MAP_A + 2, 0.f, 1.f, 0.f, "Param A-2");
		configParam(PARAM_MAP_A + 3, 0.f, 1.f, 0.f, "Param A-3");
		configParam(PARAM_MAP_A + 4, 0.f, 1.f, 0.f, "Param A-4");
		configParam(PARAM_MAP_B, 0.f, 1.f, 0.f, "Map B");
		configParam(PARAM_MAP_B + 1, 0.f, 1.f, 0.f, "Param B-1");
		configParam(PARAM_MAP_B + 2, 0.f, 1.f, 0.f, "Param B-2");
		configParam(PARAM_MAP_B + 3, 0.f, 1.f, 0.f, "Param B-3");
		configParam(PARAM_MAP_B + 4, 0.f, 1.f, 0.f, "Param B-4");

		this->paramHandles[0].text = "XX";
		this->paramHandles[1].text = "XX";
		lightDivider.setDivision(1024);
		onReset();
	}

	void process(const ProcessArgs& args) override {
		ParamQuantity* pqA = getParamQuantity(0);
		if (pqA) {
			float v = pqA->getScaledValue();
			if (v != lastA[0]) {
				lightArx[0]++;
				lastA[0] = v;
				params[PARAM_MAP_A + 1].setValue(v);
				lightAtx[1] += lastA[1] != v;
				lastA[1] = v;
				params[PARAM_MAP_A + 2].setValue(v);
				lightAtx[2] += lastA[2] != v;
				lastA[2] = v;
				params[PARAM_MAP_A + 3].setValue(v);
				lightAtx[3] += lastA[3] != v;
				lastA[3] = v;
				params[PARAM_MAP_A + 4].setValue(v);
				lightAtx[4] += lastA[4] != v;
				lastA[4] = v;
			}
			else {
				float v1 = lastA[1] = params[PARAM_MAP_A + 1].getValue();
				lightArx[1] += v1 != v;
				if (v == v1) {
					v1 = lastA[2] = params[PARAM_MAP_A + 2].getValue();
					lightArx[2] += v1 != v;
				}
				if (v == v1) {
					v1 = lastA[3] = params[PARAM_MAP_A + 3].getValue();
					lightArx[3] += v1 != v;
				}
				if (v == v1) {
					v1 = lastA[4] = params[PARAM_MAP_A + 4].getValue();
					lightArx[4] += v1 != v;
				}
				if (v1 != lastA[0]) {
					lightAtx[0]++;
					pqA->setScaledValue(v1);
					params[PARAM_MAP_A + 1].setValue(v1);
					lightAtx[1] += lastA[1] != v1;
					lastA[1] = v1;
					params[PARAM_MAP_A + 2].setValue(v1);
					lightAtx[2] += lastA[2] != v1;
					lastA[2] = v1;
					params[PARAM_MAP_A + 3].setValue(v1);
					lightAtx[3] += lastA[3] != v1;
					lastA[3] = v1;
					params[PARAM_MAP_A + 4].setValue(v1);
					lightAtx[4] += lastA[4] != v1;
					lastA[4] = v1;
				}
				lastA[0] = v1;
			}
		}

		ParamQuantity* pqB = getParamQuantity(1);
		if (pqB) {
			float v = pqB->getScaledValue();
			if (v != lastB[0]) {
				lightBrx[0]++;
				lastB[0] = v;
				params[PARAM_MAP_B + 1].setValue(v);
				lightBtx[1] += lastB[1] != v;
				lastB[1] = v;
				params[PARAM_MAP_B + 2].setValue(v);
				lightBtx[2] += lastB[2] != v;
				lastB[2] = v;
				params[PARAM_MAP_B + 3].setValue(v);
				lightBtx[3] += lastB[3] != v;
				lastB[3] = v;
				params[PARAM_MAP_B + 4].setValue(v);
				lightBtx[4] += lastB[4] != v;
				lastB[4] = v;
			}
			else {
				float v1 = lastB[1] = params[PARAM_MAP_B + 1].getValue();
				lightBrx[1] += v1 != v;
				if (v == v1) { 
					v1 = lastB[2] = params[PARAM_MAP_B + 2].getValue();
					lightBrx[2] += v1 != v;
				}
				if (v == v1) { 
					v1 = lastB[3] = params[PARAM_MAP_B + 3].getValue();
					lightBrx[3] += v1 != v;
				}
				if (v == v1) {
					v1 = lastB[4] = params[PARAM_MAP_B + 4].getValue();
					lightBrx[4] += v1 != v;
				}
				if (v1 != lastB[0]) {
					lightBtx[0]++;
					pqB->setScaledValue(v1);
					params[PARAM_MAP_B + 1].setValue(v1);
					lightBtx[1] += lastB[1] != v1;
					lastB[1] = v1;
					params[PARAM_MAP_B + 2].setValue(v1);
					lightBtx[2] += lastB[2] != v1;
					lastB[2] = v1;
					params[PARAM_MAP_B + 3].setValue(v1);
					lightBtx[3] += lastB[3] != v1;
					lastB[3] = v1;
					params[PARAM_MAP_B + 4].setValue(v1);
					lightBtx[4] += lastB[4] != v1;
					lastB[4] = v1;
				}
				lastB[0] = v1;
			}
		}

		if (lightDivider.process()) {
			lights[LIGHT_MAP_A + 0].setBrightness(paramHandles[0].moduleId >= 0 && learningId != 0 ? 1.f : 0.f);
			lights[LIGHT_MAP_A + 1].setBrightness(learningId == 0 ? 1.f : 0.f);
			lights[LIGHT_MAP_B + 0].setBrightness(paramHandles[1].moduleId >= 0 && learningId != 1 ? 1.f : 0.f);
			lights[LIGHT_MAP_B + 1].setBrightness(learningId == 1 ? 1.f : 0.f);

			for (size_t i = 0; i < 5; i++) {
				lights[LIGHT_RX_A + i].setBrightness(float(lightArx[i]) / float(lightDivider.division));
				lights[LIGHT_TX_A + i].setBrightness(float(lightAtx[i]) / float(lightDivider.division));
				lights[LIGHT_RX_B + i].setBrightness(float(lightBrx[i]) / float(lightDivider.division));
				lights[LIGHT_TX_B + i].setBrightness(float(lightBtx[i]) / float(lightDivider.division));
				lightArx[i] = lightBrx[i] = 0;
				lightAtx[i] = lightBtx[i] = 0;
			}
		}

		CVMapModuleBase<2>::process(args);
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<2>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<2>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};


struct MapButton : LEDBezel {
	X4Module* module;
	int id = 0;

	void setModule(X4Module* module) {
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
					X4Module* module;
					int id;
					void onAction(const event::Action& e) override {
						module->clearMap(id);
					}
				};
				menu->addChild(construct<UnmapItem>(&MenuItem::text, "Unmap", &UnmapItem::module, module, &UnmapItem::id, id));

				struct IndicateItem : MenuItem {
					X4Module* module;
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

struct X4Widget : ThemedModuleWidget<X4Module> {
	X4Widget(X4Module* module)
		: ThemedModuleWidget<X4Module>(module, "X4") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = 26.7f;

		MapButton* buttonA = createParamCentered<MapButton>(Vec(15.f, 59.5f), module, X4Module::PARAM_MAP_A);
		buttonA->setModule(module);
		buttonA->id = 0;
		addParam(buttonA);
		addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 47.4f), module, X4Module::LIGHT_RX_A));
		addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(15.f, 59.5f), module, X4Module::LIGHT_MAP_A));
		addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 47.4f), module, X4Module::LIGHT_TX_A));

		for (size_t i = 0; i < 4; i++) {
			addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 80.7f + o * i), module, X4Module::LIGHT_RX_A + i + 1));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(15.f, 91.2f + o * i), module, X4Module::PARAM_MAP_A + i + 1));
			addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 80.7f + o * i), module, X4Module::LIGHT_TX_A + i + 1));
		}

		MapButton* buttonB = createParamCentered<MapButton>(Vec(15.f, 210.6f), module, X4Module::PARAM_MAP_B);
		buttonB->setModule(module);
		buttonB->id = 1;
		addParam(buttonB);
		addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 198.5f), module, X4Module::LIGHT_RX_B));
		addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(15.f, 210.6f), module, X4Module::LIGHT_MAP_B));
		addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 198.5f), module, X4Module::LIGHT_TX_B));

		for (size_t i = 0; i < 4; i++) {
			addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 231.7f + o * i), module, X4Module::LIGHT_RX_B + i + 1));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(15.f, 242.2f + o * i), module, X4Module::PARAM_MAP_B + i + 1));
			addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 231.7f + o * i), module, X4Module::LIGHT_TX_B + i + 1));
		}
	}
};

} // namespace X4 
} // namespace StoermelderPackOne

Model* modelX4 = createModel<StoermelderPackOne::X4::X4Module, StoermelderPackOne::X4::X4Widget>("X4");