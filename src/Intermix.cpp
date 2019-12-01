#include "plugin.hpp"
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
	IM_IN = 1,
	IM_ADD_1V = 2,
	IM_SUB_1V = 3
};

enum OUT_MODE {
	OM_OFF = 0,
	OM_OUT = 1
};

enum PAD_MODE {
	PM_OFF = 0,
	PM_ON = 1
};

template < int PORTS >
struct IntermixModule : Module {
	enum ParamIds {
		ENUMS(MATRIX_PARAM, PORTS * PORTS),
		ENUMS(OUTPUT_PARAM, PORTS),
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
		ENUMS(MATRIX_LIGHT, PORTS * PORTS),
		ENUMS(OUTPUT_LIGHT, PORTS),
		NUM_LIGHTS
	};

	struct SceneData {
		IN_MODE input[PORTS];
		OUT_MODE output[PORTS];
		PAD_MODE matrix[PORTS][PORTS];
	};

	/** [Stored to JSON] */
	SceneData scenes[SCENE_COUNT];
	/** [Stored to JSON] */
	int sceneSelected = 0;
	/** [Stored to JSON] */
	SCENE_CV_MODE sceneMode;

	dsp::SchmittTrigger sceneTrigger;
	dsp::ClockDivider sceneDivider;
	dsp::ClockDivider lightDivider;

	IntermixModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				configParam(MATRIX_PARAM + i * PORTS + j, 0.f, 1.f, 0.f, string::f("Input %i to Output %i", j + 1, i + 1));
			}
			configParam(OUTPUT_PARAM + i, 0.f, 1.f, 1.f, string::f("Output %i", i + 1));
		}
		sceneDivider.setDivision(32);
		lightDivider.setDivision(512);
		onReset();
	}

	void onReset() override {
		for (int i = 0; i < SCENE_COUNT; i++) {
			for (int j = 0; j < PORTS; j++) {
				scenes[i].input[j] = IM_IN;
				scenes[i].output[j] = OM_OUT;
				for (int k = 0; k < PORTS; k++) {
					scenes[i].matrix[j][k] = PM_OFF;
				}
			}
		}
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
					int s = std::floor(rescale(inputs[SCENE_INPUT].getVoltage(), 0.f, 10.f, 0, SCENE_COUNT - 1));
					sceneSet(s);
					break;
				}
			}
		}

		if (sceneDivider.process()) {
			for (int i = 0; i < PORTS; i++) {
				scenes[sceneSelected].output[i] = params[OUTPUT_PARAM + i].getValue() != 0.f ? OM_OUT : OM_OFF;
				for (int j = 0; j < PORTS; j++) {
					scenes[sceneSelected].matrix[i][j] = params[MATRIX_PARAM + j * PORTS + i].getValue() != 0.f ? PM_ON : PM_OFF;
				}
			}
		}

		simd::float_4 out[PORTS / 4];
		for (int i = 0; i < PORTS; i++) {
			float v;
			switch (scenes[sceneSelected].input[i]) {
				case IN_MODE::IM_OFF:
					v = 0.f;
					break;
				case IN_MODE::IM_IN:
					v = inputs[INPUT + i].getVoltage();
					break;
				case IN_MODE::IM_ADD_1V:
					v = 1.f;
					break;
				case IN_MODE::IM_SUB_1V:
					v = -1.f;
					break;
			}

			simd::float_4 mask[PORTS / 4];
			for (int j = 0; j < PORTS / 4; j+=4) {
				mask[j / 4] = simd::float_4::mask();
			}

			for (int j = 0; j < PORTS; j++) {
				if (!scenes[sceneSelected].matrix[i][j]) mask[j / 4][j % 4] = 0.f;
			}

			for (int j = 0; j < PORTS / 4; j+=4) {
				simd::float_4 t = simd::ifelse(mask[j], simd::float_4(v), simd::float_4::zero());
				out[j] += t;
			}
		}

		for (int i = 0; i < PORTS; i++) {
			float v = params[OUTPUT_PARAM + i].getValue() > 0.f ? out[i / 4][i % 4] : 0.f;
			outputs[OUTPUT + i].setVoltage(v);
		}

		if (lightDivider.process()) {
			float s = lightDivider.division * args.sampleTime;
			for (int i = 0; i < PORTS; i++) {
				for (int j = 0; j < PORTS; j++) {
					lights[MATRIX_LIGHT + i * PORTS + j].setSmoothBrightness(params[MATRIX_PARAM + i * PORTS + j].getValue(), s);
				}
				lights[OUTPUT_LIGHT + i].setSmoothBrightness(params[OUTPUT_PARAM + i].getValue(), s);
			}
		}
	}

	inline void sceneSet(int scene) {
		if (sceneSelected == scene) return;
		sceneSelected = scene;
		for (int i = 0; i < PORTS; i++) {
			params[OUTPUT_PARAM + i].setValue(scenes[sceneSelected].output[i]);
			for (int j = 0; j < PORTS; j++) {
				params[MATRIX_PARAM + j * PORTS + i].setValue(scenes[sceneSelected].matrix[i][j]);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_t* scenesJ = json_array();
		for (int i = 0; i < SCENE_COUNT; i++) {
			json_t* inputJ = json_array();
			json_t* outputJ = json_array();
			json_t* sceneJ = json_object();
			json_t* matrixJ = json_array();
			for (int j = 0; j < PORTS; j++) {
				json_array_append_new(inputJ, json_integer(scenes[i].input[j]));
				json_array_append_new(outputJ, json_integer(scenes[i].output[j]));
				for (int k = 0; k < PORTS; k++) {
					json_array_append_new(matrixJ, json_integer(scenes[i].matrix[j][k]));
				}
			}
			json_object_set_new(sceneJ, "input", inputJ);
			json_object_set_new(sceneJ, "output", outputJ);
			json_object_set_new(sceneJ, "matrix", matrixJ);
			json_array_append_new(scenesJ, sceneJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);

		json_object_set_new(rootJ, "sceneSelected", json_integer(sceneSelected));
		json_object_set_new(rootJ, "sceneMode", json_integer(sceneMode));

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* scenesJ = json_object_get(rootJ, "scenes");
		json_t* sceneJ;
		size_t sceneIndex;
		json_array_foreach(scenesJ, sceneIndex, sceneJ) {
			json_t* inputJ = json_object_get(sceneJ, "input");
			json_t* outputJ = json_object_get(sceneJ, "output");
			json_t* matrixJ = json_object_get(sceneJ, "matrix");

			json_t* valueJ;
			size_t inputIndex;
			json_array_foreach(inputJ, inputIndex, valueJ) {
				scenes[sceneIndex].input[inputIndex] = (IN_MODE)json_integer_value(valueJ);
			}
			size_t outputIndex;
			json_array_foreach(outputJ, outputIndex, valueJ) {
				scenes[sceneIndex].output[outputIndex] = (OUT_MODE)json_integer_value(valueJ);
			}
			size_t matrixIndex;
			json_array_foreach(matrixJ, matrixIndex, valueJ) {
				scenes[sceneIndex].matrix[matrixIndex / PORTS][matrixIndex % PORTS] = (PAD_MODE)json_integer_value(valueJ);
			}
		}

		sceneSelected = json_integer_value(json_object_get(rootJ, "sceneSelected"));
		sceneMode = (SCENE_CV_MODE)json_integer_value(json_object_get(rootJ, "sceneMode"));
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
			switch (module->scenes[module->sceneSelected].input[id]) {
				case IN_MODE::IM_OFF:
					text = "OFF"; break;
				case IN_MODE::IM_IN:
					text = " - "; break;
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
				module->scenes[module->sceneSelected].input[id] = inMode;
			}

			void step() override {
				rightText = module->scenes[module->sceneSelected].input[id] == inMode ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Input mode"));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Off", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_OFF));
		menu->addChild(construct<InputItem>(&MenuItem::text, "Default", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_IN));
		menu->addChild(construct<InputItem>(&MenuItem::text, "+1V", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_ADD_1V));
		menu->addChild(construct<InputItem>(&MenuItem::text, "-1V", &InputItem::module, module, &InputItem::id, id, &InputItem::inMode, IM_SUB_1V));
	}
};


template < typename BASE >
struct IntermixButtonLight : BASE {
	IntermixButtonLight() {
		this->box.size = math::Vec(26.f, 26.f);
	}

	void drawLight(const Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, this->box.size.x, this->box.size.y, 3.4f);

		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFillColor(args.vg, color::mult(this->color, 0.6f));
		nvgFill(args.vg);
	}
};

struct IntermixButton : app::SvgSwitch {
	IntermixButton() {
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Components/IntermixButton.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Components/IntermixButton1.svg")));
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

		float xMin = 60.1f;
		float xMax = 269.9f;
		float yMin = 53.0f;
		float yMax = 264.3f;
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				Vec v = Vec(xMin + (xMax - xMin) / (PORTS - 1) * j, yMin + (yMax - yMin) / (PORTS - 1) * i);
				addParam(createParamCentered<IntermixButton>(v, module, IntermixModule<PORTS>::MATRIX_PARAM + i * PORTS + j));
				addChild(createLightCentered<IntermixButtonLight<GreenLight>>(v, module, IntermixModule<PORTS>::MATRIX_LIGHT + i * PORTS + j));
			}
		}

		for (int i = 0; i < PORTS; i++) {
			Vec v = Vec(21.9f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addParam(createParamCentered<IntermixButton>(v, module, IntermixModule<PORTS>::OUTPUT_PARAM + i));
			addChild(createLightCentered<IntermixButtonLight<RedLight>>(v, module, IntermixModule<PORTS>::OUTPUT_LIGHT + i));

			Vec vo = Vec(308.1f, yMin + (yMax - yMin) / (PORTS - 1) * i);
			addOutput(createOutputCentered<StoermelderPort>(vo, module, IntermixModule<PORTS>::OUTPUT + i));

			Vec vi0 = Vec(xMin + (xMax - xMin) / (PORTS - 1) * i, 299.5f);
			InputLedDisplay<IntermixModule<PORTS>>* inputLedDisplay = createWidgetCentered<InputLedDisplay<IntermixModule<PORTS>>>(vi0);
			inputLedDisplay->module = module;
			inputLedDisplay->id = i;
			addChild(inputLedDisplay);
			Vec vi1 = Vec(xMin + (xMax - xMin) / (PORTS - 1) * i, 323.7f);
			addInput(createInputCentered<StoermelderPort>(vi1, module, IntermixModule<PORTS>::INPUT + i));
		}

		SceneLedDisplay<IntermixModule<PORTS>>* sceneLedDisplay = createWidgetCentered<SceneLedDisplay<IntermixModule<PORTS>>>(Vec(308.1f, 299.5f));
		sceneLedDisplay->module = module;
		addChild(sceneLedDisplay);
		addInput(createInputCentered<StoermelderPort>(Vec(308.1f, 323.7f), module, IntermixModule<PORTS>::SCENE_INPUT));
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

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SceneModeMenuItem>(&MenuItem::text, "SCENE-port", &SceneModeMenuItem::module, module));
	}
};

} // namespace Intermix

Model* modelIntermix = createModel<Intermix::IntermixModule<8>, Intermix::IntermixWidget>("Intermix");