#include "plugin.hpp"
#include "StripIdFixModule.hpp"

namespace StoermelderPackOne {
namespace Glue {

const static NVGcolor LABEL_COLOR_YELLOW = nvgRGB(0xdc, 0xff, 0x46);
const static NVGcolor LABEL_COLOR_RED = nvgRGB(0xff, 0x74, 0x55);
const static NVGcolor LABEL_COLOR_CYAN = nvgRGB(0x7a, 0xfc, 0xff);
const static NVGcolor LABEL_COLOR_GREEN = nvgRGB(0x1b, 0xa8, 0xb1);
const static NVGcolor LABEL_COLOR_PINK = nvgRGB(0xff, 0x65, 0xa3);
const static NVGcolor LABEL_COLOR_WHITE = nvgRGB(0xfa, 0xfa, 0xfa);

const static NVGcolor LABEL_FONTCOLOR_DEFAULT = nvgRGB(0x08, 0x08, 0x08);
const static NVGcolor LABEL_FONTCOLOR_WHITE = nvgRGB(0xf8, 0xf8, 0xf8);

const static float LABEL_OPACITY_MAX = 1.0f;
const static float LABEL_OPACITY_MIN = 0.2f;
const static float LABEL_OPACITY_STEP = 0.05f;

const static float LABEL_WIDTH_MAX = 180.f;
const static float LABEL_WIDTH_MIN = 20.f;
const static float LABEL_WIDTH_DEFAULT = 80.f;

const static float LABEL_SIZE_MAX = 24.f;
const static float LABEL_SIZE_MIN = 8.f;
const static float LABEL_SIZE_DEFAULT = 16.f;

const static float LABEL_SKEW_MAX = 3.5f;


const std::string WHITESPACE = " \n\r\t\f\v";

std::string ltrim(const std::string& s) {
	size_t start = s.find_first_not_of(WHITESPACE);
	return (start == std::string::npos) ? "" : s.substr(start);
}

std::string rtrim(const std::string& s) {
	size_t end = s.find_last_not_of(WHITESPACE);
	return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string trim(const std::string& s) {
	return rtrim(ltrim(s));
}


struct Label {
	int moduleId;
	float x = 0.f;
	float y = 0.f;
	float width = LABEL_WIDTH_DEFAULT;
	float size = LABEL_SIZE_DEFAULT;
	float angle = 0.f;
	float skew = 0.f;
	float opacity = 1.f;
	int font = 0;
	std::string text;
	NVGcolor color = LABEL_COLOR_YELLOW;
	NVGcolor fontColor = LABEL_FONTCOLOR_DEFAULT;
};


struct GlueModule : Module, StripIdFixModule {
	enum ParamIds {
		PARAM_UNLOCK,
		PARAM_ADD_LABEL,
		PARAM_OPACITY_PLUS,
		PARAM_OPACITY_MINUS,
		PARAM_HIDE,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_LEARN,
		LIGHT_LOCK,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] the list of labels */
	std::list<Label*> labels;

	/** [Stored to JSON] default size for new labels */
	float defaultSize;
	/** [Stored to JSON] default width for new labels */
	float defaultWidth;
	/** [Stored to JSON] default angle for new labels */
	float defaultAngle;
	/** [Stored to JSON] default opacity for new labels */
	float defaultOpacity;
	/** [Stored to JSON] default color for new labels */
	NVGcolor defaultColor;
	/** [Stored to JSON] default font for new labels */
	int defaultFont;
	/** [Stored to JSON] */
	NVGcolor defaultFontColor;
	/** [Stored to JSON] */
	bool skewLabels;

	bool resetRequested = false;

	GlueModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_UNLOCK, 0.f, 1.f, 0.f, "Unlock labels for editing");
		configParam<TriggerParamQuantity>(PARAM_ADD_LABEL, 0.f, 1.f, 0.f, "Add label (Ctrl+A)");
		configParam<TriggerParamQuantity>(PARAM_OPACITY_PLUS, 0.f, 1.f, 0.f, string::f("Increase overall opacity by %i%%", int(LABEL_OPACITY_STEP * 100)));
		configParam<TriggerParamQuantity>(PARAM_OPACITY_MINUS, 0.f, 1.f, 0.f, string::f("Decrease overall opacity by %i%%", int(LABEL_OPACITY_STEP * 100)));
		configParam<TriggerParamQuantity>(PARAM_HIDE, 0.f, 1.f, 0.f, "Hide labels");
		onReset();
	}

	~GlueModule() {
		for (Label* l : labels) {
			delete l;
		}
		labels.clear();
	}

	void onReset() override {
		Module::onReset();
		for (Label* l : labels) {
			delete l;
		}
		labels.clear();
		defaultSize = LABEL_SIZE_DEFAULT;
		defaultWidth = LABEL_WIDTH_DEFAULT;
		defaultAngle = 0.f;
		defaultOpacity = LABEL_OPACITY_MAX;
		defaultColor = LABEL_COLOR_YELLOW;
		defaultFont = 0;
		defaultFontColor = LABEL_FONTCOLOR_DEFAULT;
		skewLabels = true;
		resetRequested = true;
	}

	Label* addLabel() {
		Label* l = new Label;
		l->size = defaultSize;
		l->width = defaultWidth;
		l->angle = defaultAngle;
		l->skew = random::normal() * LABEL_SKEW_MAX;
		l->color = defaultColor;
		l->opacity = defaultOpacity;
		l->font = defaultFont;
		l->fontColor = defaultFontColor;
		labels.push_back(l);
		return l;
	}

	void removeLabel(Label* l) {
		// Make sure the widget is deleted before!
		labels.remove(l);
		delete l;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "defaultSize", json_real(defaultSize));
		json_object_set_new(rootJ, "defaultWidth", json_real(defaultWidth));
		json_object_set_new(rootJ, "defaultAngle", json_real(defaultAngle));
		json_object_set_new(rootJ, "defaultOpacity", json_real(defaultOpacity));
		json_object_set_new(rootJ, "defaultColor", json_string(color::toHexString(defaultColor).c_str()));
		json_object_set_new(rootJ, "defaultFont", json_integer(defaultFont));
		json_object_set_new(rootJ, "defaultFontColor", json_string(color::toHexString(defaultFontColor).c_str()));
		json_object_set_new(rootJ, "skewLabels", json_boolean(skewLabels));

		json_t* labelsJ = json_array();
		for (Label* l : labels) {
			json_t* labelJ = json_object();
			json_object_set_new(labelJ, "moduleId", json_integer(l->moduleId));
			json_object_set_new(labelJ, "x", json_real(l->x));
			json_object_set_new(labelJ, "y", json_real(l->y));
			json_object_set_new(labelJ, "angle", json_real(l->angle));
			json_object_set_new(labelJ, "skew", json_real(l->skew));
			json_object_set_new(labelJ, "opacity", json_real(l->opacity));
			json_object_set_new(labelJ, "width", json_real(l->width));
			json_object_set_new(labelJ, "size", json_real(l->size));
			json_object_set_new(labelJ, "text", json_string(l->text.c_str()));
			json_object_set_new(labelJ, "color", json_string(color::toHexString(l->color).c_str()));
			json_object_set_new(labelJ, "font", json_integer(l->font));
			json_object_set_new(labelJ, "fontColor", json_string(color::toHexString(l->fontColor).c_str()));
			json_array_append_new(labelsJ, labelJ);
		}
		json_object_set_new(rootJ, "labels", labelsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		defaultSize = json_real_value(json_object_get(rootJ, "defaultSize"));
		defaultWidth = json_real_value(json_object_get(rootJ, "defaultWidth"));
		defaultAngle = json_real_value(json_object_get(rootJ, "defaultAngle"));
		defaultOpacity = json_real_value(json_object_get(rootJ, "defaultOpacity"));
		json_t* defaultColorJ = json_object_get(rootJ, "defaultColor");
		if (defaultColorJ) defaultColor = color::fromHexString(json_string_value(defaultColorJ));
		defaultFont = json_integer_value(json_object_get(rootJ, "defaultFont"));
		json_t* defaultFontColorJ = json_object_get(rootJ, "defaultFontColor");
		if (defaultFontColorJ) defaultFontColor = color::fromHexString(json_string_value(defaultFontColorJ));
		skewLabels = json_boolean_value(json_object_get(rootJ, "skewLabels"));

		// Hack for preventing duplicating this module
		if (APP->engine->getModule(id) != NULL && !idFixHasMap()) return;

		for (Label* l : labels) {
			delete l;
		}
		labels.clear();
		resetRequested = true;

		json_t* labelsJ = json_object_get(rootJ, "labels");
		if (labelsJ) {
			size_t labelIdx;
			json_t* labelJ;
			json_array_foreach(labelsJ, labelIdx, labelJ) {
				int moduleId = json_integer_value(json_object_get(labelJ, "moduleId"));
				moduleId = idFix(moduleId);
				if (moduleId < 0) continue;
				
				Label* l = addLabel();
				l->moduleId = moduleId;
				l->x = json_real_value(json_object_get(labelJ, "x"));
				l->y = json_real_value(json_object_get(labelJ, "y"));
				l->angle = json_real_value(json_object_get(labelJ, "angle"));
				l->skew = json_real_value(json_object_get(labelJ, "skew"));
				l->opacity = json_real_value(json_object_get(labelJ, "opacity"));
				l->width = json_real_value(json_object_get(labelJ, "width"));
				l->size = json_real_value(json_object_get(labelJ, "size"));
				json_t* textJ = json_object_get(labelJ, "text");
				if (textJ) l->text = json_string_value(textJ);
				l->color = color::fromHexString(json_string_value(json_object_get(labelJ, "color")));
				l->font = json_integer_value(json_object_get(labelJ, "font"));
				json_t* fontColorJ = json_object_get(labelJ, "fontColor");
				if (fontColorJ) l->fontColor = color::fromHexString(json_string_value(fontColorJ));
			}
		}

		idFixClearMap();
		params[PARAM_UNLOCK].setValue(0.f);
	}
};



struct LabelDrawWidget : TransparentWidget {
	std::shared_ptr<Font> font[2];
	Label* label;
	Vec rotatedSize;

	LabelDrawWidget() {
		font[0] = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		font[1] = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/RedkostComic.otf"));
	}

	void draw(const Widget::DrawArgs& args) override {
		if (!label) return;

		Rect d = Rect(Vec(0.f, 0.f), rotatedSize);

		// Draw shadow
		nvgBeginPath(args.vg);
		float r = 4; // Blur radius
		float c = 4; // Corner radius
		math::Vec b = math::Vec(-2.f, -2.f); // Offset from each corner
		nvgRect(args.vg, d.pos.x + b.x - r, d.pos.y + b.y - r, d.size.x - 2 * b.x + 2 * r, d.size.y - 2 * b.y + 2 * r);
		NVGcolor shadowColor = nvgRGBAf(0.f, 0.f, 0.f, 0.1f);
		NVGcolor transparentColor = nvgRGBAf(0.f, 0.f, 0.f, 0.f);
		nvgFillPaint(args.vg, nvgBoxGradient(args.vg, d.pos.x + b.x, d.pos.y + b.y, d.size.x - 2 * b.x, d.size.y - 2 * b.y, c, r, shadowColor, transparentColor));
		nvgFill(args.vg);

		// Draw label
		nvgBeginPath(args.vg);
		nvgRect(args.vg, d.pos.x, d.pos.y, d.size.x, d.size.y);
		nvgFillColor(args.vg, color::alpha(label->color, label->opacity));
		nvgFill(args.vg);

		// Draw text
		if (label->text.length() > 0) {
			nvgFontSize(args.vg, label->size);
			nvgFontFaceId(args.vg, font[label->font]->handle);
			nvgTextLetterSpacing(args.vg, -1.2f);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(args.vg, color::alpha(label->fontColor, label->opacity));
			NVGtextRow textRow;
			nvgTextBreakLines(args.vg, label->text.c_str(), NULL, d.size.x, &textRow, 1);
			nvgTextBox(args.vg, d.pos.x, d.pos.y + 0.2f, d.size.x, textRow.start, textRow.end);
		}
	}
};


struct LabelWidget : widget::TransparentWidget {
	Label* label;

	bool requestedDelete = false;
	bool requestedDuplicate = false;
	bool editMode = false;
	bool skew = false;

	math::Vec dragPos;

	LabelDrawWidget* widget;
	TransformWidget* transformWidget;
	float lastAngle = 360.f;
	float lastSize = 0.f;
	float lastWidth = 0.f;
	bool lastSkew = false;

	LabelWidget(Label* label) {
		this->label = label;

		widget = new LabelDrawWidget;
		widget->label = label;
		transformWidget = new TransformWidget;
		transformWidget->addChild(widget);
		addChild(transformWidget);
	}

	void step() override {
		ModuleWidget* mw = APP->scene->rack->getModule(label->moduleId);
		// Request label deletion if widget doen not exist anymore
		if (!mw) {
			requestedDelete = true;
			return;
		}

		// Clamp values
		label->x = clamp(label->x, -label->width / 2.f, mw->box.size.x - label->width / 2.f);
		label->y = clamp(label->y, -label->size / 2.f, mw->box.size.y - label->size / 2.f);
		label->opacity = clamp(label->opacity, 0.f, 1.f);
	
		// Move according to the owning module
		if (label->angle == 0 || label->angle == 180) {
			box.size = Vec(label->width, label->size);
			box.pos = mw->box.pos.plus(Vec(label->x, label->y));
		}
		else {
			box.size = Vec(label->size, label->width);
			box.pos = mw->box.pos.plus(Vec(label->x + label->width / 2.f - label->size / 2.f, label->y - label->width / 2.f + label->size / 2.f));;
		}

		widget->rotatedSize = Vec(label->width, label->size);
		widget->box.size = box.size;

		// Rotate
		if (label->angle != lastAngle || label->width != lastWidth || label->size != lastSize || lastSkew != skew) {
			float angle = label->angle + (skew ? label->skew : 0.f);
			transformWidget->identity();
			transformWidget->translate(Vec(box.size.x / 2.f, box.size.y / 2.f));
			transformWidget->rotate(M_PI/2.f * angle / 90.f);
			transformWidget->translate(Vec(- label->width / 2.f, - label->size / 2.f));
			lastAngle = label->angle;
			lastWidth = label->width;
			lastSize = label->size;
			lastSkew = skew;
		}

		TransparentWidget::step();
	}

	void onButton(const event::Button& e) override {
		if (editMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (e.action == GLFW_PRESS) {
				if (box.zeroPos().isContaining(e.pos))
					e.consume(this);
			}
		}
		if (editMode && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		TransparentWidget::onButton(e);
	}

	void onDragStart(const event::DragStart& e) override {
		if (editMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			dragPos = APP->scene->rack->mousePos.minus(parent->box.pos);
			dragPos = dragPos.minus(Vec(label->x, label->y));
			e.consume(this);
		}
		TransparentWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove& e) override {
		if (editMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			math::Vec npos = APP->scene->rack->mousePos.minus(parent->box.pos);
			math::Vec pos = npos.minus(dragPos);
			label->x = pos.x;
			label->y = pos.y;
			e.consume(this);
		}
		TransparentWidget::onDragMove(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();

		struct LabelField : ui::TextField {
			Label* l;
			// Needed for input-blur on submenu
			bool textSelected = true;
			LabelField() {
				box.size.x = 160.f;
				placeholder = "Label";
			}
			LabelField* setLabel(Label* l) {
				this->l = l;
				setText(l->text);
				selectAll();
				return this;
			}
			void step() override {
				// Keep selected
				if (textSelected) APP->event->setSelected(this);
				TextField::step();
				l->text = text;
			}
			void onSelectKey(const event::SelectKey& e) override {
				if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
					l->text = text;
					ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
					overlay->requestDelete();
					e.consume(this);
				}
				if (!e.getTarget()) {
					ui::TextField::onSelectKey(e);
				}
			}
		};

		struct AppearanceItem : MenuItem {
			Label* label;
			bool* textSelected;
			AppearanceItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct SizeSlider : ui::Slider {
					struct SizeQuantity : Quantity {
						Label* label;
						SizeQuantity(Label* label) {
							this->label = label;
						}
						void setValue(float value) override {
							label->size = math::clamp(value, LABEL_SIZE_MIN, LABEL_SIZE_MAX);
						}
						float getValue() override {
							return label->size;
						}
						float getDefaultValue() override {
							return LABEL_SIZE_DEFAULT;
						}
						std::string getLabel() override {
							return "Size";
						}
						int getDisplayPrecision() override {
							return 3;
						}
						float getMaxValue() override {
							return LABEL_SIZE_MAX;
						}
						float getMinValue() override {
							return LABEL_SIZE_MIN;
						}
					};

					Label* label;
					SizeSlider(Label* label) {
						this->label = label;
						box.size.x = 140.0f;
						quantity = new SizeQuantity(label);
					}
					~SizeSlider() {
						delete quantity;
					}
				};

				struct WidthSlider : ui::Slider {
					struct WidthQuantity : Quantity {
						Label* label;
						WidthQuantity(Label* label) {
							this->label = label;
						}
						void setValue(float value) override {
							label->width = math::clamp(value, LABEL_WIDTH_MIN, LABEL_WIDTH_MAX);
						}
						float getValue() override {
							return label->width;
						}
						float getDefaultValue() override {
							return LABEL_WIDTH_DEFAULT;
						}
						std::string getLabel() override {
							return "Width";
						}
						int getDisplayPrecision() override {
							return 3;
						}
						float getMaxValue() override {
							return LABEL_WIDTH_MAX;
						}
						float getMinValue() override {
							return LABEL_WIDTH_MIN;
						}
					};

					Label* label;
					WidthSlider(Label* label) {
						this->label = label;
						box.size.x = 140.0f;
						quantity = new WidthQuantity(label);
					}
					~WidthSlider() {
						delete quantity;
					}
				};

				struct OpacitySlider : ui::Slider {
					struct OpacityQuantity : Quantity {
						Label* label;
						OpacityQuantity(Label* label) {
							this->label = label;
						}
						void setValue(float value) override {
							label->opacity = math::clamp(value, LABEL_OPACITY_MIN, LABEL_OPACITY_MAX);
						}
						float getValue() override {
							return label->opacity;
						}
						float getDefaultValue() override {
							return 1.0f;
						}
						float getDisplayValue() override {
							return getValue() * 100;
						}
						void setDisplayValue(float displayValue) override {
							setValue(displayValue / 100);
						}
						std::string getLabel() override {
							return "Opacity";
						}
						std::string getUnit() override {
							return "%";
						}
						int getDisplayPrecision() override {
							return 3;
						}
						float getMaxValue() override {
							return LABEL_OPACITY_MAX;
						}
						float getMinValue() override {
							return LABEL_OPACITY_MIN;
						}
					};

					OpacitySlider(Label* label) {
						box.size.x = 140.0f;
						quantity = new OpacityQuantity(label);
					}
					~OpacitySlider() {
						delete quantity;
					}
				};

				struct RotateItem : MenuItem {
					Label* label;
					float angle;
					void onAction(const event::Action& e) override {
						label->angle = angle;
					}
					void step() override {
						rightText = label->angle == angle ? "✔" : "";
						MenuItem::step();
					}
				};

				struct FontItem : MenuItem {
					Label* label;
					int font;
					void onAction(const event::Action& e) override {
						label->font = font;
					}
					void step() override {
						rightText = label->font == font ? "✔" : "";
						MenuItem::step();
					}
				};

				struct FontColorItem : MenuItem {
					Label* label;
					NVGcolor color;
					void onAction(const event::Action& e) override {
						label->fontColor = color;
					}
					void step() override {
						rightText = color::toHexString(label->fontColor) == color::toHexString(color) ? "✔" : "";
						MenuItem::step();
					}
				};

				struct CustomFontColorField : ui::TextField {
					Label* label;
					bool* textSelected;
					CustomFontColorField() {
						box.size.x = 80.f;
						placeholder = color::toHexString(LABEL_FONTCOLOR_DEFAULT);
					}
					void onSelectKey(const event::SelectKey& e) override {
						if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
							label->fontColor = color::fromHexString(trim(text));
							ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
							overlay->requestDelete();
							e.consume(this);
						}
						if (!e.getTarget()) {
							ui::TextField::onSelectKey(e);
						}
					}
					void onButton(const event::Button& e) override {
						*textSelected = false;
						TextField::onButton(e);
					}
				};

				struct ColorItem : MenuItem {
					Label* label;
					NVGcolor color;
					void onAction(const event::Action& e) override {
						label->color = color;
					}
					void step() override {
						rightText = color::toHexString(label->color) == color::toHexString(color) ? "✔" : "";
						MenuItem::step();
					}
				};

				struct CustomColorField : ui::TextField {
					Label* label;
					bool* textSelected;
					CustomColorField() {
						box.size.x = 80.f;
						placeholder = "#ffffff";
					}
					void onSelectKey(const event::SelectKey& e) override {
						if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
							label->color = color::fromHexString(trim(text));
							ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
							overlay->requestDelete();
							e.consume(this);
						}
						if (!e.getTarget()) {
							ui::TextField::onSelectKey(e);
						}
					}
					void onButton(const event::Button& e) override {
						*textSelected = false;
						TextField::onButton(e);
					}
				};

				menu->addChild(new SizeSlider(label));
				menu->addChild(new WidthSlider(label));
				menu->addChild(new OpacitySlider(label));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Rotation"));
				menu->addChild(construct<RotateItem>(&MenuItem::text, "0°", &RotateItem::label, label, &RotateItem::angle, 0.f));
				menu->addChild(construct<RotateItem>(&MenuItem::text, "90°", &RotateItem::label, label, &RotateItem::angle, 90.f));
				menu->addChild(construct<RotateItem>(&MenuItem::text, "270°", &RotateItem::label, label, &RotateItem::angle, 270.f));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Font"));
				menu->addChild(construct<FontItem>(&MenuItem::text, "Default", &FontItem::label, label, &FontItem::font, 0));
				menu->addChild(construct<FontItem>(&MenuItem::text, "Handwriting", &FontItem::label, label, &FontItem::font, 1));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Font color"));
				menu->addChild(construct<FontColorItem>(&MenuItem::text, "Black", &FontColorItem::label, label, &FontColorItem::color, LABEL_FONTCOLOR_DEFAULT));
				menu->addChild(construct<FontColorItem>(&MenuItem::text, "White", &FontColorItem::label, label, &FontColorItem::color, LABEL_FONTCOLOR_WHITE));
				menu->addChild(construct<CustomFontColorField>(&TextField::text, color::toHexString(label->fontColor), &CustomFontColorField::label, label, &CustomFontColorField::textSelected, textSelected));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Color"));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Yellow", &ColorItem::label, label, &ColorItem::color, LABEL_COLOR_YELLOW));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Red", &ColorItem::label, label, &ColorItem::color, LABEL_COLOR_RED));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Cyan", &ColorItem::label, label, &ColorItem::color, LABEL_COLOR_CYAN));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Green", &ColorItem::label, label, &ColorItem::color, LABEL_COLOR_GREEN));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Pink", &ColorItem::label, label, &ColorItem::color, LABEL_COLOR_PINK));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "White", &ColorItem::label, label, &ColorItem::color, LABEL_COLOR_WHITE));
				menu->addChild(construct<CustomColorField>(&TextField::text, color::toHexString(label->color), &CustomColorField::label, label, &CustomColorField::textSelected, textSelected));
				return menu;
			}
		};

		struct LabelDuplicateItem : MenuItem {
			LabelWidget* w;
			void onAction(const event::Action& e) override {
				w->requestedDuplicate = true;
			}
		};

		struct LabelDeleteItem : MenuItem {
			LabelWidget* w;
			void onAction(const event::Action& e) override {
				w->requestedDelete = true;
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Label"));
		LabelField* labelField = construct<LabelField>()->setLabel(label);
		menu->addChild(labelField);
		menu->addChild(construct<AppearanceItem>(&AppearanceItem::text, "Appearance", &AppearanceItem::label, label, &AppearanceItem::textSelected, &labelField->textSelected));
		menu->addChild(construct<LabelDuplicateItem>(&MenuItem::text, "Duplicate", &LabelDuplicateItem::w, this));
		menu->addChild(construct<LabelDeleteItem>(&MenuItem::text, "Delete", &LabelDeleteItem::w, this, &LabelDeleteItem::rightText, "Ctrl+X"));
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (editMode && e.action == GLFW_PRESS && e.mods & GLFW_MOD_CONTROL && e.key == GLFW_KEY_X) {
			requestedDelete = true;
			e.consume(this);
		}
		TransparentWidget::onHoverKey(e);
	}
};



template < typename WIDGET >
struct LabelRemoveAction : history::ModuleAction {
	Label label;
	int moduleId;

	void undo() override {
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		WIDGET* w = dynamic_cast<WIDGET*>(mw);
		assert(w);

		LabelWidget* lw = w->labelContainer->addLabelWidget();
		lw->label->moduleId = label.moduleId;
		lw->label->x = label.x;
		lw->label->y = label.y;
		lw->label->width = label.width;
		lw->label->size = label.size;
		lw->label->angle = label.angle;
		lw->label->skew = label.skew;
		lw->label->color = label.color;
		lw->label->opacity = label.opacity;
		lw->label->text = label.text;
		lw->label->font = label.font;
		lw->label->fontColor = label.fontColor;
	}

	void redo() override {
		// Nothing to do here, it's handled as any module removal by LabelContainer
	}
};

struct DoubleUndoAction : history::ModuleAction {
	void undo() override {
		APP->history->undo();
	}
	void redo() override {
		APP->history->redo();
	}
};


struct GlueWidget;

struct LabelContainer : widget::Widget {
	GlueModule* module;
	std::list<Label*> labelsToBeDeleted;

	/** used when duplicating an existing label */
	Label* labelTemplate = NULL;

	/** labels locked? */
	bool editMode = false;
	/** labels hidden? gets its value from the module's parameter */
	bool hideMode = false;
	/** learning a module for a new label? */
	bool learnMode = false;

	ModuleWidget* mw;

	void step() override {
		Widget::step();
		if (!module) return;

		if (module->resetRequested) {
			this->clearChildren();
			for (Label* l : module->labels) {
				LabelWidget* lw = new LabelWidget(l);
				addChild(lw);
			}
			module->resetRequested = false;
			learnMode = false;
			editMode = false;
		}

		// Learn module
		if (learnMode) {
			Widget* w = APP->event->getSelectedWidget();
			addLabelAtMousePos(w);
		}

		// Traverse labels, collect delete-requests
		for (Widget* w : children) {
			LabelWidget* lw = dynamic_cast<LabelWidget*>(w);
			if (!lw) continue;
			if (lw->requestedDelete) {
				labelsToBeDeleted.push_back(lw->label);
				labelTemplate = NULL;
			}
			if (lw->requestedDuplicate) {
				lw->requestedDuplicate = false;
				labelTemplate = lw->label;
				learnMode = true;
			}
			lw->editMode = editMode;
			lw->skew = module->skewLabels;
		}

		if (labelsToBeDeleted.size() > 0) {
			history::ComplexAction* complexAction = new history::ComplexAction;
			complexAction->name = "remove module";
			// First, undo "module removal" by a "double undo"
			complexAction->push(new DoubleUndoAction);
			for (Label* l : labelsToBeDeleted) {
				LabelRemoveAction<GlueWidget>* a = new LabelRemoveAction<GlueWidget>;
				a->label = *l;
				a->moduleId = mw->module->id;
				complexAction->push(a);
				removeLabelWidget(l);
			}
			// Second, undo the label removal
			APP->history->push(complexAction);

			labelsToBeDeleted.clear();
		}

		module->lights[GlueModule::LIGHT_LEARN].setBrightness(learnMode);
		module->lights[GlueModule::LIGHT_LOCK].setBrightness(!editMode);
	}

	void draw(const DrawArgs& args) override {
		if (!hideMode) Widget::draw(args);
	}

	LabelWidget* getLabelWidget(Label* l) {
		for (Widget* w : children) {
			LabelWidget* lw = dynamic_cast<LabelWidget*>(w);
			if (!lw) continue;
			if (lw->label == l) return lw;
		}
		return NULL;
	}

	LabelWidget* addLabelWidget() {
		Label* l = module->addLabel();
		if (labelTemplate) {
			l->size = labelTemplate->size;
			l->width = labelTemplate->width;
			l->angle = labelTemplate->angle;
			l->color = labelTemplate->color;
			l->opacity = labelTemplate->opacity;
			l->font = labelTemplate->font;
			l->fontColor = labelTemplate->fontColor;
			labelTemplate = NULL;
		}
		LabelWidget* lw = new LabelWidget(l);
		addChild(lw);
		return lw;
	}

	void removeLabelWidget(Label* l) {
		LabelWidget* lw = getLabelWidget(l);
		if (!lw) return;
		removeChild(lw);
		delete lw;
		module->removeLabel(l);
	}

	void addLabelAtMousePos(Widget* w) {
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw || mw == this->mw) return;
		Module* m = mw->module;
		if (!m) return;

		// Create new label
		LabelWidget* lw = addLabelWidget();
		lw->label->text = m->model->name;
		lw->label->moduleId = m->id;

		// Move label to mouse click position
		Vec pos = APP->scene->rack->mousePos;
		pos = pos.minus(mw->box.pos);
		lw->label->x = pos.x - lw->label->width / 2.f;
		lw->label->y = pos.y - lw->label->size / 2.f;

		// Enable edit mode
		editMode = true;
		learnMode = false;
	}

	void toggleLearnMode() {
		if (!hideMode) learnMode ^= true;
	}

	void toggleEditMode() {
		if (!hideMode) editMode ^= true;
	}

	void toggleHideMode(bool doHide) {
		hideMode = doHide;
		if (hideMode) {
			editMode = false;
			learnMode = false;
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (editMode && e.action == GLFW_PRESS && e.key == GLFW_KEY_A && (e.mods & RACK_MOD_MASK) == GLFW_MOD_CONTROL) {
			// Learn module
			Widget* w = APP->event->getHoveredWidget();
			addLabelAtMousePos(w);
			e.consume(this);
		}
		Widget::onHoverKey(e);
	}
};


struct LabelButton : TL1105 {
	LabelContainer* labelContainer;
	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			labelContainer->toggleLearnMode();
		}
		TL1105::onButton(e);
	}
};

struct LockButton : TL1105 {
	LabelContainer* labelContainer;
	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			labelContainer->toggleEditMode();
		}
		TL1105::onButton(e);
	}
};

struct OpacityPlusButton : TL1105 {
	GlueModule* module;
	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			for (Label* l : module->labels)
				l->opacity = std::min(l->opacity + LABEL_OPACITY_STEP, LABEL_OPACITY_MAX);
		}
		TL1105::onButton(e);
	}
};

struct OpacityMinusButton : TL1105 {
	GlueModule* module;
	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			for (Label* l : module->labels)
				l->opacity = std::max(l->opacity - LABEL_OPACITY_STEP, LABEL_OPACITY_MIN);
		}
		TL1105::onButton(e);
	}
};

struct HideSwitch : CKSS {
	LabelContainer* labelContainer = NULL;
	void step() override {
		if (labelContainer) labelContainer->toggleHideMode(paramQuantity->getValue() > 0.f);
		CKSS::step();
	}
};


struct GlueWidget : ThemedModuleWidget<GlueModule> {
	LabelContainer* labelContainer = NULL;

	template <class TParamWidget>
	TParamWidget* createParamCentered(math::Vec pos, engine::Module* module, int paramId) {
		TParamWidget* pw = rack::createParamCentered<TParamWidget>(pos, module, paramId);
		pw->labelContainer = labelContainer;
		return pw;
	}

	GlueWidget(GlueModule* module)
		: ThemedModuleWidget<GlueModule>(module, "Glue") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			labelContainer = new LabelContainer;
			labelContainer->module = module;
			labelContainer->mw = this;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(labelContainer);

			// Move the cable-widget to the end, labels should appear below cables
			// NB: this should be considered unstable API
			std::list<Widget*>::iterator it;
			for (it = APP->scene->rack->children.begin(); it != APP->scene->rack->children.end(); ++it){
				if (*it == APP->scene->rack->cableContainer) break;
			}
			if (it != APP->scene->rack->children.end()) {
				APP->scene->rack->children.splice(APP->scene->rack->children.end(), APP->scene->rack->children, it);
			}
		}

		addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(22.5f, 143.5f), module, GlueModule::LIGHT_LEARN));
		addParam(createParamCentered<LabelButton>(Vec(22.5f, 158.8f), module, GlueModule::PARAM_ADD_LABEL));

		addChild(createLightCentered<TinyLight<YellowLight>>(Vec(22.5f, 188.3f), module, GlueModule::LIGHT_LOCK));
		addParam(createParamCentered<LockButton>(Vec(22.5f, 203.6f), module, GlueModule::PARAM_UNLOCK));

		OpacityPlusButton* b1 = rack::createParamCentered<OpacityPlusButton>(Vec(22.5f, 254.7f), module, GlueModule::PARAM_OPACITY_PLUS);
		b1->module = module;
		addParam(b1);
		OpacityMinusButton* b2 = rack::createParamCentered<OpacityMinusButton>(Vec(22.5f, 286.3f), module, GlueModule::PARAM_OPACITY_MINUS);
		b2->module = module;
		addParam(b2);
		addParam(createParamCentered<HideSwitch>(Vec(22.5f, 326.7f), module, GlueModule::PARAM_HIDE));
	}

	~GlueWidget() {
		if (labelContainer) {
			APP->scene->rack->removeChild(labelContainer);
			delete labelContainer;
		}
	}

	void fromJson(json_t* rootJ) override {
		// Handle data-storage from previous builds
		json_t* dataJ = json_object_get(rootJ, "data");
		json_t* labelsJ = json_object_get(rootJ, "labels");
		if (dataJ && labelsJ) {
			json_object_set(dataJ, "labels", labelsJ);
			json_object_set(rootJ, "labels", NULL);
		}
		ThemedModuleWidget<GlueModule>::fromJson(rootJ);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<GlueModule>::appendContextMenu(menu);

		struct DefaultAppearanceMenuItem : MenuItem {
			GlueModule* module;
			DefaultAppearanceMenuItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct SizeSlider : ui::Slider {
					struct SizeQuantity : Quantity {
						GlueModule* module;
						SizeQuantity(GlueModule* module) {
							this->module = module;
						}
						void setValue(float value) override {
							module->defaultSize = math::clamp(value, LABEL_SIZE_MIN, LABEL_SIZE_MAX);
						}
						float getValue() override {
							return module->defaultSize;
						}
						float getDefaultValue() override {
							return LABEL_SIZE_DEFAULT;
						}
						std::string getLabel() override {
							return "Default size";
						}
						int getDisplayPrecision() override {
							return 3;
						}
						float getMaxValue() override {
							return LABEL_SIZE_MAX;
						}
						float getMinValue() override {
							return LABEL_SIZE_MIN;
						}
					};

					SizeSlider(GlueModule* module) {
						box.size.x = 160.0f;
						quantity = new SizeQuantity(module);
					}
					~SizeSlider() {
						delete quantity;
					}
				};

				struct WidthSlider : ui::Slider {
					struct WidthQuantity : Quantity {
						GlueModule* module;
						WidthQuantity(GlueModule* module) {
							this->module = module;
						}
						void setValue(float value) override {
							module->defaultWidth = math::clamp(value, LABEL_WIDTH_MIN, LABEL_WIDTH_MAX);
						}
						float getValue() override {
							return module->defaultWidth;
						}
						float getDefaultValue() override {
							return LABEL_WIDTH_DEFAULT;
						}
						std::string getLabel() override {
							return "Default width";
						}
						int getDisplayPrecision() override {
							return 3;
						}
						float getMaxValue() override {
							return LABEL_WIDTH_MAX;
						}
						float getMinValue() override {
							return LABEL_WIDTH_MIN;
						}
					};

					WidthSlider(GlueModule* module) {
						box.size.x = 160.0f;
						quantity = new WidthQuantity(module);
					}
					~WidthSlider() {
						delete quantity;
					}
				};

				struct OpacitySlider : ui::Slider {
					struct OpacityQuantity : Quantity {
						GlueModule* module;
						OpacityQuantity(GlueModule* module) {
							this->module = module;
						}
						void setValue(float value) override {
							module->defaultOpacity = math::clamp(value, LABEL_OPACITY_MIN, LABEL_OPACITY_MAX);
						}
						float getValue() override {
							return module->defaultOpacity;
						}
						float getDefaultValue() override {
							return 1.0f;
						}
						float getDisplayValue() override {
							return getValue() * 100;
						}
						void setDisplayValue(float displayValue) override {
							setValue(displayValue / 100);
						}
						std::string getLabel() override {
							return "Default opacity";
						}
						std::string getUnit() override {
							return "%";
						}
						int getDisplayPrecision() override {
							return 3;
						}
						float getMaxValue() override {
							return LABEL_OPACITY_MAX;
						}
						float getMinValue() override {
							return LABEL_OPACITY_MIN;
						}
					};

					OpacitySlider(GlueModule* module) {
						box.size.x = 160.0f;
						quantity = new OpacityQuantity(module);
					}
					~OpacitySlider() {
						delete quantity;
					}
				};

				struct RotateItem : MenuItem {
					GlueModule* module;
					float angle;
					void onAction(const event::Action& e) override {
						module->defaultAngle = angle;
					}
					void step() override {
						rightText = module->defaultAngle == angle ? "✔" : "";
						MenuItem::step();
					}
				};

				struct FontItem : MenuItem {
					GlueModule* module;
					int font;
					void onAction(const event::Action& e) override {
						module->defaultFont = font;
					}
					void step() override {
						rightText = module->defaultFont == font ? "✔" : "";
						MenuItem::step();
					}
				};

				struct FontColorItem : MenuItem {
					GlueModule* module;
					NVGcolor color;
					void onAction(const event::Action& e) override {
						module->defaultFontColor = color;
					}
					void step() override {
						rightText = color::toHexString(module->defaultFontColor) == color::toHexString(color) ? "✔" : "";
						MenuItem::step();
					}
				};

				struct CustomFontColorField : ui::TextField {
					GlueModule* module;
					CustomFontColorField() {
						box.size.x = 80.f;
						placeholder = color::toHexString(LABEL_FONTCOLOR_DEFAULT);
					}
					void onSelectKey(const event::SelectKey& e) override {
						if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
							module->defaultFontColor = color::fromHexString(trim(text));
							ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
							overlay->requestDelete();
							e.consume(this);
						}
						if (!e.getTarget()) {
							ui::TextField::onSelectKey(e);
						}
					}
				};

				struct ColorItem : MenuItem {
					GlueModule* module;
					NVGcolor color;
					void onAction(const event::Action& e) override {
						module->defaultColor = color;
					}
					void step() override {
						rightText = color::toHexString(module->defaultColor) == color::toHexString(color) ? "✔" : "";
						MenuItem::step();
					}
				};

				struct CustomColorField : ui::TextField {
					GlueModule* module;
					CustomColorField() {
						box.size.x = 80.f;
						placeholder = "#ffffff";
					}
					void onSelectKey(const event::SelectKey& e) override {
						if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
							module->defaultColor = color::fromHexString(trim(text));
							ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
							overlay->requestDelete();
							e.consume(this);
						}
						if (!e.getTarget()) {
							ui::TextField::onSelectKey(e);
						}
					}
				};

				menu->addChild(new SizeSlider(module));
				menu->addChild(new WidthSlider(module));
				menu->addChild(new OpacitySlider(module));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Default rotation"));
				menu->addChild(construct<RotateItem>(&MenuItem::text, "0°", &RotateItem::module, module, &RotateItem::angle, 0.f));
				menu->addChild(construct<RotateItem>(&MenuItem::text, "90°", &RotateItem::module, module, &RotateItem::angle, 90.f));
				menu->addChild(construct<RotateItem>(&MenuItem::text, "270°", &RotateItem::module, module, &RotateItem::angle, 270.f));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Default font"));
				menu->addChild(construct<FontItem>(&MenuItem::text, "Default", &FontItem::module, module, &FontItem::font, 0));
				menu->addChild(construct<FontItem>(&MenuItem::text, "Handwriting", &FontItem::module, module, &FontItem::font, 1));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Default font color"));
				menu->addChild(construct<FontColorItem>(&MenuItem::text, "Black", &FontColorItem::module, module, &FontColorItem::color, LABEL_FONTCOLOR_DEFAULT));
				menu->addChild(construct<FontColorItem>(&MenuItem::text, "White", &FontColorItem::module, module, &FontColorItem::color, LABEL_FONTCOLOR_WHITE));
				menu->addChild(construct<CustomFontColorField>(&TextField::text, color::toHexString(module->defaultFontColor), &CustomFontColorField::module, module));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Default color"));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Yellow", &ColorItem::module, module, &ColorItem::color, LABEL_COLOR_YELLOW));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Red", &ColorItem::module, module, &ColorItem::color, LABEL_COLOR_RED));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Cyan", &ColorItem::module, module, &ColorItem::color, LABEL_COLOR_CYAN));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Green", &ColorItem::module, module, &ColorItem::color, LABEL_COLOR_GREEN));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "Pink", &ColorItem::module, module, &ColorItem::color, LABEL_COLOR_PINK));
				menu->addChild(construct<ColorItem>(&MenuItem::text, "White", &ColorItem::module, module, &ColorItem::color, LABEL_COLOR_WHITE));
				menu->addChild(construct<CustomColorField>(&TextField::text, color::toHexString(module->defaultColor), &CustomColorField::module, module));
				return menu;
			}
		};

		struct SkewItem : MenuItem {
			GlueModule* module;
			void onAction(const event::Action& e) override {
				module->skewLabels ^= true;
			}
			void step() override {
				rightText = module->skewLabels ? "✔" : "";
				MenuItem::step();
			}
		};

		struct LabelMenuItem : MenuItem {
			LabelContainer* labelContainer;
			Label* label;
			LabelMenuItem() {
				rightText = RIGHT_ARROW;
			}
			void step() override {
				text = getModuleName() + " - " + label->text;
				MenuItem::step();
			}

			std::string getModuleName() {
				ModuleWidget* mw = APP->scene->rack->getModule(label->moduleId);
				if (!mw) return "<ERROR>";
				Module* m = mw->module;
				if (!m) return "<ERROR>";
				std::string s = mw->model->name;
				return s;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct LabelDeleteItem : MenuItem {
					LabelContainer* labelContainer;
					Label* label;
					void onAction(const event::Action& e) override {
						labelContainer->removeLabelWidget(label);
					}
				};

				menu->addChild(construct<LabelDeleteItem>(&MenuItem::text, "Delete", &LabelDeleteItem::labelContainer, labelContainer, &LabelDeleteItem::label, label));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<DefaultAppearanceMenuItem>(&MenuItem::text, "Label appearance", &DefaultAppearanceMenuItem::module, module));
		menu->addChild(construct<SkewItem>(&MenuItem::text, "Skew labels", &SkewItem::module, module));

		if (module->labels.size() > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Labels"));

			for (Label* l : module->labels) {
				menu->addChild(construct<LabelMenuItem>(&LabelMenuItem::labelContainer, labelContainer, &LabelMenuItem::label, l));
			}
		}
	}
};

} // namespace Glue
} // namespace StoermelderPackOne

Model* modelGlue = createModel<StoermelderPackOne::Glue::GlueModule, StoermelderPackOne::Glue::GlueWidget>("Glue");