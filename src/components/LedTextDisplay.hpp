#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

using namespace rack;

struct StoermelderLedDisplay : LightWidget {
	NVGcolor color = nvgRGB(0xef, 0xef, 0xef);
	std::string text;
	Vec textOffset;

	StoermelderLedDisplay() {
		box.size = Vec(39.1f, 13.2f);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (text.length() > 0) {
				std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
				nvgFillColor(args.vg, color);
				nvgFontFaceId(args.vg, font->handle);
				nvgTextLetterSpacing(args.vg, 0.0);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFontSize(args.vg, 12);
#ifndef METAMODULE
				float xOffset = 0.f;
#else
				float xOffset = 12.f;
#endif
				nvgTextBox(args.vg, xOffset, box.size.y / 2.f, box.size.x, text.c_str(), NULL);
			}
		}
	}
};


template < typename MODULE, int SCENE_MAX >
struct SceneLedDisplay : StoermelderPackOne::StoermelderLedDisplay {
	MODULE* module;

	void step() override {
		if (module) {
			text = string::f("%02d", module->sceneSelected + 1);
		} 
		else {
			text = "00";
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
		ui::Menu* menu = createMenu();

		struct SceneItem : MenuItem {
			MODULE* module;
			int scene;
			
			void onAction(const event::Action& e) override {
				module->sceneSet(scene);
			}

			void step() override {
				rightText = module->sceneSelected == scene ? "✔" : "";
				MenuItem::step();
			}
		};

		struct CopyMenuItem : MenuItem {
			MODULE* module;
			CopyMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct CopyItem : MenuItem {
					MODULE* module;
					int scene;
					
					void onAction(const event::Action& e) override {
						module->sceneCopy(scene);
					}
				};

				for (int i = 0; i < SCENE_MAX; i++) {
					menu->addChild(construct<CopyItem>(&MenuItem::text, string::f("%02u", i + 1), &CopyItem::module, module, &CopyItem::scene, i));
				}

				return menu;
			}
		};

		struct CountMenuItem : MenuItem {
			MODULE* module;
			CountMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct CountItem : MenuItem {
					MODULE* module;
					int count;
					
					void onAction(const event::Action& e) override {
						module->sceneSetCount(count);
					}

					void step() override {
						rightText = module->sceneCount == count ? "✔" : "";
						MenuItem::step();
					}
				};

				for (int i = 0; i < SCENE_MAX; i++) {
					menu->addChild(construct<CountItem>(&MenuItem::text, string::f("%02u", i + 1), &CountItem::module, module, &CountItem::count, i + 1));
				}

				return menu;
			}
		};

		struct ResetItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->sceneReset();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scene"));
		for (int i = 0; i < SCENE_MAX; i++) {
			menu->addChild(construct<SceneItem>(&MenuItem::text, string::f("%02u", i + 1), &SceneItem::module, module, &SceneItem::scene, i));
		}
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<CountMenuItem>(&MenuItem::text, "Count", &CountMenuItem::module, module));
		menu->addChild(construct<CopyMenuItem>(&MenuItem::text, "Copy to", &CopyMenuItem::module, module));
		menu->addChild(construct<ResetItem>(&MenuItem::text, "Reset", &ResetItem::module, module));
	}
};


struct LedTextDisplay : OpaqueWidget {
	std::string text;
	float fontSize;
	math::Vec textOffset;
	NVGcolor color;
	NVGcolor bgColor;

	LedTextDisplay() {
		fontSize = 12.f;
		color = nvgRGB(0xff, 0xd7, 0x14);
		bgColor = color::BLACK;
		textOffset = math::Vec(5, 2);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		if (layer == 1) {
			if (bgColor.a > 0.0) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.0);
				nvgFillColor(args.vg, bgColor);
				nvgFill(args.vg);
			}

			std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			nvgFillColor(args.vg, color);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, 0.0);
			nvgFontSize(args.vg, fontSize);
			nvgTextBox(args.vg, textOffset.x, textOffset.y + fontSize, box.size.x - 2 * textOffset.x, text.c_str(), NULL);
		}
		nvgResetScissor(args.vg);
	}
};

} // namespace StoermelderPackOne