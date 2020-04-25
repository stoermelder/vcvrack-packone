#include "plugin.hpp"
#include "settings.hpp"

namespace Goto {

struct GotoModule : Module {
	enum ParamIds {
		ENUMS(PARAM_SLOT, 10),
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_SLOT, 10 * 3),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	GotoModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (int i = 0; i < 10; i++) {
            configParam<TriggerParamQuantity>(PARAM_SLOT + i, 0, 1, 0, string::f("Jump point %i (SHIFT+%i)", i + 1, (i + 1) % 10));
        }
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};

struct JumpPoint {
	int moduleId;
	float zoom = 1.f;
};


struct HotkeyContainer : widget::Widget {
	ModuleWidget* mw;
	dsp::ClockDivider divider;

	JumpPoint jumpPoints[10];
	int learnJumpPoint = -1;

	HotkeyContainer() {
		divider.setDivision(APP->window->getMonitorRefreshRate());
	}

	void step() override {
		Widget::step();

		if (learnJumpPoint >= 0) {
			// Learn module
			Widget* w = APP->event->getSelectedWidget();
			if (!w) goto j;
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
			if (!mw || mw == this->mw) goto j;
			Module* m = mw->module;
			if (!m) goto j;
			jumpPoints[learnJumpPoint].moduleId = m->id;
			jumpPoints[learnJumpPoint].zoom = rack::settings::zoom;
			learnJumpPoint = -1;
		}

		j:
		if (divider.process()) {
			for (int i = 0; i < 10; i++) {
				if (jumpPoints[i].moduleId >= 0) {
					ModuleWidget* mw = APP->scene->rack->getModule(jumpPoints[i].moduleId);
					if (!mw) jumpPoints[i].moduleId = -1;
				}
			}
		}

		if (mw->module) {
			for (int i = 0; i < 10; i++) {
				mw->module->lights[GotoModule::LIGHT_SLOT + i * 3 + 0].setBrightness(learnJumpPoint == i);
				mw->module->lights[GotoModule::LIGHT_SLOT + i * 3 + 1].setBrightness(learnJumpPoint != i && jumpPoints[i].moduleId >= 0);
				mw->module->lights[GotoModule::LIGHT_SLOT + i * 3 + 2].setBrightness(0.f);
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
				// Move the view to center the mapped module
				// NB: unstable API!
				Vec offset = mw->box.pos;
				offset = offset.plus(mw->box.size.mult(0.5f));
				offset = offset.mult(APP->scene->rackScroll->zoomWidget->zoom);
				offset = offset.minus(APP->scene->box.size.mult(0.5f));
				APP->scene->rackScroll->offset = offset;
				rack::settings::zoom = jumpPoints[i].zoom;
			}
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT && e.key >= GLFW_KEY_0 && e.key <= GLFW_KEY_9) {
			int i = (e.key - GLFW_KEY_0 + 9) % 10;
			executeJump(i);
			e.consume(this);
		}
		Widget::onHoverKey(e);
	}
};


struct GotoButton : LEDButton {
	HotkeyContainer* hotkeyContainer;
	LongPressButton lpb;
	int id;

	float frameRate = 1.f;

	void step() override {
		if (frameRate >= 1.f) {
			frameRate = 1.f / APP->window->getLastFrameRate();
		}
		if (paramQuantity) {
			lpb.param = paramQuantity->getParam();
			switch (lpb.process(frameRate)) {
				default:
				case LongPressButton::NO_PRESS:
					break;
				case LongPressButton::SHORT_PRESS:
					hotkeyContainer->executeJump(id);
					break;
				case LongPressButton::LONG_PRESS:
					hotkeyContainer->learnJump(id);
					break;
			}
		}
		LEDButton::step();
	}
};


struct GotoWidget : ThemedModuleWidget<GotoModule> {
	HotkeyContainer* hotkeyContainer = NULL;

	GotoWidget(GotoModule* module)
		: ThemedModuleWidget<GotoModule>(module, "Goto") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			hotkeyContainer = new HotkeyContainer;
			hotkeyContainer->mw = this;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(hotkeyContainer);
		}

		for (int i = 0; i < 10; i++) {
			float o = i * 23.6f;
			GotoButton* jumpButton = createParamCentered<GotoButton>(Vec(22.5f, 93.5f + o), module, GotoModule::PARAM_SLOT + i);
			jumpButton->hotkeyContainer = hotkeyContainer;
			jumpButton->id = i;
			addParam(jumpButton);
			if (module) {
				module->params[GotoModule::PARAM_SLOT + i].setValue(0.f);
			}
			addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 93.5f + o), module, GotoModule::LIGHT_SLOT + i * 3));
		}
	}

	~GotoWidget() {
		if (hotkeyContainer) {
			APP->scene->rack->removeChild(hotkeyContainer);
			delete hotkeyContainer;
		}
	}

	json_t* toJson() override {
		json_t* rootJ = ModuleWidget::toJson();

		json_t* jumpPointsJ = json_array();
		for (JumpPoint jp : hotkeyContainer->jumpPoints) {
			json_t* jumpPointJ = json_object();
			json_object_set_new(jumpPointJ, "moduleId", json_integer(jp.moduleId));
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

		json_t* jumpPointsJ = json_object_get(rootJ, "jumpPoints");
		for (int i = 0; i < 10; i++) {
			json_t* jumpPointJ = json_array_get(jumpPointsJ, i);
			hotkeyContainer->jumpPoints[i].moduleId = json_integer_value(json_object_get(jumpPointJ, "moduleId"));
			hotkeyContainer->jumpPoints[i].zoom = json_real_value(json_object_get(jumpPointJ, "zoom"));
		}

		ModuleWidget::fromJson(rootJ);
	}
};

} // namespace Goto

Model* modelGoto = createModel<Goto::GotoModule, Goto::GotoWidget>("Goto");