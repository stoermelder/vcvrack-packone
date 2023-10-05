#include "plugin.hpp"
#include "components/MenuColorLabel.hpp"
#include "components/MenuColorField.hpp"
#include "components/MenuColorPicker.hpp"
#include "ui/ModuleSelectProcessor.hpp"
#include "ui/keyboard.hpp"

namespace StoermelderPackOne {
namespace Stroke {

enum class KEY_MODE {
	OFF = 0,
	CV_TRIGGER = 1,
	CV_GATE = 2,
	CV_TOGGLE = 3,
	S_PARAM_RAND = 9,
	S_PARAM_COPY = 10,
	S_PARAM_PASTE = 11,
	S_ZOOM_MODULE_90 = 12,
	S_ZOOM_MODULE_90_SMOOTH = 121,
	S_ZOOM_MODULE_30 = 14,
	S_ZOOM_MODULE_30_SMOOTH = 141,
	S_ZOOM_MODULE_CUSTOM = 16,
	S_ZOOM_MODULE_CUSTOM_SMOOTH = 161,
	S_ZOOM_MODULE_ID = 17,
	S_ZOOM_MODULE_ID_SMOOTH = 171,
	S_ZOOM_OUT = 13,
	S_ZOOM_OUT_SMOOTH = 131,
	S_ZOOM_TOGGLE = 15,
	S_ZOOM_TOGGLE_SMOOTH = 151,
	S_CABLE_OPACITY = 20,
	S_CABLE_COLOR_NEXT = 21,
	S_CABLE_COLOR = 24,
	S_CABLE_ROTATE = 22,
	S_CABLE_VISIBILITY = 23,
	S_CABLE_MULTIDRAG = 25, // disabled
	S_FRAMERATE = 30, // not supported in v2
	S_BUSBOARD = 31, // not supported in v2
	S_ENGINE_PAUSE = 32, // not supported in v2
	S_MODULE_LOCK = 33,
	S_MODULE_ADD = 34,
	S_MODULE_ADD_RANDOM = 38,
	S_MODULE_DISPATCH = 35,
	S_MODULE_PRESET_SAVE = 36,
	S_MODULE_PRESET_SAVE_DEFAULT = 37,
	S_SCROLL_LEFT = 40,
	S_SCROLL_RIGHT = 41,
	S_SCROLL_UP = 42,
	S_SCROLL_DOWN = 43
};

template <int PORTS>
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
		bool isMapped() { return button != -1 || key != -1; }
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	Key keys[PORTS];

	Key* keyTemp = NULL;
	Key* keyTempHeld = NULL;
	Key* keyTempDisable = NULL;

	dsp::PulseGenerator pulse[PORTS];

	dsp::PulseGenerator lightPulse[PORTS];
	dsp::ClockDivider lightDivider;

	StrokeModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < PORTS; i++) {
			configOutput(OUTPUT + i, string::f("Hotkey %i trigger/gate", i + 1));
		}
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

	void keyHeld(int idx) {
		keyTempHeld = &keys[idx];
		lightPulse[idx].trigger(0.1f);
	}

	void keyDisable(int idx) {
		switch (keys[idx].mode) {
			case KEY_MODE::CV_GATE:
				keys[idx].high = false; break;
			default:
				keyTempDisable = &keys[idx];
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

		json_t* keysJ = json_object_get(rootJ, "keys");
		for (int i = 0; i < PORTS; i++) {
			json_t* keyJ = json_array_get(keysJ, i);
			keys[i].button = json_integer_value(json_object_get(keyJ, "button"));
			keys[i].key = keyFix(json_integer_value(json_object_get(keyJ, "key")));
			keys[i].mods = json_integer_value(json_object_get(keyJ, "mods")) & (GLFW_MOD_ALT | GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
			keys[i].mode = (KEY_MODE)json_integer_value(json_object_get(keyJ, "mode"));
			keys[i].high = json_boolean_value(json_object_get(keyJ, "high"));
			json_t* dataJ = json_object_get(keyJ, "data");
			if (dataJ) keys[i].data = json_string_value(dataJ);
		}
	}
};



// -- Commands --

struct CmdBase {
	virtual ~CmdBase() { }
	virtual void initialCmd(KEY_MODE keyMode) { }
	virtual bool followUpCmd(KEY_MODE keyMode) { return true; }
	virtual void step() { }
}; // struct CmdBase


struct CmdParamRand : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) return;
		ParamQuantity* q = p->getParamQuantity();
		if (!q) return;
		q->setScaledValue(random::uniform());
	}
}; // struct CmdParamRand


struct CmdParamCopyPaste : CmdBase {
	static bool cmd(KEY_MODE keyMode) {
		static float tempParamValue;
		static bool tempParamInitialized = false;

		Widget* w = APP->event->getHoveredWidget();
		if (!w) return true;
		ParamWidget* p = dynamic_cast<ParamWidget*>(w);
		if (!p) return true;
		ParamQuantity* q = p->getParamQuantity();
		if (!q) return true;

		if (keyMode == KEY_MODE::S_PARAM_COPY) {
			tempParamValue = q->getScaledValue();
			tempParamInitialized = true;
		}

		if (keyMode == KEY_MODE::S_PARAM_PASTE && tempParamInitialized) {
			q->setScaledValue(tempParamValue);
		}

		return false;
	}

	void initialCmd(KEY_MODE keyMode) override {
		cmd(keyMode);
	}

	bool followUpCmd(KEY_MODE keyMode) override {
		if (keyMode != KEY_MODE::S_PARAM_PASTE) return true;
		return cmd(keyMode);
	}
}; // struct CmdParamCopyPase


struct CmdZoomModule : CmdBase {
	float scale;
	void initialCmd(KEY_MODE keyMode) override {
		zoomIn(scale);
	}
	static void zoomIn(float s) {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw) return;
		StoermelderPackOne::Rack::ViewportCenter{mw, s};
	}
}; // struct CmdZoomModule


struct CmdZoomModuleSmooth : CmdBase {
	float scale;
	StoermelderPackOne::Rack::ViewportCenterSmooth viewportCenterSmooth;
	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw) return;
		Vec p = mw->box.size.mult(Vec(1.f - scale, 1.f - scale));
		viewportCenterSmooth.trigger(mw->box.grow(p), 1.f / APP->window->getLastFrameDuration(), 0.6f);
	}
	void step() override {
		viewportCenterSmooth.process();
	}
}; // struct CmdZoomModuleSmooth


struct CmdZoomModuleCustom : CmdBase {
	std::string* data;
	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw) return;
		StoermelderPackOne::Rack::ViewportCenter{mw, -1.f, std::stof(*data)};
	}
}; // struct CmdZoomModuleCustom


struct CmdZoomModuleCustomSmooth : CmdBase {
	std::string* data;
	StoermelderPackOne::Rack::ViewportCenterSmooth viewportCenterSmooth;
	void initialCmd(KEY_MODE keyMode) override {
		float zoom = std::stof(*data);
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw) return;
		viewportCenterSmooth.trigger(mw, zoom, 1.f / APP->window->getLastFrameDuration(), 0.6f);
	}
	void step() override {
		viewportCenterSmooth.process();
	}
}; // struct CmdZoomModuleCustomSmooth


struct CmdZoomModuleId : CmdBase {
	std::string* data;
	float scale;
	void initialCmd(KEY_MODE keyMode) override {
		if (*data == "") return;
		int64_t moduleId = std::stoll(*data);
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		if (!mw) return;
		StoermelderPackOne::Rack::ViewportCenter{mw, scale};
	}
}; // struct CmdZoomModuleId


struct CmdZoomModuleIdSmooth : CmdBase {
	std::string* data;
	float scale;
	StoermelderPackOne::Rack::ViewportCenterSmooth viewportCenterSmooth;
	void initialCmd(KEY_MODE keyMode) override {
		if (*data == "") return;
		int64_t moduleId = std::stoll(*data);
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		if (!mw) return;
		Vec p = mw->box.size.mult(Vec(1.f - scale, 1.f - scale));
		viewportCenterSmooth.trigger(mw->box.grow(p), 1.f / APP->window->getLastFrameDuration(), 0.6f);
	}
	void step() override {
		viewportCenterSmooth.process();
	}
}; // struct CmdZoomModuleIdSmooth



struct CmdZoomOut : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		zoomOut();
	}
	static void zoomOut() {
		math::Rect moduleBox = APP->scene->rack->getModuleContainer()->getChildrenBoundingBox();
		if (!moduleBox.size.isFinite()) return;
		StoermelderPackOne::Rack::ViewportCenter{moduleBox};
	}
}; // struct CmdZoomOut


struct CmdZoomOutSmooth : CmdBase {
	StoermelderPackOne::Rack::ViewportCenterSmooth viewportCenterSmooth;
	void initialCmd(KEY_MODE keyMode) override {
		math::Rect moduleBox = APP->scene->rack->getModuleContainer()->getChildrenBoundingBox();
		if (!moduleBox.size.isFinite()) return;
		viewportCenterSmooth.trigger(moduleBox, 1.f / APP->window->getLastFrameDuration(), 0.6f);
	}
	void step() override {
		viewportCenterSmooth.process();
	}
}; // struct CmdZoomOutSmooth


struct CmdZoomToggle : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		if (std::log2(APP->scene->rackScroll->getZoom()) > 1.f) CmdZoomOut::zoomOut(); else CmdZoomModule::zoomIn(0.9f);
	}
}; // struct CmdZoomToggle


struct CmdZoomToggleSmooth : CmdZoomModuleSmooth {
	void initialCmd(KEY_MODE keyMode) override {
		if (std::log2(APP->scene->rackScroll->getZoom()) > 1.f) {
			math::Rect moduleBox = APP->scene->rack->getModuleContainer()->getChildrenBoundingBox();
			if (!moduleBox.size.isFinite()) return;
			viewportCenterSmooth.trigger(moduleBox, 1.f / APP->window->getLastFrameDuration(), 0.6f);
		}
		else {
			CmdZoomModuleSmooth::initialCmd(keyMode);
		}
	}
}; // struct CmdZoomToggleSmooth


struct CmdCableOpacity : CmdBase {
	std::string* data;
	void initialCmd(KEY_MODE keyMode) override {
		if (settings::cableOpacity == 0.f) {
			settings::cableOpacity = std::stof(*data);
		}
		else {
			*data = string::f("%f", settings::cableOpacity);
			settings::cableOpacity = 0.f;
		}
	}
}; // struct CmdCableOpacity


struct CmdCableVisibility : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		if (APP->scene->rack->getCableContainer()->visible) {
			APP->scene->rack->getCableContainer()->hide();
		}
		else {
			APP->scene->rack->getCableContainer()->show();
		}
	}
}; // struct CmdCableVisibility


struct CmdCableColorNext : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		PortWidget* pw = dynamic_cast<PortWidget*>(w);
		if (!pw) return;
		CableWidget* cw = APP->scene->rack->getTopCable(pw);
		if (!cw) return;
		cw->color = APP->scene->rack->getNextCableColor();
	}
}; // struct CmdCableColorNext


struct CmdCableColor : CmdBase {
	std::string* data;
	void initialCmd(KEY_MODE keyMode) override {
		NVGcolor c = color::fromHexString(*data);
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		PortWidget* pw = dynamic_cast<PortWidget*>(w);
		if (!pw) return;
		CableWidget* cw = APP->scene->rack->getTopCable(pw);
		if (!cw) return;
		cw->color = c;
	}
}; // struct CmdCableColor


struct CmdCableRotate : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		PortWidget* pw = dynamic_cast<PortWidget*>(w);
		if (!pw) return;

		Widget* cc = APP->scene->rack->getCableContainer();
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
}; // struct CmdCableRotate

/*
struct CmdCableMultiDrag : CmdBase {
	PortWidget* pwSource = NULL;
	int cableId = -1;

	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		pwSource = dynamic_cast<PortWidget*>(w);
		if (!pwSource) return;
		if (!APP->scene->rack->incompleteCable) return;
		cableId = APP->scene->rack->incompleteCable->cable->id;
	}

	bool followUpCmd(KEY_MODE keyMode) override {
		if (keyMode != KEY_MODE::S_CABLE_MULTIDRAG) return true;
		if (!pwSource || pwSource->type != PortWidget::Type::OUTPUT) return true;

		CableWidget* cw1 = findCableWidget(cableId);
		if (!cw1) return true;
		PortWidget* pwTarget = cw1->outputPort;
		std::list<CableWidget*> todo;

		Widget* cc = APP->scene->rack->getCableContainer();
		std::list<Widget*>::iterator it;
		for (it = cc->children.begin(); it != cc->children.end(); it++) {
			CableWidget* cw = dynamic_cast<CableWidget*>(*it);
			assert(cw);
			// Ignore incomplete cables
			if (!cw->isComplete())
				continue;
			if (cw->outputPort == pwSource) {
				todo.push_back(cw);
			}
		}

		if (todo.size() > 0) {
			history::ComplexAction* hc = new history::ComplexAction;
			hc->name = "multi-drag cables";

			for (CableWidget* cw : todo) {
				CableOutputChange* h = new CableOutputChange;
				h->cableId = cw->cable->id;
				h->oldOutputModuleId = cw->outputPort->module->id;
				h->oldOutputId = cw->outputPort->portId;

				cw->setOutput(pwTarget);

				h->newOutputModuleId = cw->outputPort->module->id;
				h->newOutputId = cw->outputPort->portId;
				hc->push(h);
			}

			APP->history->push(hc);
		}
		return true;
	}

	static CableWidget* findCableWidget(int cableId) {
		for (auto it = APP->scene->rack->getCableContainer()->children.begin(); it != APP->scene->rack->cableContainer->children.end(); it++) {
			CableWidget* cw = dynamic_cast<CableWidget*>(*it);
			if (cw->cable->id == cableId) return cw;
		}
		return NULL;
	}

	struct CableOutputChange : history::Action {
		int cableId;
		int64_t oldOutputModuleId;
		int oldOutputId;
		int64_t newOutputModuleId;
		int newOutputId;

		void undo() override {
			CableWidget* cw = findCableWidget(cableId);
			if (!cw) return;
			app::ModuleWidget* outputModule = APP->scene->rack->getModule(oldOutputModuleId);
			assert(outputModule);
			app::PortWidget* outputPort = outputModule->getOutput(oldOutputId);
			assert(outputPort);
			cw->setOutput(outputPort);
		}

		void redo() override {
			CableWidget* cw = findCableWidget(cableId);
			if (!cw) return;
			app::ModuleWidget* outputModule = APP->scene->rack->getModule(newOutputModuleId);
			assert(outputModule);
			app::PortWidget* outputPort = outputModule->getOutput(newOutputId);
			assert(outputPort);
			cw->setOutput(outputPort);
		}
	}; // struct CableOutputChange
}; // struct CmdCableMultiDrag
*/

/*
struct CmdFramerate : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		if (APP->scene->frameRateWidget->visible) {
			APP->scene->frameRateWidget->hide();
		}
		else {
			APP->scene->frameRateWidget->show();
		}
	}
}; // struct CmdFramerate
*/


struct CmdModuleLock : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		settings::lockModules ^= true;
	}
}; // struct CmdModuleLock


struct CmdModuleAdd : CmdBase {
	std::string* data;
	void initialCmd(KEY_MODE keyMode) override {
		if (*data == "") return;
		json_error_t error;
		json_t* oJ = json_loads(data->c_str(), 0, &error);
		DEFER({
			json_decref(oJ);
		});

		json_t* moduleJ = json_object_get(oJ, "module");
		// Get slugs
		json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
		if (!pluginSlugJ) return;
		json_t* modelSlugJ = json_object_get(moduleJ, "model");
		if (!modelSlugJ) return;

		std::string pluginSlug = json_string_value(pluginSlugJ);
		std::string modelSlug = json_string_value(modelSlugJ);

		// Get Model
		plugin::Model* model = plugin::getModel(pluginSlug, modelSlug);
		if (!model) return;

		// Create Module
		engine::Module* addedModule = model->createModule();
		APP->engine->addModule(addedModule);

		// Create ModuleWidget
		ModuleWidget* moduleWidget = model->createModuleWidget(addedModule);
		assert(moduleWidget);
		APP->scene->rack->addModuleAtMouse(moduleWidget);
		moduleWidget->fromJson(moduleJ);

		// ModuleAdd history action
		history::ModuleAdd* h = new history::ModuleAdd;
		h->name = "create module";
		h->setModule(moduleWidget);
		APP->history->push(h);
	}
}; // struct CmdModuleAdd


struct CmdModuleAddRandom : CmdBase {
	static std::vector<std::tuple<std::string, std::string>> models;

	void initModels() {
		for (plugin::Plugin* plugin : rack::plugin::plugins) {
			for (plugin::Model* model : plugin->models) {
				models.push_back(std::make_tuple(plugin->slug, model->slug));
			}
		}
	}

	void initialCmd(KEY_MODE keyMode) override {
		if (models.size() == 0) {
			initModels();
		}

		// Choose a random model
		std::tuple<std::string, std::string> m = models.at(random::u32() % models.size());

		// Get Model
		plugin::Model* model = plugin::getModel(std::get<0>(m), std::get<1>(m));
		if (!model) return;

		// Create Module
		engine::Module* addedModule = model->createModule();
		APP->engine->addModule(addedModule);

		// Create ModuleWidget
		ModuleWidget* moduleWidget = model->createModuleWidget(addedModule);
		assert(moduleWidget);
		APP->scene->rack->addModuleAtMouse(moduleWidget);

		// ModuleAdd history action
		history::ModuleAdd* h = new history::ModuleAdd;
		h->name = "create module";
		h->setModule(moduleWidget);
		APP->history->push(h);
	}
}; // struct CmdModuleAddRandom

std::vector<std::tuple<std::string, std::string>> CmdModuleAddRandom::models;


struct CmdModuleDispatch : CmdBase {
	std::string* data;
	void initialCmd(KEY_MODE keyMode) override {
		if (*data == "") return;
		dispatch(GLFW_PRESS);
	}

	bool followUpCmd(KEY_MODE keyMode) override { 
		if (keyMode != KEY_MODE::S_MODULE_DISPATCH) return true;
		if (*data == "") return true;
		dispatch(GLFW_RELEASE);
		return true;
	}

	void dispatch(int action) {
		json_error_t error;
		json_t* oJ = json_loads(data->c_str(), 0, &error);
		DEFER({
			json_decref(oJ);
		});

		int64_t moduleId = json_integer_value(json_object_get(oJ, "moduleId"));
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		if (!mw) return;

		Vec pos;
		pos.x = json_real_value(json_object_get(oJ, "x"));
		pos.y = json_real_value(json_object_get(oJ, "y"));
		int key = json_integer_value(json_object_get(oJ, "key"));
		int scancode = json_integer_value(json_object_get(oJ, "scancode"));
		int mods = json_integer_value(json_object_get(oJ, "mods"));

		EventContext c;
		event::HoverKey e;
		e.context = &c;
		e.key = key;
		e.keyName = glfwGetKeyName(key, scancode);
		e.mods = mods;
		e.action = action;
		e.pos = pos;
		mw->onHoverKey(e);
	}
}; // struct CmdModuleDispatch


struct CmdModulePresetSave : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw) return;
		mw->saveDialog();
	}
}; // struct CmdModulePresetSave


struct CmdModulePresetSaveDefault : CmdBase {
	void initialCmd(KEY_MODE keyMode) override {
		Widget* w = APP->event->getHoveredWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw) return;
		mw->saveTemplateDialog();
	}
}; // struct CmdModulePresetSaveDefault


struct CmdRackMove : CmdBase {
	KEY_MODE keyMode;
	float x = 0.f;
	float y = 0.f;
	float arrowSpeed = 30.0;
	void initialCmd(KEY_MODE keyMode) override {
		this->keyMode = keyMode;
		math::Vec newOffset = APP->scene->rackScroll->offset;
		newOffset.x += x * arrowSpeed;
		newOffset.y += y * arrowSpeed;
		APP->scene->rackScroll->offset = newOffset;
	}
	bool followUpCmd(KEY_MODE keyMode) override {
		if (this->keyMode != keyMode) return true;
		initialCmd(keyMode);
		return false;
	}
};

/*
struct CmdBusboard {
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

	RackRail* rackrail = NULL;
	RackRail* rackrackOrg = NULL;
	
	~CmdBusboard() {
		process(true);
		delete rackrail;
	}

	void process(bool forceRemove = false) {
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
}; // struct CmdBusboard
*/

// -- GUI --

template<int PORTS>
struct KeyContainer : Widget {
	StrokeModule<PORTS>* module = NULL;
	int learnIdx = -1;
	int learnIdxEx = -1;
	std::function<void(int key, int scancode, int mods)> learnCallback = { };

	ModuleSelectProcessor moduleSelectProcessor;

	CmdBase* previousCmd = NULL;
	//CmdBusboard* cmdBusboard = NULL;

	~KeyContainer() {
		if (previousCmd) delete previousCmd;
		//if (cmdBusboard) delete cmdBusboard;
	}

	template <class T, typename... Args>
	void processCmd(Args... args) {
		KEY_MODE keyMode = module->keyTemp->mode;
		if (previousCmd) {
			bool shouldClear = previousCmd->followUpCmd(keyMode);
			if (shouldClear) {
				delete previousCmd;
				previousCmd = NULL;
			}
			else {
				return;
			}
		}
		previousCmd = construct<T>(args...);
		previousCmd->initialCmd(keyMode);
	}

	void processCmdHeld() {
		KEY_MODE keyMode = module->keyTempHeld->mode;
		if (previousCmd) {
			bool shouldClear = previousCmd->followUpCmd(keyMode);
			if (shouldClear) {
				delete previousCmd;
				previousCmd = NULL;
			}
		}
	}

	void processCmdDisable() {
		KEY_MODE keyMode = module->keyTempDisable->mode;
		if (previousCmd) {
			bool shouldClear = previousCmd->followUpCmd(keyMode);
			if (shouldClear) {
				delete previousCmd;
				previousCmd = NULL;
			}
		}
	}

	void draw(const DrawArgs& args) override {
		if (module && module->keyTemp != NULL) {
			switch (module->keyTemp->mode) {
				case KEY_MODE::S_PARAM_RAND:
					processCmd<CmdParamRand>(); break;
				case KEY_MODE::S_PARAM_COPY:
					processCmd<CmdParamCopyPaste>(); break;
				case KEY_MODE::S_PARAM_PASTE:
					processCmd<CmdParamCopyPaste>(); break;
				case KEY_MODE::S_ZOOM_MODULE_90:
					processCmd<CmdZoomModule>(&CmdZoomModule::scale, 0.9f); break;
				case KEY_MODE::S_ZOOM_MODULE_90_SMOOTH:
					processCmd<CmdZoomModuleSmooth>(&CmdZoomModuleSmooth::scale, 0.95f); break;
				case KEY_MODE::S_ZOOM_MODULE_30:
					processCmd<CmdZoomModule>(&CmdZoomModule::scale, 0.3f); break;
				case KEY_MODE::S_ZOOM_MODULE_30_SMOOTH:
					processCmd<CmdZoomModuleSmooth>(&CmdZoomModuleSmooth::scale, 0.3f); break;
				case KEY_MODE::S_ZOOM_MODULE_CUSTOM:
					processCmd<CmdZoomModuleCustom>(&CmdZoomModuleCustom::data, &module->keyTemp->data); break;
				case KEY_MODE::S_ZOOM_MODULE_CUSTOM_SMOOTH:
					processCmd<CmdZoomModuleCustomSmooth>(&CmdZoomModuleCustomSmooth::data, &module->keyTemp->data); break;
				case KEY_MODE::S_ZOOM_MODULE_ID:
					processCmd<CmdZoomModuleId>(&CmdZoomModuleId::data, &module->keyTemp->data, &CmdZoomModuleId::scale, 0.9f); break;
				case KEY_MODE::S_ZOOM_MODULE_ID_SMOOTH:
					processCmd<CmdZoomModuleIdSmooth>(&CmdZoomModuleIdSmooth::data, &module->keyTemp->data, &CmdZoomModuleIdSmooth::scale, 0.95f); break;
				case KEY_MODE::S_ZOOM_OUT:
					processCmd<CmdZoomOut>(); break;
				case KEY_MODE::S_ZOOM_OUT_SMOOTH:
					processCmd<CmdZoomOutSmooth>(); break;
				case KEY_MODE::S_ZOOM_TOGGLE:
					processCmd<CmdZoomToggle>(); break;
				case KEY_MODE::S_ZOOM_TOGGLE_SMOOTH:
					processCmd<CmdZoomToggleSmooth>(&CmdZoomToggleSmooth::scale, 0.95f); break;
				case KEY_MODE::S_CABLE_OPACITY:
					processCmd<CmdCableOpacity>(&CmdCableOpacity::data, &module->keyTemp->data); break;
				case KEY_MODE::S_CABLE_COLOR_NEXT:
					processCmd<CmdCableColorNext>(); break;
				case KEY_MODE::S_CABLE_COLOR:
					processCmd<CmdCableColor>(&CmdCableColor::data, &module->keyTemp->data); break;
				case KEY_MODE::S_CABLE_ROTATE:
					processCmd<CmdCableRotate>(); break;
				case KEY_MODE::S_CABLE_VISIBILITY:
					processCmd<CmdCableVisibility>(); break;
				case KEY_MODE::S_CABLE_MULTIDRAG:
					//processCmd<CmdCableMultiDrag>(); 
					break;
				case KEY_MODE::S_FRAMERATE:
					//processCmd<CmdFramerate>();
					break;
				case KEY_MODE::S_MODULE_LOCK:
					processCmd<CmdModuleLock>(); break;
				case KEY_MODE::S_MODULE_ADD:
					processCmd<CmdModuleAdd>(&CmdModuleAdd::data, &module->keyTemp->data); break;
				case KEY_MODE::S_MODULE_ADD_RANDOM:
					processCmd<CmdModuleAddRandom>(); break;
				case KEY_MODE::S_MODULE_DISPATCH:
					processCmd<CmdModuleDispatch>(&CmdModuleDispatch::data, &module->keyTemp->data); break;
				case KEY_MODE::S_MODULE_PRESET_SAVE:
					processCmd<CmdModulePresetSave>(); break;
				case KEY_MODE::S_MODULE_PRESET_SAVE_DEFAULT:
					processCmd<CmdModulePresetSaveDefault>(); break;
				case KEY_MODE::S_SCROLL_LEFT:
					processCmd<CmdRackMove>(&CmdRackMove::x, -1.f, &CmdRackMove::y, 0.f); break;
				case KEY_MODE::S_SCROLL_RIGHT:
					processCmd<CmdRackMove>(&CmdRackMove::x, 1.f, &CmdRackMove::y, 0.f); break;
				case KEY_MODE::S_SCROLL_UP:
					processCmd<CmdRackMove>(&CmdRackMove::x, 0.f, &CmdRackMove::y, -1.f); break;
				case KEY_MODE::S_SCROLL_DOWN:
					processCmd<CmdRackMove>(&CmdRackMove::x, 0.f, &CmdRackMove::y, 1.f); break;
				case KEY_MODE::S_BUSBOARD:
					//if (!cmdBusboard) cmdBusboard = new CmdBusboard;
					//cmdBusboard->process();
					break;
				default:
					break;
			}
			module->keyTemp = NULL;
		}

		if (module && module->keyTempHeld != NULL) {
			switch (module->keyTempHeld->mode) {
				case KEY_MODE::S_SCROLL_LEFT:
				case KEY_MODE::S_SCROLL_RIGHT:
				case KEY_MODE::S_SCROLL_UP:
				case KEY_MODE::S_SCROLL_DOWN:
					processCmdHeld(); break;
				default:
					break;
			}
			module->keyTempHeld = NULL;
		}

		if (module && module->keyTempDisable != NULL) {
			switch (module->keyTempDisable->mode) {
				case KEY_MODE::S_CABLE_MULTIDRAG:
					processCmdDisable(); break;
				default:
					break;
			}
			module->keyTempDisable = NULL;
		}
		if (previousCmd) {
			previousCmd->step();
		}
	}

	void onButton(const event::Button& e) override {
		if (module && !module->isBypassed() && (e.button > 2 || (e.mods & (GLFW_MOD_ALT | GLFW_MOD_CONTROL | GLFW_MOD_SHIFT))) != 0) {
			int e_mods = e.mods & (GLFW_MOD_ALT | GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);

			if (e.action == GLFW_PRESS) {
				if (learnIdx >= 0) {
					module->keys[learnIdx].button = e.button;
					module->keys[learnIdx].key = -1;
					module->keys[learnIdx].mods = e_mods;
					learnIdx = -1;
					e.consume(this);
				}
				else {
					for (int i = 0; i < PORTS; i++) {
						if (e.button == module->keys[i].button && e_mods == module->keys[i].mods) {
							module->keyEnable(i);
							// Do not consume mouse events for buttons 0/1/2
							if (e.button > 2) e.consume(this);
						}
					}
				}
			}
			if (e.action == RACK_HELD) {
				for (int i = 0; i < PORTS; i++) {
					if (e.button == module->keys[i].button && e_mods == module->keys[i].mods) {
						// Do not consume mouse events for buttons 0/1/2
						if (e.button > 2) e.consume(this);
					}
				}
			}
			if (e.action == GLFW_RELEASE) {
				for (int i = 0; i < PORTS; i++) {
					if (e.button == module->keys[i].button) {
						module->keyDisable(i);
						// Do not consume mouse events for buttons 0/1/2
						if (e.button > 2) e.consume(this);
					}
				}
			}
		}
		Widget::onButton(e);
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (module && !module->isBypassed()) {
			int e_mods = e.mods & (GLFW_MOD_ALT | GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
			int e_key = keyFix(e.key);

			if (e.action == GLFW_PRESS) {
				if (learnCallback) {
					std::string kn = keyName(e_key);
					if (!kn.empty()) {
						learnCallback(e_key, e.scancode, e_mods);
						learnCallback = { };
						learnIdx = -1;
						e.consume(this);
					}
				}
				else if (learnIdx >= 0) {
					std::string kn = keyName(e_key);
					if (!kn.empty()) {
						module->keys[learnIdx].button = -1;
						module->keys[learnIdx].key = e_key;
						module->keys[learnIdx].mods = e_mods;
						learnIdx = -1;
						e.consume(this);
					}
				}
				else {
					for (int i = 0; i < PORTS; i++) {
						if (e_key == module->keys[i].key && e_mods == module->keys[i].mods) {
							module->keyEnable(i);
							e.consume(this);
						}
					}
				}
			}
			if (e.action == RACK_HELD) {
				for (int i = 0; i < PORTS; i++) {
					if (e_key == module->keys[i].key && e_mods == module->keys[i].mods) {
						module->keyHeld(i);
						e.consume(this);
					}
				}
			}
			if (e.action == GLFW_RELEASE) {
				for (int i = 0; i < PORTS; i++) {
					if (e_key == module->keys[i].key) {
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

	void enableLearn(int idx, std::function<void(int,int,int)> callback) {
		learnIdx = idx;
		learnCallback = callback;
	}
};



template<int PORTS>
struct KeyDisplay : StoermelderLedDisplay {
	KeyContainer<PORTS>* keyContainer;
	StrokeModule<PORTS>* module;
	int idx;

	ui::Tooltip* tooltip = NULL;

	void step() override {
		if (keyContainer && keyContainer->learnIdx == idx) {
			color = keyContainer->learnCallback || keyContainer->learnIdxEx == idx ? color::RED : nvgRGBA(0xef, 0xef, 0xef, 0xa0);
			text = "<LRN>";
			module->lights[StrokeModule<PORTS>::LIGHT_ALT + idx].setBrightness(0.1f);
			module->lights[StrokeModule<PORTS>::LIGHT_CTRL + idx].setBrightness(0.1f);
			module->lights[StrokeModule<PORTS>::LIGHT_SHIFT + idx].setBrightness(0.1f);
		}
		else if (module) {
			color = nvgRGBA(0xef, 0xef, 0xef, 0xff);
			text = module->keys[idx].key >= 0 ? keyName(module->keys[idx].key) : module->keys[idx].button >= 0 ? string::f("MB %i", module->keys[idx].button + 1) : "";
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
			KeyContainer<PORTS>* keyContainer;
			int idx;
			void step() override {
				rightText = 
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_90 ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_90_SMOOTH ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_30 ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_30_SMOOTH ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_CUSTOM ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_CUSTOM_SMOOTH ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_OUT ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_OUT_SMOOTH ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_TOGGLE ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_TOGGLE_SMOOTH ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_ID ||
					module->keys[idx].mode == KEY_MODE::S_ZOOM_MODULE_ID_SMOOTH ||
					module->keys[idx].mode == KEY_MODE::S_SCROLL_LEFT ||
					module->keys[idx].mode == KEY_MODE::S_SCROLL_RIGHT ||
					module->keys[idx].mode == KEY_MODE::S_SCROLL_UP ||
					module->keys[idx].mode == KEY_MODE::S_SCROLL_DOWN
						? "✔" : RIGHT_ARROW;
				MenuItem::step();
			}

			Menu* createChildMenu() override {
				struct ZoomModuleCustomItem : MenuItem {
					StrokeModule<PORTS>* module;
					KEY_MODE mode;
					int idx;
					void step() override {
						rightText = module->keys[idx].mode == mode ? "✔ " RIGHT_ARROW : "";
						MenuItem::step();
					}
					void onAction(const event::Action& e) override {
						module->keys[idx].mode = mode;
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

						if (module->keys[idx].mode == mode) {
							Menu* menu = new Menu;
							menu->addChild(new ZoomModuleSlider(module, idx));
							return menu;
						}
						return NULL;
					}
				};

				struct ZoomModuleIdItem : ModeMenuItem {
					KeyContainer<PORTS>* keyContainer;
					void onAction(const event::Action& e) override {
						ModeMenuItem::module->keys[ModeMenuItem::idx].mode = ModeMenuItem::mode;
						ModeMenuItem::module->keys[ModeMenuItem::idx].high = false;
						ModeMenuItem::module->keys[ModeMenuItem::idx].data = "";
					}

					Menu* createChildMenu() override {
						struct LearnItem : MenuItem {
							KeyContainer<PORTS>* keyContainer;
							int idx;
							void onAction(const event::Action& e) override {
								keyContainer->learnIdx = keyContainer->learnIdxEx = idx;
								keyContainer->module->keys[idx].data = "";
								KeyContainer<PORTS>* _keyContainer = keyContainer;
								std::string* _data = &keyContainer->module->keys[idx].data;
								auto callback = [_keyContainer,_data](ModuleWidget* mw, Vec pos) {
									*_data = string::f("%lld", (long long)mw->module->getId());
									_keyContainer->learnIdx = _keyContainer->learnIdxEx = -1;
								};
								keyContainer->moduleSelectProcessor.startLearn(callback);
							}
						};
	
						if (ModeMenuItem::module->keys[ModeMenuItem::idx].mode == ModeMenuItem::mode) {
							Menu* menu = new Menu;
							LearnItem* learnItem = construct<LearnItem>(&MenuItem::text, "Learn module", &LearnItem::keyContainer, keyContainer, &LearnItem::idx, ModeMenuItem::idx);
							menu->addChild(learnItem);

							if (ModeMenuItem::module->keys[ModeMenuItem::idx].data != "") {
								int64_t moduleId = std::stoll(ModeMenuItem::module->keys[ModeMenuItem::idx].data);
								ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
								if (mw) {
									std::string name = mw->model->plugin->brand + " " + mw->module->model->name;
									menu->addChild(new MenuSeparator);
									menu->addChild(construct<MenuLabel>(&MenuLabel::text, name));
									menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("ID %lld", (long long)mw->module->getId())));
								}
							}

							return menu;
						}
						return NULL;
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom to module", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_90));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom to module (smooth)", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_90_SMOOTH));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom to module 1/3", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_30));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom to module 1/3 (smooth)", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_30_SMOOTH));
				menu->addChild(construct<ZoomModuleCustomItem>(&MenuItem::text, "Zoom level to module", &ZoomModuleCustomItem::module, module, &ZoomModuleCustomItem::idx, idx, &ZoomModuleCustomItem::mode, KEY_MODE::S_ZOOM_MODULE_CUSTOM));
				menu->addChild(construct<ZoomModuleCustomItem>(&MenuItem::text, "Zoom level to module (smooth)", &ZoomModuleCustomItem::module, module, &ZoomModuleCustomItem::idx, idx, &ZoomModuleCustomItem::mode, KEY_MODE::S_ZOOM_MODULE_CUSTOM_SMOOTH));
				menu->addChild(construct<ZoomModuleIdItem>(&MenuItem::text, "Zoom to specific module", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_ID, &ZoomModuleIdItem::keyContainer, keyContainer));
				menu->addChild(construct<ZoomModuleIdItem>(&MenuItem::text, "Zoom to specific module (smooth)", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_MODULE_ID_SMOOTH, &ZoomModuleIdItem::keyContainer, keyContainer));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom out", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_OUT));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom out (smooth)", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_OUT_SMOOTH));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom toggle", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_TOGGLE));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Zoom toggle (smooth)", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ZOOM_TOGGLE_SMOOTH));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Scroll left", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_SCROLL_LEFT));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Scroll right", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_SCROLL_RIGHT));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Scroll up", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_SCROLL_UP));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Scroll down", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_SCROLL_DOWN));
				return menu;
			}
		}; // struct ViewMenuItem

		struct ModuleMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			KeyContainer<PORTS>* keyContainer;
			int idx;
			void step() override {
				rightText = 
					module->keys[idx].mode == KEY_MODE::S_MODULE_ADD || 
					module->keys[idx].mode == KEY_MODE::S_MODULE_ADD_RANDOM ||
					module->keys[idx].mode == KEY_MODE::S_MODULE_DISPATCH ||
					module->keys[idx].mode == KEY_MODE::S_MODULE_PRESET_SAVE || 
					module->keys[idx].mode == KEY_MODE::S_MODULE_PRESET_SAVE_DEFAULT
						? "✔" : RIGHT_ARROW;
				MenuItem::step();
			}

			Menu* createChildMenu() override {
				struct ModuleAddItem : ModeMenuItem {
					KeyContainer<PORTS>* keyContainer;
					void step() override {
						ModeMenuItem::rightText = ModeMenuItem::module->keys[ModeMenuItem::idx].mode == KEY_MODE::S_MODULE_ADD ? "✔ " RIGHT_ARROW : "";
						ModeMenuItem::step();
					}
					void onAction(const event::Action& e) override {
						ModeMenuItem::module->keys[ModeMenuItem::idx].mode = KEY_MODE::S_MODULE_ADD;
						ModeMenuItem::module->keys[ModeMenuItem::idx].high = false;
						ModeMenuItem::module->keys[ModeMenuItem::idx].data = "";
					}

					Menu* createChildMenu() override {
						struct MenuAddLearnItem : MenuItem {
							KeyContainer<PORTS>* keyContainer;
							int idx;
							void onAction(const event::Action& e) override {
								keyContainer->learnIdx = keyContainer->learnIdxEx = idx;
								keyContainer->module->keys[idx].data = "";
								KeyContainer<PORTS>* _keyContainer = keyContainer;
								std::string* _data = &keyContainer->module->keys[idx].data;
								auto callback = [_keyContainer,_data](ModuleWidget* mw, Vec pos) {
									json_t* oJ = json_object();
									json_object_set_new(oJ, "name", json_string((mw->model->plugin->brand + " " + mw->module->model->name).c_str()));
									json_object_set_new(oJ, "module", mw->toJson());
									*_data = json_dumps(oJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
									json_decref(oJ);
									_keyContainer->learnIdx = _keyContainer->learnIdxEx = -1;
								};
								keyContainer->moduleSelectProcessor.startLearn(callback);
							}
						};
	
						if (ModeMenuItem::module->keys[ModeMenuItem::idx].mode == KEY_MODE::S_MODULE_ADD) {
							Menu* menu = new Menu;
							MenuAddLearnItem* learnItem = construct<MenuAddLearnItem>(&MenuItem::text, "Learn module", &MenuAddLearnItem::keyContainer, keyContainer, &MenuAddLearnItem::idx, ModeMenuItem::idx);
							menu->addChild(learnItem);

							if (ModeMenuItem::module->keys[ModeMenuItem::idx].data != "") {
								json_error_t error;
								json_t* oJ = json_loads(ModeMenuItem::module->keys[ModeMenuItem::idx].data.c_str(), 0, &error);
								std::string name = json_string_value(json_object_get(oJ, "name"));
								menu->addChild(new MenuSeparator);
								menu->addChild(construct<MenuLabel>(&MenuLabel::text, name));
								json_decref(oJ);
							}

							return menu;
						}
						return NULL;
					}
				};

				struct ModuleDispatchItem : ModeMenuItem {
					KeyContainer<PORTS>* keyContainer;
					void step() override {
						ModeMenuItem::rightText = ModeMenuItem::module->keys[ModeMenuItem::idx].mode == KEY_MODE::S_MODULE_DISPATCH ? "✔ " RIGHT_ARROW : "";
						ModeMenuItem::step();
					}
					void onAction(const event::Action& e) override {
						ModeMenuItem::module->keys[ModeMenuItem::idx].mode = KEY_MODE::S_MODULE_DISPATCH;
						ModeMenuItem::module->keys[ModeMenuItem::idx].high = false;
						ModeMenuItem::module->keys[ModeMenuItem::idx].data = "";
					}

					Menu* createChildMenu() override {
						struct DispatchLearnItem : MenuItem {
							KeyContainer<PORTS>* keyContainer;
							int idx;
							void onAction(const event::Action& e) override {
								keyContainer->learnIdx = keyContainer->learnIdxEx = idx;
								keyContainer->module->keys[idx].data = "";
								KeyContainer<PORTS>* _keyContainer = keyContainer;
								std::string* _data = &keyContainer->module->keys[idx].data;
								auto callback = [_keyContainer,_data](ModuleWidget* mw, Vec pos) {
									json_t* oJ = json_object();
									json_object_set_new(oJ, "name", json_string((mw->model->plugin->brand + " " + mw->module->model->name).c_str()));
									json_object_set_new(oJ, "moduleId", json_integer(mw->module->id));
									json_object_set_new(oJ, "x", json_real(pos.x));
									json_object_set_new(oJ, "y", json_real(pos.y));
									*_data = json_dumps(oJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
									json_decref(oJ);
									_keyContainer->learnIdx = _keyContainer->learnIdxEx = -1;
								};
								keyContainer->moduleSelectProcessor.startLearn(callback);
							}
						};

						struct DispatchLearnKeyItem : MenuItem {
							KeyContainer<PORTS>* keyContainer;
							int idx;
							void onAction(const event::Action& e) override {
								std::string* _data = &keyContainer->module->keys[idx].data;
								if (*_data == "") return;
								auto callback = [_data](int key, int scancode, int mods) {
									json_error_t error;
									json_t* oJ = json_loads(_data->c_str(), 0, &error);
									json_object_set_new(oJ, "key", json_integer(key));
									json_object_set_new(oJ, "scancode", json_integer(scancode));
									json_object_set_new(oJ, "mods", json_integer(mods));
									*_data = json_dumps(oJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
									json_decref(oJ);
								};
								keyContainer->enableLearn(idx, callback);
							}
						};
	
						if (ModeMenuItem::module->keys[ModeMenuItem::idx].mode == KEY_MODE::S_MODULE_DISPATCH) {
							Menu* menu = new Menu;
							menu->addChild(construct<DispatchLearnItem>(&MenuItem::text, "Learn module", &DispatchLearnItem::keyContainer, keyContainer, &DispatchLearnItem::idx, ModeMenuItem::idx));
							menu->addChild(construct<DispatchLearnKeyItem>(&MenuItem::text, "Learn hotkey", &DispatchLearnKeyItem::keyContainer, keyContainer, &DispatchLearnKeyItem::idx, ModeMenuItem::idx));

							if (ModeMenuItem::module->keys[ModeMenuItem::idx].data != "") {
								json_error_t error;
								json_t* oJ = json_loads(ModeMenuItem::module->keys[ModeMenuItem::idx].data.c_str(), 0, &error);
								std::string name = json_string_value(json_object_get(oJ, "name"));
								menu->addChild(new MenuSeparator);
								menu->addChild(construct<MenuLabel>(&MenuLabel::text, name));

								json_t* keyJ = json_object_get(oJ, "key");
								json_t* modsJ = json_object_get(oJ, "mods");
								if (keyJ) {
									std::string key = keyName(json_integer_value(keyJ));
									int mods = json_integer_value(modsJ);
									std::string alt = mods & GLFW_MOD_ALT ? RACK_MOD_ALT_NAME "+" : "";
									std::string ctrl = mods & GLFW_MOD_CONTROL ? RACK_MOD_CTRL_NAME "+" : "";
									std::string shift = mods & GLFW_MOD_SHIFT ? RACK_MOD_SHIFT_NAME "+" : "";
									std::string s = string::f("Hotkey: %s%s%s%s", alt.c_str(), ctrl.c_str(), shift.c_str(), key.c_str());
									menu->addChild(construct<MenuLabel>(&MenuLabel::text, s));
								}

								json_decref(oJ);
							}

							return menu;
						}
						return NULL;
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<ModuleAddItem>(&MenuItem::text, "Add module", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MODULE_ADD, &ModuleAddItem::keyContainer, keyContainer));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Add random module", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MODULE_ADD_RANDOM));
				menu->addChild(construct<ModuleDispatchItem>(&MenuItem::text, "Send hotkey to module (experimental)", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MODULE_DISPATCH, &ModuleDispatchItem::keyContainer, keyContainer));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Save preset", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MODULE_PRESET_SAVE));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Save default preset", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MODULE_PRESET_SAVE_DEFAULT));
				return menu;
			}
		}; // struct ModuleMenuItem

		struct CableMenuItem : MenuItem {
			StrokeModule<PORTS>* module;
			int idx;
			void step() override {
				rightText = 
					module->keys[idx].mode == KEY_MODE::S_CABLE_OPACITY ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_VISIBILITY ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR_NEXT ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR ||
					module->keys[idx].mode == KEY_MODE::S_CABLE_ROTATE || 
					module->keys[idx].mode == KEY_MODE::S_CABLE_MULTIDRAG
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
					NVGcolor color;
					bool firstRun = true;

					void step() override {
						if (module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR) {
							if (firstRun) {
								color = color::fromHexString(module->keys[idx].data);
								firstRun = false;
							}
							module->keys[idx].data = color::toHexString(color);
							rightText = "✔ " RIGHT_ARROW;
						}
						MenuItem::step();
					}

					void onAction(const event::Action& e) override {
						if (module->keys[idx].mode != KEY_MODE::S_CABLE_COLOR) {
							module->keys[idx].mode = KEY_MODE::S_CABLE_COLOR;
							module->keys[idx].high = false;
							module->keys[idx].data = color::toHexString(color::BLACK);
						}
					}

					Menu* createChildMenu() override {
						if (module->keys[idx].mode == KEY_MODE::S_CABLE_COLOR) {
							
							Menu* menu = new Menu;
							menu->addChild(construct<MenuColorLabel>(&MenuColorLabel::fillColor, &color));
							menu->addChild(new MenuSeparator);
							menu->addChild(construct<MenuColorPicker>(&MenuColorPicker::color, &color));
							menu->addChild(new MenuSeparator);
							menu->addChild(construct<MenuColorField>(&MenuColorField::color, &color));
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
				//menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Multi-drag (for mouse-buttons)", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_CABLE_MULTIDRAG));
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
				//menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle framerate display", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_FRAMERATE));
				//menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle busboard", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_BUSBOARD));
				//menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle engine pause", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_ENGINE_PAUSE));
				menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle lock modules", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::S_MODULE_LOCK));
				return menu;
			}
		}; // struct SpecialMenuItem

		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Hotkey %i", idx + 1)));
		menu->addChild(createMenuItem("Learn", "", [=]() { keyContainer->enableLearn(idx); }));
		menu->addChild(createMenuItem("Clear", "",
			[=]() { 
				module->keys[idx].button = -1;
				module->keys[idx].key = -1;
				module->keys[idx].mods = 0; 
			}
		));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Off", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::OFF));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "CV output"));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Trigger", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::CV_TRIGGER));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Gate", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::CV_GATE));
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Toggle", &ModeMenuItem::module, module, &ModeMenuItem::idx, idx, &ModeMenuItem::mode, KEY_MODE::CV_TOGGLE));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Commands"));
		menu->addChild(construct<ViewMenuItem>(&MenuItem::text, "View", &ViewMenuItem::module, module, &ViewMenuItem::idx, idx, &ViewMenuItem::keyContainer, keyContainer));
		menu->addChild(construct<ParamMenuItem>(&MenuItem::text, "Parameters", &ParamMenuItem::module, module, &ParamMenuItem::idx, idx));
		menu->addChild(construct<ModuleMenuItem>(&MenuItem::text, "Modules", &ModuleMenuItem::module, module, &ModuleMenuItem::idx, idx, &ModuleMenuItem::keyContainer, keyContainer));
		menu->addChild(construct<CableMenuItem>(&MenuItem::text, "Cables", &CableMenuItem::module, module, &CableMenuItem::idx, idx));
		menu->addChild(construct<SpecialMenuItem>(&MenuItem::text, "Special", &SpecialMenuItem::module, module, &SpecialMenuItem::idx, idx));
		keyContainer->moduleSelectProcessor.setOwner(this);
	}

	void onHover(const event::Hover& e) override {
		Widget::onHover(e);
		e.stopPropagating();
		// Consume if not consumed by child
		if (!e.isConsumed())
			e.consume(this);
	}

	void onEnter(const event::Enter& e) override {
		struct KeyDisplayTooltip : ui::Tooltip {
			StrokeModule<PORTS>* module;
			KeyDisplay* keyDisplay;

			void step() override {
				switch (module->keys[keyDisplay->idx].mode) {
					case KEY_MODE::OFF:
						text = "Off"; break;
					case KEY_MODE::CV_TRIGGER:
						text = "CV: Trigger"; break;
					case KEY_MODE::CV_GATE:
						text = "CV: Gate"; break;
					case KEY_MODE::CV_TOGGLE:
						text = "CV: Toggle"; break;
					case KEY_MODE::S_PARAM_RAND:
						text = "Parameter: Randomize"; break;
					case KEY_MODE::S_PARAM_COPY:
						text = "Parameter: Value copy"; break;
					case KEY_MODE::S_PARAM_PASTE:
						text = "Parameter: Value paste"; break;
					case KEY_MODE::S_ZOOM_MODULE_90:
						text = "View: Zoom to module"; break;
					case KEY_MODE::S_ZOOM_MODULE_90_SMOOTH:
						text = "View: Zoom to module (smooth)"; break;
					case KEY_MODE::S_ZOOM_MODULE_30:
						text = "View: Zoom to module 1/3"; break;
					case KEY_MODE::S_ZOOM_MODULE_30_SMOOTH:
						text = "View: Zoom to module 1/3 (smooth)"; break;
					case KEY_MODE::S_ZOOM_MODULE_CUSTOM:
						text = "View: Zoom level to module"; break;
					case KEY_MODE::S_ZOOM_MODULE_CUSTOM_SMOOTH:
						text = "View: Zoom level to module (smooth)"; break;
					case KEY_MODE::S_ZOOM_MODULE_ID:
						text = "View: Zoom to specific module"; break;
					case KEY_MODE::S_ZOOM_MODULE_ID_SMOOTH:
						text = "View: Zoom to specific module (smooth)"; break;
					case KEY_MODE::S_ZOOM_OUT:
						text = "View: Zoom out"; break;
					case KEY_MODE::S_ZOOM_OUT_SMOOTH:
						text = "View: Zoom out (smooth)"; break;
					case KEY_MODE::S_ZOOM_TOGGLE:
						text = "View: Zoom toggle"; break;
					case KEY_MODE::S_ZOOM_TOGGLE_SMOOTH:
						text = "View: Zoom toggle (smooth)"; break;
					case KEY_MODE::S_CABLE_OPACITY:
						text = "Cable: Toggle opacity"; break;
					case KEY_MODE::S_CABLE_COLOR_NEXT:
						text = "Cable: Next color"; break;
					case KEY_MODE::S_CABLE_COLOR:
						text = "Cable: Color"; break;
					case KEY_MODE::S_CABLE_ROTATE:
						text = "Cable: Rotate ordering"; break;
					case KEY_MODE::S_CABLE_VISIBILITY:
						text = "Cable: Toggle visibility"; break;
					case KEY_MODE::S_CABLE_MULTIDRAG:
						break;
					case KEY_MODE::S_FRAMERATE:
						text = "Toggle framerate display"; break;
					case KEY_MODE::S_ENGINE_PAUSE:
						text = "Toggle engine pause"; break;
					case KEY_MODE::S_MODULE_LOCK:
						text = "Toggle lock modules"; break;
					case KEY_MODE::S_MODULE_ADD:
						text = "Module: Add"; break;
					case KEY_MODE::S_MODULE_ADD_RANDOM:
						text = "Module: Add random"; break;
					case KEY_MODE::S_MODULE_DISPATCH:
						text = "Module: Send hotkey"; break;
					case KEY_MODE::S_MODULE_PRESET_SAVE:
						text = "Module: Save preset"; break;
					case KEY_MODE::S_MODULE_PRESET_SAVE_DEFAULT:
						text = "Module: Save default preset"; break;
					case KEY_MODE::S_SCROLL_LEFT:
						text = "Scroll left"; break;
					case KEY_MODE::S_SCROLL_RIGHT:
						text = "Scroll right"; break;
					case KEY_MODE::S_SCROLL_UP:
						text = "Scroll up"; break;
					case KEY_MODE::S_SCROLL_DOWN:
						text = "Scroll down"; break;
					case KEY_MODE::S_BUSBOARD:
						text = "Toggle busboard"; break;
				}

				Tooltip::step();
				// Position at bottom-right of parameter
				box.pos = keyDisplay->getAbsoluteOffset(keyDisplay->box.size).round();
				// Fit inside parent (copied from Tooltip.cpp)
				assert(parent);
				box = box.nudge(parent->box.zeroPos());
			}
		};

		if (settings::tooltips && !tooltip && module->keys[idx].isMapped()) {
			KeyDisplayTooltip* keyDisplayTooltip = new KeyDisplayTooltip;
			keyDisplayTooltip->module = module;
			keyDisplayTooltip->keyDisplay = this;
			APP->scene->addChild(keyDisplayTooltip);
			tooltip = keyDisplayTooltip;
		}
	}

	void onLeave(const event::Leave& e) override {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
			tooltip = NULL;
		}
	}

	void onDeselect(const event::Deselect& e) override {
		StoermelderLedDisplay::onDeselect(e);
		keyContainer->moduleSelectProcessor.processDeselect();
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