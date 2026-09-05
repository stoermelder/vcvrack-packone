#pragma once
#include <rack.hpp>
#include "../pluginhelpers.hpp"
#include "../pluginsettings.hpp"

extern rack::plugin::Plugin* pluginInstance;

namespace StoermelderPackOne {

using namespace rack;

template < typename MODULE, typename BASE = ModuleWidget >
struct ThemedModuleWidget : BASE {
	MODULE* module;
	std::string baseName;
	std::string manualName;
	int panelTheme = -1;

	bool disableDuplicateAction = false;
	bool disableDarkPanel = false;
	// Set to true on a module to skip the base-class panel decoration (border + edge vignette).
	bool disablePanelDecoration = false;

	ThemedModuleWidget(MODULE* module, std::string baseName, std::string manualName = "", bool disableDarkPanel = false) {
		this->module = module;
		this->baseName = baseName;
		this->manualName = manualName;
		this->disableDarkPanel = disableDarkPanel;

#ifdef METAMODULE
		BASE::setPanel(Svg::load(asset::plugin(pluginInstance, "res/" + baseName + ".svg")));
#else
		if (module) {
			// Normal operation
			BASE::setPanel(Svg::load(asset::plugin(pluginInstance, panel())));
		}
		else {
			// Module Browser
			if (!settings::preferDarkPanels || disableDarkPanel) {
				BASE::setPanel(Svg::load(asset::plugin(pluginInstance, "res/" + baseName + ".svg")));
			}
			else {
				BASE::setPanel(Svg::load(asset::plugin(pluginInstance, "res/dark/" + baseName + ".svg")));
			}
		}
#endif
	}

#ifndef METAMODULE
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

		if (!disableDarkPanel) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<PanelMenuItem>(&MenuItem::text, "Panel", &PanelMenuItem::module, module));
		}
		BASE::appendContextMenu(menu);
	}

	void step() override {
		if (module) {
			int theme = -1;
			if (module->panelTheme == -1) {
				theme = settings::preferDarkPanels && !disableDarkPanel ? 1 : 0;
			}
			else {
				theme = disableDarkPanel ? 0 : module->panelTheme;
			}
			if (theme != panelTheme) {
				panelTheme = theme;
				BASE::setPanel(Svg::load(asset::plugin(pluginInstance, panel())));
			}
		}

		if (settings::headless) return;
		BASE::step();
	}

	std::string panel() {
		int theme = disableDarkPanel ? 0 : panelTheme;
		switch (theme) {
			default:
			case 0:
				return "res/" + baseName + ".svg";
			case 1:
				return "res/dark/" + baseName + ".svg";
			case 2:
				return "res/bright/" + baseName + ".svg";
		}
	}

	void draw(const typename BASE::DrawArgs& args) override {
		BASE::draw(args);

		if (disablePanelDecoration || settings::headless) return;

		const float w = this->box.size.x;
		const float h = this->box.size.y;
		if (w <= 0.f || h <= 0.f) return;

		const float radius = 2.5f;
		const float feather = mm2px(3.f);

		// Effective panel brightness, taking the Rack-wide dark-panel preference into account.
		bool dark = panelTheme == 1 || (panelTheme == -1 && settings::preferDarkPanels && !disableDarkPanel);

		NVGcolor vignetteColor = nvgRGBAf(0.f, 0.f, 0.f, 0.07f);
		NVGcolor borderColor = dark ? nvgRGBAf(1.f, 1.f, 1.f, 0.07f) : nvgRGBAf(0.f, 0.f, 0.f, 0.13f);

		nvgSave(args.vg);

		// Soft darkening towards the panel edges for a bit of depth.
		NVGpaint vignette = nvgBoxGradient(args.vg, feather, feather, w - 2.f * feather, h - 2.f * feather,
			radius, feather, nvgRGBAf(0.f, 0.f, 0.f, 0.f), vignetteColor);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, w, h, radius);
		nvgFillPaint(args.vg, vignette);
		nvgFill(args.vg);

		// Thin inset border to crisp up the panel outline.
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, w - 1.f, h - 1.f, radius);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, borderColor);
		nvgStroke(args.vg);

		nvgRestore(args.vg);
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
#endif
};

} // namespace StoermelderPackOne