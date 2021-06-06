#include "plugin.hpp"
#include "digital.hpp"
#include "IntermixBase.hpp"
#include "components/MatrixButton.hpp"

namespace StoermelderPackOne {
namespace Intermix {

const int SCENE_MAX = 8;

enum SCENE_CV_MODE {
	OFF = -1,
	TRIG_FWD = 0,
	VOLT = 8,
	C4 = 9,
	ARM = 7
};

enum IN_MODE {
	IM_OFF = 0,
	IM_DIRECT = 1,
	IM_FADE = 2,
	IM_SUB_12C = 12,
	IM_SUB_11C = 13,
	IM_SUB_10C = 14,
	IM_SUB_09C = 15,
	IM_SUB_08C = 16,
	IM_SUB_07C = 17,
	IM_SUB_06C = 18,
	IM_SUB_05C = 19,
	IM_SUB_04C = 20,
	IM_SUB_03C = 21,
	IM_SUB_02C = 22,
	IM_SUB_01C = 23,
	IM_ADD_01C = 25,
	IM_ADD_02C = 26,
	IM_ADD_03C = 27,
	IM_ADD_04C = 28,
	IM_ADD_05C = 29,
	IM_ADD_06C = 30,
	IM_ADD_07C = 31,
	IM_ADD_08C = 32,
	IM_ADD_09C = 33,
	IM_ADD_10C = 34,
	IM_ADD_11C = 35,
	IM_ADD_12C = 36
};

enum OUT_MODE {
	OM_OFF = 0,
	OM_OUT = 1
};

template < int PORTS >
struct IntermixModule : Module, IntermixBase<PORTS> {
	enum ParamIds {
		ENUMS(PARAM_MATRIX, PORTS * PORTS),
		ENUMS(PARAM_OUTPUT, PORTS),
		ENUMS(PARAM_SCENE, SCENE_MAX),
		ENUMS(PARAM_AT, PORTS),
		PARAM_FADEIN,
		PARAM_FADEOUT,
		ENUMS(PARAM_X_MAP, PORTS),
		ENUMS(PARAM_Y_MAP, PORTS),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, PORTS),
		INPUT_SCENE,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_MATRIX, PORTS * PORTS * 3),
		ENUMS(LIGHT_OUTPUT, PORTS),
		ENUMS(LIGHT_SCENE, PORTS),
		NUM_LIGHTS
	};

	struct SceneData {
		IN_MODE input[PORTS]; 
		alignas(16) OUT_MODE output[PORTS];
		alignas(16) float outputAt[PORTS];
		alignas(16) float matrix[PORTS][PORTS];
	};

	alignas(16) float currentMatrix[PORTS][PORTS];

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	float padBrightness;
	/** [Stored to JSON] */
	bool inputVisualize;
	/** [Stored to JSON] */
	IN_MODE inputMode[PORTS];
	/** [Stored to JSON] */
	bool outputClamp;
	/** [Stored to JSON] */
	SceneData scenes[SCENE_MAX];
	/** [Stored to JSON] */
	int sceneSelected = 0;
	/** [Stored to JSON] */
	SCENE_CV_MODE sceneMode;
	/** [Stored to JSON] */
	bool sceneInputMode;
	/** [Stored to JSON] */
	bool sceneAtMode;
	/** [Stored to JSON] */
	int sceneCount;

	int sceneNext = -1;

	/** [Stored to JSON] */
	int channelCount = 1;

	LinearFade fader[PORTS][PORTS][PORT_MAX_CHANNELS];
	//dsp::TSlewLimiter<simd::float_4> outputAtSlew[PORTS / 4];

	dsp::SchmittTrigger sceneTrigger;
	dsp::SchmittTrigger mapTrigger[PORTS];
	dsp::ClockDivider sceneDivider;
	dsp::ClockDivider lightDivider;

	IntermixModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < SCENE_MAX; i++) {
			configParam(PARAM_SCENE + i, 0.f, 1.f, 0.f, string::f("Scene %i", i + 1));
		}
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				configParam<MatrixButtonParamQuantity>(PARAM_MATRIX + i * PORTS + j, 0.f, 1.f, 0.f, string::f("Input %i to Output %i", j + 1, i + 1));
			}
			configParam(PARAM_OUTPUT + i, 0.f, 1.f, 0.f, string::f("Output %i disable", i + 1));
			configParam(PARAM_AT + i, -2.f, 2.f, 1.f, string::f("Output %i attenuverter", i + 1), "x");
			configParam(PARAM_X_MAP + i, 0.f, 1.f, 0.f, string::f("Matrix col %i", i + 1));
			configParam(PARAM_Y_MAP + i, 0.f, 1.f, 0.f, string::f("Matrix row %i", i + 1));
		}
		configParam(PARAM_FADEIN, 0.f, 4.f, 0.f, "Fade in", "s");
		configParam(PARAM_FADEOUT, 0.f, 4.f, 0.f, "Fade out", "s");
		sceneDivider.setDivision(32);
		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		padBrightness = 0.75f;
		inputVisualize = false;
		outputClamp = true;
		for (int i = 0; i < SCENE_MAX; i++) {
			inputMode[i] = IM_DIRECT;
			for (int j = 0; j < PORTS; j++) {
				scenes[i].input[j] = IM_DIRECT;
				scenes[i].output[j] = OM_OUT;
				scenes[i].outputAt[j] = 1.f;
				for (int k = 0; k < PORTS; k++) {
					scenes[i].matrix[j][k] = 0.f;
				}
			}
		}
		sceneMode = SCENE_CV_MODE::TRIG_FWD;
		sceneInputMode = false;
		sceneAtMode = true;
		sceneCount = SCENE_MAX;
		sceneSet(0);
		Module::onReset();
	}

	void process(const ProcessArgs& args) override {
		if (inputs[INPUT_SCENE].isConnected()) {
			switch (sceneMode) {
				case SCENE_CV_MODE::OFF: {
					break;
				}
				case SCENE_CV_MODE::TRIG_FWD: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						int s = (sceneSelected + 1) % sceneCount;
						sceneSet(s);
					}
					break;
				}
				case SCENE_CV_MODE::C4: {
					int s = std::round(clamp(inputs[INPUT_SCENE].getVoltage() * 12.f, 0.f, sceneCount - 1.f));
					sceneSet(s);
					break;
				}
				case SCENE_CV_MODE::VOLT: {
					int s = std::floor(rescale(inputs[INPUT_SCENE].getVoltage(), 0.f, 10.f, 0, sceneCount - 1e-3f));
					sceneSet(s);
					break;
				}
				case SCENE_CV_MODE::ARM: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						sceneSet(sceneNext);
					}
					break;
				}
			}
		}

		if (sceneDivider.process()) {
			int sceneFound = -1;
			for (int i = 0; i < SCENE_MAX; i++) {
				if (params[PARAM_SCENE + i].getValue() > 0.f) {
					if (i != sceneSelected) {
						if (sceneMode == SCENE_CV_MODE::ARM)
							sceneNext = i;
						else
							sceneSet(i);
						break;
					}
					sceneFound = i;
				}
			}
			if (sceneFound == -1) {
				params[PARAM_SCENE + sceneSelected].setValue(1.f);
			}

			for (int i = 0; i < PORTS; i++) {
				if (params[PARAM_X_MAP + i].getValue() > 0.f) {
					for (int j = 0; j < PORTS; j++) {
						if (mapTrigger[j].process(params[PARAM_Y_MAP + j].getValue())) {
							float v = params[PARAM_MATRIX + j * PORTS + i].getValue();
							v = v == 1.f ? 0.f : 1.f;
							params[PARAM_MATRIX + j * PORTS + i].setValue(v);
						}
					}
				}
			}

			float f1 = params[PARAM_FADEIN].getValue();
			float f2 = params[PARAM_FADEOUT].getValue();
			for (int i = 0; i < PORTS; i++) {
				scenes[sceneSelected].output[i] = params[PARAM_OUTPUT + i].getValue() == 0.f ? OM_OUT : OM_OFF;
				scenes[sceneSelected].outputAt[i] = params[PARAM_AT + i].getValue();
				for (int j = 0; j < PORTS; j++) {
					float p = params[PARAM_MATRIX + j * PORTS + i].getValue();
					for (int c = 0; c < channelCount; c++) {
						fader[i][j][c].setRiseFall(f1, f2);
						if (p != scenes[sceneSelected].matrix[i][j] && p == 1.f) fader[i][j][c].triggerFadeIn();
						if (p != scenes[sceneSelected].matrix[i][j] && p == 0.f) fader[i][j][c].triggerFadeOut();
					}
					scenes[sceneSelected].matrix[i][j] = currentMatrix[i][j] = p;
				}
			}
		}

		// DSP processing
		for (int c = 0; c < channelCount; c++) {
			simd::float_4 out[PORTS / 4] = {};
			for (int i = 0; i < PORTS; i++) {
				float v;
				IN_MODE mode = sceneInputMode ? scenes[sceneSelected].input[i] : inputMode[i];
				switch (mode) {
					case IN_MODE::IM_OFF:
						continue;
					case IN_MODE::IM_DIRECT:
						if (!inputs[INPUT + i].isConnected()) continue;
						v = inputs[INPUT + i].getPolyVoltage(c);
						break;
					case IN_MODE::IM_FADE:
						if (!inputs[INPUT + i].isConnected()) continue;
						v = inputs[INPUT + i].getPolyVoltage(c);
						for (int j = 0; j < PORTS; j++) {
							currentMatrix[i][j] = fader[i][j][c].process(args.sampleTime);
						}
						break;
					default:
						v = (mode - 24) / 12.f;
						break;
				}

				for (int j = 0; j < PORTS; j+=4) {
					simd::float_4 v1 = simd::float_4::load(&currentMatrix[i][j]);
					simd::float_4 v2 = v1 * simd::float_4(v);
					out[j / 4] += v2;
				}
			}


			// -- Standard code --
			/*
			for (int i = 0; i < PORTS; i++) {
				float v = scenes[sceneSelected].output[i] == OM_OUT ? out[i / 4][i % 4] : 0.f;
				if (outputClamp) v = clamp(v, -10.f, 10.f);
				outputs[OUTPUT + i].setVoltage(v);
			}
			*/
			// -- Standard code --

			// -- SIMD code --
			simd::float_4 oc = outputClamp;
			for (int j = 0; j < PORTS; j+=4) {
				// Check for OUT_MODE
				simd::int32_4 o1 = simd::int32_4::load((int32_t*)&scenes[sceneSelected].output[j]);
				simd::float_4 o2 = simd::float_4(o1 != 0) == -1.f;
				out[j / 4] = simd::ifelse(o2, out[j / 4], simd::float_4::zero());
				// Clamp if outputClamp it set
				out[j / 4] = simd::ifelse(oc == 1.f, simd::clamp(out[j / 4], -10.f, 10.f), out[j / 4]);
				// Attenuverters
				simd::float_4 at = simd::float_4::load(&scenes[sceneSelected].outputAt[j]);
				//at = outputAtSlew[j / 4].process(args.sampleTime, at);
				out[j / 4] *= at;
			}

			for (int i = 0; i < PORTS; i++) {
				outputs[OUTPUT + i].setVoltage(out[i / 4][i % 4], c);
			}
			// -- SIMD code --
		}

		for (int i = 0; i < PORTS; i++) {
			outputs[OUTPUT + i].setChannels(channelCount);
		}

		// Lights
		if (lightDivider.process()) {
			float s = lightDivider.getDivision() * args.sampleTime;

			for (int i = 0; i < SCENE_MAX; i++) {
				float v = (i == sceneSelected) * padBrightness;
				v = std::max(i < sceneCount ? 0.05f : 0.f, v);
				lights[LIGHT_SCENE + i].setSmoothBrightness(v, s);
			}

			if (inputVisualize) {
				float in[PORTS];
				for (int i = 0; i < PORTS; i++) {
					in[i] = rescale(inputs[INPUT + i].getVoltage(), -10.f, 10.f, -1.f, 1.f);
				}
				for (int i = 0; i < PORTS; i++) {
					for (int j = 0; j < PORTS; j++) {
						float v = currentMatrix[j][i] * (in[j] * padBrightness);
						lights[LIGHT_MATRIX + (i * PORTS + j) * 3 + 0].setBrightness(v < 0.f ? -v : 0.f);
						lights[LIGHT_MATRIX + (i * PORTS + j) * 3 + 1].setBrightness(v > 0.f ?  v : 0.f);
						lights[LIGHT_MATRIX + (i * PORTS + j) * 3 + 2].setBrightness(0.f);
					}
				}
			}
			else {
				for (int i = 0; i < PORTS; i++) {
					for (int j = 0; j < PORTS; j++) {
						float v = currentMatrix[j][i] * padBrightness;
						lights[LIGHT_MATRIX + (i * PORTS + j) * 3 + 0].setSmoothBrightness(v, s);
						lights[LIGHT_MATRIX + (i * PORTS + j) * 3 + 1].setSmoothBrightness(v, s);
						lights[LIGHT_MATRIX + (i * PORTS + j) * 3 + 2].setSmoothBrightness(v, s);
					}
				}
			}
			for (int i = 0; i < PORTS; i++) {
				float v = (scenes[sceneSelected].output[i] != OM_OUT) * padBrightness;
				lights[LIGHT_OUTPUT + i].setSmoothBrightness(v, s);
			}
		}

		// Expander
		if (rightExpander.module && rightExpander.module->model == modelIntermixGate) {
			rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
			rightExpander.messageFlipRequested = true;
		}
	}

	inline void sceneSet(int scene) {
		if (sceneSelected == scene) return;
		if (scene < 0) return;
		int scenePrevious = sceneSelected;
		sceneSelected = std::min(scene, sceneCount - 1);
		sceneNext = -1;

		for (int i = 0; i < SCENE_MAX; i++) {
			params[PARAM_SCENE + i].setValue(i == sceneSelected);
		}

		/*
		simd::float_4 at[PORTS / 4];
		float f1 = params[PARAM_FADEIN].getValue();
		float f2 = params[PARAM_FADEOUT].getValue();
		*/
		for (int i = 0; i < PORTS; i++) {
			params[PARAM_OUTPUT + i].setValue(scenes[sceneSelected].output[i] != OM_OUT);

			/*
			float at0 = params[PARAM_AT + i].getValue();
			float at1 = scenes[sceneSelected].outputAt[i];
			at[i / 4][i % 4] = at0 > at1 ? (at0 - at1) : (at1 - at0);
			*/
			if (sceneAtMode) {
				params[PARAM_AT + i].setValue(scenes[sceneSelected].outputAt[i]);
			}
			for (int j = 0; j < PORTS; j++) {
				float p = scenes[sceneSelected].matrix[i][j];
				params[PARAM_MATRIX + j * PORTS + i].setValue(p);
				for (int c = 0; c < channelCount; c++) {
					if (p != scenes[scenePrevious].matrix[i][j] && p == 1.f) fader[i][j][c].triggerFadeIn();
					if (p != scenes[scenePrevious].matrix[i][j] && p == 0.f) fader[i][j][c].triggerFadeOut();
				}
				currentMatrix[i][j] = p;
			}
		}
		/*
		for (int i = 0; i < PORTS / 4; i++) {
			outputAtSlew[i].setRiseFall(at[i] / f1, at[i] / f2);
		}
		*/
	}

	void sceneCopy(int scene) {
		if (sceneSelected == scene) return;
		for (int i = 0; i < PORTS; i++) {
			scenes[scene].input[i] = scenes[sceneSelected].input[i];
			scenes[scene].output[i] = scenes[sceneSelected].output[i];
			scenes[scene].outputAt[i] = scenes[sceneSelected].outputAt[i];
			for (int j = 0; j < PORTS; j++) {
				scenes[scene].matrix[i][j] = scenes[sceneSelected].matrix[i][j];
			}
		}
	}

	void sceneReset() {
		for (int i = 0; i < PORTS; i++) {
			scenes[sceneSelected].input[i] = IN_MODE::IM_DIRECT;
			scenes[sceneSelected].output[i] = OUT_MODE::OM_OUT;
			params[PARAM_OUTPUT + i].setValue(0.f);
			scenes[sceneSelected].outputAt[i] = 1.f;
			params[PARAM_AT + i].setValue(1.f);
			for (int j = 0; j < PORTS; j++) {
				scenes[sceneSelected].matrix[i][j] = 0.f;
				params[PARAM_MATRIX + j * PORTS + i].setValue(0.f);
				currentMatrix[i][j] = 0.f;
				for (int c = 0; c < channelCount; c++) {
					fader[i][j][c].reset(0.f);
				}
			}
		}
	}

	void sceneSetCount(int count) {
		sceneCount = count;
		sceneSelected = std::min(sceneSelected, sceneCount - 1);
	}

	typename IntermixBase<PORTS>::IntermixMatrix expGetCurrentMatrix() override {
		return currentMatrix;
	}

	typename IntermixBase<PORTS>::IntermixMatrix expGetMatrix() override {
		return scenes[sceneSelected].matrix;
	}

	int expGetChannelCount() override { 
		return channelCount; 
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "padBrightness", json_real(padBrightness));
		json_object_set_new(rootJ, "inputVisualize", json_boolean(inputVisualize));
		json_object_set_new(rootJ, "outputClamp", json_boolean(outputClamp));
		json_object_set_new(rootJ, "channelCount", json_integer(channelCount));

		json_t* inputsJ = json_array();
		for (int i = 0; i < PORTS; i++) {
			json_array_append_new(inputsJ, json_integer(inputMode[i]));
		}
		json_object_set_new(rootJ, "inputMode", inputsJ);

		json_t* scenesJ = json_array();
		for (int i = 0; i < SCENE_MAX; i++) {
			json_t* inputJ = json_array();
			json_t* outputJ = json_array();
			json_t* outputAtJ = json_array();
			json_t* matrixJ = json_array();
			for (int j = 0; j < PORTS; j++) {
				json_array_append_new(inputJ, json_integer(scenes[i].input[j]));
				json_array_append_new(outputJ, json_integer(scenes[i].output[j]));
				json_array_append_new(outputAtJ, json_real(scenes[i].outputAt[j]));
				for (int k = 0; k < PORTS; k++) {
					json_array_append_new(matrixJ, json_real(scenes[i].matrix[j][k]));
				}
			}

			json_t* sceneJ = json_object();
			json_object_set_new(sceneJ, "input", inputJ);
			json_object_set_new(sceneJ, "output", outputJ);
			json_object_set_new(sceneJ, "outputAt", outputAtJ);
			json_object_set_new(sceneJ, "matrix", matrixJ);
			json_array_append_new(scenesJ, sceneJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);

		json_object_set_new(rootJ, "sceneSelected", json_integer(sceneSelected));
		json_object_set_new(rootJ, "sceneMode", json_integer(sceneMode));
		json_object_set_new(rootJ, "sceneInputMode", json_boolean(sceneInputMode));
		json_object_set_new(rootJ, "sceneAtMode", json_boolean(sceneAtMode));
		json_object_set_new(rootJ, "sceneCount", json_integer(sceneCount));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		padBrightness = json_real_value(json_object_get(rootJ, "padBrightness"));
		inputVisualize = json_boolean_value(json_object_get(rootJ, "inputVisualize"));
		outputClamp = json_boolean_value(json_object_get(rootJ, "outputClamp"));
		channelCount = json_integer_value(json_object_get(rootJ, "channelCount"));

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
			json_t* inputJ = json_object_get(sceneJ, "input");
			json_t* outputJ = json_object_get(sceneJ, "output");
			json_t* outputAtJ = json_object_get(sceneJ, "outputAt");
			json_t* matrixJ = json_object_get(sceneJ, "matrix");
			json_t* valueJ;
			size_t index;
			json_array_foreach(inputJ, index, valueJ) {
				scenes[sceneIndex].input[index] = (IN_MODE)json_integer_value(valueJ);
			}
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
		sceneInputMode = json_boolean_value(json_object_get(rootJ, "sceneInputMode"));
		json_t* sceneAtModeJ = json_object_get(rootJ, "sceneAtMode");
		if (sceneAtModeJ) sceneAtMode = json_boolean_value(sceneAtModeJ);
		json_t* sceneCountJ = json_object_get(rootJ, "sceneCount");
		if (sceneCountJ) sceneCount = json_integer_value(sceneCountJ);

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				float v = scenes[sceneSelected].matrix[i][j];
				currentMatrix[i][j] = v;
				for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
					fader[i][j][c].reset(v);
				}
			}
		}
	}
};



template < typename MODULE >
struct InputLedDisplay : StoermelderLedDisplay {
	MODULE* module;
	int id;

	void step() override {
		if (module) {
			IN_MODE mode = module->sceneInputMode ? module->scenes[module->sceneSelected].input[id] : module->inputMode[id];
			switch (mode) {
				case IN_MODE::IM_OFF:
					text = "OFF"; break;
				case IN_MODE::IM_DIRECT:
					text = "<->"; break;
				case IN_MODE::IM_FADE:
					text = "FAD"; break;
				default:
					text = (mode - 24 > 0 ? "+" : "-") + string::f("%02i", std::abs(mode - 24));
					break;
			}
		} 
		else {
			text = "-X-";
		}
		StoermelderLedDisplay::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		StoermelderLedDisplay::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();

		struct InputItem : MenuItem {
			MODULE* module;
			int id;
			IN_MODE inMode;
			
			void onAction(const event::Action& e) override {
				if (module->sceneInputMode)
					module->scenes[module->sceneSelected].input[id] = inMode;
				else
					module->inputMode[id] = inMode;
			}

			void step() override {
				if (module->sceneInputMode)
					rightText = module->scenes[module->sceneSelected].input[id] == inMode ? "✔" : "";
				else
					rightText = module->inputMode[id] == inMode ? "✔" : "";
				MenuItem::step();
			}
		};

		struct InputSubtractItem : MenuItem {
			MODULE* module;
			int id;
			InputSubtractItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (int i = 12; i > 0; i--) {
					menu->addChild(construct<InputItem>(&MenuItem::text, string::f("-%02i cent", i), &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, (IN_MODE)(24 - i)));
				}
				return menu;
			}
		};

		struct InputAddItem : MenuItem {
			MODULE* module;
			int id;
			InputAddItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (int i = 1; i <= 12; i++) {
					menu->addChild(construct<InputItem>(&MenuItem::text, string::f("+%02i cent", i), &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, (IN_MODE)(24 + i)));
				}
				return menu;
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Input mode"));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Off", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_OFF));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Direct", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_DIRECT));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Linear fade", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_FADE));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Constant voltage"));
		menu->addChild(construct<InputSubtractItem>(&MenuItem::text, "Subtract", &InputSubtractItem::module, module, &InputSubtractItem::id, id));
		menu->addChild(construct<InputAddItem>(&MenuItem::text, "Add", &InputAddItem::module, module, &InputAddItem::id, id));
	}
};



/*
struct IntermixKnob : app::SvgKnob {
	IntermixKnob() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/IntermixKnob.svg")));
		sw->setSize(Vec(22.7f, 22.7f));
		fb->removeChild(shadow);
		delete shadow;
	}
};
*/

struct IntermixWidget : ThemedModuleWidget<IntermixModule<8>> {
	const static int PORTS = 8;

	IntermixWidget(IntermixModule<PORTS>* module)
		: ThemedModuleWidget<IntermixModule<8>>(module, "Intermix") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float xMin = 61.9f;
		float xMax = 271.7f;
		float yMin = 53.0f;
		float yMax = 264.3f;

		// Parameters and ports
		for (int i = 0; i < SCENE_MAX; i++) {
			Vec v = Vec(23.1f, yMin + (yMax - yMin) / (SCENE_MAX - 1) * i);
			addParam(createParamCentered<MatrixButton>(v, module, IntermixModule<PORTS>::PARAM_SCENE + i));
		}

		SceneLedDisplay<IntermixModule<PORTS>, SCENE_MAX>* sceneLedDisplay = createWidgetCentered<SceneLedDisplay<IntermixModule<PORTS>, SCENE_MAX>>(Vec(23.1f, 302.3f));
		sceneLedDisplay->module = module;
		addChild(sceneLedDisplay);
		addInput(createInputCentered<StoermelderPort>(Vec(23.1f, 326.7f), module, IntermixModule<PORTS>::INPUT_SCENE));

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (PORTS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addParam(createParamCentered<MatrixButton>(v, module, IntermixModule<PORTS>::PARAM_MATRIX + i * PORTS + j));
			}
		}

		struct DummyMapButton : ParamWidget {
			DummyMapButton() {
				this->box.size = Vec(5.f, 5.f);
			}
		};

		for (int i = 0; i < PORTS; i++) {
			Vec v = Vec(313.5f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addParam(createParamCentered<MatrixButton>(v, module, IntermixModule<PORTS>::PARAM_OUTPUT + i));

			Vec vo1 = Vec(381.9f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addOutput(createOutputCentered<StoermelderPort>(vo1, module, IntermixModule<PORTS>::OUTPUT + i));
			Vec vo2 = Vec(343.6f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addParam(createParamCentered<StoermelderSmallKnob>(vo2, module, IntermixModule<PORTS>::PARAM_AT + i));
			Vec vo3 = Vec(289.2f, yMin + (yMax - yMin) / (PORTS - 1) * i - 11.2f);
			addParam(createParamCentered<DummyMapButton>(vo3, module, IntermixModule<PORTS>::PARAM_Y_MAP + i));

			Vec vi0 = Vec(xMin + (xMax - xMin) / (PORTS - 1) * i, 302.3f);
			InputLedDisplay<IntermixModule<PORTS>>* inputLedDisplay = createWidgetCentered<InputLedDisplay<IntermixModule<PORTS>>>(vi0);
			inputLedDisplay->module = module;
			inputLedDisplay->id = i;
			addChild(inputLedDisplay);
			Vec vi1 = Vec(xMin + (xMax - xMin) / (PORTS - 1) * i, 326.7f);
			addInput(createInputCentered<StoermelderPort>(vi1, module, IntermixModule<PORTS>::INPUT + i));
			Vec vi2 = Vec(xMin + (xMax - xMin) / (PORTS - 1) * i - 11.2f, 281.9f);
			addParam(createParamCentered<DummyMapButton>(vi2, module, IntermixModule<PORTS>::PARAM_X_MAP + i));
		}

		addParam(createParamCentered<StoermelderTrimpot>(Vec(311.7f, 300.8f), module, IntermixModule<PORTS>::PARAM_FADEIN));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(311.7f, 330.1f), module, IntermixModule<PORTS>::PARAM_FADEOUT));

		// Lights
		for (int i = 0; i < SCENE_MAX; i++) {
			Vec v = Vec(23.1f, yMin + (yMax - yMin) / (SCENE_MAX - 1) * i);
			addChild(createLightCentered<MatrixButtonLight<YellowLight, IntermixModule<PORTS>>>(v, module, IntermixModule<PORTS>::LIGHT_SCENE + i));
		}

		for (int i = 0; i < PORTS; i++) {
			Vec v = Vec(313.5f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addChild(createLightCentered<MatrixButtonLight<RedLight, IntermixModule<PORTS>>>(v, module, IntermixModule<PORTS>::LIGHT_OUTPUT + i));
			for (int j = 0; j < PORTS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (PORTS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addChild(createLightCentered<MatrixButtonLight<RedGreenBlueLight, IntermixModule<PORTS>>>(v, module, IntermixModule<PORTS>::LIGHT_MATRIX + (i * PORTS + j) * 3));
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<IntermixModule<8>>::appendContextMenu(menu);
		IntermixModule<PORTS>* module = dynamic_cast<IntermixModule<PORTS>*>(this->module);
		assert(module);

		struct NumberOfChannelsMenuItem : MenuItem {
			NumberOfChannelsMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct NumberOfChannelsItem : MenuItem {
				IntermixModule<PORTS>* module;
				int channelCount;
				void onAction(const event::Action& e) override {
					module->channelCount = channelCount;
				}
				void step() override {
					rightText = module->channelCount == channelCount ? "✔" : "";
					MenuItem::step();
				}
			};

			IntermixModule<PORTS>* module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (int i = 1; i <= PORT_MAX_CHANNELS; i++) {
					menu->addChild(construct<NumberOfChannelsItem>(&MenuItem::text, string::f("%i", i), &NumberOfChannelsItem::module, module, &NumberOfChannelsItem::channelCount, i));
				}
				return menu;
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
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "Off", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::OFF));
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "Trigger", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::TRIG_FWD));
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "0..10V", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::VOLT));
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "C4-G4", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::C4));
				menu->addChild(construct<SceneModeItem>(&MenuItem::text, "Arm", &SceneModeItem::module, module, &SceneModeItem::sceneMode, SCENE_CV_MODE::ARM));
				return menu;
			}
		};

		struct SceneInputModeItem : MenuItem {
			IntermixModule<PORTS>* module;
			void onAction(const event::Action& e) override {
				module->sceneInputMode ^= true;
			}
			void step() override {
				rightText = module->sceneInputMode ? "✔" : "";
				MenuItem::step();
			}
		};

		struct SceneAtModeItem : MenuItem {
			IntermixModule<PORTS>* module;
			void onAction(const event::Action& e) override {
				module->sceneAtMode ^= true;
			}
			void step() override {
				rightText = module->sceneAtMode ? "✔" : "";
				MenuItem::step();
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

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<NumberOfChannelsMenuItem>(&MenuItem::text, "Channels", &NumberOfChannelsMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SceneModeMenuItem>(&MenuItem::text, "Port SCENE-mode", &SceneModeMenuItem::module, module));
		menu->addChild(construct<SceneInputModeItem>(&MenuItem::text, "Include input-mode in scenes", &SceneInputModeItem::module, module));
		menu->addChild(construct<SceneAtModeItem>(&MenuItem::text, "Include attenuverters in scenes", &SceneAtModeItem::module, module));
		menu->addChild(construct<OutputClampItem>(&MenuItem::text, "Limit output to -10..10V", &OutputClampItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(new BrightnessSlider(module));
		menu->addChild(construct<InputVisualizeItem>(&MenuItem::text, "Visualize input on pads", &InputVisualizeItem::module, module));
	}
};

} // namespace Intermix
} // namespace StoermelderPackOne

Model* modelIntermix = createModel<StoermelderPackOne::Intermix::IntermixModule<8>, StoermelderPackOne::Intermix::IntermixWidget>("Intermix");