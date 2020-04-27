#include "plugin.hpp"

namespace Goto {

enum class TRIGGERMODE {
	POLYTRIGGER = 0,
	C4 = 1
};

template < int SLOTS >
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

	dsp::SchmittTrigger trigger[SLOTS];
	int jumpTrigger = -1;
	bool jumpTriggerUsed = false;

	float triggerVoltage;

	GotoModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < SLOTS; i++) {
			configParam<TriggerParamQuantity>(PARAM_SLOT + i, 0, 1, 0, string::f("Jump point %i (SHIFT+%i)", i + 1, (i + 1) % 10));
		}
	}

	void onReset() override {
		Module::onReset();
		triggerMode = TRIGGERMODE::POLYTRIGGER;
		triggerVoltage = 0.f;
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
				case TRIGGERMODE::C4: {
					float v = inputs[INPUT_TRIG].getVoltage();
					if (v != 0.f && triggerVoltage != v) {
						triggerVoltage = v;
						jumpTrigger = std::round(clamp(triggerVoltage * 12.f, 0.f, SLOTS - 1.f));
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
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		triggerMode = (TRIGGERMODE)json_integer_value(json_object_get(rootJ, "triggerMode"));
	}
};


struct GotoTarget {
	int moduleId = -1;
	float x = 0, y = 0;
	float zoom = 1.f;
};

template < int SLOTS >
struct GotoContainer : widget::Widget {
	ModuleWidget* mw;
	StoermelderPackOne::Rack::ViewportCenterSmooth viewportCenterSmooth;
	dsp::ClockDivider divider;

	/** [Stored to JSON] */
	GotoTarget jumpPoints[SLOTS];
	/** [Stored to JSON] */
	bool smoothTransition = false;
	/** [Stored to JSON] */
	bool centerModule = true;
	/** [Stored to JSON] */
	bool ignoreZoom = false;

	int learnJumpPoint = -1;
	bool useHotkeys = true;

	GotoContainer() {
		divider.setDivision(APP->window->getMonitorRefreshRate());
	}

	void step() override {
		Widget::step();
		viewportCenterSmooth.process();

		if (learnJumpPoint >= 0) {
			// Learn module
			Widget* w = APP->event->getSelectedWidget();
			if (!w) goto j;
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
			if (!mw || mw == this->mw) goto j;
			Module* m = mw->module;
			if (!m) goto j;

			Vec source = APP->scene->rackScroll->offset;
			source = source.plus(APP->scene->box.size.mult(0.5f));
			source = source.div(APP->scene->rackScroll->zoomWidget->zoom);

			jumpPoints[learnJumpPoint].moduleId = m->id;
			jumpPoints[learnJumpPoint].x = source.x;
			jumpPoints[learnJumpPoint].y = source.y;
			jumpPoints[learnJumpPoint].zoom = rack::settings::zoom;
			learnJumpPoint = -1;
		}

		j:
		if (divider.process()) {
			for (int i = 0; i < SLOTS; i++) {
				if (jumpPoints[i].moduleId >= 0) {
					ModuleWidget* mw = APP->scene->rack->getModule(jumpPoints[i].moduleId);
					if (!mw) jumpPoints[i].moduleId = -1;
				}
			}
		}

		if (mw->module) {
			for (int i = 0; i < SLOTS; i++) {
				mw->module->lights[GotoModule<SLOTS>::LIGHT_SLOT + i * 3 + 0].setBrightness(learnJumpPoint == i);
				mw->module->lights[GotoModule<SLOTS>::LIGHT_SLOT + i * 3 + 1].setBrightness(learnJumpPoint != i && jumpPoints[i].moduleId >= 0);
				mw->module->lights[GotoModule<SLOTS>::LIGHT_SLOT + i * 3 + 2].setBrightness(0.f);
			}
		}
	}

	void learnJump(int i) {
		if (jumpPoints[i].moduleId >= 0) {
			jumpPoints[i].moduleId = -1;
		}
		else {
			learnJumpPoint = i;
		}
	}

	void executeJump(int i) {
		if (jumpPoints[i].moduleId >= 0) {
			ModuleWidget* mw = APP->scene->rack->getModule(jumpPoints[i].moduleId);
			if (mw) {
				if (smoothTransition) {
					float zoom = !ignoreZoom ? jumpPoints[i].zoom : rack::settings::zoom;
					if (centerModule) {
						viewportCenterSmooth.trigger(mw, zoom, APP->window->getLastFrameRate());
					}
					else {
						viewportCenterSmooth.trigger(Vec(jumpPoints[i].x, jumpPoints[i].y), zoom, APP->window->getLastFrameRate());
					}
				}
				else {
					if (centerModule) {
						StoermelderPackOne::Rack::ViewportCenter{mw};
					}
					else {
						StoermelderPackOne::Rack::ViewportCenter{Vec(jumpPoints[i].x, jumpPoints[i].y)};
					}
					if (!ignoreZoom) {
						rack::settings::zoom = jumpPoints[i].zoom;
					}
				}
			}
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (useHotkeys && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT && e.key >= GLFW_KEY_0 && e.key <= GLFW_KEY_9) {
			int i = (e.key - GLFW_KEY_0 + 9) % 10;
			executeJump(i);
			e.consume(this);
		}
		Widget::onHoverKey(e);
	}
};


template < typename CONTAINER >
struct GotoButton : LEDButton {
	CONTAINER* gotoContainer;
	LongPressButton lpb;
	int id;

	void step() override {
		if (paramQuantity) {
			lpb.param = paramQuantity->getParam();
			switch (lpb.process(1.f / APP->window->getLastFrameRate())) {
				default:
				case LongPressButton::NO_PRESS:
					break;
				case LongPressButton::SHORT_PRESS:
					gotoContainer->executeJump(id);
					break;
				case LongPressButton::LONG_PRESS:
					gotoContainer->learnJump(id);
					break;
			}
		}
		LEDButton::step();
	}
};


struct GotoWidget : ThemedModuleWidget<GotoModule<10>> {
	GotoContainer<10>* gotoContainer = NULL;
	GotoModule<10>* module;

	GotoWidget(GotoModule<10>* module)
		: ThemedModuleWidget<GotoModule<10>>(module, "Goto") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			gotoContainer = new GotoContainer<10>;
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

	void step() override {
		if (module) {
			gotoContainer->useHotkeys = !module->jumpTriggerUsed;
			if (module->jumpTrigger >= 0) {
				gotoContainer->executeJump(module->jumpTrigger);
				module->jumpTrigger = -1;
			}
		}
		ThemedModuleWidget<GotoModule<10>>::step();
	}

	json_t* toJson() override {
		json_t* rootJ = ModuleWidget::toJson();

		json_object_set_new(rootJ, "smoothTransition", json_boolean(gotoContainer->smoothTransition));
		json_object_set_new(rootJ, "centerModule", json_boolean(gotoContainer->centerModule));
		json_object_set_new(rootJ, "ignoreZoom", json_boolean(gotoContainer->ignoreZoom));

		json_t* jumpPointsJ = json_array();
		for (GotoTarget jp : gotoContainer->jumpPoints) {
			json_t* jumpPointJ = json_object();
			json_object_set_new(jumpPointJ, "moduleId", json_integer(jp.moduleId));
			json_object_set_new(jumpPointJ, "x", json_real(jp.x));
			json_object_set_new(jumpPointJ, "y", json_real(jp.y));
			json_object_set_new(jumpPointJ, "zoom", json_real(jp.zoom));
			json_array_append_new(jumpPointsJ, jumpPointJ);
		}
		json_object_set_new(rootJ, "jumpPoints", jumpPointsJ);

		return rootJ;
	}

	void fromJson(json_t* rootJ) override {
		// Hack for preventing duplication of this module
		json_t* idJ = json_object_get(rootJ, "id");
		if (idJ && APP->engine->getModule(json_integer_value(idJ)) != NULL) return;

		gotoContainer->smoothTransition = json_boolean_value(json_object_get(rootJ, "smoothTransition"));
		gotoContainer->centerModule = json_boolean_value(json_object_get(rootJ, "centerModule"));
		gotoContainer->ignoreZoom = json_boolean_value(json_object_get(rootJ, "ignoreZoom"));

		json_t* jumpPointsJ = json_object_get(rootJ, "jumpPoints");
		for (int i = 0; i < 10; i++) {
			json_t* jumpPointJ = json_array_get(jumpPointsJ, i);
			gotoContainer->jumpPoints[i].moduleId = json_integer_value(json_object_get(jumpPointJ, "moduleId"));
			gotoContainer->jumpPoints[i].x = json_real_value(json_object_get(jumpPointJ, "x"));
			gotoContainer->jumpPoints[i].y = json_real_value(json_object_get(jumpPointJ, "y"));
			gotoContainer->jumpPoints[i].zoom = json_real_value(json_object_get(jumpPointJ, "zoom"));
		}

		ThemedModuleWidget<GotoModule<10>>::fromJson(rootJ);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<GotoModule<10>>::appendContextMenu(menu);

		struct SmoothTransitionItem : MenuItem {
			GotoContainer<10>* gotoContainer;
			void onAction(const event::Action& e) override {
				gotoContainer->smoothTransition ^= true;
			}
			void step() override {
				rightText = gotoContainer->smoothTransition ? "✔" : "";
				MenuItem::step();
			}
		};

		struct CenterModuleItem : MenuItem {
			GotoContainer<10>* gotoContainer;
			void onAction(const event::Action& e) override {
				gotoContainer->centerModule ^= true;
			}
			void step() override {
				rightText = gotoContainer->centerModule ? "✔" : "";
				MenuItem::step();
			}
		};

		struct IgnoreZoomItem : MenuItem {
			GotoContainer<10>* gotoContainer;
			void onAction(const event::Action& e) override {
				gotoContainer->ignoreZoom ^= true;
			}
			void step() override {
				rightText = gotoContainer->ignoreZoom ? "✔" : "";
				MenuItem::step();
			}
		};

		struct TriggerModeMenuItem : MenuItem {
			struct TriggerModeItem : MenuItem {
				GotoModule<10>* module;
				TRIGGERMODE triggerMode;
				void onAction(const event::Action& e) override {
					module->triggerMode = triggerMode;
				}
				void step() override {
					rightText = module->triggerMode == triggerMode ? "✔" : "";
					MenuItem::step();
				}
			};

			GotoModule<10>* module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<TriggerModeItem>(&MenuItem::text, "Polyphonic trigger", &TriggerModeItem::module, module, &TriggerModeItem::triggerMode, TRIGGERMODE::POLYTRIGGER));
				menu->addChild(construct<TriggerModeItem>(&MenuItem::text, "C4", &TriggerModeItem::module, module, &TriggerModeItem::triggerMode, TRIGGERMODE::C4));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SmoothTransitionItem>(&MenuItem::text, "Smooth transition", &SmoothTransitionItem::gotoContainer, gotoContainer));
		menu->addChild(construct<CenterModuleItem>(&MenuItem::text, "Center module", &CenterModuleItem::gotoContainer, gotoContainer));
		menu->addChild(construct<IgnoreZoomItem>(&MenuItem::text, "Ignore zoom level", &IgnoreZoomItem::gotoContainer, gotoContainer));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<TriggerModeMenuItem>(&MenuItem::text, "Trigger port-mode", &TriggerModeMenuItem::rightText, RIGHT_ARROW, &TriggerModeMenuItem::module, module));
	}
};

} // namespace Goto

Model* modelGoto = createModel<Goto::GotoModule<10>, Goto::GotoWidget>("Goto");