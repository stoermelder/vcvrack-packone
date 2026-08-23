#include "../../plugin.hpp"
#include "../../utils/digital.hpp"
#include "../../components/Knobs.hpp"
#include "../../components/MatrixButton.hpp"
#include "IntermixBase.hpp"

namespace StoermelderPackOne {
namespace Intermix {

const int SCENE_MAX = 8;

enum SCENE_CV_MODE {
	OFF = -1,
	TRIG_FWD = 0,
	TRIG_REV = 1,
	TRIG_PINGPONG = 2,
	TRIG_ALT = 3,
	TRIG_RANDOM = 4,
	TRIG_RANDOM_WO_REPEAT = 5,
	TRIG_RANDOM_WALK = 6,
	TRIG_SHUFFLE = 10,
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
struct IntermixModule : IntermixChainModule, IntermixBase<PORTS> {
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
		INPUT_RESET,
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
	int sceneCount = SCENE_MAX;
	/** [Stored to JSON] */
	bool sceneLock;

	int sceneNext = -1;
	int sceneCvModeDir = 1;
	int sceneCvModeAlt = 0;
	std::vector<int> sceneCvModeShuffle;

	/** [Stored to JSON] */
	int channelCount = 1;

	/** [Stored to JSON] */
	FADE_LENGTH fadeLengthMode = FADE_LENGTH_4S;

	LinearFade fader[PORTS][PORTS][PORT_MAX_CHANNELS];
	uint32_t fadeInTs[PORTS] = {};
	uint32_t fadeOutTs[PORTS] = {};
	//dsp::TSlewLimiter<simd::float_4> outputAtSlew[PORTS / 4];

	uint32_t ts = 0;

	dsp::SchmittTrigger sceneTrigger;
	dsp::SchmittTrigger mapTrigger[PORTS];
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;
	ClockDividerEx sceneDivider;
	ClockDividerEx lightDivider;

	IntermixModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_SCENE, "Scene selection");
		inputInfos[INPUT_SCENE]->description = "Triggers scenes depending on the operating mode selected on the context menu.";
		configInput(INPUT_RESET, "Scene reset");
		inputInfos[INPUT_RESET]->description = "Resets the current scene according to the selected scene-mode.";
		for (int i = 0; i < SCENE_MAX; i++) {
			configSwitch(PARAM_SCENE + i, 0.f, 1.f, 0.f, string::f("Scene %i", i + 1));
		}
		for (int i = 0; i < PORTS; i++) {
			configInput(INPUT + i, string::f("Signal %i", i + 1));
			configOutput(OUTPUT + i, string::f("Mix %i", i + 1));
			outputInfos[OUTPUT + i]->description = "Polyphonic mix bus.";
			for (int j = 0; j < PORTS; j++) {
				configParam<MatrixButtonParamQuantity>(PARAM_MATRIX + i * PORTS + j, 0.f, 1.f, 0.f, string::f("Input %i to Output %i", j + 1, i + 1));
			}
			configSwitch(PARAM_OUTPUT + i, 0.f, 1.f, 0.f, string::f("Output %i disable", i + 1));
			configParam(PARAM_AT + i, -2.f, 2.f, 1.f, string::f("Output %i attenuverter", i + 1), "x");
			configSwitch(PARAM_X_MAP + i, 0.f, 1.f, 0.f, string::f("Matrix col %i", i + 1));
			configSwitch(PARAM_Y_MAP + i, 0.f, 1.f, 0.f, string::f("Matrix row %i", i + 1));
		}
		auto pqFadeIn = configParam<FadeLengthParamQuantity<IntermixModule<PORTS>>>(PARAM_FADEIN, 0.f, 4.f, 0.f, "Fade in", "s");
		pqFadeIn->module = this;
		pqFadeIn->description = "Fade-in time applied to a signal when a matrix button is engaged.";
		auto pqFadeOut = configParam<FadeLengthParamQuantity<IntermixModule<PORTS>>>(PARAM_FADEOUT, 0.f, 4.f, 0.f, "Fade out", "s");
		pqFadeOut->module = this;
		pqFadeOut->description = "Fade-out time applied to a signal when a matrix button is disengaged.";
		sceneDivider.setDivision(64);

		ResetEvent re;
		onReset(re);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		lightDivider.setDivision(e.sampleRate / 100.f);
	}
	
	void onReset(const ResetEvent& e) override {
		padBrightness = 0.75f;
		inputVisualize = false;
		outputClamp = true;
		for (int i = 0; i < PORTS; i++) {
			inputMode[i] = IM_DIRECT;
		}
		for (int i = 0; i < SCENE_MAX; i++) {
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
		sceneLock = false;
		sceneSet(0);
		Module::onReset(e);
	}

	void process(const ProcessArgs& args) override {
		ts++;

		// Reset input with cooldown
		if (inputs[INPUT_RESET].isConnected()) {
			if (resetTrigger.process(inputs[INPUT_RESET].getVoltage())) {
				resetTimer.reset();
				switch (sceneMode) {
					case SCENE_CV_MODE::TRIG_FWD: {
						sceneSet(0);
						break;
					}
					case SCENE_CV_MODE::TRIG_REV: {
						sceneSet(sceneCount - 1);
						break;
					}
					case SCENE_CV_MODE::TRIG_PINGPONG: {
						sceneCvModeDir = 1;
						sceneSet(0);
						break;
					}
					case SCENE_CV_MODE::TRIG_ALT: {
						sceneCvModeDir = 1;
						sceneCvModeAlt = 0;
						sceneSet(0);
						break;
					}
					case SCENE_CV_MODE::TRIG_SHUFFLE: {
						sceneCvModeShuffle.clear();
						for (int i = 0; i < sceneCount; i++) {
							sceneCvModeShuffle.push_back(i);
						}
						std::mt19937 rng(random::u32());
						std::shuffle(std::begin(sceneCvModeShuffle), std::end(sceneCvModeShuffle), rng);
						int s = std::min(std::max(0, sceneCvModeShuffle.back()), sceneCount - 1);
						sceneCvModeShuffle.pop_back();
						sceneSet(s);
						break;
					}
					case SCENE_CV_MODE::TRIG_RANDOM:
					case SCENE_CV_MODE::TRIG_RANDOM_WALK:
					case SCENE_CV_MODE::TRIG_RANDOM_WO_REPEAT:
					default: {
						break;
					}
				}
			}
			else {
				resetTimer.process(args.sampleTime);
			}
		}

		// Scene CV input
		if (inputs[INPUT_SCENE].isConnected()) {
			switch (sceneMode) {
				case SCENE_CV_MODE::OFF: {
					break;
				}
				case SCENE_CV_MODE::TRIG_FWD: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						if (!inputs[INPUT_RESET].isConnected() || resetTimer.getTime() >= 1e-3f) {
							int s = (sceneSelected + 1) % sceneCount;
							sceneSet(s);
						}
					}
					break;
				}
				case SCENE_CV_MODE::TRIG_REV: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						if (!inputs[INPUT_RESET].isConnected() || resetTimer.getTime() >= 1e-3f) {
							int s = (sceneSelected - 1 + sceneCount) % sceneCount;
							sceneSet(s);
						}
					}
					break;
				}
				case SCENE_CV_MODE::TRIG_PINGPONG: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						if (!inputs[INPUT_RESET].isConnected() || resetTimer.getTime() >= 1e-3f) {
							int s = sceneSelected + sceneCvModeDir;
							if (s >= sceneCount - 1)
								sceneCvModeDir = -1;
							if (s <= 0)
								sceneCvModeDir = 1;
							sceneSet(s);
						}
					}
					break;
				}
				case SCENE_CV_MODE::TRIG_ALT: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						if (!inputs[INPUT_RESET].isConnected() || resetTimer.getTime() >= 1e-3f) {
							int s = 0;
							if (sceneSelected == 0) {
								s = sceneCvModeAlt + sceneCvModeDir;
								if (s >= sceneCount - 1)
									sceneCvModeDir = -1;
								if (s <= 0)
									sceneCvModeDir = 1;
								sceneCvModeAlt = std::max(0, std::min(s, sceneCount - 1));
							}
							sceneSet(s);
						}
					}
					break;
				}
				case SCENE_CV_MODE::TRIG_RANDOM: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						int s = random::u32() % sceneCount;
						sceneSet(s);
					}
					break;
				}
				case SCENE_CV_MODE::TRIG_RANDOM_WO_REPEAT: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						int s = random::u32() % sceneCount;
						if (s == sceneSelected) s = (s + 1) % sceneCount;
						sceneSet(s);
					}
					break;
				}
				case SCENE_CV_MODE::TRIG_RANDOM_WALK: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						int s = std::min(std::max(0, sceneSelected + (random::u32() % 2 == 0 ? -1 : 1)), sceneCount - 1);
						sceneSet(s);
					}
					break;
				}
				case SCENE_CV_MODE::TRIG_SHUFFLE: {
					if (sceneTrigger.process(inputs[INPUT_SCENE].getVoltage())) {
						if (sceneCvModeShuffle.size() == 0) {
							for (int i = 0; i < sceneCount; i++) {
								sceneCvModeShuffle.push_back(i);
							}
							std::mt19937 rng(random::u32());
							std::shuffle(std::begin(sceneCvModeShuffle), std::end(sceneCvModeShuffle), rng);
						}
						int s = std::min(std::max(0, sceneCvModeShuffle.back()), sceneCount - 1);
						sceneCvModeShuffle.pop_back();
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
						if (!inputs[INPUT_RESET].isConnected() || resetTimer.getTime() >= 1e-3f) {
							sceneSet(sceneNext);
						}
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
				bool fadeIn = ts - fadeInTs[i] > sceneDivider.getDivision() * 2;
				bool fadeOut = ts - fadeOutTs[i] > sceneDivider.getDivision() * 2;
				scenes[sceneSelected].output[i] = params[PARAM_OUTPUT + i].getValue() == 0.f ? OM_OUT : OM_OFF;
				scenes[sceneSelected].outputAt[i] = params[PARAM_AT + i].getValue();
				for (int j = 0; j < PORTS; j++) {
					float p = params[PARAM_MATRIX + j * PORTS + i].getValue();
					for (int c = 0; c < channelCount; c++) {
						if (fadeIn) fader[i][j][c].setRise(f1);
						if (fadeOut) fader[i][j][c].setFall(f2);
						if (p != scenes[sceneSelected].matrix[i][j] && p == 1.f) fader[i][j][c].triggerFadeIn();
						if (p != scenes[sceneSelected].matrix[i][j] && p == 0.f) fader[i][j][c].triggerFadeOut();
					}
					scenes[sceneSelected].matrix[i][j] = p;
					IN_MODE mode = sceneInputMode ? scenes[sceneSelected].input[i] : inputMode[i];
					if (mode != IN_MODE::IM_FADE) currentMatrix[i][j] = p;
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
		rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
		rightExpander.messageFlipRequested = true;
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

	int expGetChannelCount() override { 
		return channelCount;
	}

	void expSetFade(int i, float* fadeIn, float* fadeOut) override {
		if (fadeIn) {
			fadeInTs[i] = ts;
			for (int j = 0; j < PORTS; j++) {
				for (int c = 0; c < channelCount; c++) {
					fader[i][j][c].setRise(fadeIn[j]);
				}
			}
		}
		if (fadeOut) {
			fadeOutTs[i] = ts;
			for (int j = 0; j < PORTS; j++) {
				for (int c = 0; c < channelCount; c++) {
					fader[i][j][c].setFall(fadeOut[j]);
				}
			}
		}
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
		json_object_set_new(rootJ, "sceneLock", json_boolean(sceneLock));
		json_object_set_new(rootJ, "fadeLengthMode", json_integer(fadeLengthMode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* padBrightnessJ = json_object_get(rootJ, "padBrightness");
		if (padBrightnessJ) padBrightness = json_real_value(padBrightnessJ);
		json_t* inputVisualizeJ = json_object_get(rootJ, "inputVisualize");
		if (inputVisualizeJ) inputVisualize = json_boolean_value(inputVisualizeJ);
		json_t* outputClampJ = json_object_get(rootJ, "outputClamp");
		if (outputClampJ) outputClamp = json_boolean_value(outputClampJ);
		json_t* channelCountJ = json_object_get(rootJ, "channelCount");
		if (channelCountJ) channelCount = json_integer_value(channelCountJ);

		json_t* inputsJ = json_object_get(rootJ, "inputMode");
		if (inputsJ) {
			// Bounded to the fixed-size destinations: hand-edited or corrupted
			// patches may contain more entries than these members hold.
			size_t maxInputs = std::min((size_t)PORTS, json_array_size(inputsJ));
			for (size_t inputIndex = 0; inputIndex < maxInputs; inputIndex++) {
				inputMode[inputIndex] = (IN_MODE)json_integer_value(json_array_get(inputsJ, inputIndex));
			}
		}

		json_t* scenesJ = json_object_get(rootJ, "scenes");
		if (scenesJ) {
			size_t maxScenes = std::min((size_t)SCENE_MAX, json_array_size(scenesJ));
			for (size_t sceneIndex = 0; sceneIndex < maxScenes; sceneIndex++) {
				json_t* sceneJ = json_array_get(scenesJ, sceneIndex);
				json_t* inputJ = json_object_get(sceneJ, "input");
				json_t* outputJ = json_object_get(sceneJ, "output");
				json_t* outputAtJ = json_object_get(sceneJ, "outputAt");
				json_t* matrixJ = json_object_get(sceneJ, "matrix");
				if (inputJ) {
					size_t maxIn = std::min((size_t)PORTS, json_array_size(inputJ));
					for (size_t index = 0; index < maxIn; index++) {
						scenes[sceneIndex].input[index] = (IN_MODE)json_integer_value(json_array_get(inputJ, index));
					}
				}
				if (outputJ) {
					size_t maxOut = std::min((size_t)PORTS, json_array_size(outputJ));
					for (size_t index = 0; index < maxOut; index++) {
						scenes[sceneIndex].output[index] = (OUT_MODE)json_integer_value(json_array_get(outputJ, index));
					}
				}
				if (outputAtJ) {
					size_t maxAt = std::min((size_t)PORTS, json_array_size(outputAtJ));
					for (size_t index = 0; index < maxAt; index++) {
						scenes[sceneIndex].outputAt[index] = json_real_value(json_array_get(outputAtJ, index));
					}
				}
				if (matrixJ) {
					// matrix is [PORTS][PORTS]; a longer array is truncated row-wise
					size_t maxMatrix = std::min((size_t)(PORTS * PORTS), json_array_size(matrixJ));
					for (size_t index = 0; index < maxMatrix; index++) {
						scenes[sceneIndex].matrix[index / PORTS][index % PORTS] = json_real_value(json_array_get(matrixJ, index));
					}
				}
			}
		}

		json_t* sceneSelectedJ = json_object_get(rootJ, "sceneSelected");
		if (sceneSelectedJ) sceneSelected = json_integer_value(sceneSelectedJ);
		json_t* sceneModeJ = json_object_get(rootJ, "sceneMode");
		if (sceneModeJ) sceneMode = (SCENE_CV_MODE)json_integer_value(sceneModeJ);
		json_t* sceneInputModeJ = json_object_get(rootJ, "sceneInputMode");
		if (sceneInputModeJ) sceneInputMode = json_boolean_value(sceneInputModeJ);
		json_t* sceneAtModeJ = json_object_get(rootJ, "sceneAtMode");
		if (sceneAtModeJ) sceneAtMode = json_boolean_value(sceneAtModeJ);
		json_t* sceneCountJ = json_object_get(rootJ, "sceneCount");
		if (sceneCountJ) sceneCount = json_integer_value(sceneCountJ);
		json_t* sceneLockJ = json_object_get(rootJ, "sceneLock");
		if (sceneLockJ) sceneLock = json_boolean_value(sceneLockJ);

		json_t* fadeLengthModeJ = json_object_get(rootJ, "fadeLengthMode");
		if (fadeLengthModeJ) fadeLengthMode = (FADE_LENGTH)json_integer_value(fadeLengthModeJ);

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

		menu->addChild(createMenuLabel("Input mode"));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Off", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_OFF));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Direct", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_DIRECT));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Linear fade", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_FADE));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Constant voltage"));
		menu->addChild(createSubmenuItem("Subtract", "",
			[this](Menu* menu) {
				for (int i = 12; i > 0; i--) {
					menu->addChild(construct<InputItem>(&MenuItem::text, string::f("-%02i cent", i), &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, (IN_MODE)(24 - i)));
				}
			}
		));
		menu->addChild(createSubmenuItem("Add", "",
			[this](Menu* menu) {
				for (int i = 1; i <= 12; i++) {
					menu->addChild(construct<InputItem>(&MenuItem::text, string::f("+%02i cent", i), &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, (IN_MODE)(24 + i)));
				}
			}
		));
	}
};



/*
struct IntermixKnob : app::SvgKnob {
	IntermixKnob() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/IntermixKnob.svg")));
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

		struct IntermixMatrixButton : MatrixButton {
			void onDragStart(const event::DragStart& e) override {
				IntermixModule<PORTS>* module = dynamic_cast<IntermixModule<PORTS>*>(getParamQuantity()->module);
				if (module->sceneLock) {
					e.consume(this);
				}
				else {
					MatrixButton::onDragStart(e);
				}
			}
		};

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (PORTS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addParam(createParamCentered<IntermixMatrixButton>(v, module, IntermixModule<PORTS>::PARAM_MATRIX + i * PORTS + j));
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
		addInput(createInputCentered<StoermelderPort>(Vec(381.9f, 326.7f), module, IntermixModule<PORTS>::INPUT_RESET));

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
		ThemedModuleWidget<IntermixModule<PORTS>>::appendContextMenu(menu);
		IntermixModule<PORTS>* module = dynamic_cast<IntermixModule<PORTS>*>(this->module);

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
		menu->addChild(createBoolPtrMenuItem("Scene lock", "", &module->sceneLock));
		menu->addChild(createSubmenuItem("Channels", string::f("%i", module->channelCount),
			[=](Menu* menu) {
				for (int i = 1; i <= PORT_MAX_CHANNELS; i++) {
					menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(string::f("%i", i), &module->channelCount, i));
				}
			}
		));
		menu->addChild(new MenuSeparator());
		menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem("Port SCENE-mode",
			{
				{ SCENE_CV_MODE::OFF, "Off" },
				{ SCENE_CV_MODE::TRIG_FWD, "Trigger" },
				{ SCENE_CV_MODE::TRIG_REV, "Trigger reverse" },
				{ SCENE_CV_MODE::TRIG_PINGPONG, "Ping-pong" },
				{ SCENE_CV_MODE::TRIG_ALT, "Alternate" },
				{ SCENE_CV_MODE::TRIG_RANDOM, "Random" },
				{ SCENE_CV_MODE::TRIG_RANDOM_WO_REPEAT, "Random no repeat" },
				{ SCENE_CV_MODE::TRIG_RANDOM_WALK, "Random walk" },
				{ SCENE_CV_MODE::TRIG_SHUFFLE, "Shuffle" },
				{ SCENE_CV_MODE::VOLT, "0..10V" },
				{ SCENE_CV_MODE::C4, "C4-G4" },
				{ SCENE_CV_MODE::ARM, "Arm" }
			},
			&module->sceneMode
		));
		menu->addChild(createBoolPtrMenuItem("Include input-mode in scenes", "", &module->sceneInputMode));
		menu->addChild(createBoolPtrMenuItem("Include attenuverters in scenes", "", &module->sceneAtMode));
		menu->addChild(createBoolPtrMenuItem("Limit output to -10..10V", "", &module->outputClamp));
		menu->addChild(new MenuSeparator());
		menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<FADE_LENGTH>("Fade length",
			{
				{ FADE_LENGTH::FADE_LENGTH_4S, "4s" },
				{ FADE_LENGTH::FADE_LENGTH_15S, "15s" },
				{ FADE_LENGTH::FADE_LENGTH_60S, "60s" }
			},
			[=]() { return module->fadeLengthMode; },
			[=](FADE_LENGTH m) { module->fadeLengthMode = m; }
		));
		menu->addChild(new MenuSeparator());
		menu->addChild(new BrightnessSlider(module));
		menu->addChild(createBoolPtrMenuItem("Visualize input on pads", "", &module->inputVisualize));
	}
};

} // namespace Intermix
} // namespace StoermelderPackOne

Model* modelIntermix = createModel<StoermelderPackOne::Intermix::IntermixModule<8>, StoermelderPackOne::Intermix::IntermixWidget>("Intermix");