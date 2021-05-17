#include "plugin.hpp"
#include "components/MenuColorLabel.hpp"

namespace StoermelderPackOne {
namespace Stroke {

static std::string keyName(int key) {
	const char* k = glfwGetKeyName(key, 0);
	if (k) {
		std::string str = k;
		for (auto& c : str) c = std::toupper(c);
		return str;
	}

	switch (key) {
		case GLFW_KEY_SPACE:			return "SPACE";
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
		case GLFW_KEY_PAGE_DOWN:		return "PG-DW";
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
		default:						return "";
	}
}

enum class KEY_MODE {
	OFF = 0,
	CV_TRIGGER = 1,
	CV_GATE = 2,
	CV_TOGGLE = 3,
	S_PARAM_RAND = 9,
	S_PARAM_COPY = 10,
	S_PARAM_PASTE = 11,
	S_ZOOM_MODULE_90 = 12,
	S_ZOOM_MODULE_30 = 14,
	S_ZOOM_MODULE_CUSTOM = 16,
	S_ZOOM_OUT = 13,
	S_ZOOM_TOGGLE = 15,
	S_MOVE_LEFT = 40,
	S_MOVE_RIGHT = 41,
	S_MOVE_UP = 42,
	S_MOVE_DOWN = 43,
	S_CABLE_OPACITY = 20,
	S_CABLE_COLOR_NEXT = 21,
	S_CABLE_COLOR = 24,
	S_CABLE_ROTATE = 22,
	S_CABLE_VISIBILITY = 23,
	S_FRAMERATE = 30,
	S_BUSBOARD = 31,
	S_ENGINE_PAUSE = 32,
	S_MODULE_LOCK = 33
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
		ENUMS(LIGHT_TRIG, PORTS),
		NUM_LIGHTS
	};

	struct Key {
		int button = -1;
		int key = -1;
		int mods;
		KEY_MODE mode;
		bool high;
		std::string data;
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	Key keys[PORTS];

	Key* keyTemp = NULL;

	dsp::PulseGenerator pulse[PORTS];

	dsp::PulseGenerator lightPulse[PORTS];
	dsp::ClockDivider lightDivider;

	StrokeModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
		lightDivider.setDivision(512);
	}

	void onReset() override {
		Module::onReset();
		for (int i = 0; i < PORTS; i++) {
			keys[i].button = -1;
			keys[i].key = -1;
			keys[i].mods = 0;
			keys[i].mode = KEY_MODE::CV_TRIGGER;
			keys[i].high = false;
			keys[i].data = "";
		}
	}

	void process(const ProcessArgs& args) override {
		for (int i = 0; i < PORTS; i++) {
			if (keys[i].key >= 0 || keys[i].button >= 0) {
				switch (keys[i].mode) {
					case KEY_MODE::CV_TRIGGER:
						outputs[OUTPUT + i].setVoltage(pulse[i].process(args.sampleTime) * 10.f);
						break;
					case KEY_MODE::CV_GATE:
					case KEY_MODE::CV_TOGGLE:
						outputs[OUTPUT + i].setVoltage(keys[i].high * 10.f);
						break;
					default:
						break;
				}	
			}
		}

		if (lightDivider.process()) {
			for (size_t i = 0; i < PORTS; i++) {
				bool b = lightPulse[i].process(lightDivider.getDivision() * args.sampleTime);
				lights[LIGHT_TRIG + i].setBrightness(b);
			}
		}
	}

	void keyEnable(int idx) {
		switch (keys[idx].mode) {
			case KEY_MODE::OFF:
				break;
			case KEY_MODE::CV_TRIGGER:
				pulse[idx].trigger(); break;
			case KEY_MODE::CV_GATE:
				keys[idx].high = true; break;
			case KEY_MODE::CV_TOGGLE:
				keys[idx].high ^= true; break;
			default:
				keyTemp = &keys[idx];
				break;
		}
		lightPulse[idx].trigger(0.2f);
	}

	void keyDisable(int idx) {
		switch (keys[idx].mode) {
			case KEY_MODE::CV_GATE:
				keys[idx].high = false; break;
			case KEY_MODE::S_MOVE_DOWN:
			case KEY_MODE::S_MOVE_UP:
			case KEY_MODE::S_MOVE_LEFT:
			case KEY_MODE::S_MOVE_RIGHT:
				keyTemp = NULL; break;
			default:
				break;
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* keysJ = json_array();
		for (int i = 0; i < PORTS; i++) {
			json_t* keyJ = json_object();
			json_object_set_new(keyJ, "button", json_integer(keys[i].button));
			json_object_set_new(keyJ, "key", json_integer(keys[i].key));
			json_object_set_new(keyJ, "mods", json_integer(keys[i].mods));
			json_object_set_new(keyJ, "mode", json_integer((int)keys[i].mode));
			json_object_set_new(keyJ, "high", json_boolean(keys[i].high));
			json_object_set_new(keyJ, "data", json_string(keys[i].data.c_str()));
			json_array_append_new(keysJ, keyJ);
		}
		json_object_set_new(rootJ, "keys", keysJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		// Hack for preventing duplicating this module
		// if (APP->engine->getModule(id) != NULL) return;

		json_t* keysJ = json_object_get(rootJ, "keys");
		for (int i = 0; i < PORTS; i++) {
			json_t* keyJ = json_array_get(keysJ, i);
			keys[i].button = json_integer_value(json_object_get(keyJ, "button"));
			keys[i].key = json_integer_value(json_object_get(keyJ, "key"));
			keys[i].mods = json_integer_value(json_object_get(keyJ, "mods"));
			keys[i].mode = (KEY_MODE)json_integer_value(json_object_get(keyJ, "mode"));
			keys[i].high = json_boolean_value(json_object_get(keyJ, "high"));
			json_t* dataJ = json_object_get(keyJ, "data");
			if (dataJ) keys[i].data = json_string_value(dataJ);
		}
	}
};


struct ModifiedRackRail : RackRail {
	bool drawRails = true;

	void draw(const DrawArgs& args) override {
		const float railHeight = 15;

		// Background color
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.0, 0.0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(0x30, 0x30, 0x30));
		nvgFill(args.vg);

		if (drawRails) {
			// Rails
			float holeRadius = 4.0;
			for (float y = 0; y < box.size.y; y += RACK_GRID_HEIGHT) {
				nvgFillColor(args.vg, nvgRGB(0xc9, 0xc9, 0xc9));
				nvgStrokeWidth(args.vg, 1.0);
				nvgStrokeColor(args.vg, nvgRGB(0x9d, 0x9f, 0xa2));
				// Top rail
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 0, y, box.size.x, railHeight);
				for (float x = 0; x < box.size.x; x += RACK_GRID_WIDTH) {
					nvgCircle(args.vg, x + RACK_GRID_WIDTH / 2, y + railHeight / 2, holeRadius);
					nvgPathWinding(args.vg, NVG_HOLE);
				}
				nvgFill(args.vg);

				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, y + railHeight - 0.5);
				nvgLineTo(args.vg, box.size.x, y + railHeight - 0.5);
				nvgStroke(args.vg);

				// Bottom rail
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 0, y + RACK_GRID_HEIGHT - railHeight, box.size.x, railHeight);
				for (float x = 0; x < box.size.x; x += RACK_GRID_WIDTH) {
					nvgCircle(args.vg, x + RACK_GRID_WIDTH / 2, y + RACK_GRID_HEIGHT - railHeight + railHeight / 2, holeRadius);
					nvgPathWinding(args.vg, NVG_HOLE);
				}
				nvgFill(args.vg);

				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, y + RACK_GRID_HEIGHT - 0.5);
				nvgLineTo(args.vg, box.size.x, y + RACK_GRID_HEIGHT - 0.5);
				nvgStroke(args.vg);
			}
		}
	}
}; // struct ModifiedRackRail


template < int PORTS >
struct KeyContainer : Widget {
	StrokeModule<PORTS>* module = NULL;
	int learnIdx = -1;

	float tempParamValue = 0.f;

	RackRail* rackrail = NULL;
	RackRail* rackrackOrg;

	~KeyContainer() {
		cmdBusboard(true);
		if (rackrail) delete rackrail;
	}

	void step() override {
		if (module && module->keyTemp != NULL) {
			switch (module->keyTemp->mode) {
				case KEY_MODE::S_PARAM_RAND:
					cmdParamRand(); break;
				case KEY_MODE::S_PARAM_COPY:
					cmdParamCopy(); break;
				case KEY_MODE::S_PARAM_PASTE:
					cmdParamPaste(); break;
				case KEY_MODE::S_ZOOM_MODULE_90:
					cmdZoomModule(0.9f); break;
				case KEY_MODE::S_ZOOM_MODULE_30:
					cmdZoomModule(0.3f); break;
				case KEY_MODE::S_ZOOM_MODULE_CUSTOM:
					settings::zoom = std::stof(module->keyTemp->data);
					cmdZoomModule(-1.f); break;
				case KEY_MODE::S_ZOOM_OUT:
					cmdZoomOut(); break;
				case KEY_MODE::S_ZOOM_TOGGLE:
					cmdZoomToggle(); break;
				case KEY_MODE::S_MOVE_DOWN:
					cmdMove(0, 5); break;
				case KEY_MODE::S_MOVE_UP:
					cmdMove(0, -5); break;
				case KEY_MODE::S_MOVE_LEFT:
					cmdMove(-5, 0); break;
				case KEY_MODE::S_MOVE_RIGHT:
					cmdMove(5, 0); break;
				case KEY_MODE::S_CABLE_OPACITY:
					cmdCableOpacity(); break;
				case KEY_MODE::S_CABLE_COLOR_NEXT:
					cmdCableColorNext(); break;
				case KEY_MODE::S_CABLE_COLOR:
					cmdCableColor(color::fromHexString(module->keyTemp->data)); break;
				case KEY_MODE::S_CABLE_ROTATE:
					cmdCableRotate(); break;
				case KEY_MODE::S_CABLE_VISIBILITY:
					cmdCableVisibility(); break;
				case KEY_MODE::S_FRAMERATE:
					cmdFramerate(); break;
				case KEY_MODE::S_BUSBOARD:
					cmdBusboard(); break;
				case KEY_MODE::S_ENGINE_PAUSE:
					cmdEnginePause(); break;
				case KEY_MODE::S_MODULE_LOCK:
					cmdModuleLock(); break;
				default:
					break;
			}
			switch (module->keyTemp->mode) {
				case KEY_MODE::S_MOVE_DOWN:
				case KEY_MODE::S_MOVE_UP:
				case KEY_MODE::S_MOVE_LEFT:
				case KEY_MODE::S_MOVE_RIGHT:
					break;
				default:
					module->keyTemp = NULL;
			}
		}
		Widget::step();
	}

	void cmdParamRand() {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) return;
		ParamQuantity* q = p->paramQuantity;
		if (!q) return;
		q->setScaledValue(random::uniform());
	}

	void cmdParamCopy() {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) return;
		ParamQuantity* q = p->paramQuantity;
		if (!q) return;
		tempParamValue = q->getScaledValue();
	}

	void cmdParamPaste() {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) return;
		ParamQuantity* q = p->paramQuantity;
		if (!q) return;
		q->setScaledValue(tempParamValue);
	}

	void cmdZoomModule(float scale) {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw) return;
		StoermelderPackOne::Rack::ViewportCenter{mw, scale};
	}

	void cmdZoomOut() {
		math::Rect moduleBox = APP->scene->rack->moduleContainer->getChildrenBoundingBox();
		if (!moduleBox.size.isFinite()) return;
		StoermelderPackOne::Rack::ViewportCenter{moduleBox};
	}

	void cmdZoomToggle() {
		if (settings::zoom > 1.f) cmdZoomOut(); else cmdZoomModule(0.9f);
	}

	void cmdMove(int x, int y) {
		math::Vec newOffset = APP->scene->rackScroll->offset;
		newOffset.x += math::clamp(x * 10.0f, -100.0f, 100.0f);
		newOffset.y += math::clamp(y * 10.0f, -100.0f, 100.0f);
		APP->scene->rackScroll->offset = newOffset;
	}

	void cmdCableOpacity() {
		if (settings::cableOpacity == 0.f) {
			settings::cableOpacity = std::stof(module->keyTemp->data);
		}
		else {
			module->keyTemp->data = string::f("%f", settings::cableOpacity);
			settings::cableOpacity = 0.f;
		}
	}

	void cmdCableVisibility() {
		if (APP->scene->rack->cableContainer->visible) {
			APP->scene->rack->cableContainer->hide();
		}
		else {
			APP->scene->rack->cableContainer->show();
		}
	}

	void cmdCableColorNext() {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		PortWidget* pw = dynamic_cast<PortWidget*>(w);
		if (!pw) return;
		CableWidget* cw = APP->scene->rack->getTopCable(pw);
		if (!cw) return;
		int cid = APP->scene->rack->nextCableColorId++;
		APP->scene->rack->nextCableColorId %= settings::cableColors.size();
		cw->color = settings::cableColors[cid];
	}

	void cmdCableColor(NVGcolor c) {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		PortWidget* pw = dynamic_cast<PortWidget*>(w);
		if (!pw) return;
		CableWidget* cw = APP->scene->rack->getTopCable(pw);
		if (!cw) return;
		cw->color = c;
	}

	void cmdCableRotate() {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		PortWidget* pw = dynamic_cast<PortWidget*>(w);
		if (!pw) return;

		Widget* cc = APP->scene->rack->cableContainer;
		std::list<Widget*>::iterator it;
		for (it = cc->children.begin(); it != cc->children.end(); it++) {
			CableWidget* cw = dynamic_cast<CableWidget*>(*it);
			assert(cw);
			// Ignore incomplete cables
			if (!cw->isComplete())
				continue;
			if (cw->inputPort == pw || cw->outputPort == pw)
				break;
		}
		if (it != cc->children.end()) {
			cc->children.splice(cc->children.end(), cc->children, it);
		}
	}

	void cmdFramerate() {
		if (APP->scene->frameRateWidget->visible) {
			APP->scene->frameRateWidget->hide();
		}
		else {
			APP->scene->frameRateWidget->show();
		}
	}

	void cmdBusboard(bool forceRemove = false) {
		if (!rackrail) {
			rackrail = new ModifiedRackRail;
			rackrackOrg = APP->scene->rack->railFb->getFirstDescendantOfType<RackRail>();
		}

		RackRail* r = APP->scene->rack->railFb->getFirstDescendantOfType<RackRail>();
		if (r == rackrail) {
			APP->scene->rack->railFb->removeChild(rackrail);
			APP->scene->rack->railFb->addChild(rackrackOrg);
		}
		if (r != rackrail && !forceRemove) {
			APP->scene->rack->railFb->removeChild(rackrackOrg);
			APP->scene->rack->railFb->addChild(rackrail);
		}
		APP->scene->rack->railFb->dirty = true;
	}

	void cmdEnginePause() {
		APP->engine->setPaused(!APP->engine->isPaused());
	}

	void cmdModuleLock() {
		settings::lockModules ^= true;
	}

	void onButton(const event::Button& e) override {
		if (module && !module->bypass && e.button > 2) {
			if (e.action == GLFW_PRESS) {
				if (learnIdx >= 0) {
					module->keys[learnIdx].button = e.button;
					module->keys[learnIdx].key = -1;
					module->keys[learnIdx].mods = e.mods;
					learnIdx = -1;
					e.consume(this);
				}
				else {
					for (int i = 0; i < PORTS; i++) {
						if (e.button == module->keys[i].button && e.mods == module->keys[i].mods) {
							module->keyEnable(i);
							e.consume(this);
						}
					}
				}
			}
			if (e.action == RACK_HELD) {
				for (int i = 0; i < PORTS; i++) {
					if (e.button == module->keys[i].button && e.mods == module->keys[i].mods) {
						e.consume(this);
					}
				}
			}
			if (e.action == GLFW_RELEASE) {
				for (int i = 0; i < PORTS; i++) {
					if (e.button == module->keys[i].button) {
						module->keyDisable(i);
						e.consume(this);
					}
				}
			}
		}
		Widget::onButton(e);
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (module && !module->bypass) {
			if (e.action == GLFW_PRESS) {
				if (learnIdx >= 0) {
					std::string kn = keyName(e.key);
					if (!kn.empty()) {
						module->keys[learnIdx].button = -1;
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
			if (e.action == RACK_HELD) {
				for (int i = 0; i < PORTS; i++) {
					if (e.key == module->keys[i].key && e.mods == module->keys[i].mods) {
						e.consume(this);
					}
				}
			}
			if (e.action == GLFW_RELEASE) {
				for (int i = 0; i < PORTS; i++) {
					if (e.key == module->keys[i].key) {
						module->keyDisable(i);
						e.consume(this);
					}
				}
			}
		}
		Widget::onHoverKey(e);
	}

	void enableLearn(int idx) {
		learnIdx = learnIdx != idx ? idx : -1;
	}
};

template < int PORTS >
struct KeyDisplay : StoermelderLedDisplay {
	KeyContainer<PORTS>* keyContainer;
	StrokeModule<PORTS>* module;
	int idx;

	void step() override {
		if (keyContainer && keyContainer->learnIdx == idx) {
			color.a = 0.6f;
			text = "<LRN>";
			module->lights[StrokeModule<PORTS>::LIGHT_ALT + idx].setBrightness(0.1f);
			module->lights[StrokeModule<PORTS>::LIGHT_CTRL + idx].setBrightness(0.1f);
			module->lights[StrokeModule<PORTS>::LIGHT_SHIFT + idx].setBrightness(0.1f);
		}
		else if (module) {
			color.a = 1.f;
			text = module->keys[idx].key >= 0 ? keyName(module->keys[idx].key) : module->keys[idx].button > 0 ? string::f("MB %i", module->keys[idx].button + 1) : "";
			module->lights[StrokeModule<PORTS>::LIGHT_ALT + idx].setBrightness(module->keys[idx].mods & GLFW_MOD_ALT ? 0.7f : 0.f);
			module->lights[StrokeModule<PORTS>::LIGHT_CTRL + idx].setBrightness(module->keys[idx].mods & GLFW_MOD_CONTROL ? 0.7f : 0.f);
			module->lights[StrokeModule<PORTS>::LIGHT_SHIFT + idx].setBrightness(module->keys[idx].mods & GLFW_MOD_SHIFT ? 0.7f : 0.f);
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
		struct LearnMenuItem : MenuItem {
			KeyContainer<PORTS>* keyContainer;
			int idx;
			void onAction(const event::Action& e) override {
				keyContainer->enableLearn(idx);
			}
		};

		struct ClearMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			int idx;
			void onAction(const event::Action& e) override {
				module->keys[idx].button = -1;
				module->keys[idx].key = -1;
				module->keys[idx].mods = 0;
			}
		};

		struct ModeMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			KEY_MODE mode;
			int idx;
			void step() override {
				rightText = CHECKMARK(module->keys[idx].mode == mode);
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				module->keys[idx].mode = mode;
				module->keys[idx].high = false;
			}
		};

		struct ParamMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			int idx;
			void step() override {
				rightText = 
					module->keys[idx].mode == KEY_MODE::S_PARAM_RAND ||
					module->keys[idx].mode == KEY_MODE::S_PARAM_COPY ||
					module->keys[idx].mode == KEY_MODE::S_PARAM_PASTE
						? "✔" : RIGHT_ARROW;
				MenuItem::step();
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Randomize", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_PARAM_RAND));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Value copy", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_PARAM_COPY));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Value paste", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_PARAM_PASTE));
				return menu;
			}
		}; // struct ParamMenuItem

		struct ViewMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			int idx;
			void step() override {
				rightText = 
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_90 ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_30 ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_CUSTOM ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_OUT ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_TOGGLE ||
					module->keys[idx].mode == KEY_MODE::S_MOVE_DOWN ||
					module->keys[idx].mode == KEY_MODE::S_MOVE_UP ||
					module->keys[idx].mode == KEY_MODE::S_MOVE_LEFT ||
					module->keys[idx].mode == KEY_MODE::S_MOVE_RIGHT
						? "✔" : RIGHT_ARROW;
				MenuItem::step();
			}

			Menu* createChildMenu() override {
				struct ModeZoomModuleCustomItem : MenuItem {
					StrokeModule<PORTS>* module;
					int idx;
					void step() override {
						rightText = module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_CUSTOM ? "✔ " RIGHT_ARROW : "";
						MenuItem::step();
					}
					void onAction(const event::Action& e) override {
						module->keys[idx].mode = KEY_MODE::S_ZOOM_MODULE_CUSTOM;
						module->keys[idx].high = false;
						module->keys[idx].data = "0";
					}

					Menu* createChildMenu() override {
						struct ZoomModuleSlider : ui::Slider {
							struct ZoomModuleQuantity : Quantity {
								StrokeModule<PORTS>* module;
								int idx;
								ZoomModuleQuantity(StrokeModule<PORTS>* module, int idx) {
									this->module = module;
									this->idx = idx;
								}
								void setValue(float value) override {
									module->keys[idx].data = string::f("%f", clamp(value, -2.f, 2.f));
								}
								float getValue() override {
									return std::stof(module->keys[idx].data);
								}
								float getDefaultValue() override {
									return 0.0f;
								}
								float getDisplayValue() override {
									return std::round(std::pow(2.f, getValue()) * 100);
								}
								void setDisplayValue(float displayValue) override {
									setValue(std::log2(displayValue / 100));
								}
								std::string getLabel() override {
									return "Zoom";
								}
								std::string getUnit() override {
									return "%";
								}
								float getMaxValue() override {
									return 2.f;
								}
								float getMinValue() override {
									return -2.f;
								}
							};

							ZoomModuleSlider(StrokeModule<PORTS>* module, int idx) {
								box.size.x = 180.0f;
								quantity = new ZoomModuleQuantity(module, idx);
							}
							~ZoomModuleSlider() {
								delete quantity;
							}
						};

						if (module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_CUSTOM) {
							Menu* menu = new Menu;
							menu->addChild(new ZoomModuleSlider(module, idx));
							return menu;
						}
						return NULL;
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom to module", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_90));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom to module 1/3", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_30));
				menu->addChild(construct<ModeZoomModuleCustomItem>(&MenuItem::text, "Zoom level to module", &ModeZoomModuleCustomItem::module, module, &ModeZoomModuleCustomItem::idx, idx));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom out", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_OUT));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom toggle", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_TOGGLE));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Move up", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MOVE_UP));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Move down", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MOVE_DOWN));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Move left", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MOVE_LEFT));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Move right", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MOVE_RIGHT));
				return menu;
			}
		}; // struct ViewMenuItem

		struct CableMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			int idx;
			void step() override {
				rightText = 
					module->keys[idx].mode == KEY_MODE::S_CABLE_OPACITY ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_VISIBILITY ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR_NEXT ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_ROTATE
						? "✔" : RIGHT_ARROW;
				MenuItem::step();
			}

			Menu* createChildMenu() override {
				struct CableOpacityItem : ModeMenuItem {
					void onAction(const event::Action& e) override {
						ModeMenuItem::onAction(e);
						ModeMenuItem::module->keys[ModeMenuItem::idx].data = "0";
					}
				};

				struct CableColorMenuItem : MenuItem {
					StrokeModule<PORTS>* module;
					int idx;
					void step() override {
						rightText = module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR ? "✔ " RIGHT_ARROW : "";
						MenuItem::step();
					}
					void onAction(const event::Action& e) override {
						module->keys[idx].mode = KEY_MODE::S_CABLE_COLOR;
						module->keys[idx].high = false;
						module->keys[idx].data = color::toHexString(color::BLACK);
					}

					Menu* createChildMenu() override {
						struct ColorField : ui::TextField {
							StrokeModule<PORTS>* module;
							int idx;
							ColorField() {
								box.size.x = 80.f;
								placeholder = color::toHexString(color::BLACK);
							}
							void onSelectKey(const event::SelectKey& e) override {
								if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
									module->keys[idx].data = string::trim(text);
									ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
									overlay->requestDelete();
									e.consume(this);
								}
								if (!e.getTarget()) {
									ui::TextField::onSelectKey(e);
								}
							}
						};

						if (module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR) {
							Menu* menu = new Menu;
							menu->addChild(construct<MenuColorLabel>(&MenuColorLabel::fillColor, color::fromHexString(module->keys[idx].data)));
							menu->addChild(construct<ColorField>(&ColorField::module, module, &ColorField::idx, idx, &TextField::text, module->keys[idx].data));
							return menu;
						}
						return NULL;
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<CableOpacityItem>(&MenuItem::text, "Toggle opacity", &CableOpacityItem::module, module, &CableOpacityItem::idx, idx, &CableOpacityItem::mode, KEY_MODE::S_CABLE_OPACITY));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle visibility", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_CABLE_VISIBILITY));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Next color", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_CABLE_COLOR_NEXT));
				menu->addChild(construct<CableColorMenuItem>(&MenuItem::text, "Color", &CableColorMenuItem::module, module, &CableColorMenuItem::idx, idx));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Rotate ordering", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_CABLE_ROTATE));
				return menu;
			}
		}; // struct CableMenuItem

		struct SpecialMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			int idx;
			void step() override {
				rightText = 
					module->keys[idx].mode == KEY_MODE::S_FRAMERATE ||
					module->keys[idx].mode == KEY_MODE::S_BUSBOARD ||
					module->keys[idx].mode == KEY_MODE::S_ENGINE_PAUSE ||
					module->keys[idx].mode == KEY_MODE::S_MODULE_LOCK
						? "✔" : RIGHT_ARROW;
				MenuItem::step();
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle framerate display", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_FRAMERATE));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle busboard", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_BUSBOARD));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle engine pause", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ENGINE_PAUSE));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle lock modules", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MODULE_LOCK));
				return menu;
			}
		}; // struct SpecialMenuItem

		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Hotkey %i", idx + 1)));
		menu->addChild(construct<LearnMenuItem>(&MenuItem::text, "Learn", &LearnMenuItem::keyContainer, keyContainer, &LearnMenuItem::idx, idx));
		menu->addChild(construct<ClearMenuItem>(&MenuItem::text, "Clear", &ClearMenuItem::module, module, &ClearMenuItem::idx, idx));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Output mode"));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Off", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::OFF));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Trigger", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::CV_TRIGGER));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Gate", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::CV_GATE));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::CV_TOGGLE));
		menu->addChild(construct<ParamMenuItem>(&MenuItem::text, "Parameter commands", &ParamMenuItem::module, module, &ParamMenuItem::idx, idx));
		menu->addChild(construct<ViewMenuItem>(&MenuItem::text, "View commands", &ViewMenuItem::module, module, &ViewMenuItem::idx, idx));
		menu->addChild(construct<CableMenuItem>(&MenuItem::text, "Cable commands", &CableMenuItem::module, module, &CableMenuItem::idx, idx));
		menu->addChild(construct<SpecialMenuItem>(&MenuItem::text, "Special commands", &SpecialMenuItem::module, module, &SpecialMenuItem::idx, idx));
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
			ledDisplay->module = module;
			ledDisplay->keyContainer = keyContainer;
			ledDisplay->idx = i;
			addChild(ledDisplay);

			addChild(createLightCentered<TinyLight<YellowLight>>(Vec(60.2f, 40.f + i * 29.4f), module, StrokeModule<10>::LIGHT_TRIG + i));
			addOutput(createOutputCentered<StoermelderPort>(Vec(71.8f, 50.1f + i * 29.4f), module, StrokeModule<10>::OUTPUT + i));
		}
	}

	~StrokeWidget() {
		if (keyContainer) {
			APP->scene->rack->removeChild(keyContainer);
			delete keyContainer;
		}
	}
};

} // namespace Stroke
} // namespace StoermelderPackOne

Model* modelStroke = createModel<StoermelderPackOne::Stroke::StrokeModule<10>, StoermelderPackOne::Stroke::StrokeWidget>("Stroke");