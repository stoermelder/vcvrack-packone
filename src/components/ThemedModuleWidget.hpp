#include "plugin.hpp"
#include <thread>

namespace StoermelderPackOne {

template < typename MODULE, typename BASE = ModuleWidget >
struct ThemedModuleWidget : BASE {
	MODULE* module;
	std::string baseName;
	int panelTheme = -1;

	struct HalfPanel : SvgPanel {
		ThemedModuleWidget<MODULE, BASE>* w;
		void draw(const DrawArgs& args) override {
			if (!w) return;
			nvgScissor(args.vg, w->box.size.x / 2.f, 0, w->box.size.x, w->box.size.y);
			SvgPanel::draw(args);
			nvgResetScissor(args.vg);
		}
	};

	ThemedModuleWidget(MODULE* module, std::string baseName) {
		this->module = module;
		this->baseName = baseName;

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
		struct ManualItem : MenuItem {
			std::string baseName;
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/" + baseName + ".md");
				t.detach();
			}
		};

		struct PanelMenuItem : MenuItem {
			MODULE* module;

			PanelMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				struct PanelThemeItem : MenuItem {
					MODULE* module;
					int theme;
					void onAction(const event::Action& e) override {
						module->panelTheme = theme;
					}
					void step() override {
						rightText = module->panelTheme == theme ? "✔" : "";
						MenuItem::step();
					}
				};

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
				menu->addChild(construct<PanelThemeItem>(&MenuItem::text, "Blue", &PanelThemeItem::module, module, &PanelThemeItem::theme, 0));
				menu->addChild(construct<PanelThemeItem>(&MenuItem::text, "Dark", &PanelThemeItem::module, module, &PanelThemeItem::theme, 1));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<PanelThemeDefaultItem>(&MenuItem::text, "Blue as default", &PanelThemeDefaultItem::theme, 0));
				menu->addChild(construct<PanelThemeDefaultItem>(&MenuItem::text, "Dark as default", &PanelThemeDefaultItem::theme, 1));
				return menu;
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual", &ManualItem::baseName, baseName));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PanelMenuItem>(&MenuItem::text, "Panel", &PanelMenuItem::module, module));
		BASE::appendContextMenu(menu);
	}

	void step() override {
		if (module && module->panelTheme != panelTheme) {
			panelTheme = module->panelTheme;
			BASE::setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, panel())));
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
};

} // namespace StoermelderPackOne