#include "plugin.hpp"
#include <thread>

namespace Detour {

const int SCENE_MAX = 8;
const int MAX_DELAY = 32;

enum MODE {
	MONO,
	POLY
};

enum SCENE_CV_MODE {
	OFF = -1,
	TRIG_FWD = 0,
	VOLT = 8,
	C4 = 9,
	ARM = 7
};

template < int PORTS, int SENDS = 8 >
struct DetourModule : Module {
	enum ParamIds {
		ENUMS(PARAM_MATRIX, PORTS * SENDS),
		ENUMS(PARAM_SCENE, SCENE_MAX),
		ENUMS(PARAM_X_MAP, SENDS),
		ENUMS(PARAM_Y_MAP, PORTS),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, PORTS),
		ENUMS(INPUT_RETURN, SENDS),
		INPUT_SCENE,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, PORTS),
		ENUMS(OUTPUT_SEND, SENDS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_MATRIX, PORTS * SENDS),
		ENUMS(LIGHT_SCENE, PORTS),
		NUM_LIGHTS
	};

	struct SceneData {
		alignas(16) float matrix[PORTS][PORTS];
	};

	const int countSend = SENDS;
	const int countPort = PORTS;

	alignas(16) float currentMatrix[PORTS][PORTS];
	alignas(16) float history[PORTS][SENDS + 1][MAX_DELAY + 1];
	int sceneNext = -1;

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	MODE channelMode[SENDS];
	/** [Stored to JSON] */
	int channelDelay[SENDS];
	/** [Stored to JSON] */
	float padBrightness;
	/** [Stored to JSON] */
	SceneData scenes[SCENE_MAX];
	/** [Stored to JSON] */
	int sceneSelected = 0;
	/** [Stored to JSON] */
	SCENE_CV_MODE sceneMode;
	/** [Stored to JSON] */
	int sceneCount;

	int currentFrame;

	dsp::SchmittTrigger sceneTrigger;
	dsp::SchmittTrigger mapTrigger[PORTS];
	dsp::ClockDivider sceneDivider;
	dsp::ClockDivider lightDivider;

	DetourModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < SCENE_MAX; i++) {
			configParam(PARAM_SCENE + i, 0.f, 1.f, 0.f, string::f("Scene %i", i + 1));
		}
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < SENDS; j++) {
				configParam<MatrixButtonParamQuantity>(PARAM_MATRIX + i * PORTS + j, 0.f, 1.f, 0.f, string::f("Input %i to Send/Return %i", i + 1, j + 1));
			}
			configParam(PARAM_Y_MAP + i, 0.f, 1.f, 0.f, string::f("Matrix row %i", i + 1));
		}
		for (int j = 0; j < SENDS; j++) {
			configParam(PARAM_X_MAP + j, 0.f, 1.f, 0.f, string::f("Matrix col %i", j + 1));
		}
		currentFrame = 0;
		sceneDivider.setDivision(32);
		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		padBrightness = 0.75f;
		for (int i = 0; i < SCENE_MAX; i++) {
			for (int j = 0; j < PORTS; j++) {
				for (int k = 0; k < SENDS; k++) {
					scenes[i].matrix[j][k] = 0.f;
				}
			}
		}
		for (int i = 0; i < SENDS; i++) {
			channelMode[i] = MODE::MONO;
			channelDelay[i] = 2;
		}
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < SENDS + 1; j++) {
				for (int k = 0; k < MAX_DELAY; k++) {
					history[i][j][k] = 0.f;
				}
			}
		}
		sceneMode = SCENE_CV_MODE::TRIG_FWD;
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

			for (int i = 0; i < SENDS; i++) {
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

			int maxInput = 0;
			for (int i = 0; i < PORTS; i++) {
				if (inputs[INPUT + i].isConnected())
					maxInput = i + 1;
			}

			for (int i = 0; i < SENDS; i++) {
				int j1 = -1;
				for (int j = 0; j < PORTS; j++) {
					float p = params[PARAM_MATRIX + j * PORTS + i].getValue();
					if (p == 1.f && scenes[sceneSelected].matrix[j][i] != 1.f) j1 = j;
					scenes[sceneSelected].matrix[j][i] = currentMatrix[j][i] = p;
				}
				// Only allow one active channel if in MONO-channelMode
				if (channelMode[i] == MODE::MONO) {
					if (j1 >= 0) {
						for (int j = 0; j < PORTS; j++) {
							if (j == j1) continue;
							scenes[sceneSelected].matrix[j][i] = 0.f;
							params[PARAM_MATRIX + j * PORTS + i].setValue(0.f);
						}
					}
					outputs[OUTPUT_SEND + i].setChannels(1);
				}
				else {
					outputs[OUTPUT_SEND + i].setChannels(maxInput);
				}
			}
		}

		// DSP processing
		for (int i = 0; i < PORTS; i++) {
			if (!inputs[INPUT + i].isConnected()) continue;
			float out = history[i][0][currentFrame] = inputs[INPUT + i].getVoltage();

			for (int j = 0; j < SENDS; j++) {
				if (inputs[INPUT_RETURN + j].isConnected()) {
					if (currentMatrix[i][j] == 1.f) {
						if (channelMode[j] == MODE::MONO) {
							outputs[OUTPUT_SEND + j].setVoltage(out);
							out = inputs[INPUT_RETURN + j].getVoltage();
						}
						else {
							outputs[OUTPUT_SEND + j].setVoltage(out, i);
							out = inputs[INPUT_RETURN + j].getVoltage(i);
						}
					}
					else {
						int f = (currentFrame - channelDelay[j] + MAX_DELAY) % MAX_DELAY;
						out = history[i][j][f];
					}
				}
				history[i][j + 1][currentFrame] = out;
			}
			outputs[OUTPUT + i].setVoltage(out);
		}
		currentFrame = (currentFrame + 1) % MAX_DELAY;

		// Lights
		if (lightDivider.process()) {
			float s = lightDivider.getDivision() * args.sampleTime;

			for (int i = 0; i < SCENE_MAX; i++) {
				float v = (i == sceneSelected) * padBrightness;
				v = std::max(i < sceneCount ? 0.05f : 0.f, v);
				lights[LIGHT_SCENE + i].setSmoothBrightness(v, s);
			}
			for (int i = 0; i < PORTS; i++) {
				for (int j = 0; j < SENDS; j++) {
					float v = currentMatrix[i][j] * padBrightness;
					lights[LIGHT_MATRIX + i * PORTS + j].setSmoothBrightness(v, s);
				}
			}
		}
	}

	void sceneSet(int scene) {
		if (sceneSelected == scene) return;
		if (scene < 0) return;
		sceneSelected = std::min(scene, sceneCount - 1);
		sceneNext = -1;

		for (int i = 0; i < SCENE_MAX; i++) {
			params[PARAM_SCENE + i].setValue(i == sceneSelected);
			for (int j = 0; j < SENDS; j++) {
				float p = scenes[sceneSelected].matrix[i][j];
				params[PARAM_MATRIX + i * PORTS + j].setValue(p);
				currentMatrix[i][j] = p;
			}
		}
	}

	void sceneCopy(int scene) {
		if (sceneSelected == scene) return;
		for (int i = 0; i < SCENE_MAX; i++) {
			for (int j = 0; j < SENDS; j++) {
				scenes[scene].matrix[i][j] = scenes[sceneSelected].matrix[i][j];
			}
		}
	}

	void sceneReset() {
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				scenes[sceneSelected].matrix[i][j] = 0.f;
				params[PARAM_MATRIX + j * PORTS + i].setValue(0.f);
				currentMatrix[i][j] = 0.f;
			}
		}
	}

	void sceneSetCount(int count) {
		sceneCount = count;
		sceneSelected = std::min(sceneSelected, sceneCount - 1);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "padBrightness", json_real(padBrightness));

		json_t* channelsJ = json_array();
		for (int i = 0; i < PORTS; i++) {
			json_t* channelJ = json_object();
			json_object_set_new(channelJ, "mode", json_integer(channelMode[i]));
			json_object_set_new(channelJ, "delay", json_integer(channelDelay[i]));
			json_array_append_new(channelsJ, channelJ);
		}
		json_object_set_new(rootJ, "channel", channelsJ);

		json_t* scenesJ = json_array();
		for (int i = 0; i < SCENE_MAX; i++) {
			json_t* matrixJ = json_array();
			for (int j = 0; j < PORTS; j++) {
				for (int k = 0; k < SENDS; k++) {
					json_array_append_new(matrixJ, json_real(scenes[i].matrix[j][k]));
				}
			}

			json_t* sceneJ = json_object();
			json_object_set_new(sceneJ, "matrix", matrixJ);
			json_array_append_new(scenesJ, sceneJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);

		json_object_set_new(rootJ, "sceneSelected", json_integer(sceneSelected));
		json_object_set_new(rootJ, "sceneMode", json_integer(sceneMode));
		json_object_set_new(rootJ, "sceneCount", json_integer(sceneCount));

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		
		padBrightness = json_real_value(json_object_get(rootJ, "padBrightness"));

		json_t* channelsJ = json_object_get(rootJ, "channel");
		json_t* channelJ;
		size_t channelIndex;
		json_array_foreach(channelsJ, channelIndex, channelJ) {
			channelMode[channelIndex] = (MODE)json_integer_value(json_object_get(channelJ, "mode"));
			channelDelay[channelIndex] = json_integer_value(json_object_get(channelJ, "delay"));
		}

		json_t* scenesJ = json_object_get(rootJ, "scenes");
		json_t* sceneJ;
		size_t sceneIndex;
		json_array_foreach(scenesJ, sceneIndex, sceneJ) {
			json_t* matrixJ = json_object_get(sceneJ, "matrix");
			json_t* valueJ;
			size_t index;
			json_array_foreach(matrixJ, index, valueJ) {
				scenes[sceneIndex].matrix[index / PORTS][index % PORTS] = json_real_value(valueJ);
			}
		}

		sceneSelected = json_integer_value(json_object_get(rootJ, "sceneSelected"));
		sceneMode = (SCENE_CV_MODE)json_integer_value(json_object_get(rootJ, "sceneMode"));
		json_t* sceneCountJ = json_object_get(rootJ, "sceneCount");
		if (sceneCountJ) sceneCount = json_integer_value(sceneCountJ);

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < SENDS; j++) {
				float v = scenes[sceneSelected].matrix[i][j];
				currentMatrix[i][j] = v;
			}
		}
	}
};


struct DetourWidget : ThemedModuleWidget<DetourModule<8, 8>> {
	const static int PORTS = 8;
	const static int SENDS = 8;
	typedef DetourModule<PORTS, SENDS> MODULE;

	DetourWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Detour") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float xMin = 93.3f;
		float xMax = 303.1f;
		float yMin = 53.0f;
		float yMax = 264.3f;

		// Parameters and ports
		for (int i = 0; i < SCENE_MAX; i++) {
			Vec v = Vec(23.1f, yMin + (yMax - yMin) / (SCENE_MAX - 1) * i);
			addParam(createParamCentered<MatrixButton>(v, module, MODULE::PARAM_SCENE + i));
		}

		SceneLedDisplay<MODULE, SCENE_MAX>* sceneLedDisplay = createWidgetCentered<SceneLedDisplay<MODULE, SCENE_MAX>>(Vec(23.1f, 304.9f));
		sceneLedDisplay->module = module;
		addChild(sceneLedDisplay);
		addInput(createInputCentered<StoermelderPort>(Vec(23.1f, 327.9f), module, MODULE::INPUT_SCENE));

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < SENDS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (SENDS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addParam(createParamCentered<MatrixButton>(v, module, MODULE::PARAM_MATRIX + i * PORTS + j));
			}
		}

		struct DummyMapButton : ParamWidget {
			DummyMapButton() {
				this->box.size = Vec(5.f, 5.f);
			}
		};

		for (int i = 0; i < PORTS; i++) {
			Vec v;
			v = Vec(60.1f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addInput(createInputCentered<StoermelderPort>(v, module, MODULE::INPUT + i));
			v = Vec(336.2f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addOutput(createOutputCentered<StoermelderPort>(v, module, MODULE::OUTPUT + i));
			v = Vec(320.5f, yMin + (yMax - yMin) / (PORTS - 1) * i - 11.2f);
			addParam(createParamCentered<DummyMapButton>(v, module, MODULE::PARAM_Y_MAP + i));
		}
		for (int i = 0; i < SENDS; i++) {
			Vec v;
			v = Vec(xMin + (xMax - xMin) / (SENDS - 1) * i, 327.9f);
			addInput(createInputCentered<StoermelderPort>(v, module, MODULE::INPUT_RETURN + i));
			v = Vec(xMin + (xMax - xMin) / (SENDS - 1) * i, 297.3f);
			addOutput(createOutputCentered<StoermelderPort>(v, module, MODULE::OUTPUT_SEND + i));
			v = Vec(xMin + (xMax - xMin) / (SENDS - 1) * i - 11.2f, 281.9f);
			addParam(createParamCentered<DummyMapButton>(v, module, MODULE::PARAM_X_MAP + i));
		}

		// Lights
		for (int i = 0; i < SCENE_MAX; i++) {
			Vec v = Vec(23.1f, yMin + (yMax - yMin) / (SCENE_MAX - 1) * i);
			addChild(createLightCentered<MatrixButtonLight<YellowLight, MODULE>>(v, module, MODULE::LIGHT_SCENE + i));
		}

		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < SENDS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (SENDS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addChild(createLightCentered<MatrixButtonLight<WhiteLight, MODULE>>(v, module, MODULE::LIGHT_MATRIX + i * PORTS + j));
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Detour.md");
				t.detach();
			}
		};

		struct ChannelsMenuItem : MenuItem {
			ChannelsMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct ChannelMenuItem : MenuItem {
				MODULE* module;
				int channel;

				ChannelMenuItem() {
					rightText = RIGHT_ARROW;
				}

				Menu* createChildMenu() override {
					Menu* menu = new Menu;

					struct ChannelModeItem : MenuItem {
						MODULE* module;
						int channel;
						
						void onAction(const event::Action& e) override {
							if (module->channelMode[channel] == MODE::POLY) {
								for (int i = 0; i < SCENE_MAX; i++) {
									for (int j = 0; j < module->countPort; j++) {
										module->scenes[i].matrix[j][channel] = 0.f;
									}
								}
								module->channelMode[channel] = MODE::MONO;
							}
							else {
								module->channelMode[channel] = MODE::POLY;
							} 
						}

						void step() override {
							rightText = module->channelMode[channel] == MODE::MONO ? "Monophonic" : "Polyphonic";
							MenuItem::step();
						}
					};

					struct DelaySlider : ui::Slider {
						struct DelayQuantity : Quantity {
							MODULE* module;
							int channel;
							float v = -1.f;

							DelayQuantity(MODULE* module, int channel) {
								this->module = module;
								this->channel = channel;
							}
							void setValue(float value) override {
								v = clamp(value, 2.f, float(MAX_DELAY - 1));
								module->channelDelay[channel] = int(v);
							}
							float getValue() override {
								if (v < 0.f) v = module->channelDelay[channel];
								return v;
							}
							float getDefaultValue() override {
								return 2.f;
							}
							float getMinValue() override {
								return 2.f;
							}
							float getMaxValue() override {
								return MAX_DELAY - 1;
							}
							float getDisplayValue() override {
								return getValue();
							}
							std::string getDisplayValueString() override {
								return string::f("%i", int(getValue()));
							}
							void setDisplayValue(float displayValue) override {
								setValue(displayValue);
							}
							std::string getLabel() override {
								return "Bypass delay";
							}
							std::string getUnit() override {
								return "";
							}
						};

						DelaySlider(MODULE* module, int channel) {
							this->box.size.x = 160.0;
							quantity = new DelayQuantity(module, channel);
						}
						~DelaySlider() {
							delete quantity;
						}
						void onDragMove(const event::DragMove& e) override {
							if (quantity) {
								quantity->moveScaledValue(0.002f * e.mouseDelta.x);
							}
						}
					};

					menu->addChild(construct<ChannelModeItem>(&MenuItem::text, "Mode", &ChannelModeItem::module, module, &ChannelModeItem::channel, channel));
					menu->addChild(new DelaySlider(module, channel));
					return menu;
				}
			};

			MODULE* module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (int i = 0; i < module->countSend; i++) {
					menu->addChild(construct<ChannelMenuItem>(&MenuItem::text, string::f("Send/Return %i", i + 1), &ChannelMenuItem::module, module, &ChannelMenuItem::channel, i));
				}
				return menu;
			}
		};

		struct SceneModeMenuItem : MenuItem {
			SceneModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct SceneModeItem : MenuItem {
				MODULE* module;
				SCENE_CV_MODE sceneMode;
				
				void onAction(const event::Action& e) override {
					module->sceneMode = sceneMode;
				}

				void step() override {
					rightText = module->sceneMode == sceneMode ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
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

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SceneModeMenuItem>(&MenuItem::text, "SCENE-port", &SceneModeMenuItem::module, module));
		menu->addChild(construct<ChannelsMenuItem>(&MenuItem::text, "Send/Return", &ChannelsMenuItem::module, module));
	}
};

} // namespace Detour

Model* modelDetour = createModel<Detour::DetourModule<8>, Detour::DetourWidget>("Detour");