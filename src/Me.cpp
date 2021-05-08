#include "plugin.hpp"
#include "components/OverlayMessageWidget.hpp"

namespace StoermelderPackOne {
namespace Me {

struct MeModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_ACTIVE,
		NUM_LIGHTS
	};

	MeModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}
};

struct MeWidget : ModuleWidget, OverlayMessageProvider {
	bool active = false;
	Widget* lastSelectedWidget = NULL;
	ParamWidget* pw = NULL;
	int p = -1;

	MeWidget(MeModule* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Me.svg")));

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 330.0f), module, MeModule::LIGHT_ACTIVE));

		if (module) {
			active = registerSingleton("Me", this);
			if (active) {
				OverlayMessageWidget::registerProvider(this);
			}
		}
	}

	~MeWidget() {
		if (module && active) {
			unregisterSingleton("Me", this);
			OverlayMessageWidget::unregisterProvider(this);
		}
	}

	void step() override {
		ModuleWidget::step();
		if (!module) return;
		
		module->lights[MeModule::LIGHT_ACTIVE].setBrightness(active);

		Widget* w = APP->event->getDraggedWidget();
		// Only handle left button events
		if (!w || APP->event->dragButton != GLFW_MOUSE_BUTTON_LEFT) {
			lastSelectedWidget = NULL;
			pw = NULL;
			p = -1;
		}
		else {
			if (w != lastSelectedWidget) {
				lastSelectedWidget = w;
				// Was the last touched widget an ParamWidget?
				pw = dynamic_cast<ParamWidget*>(lastSelectedWidget);
			}
			p = pw != NULL ? 0 : -1;
		}
	}

	int nextOverlayMessageId() override {
		if (p == 0) {
			p = -1;
			return 0;
		}
		return -1;
	}

	void getOverlayMessage(int id, Message& m) override {
		if (id != 0) return;
		if (!pw) return;
		ParamQuantity* paramQuantity = pw->paramQuantity;
		if (!paramQuantity) return;

		m.title = paramQuantity->getDisplayValueString() + paramQuantity->getUnit();
		m.subtitle[0] = paramQuantity->module->model->name;
		m.subtitle[1] = paramQuantity->label;
	}


	void appendContextMenu(Menu* menu) override {
		struct WhiteOverlayTextItem : MenuItem {
			void step() override {
				rightText = CHECKMARK(color::toHexString(pluginSettings.overlayTextColor) == color::toHexString(color::WHITE));
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				if (color::toHexString(pluginSettings.overlayTextColor) != color::toHexString(color::WHITE)) {
					pluginSettings.overlayTextColor = color::WHITE;
				}
				else {
					pluginSettings.overlayTextColor = bndGetTheme()->menuTheme.textColor;
				}
				pluginSettings.saveToJson();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<WhiteOverlayTextItem>(&MenuItem::text, "White overlay text"));
	}
};

} // namespace Me
} // namespace StoermelderPackOne

Model* modelMe = createModel<StoermelderPackOne::Me::MeModule, StoermelderPackOne::Me::MeWidget>("Me");