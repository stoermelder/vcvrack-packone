#include "plugin.hpp"
#include <thread>

namespace StoermelderPackOne {

template < typename MODULE, typename BASE = ModuleWidget >
struct ThemedModuleWidget : BASE {
	MODULE* module;
	std::string baseName;
	std::string manualName;
	int panelTheme = -1;

	bool disableDuplicateAction = false;

	struct HalfPanel : SvgPanel {
		ThemedModuleWidget<MODULE, BASE>* w;
		void draw(const DrawArgs& args) override {
			if (!w) return;
			nvgScissor(args.vg, w->box.size.x / 2.f, 0, w->box.size.x, w->box.size.y);
			SvgPanel::draw(args);
			nvgResetScissor(args.vg);
		}
	};

	ThemedModuleWidget(MODULE* module, std::string baseName, std::string manualName = "") {
		this->module = module;
		this->baseName = baseName;
		this->manualName = manualName;

		if (module) {
			// Normal operation
			BASE::setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, panel())));
		}
		else {
			// Module Browser
			BASE::setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/" + baseName + ".svg")));
			HalfPanel* darkPanel = new HalfPanel();
			darkPanel->w = this;
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/dark/" + baseName + ".svg")));
			BASE::addChild(darkPanel);
		}
	}

	void appendContextMenu(Menu* menu) override {
		if (disableDuplicateAction) {
			MenuItem* item = NULL;
			for (auto rit = menu->children.begin(); rit != menu->children.end(); rit++) {
				item = dynamic_cast<MenuItem*>(*rit);
				if (item && (item->text == "Duplicate" || item->text == "└ with cables")) {
					item->visible = false;
				}
			}
		}

		struct PanelMenuItem : MenuItem {
			MODULE* module;

			PanelMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				struct PanelThemeDefaultItem : MenuItem {
					int theme;
					void onAction(const event::Action& e) override {
						pluginSettings.panelThemeDefault = theme;
						pluginSettings.saveToJson();
					}
					void step() override {
						rightText = pluginSettings.panelThemeDefault == theme ? "✔" : "";
						MenuItem::step();
					}
				};

				Menu* menu = new Menu;
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Blue", &module->panelTheme, 0));
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Dark", &module->panelTheme, 1));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<PanelThemeDefaultItem>(&MenuItem::text, "Blue as default", &PanelThemeDefaultItem::theme, 0));
				menu->addChild(construct<PanelThemeDefaultItem>(&MenuItem::text, "Dark as default", &PanelThemeDefaultItem::theme, 1));
				menu->addChild(new MenuSeparator);
				menu->addChild(createBoolMenuItem("Use Rack setting", "",
					[=]() {
						return module->panelTheme == -1;
					}, 
					[=](bool b) {
						pluginSettings.panelThemeDefault = -1;
						pluginSettings.saveToJson();
						module->panelTheme = -1;
					}
				));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PanelMenuItem>(&MenuItem::text, "Panel", &PanelMenuItem::module, module));
		BASE::appendContextMenu(menu);
	}

	void step() override {
		if (module) {
			if (module->panelTheme == -1) {
				if ((int)settings::preferDarkPanels != panelTheme) {
					panelTheme = (int)settings::preferDarkPanels;
					BASE::setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, panel())));
				}
			}
			else if (module->panelTheme != panelTheme) {
				panelTheme = module->panelTheme;
				BASE::setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, panel())));
			}
		}
		BASE::step();
	}

	std::string panel() {
		switch (panelTheme) {
			default:
			case 0:
				return "res/" + baseName + ".svg";
			case 1:
				return "res/dark/" + baseName + ".svg";
			case 2:
				return "res/bright/" + baseName + ".svg";
		}
	}

	void onHoverKey(const Widget::HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (disableDuplicateAction && e.keyName == "c" && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
				e.consume(NULL);
				return;
			}
			if (disableDuplicateAction && e.keyName == "d" && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
				e.consume(NULL);
				return;
			}
			if (disableDuplicateAction && e.keyName == "d" && (e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
				e.consume(NULL);
				return;
			}
		}

		ModuleWidget::onHoverKey(e);
	}
};

} // namespace StoermelderPackOne