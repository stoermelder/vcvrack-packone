#include "plugin.hpp"
#include "ui/ViewportHelper.hpp"
#include "ui/ModuleSelectProcessor.hpp"

namespace StoermelderPackOne {
namespace Goto {

enum class TRIGGERMODE {
	POLYTRIGGER = 0,
	C5 = 1
};

enum class JUMPPOS {
	ABSOLUTE = 0,	// deprecated, not supported anymore
	MODULE_CENTER = 1,
	MODULE_TOPLEFT = 2
};

struct GotoTarget {
	std::vector<int64_t> moduleIds;
	float zoom = 1.f;
};

template <int SLOTS>
struct GotoModule : Module {
	enum ParamIds {
		ENUMS(PARAM_SLOT, SLOTS),
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_TRIG,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_SLOT, SLOTS * 3),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	TRIGGERMODE triggerMode;

	/** [Stored to JSON] */
	GotoTarget jumpPoints[SLOTS];
	/** [Stored to JSON] */
	bool smoothTransition;
	/** [Stored to JSON] */
	JUMPPOS jumpPos;
	/** [Stored to JSON] */
	bool ignoreZoom;

	dsp::SchmittTrigger trigger[SLOTS];
	/** Helper-variable for pushing a triggered slot towards the widget/gui-thread */
	int jumpTrigger = -1;
	/** Helper-variable for pushing a connected cable towards the widget/gui-thread */
	bool jumpTriggerUsed = false;
	/** Helper-variable for requesting a reset of the widget */
	bool resetRequested = false;

	/** Stores the last voltage seen on the input-port */
	float triggerVoltage;


	struct GotoSwitchQuantity : SwitchQuantity {
		int jumpPoint;

		std::string getString() override {
			GotoModule<SLOTS>* module = reinterpret_cast<GotoModule<SLOTS>*>(this->module);
			if (module->jumpPoints[jumpPoint].moduleIds.size() > 0) {
				return string::f("Jump point %i (SHIFT+%i): %i module(s)", jumpPoint + 1, (jumpPoint + 1) % 10, module->jumpPoints[jumpPoint].moduleIds.size());
			}
			else {
				return string::f("Jump point %i (SHIFT+%i)", jumpPoint + 1, (jumpPoint + 1) % 10);
			}
		}
	};


	GotoModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_TRIG, "Jump point trigger");
		inputInfos[INPUT_TRIG]->description = "Operating mode is set on the context menu.";
		for (int i = 0; i < SLOTS; i++) {
			auto pq = configSwitch<GotoSwitchQuantity>(PARAM_SLOT + i, 0.f, 1.f, 0.f);
			pq->description = "Short-press to jump\nLong-press to learn/clear";
			pq->jumpPoint = i;
		}
		onReset();
	}

	void onReset() override {
		Module::onReset();
		triggerMode = TRIGGERMODE::POLYTRIGGER;
		triggerVoltage = 0.f;
		smoothTransition = false;
		jumpPos = JUMPPOS::MODULE_CENTER;
		ignoreZoom = false;
		for (int i = 0; i < SLOTS; i++) {
			jumpPoints[i].moduleIds.clear();
		}
		resetRequested = true;
	}

	void process(const ProcessArgs& args) override {
		jumpTriggerUsed = inputs[INPUT_TRIG].isConnected();
		if (jumpTriggerUsed) {
			switch (triggerMode) {
				case TRIGGERMODE::POLYTRIGGER: {
					for (int i = 0; i < SLOTS; i++) {
						if (trigger[i].process(inputs[INPUT_TRIG].getVoltage(i))) {
							jumpTrigger = i;
						}
					}
					break;
				}
				case TRIGGERMODE::C5: {
					float v = inputs[INPUT_TRIG].getVoltage();
					if (v != 0.f && triggerVoltage != v) {
						triggerVoltage = v;
						float t = (triggerVoltage - 1.f) * 12.f;
						if (t >= 0 && t <= (SLOTS - 1)) {
							jumpTrigger = std::round(t);
						}
					}
					break;
				}
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "triggerMode", json_integer((int)triggerMode));

		json_object_set_new(rootJ, "smoothTransition", json_boolean(smoothTransition));
		json_object_set_new(rootJ, "centerModule", json_integer((int)jumpPos));
		json_object_set_new(rootJ, "ignoreZoom", json_boolean(ignoreZoom));

		json_t* jumpPointsJ = json_array();
		for (GotoTarget jp : jumpPoints) {
			json_t* jumpPointJ = json_object();
			json_t* moduleIdsJ = json_array();
			for (int64_t moduleId : jp.moduleIds) json_array_append_new(moduleIdsJ, json_integer(moduleId));
			json_object_set_new(jumpPointJ, "moduleIds", moduleIdsJ);
			json_object_set_new(jumpPointJ, "zoom", json_real(jp.zoom));
			json_array_append_new(jumpPointsJ, jumpPointJ);
		}
		json_object_set_new(rootJ, "jumpPoints", jumpPointsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		triggerMode = (TRIGGERMODE)json_integer_value(json_object_get(rootJ, "triggerMode"));

		smoothTransition = json_boolean_value(json_object_get(rootJ, "smoothTransition"));
		jumpPos = (JUMPPOS)json_integer_value(json_object_get(rootJ, "centerModule"));
		ignoreZoom = json_boolean_value(json_object_get(rootJ, "ignoreZoom"));

		json_t* jumpPointsJ = json_object_get(rootJ, "jumpPoints");
		for (int i = 0; i < 10; i++) {
			json_t* jumpPointJ = json_array_get(jumpPointsJ, i);
			json_t* moduleIdJ = json_object_get(jumpPointJ, "moduleId");
			if (moduleIdJ) {
				jumpPoints[i].moduleIds.push_back(json_integer_value(moduleIdJ));
			}
			else {
				json_t* moduleIdsJ = json_object_get(jumpPointJ, "moduleIds");
				size_t j;
				jumpPoints[i].moduleIds.clear();
				json_array_foreach(moduleIdsJ, j, moduleIdJ) {
					jumpPoints[i].moduleIds.push_back(json_integer_value(moduleIdJ));
				}
			}
			
			jumpPoints[i].zoom = json_real_value(json_object_get(jumpPointJ, "zoom"));
		}
	}
};



template <int SLOTS>
struct GotoContainer : widget::Widget {
	GotoModule<SLOTS>* module;
	ModuleWidget* mw;

	StoermelderPackOne::Rack::ViewportCenterSmooth viewportCenterSmooth;
	dsp::ClockDivider divider;
	int learnJumpPoint = -1;

	ModuleSelectProcessor moduleSelectProcessor;

	void draw(const DrawArgs& args) override {
		if (!module) return;
		divider.setDivision((uint32_t)APP->window->getMonitorRefreshRate());

		if (module->resetRequested) {
			learnJumpPoint = -1;
			viewportCenterSmooth.reset();
			module->resetRequested = false;
		}

		viewportCenterSmooth.process();

		if (divider.process()) {
			for (int i = 0; i < SLOTS; i++) {
				for (auto it = module->jumpPoints[i].moduleIds.begin(); it != module->jumpPoints[i].moduleIds.end();) {
					ModuleWidget* mw = APP->scene->rack->getModule(*it);
					if (!mw) it = module->jumpPoints[i].moduleIds.erase(it);
					else it++;
				}
			}
		}

		for (int i = 0; i < SLOTS; i++) {
			module->lights[GotoModule<SLOTS>::LIGHT_SLOT + i * 3 + 0].setBrightness(learnJumpPoint == i);
			module->lights[GotoModule<SLOTS>::LIGHT_SLOT + i * 3 + 1].setBrightness(learnJumpPoint != i && module->jumpPoints[i].moduleIds.size() > 0);
			module->lights[GotoModule<SLOTS>::LIGHT_SLOT + i * 3 + 2].setBrightness(0.f);
		}

		if (module->jumpTrigger >= 0) {
			executeJump(module->jumpTrigger);
			module->jumpTrigger = -1;
		}
	}

	bool hasJump(int i) {
		return module->jumpPoints[i].moduleIds.size() > 0;
	}

	void learnJump(int i) {
		if (hasJump(i)) {
			clearJump(i);
		}
		else {
			learnJumpPoint = i;
			moduleSelectProcessor.setOwner(this);
			moduleSelectProcessor.startLearn([=](ModuleWidget* mw, Vec pos) {
				module->jumpPoints[learnJumpPoint].moduleIds.clear();
				module->jumpPoints[learnJumpPoint].moduleIds.push_back(mw->module->getId());
				module->jumpPoints[learnJumpPoint].zoom = std::log2(APP->scene->rackScroll->getZoom());
				learnJumpPoint = -1;
			});
		}
	}

	void learnJump(int i, std::vector<int64_t>& moduleIds) {
		module->jumpPoints[i].moduleIds.clear();
		for(int64_t moduleId : moduleIds) {
			module->jumpPoints[i].moduleIds.push_back(moduleId);
		}
		module->jumpPoints[i].zoom = std::log2(APP->scene->rackScroll->getZoom());
	}

	void clearJump(int i) {
		module->jumpPoints[i].moduleIds.clear();
	}

	void triggerJump(int i) {
		module->jumpTrigger = i;
	}

	void executeJump(int i) {
		if (module->jumpPoints[i].moduleIds.size() > 0) {
			math::Vec min = math::Vec(INFINITY, INFINITY);
			math::Vec max = math::Vec(-INFINITY, -INFINITY);
			for (int64_t moduleId : module->jumpPoints[i].moduleIds) {
				ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
				if (mw) {
					min = min.min(mw->getBox().getTopLeft());
					max = max.max(mw->getBox().getBottomRight());
				}
			}
			math::Rect moduleBox = math::Rect::fromMinMax(min, max);

			float zoom = (!module->ignoreZoom) ? module->jumpPoints[i].zoom : std::log2(APP->scene->rackScroll->getZoom());
			if (module->smoothTransition) {
				switch (module->jumpPos) {
					case JUMPPOS::ABSOLUTE: {
						// deprecated
						break;
					}
					case JUMPPOS::MODULE_CENTER: {
						//viewportCenterSmooth.trigger(mw, zoom, 1.f / APP->window->getLastFrameDuration());
						Vec source = APP->scene->rackScroll->offset / APP->scene->rackScroll->getZoom();
						Vec center = APP->scene->rackScroll->getSize() * (1.f / APP->scene->rackScroll->getZoom()) * 0.5f;
						Vec p1 = source + center;
						Vec p2 = moduleBox.getCenter();
						float f = sqrt((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y));
						viewportCenterSmooth.trigger(moduleBox, zoom, 1.f / APP->window->getLastFrameDuration(), f / 1000.f);
						break;
					}
					case JUMPPOS::MODULE_TOPLEFT: {
						Vec source = APP->scene->rackScroll->offset / APP->scene->rackScroll->getZoom();
						Vec center = APP->scene->rackScroll->getSize() * (1.f / APP->scene->rackScroll->getZoom()) * 0.5f;
						Vec p1 = source + center;
						Vec p2 = moduleBox.getTopLeft();
						p2 = p2 + (APP->scene->rackScroll->getSize() * (1.f / std::pow(2.f, zoom))) * 0.5f;
						float f = sqrt((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y));
						viewportCenterSmooth.trigger(p2, zoom, 1.f / APP->window->getLastFrameDuration(), f / 1000.f);
						break;
					}
				}
			}
			else {
				switch (module->jumpPos) {
					case JUMPPOS::ABSOLUTE: {
						// deprecated
						break;
					}
					case JUMPPOS::MODULE_CENTER: {
						StoermelderPackOne::Rack::ViewportCenter{moduleBox, -1.f, zoom};
						break;
					}
					case JUMPPOS::MODULE_TOPLEFT: {
						StoermelderPackOne::Rack::ViewportTopLeft{moduleBox, -1.f, zoom};
						break;
					}
				}
			}
		}
	}

	void step() override {
		moduleSelectProcessor.step();
		Widget::step();
	}

	void onDeselect(const event::Deselect& e) override {
		Widget::onDeselect(e);
		moduleSelectProcessor.processDeselect();
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (module && !module->jumpTriggerUsed) {
			bool numkey1 = e.key >= GLFW_KEY_0 && e.key <= GLFW_KEY_9; 
			bool numkey2 = e.key >= GLFW_KEY_KP_0 && e.key <= GLFW_KEY_KP_9;
			if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT && (numkey1 || numkey2)) {
				int i = (e.key - (numkey1 ? GLFW_KEY_0 : GLFW_KEY_KP_0) + 9) % 10;
				if (module->jumpPoints[i].moduleIds.size() > 0) {
					executeJump(i);
					e.consume(this);
				}
			}
		}
		Widget::onHoverKey(e);
	}
};


template <typename CONTAINER>
struct GotoButton : LEDButton {
	CONTAINER* gotoContainer;
	LongPressButton lpb;
	int id;

	void step() override {
		ParamQuantity* paramQuantity = getParamQuantity();
		if (paramQuantity) {
			lpb.param = paramQuantity->getParam();
			switch (lpb.process(APP->window->getLastFrameDuration())) {
				default:
				case LongPressButton::NO_PRESS:
					break;
				case LongPressButton::SHORT_PRESS:
					gotoContainer->triggerJump(id);
					break;
				case LongPressButton::LONG_PRESS:
					gotoContainer->learnJump(id);
					break;
			}
		}
		LEDButton::step();
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(createMenuItem("Clear", "", [=]() { gotoContainer->clearJump(id); }, !gotoContainer->hasJump(id)));
		menu->addChild(createMenuItem("Learn current selection", "", [=]() {
			std::vector<int64_t> moduleIds;
			for (ModuleWidget* mw : APP->scene->rack->getSelected()) {
				moduleIds.push_back(mw->module->getId());
			}
			gotoContainer->learnJump(id, moduleIds);
		}, APP->scene->rack->getSelected().size() == 0));
	}
};


struct GotoWidget : ThemedModuleWidget<GotoModule<10>> {
	GotoContainer<10>* gotoContainer = NULL;
	GotoModule<10>* module;

	GotoWidget(GotoModule<10>* module)
		: ThemedModuleWidget<GotoModule<10>>(module, "Goto") {
		setModule(module);
		this->module = module;
		this->disableDuplicateAction = true;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			gotoContainer = new GotoContainer<10>;
			gotoContainer->module = module;
			gotoContainer->mw = this;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(gotoContainer);
		}

		for (int i = 0; i < 10; i++) {
			float o = i * 23.6f;
			GotoButton<GotoContainer<10>>* jumpButton = createParamCentered<GotoButton<GotoContainer<10>>>(Vec(22.5f, 76.4f + o), module, GotoModule<10>::PARAM_SLOT + i);
			jumpButton->gotoContainer = gotoContainer;
			jumpButton->id = i;
			addParam(jumpButton);
			if (module) {
				module->params[GotoModule<10>::PARAM_SLOT + i].setValue(0.f);
			}
			addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 76.4f + o), module, GotoModule<10>::LIGHT_SLOT + i * 3));
		}
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, GotoModule<10>::INPUT_TRIG));
	}

	~GotoWidget() {
		if (gotoContainer) {
			APP->scene->rack->removeChild(gotoContainer);
			delete gotoContainer;
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<GotoModule<10>>::appendContextMenu(menu);

		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Smooth transition", "", &module->smoothTransition));
		menu->addChild(createBoolPtrMenuItem("Ignore zoom level", "", &module->ignoreZoom));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Jump position"));
		menu->addChild(Rack::createValuePtrMenuItem("Module centering", &module->jumpPos, JUMPPOS::MODULE_CENTER));
		menu->addChild(Rack::createValuePtrMenuItem("Module top left", &module->jumpPos, JUMPPOS::MODULE_TOPLEFT));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Trigger port"));
		menu->addChild(Rack::createValuePtrMenuItem("Polyphonic trigger", &module->triggerMode, TRIGGERMODE::POLYTRIGGER));
		menu->addChild(Rack::createValuePtrMenuItem("C5-A5", &module->triggerMode, TRIGGERMODE::C5));
	}
};

} // namespace Goto
} // namespace StoermelderPackOne

Model* modelGoto = createModel<StoermelderPackOne::Goto::GotoModule<10>, StoermelderPackOne::Goto::GotoWidget>("Goto");