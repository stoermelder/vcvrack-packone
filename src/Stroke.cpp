#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Stroke {

static const char* keyName(int key) {
	switch (key) {
		case GLFW_KEY_A:				return "A";
		case GLFW_KEY_B:				return "B";
		case GLFW_KEY_C:				return "C";
		case GLFW_KEY_D:				return "D";
		case GLFW_KEY_E:				return "E";
		case GLFW_KEY_F:				return "F";
		case GLFW_KEY_G:				return "G";
		case GLFW_KEY_H:				return "H";
		case GLFW_KEY_I:				return "I";
		case GLFW_KEY_J:				return "J";
		case GLFW_KEY_K:				return "K";
		case GLFW_KEY_L:				return "L";
		case GLFW_KEY_M:				return "M";
		case GLFW_KEY_N:				return "N";
		case GLFW_KEY_O:				return "O";
		case GLFW_KEY_P:				return "P";
		case GLFW_KEY_Q:				return "Q";
		case GLFW_KEY_R:				return "R";
		case GLFW_KEY_S:				return "S";
		case GLFW_KEY_T:				return "T";
		case GLFW_KEY_U:				return "U";
		case GLFW_KEY_V:				return "V";
		case GLFW_KEY_W:				return "W";
		case GLFW_KEY_X:				return "X";
		case GLFW_KEY_Y:				return "Y";
		case GLFW_KEY_Z:				return "Z";
		case GLFW_KEY_1:				return "1";
		case GLFW_KEY_2:				return "2";
		case GLFW_KEY_3:				return "3";
		case GLFW_KEY_4:				return "4";
		case GLFW_KEY_5:				return "5";
		case GLFW_KEY_6:				return "6";
		case GLFW_KEY_7:				return "7";
		case GLFW_KEY_8:				return "8";
		case GLFW_KEY_9:				return "9";
		case GLFW_KEY_0:				return "0";
		case GLFW_KEY_SPACE:			return "SPACE";
		case GLFW_KEY_MINUS:			return "-";
		case GLFW_KEY_EQUAL:			return "=";
		case GLFW_KEY_LEFT_BRACKET:		return "(";
		case GLFW_KEY_RIGHT_BRACKET:	return ")";
		case GLFW_KEY_BACKSLASH:		return "\\";
		case GLFW_KEY_SEMICOLON:		return ":";
		case GLFW_KEY_APOSTROPHE:		return "'";
		case GLFW_KEY_GRAVE_ACCENT:		return "^";
		case GLFW_KEY_COMMA:			return ",";
		case GLFW_KEY_PERIOD:			return ".";
		case GLFW_KEY_SLASH:			return "/";
		case GLFW_KEY_WORLD_1:			return "W1";
		case GLFW_KEY_WORLD_2:			return "W2";
		case GLFW_KEY_ESCAPE:			return "ESC";
		case GLFW_KEY_F1:				return "F1";
		case GLFW_KEY_F2:				return "F2";
		case GLFW_KEY_F3:				return "F3";
		case GLFW_KEY_F4:				return "F4";
		case GLFW_KEY_F5:				return "F5";
		case GLFW_KEY_F6:				return "F6";
		case GLFW_KEY_F7:				return "F7";
		case GLFW_KEY_F8:				return "F8";
		case GLFW_KEY_F9:				return "F9";
		case GLFW_KEY_F10:				return "F10";
		case GLFW_KEY_F11:				return "F11";
		case GLFW_KEY_F12:				return "F12";
		case GLFW_KEY_F13:				return "F13";
		case GLFW_KEY_F14:				return "F14";
		case GLFW_KEY_F15:				return "F15";
		case GLFW_KEY_F16:				return "F16";
		case GLFW_KEY_F17:				return "F17";
		case GLFW_KEY_F18:				return "F18";
		case GLFW_KEY_F19:				return "F19";
		case GLFW_KEY_F20:				return "F20";
		case GLFW_KEY_F21:				return "F21";
		case GLFW_KEY_F22:				return "F22";
		case GLFW_KEY_F23:				return "F23";
		case GLFW_KEY_F24:				return "F24";
		case GLFW_KEY_F25:				return "F25";
		case GLFW_KEY_UP:				return "UP";
		case GLFW_KEY_DOWN:				return "DOWN";
		case GLFW_KEY_LEFT:				return "LEFT";
		case GLFW_KEY_RIGHT:			return "RIGHT";
		case GLFW_KEY_TAB:				return "TAB";
		case GLFW_KEY_ENTER:			return "ENTER";
		case GLFW_KEY_BACKSPACE:		return "BS";
		case GLFW_KEY_INSERT:			return "INS";
		case GLFW_KEY_DELETE:			return "DEL";
		case GLFW_KEY_PAGE_UP:			return "PG-UP";
		case GLFW_KEY_PAGE_DOWN:		return "PG DN";
		case GLFW_KEY_HOME:				return "HOME";
		case GLFW_KEY_END:				return "END";
		case GLFW_KEY_KP_0:				return "KP 0";
		case GLFW_KEY_KP_1:				return "KP 1";
		case GLFW_KEY_KP_2:				return "KP 2";
		case GLFW_KEY_KP_3:				return "KP 3";
		case GLFW_KEY_KP_4:				return "KP 4";
		case GLFW_KEY_KP_5:				return "KP 5";
		case GLFW_KEY_KP_6:				return "KP 6";
		case GLFW_KEY_KP_7:				return "KP 7";
		case GLFW_KEY_KP_8:				return "KP 8";
		case GLFW_KEY_KP_9:				return "KP 9";
		case GLFW_KEY_KP_DIVIDE:		return "KP /";
		case GLFW_KEY_KP_MULTIPLY:		return "KP *";
		case GLFW_KEY_KP_SUBTRACT:		return "KP -";
		case GLFW_KEY_KP_ADD:			return "KP +";
		case GLFW_KEY_KP_DECIMAL:		return "KP .";
		case GLFW_KEY_KP_EQUAL:			return "KP =";
		case GLFW_KEY_KP_ENTER:			return "KP E";
		case GLFW_KEY_PRINT_SCREEN:		return "PRINT";
		case GLFW_KEY_PAUSE:			return "PAUSE";
		default:						return NULL;
	}
}

enum class KEY_MODE {
	TRIGGER,
	GATE,
	TOGGLE
};

template < int PORTS >
struct StrokeModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_ALT, PORTS),
		ENUMS(LIGHT_CTRL, PORTS),
		ENUMS(LIGHT_SHIFT, PORTS),
		NUM_LIGHTS
	};

	struct Key {
		int key = -1;
		int mods;
		KEY_MODE mode;
		bool high;
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	Key keys[PORTS];
	/** [Stored to JSON] */
	bool polyphonicOutput = false;

	dsp::PulseGenerator pulse[PORTS];

	StrokeModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		for (int i = 0; i < PORTS; i++) {
			keys[i].key = -1;
			keys[i].mods = 0;
			keys[i].mode = KEY_MODE::TRIGGER;
			keys[i].high = false;
		}
	}

	void process(const ProcessArgs& args) override {
		for (int i = 0; i < PORTS; i++) {
			if (keys[i].key >= 0) {
				switch (keys[i].mode) {
					case KEY_MODE::TRIGGER:
						setOutputVoltage(OUTPUT, i, pulse[i].process(args.sampleTime) * 10.f);
						break;
					case KEY_MODE::GATE:
					case KEY_MODE::TOGGLE:
						setOutputVoltage(OUTPUT, i, keys[i].high * 10.f);
						break;
				}	
			}
		}

		outputs[OUTPUT].setChannels(polyphonicOutput ? PORTS : 1);
	}

	inline void setOutputVoltage(int out, int idx, float v) {
		if (polyphonicOutput) {
			outputs[out].setVoltage(v, idx);
		}
		else {
			outputs[out + idx].setVoltage(v);
		}
	}

	void keyEnable(int idx) {
		switch (keys[idx].mode) {
			case KEY_MODE::TRIGGER:
				pulse[idx].trigger(); break;
			case KEY_MODE::GATE:
				keys[idx].high = true; break;
			case KEY_MODE::TOGGLE:
				keys[idx].high ^= true; break;
		}
	}

	void keyDisable(int idx) {
		switch (keys[idx].mode) {
			case KEY_MODE::TRIGGER:
				break;
			case KEY_MODE::GATE:
				keys[idx].high = false; break;
			case KEY_MODE::TOGGLE:
				break;
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "polyphonicOutput", json_boolean(polyphonicOutput));

		json_t* keysJ = json_array();
		for (int i = 0; i < PORTS; i++) {
			json_t* keyJ = json_object();
			json_object_set_new(keyJ, "key", json_integer(keys[i].key));
			json_object_set_new(keyJ, "mods", json_integer(keys[i].mods));
			json_object_set_new(keyJ, "mode", json_integer((int)keys[i].mode));
			json_object_set_new(keyJ, "high", json_boolean(keys[i].high));
			json_array_append_new(keysJ, keyJ);
		}
		json_object_set_new(rootJ, "keys", keysJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		polyphonicOutput = json_boolean_value(json_object_get(rootJ, "polyphonicOutput"));

		// Hack for preventing duplicating this module
		if (APP->engine->getModule(id) != NULL) return;

		json_t* keysJ = json_object_get(rootJ, "keys");
		for (int i = 0; i < PORTS; i++) {
			json_t* keyJ = json_array_get(keysJ, i);
			keys[i].key = json_integer_value(json_object_get(keyJ, "key"));
			keys[i].mods = json_integer_value(json_object_get(keyJ, "mods"));
			keys[i].mode = (KEY_MODE)json_integer_value(json_object_get(keyJ, "mode"));
			keys[i].high = json_boolean_value(json_object_get(keyJ, "high"));
		}
	}
};


template < int PORTS >
struct KeyContainer : Widget {
	StrokeModule<PORTS>* module;
	int learnIdx = -1;

	void onHoverKey(const event::HoverKey& e) override {
		if (module && e.action == GLFW_PRESS) {
			if (learnIdx >= 0) {
				const char* kn = keyName(e.key);
				if (kn) {
					module->keys[learnIdx].key = e.key;
					module->keys[learnIdx].mods = e.mods;
					learnIdx = -1;
					e.consume(this);
				}
			}
			else {
				for (int i = 0; i < PORTS; i++) {
					if (e.key == module->keys[i].key && e.mods == module->keys[i].mods) {
						module->keyEnable(i);
						e.consume(this);
					}
				}
			}
		}
		if (module && e.action == RACK_HELD) {
			for (int i = 0; i < PORTS; i++) {
				if (e.key == module->keys[i].key && e.mods == module->keys[i].mods) {
					e.consume(this);
				}
			}
		}
		if (module && e.action == GLFW_RELEASE) {
			for (int i = 0; i < PORTS; i++) {
				if (e.key == module->keys[i].key) {
					module->keyDisable(i);
					e.consume(this);
				}
			}
		}
		Widget::onHoverKey(e);
	}

	void enableLearn(int idx) {
		learnIdx = idx;
	}
};

template < int PORTS >
struct KeyDisplay : widget::OpaqueWidget {
	KeyContainer<PORTS>* keyContainer;
	StrokeModule<PORTS>* module;
	int idx;

	std::shared_ptr<Font> font;
	NVGcolor color;
	std::string text;

	KeyDisplay() {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		color = nvgRGB(0xef, 0xef, 0xef);
		box.size = Vec(37.9f, 16.f);
	}

	void draw(const DrawArgs& args) override {
		if (text.length() > 0) {
			nvgFillColor(args.vg, color);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, 0.0);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFontSize(args.vg, 12);
			nvgTextBox(args.vg, 0.f, box.size.y / 2.f + 1.8f, box.size.x, text.c_str(), NULL);
		}
	}

	void step() override {
		if (keyContainer && keyContainer->learnIdx == idx) {
			color.a = 0.6f;
			text = "<LRN>";
			module->lights[StrokeModule<PORTS>::LIGHT_ALT + idx].setBrightness(0.f);
			module->lights[StrokeModule<PORTS>::LIGHT_CTRL + idx].setBrightness(0.f);
			module->lights[StrokeModule<PORTS>::LIGHT_SHIFT + idx].setBrightness(0.f);
		}
		else if (module) {
			color.a = 1.f;
			text = module->keys[idx].key >= 0 ? keyName(module->keys[idx].key) : "";
			module->lights[StrokeModule<PORTS>::LIGHT_ALT + idx].setBrightness(module->keys[idx].mods & GLFW_MOD_ALT ? 0.7f : 0.f);
			module->lights[StrokeModule<PORTS>::LIGHT_CTRL + idx].setBrightness(module->keys[idx].mods & GLFW_MOD_CONTROL ? 0.7f : 0.f);
			module->lights[StrokeModule<PORTS>::LIGHT_SHIFT + idx].setBrightness(module->keys[idx].mods & GLFW_MOD_SHIFT ? 0.7f : 0.f);
		} 
		OpaqueWidget::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		OpaqueWidget::onButton(e);
	}

	void createContextMenu() {
		struct LearnMenuItem : MenuItem {
			KeyContainer<PORTS>* keyContainer;
			int idx;
			void onAction(const event::Action& e) override {
				keyContainer->enableLearn(idx);
			}
		};

		struct ModeMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			KEY_MODE mode;
			int idx;
			void step() override {
				rightText = module->keys[idx].mode == mode ? "✔" : "";
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				module->keys[idx].mode = mode;
				module->keys[idx].high = false;
			}
		};

		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Hotkey %i", idx + 1)));
		menu->addChild(construct<LearnMenuItem>(&MenuItem::text, "Learn", &LearnMenuItem::keyContainer, keyContainer, &LearnMenuItem::idx, idx));
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Mode"));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Trigger", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::TRIGGER));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Gate", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::GATE));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::TOGGLE));
	}
};


struct StrokeWidget : ThemedModuleWidget<StrokeModule<10>> {
	KeyContainer<10>* keyContainer = NULL;

	StrokeWidget(StrokeModule<10>* module)
		: ThemedModuleWidget<StrokeModule<10>>(module, "Stroke") {
		setModule(module);

		if (module) {
			keyContainer = new KeyContainer<10>;
			keyContainer->module = module;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(keyContainer);
		}

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < 10; i++) {
			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(8.6f, 50.1f + i * 29.4f), module, StrokeModule<10>::LIGHT_SHIFT + i));
			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(14.f, 50.1f + i * 29.4f), module, StrokeModule<10>::LIGHT_CTRL + i));
			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(19.4f, 50.1f + i * 29.4f), module, StrokeModule<10>::LIGHT_ALT + i));

			KeyDisplay<10>* ledDisplay = createWidgetCentered<KeyDisplay<10>>(Vec(45.f, 50.1f + i * 29.4f));
			ledDisplay->box.size = Vec(39.1f, 13.2f);
			ledDisplay->module = module;
			ledDisplay->keyContainer = keyContainer;
			ledDisplay->idx = i;
			addChild(ledDisplay);

			addOutput(createOutputCentered<StoermelderPort>(Vec(71.8f, 50.1f + i * 29.4f), module, StrokeModule<10>::OUTPUT + i));
		}
	}

	~StrokeWidget() {
		if (keyContainer) {
			APP->scene->rack->removeChild(keyContainer);
			delete keyContainer;
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<StrokeModule<10>>::appendContextMenu(menu);
		StrokeModule<10>* module = dynamic_cast<StrokeModule<10>*>(this->module);

		struct PolyphonicOutputItem : MenuItem {
			StrokeModule<10>* module;
			void onAction(const event::Action& e) override {
				module->polyphonicOutput ^= true;
			}
			void step() override {
				rightText = module->polyphonicOutput ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PolyphonicOutputItem>(&MenuItem::text, "Polyphonic output", &PolyphonicOutputItem::module, module));
	}
};

} // namespace Stroke
} // namespace StoermelderPackOne

Model* modelStroke = createModel<StoermelderPackOne::Stroke::StrokeModule<10>, StoermelderPackOne::Stroke::StrokeWidget>("Stroke");