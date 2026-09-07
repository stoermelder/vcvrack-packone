#include "../../plugin.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../../utils/keyboard.hpp"

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

	int panelTheme = 0;

	MeModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		ResetEvent re;
		onReset(re);
	}
};


struct OverlayMagnifierWidget : widget::Widget {
	bool active = false;
	bool learning = false;
	Vec mousePos;

	int capturedImage = -1;
	int capturedImageW = 0, capturedImageH = 0;

	void step() override {
		box.pos = Vec(0, 0);
		box.size = APP->scene->box.size;
		widget::Widget::step();
	}

	void onHover(const event::Hover& e) override {
		mousePos = e.pos;
		widget::Widget::onHover(e);
	}

	void onHoverKey(const event::HoverKey& e) override {
		int e_mods = e.mods & (GLFW_MOD_ALT | RACK_MOD_CTRL | GLFW_MOD_SHIFT);
		int e_key = keyFix(e.key);

		if (learning) {
			if (e.action == GLFW_PRESS && !keyName(e_key).empty()) {
				pluginSettings.magnifierKey = e_key;
				pluginSettings.magnifierMods = e_mods;
				pluginSettings.saveToJson();
				learning = false;
				e.consume(this);
				return;
			}
		}
		else if (pluginSettings.magnifierKey >= 0) {
			if (e_key == pluginSettings.magnifierKey && e_mods == pluginSettings.magnifierMods) {
				if (e.action == GLFW_PRESS) {
					active = true;
					e.consume(this);
				}
				else if (e.action == GLFW_RELEASE) {
					active = false;
					e.consume(this);
				}
			}
		}

		widget::Widget::onHoverKey(e);
	}

	void draw(const DrawArgs& args) override {
		if (!active) {
			if (capturedImage >= 0) {
				nvgDeleteImage(args.vg, capturedImage);
				capturedImage = -1;
			}
			return;
		}

		float radius = pluginSettings.magnifierRadius;
		float zoom = pluginSettings.magnifierZoom;
		// The scene radius captured around the cursor (in logical pixels)
		float captureR = radius / zoom;

		float pixelRatio = APP->window->pixelRatio;
		int winW = (int)(APP->scene->box.size.x * pixelRatio);
		int winH = (int)(APP->scene->box.size.y * pixelRatio);

		// Place the display circle diagonally offset from the cursor so it never
		// overlaps the capture area. Prefer upper-left; flip each axis independently
		// when more than ~1/3 of the circle would go outside the window.
		float gap = 8.f;
		float offsetDist = radius + captureR + gap;
		float diag = offsetDist * 0.7071f;
		float maxOvhg = radius * 0.7f;
		Vec sceneSize = APP->scene->box.size;

		float dcx = mousePos.x - diag;
		if (dcx < radius - maxOvhg)
			dcx = mousePos.x + diag;
		dcx = math::clamp(dcx, radius - maxOvhg, sceneSize.x - radius + maxOvhg);

		float dcy = mousePos.y - diag;
		if (dcy < radius - maxOvhg)
			dcy = mousePos.y + diag;
		dcy = math::clamp(dcy, radius - maxOvhg, sceneSize.y - radius + maxOvhg);

		Vec displayCenter = Vec(dcx, dcy);

		// Capture area in framebuffer pixels, centered on the cursor
		int iCapW = std::max(1, (int)(captureR * 2.f * pixelRatio));
		int iCapH = iCapW;

		float glMx = mousePos.x * pixelRatio;
		float glMy = winH - mousePos.y * pixelRatio;  // GL Y is flipped

		int capX = math::clamp((int)(glMx - iCapW * 0.5f), 0, winW - iCapW);
		int capY = math::clamp((int)(glMy - iCapH * 0.5f), 0, winH - iCapH);

		// Read from back buffer. Since NanoVG batches all draws until nvgEndFrame,
		// the back buffer holds the previous frame. The capture area is always clear
		// because the display circle is offset far enough never to overlap it.
		std::vector<uint8_t> pixels(iCapW * iCapH * 4);
		GLint packAlign;
		glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(capX, capY, iCapW, iCapH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glPixelStorei(GL_PACK_ALIGNMENT, packAlign);

		// Flip Y (GL bottom-left → NVG top-left)
		std::vector<uint8_t> flipped(pixels.size());
		int rowBytes = iCapW * 4;
		for (int row = 0; row < iCapH; row++) {
			memcpy(flipped.data() + row * rowBytes,
				pixels.data() + (iCapH - 1 - row) * rowBytes, rowBytes);
		}

		if (capturedImage < 0 || capturedImageW != iCapW || capturedImageH != iCapH) {
			if (capturedImage >= 0) nvgDeleteImage(args.vg, capturedImage);
			capturedImage = nvgCreateImageRGBA(args.vg, iCapW, iCapH, 0, flipped.data());
			capturedImageW = iCapW;
			capturedImageH = iCapH;
		}
		else {
			nvgUpdateImage(args.vg, capturedImage, flipped.data());
		}
		if (capturedImage < 0) return;

		// Map the captured patch (logical extent 2*captureR, centered on mousePos)
		// onto the display circle (logical extent 2*radius, centered on displayCenter).
		// ox/oy: where the top-left of the patch appears in screen space.
		float ox = displayCenter.x - radius;
		float oy = displayCenter.y - radius;
		float ex = radius * 2.f;
		float ey = radius * 2.f;

		NVGpaint imgPaint = nvgImagePattern(args.vg, ox, oy, ex, ey, 0.f, capturedImage, 1.f);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, displayCenter.x, displayCenter.y, radius);
		nvgFillPaint(args.vg, imgPaint);
		nvgFill(args.vg);

		// Edge vignette
		NVGpaint vignette = nvgRadialGradient(args.vg, displayCenter.x, displayCenter.y,
			radius * 0.65f, radius,
			nvgRGBAf(0.f, 0.f, 0.f, 0.f), nvgRGBAf(0.f, 0.f, 0.f, 0.3f));
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, displayCenter.x, displayCenter.y, radius);
		nvgFillPaint(args.vg, vignette);
		nvgFill(args.vg);

		// Rim
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, displayCenter.x, displayCenter.y, radius);
		nvgStrokeWidth(args.vg, 3.f);
		nvgStrokeColor(args.vg, nvgRGBAf(0.2f, 0.2f, 0.2f, 0.8f));
		nvgStroke(args.vg);

		/*
		// Small crosshair dot on the cursor to show what's being magnified
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, mousePos.x, mousePos.y, 3.f);
		nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.8f));
		nvgFill(args.vg);
		*/
	}
};


struct MeWidget : ThemedModuleWidget<MeModule>, OverlayMessageProvider {
	bool active = false;
	Widget* lastSelectedWidget = NULL;
	ParamWidget* pw = NULL;
	int p = -1;
	OverlayMagnifierWidget* magnifierOverlay = NULL;

	MeWidget(MeModule* module)
		: ThemedModuleWidget<MeModule>(module, "Me", "", true) {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(15.f, 330.0f), module, MeModule::LIGHT_ACTIVE));

		if (module) {
			active = registerSingleton("Me", this);
			if (active) {
				OverlayMessageWidget::registerProvider(this);
			}
		}

		magnifierOverlay = new OverlayMagnifierWidget;
		APP->scene->addChild(magnifierOverlay);
	}

	~MeWidget() {
		if (module && active) {
			unregisterSingleton("Me", this);
			OverlayMessageWidget::unregisterProvider(this);
		}
		if (magnifierOverlay) {
			magnifierOverlay->requestDelete();
			magnifierOverlay = NULL;
		}
	}

	void step() override {
		ThemedModuleWidget<MeModule>::step();
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

		// Magnifier settings
		struct MagnifierLabel : MenuLabel {
			MagnifierLabel() { text = "Magnifier"; }
			~MagnifierLabel() { pluginSettings.saveToJson(); }
		};

		struct LearnHotkeyItem : MenuItem {
			OverlayMagnifierWidget* overlay;
			void onAction(const event::Action& e) override {
				overlay->learning = !overlay->learning;
				e.consume(this);
			}
			void step() override {
				if (overlay->learning) {
					text = "Learning... (press key)";
					rightText = "";
				}
				else {
					text = "Hotkey";
					if (pluginSettings.magnifierKey >= 0) {
						std::string mods;
						if (pluginSettings.magnifierMods & GLFW_MOD_ALT) mods += RACK_MOD_ALT_NAME "+";
						if (pluginSettings.magnifierMods & RACK_MOD_CTRL) mods += RACK_MOD_CTRL_NAME "+";
						if (pluginSettings.magnifierMods & GLFW_MOD_SHIFT) mods += "Shift+";
						rightText = mods + keyName(pluginSettings.magnifierKey);
					}
					else {
						rightText = "(none)";
					}
				}
				MenuItem::step();
			}
		};

		struct ClearHotkeyItem : MenuItem {
			void onAction(const event::Action& e) override {
				pluginSettings.magnifierKey = -1;
				pluginSettings.magnifierMods = 0;
				pluginSettings.saveToJson();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(new MagnifierLabel);
		LearnHotkeyItem* learnItem = construct<LearnHotkeyItem>(&LearnHotkeyItem::overlay, magnifierOverlay);
		menu->addChild(learnItem);
		menu->addChild(construct<ClearHotkeyItem>(&MenuItem::text, "Clear hotkey"));
		menu->addChild(Rack::createPtrSlider(&pluginSettings.magnifierRadius, 40.f, 300.f, 120.f, "Size (radius)", " px", 1.f, 140.0f));
		menu->addChild(Rack::createPtrSlider(&pluginSettings.magnifierZoom, 1.5f, 8.f, 3.f, "Zoom factor", "x", 1.f, 140.0f));
	}
};

} // namespace Me
} // namespace StoermelderPackOne

Model* modelMe = createModel<StoermelderPackOne::Me::MeModule, StoermelderPackOne::Me::MeWidget>("Me");