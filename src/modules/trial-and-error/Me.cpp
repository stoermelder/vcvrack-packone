#include "../../plugin.hpp"
#include "../../ui/OverlayMessageWidget.hpp"

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
		setPanel(Svg::load(asset::plugin(pluginInstance, "res/Me.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

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
		ParamQuantity* paramQuantity = pw->getParamQuantity();
		if (!paramQuantity) return;

		m.title = paramQuantity->getDisplayValueString() + paramQuantity->getUnit();
		m.subtitle[0] = paramQuantity->module->model->name;
		m.subtitle[1] = paramQuantity->name;
	}


	void appendContextMenu(Menu* menu) override {
		struct OverlayLabel : MenuLabel {
			OverlayLabel() {
				text = "Overlay settings";
			}
			~OverlayLabel() {
				pluginSettings.saveToJson();
			}
		};

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
			}
		};

		struct HposMenuItem : MenuItem {
			Menu* createChildMenu() override {
				struct HposItem : MenuItem {
					OverlayMessageWidget::HPOS pos;
					void onAction(const event::Action& e) override {
						pluginSettings.overlayHpos = (int)pos;
					}
					void step() override {
						rightText = CHECKMARK(pluginSettings.overlayHpos == (int)pos);
						MenuItem::step();
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<HposItem>(&MenuItem::text, "Center", &HposItem::pos, OverlayMessageWidget::HPOS::CENTER));
				menu->addChild(construct<HposItem>(&MenuItem::text, "Left", &HposItem::pos, OverlayMessageWidget::HPOS::LEFT));
				menu->addChild(construct<HposItem>(&MenuItem::text, "Right", &HposItem::pos, OverlayMessageWidget::HPOS::RIGHT));
				return menu;
			}
		};

		struct VposMenuItem : MenuItem {
			Menu* createChildMenu() override {
				struct VposItem : MenuItem {
					OverlayMessageWidget::VPOS pos;
					void onAction(const event::Action& e) override {
						pluginSettings.overlayVpos = (int)pos;
					}
					void step() override {
						rightText = CHECKMARK(pluginSettings.overlayVpos == (int)pos);
						MenuItem::step();
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<VposItem>(&MenuItem::text, "Bottom", &VposItem::pos, OverlayMessageWidget::VPOS::BOTTOM));
				menu->addChild(construct<VposItem>(&MenuItem::text, "Top", &VposItem::pos, OverlayMessageWidget::VPOS::TOP));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(new OverlayLabel);
		menu->addChild(construct<WhiteOverlayTextItem>(&MenuItem::text, "White text"));
		menu->addChild(construct<HposMenuItem>(&MenuItem::text, "Horizontal position", &MenuItem::rightText, RIGHT_ARROW));
		menu->addChild(construct<VposMenuItem>(&MenuItem::text, "Vertical position", &MenuItem::rightText, RIGHT_ARROW));
		menu->addChild(Rack::createPtrSlider(&pluginSettings.overlayOpacity, 0.f, 1.f, 1.0f, "Opacity", "%", 100.f, 140.0f));
		menu->addChild(Rack::createPtrSlider(&pluginSettings.overlayScale, 1.f, 5.f, 1.0f, "Scale", "", 1.f, 140.0f));
	}
};

} // namespace Me
} // namespace StoermelderPackOne

Model* modelMe = createModel<StoermelderPackOne::Me::MeModule, StoermelderPackOne::Me::MeWidget>("Me");