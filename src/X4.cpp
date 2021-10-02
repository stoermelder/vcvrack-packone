#include "plugin.hpp"
#include "MapModuleBase.hpp"
#include "components/Knobs.hpp"
#include "components/MapButton.hpp"

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
	/** [Stored to Json] */
	bool audioRate;

	/** [Stored to Json] */
	bool readParamA[5];
	/** [Stored to Json] */
	bool readParamB[5];

	float lastA[5];
	float lastB[5];
	int lightArx[5];
	int lightBrx[5];
	int lightAtx[5];
	int lightBtx[5];

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;

	X4Module() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<MapParamQuantity<X4Module>>(PARAM_MAP_A, 0.f, 1.f, 0.f, "Map A");
		MapParamQuantity<X4Module>* pq1 = dynamic_cast<MapParamQuantity<X4Module>*>(paramQuantities[PARAM_MAP_A]);
		pq1->mymodule = this;
		pq1->id = 0;
		configParam(PARAM_MAP_A + 1, 0.f, 1.f, 0.f, "Param A-1");
		configParam(PARAM_MAP_A + 2, 0.f, 1.f, 0.f, "Param A-2");
		configParam(PARAM_MAP_A + 3, 0.f, 1.f, 0.f, "Param A-3");
		configParam(PARAM_MAP_A + 4, 0.f, 1.f, 0.f, "Param A-4");
		configParam<MapParamQuantity<X4Module>>(PARAM_MAP_B, 0.f, 1.f, 0.f, "Map B");
		MapParamQuantity<X4Module>* pq2 = dynamic_cast<MapParamQuantity<X4Module>*>(paramQuantities[PARAM_MAP_B]);
		pq2->mymodule = this;
		pq2->id = 1;
		configParam(PARAM_MAP_B + 1, 0.f, 1.f, 0.f, "Param B-1");
		configParam(PARAM_MAP_B + 2, 0.f, 1.f, 0.f, "Param B-2");
		configParam(PARAM_MAP_B + 3, 0.f, 1.f, 0.f, "Param B-3");
		configParam(PARAM_MAP_B + 4, 0.f, 1.f, 0.f, "Param B-4");

		this->paramHandles[0].text = "X4";
		this->paramHandles[1].text = "X4";
		processDivider.setDivision(32);
		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		audioRate = false;
		for (size_t i = 0; i < 5; i++) {
			readParamA[i] = readParamB[i] = true;
		}
		CVMapModuleBase<2>::onReset();
	}

	void process(const ProcessArgs& args) override {
		if (audioRate || processDivider.process()) {
			ParamQuantity* pqA = getParamQuantity(0);
			if (pqA) {
				float v = pqA->getScaledValue();
				if (!isNear(v, lastA[0])) {
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
					float v1 = -1.f;
					if (readParamA[1]) {
						v1 = lastA[1] = params[PARAM_MAP_A + 1].getValue();
						lightArx[1] += !isNear(v1, v);
					}
					if (readParamA[2] && (isNear(v, v1) || v1 == -1.f)) {
						v1 = lastA[2] = params[PARAM_MAP_A + 2].getValue();
						lightArx[2] += !isNear(v1, v);
					}
					if (readParamA[3] && (isNear(v, v1) || v1 == -1.f)) {
						v1 = lastA[3] = params[PARAM_MAP_A + 3].getValue();
						lightArx[3] += !isNear(v1, v);
					}
					if (readParamA[4] && (isNear(v, v1) || v1 == -1.f)) {
						v1 = lastA[4] = params[PARAM_MAP_A + 4].getValue();
						lightArx[4] += !isNear(v1, v);
					}
					if (!isNear(v1, lastA[0]) && v1 != -1.f) {
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
				if (!isNear(v, lastB[0])) {
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
					float v1 = -1.f;
					if (readParamB[1]) {
						v1 = lastB[1] = params[PARAM_MAP_B + 1].getValue();
						lightBrx[1] += !isNear(v1, v);
					}
					if (readParamB[2] && (isNear(v, v1) || v1 == -1.f)) { 
						v1 = lastB[2] = params[PARAM_MAP_B + 2].getValue();
						lightBrx[2] += !isNear(v1, v);
					}
					if (readParamB[3] && (isNear(v, v1) || v1 == -1.f)) { 
						v1 = lastB[3] = params[PARAM_MAP_B + 3].getValue();
						lightBrx[3] += !isNear(v1, v);
					}
					if (readParamB[4] && (isNear(v, v1) || v1 == -1.f)) {
						v1 = lastB[4] = params[PARAM_MAP_B + 4].getValue();
						lightBrx[4] += !isNear(v1, v);
					}
					if (v1 != lastB[0] && v1 != -1.f) {
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
		}

		if (lightDivider.process()) {
			lights[LIGHT_MAP_A + 0].setBrightness(paramHandles[0].moduleId >= 0 && learningId != 0 ? 1.f : 0.f);
			lights[LIGHT_MAP_A + 1].setBrightness(learningId == 0 ? 1.f : 0.f);
			lights[LIGHT_MAP_B + 0].setBrightness(paramHandles[1].moduleId >= 0 && learningId != 1 ? 1.f : 0.f);
			lights[LIGHT_MAP_B + 1].setBrightness(learningId == 1 ? 1.f : 0.f);

			float d = float(lightDivider.division) / float(audioRate ? 1 : processDivider.division);
			for (size_t i = 0; i < 5; i++) {
				lights[LIGHT_RX_A + i].setBrightness(float(lightArx[i]) / d);
				lights[LIGHT_TX_A + i].setBrightness(float(lightAtx[i]) / d);
				lights[LIGHT_RX_B + i].setBrightness(float(lightBrx[i]) / d);
				lights[LIGHT_TX_B + i].setBrightness(float(lightBtx[i]) / d);
				lightArx[i] = lightBrx[i] = 0;
				lightAtx[i] = lightBtx[i] = 0;
			}
		}

		CVMapModuleBase<2>::process(args);
	}

	void commitLearn() override {
		CVMapModuleBase<2>::commitLearn();
		disableLearn(learningId);
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<2>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));

		json_t* readParamJ = json_array();
		json_t* readParamAJ = json_array();
		json_t* readParamBJ = json_array();
		for (size_t i = 0; i < 5; i++) {
			json_array_append_new(readParamAJ, json_boolean(readParamA[i]));
			json_array_append_new(readParamBJ, json_boolean(readParamB[i]));
		}
		json_array_append_new(readParamJ, readParamAJ);
		json_array_append_new(readParamJ, readParamBJ);
		json_object_set_new(rootJ, "readParam", readParamJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<2>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		audioRate = json_boolean_value(json_object_get(rootJ, "audioRate"));

		json_t* readParamJ = json_object_get(rootJ, "readParam");
		if (!readParamJ) return;
		json_t* readParamAJ = json_array_get(readParamJ, 0);
		json_t* readParamBJ = json_array_get(readParamJ, 1);
		for (size_t i = 0; i < 5; i++) {
			readParamA[i] = json_boolean_value(json_array_get(readParamAJ, i));
			readParamB[i] = json_boolean_value(json_array_get(readParamBJ, i));
		}
	}
};



struct X4Trimpot : StoermelderTrimpot {
	bool* readParam;

	void onDoubleClick(const event::DoubleClick& e) override {
		*readParam ^= true;
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Read", readParam));
	}
};


struct X4Widget : ThemedModuleWidget<X4Module> {
	X4Widget(X4Module* module)
		: ThemedModuleWidget<X4Module>(module, "X4") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = 26.7f;

		MapButton<X4Module>* buttonA = createParamCentered<MapButton<X4Module>>(Vec(15.f, 59.5f), module, X4Module::PARAM_MAP_A);
		buttonA->setModule(module);
		buttonA->id = 0;
		addParam(buttonA);
		addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 47.4f), module, X4Module::LIGHT_RX_A));
		addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(15.f, 59.5f), module, X4Module::LIGHT_MAP_A));
		addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 47.4f), module, X4Module::LIGHT_TX_A));

		for (size_t i = 0; i < 4; i++) {
			addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 80.7f + o * i), module, X4Module::LIGHT_RX_A + i + 1));
			X4Trimpot* tp = createParamCentered<X4Trimpot>(Vec(15.f, 91.2f + o * i), module, X4Module::PARAM_MAP_A + i + 1);
			tp->readParam = &module->readParamA[i + 1];
			addParam(tp);
			addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 80.7f + o * i), module, X4Module::LIGHT_TX_A + i + 1));
		}

		MapButton<X4Module>* buttonB = createParamCentered<MapButton<X4Module>>(Vec(15.f, 210.6f), module, X4Module::PARAM_MAP_B);
		buttonB->setModule(module);
		buttonB->id = 1;
		addParam(buttonB);
		addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 198.5f), module, X4Module::LIGHT_RX_B));
		addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(15.f, 210.6f), module, X4Module::LIGHT_MAP_B));
		addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 198.5f), module, X4Module::LIGHT_TX_B));

		for (size_t i = 0; i < 4; i++) {
			addChild(createLightCentered<TinyLight<YellowLight>>(Vec(6.1f, 231.7f + o * i), module, X4Module::LIGHT_RX_B + i + 1));
			X4Trimpot* tp = createParamCentered<X4Trimpot>(Vec(15.f, 242.2f + o * i), module, X4Module::PARAM_MAP_B + i + 1);
			tp->readParam = &module->readParamB[i + 1];
			addParam(tp);
			addChild(createLightCentered<TinyLight<BlueLight>>(Vec(24.0f, 231.7f + o * i), module, X4Module::LIGHT_TX_B + i + 1));
		}
	}


	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<X4Module>::appendContextMenu(menu);
		X4Module* module = dynamic_cast<X4Module*>(this->module);

		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Audio rate processing", &module->audioRate));
	}
};

} // namespace X4 
} // namespace StoermelderPackOne

Model* modelX4 = createModel<StoermelderPackOne::X4::X4Module, StoermelderPackOne::X4::X4Widget>("X4");