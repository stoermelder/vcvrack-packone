#include "plugin.hpp"
#include "digital.hpp"
#include <thread>

namespace Intermix {

const int SCENE_COUNT = 8;

enum SCENE_CV_MODE {
	TRIG_FWD = 0,
	VOLT = 8,
	C4 = 9
};

enum IN_MODE {
	IM_OFF = 0,
	IM_DIRECT = 1,
	IM_FADE = 4,
	IM_ADD_1V = 2,
	IM_SUB_1V = 3
};

enum OUT_MODE {
	OM_OFF = 0,
	OM_OUT = 1
};

template < int PORTS >
struct IntermixModule : Module {
	enum ParamIds {
		ENUMS(MATRIX_PARAM, PORTS * PORTS),
		ENUMS(OUTPUT_PARAM, PORTS),
		ENUMS(SCENE_PARAM, SCENE_COUNT),
		ENUMS(AT_PARAM, PORTS),
		FADEIN_PARAM,
		FADEOUT_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, PORTS),
		SCENE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(MATRIX_LIGHT, PORTS * PORTS * 3),
		ENUMS(OUTPUT_LIGHT, PORTS),
		ENUMS(SCENE_LIGHT, PORTS),
		NUM_LIGHTS
	};

	struct SceneData {
		alignas(16) OUT_MODE output[PORTS];
		alignas(16) float outputAt[PORTS];
		alignas(16) float matrix[PORTS][PORTS];
	};

	alignas(16) float currentMatrix[PORTS][PORTS];

	/** [Stored to JSON] */
	float padBrightness;
	/** [Stored to JSON] */
	bool inputVisualize;
	/** [Stored to JSON] */
	IN_MODE inputMode[PORTS];
	/** [Stored to JSON] */
	bool outputClamp;
	/** [Stored to JSON] */
	SceneData scenes[SCENE_COUNT];
	/** [Stored to JSON] */
	int sceneSelected = 0;
	/** [Stored to JSON] */
	SCENE_CV_MODE sceneMode;

	LinearFade fader[PORTS][PORTS];
	dsp::TSlewLimiter<simd::float_4> outputAtSlew[PORTS / 4];

	dsp::SchmittTrigger sceneTrigger;
	dsp::ClockDivider sceneDivider;
	dsp::ClockDivider lightDivider;

	IntermixModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < SCENE_COUNT; i++) {
			configParam(SCENE_PARAM + i, 0.f, 1.f, 0.f, string::f("Scene %i", i + 1));
		}
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				configParam(MATRIX_PARAM + i * PORTS + j, 0.f, 1.f, 0.f, string::f("Input %i to Output %i", j + 1, i + 1));
			}
			configParam(OUTPUT_PARAM + i, 0.f, 1.f, 0.f, string::f("Disable output %i", i + 1));
			configParam(AT_PARAM + i, -1.f, 1.f, 1.f, string::f("Output %i attenuverter", i + 1), "x");
		}
		configParam(FADEIN_PARAM, 0.f, 4.f, 0.5f, "Fade in", "s");
		configParam(FADEOUT_PARAM, 0.f, 4.f, 0.5f, "Fade out", "s");
		sceneDivider.setDivision(32);
		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		padBrightness = 0.75f;
		inputVisualize = true;
		outputClamp = true;
		for (int i = 0; i < SCENE_COUNT; i++) {
			inputMode[i] = IM_DIRECT;
			for (int j = 0; j < PORTS; j++) {
				scenes[i].output[j] = OM_OUT;
				scenes[i].outputAt[j] = 0.f;
				for (int k = 0; k < PORTS; k++) {
					scenes[i].matrix[j][k] = 0.f;
				}
			}
		}
		sceneSet(0);
		sceneMode = SCENE_CV_MODE::TRIG_FWD;
		Module::onReset();
	}

	void process(const ProcessArgs& args) override {
		if (inputs[SCENE_INPUT].isConnected()) {
			switch (sceneMode) {
				case SCENE_CV_MODE::TRIG_FWD: {
					if (sceneTrigger.process(inputs[SCENE_INPUT].getVoltage())) {
						int s = (sceneSelected + 1) % SCENE_COUNT;
						sceneSet(s);
					}
					break;
				}
				case SCENE_CV_MODE::C4: {
					int s = std::round(clamp(inputs[SCENE_INPUT].getVoltage() * 12.f, 0.f, SCENE_COUNT - 1.f));
					sceneSet(s);
					break;
				}
				case SCENE_CV_MODE::VOLT: {
					int s = std::floor(rescale(inputs[SCENE_INPUT].getVoltage(), 0.f, 10.f, 0, SCENE_COUNT - 1e-3f));
					sceneSet(s);
					break;
				}
			}
		}

		if (sceneDivider.process()) {
			int sceneFound = -1;
			for (int i = 0; i < SCENE_COUNT; i++) {
				if (params[SCENE_PARAM + i].getValue() > 0.f) {
					if (i != sceneSelected) {
						sceneSet(i);
						break;
					}
					sceneFound = i;
				}
			}
			if (sceneFound == -1) {
				params[SCENE_PARAM + sceneSelected].setValue(1.f);
			}

			float f1 = params[FADEIN_PARAM].getValue();
			float f2 = params[FADEOUT_PARAM].getValue();
			for (int i = 0; i < PORTS; i++) {
				scenes[sceneSelected].output[i] = params[OUTPUT_PARAM + i].getValue() == 0.f ? OM_OUT : OM_OFF;
				scenes[sceneSelected].outputAt[i] = params[AT_PARAM + i].getValue();
				for (int j = 0; j < PORTS; j++) {
					fader[i][j].setRiseFall(f1, f2);
					float p = params[MATRIX_PARAM + j * PORTS + i].getValue();
					if (p != scenes[sceneSelected].matrix[i][j] && p == 1.f) fader[i][j].triggerFadeIn();
					if (p != scenes[sceneSelected].matrix[i][j] && p == 0.f) fader[i][j].triggerFadeOut();
					scenes[sceneSelected].matrix[i][j] = currentMatrix[i][j] = p;
				}
			}
		}

		simd::float_4 out[PORTS / 4] = {};
		for (int i = 0; i < PORTS; i++) {
			float v;
			switch (inputMode[i]) {
				case IN_MODE::IM_OFF:
					continue;
				case IN_MODE::IM_DIRECT:
					if (!inputs[INPUT + i].isConnected()) continue;
					v = inputs[INPUT + i].getVoltage();
					break;
				case IN_MODE::IM_FADE:
					if (!inputs[INPUT + i].isConnected()) continue;
					v = inputs[INPUT + i].getVoltage();
					for (int j = 0; j < PORTS; j++) {
						currentMatrix[i][j] = fader[i][j].process(args.sampleTime);
					}
					break;
				case IN_MODE::IM_ADD_1V:
					v = 1.f;
					break;
				case IN_MODE::IM_SUB_1V:
					v = -1.f;
					break;
			}

			for (int j = 0; j < PORTS; j+=4) {
				simd::float_4 v1 = simd::float_4::load(&currentMatrix[i][j]);
				simd::float_4 v2 = v1 * simd::float_4(v);
				out[j / 4] += v2;
			}
		}

		/*
		// Standard code
		for (int i = 0; i < PORTS; i++) {
			float v = scenes[sceneSelected].output[i] == OM_OUT ? out[i / 4][i % 4] : 0.f;
			if (outputClamp) v = clamp(v, -10.f, 10.f);
			outputs[OUTPUT + i].setVoltage(v);
		}
		*/

		// SIMD code
		simd::float_4 c = outputClamp;
		for (int j = 0; j < PORTS; j+=4) {
			// Check for OUT_MODE
			simd::int32_4 o1 = simd::int32_4::load((int32_t*)&scenes[sceneSelected].output[j]);
			simd::float_4 o2 = simd::float_4(o1 != 0) == -1.f;
			out[j / 4] = simd::ifelse(o2, out[j / 4], simd::float_4::zero());
			// Clamp if outputClamp it set
			out[j / 4] = simd::ifelse(c == 1.f, simd::clamp(out[j / 4], -10.f, 10.f), out[j / 4]);
			// Attenuverters
			simd::float_4 at = simd::float_4::load(&scenes[sceneSelected].outputAt[j]);
			//at = outputAtSlew[j / 4].process(args.sampleTime, at);
			out[j / 4] *= at;
		}

		for (int i = 0; i < PORTS; i++) {
			outputs[OUTPUT + i].setVoltage(out[i / 4][i % 4]);
		}
		// --

		if (lightDivider.process()) {
			float s = lightDivider.getDivision() * args.sampleTime;

			for (int i = 0; i < SCENE_COUNT; i++) {
				lights[SCENE_LIGHT + i].setSmoothBrightness((i == sceneSelected) * padBrightness, s);
			}

			if (inputVisualize) {
				float in[PORTS];
				for (int i = 0; i < PORTS; i++) {
					in[i] = rescale(inputs[INPUT + i].getVoltage(), -10.f, 10.f, -1.f, 1.f);
				}
				for (int i = 0; i < PORTS; i++) {
					for (int j = 0; j < PORTS; j++) {
						float v = currentMatrix[j][i] * (in[j] * padBrightness);
						lights[MATRIX_LIGHT + (i * PORTS + j) * 3 + 0].setBrightness(v < 0.f ? -v : 0.f);
						lights[MATRIX_LIGHT + (i * PORTS + j) * 3 + 1].setBrightness(v > 0.f ?  v : 0.f);
						lights[MATRIX_LIGHT + (i * PORTS + j) * 3 + 2].setBrightness(0.f);
					}
				}
			}
			else {
				for (int i = 0; i < PORTS; i++) {
					for (int j = 0; j < PORTS; j++) {
						float v = currentMatrix[j][i] * padBrightness;
						lights[MATRIX_LIGHT + (i * PORTS + j) * 3 + 0].setSmoothBrightness(v, s);
						lights[MATRIX_LIGHT + (i * PORTS + j) * 3 + 1].setSmoothBrightness(v, s);
						lights[MATRIX_LIGHT + (i * PORTS + j) * 3 + 2].setSmoothBrightness(v, s);
					}
				}
			}
			for (int i = 0; i < PORTS; i++) {
				float v = (scenes[sceneSelected].output[i] != OM_OUT) * padBrightness;
				lights[OUTPUT_LIGHT + i].setSmoothBrightness(v, s);
			}
		}
	}

	inline void sceneSet(int scene) {
		if (sceneSelected == scene) return;
		int scenePrevious = sceneSelected;
		sceneSelected = scene;

		for (int i = 0; i < SCENE_COUNT; i++) {
			params[SCENE_PARAM + i].setValue(i == sceneSelected);
		}

		/*
		simd::float_4 at[PORTS / 4];
		float f1 = params[FADEIN_PARAM].getValue();
		float f2 = params[FADEOUT_PARAM].getValue();
		*/
		for (int i = 0; i < PORTS; i++) {
			params[OUTPUT_PARAM + i].setValue(scenes[sceneSelected].output[i] != OM_OUT);

			/*
			float at0 = params[AT_PARAM + i].getValue();
			float at1 = scenes[sceneSelected].outputAt[i];
			at[i / 4][i % 4] = at0 > at1 ? (at0 - at1) : (at1 - at0);
			*/
			params[AT_PARAM + i].setValue(scenes[sceneSelected].outputAt[i]);
			for (int j = 0; j < PORTS; j++) {
				float p = scenes[sceneSelected].matrix[i][j];
				params[MATRIX_PARAM + j * PORTS + i].setValue(p);
				if (p != scenes[scenePrevious].matrix[i][j] && p == 1.f) fader[i][j].triggerFadeIn();
				if (p != scenes[scenePrevious].matrix[i][j] && p == 0.f) fader[i][j].triggerFadeOut();
				currentMatrix[i][j] = p;
			}
		}
		/*
		for (int i = 0; i < PORTS / 4; i++) {
			outputAtSlew[i].setRiseFall(at[i] / f1, at[i] / f2);
		}
		*/
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_object_set_new(rootJ, "padBrightness", json_real(padBrightness));
		json_object_set_new(rootJ, "inputVisualize", json_boolean(inputVisualize));
		json_object_set_new(rootJ, "outputClamp", json_boolean(outputClamp));

		json_t* inputsJ = json_array();
		for (int i = 0; i < PORTS; i++) {
			json_array_append_new(inputsJ, json_integer(inputMode[i]));
		}
		json_object_set_new(rootJ, "inputMode", inputsJ);

		json_t* scenesJ = json_array();
		for (int i = 0; i < SCENE_COUNT; i++) {
			json_t* outputJ = json_array();
			json_t* outputAtJ = json_array();
			json_t* matrixJ = json_array();
			for (int j = 0; j < PORTS; j++) {
				json_array_append_new(outputJ, json_integer(scenes[i].output[j]));
				json_array_append_new(outputAtJ, json_real(scenes[i].outputAt[j]));
				for (int k = 0; k < PORTS; k++) {
					json_array_append_new(matrixJ, json_real(scenes[i].matrix[j][k]));
				}
			}

			json_t* sceneJ = json_object();
			json_object_set_new(sceneJ, "output", outputJ);
			json_object_set_new(sceneJ, "outputAt", outputAtJ);
			json_object_set_new(sceneJ, "matrix", matrixJ);
			json_array_append_new(scenesJ, sceneJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);

		json_object_set_new(rootJ, "sceneSelected", json_integer(sceneSelected));
		json_object_set_new(rootJ, "sceneMode", json_integer(sceneMode));

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		padBrightness = json_real_value(json_object_get(rootJ, "padBrightness"));
		inputVisualize = json_boolean_value(json_object_get(rootJ, "inputVisualize"));
		outputClamp = json_boolean_value(json_object_get(rootJ, "outputClamp"));

		json_t* inputsJ = json_object_get(rootJ, "inputMode");
		json_t* inputJ;
		size_t inputIndex;
		json_array_foreach(inputsJ, inputIndex, inputJ) {
			inputMode[inputIndex] = (IN_MODE)json_integer_value(inputJ);
		}

		json_t* scenesJ = json_object_get(rootJ, "scenes");
		json_t* sceneJ;
		size_t sceneIndex;
		json_array_foreach(scenesJ, sceneIndex, sceneJ) {
			json_t* outputJ = json_object_get(sceneJ, "output");
			json_t* outputAtJ = json_object_get(sceneJ, "outputAt");
			json_t* matrixJ = json_object_get(sceneJ, "matrix");
			json_t* valueJ;
			size_t index;
			json_array_foreach(outputJ, index, valueJ) {
				scenes[sceneIndex].output[index] = (OUT_MODE)json_integer_value(valueJ);
			}
			json_array_foreach(outputAtJ, index, valueJ) {
				scenes[sceneIndex].outputAt[index] = json_real_value(valueJ);
			}
			json_array_foreach(matrixJ, index, valueJ) {
				scenes[sceneIndex].matrix[index / PORTS][index % PORTS] = json_real_value(valueJ);
			}
		}

		sceneSelected = json_integer_value(json_object_get(rootJ, "sceneSelected"));
		sceneMode = (SCENE_CV_MODE)json_integer_value(json_object_get(rootJ, "sceneMode"));

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				float v = scenes[sceneSelected].matrix[i][j];
				currentMatrix[i][j] = v;
				fader[i][j].reset(v);
			}
		}
	}
};



template < typename MODULE >
struct SceneLedDisplay : LedDisplayChoice {
	MODULE* module;

	SceneLedDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		box.size = Vec(16.9f, 16.f);
		textOffset = Vec(3.f, 11.5f);
	}

	void step() override {
		if (module) {
			text = string::f("%02d", module->sceneSelected + 1);
		}
		LedDisplayChoice::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		LedDisplayChoice::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();

		struct SceneItem : MenuItem {
			MODULE* module;
			int scene;
			
			void onAction(const event::Action& e) override {
				module->sceneSet(scene);
			}

			void step() override {
				rightText = module->sceneSelected == scene ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scene"));
		for (int i = 0; i < SCENE_COUNT; i++) {
			menu->addChild(construct<SceneItem>(&MenuItem::text, string::f("%02u", i + 1), &SceneItem::module, module, &SceneItem::scene, i));
		}
	}
};

template < typename MODULE >
struct InputLedDisplay : LedDisplayChoice {
	MODULE* module;
	int id;

	InputLedDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		box.size = Vec(25.1f, 16.f);
		textOffset = Vec(4.f, 11.5f);
	}

	void step() override {
		if (module) {
			switch (module->inputMode[id]) {
				case IN_MODE::IM_OFF:
					text = "OFF"; break;
				case IN_MODE::IM_DIRECT:
					text = "<->"; break;
				case IN_MODE::IM_FADE:
					text = "FAD"; break;
				case IN_MODE::IM_ADD_1V:
					text = "+1V"; break;
				case IN_MODE::IM_SUB_1V:
					text = "-1V"; break;
			}
		}
		LedDisplayChoice::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		LedDisplayChoice::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();

		struct InputItem : MenuItem {
			MODULE* module;
			int id;
			IN_MODE inMode;
			
			void onAction(const event::Action& e) override {
				module->inputMode[id] = inMode;
			}

			void step() override {
				rightText = module->inputMode[id] == inMode ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Input mode"));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Off", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_OFF));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Direct", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_DIRECT));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Linear fade", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_FADE));
		menu->addChild(construct<InputItem>(&MenuItem::text, "+1V", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_ADD_1V));
		menu->addChild(construct<InputItem>(&MenuItem::text, "-1V", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_SUB_1V));
	}
};


template < typename BASE, typename MODULE >
struct IntermixButtonLight : BASE {
	IntermixButtonLight() {
		this->box.size = math::Vec(26.5f, 26.5f);
	}

	void drawLight(const Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.8f, 0.8f, this->box.size.x - 2 * 0.8f, this->box.size.y - 2 * 0.8f, 3.4f);

		//nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFillColor(args.vg, this->color);
		nvgFill(args.vg);
	}
};

struct IntermixButton : app::SvgSwitch {
	IntermixButton() {
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Components/IntermixButton.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Components/IntermixButton1.svg")));
		fb->removeChild(shadow);
		delete shadow;
	}
};


struct IntermixWidget : ModuleWidget {
	const static int PORTS = 8;

	IntermixWidget(IntermixModule<PORTS>* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Intermix.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float xMin = 97.6f;
		float xMax = 307.4f;
		float yMin = 53.0f;
		float yMax = 264.3f;

		// Parameters and ports
		for (int i = 0; i < SCENE_COUNT; i++) {
			Vec v = Vec(23.1f, yMin + (yMax - yMin) / (SCENE_COUNT - 1) * i);
			addParam(createParamCentered<IntermixButton>(v, module, IntermixModule<PORTS>::SCENE_PARAM + i));
		}

		SceneLedDisplay<IntermixModule<PORTS>>* sceneLedDisplay = createWidgetCentered<SceneLedDisplay<IntermixModule<PORTS>>>(Vec(23.1f, 299.5f));
		sceneLedDisplay->module = module;
		addChild(sceneLedDisplay);
		addInput(createInputCentered<StoermelderPort>(Vec(23.1f, 323.7f), module, IntermixModule<PORTS>::SCENE_INPUT));

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (PORTS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addParam(createParamCentered<IntermixButton>(v, module, IntermixModule<PORTS>::MATRIX_PARAM + i * PORTS + j));
			}
		}

		for (int i = 0; i < PORTS; i++) {
			Vec v = Vec(59.4f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addParam(createParamCentered<IntermixButton>(v, module, IntermixModule<PORTS>::OUTPUT_PARAM + i));

			Vec vo = Vec(381.9f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addOutput(createOutputCentered<StoermelderPort>(vo, module, IntermixModule<PORTS>::OUTPUT + i));
			Vec vi2 = Vec(345.6f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addParam(createParamCentered<StoermelderSmallKnob>(vi2, module, IntermixModule<PORTS>::AT_PARAM + i));

			Vec vi0 = Vec(xMin + (xMax - xMin) / (PORTS - 1) * i, 299.5f);
			InputLedDisplay<IntermixModule<PORTS>>* inputLedDisplay = createWidgetCentered<InputLedDisplay<IntermixModule<PORTS>>>(vi0);
			inputLedDisplay->module = module;
			inputLedDisplay->id = i;
			addChild(inputLedDisplay);
			Vec vi1 = Vec(xMin + (xMax - xMin) / (PORTS - 1) * i, 323.7f);
			addInput(createInputCentered<StoermelderPort>(vi1, module, IntermixModule<PORTS>::INPUT + i));
		}

		addParam(createParamCentered<StoermelderTrimpot>(Vec(59.4f, 300.3f), module, IntermixModule<PORTS>::FADEIN_PARAM));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(59.4f, 327.3f), module, IntermixModule<PORTS>::FADEOUT_PARAM));

		// Lights
		for (int i = 0; i < SCENE_COUNT; i++) {
			Vec v = Vec(23.1f, yMin + (yMax - yMin) / (SCENE_COUNT - 1) * i);
			addChild(createLightCentered<IntermixButtonLight<YellowLight, IntermixModule<PORTS>>>(v, module, IntermixModule<PORTS>::SCENE_LIGHT + i));
		}

		for (int i = 0; i < PORTS; i++) {
			Vec v = Vec(59.4f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addChild(createLightCentered<IntermixButtonLight<RedLight, IntermixModule<PORTS>>>(v, module, IntermixModule<PORTS>::OUTPUT_LIGHT + i));
			for (int j = 0; j < PORTS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (PORTS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addChild(createLightCentered<IntermixButtonLight<RedGreenBlueLight, IntermixModule<PORTS>>>(v, module, IntermixModule<PORTS>::MATRIX_LIGHT + (i * PORTS + j) * 3));
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		IntermixModule<PORTS>* module = dynamic_cast<IntermixModule<PORTS>*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Intermix.md");
				t.detach();
			}
		};

		struct SceneModeMenuItem : MenuItem {
			SceneModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct SceneModeItem : MenuItem {
				IntermixModule<PORTS>* module;
				SCENE_CV_MODE sceneMode;
				
				void onAction(const event::Action& e) override {
					module->sceneMode = sceneMode;
				}

				void step() override {
					rightText = module->sceneMode == sceneMode ? "✔" : "";
					MenuItem::step();
				}
			};

			IntermixModule<PORTS>* module;
			int id;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "Trigger", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::TRIG_FWD));
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "0..10V", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::VOLT));
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "C4-G4", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::C4));
				return menu;
			}
		};

		struct OutputClampItem : MenuItem {
			IntermixModule<PORTS>* module;
			
			void onAction(const event::Action& e) override {
				module->outputClamp ^= true;
			}

			void step() override {
				rightText = module->outputClamp ? "✔" : "";
				MenuItem::step();
			}
		};

		struct InputVisualizeItem : MenuItem {
			IntermixModule<PORTS>* module;
			
			void onAction(const event::Action& e) override {
				module->inputVisualize ^= true;
			}

			void step() override {
				rightText = module->inputVisualize ? "✔" : "";
				MenuItem::step();
			}
		};

		struct BrightnessSlider : ui::Slider {
			struct BrightnessQuantity : Quantity {
				IntermixModule<PORTS>* module;
				const float MAX = 2.f;

				BrightnessQuantity(IntermixModule<PORTS>* module) {
					this->module = module;
				}
				void setValue(float value) override {
					module->padBrightness = math::clamp(value * MAX, 0.f, MAX);
				}
				float getValue() override {
					return module->padBrightness / MAX;
				}
				float getDefaultValue() override {
					return (1.f / MAX) * 0.75f;
				}
				float getDisplayValue() override {
					return getValue() * 100 * MAX;
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue / (100 * MAX));
				}
				std::string getLabel() override {
					return "Pad brightness";
				}
				std::string getUnit() override {
					return "%";
				}
			};

			BrightnessSlider(IntermixModule<PORTS>* module) {
				this->box.size.x = 200.0;
				quantity = new BrightnessQuantity(module);
			}
			~BrightnessSlider() {
				delete quantity;
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SceneModeMenuItem>(&MenuItem::text, "SCENE-port", &SceneModeMenuItem::module, module));
		menu->addChild(construct<OutputClampItem>(&MenuItem::text, "Limit output to -10..10V", &OutputClampItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(new BrightnessSlider(module));
		menu->addChild(construct<InputVisualizeItem>(&MenuItem::text, "Visualize input on pads", &InputVisualizeItem::module, module));
	}
};

} // namespace Intermix

Model* modelIntermix = createModel<Intermix::IntermixModule<8>, Intermix::IntermixWidget>("Intermix");