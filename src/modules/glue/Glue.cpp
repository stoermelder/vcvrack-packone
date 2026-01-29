#include "../../plugin.hpp"
#include "../../utils/StripIdFixModule.hpp"

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


struct ModuleLabel {
	int64_t moduleId;
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


struct CableLabel {
	int64_t cableId;
	bool atInput = true; // Whether label is at input port (true) or output port (false)
	float width = LABEL_WIDTH_DEFAULT;
	float size = LABEL_SIZE_DEFAULT;
	float distance = 40.f; // Distance from port along cable
	int font = 0;
	std::string text;
	NVGcolor color = LABEL_COLOR_YELLOW; // Auto-set from cable, but can be overridden
	NVGcolor fontColor = LABEL_FONTCOLOR_DEFAULT; // Auto-set for contrast
	
	// Transient port references for tracking during incomplete cable state (not stored to JSON)
	PortWidget* lastOutputPort = NULL;
	PortWidget* lastInputPort = NULL;
	
	// Cache for placement calculations
	Vec cachedOutputPos = Vec(-1.f, -1.f);
	Vec cachedInputPos = Vec(-1.f, -1.f);
	float cachedWidth = 0.f;
	float cachedSize = 0.f;
	float cachedDistance = 0.f;
	bool cachedAtInput = true;
	Vec cachedBoxPos;
	Vec cachedBoxSize;
	float cachedLabelAngle = 0.f;
	Vec cachedRotatedSize;
	bool cacheValid = false;
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
	std::list<ModuleLabel*> moduleLabels;

	/** [Stored to JSON] the list of cable labels */
	std::list<CableLabel*> cableLabels;
	
	/** Transient list of cable labels requested for deletion */
	std::list<CableLabel*> cableLabelsToDelete;

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
		configSwitch(PARAM_UNLOCK, 0.f, 1.f, 0.f, "Unlock labels for editing (" RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+G");
		configSwitch(PARAM_ADD_LABEL, 0.f, 1.f, 0.f, "Add module label (" RACK_MOD_CTRL_NAME "+G)");
		configSwitch(PARAM_OPACITY_PLUS, 0.f, 1.f, 0.f, string::f("Increase overall opacity by %i%%", int(LABEL_OPACITY_STEP * 100)));
		configSwitch(PARAM_OPACITY_MINUS, 0.f, 1.f, 0.f, string::f("Decrease overall opacity by %i%%", int(LABEL_OPACITY_STEP * 100)));
		configSwitch(PARAM_HIDE, 0.f, 1.f, 0.f, "Hide labels");
		onReset();
	}

	~GlueModule() {
		clearLabels();
		clearCableLabels();
	}

	void onReset() override {
		Module::onReset();
		for (ModuleLabel* l : moduleLabels) {
			delete l;
		}
		moduleLabels.clear();
		for (CableLabel* cl : cableLabels) {
			delete cl;
		}
		cableLabels.clear();
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

	ModuleLabel* addModuleLabel() {
		ModuleLabel* l = new ModuleLabel;
		l->size = defaultSize;
		l->width = defaultWidth;
		l->angle = defaultAngle;
		l->skew = random::normal() * LABEL_SKEW_MAX;
		l->color = defaultColor;
		l->opacity = defaultOpacity;
		l->font = defaultFont;
		l->fontColor = defaultFontColor;
		moduleLabels.push_back(l);
		return l;
	}

	void removeModuleLabel(ModuleLabel* l) {
		// Make sure the widget is deleted before!
		moduleLabels.remove(l);
		delete l;
	}

	void clearLabels() {
		for (ModuleLabel* l : moduleLabels) {
			delete l;
		}
		moduleLabels.clear();
		resetRequested = true;
	}

	CableLabel* addCableLabel() {
		CableLabel* cl = new CableLabel;
		cl->size = defaultSize;
		cl->width = defaultWidth;
		cl->font = defaultFont;
		cl->distance = rescale(rack::random::uniform(), 0.f, 1.f, 20.f, 40.f);
		// color and fontColor are auto-set from cable
		cableLabels.push_back(cl);
		return cl;
	}

	void removeCableLabel(CableLabel* cl) {
		cableLabels.remove(cl);
		delete cl;
	}

	void clearCableLabels() {
		for (CableLabel* cl : cableLabels) {
			delete cl;
		}
		cableLabels.clear();
		resetRequested = true;
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
		json_t* labelsJ = moduleLabelToJson();
		json_object_set_new(rootJ, "labels", labelsJ);
		json_t* cableLabelsJ = cableLabelToJson();
		json_object_set_new(rootJ, "cableLabels", cableLabelsJ);
		return rootJ;
	}

	json_t* moduleLabelToJson() {
		json_t* labelsJ = json_array();
		for (ModuleLabel* l : moduleLabels) {
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
		return labelsJ;
	}

	json_t* cableLabelToJson() {
		json_t* cableLabelsJ = json_array();
		for (CableLabel* cl : cableLabels) {
			json_t* cableLabelJ = json_object();
			json_object_set_new(cableLabelJ, "cableId", json_integer(cl->cableId));
			json_object_set_new(cableLabelJ, "atInput", json_boolean(cl->atInput));
			json_object_set_new(cableLabelJ, "width", json_real(cl->width));
			json_object_set_new(cableLabelJ, "size", json_real(cl->size));
			json_object_set_new(cableLabelJ, "distance", json_real(cl->distance));
			json_object_set_new(cableLabelJ, "text", json_string(cl->text.c_str()));
			json_object_set_new(cableLabelJ, "font", json_integer(cl->font));
			// Note: color and fontColor are auto-calculated from cable, not saved
			json_array_append_new(cableLabelsJ, cableLabelJ);
		}
		return cableLabelsJ;
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

		json_t* labelsJ = json_object_get(rootJ, "labels");
		moduleLabelFromJson(labelsJ);

		json_t* cableLabelsJ = json_object_get(rootJ, "cableLabels");
		cableLabelFromJson(cableLabelsJ);

		idFixClearMap();
		params[PARAM_UNLOCK].setValue(0.f);
	}

	void moduleLabelFromJson(json_t* labelsJ) {
		clearLabels();
		if (labelsJ) {
			size_t labelIdx;
			json_t* labelJ;
			json_array_foreach(labelsJ, labelIdx, labelJ) {
				int64_t moduleId = json_integer_value(json_object_get(labelJ, "moduleId"));
				moduleId = idFix(moduleId);
				if (moduleId < 0) continue;
				
				ModuleLabel* l = addModuleLabel();
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
	}

	void cableLabelFromJson(json_t* cableLabelsJ) {
		clearCableLabels();
		if (cableLabelsJ) {
			size_t labelIdx;
			json_t* cableLabelJ;
			json_array_foreach(cableLabelsJ, labelIdx, cableLabelJ) {
				int64_t cableId = json_integer_value(json_object_get(cableLabelJ, "cableId"));
				if (cableId < 0) continue;
				
				CableLabel* cl = addCableLabel();
				cl->cableId = cableId;
				cl->atInput = json_boolean_value(json_object_get(cableLabelJ, "atInput"));
				cl->width = json_real_value(json_object_get(cableLabelJ, "width"));
				cl->size = json_real_value(json_object_get(cableLabelJ, "size"));
				json_t* distanceJ = json_object_get(cableLabelJ, "distance");
				if (distanceJ) cl->distance = json_real_value(distanceJ);
				json_t* textJ = json_object_get(cableLabelJ, "text");
				if (textJ) cl->text = json_string_value(textJ);
				cl->font = json_integer_value(json_object_get(cableLabelJ, "font"));
			}
		}
	}
};



struct ModuleLabelDrawWidget : TransparentWidget {
	ModuleLabel* label;
	Vec rotatedSize;

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
			std::shared_ptr<Font> font;
			switch (label->font) {
				case 0:
					font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
					break;
				case 1:
					font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/RedkostComic.otf"));
					break;
			}

			nvgFontSize(args.vg, label->size);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -1.2f);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(args.vg, color::alpha(label->fontColor, label->opacity));
			NVGtextRow textRow;
			nvgTextBreakLines(args.vg, label->text.c_str(), NULL, d.size.x, &textRow, 1);
			nvgTextBox(args.vg, d.pos.x, d.pos.y + 0.2f, d.size.x, textRow.start, textRow.end);
		}
	}
};


struct ModuleLabelWidget : widget::TransparentWidget {
	ModuleLabel* label;

	bool requestedDelete = false;
	bool requestedDuplicate = false;
	bool editMode = false;
	bool skew = false;

	math::Vec dragPos;

	ModuleLabelDrawWidget* widget;
	TransformWidget* transformWidget;
	float lastAngle = 360.f;
	float lastSize = 0.f;
	float lastWidth = 0.f;
	bool lastSkew = false;

	ModuleLabelWidget(ModuleLabel* label) {
		this->label = label;

		widget = new ModuleLabelDrawWidget;
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
			dragPos = APP->scene->rack->getMousePos().minus(parent->box.pos);
			dragPos = dragPos.minus(Vec(label->x, label->y));
			e.consume(this);
		}
		TransparentWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove& e) override {
		if (editMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			math::Vec npos = APP->scene->rack->getMousePos().minus(parent->box.pos);
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
			ModuleLabel* l;
			// Needed for input-blur on submenu
			bool textSelected = true;
			LabelField() {
				box.size.x = 160.f;
				placeholder = "Label";
			}
			LabelField* setLabel(ModuleLabel* l) {
				this->l = l;
				setText(l->text);
				selectAll();
				return this;
			}
			void step() override {
				// Keep selected
				if (textSelected) APP->event->setSelectedWidget(this);
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
			ModuleLabel* label;
			bool* textSelected;
			AppearanceItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(Rack::createPtrSlider(&label->size, LABEL_SIZE_MIN, LABEL_SIZE_MAX, LABEL_SIZE_DEFAULT, "Size", "", 1.f, 140.0f));
				menu->addChild(Rack::createPtrSlider(&label->width, LABEL_WIDTH_MIN, LABEL_WIDTH_MAX, LABEL_WIDTH_DEFAULT, "Width", "", 1.f, 140.0f));
				menu->addChild(Rack::createPtrSlider(&label->opacity, LABEL_OPACITY_MIN, LABEL_OPACITY_MAX, 1.0f, "Opacity", "%", 100.f, 140.0f));
				menu->addChild(new MenuSeparator);
				menu->addChild(createMenuLabel("Rotation"));
				menu->addChild(Rack::createValuePtrMenuItem("0°", &label->angle, 0.f));
				menu->addChild(Rack::createValuePtrMenuItem("90°", &label->angle, 90.f));
				menu->addChild(Rack::createValuePtrMenuItem("270°", &label->angle, 270.f));
				menu->addChild(new MenuSeparator);
				menu->addChild(Rack::createColorSubmenuItem("Color", &label->color, {
					{ LABEL_COLOR_YELLOW, "Yellow" },
					{ LABEL_COLOR_RED, "Red" },
					{ LABEL_COLOR_CYAN, "Cyan" },
					{ LABEL_COLOR_GREEN, "Green" },
					{ LABEL_COLOR_PINK, "Pink" },
					{ LABEL_COLOR_WHITE, "White" }
				}, true, true, textSelected));
				menu->addChild(new MenuSeparator);
				menu->addChild(createMenuLabel("Font"));
				menu->addChild(Rack::createValuePtrMenuItem("Default", &label->font, 0));
				menu->addChild(Rack::createValuePtrMenuItem("Handwriting", &label->font, 1));
				menu->addChild(new MenuSeparator);
				menu->addChild(Rack::createColorSubmenuItem("Font color", &label->fontColor, {
					{ LABEL_FONTCOLOR_DEFAULT, "Black" },
					{ LABEL_FONTCOLOR_WHITE, "White" }
				}, true, true, textSelected));
				return menu;
			}
		};

		menu->addChild(createMenuLabel("Module Label"));
		LabelField* labelField = construct<LabelField>()->setLabel(label);
		menu->addChild(labelField);
		menu->addChild(construct<AppearanceItem>(&AppearanceItem::text, "Appearance", &AppearanceItem::label, label, &AppearanceItem::textSelected, &labelField->textSelected));
		menu->addChild(createMenuItem("Duplicate", "", [=]() { requestedDuplicate = true; }));
		menu->addChild(createMenuItem("Delete", "Ctrl+X", [=]() { requestedDelete = true; }));
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (editMode && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_X) {
			requestedDelete = true;
			e.consume(this);
		}
		TransparentWidget::onHoverKey(e);
	}
};


struct CableLabelDrawWidget : TransparentWidget {
	CableLabel* cableLabel;
	Vec rotatedSize;

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (layer != 3) return;
		if (!cableLabel) return;

		Rect d = Rect(Vec(0.f, 0.f), rotatedSize);

		// Draw shadow
		nvgBeginPath(args.vg);
		float r = 4;
		float c = 4;
		math::Vec b = math::Vec(-2.f, -2.f);
		nvgRect(args.vg, d.pos.x + b.x - r, d.pos.y + b.y - r, d.size.x - 2 * b.x + 2 * r, d.size.y - 2 * b.y + 2 * r);
		NVGcolor shadowColor = nvgRGBAf(0.f, 0.f, 0.f, 0.1f);
		NVGcolor transparentColor = nvgRGBAf(0.f, 0.f, 0.f, 0.f);
		nvgFillPaint(args.vg, nvgBoxGradient(args.vg, d.pos.x + b.x, d.pos.y + b.y, d.size.x - 2 * b.x, d.size.y - 2 * b.y, c, r, shadowColor, transparentColor));
		nvgFill(args.vg);

		// Draw label
		nvgBeginPath(args.vg);
		nvgRect(args.vg, d.pos.x, d.pos.y, d.size.x, d.size.y);
		nvgFillColor(args.vg, color::alpha(cableLabel->color, settings::cableOpacity));
		nvgFill(args.vg);

		// Draw text
		if (cableLabel->text.length() > 0) {
			std::shared_ptr<Font> font;
			switch (cableLabel->font) {
				case 0:
					font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
					break;
				case 1:
					font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/RedkostComic.otf"));
					break;
			}

			nvgFontSize(args.vg, cableLabel->size);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -1.2f);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(args.vg, color::alpha(cableLabel->fontColor, settings::cableOpacity));
			NVGtextRow textRow;
			nvgTextBreakLines(args.vg, cableLabel->text.c_str(), NULL, d.size.x, &textRow, 1);
			nvgTextBox(args.vg, d.pos.x, d.pos.y + 0.2f, d.size.x, textRow.start, textRow.end);
		}
	}
};


struct CableLabelWidget : widget::TransparentWidget {
	CableLabel* cableLabel;

	bool requestedDelete = false;
	bool requestedDuplicate = false;
	bool editMode = false;
	bool skew = false;

	CableLabelDrawWidget* widget;
	TransformWidget* transformWidget;
	float lastAngle = 360.f;
	float lastSize = 0.f;
	float lastWidth = 0.f;
	bool lastSkew = false;

	CableLabelWidget(CableLabel* cableLabel) {
		this->cableLabel = cableLabel;

		widget = new CableLabelDrawWidget;
		widget->cableLabel = cableLabel;
		transformWidget = new TransformWidget;
		transformWidget->addChild(widget);
		addChild(transformWidget);
	}

	void step() override {
		// Find the cable in the rack - search by cable ID
		CableWidget* cw = NULL;
		for (Widget* w : APP->scene->rack->getCableContainer()->children) {
			CableWidget* cwTest = dynamic_cast<CableWidget*>(w);
			if (cwTest) {
				// Match by cable ID if cable exists, or by stored ports if incomplete
				if (cwTest->cable && cwTest->cable->id == cableLabel->cableId) {
					cw = cwTest;
					break;
				}
			}
		}

		// If cable not found, it might be incomplete - search by matching ports
		if (!cw) {
			for (Widget* w : APP->scene->rack->getCableContainer()->children) {
				CableWidget* cwTest = dynamic_cast<CableWidget*>(w);
				if (cwTest && !cwTest->cable) {
					// Check if this incomplete cable matches our stored cable ID context
					// by checking if it has the same ports as our labeled cable
					if (cableLabel->lastOutputPort && cableLabel->lastInputPort) {
						if (cwTest->outputPort == cableLabel->lastOutputPort || 
						    cwTest->inputPort == cableLabel->lastInputPort) {
							cw = cwTest;
							break;
						}
					}
				}
			}
		}

		// Request deletion only if cable truly doesn't exist anymore
		if (!cw) {
			// Check if the cable still exists in engine
			bool cableExistsInEngine = false;
			for (Widget* w : APP->scene->rack->getCableContainer()->children) {
				CableWidget* cwTest = dynamic_cast<CableWidget*>(w);
				if (cwTest && cwTest->cable && cwTest->cable->id == cableLabel->cableId) {
					cableExistsInEngine = true;
					break;
				}
			}
			if (!cableExistsInEngine) {
				requestedDelete = true;
			}
			visible = false;
			return;
		}

		// Store port references for tracking during incomplete state
		if (cw->isComplete()) {
			cableLabel->lastOutputPort = cw->outputPort;
			cableLabel->lastInputPort = cw->inputPort;
		}

		// Show label even if cable is incomplete (being dragged), as long as we can position it
		if (!cw->cable || !cw->isComplete()) {
			// Only show if at least one port is connected so we can calculate position
			if (!cw->outputPort && !cw->inputPort) {
				visible = false;
				return;
			}
			// For incomplete cables, we'll show the label where it would be
		}
		visible = true;

		// Get positions
		Vec outputPos = cw->getOutputPos();
		Vec inputPos = cw->getInputPos();
		
		// Check if we can use cached calculations
		if (cableLabel->cacheValid && 
		    cableLabel->cachedOutputPos.equals(outputPos) && 
		    cableLabel->cachedInputPos.equals(inputPos) &&
		    cableLabel->cachedWidth == cableLabel->width &&
		    cableLabel->cachedSize == cableLabel->size &&
		    cableLabel->cachedDistance == cableLabel->distance &&
		    cableLabel->cachedAtInput == cableLabel->atInput) {
			// Use cached values
			box.pos = cableLabel->cachedBoxPos;
			box.size = cableLabel->cachedBoxSize;
			widget->rotatedSize = cableLabel->cachedRotatedSize;
			widget->box.size = box.size;
			
			// Set transform from cached angle
			transformWidget->identity();
			transformWidget->translate(Vec(box.size.x / 2.f, box.size.y / 2.f));
			transformWidget->rotate(cableLabel->cachedLabelAngle);
			transformWidget->translate(Vec(-cableLabel->width / 2.f, -cableLabel->size / 2.f));
			
			// Still need to update colors
			cableLabel->color = cw->color;
			float brightness = (cw->color.r * 0.299f + cw->color.g * 0.587f + cw->color.b * 0.114f);
			cableLabel->fontColor = brightness > 0.5f ? LABEL_FONTCOLOR_DEFAULT : LABEL_FONTCOLOR_WHITE;
			return;
		}
		
		// Cache miss - need to recalculate
		cableLabel->cachedOutputPos = outputPos;
		cableLabel->cachedInputPos = inputPos;
		cableLabel->cachedWidth = cableLabel->width;
		cableLabel->cachedSize = cableLabel->size;
		cableLabel->cachedDistance = cableLabel->distance;
		cableLabel->cachedAtInput = cableLabel->atInput;
		
		// Calculate slump position (matching VCV Rack's getSlumpPos function)
		// This is exactly how VCV Rack calculates the cable curve control point
		float dist = outputPos.minus(inputPos).norm();
		Vec slump = outputPos.plus(inputPos).div(2.f);
		slump.y += (1.0f - settings::cableTension) * (150.0f + 1.0f * dist);
		
		// Adjust endpoints toward slump (matching VCV Rack's cable drawing)
		outputPos = outputPos.plus(slump.minus(outputPos).normalize().mult(14.f));
		inputPos = inputPos.plus(slump.minus(inputPos).normalize().mult(14.f));
		
		// Calculate position at configurable distance from port along the cable curve
		// Use iterative approach to find t value that gives desired distance
		float targetDist = cableLabel->distance; // Distance from port in pixels
		
		// Binary search for t value that gives target distance from the appropriate port
		// For output-side labels, measure from output; for input-side labels, measure from input
		float t = 0.f;
		float tMin = 0.f;
		float tMax = 0.5f; // Only search first half of cable
		Vec referencePort = cableLabel->atInput ? inputPos : outputPos;
		
		for (int i = 0; i < 10; i++) {
			t = (tMin + tMax) / 2.f;
			float tTest = cableLabel->atInput ? (1.f - t) : t;
			float oneMinusT = 1.f - tTest;
			
			// Calculate position at t
			Vec pos = outputPos.mult(oneMinusT * oneMinusT)
				.plus(slump.mult(2.f * oneMinusT * tTest))
				.plus(inputPos.mult(tTest * tTest));
			
			// Measure distance from the reference port
			float currentDist = pos.minus(referencePort).norm();
			if (currentDist < targetDist) {
				tMin = t;
			}
			else {
				tMax = t;
			}
		}
		
		// Use the found t value (already adjusted for input/output in the search)
		float tFinal = cableLabel->atInput ? (1.f - t) : t;
		float oneMinusT = 1.f - tFinal;
		
		Vec labelCenter = outputPos.mult(oneMinusT * oneMinusT)
			.plus(slump.mult(2.f * oneMinusT * tFinal))
			.plus(inputPos.mult(tFinal * tFinal));
		
		// Calculate tangent vector (derivative of quadratic Bezier)
		// B'(t) = 2(1-t)(P₁-P₀) + 2t(P₂-P₁)
		Vec tangent = slump.minus(outputPos).mult(2.f * oneMinusT)
			.plus(inputPos.minus(slump).mult(2.f * tFinal));
		
		// Calculate angle along the cable
		float tangentAngle = std::atan2(tangent.y, tangent.x);
		
		// Rotate label 90° from tangent so short side (height) aligns with cable
		// Keep text readable (never upside down)
		float labelAngle = tangentAngle + M_PI / 2.f;
		if (labelAngle < -M_PI / 2.f) labelAngle += M_PI;
		if (labelAngle > M_PI / 2.f) labelAngle -= M_PI;
		
		// Set cable color (from cable widget)
		cableLabel->color = cw->color;
		
		// Calculate contrasting font color based on cable color brightness
		float brightness = (cw->color.r * 0.299f + cw->color.g * 0.587f + cw->color.b * 0.114f);
		cableLabel->fontColor = brightness > 0.5f ? LABEL_FONTCOLOR_DEFAULT : LABEL_FONTCOLOR_WHITE;
		
		// Calculate perpendicular offset from cable, always pointing downward
		float offsetDist = cableLabel->width / 2.f + 3.f; // Half the label's perpendicular extent + gap
		// Perpendicular to tangent is at tangentAngle ± π/2
		// Choose the perpendicular that points downward (positive Y)
		float perpAngle = tangentAngle + M_PI / 2.f;
		Vec perpDir = Vec(std::cos(perpAngle), std::sin(perpAngle));
		// If this perpendicular points upward, flip it
		if (perpDir.y < 0.f) {
			perpAngle += M_PI;
			perpDir = Vec(std::cos(perpAngle), std::sin(perpAngle));
		}
		Vec perpOffset = perpDir.mult(offsetDist);
		
		// Position label - box size is swapped due to 90° rotation
		box.size = Vec(cableLabel->size, cableLabel->width);
		box.pos = labelCenter.plus(perpOffset).minus(Vec(cableLabel->size / 2.f, cableLabel->width / 2.f));

		widget->rotatedSize = Vec(cableLabel->width, cableLabel->size);
		widget->box.size = box.size;

		// Rotate label 90° from tangent
		transformWidget->identity();
		transformWidget->translate(Vec(box.size.x / 2.f, box.size.y / 2.f));
		transformWidget->rotate(labelAngle);
		transformWidget->translate(Vec(-cableLabel->width / 2.f, -cableLabel->size / 2.f));
		
		// Store in cache
		cableLabel->cachedBoxPos = box.pos;
		cableLabel->cachedBoxSize = box.size;
		cableLabel->cachedLabelAngle = labelAngle;
		cableLabel->cachedRotatedSize = widget->rotatedSize;
		cableLabel->cacheValid = true;

		TransparentWidget::step();
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (editMode && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_X) {
			requestedDelete = true;
			e.consume(this);
		}
		TransparentWidget::onHoverKey(e);
	}
};


// Forward declaration
struct LabelContainer;

// Helper structure to store Glue module reference for adding cable labels
struct GlueHelper {
	static GlueModule* glueModule;
	static LabelContainer* labelContainer;
	
	static void addCableLabelForCable(CableWidget* cw, bool atInput);
	static bool getEditMode();
};

GlueModule* GlueHelper::glueModule = NULL;
LabelContainer* GlueHelper::labelContainer = NULL;

// Port context menu extender for adding cable labels
struct PortWidgetContextExtender {
	Widget* lastSelectedWidget = NULL;

	struct AddCableLabelItem : MenuItem {
		CableWidget* cw;
		bool atInput;
		CableLabel* existingLabel = NULL;

		Menu* createChildMenu() override {
			Menu* menu = new Menu;
			menu->addChild(createMenuLabel("Cable Label"));

			if (!existingLabel) {
				menu->addChild(createMenuItem("Add", "", [=]() {
					GlueHelper::addCableLabelForCable(cw, atInput);
				}));
				return menu;
			}

			struct CableLabelField : ui::TextField {
				CableLabel* cl;
				bool textSelected = true;
				CableLabelField() {
					box.size.x = 160.f;
					placeholder = "Cable Label";
				}
				CableLabelField* setCableLabel(CableLabel* cl) {
					this->cl = cl;
					setText(cl->text);
					selectAll();
					return this;
				}
				void step() override {
					if (textSelected) APP->event->setSelectedWidget(this);
					TextField::step();
					cl->text = text;
				}
				void onSelectKey(const event::SelectKey& e) override {
					if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
						cl->text = text;
						ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
						overlay->requestDelete();
						e.consume(this);
					}
					if (!e.getTarget()) {
						ui::TextField::onSelectKey(e);
					}
				}
			};

			struct CableAppearanceItem : MenuItem {
				CableLabel* cableLabel;
				bool* textSelected;
				CableAppearanceItem() {
					rightText = RIGHT_ARROW;
				}
				Menu* createChildMenu() override {
					Menu* menu = new Menu;
					menu->addChild(Rack::createPtrSlider(&cableLabel->size, LABEL_SIZE_MIN, LABEL_SIZE_MAX, LABEL_SIZE_DEFAULT, "Size", "", 1.f, 140.0f));
					menu->addChild(Rack::createPtrSlider(&cableLabel->width, LABEL_WIDTH_MIN, LABEL_WIDTH_MAX, LABEL_WIDTH_DEFAULT, "Width", "", 1.f, 140.0f));
					menu->addChild(Rack::createPtrSlider(&cableLabel->distance, 10.f, 200.f, 40.f, "Distance", "px", 1.f, 140.0f));
					menu->addChild(new MenuSeparator);
					menu->addChild(createMenuLabel("Font"));
					menu->addChild(Rack::createValuePtrMenuItem("Default", &cableLabel->font, 0));
					menu->addChild(Rack::createValuePtrMenuItem("Handwriting", &cableLabel->font, 1));
					menu->addChild(new MenuSeparator);
					menu->addChild(createMenuLabel("Position"));
					
					// Check if labels exist at other positions on this cable
					bool inputLabelExists = false;
					bool outputLabelExists = false;
					if (GlueHelper::glueModule) {
						for (CableLabel* cl : GlueHelper::glueModule->cableLabels) {
							if (cl->cableId == cableLabel->cableId) {
								if (cl->atInput) inputLabelExists = true;
								else outputLabelExists = true;
							}
						}
					}
					
					// Only allow switching to input if no label exists there (or this is already at input)
					MenuItem* inputItem = Rack::createValuePtrMenuItem("At Input Port", &cableLabel->atInput, true);
					if (inputLabelExists && !cableLabel->atInput) {
						inputItem->disabled = true;
					}
					menu->addChild(inputItem);
					
					// Only allow switching to output if no label exists there (or this is already at output)
					MenuItem* outputItem = Rack::createValuePtrMenuItem("At Output Port", &cableLabel->atInput, false);
					if (outputLabelExists && cableLabel->atInput) {
						outputItem->disabled = true;
					}
					menu->addChild(outputItem);
					return menu;
				}
			};

			CableLabelField* labelField = construct<CableLabelField>()->setCableLabel(existingLabel);
			menu->addChild(labelField);
			menu->addChild(construct<CableAppearanceItem>(&CableAppearanceItem::text, "Appearance", &CableAppearanceItem::cableLabel, existingLabel, &CableAppearanceItem::textSelected, &labelField->textSelected));
			menu->addChild(createMenuItem("Delete", "", [=]() {
				// Mark this cable label for deletion
				if (GlueHelper::glueModule) {
					GlueHelper::glueModule->cableLabelsToDelete.push_back(existingLabel);
				}
			}));
			return menu;
		}
	};

	void step() {
		if (!GlueHelper::glueModule || !GlueHelper::labelContainer) return;
		if (!GlueHelper::getEditMode()) return;

		Widget* w = APP->event->getDraggedWidget();
		if (!w) return;

		// Only handle right button events
		if (APP->event->dragButton != GLFW_MOUSE_BUTTON_RIGHT) {
			lastSelectedWidget = NULL;
			return;
		}

		if (w != lastSelectedWidget) {
			lastSelectedWidget = w;

			// Was the last touched widget a PortWidget?
			PortWidget* pw = dynamic_cast<PortWidget*>(w);
			if (!pw) return;

			// Retrieve the context menu, if available
			MenuOverlay* overlay = NULL;
			for (auto rit = APP->scene->children.rbegin(); rit != APP->scene->children.rend(); rit++) {
				overlay = dynamic_cast<MenuOverlay*>(*rit);
				if (overlay) break;
			}
			if (!overlay) return;

			Menu* menu = overlay->getFirstDescendantOfType<Menu>();
			if (!menu) return;

			extendPortWidgetContextMenu(pw, menu);
		}
	}

	void extendPortWidgetContextMenu(PortWidget* pw, Menu* menu) {
		if (!pw || !pw->module) return;

		// Get cables connected to this port
		std::vector<CableWidget*> cws = APP->scene->rack->getCompleteCablesOnPort(pw);
		if (cws.empty()) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("GLUE Cable Label"));

		for (auto it = cws.rbegin(); it != cws.rend(); it++) {
			CableWidget* cw = *it;
			if (!cw->cable) continue;

			PortWidget* otherPw = (pw->type == engine::Port::INPUT) ? cw->outputPort : cw->inputPort;
			if (!otherPw) continue;

			bool atInput = (pw->type == engine::Port::INPUT);
			
			// Check if label already exists for this cable at this port
			CableLabel* existingLabel = NULL;
			if (GlueHelper::glueModule) {
				for (CableLabel* cl : GlueHelper::glueModule->cableLabels) {
					if (cl->cableId == cw->cable->id) {
						if (cl->atInput == atInput) {
							existingLabel = cl;
						} 
					}
				}
			}

			std::string labelText = otherPw->module->model->name + ": " + otherPw->getPortInfo()->getName();
			AddCableLabelItem* item = createMenuItem<AddCableLabelItem>(labelText, RIGHT_ARROW);
			item->cw = cw;
			item->atInput = atInput;
			item->existingLabel = existingLabel;
			menu->addChild(item);
		}
	}
};


template < typename WIDGET >
struct ModuleLabelRemoveAction : history::ModuleAction {
	ModuleLabel label;
	int64_t moduleId;

	void undo() override {
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		WIDGET* w = dynamic_cast<WIDGET*>(mw);
		assert(w);

		ModuleLabelWidget* lw = w->labelContainer->addModuleLabelWidget();
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
	std::list<ModuleLabel*> moduleLabelsToBeDeleted;
	std::list<CableLabel*> cableLabelsToBeDeleted;

	/** used when duplicating an existing label */
	ModuleLabel* moduleLabelTemplate = NULL;
	CableLabel* cableLabelTemplate = NULL;

	/** labels locked? */
	bool editMode = false;
	/** labels hidden? gets its value from the module's parameter */
	bool hideMode = false;
	/** learning a module for a new label? */
	bool learnMode = false;

	ModuleWidget* mw;
	
	/** Port context menu extender */
	PortWidgetContextExtender portExtender;

	void step() override {
		Widget::step();
		if (!module) return;

		// Step the port extender to catch context menus
		// Only do this if we're the registered Glue module (prevents duplicate menu entries)
		if (this == GlueHelper::labelContainer) {
			portExtender.step();
		}

		if (module->resetRequested) {
			this->clearChildren();
			for (ModuleLabel* l : module->moduleLabels) {
				ModuleLabelWidget* lw = new ModuleLabelWidget(l);
				addChild(lw);
			}
			for (CableLabel* cl : module->cableLabels) {
				CableLabelWidget* clw = new CableLabelWidget(cl);
				addChild(clw);
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
			ModuleLabelWidget* lw = dynamic_cast<ModuleLabelWidget*>(w);
			if (lw) {
				if (lw->requestedDelete) {
					moduleLabelsToBeDeleted.push_back(lw->label);
					moduleLabelTemplate = NULL;
				}
				if (lw->requestedDuplicate) {
					lw->requestedDuplicate = false;
					moduleLabelTemplate = lw->label;
					learnMode = true;
				}
				lw->editMode = editMode;
				lw->skew = module->skewLabels;
				continue;
			}

			CableLabelWidget* clw = dynamic_cast<CableLabelWidget*>(w);
			if (clw) {
				if (clw->requestedDelete) {
					cableLabelsToBeDeleted.push_back(clw->cableLabel);
					cableLabelTemplate = NULL;
				}
				if (clw->requestedDuplicate) {
					clw->requestedDuplicate = false;
					cableLabelTemplate = clw->cableLabel;
					// Can't learn for cable labels, they require explicit cable selection
				}
				clw->editMode = editMode;
				// Cable labels don't use skew
			}
		}

		if (moduleLabelsToBeDeleted.size() > 0) {
			history::ComplexAction* complexAction = new history::ComplexAction;
			complexAction->name = "remove module";
			// First, undo "module removal" by a "double undo"
			complexAction->push(new DoubleUndoAction);
			for (ModuleLabel* l : moduleLabelsToBeDeleted) {
				ModuleLabelRemoveAction<GlueWidget>* a = new ModuleLabelRemoveAction<GlueWidget>;
				a->label = *l;
				a->moduleId = mw->module->id;
				complexAction->push(a);
				removeLabelWidget(l);
			}
			// Second, undo the label removal
			APP->history->push(complexAction);

			moduleLabelsToBeDeleted.clear();
		}

		if (cableLabelsToBeDeleted.size() > 0) {
			for (CableLabel* cl : cableLabelsToBeDeleted) {
				removeCableLabelWidget(cl);
			}
			cableLabelsToBeDeleted.clear();
		}
		
		// Handle cable labels marked for deletion from menu
		if (module->cableLabelsToDelete.size() > 0) {
			for (CableLabel* cl : module->cableLabelsToDelete) {
				removeCableLabelWidget(cl);
			}
			module->cableLabelsToDelete.clear();
		}

		module->lights[GlueModule::LIGHT_LEARN].setBrightness(learnMode);
		module->lights[GlueModule::LIGHT_LOCK].setBrightness(!editMode);
	}

	void draw(const DrawArgs& args) override {
		if (!hideMode) Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (!hideMode) Widget::drawLayer(args, layer);
	}

	ModuleLabelWidget* getModuleLabelWidget(ModuleLabel* l) {
		for (Widget* w : children) {
			ModuleLabelWidget* lw = dynamic_cast<ModuleLabelWidget*>(w);
			if (!lw) continue;
			if (lw->label == l) return lw;
		}
		return NULL;
	}

	ModuleLabelWidget* addModuleLabelWidget() {
		ModuleLabel* l = module->addModuleLabel();
		if (moduleLabelTemplate) {
			l->size = moduleLabelTemplate->size;
			l->width = moduleLabelTemplate->width;
			l->angle = moduleLabelTemplate->angle;
			l->color = moduleLabelTemplate->color;
			l->opacity = moduleLabelTemplate->opacity;
			l->font = moduleLabelTemplate->font;
			l->fontColor = moduleLabelTemplate->fontColor;
			moduleLabelTemplate = NULL;
		}
		ModuleLabelWidget* lw = new ModuleLabelWidget(l);
		addChild(lw);
		return lw;
	}

	void removeLabelWidget(ModuleLabel* l) {
		ModuleLabelWidget* lw = getModuleLabelWidget(l);
		if (!lw) return;
		removeChild(lw);
		delete lw;
		module->removeModuleLabel(l);
	}

	CableLabelWidget* getCableLabelWidget(CableLabel* cl) {
		for (Widget* w : children) {
			CableLabelWidget* clw = dynamic_cast<CableLabelWidget*>(w);
			if (clw && clw->cableLabel == cl) return clw;
		}
		return NULL;
	}

	CableLabelWidget* addCableLabelWidget(int64_t cableId, bool atInput) {
		// Check if label already exists for this cable and port
		for (Widget* w : children) {
			CableLabelWidget* existingClw = dynamic_cast<CableLabelWidget*>(w);
			if (existingClw && existingClw->cableLabel->cableId == cableId && existingClw->cableLabel->atInput == atInput) {
				return existingClw; // Return existing label instead of creating duplicate
			}
		}
		
		CableLabel* cl = module->addCableLabel();
		cl->cableId = cableId;
		cl->atInput = atInput;

		if (cableLabelTemplate) {
			cl->size = cableLabelTemplate->size;
			cl->width = cableLabelTemplate->width;
			cl->font = cableLabelTemplate->font;
			// color and fontColor are auto-set from cable
			cableLabelTemplate = NULL;
		}
		
		CableLabelWidget* clw = new CableLabelWidget(cl);
		addChild(clw);
		return clw;
	}

	void removeCableLabelWidget(CableLabel* cl) {
		CableLabelWidget* clw = getCableLabelWidget(cl);
		if (!clw) return;
		removeChild(clw);
		delete clw;
		module->removeCableLabel(cl);
	}

	void addLabelAtMousePos(Widget* w) {
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw || mw == this->mw) return;
		Module* m = mw->module;
		if (!m) return;

		// Create new label
		ModuleLabelWidget* lw = addModuleLabelWidget();
		lw->label->text = m->model->name;
		lw->label->moduleId = m->id;

		// Move label to mouse click position
		Vec pos = APP->scene->rack->getMousePos();
		pos = pos.minus(mw->box.pos);
		lw->label->x = pos.x - lw->label->width / 2.f;
		lw->label->y = pos.y - lw->label->size / 2.f;

		// Enable edit mode
		editMode = true;
		learnMode = false;
		if (APP->window) glfwSetCursor(APP->window->win, NULL);
	}

	void toggleLearnMode() {
		if (!hideMode) learnMode ^= true;
		GLFWcursor* cursor = NULL;
		if (learnMode) {
			cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		}
		if (APP->window) glfwSetCursor(APP->window->win, cursor);
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
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_G) {
			if (editMode && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
				// Learn module
				Widget* w = APP->event->getHoveredWidget();
				addLabelAtMousePos(w);
				e.consume(this);
			}
			if ((e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
				toggleEditMode();
				e.consume(this);
			}
		}
		Widget::onHoverKey(e);
	}
};


// Implementation of GlueHelper::addCableLabelForCable
void GlueHelper::addCableLabelForCable(CableWidget* cw, bool atInput) {
	if (!glueModule || !labelContainer || !cw || !cw->cable) return;
	
	// Create cable label
	CableLabelWidget* clw = labelContainer->addCableLabelWidget(cw->cable->id, atInput);
	
	// Set default text
	PortWidget* pw = atInput ? cw->inputPort : cw->outputPort;
	if (pw) {
		clw->cableLabel->text = pw->getPortInfo()->getName();
	}
}

bool GlueHelper::getEditMode() {
	if (labelContainer) {
		return labelContainer->editMode;
	}
	return false;
}


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
			for (ModuleLabel* l : module->moduleLabels)
				l->opacity = std::min(l->opacity + LABEL_OPACITY_STEP, LABEL_OPACITY_MAX);
		}
		TL1105::onButton(e);
	}
};

struct OpacityMinusButton : TL1105 {
	GlueModule* module;
	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			for (ModuleLabel* l : module->moduleLabels)
				l->opacity = std::max(l->opacity - LABEL_OPACITY_STEP, LABEL_OPACITY_MIN);
		}
		TL1105::onButton(e);
	}
};

struct HideSwitch : CKSS {
	LabelContainer* labelContainer = NULL;
	void step() override {
		if (labelContainer) labelContainer->toggleHideMode(getParamQuantity()->getValue() > 0.f);
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
		disableDuplicateAction = true;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			labelContainer = new LabelContainer;
			labelContainer->module = module;
			labelContainer->mw = this;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(labelContainer);

			// Set global helper only if no Glue module is already registered
			// This ensures only the first Glue module extends port context menus
			if (!GlueHelper::glueModule) {
				GlueHelper::glueModule = module;
				GlueHelper::labelContainer = labelContainer;
			}

			// Move the cable-widget to the end, labels should appear below cables
			// NB: this should be considered unstable API
			std::list<Widget*>::iterator it;
			for (it = APP->scene->rack->children.begin(); it != APP->scene->rack->children.end(); ++it){
				if (*it == APP->scene->rack->getCableContainer()) break;
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
			// Clear global helper only if this module is the one currently registered
			if (GlueHelper::glueModule == module) {
				GlueHelper::glueModule = NULL;
				GlueHelper::labelContainer = NULL;
			}
			
			APP->scene->rack->removeChild(labelContainer);
			delete labelContainer;
		}
	}

	void consolidate() {
		struct GlueChangeAction : history::ModuleAction {
			json_t* oldLabelJ;
			json_t* newLabelJ;
			void undo() override {
				GlueWidget* mw = dynamic_cast<GlueWidget*>(APP->scene->rack->getModule(moduleId));
				assert(mw);
				mw->module->moduleLabelFromJson(oldLabelJ);
			}
			void redo() override {
				GlueWidget* mw = dynamic_cast<GlueWidget*>(APP->scene->rack->getModule(moduleId));
				assert(mw);
				mw->module->moduleLabelFromJson(newLabelJ);
			}
		};

		std::list<ModuleWidget*> toBeRemoved;
		for (Widget* w : APP->scene->rack->getModuleContainer()->children) {
			GlueWidget* gw = dynamic_cast<GlueWidget*>(w);
			if (!gw || gw == this) continue;
			toBeRemoved.push_back(gw);
		}
		if (toBeRemoved.size() == 0) return;

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = "stoermelder GLUE consolidate";
		
		GlueChangeAction* mc = new GlueChangeAction;
		mc->moduleId = module->id;
		mc->oldLabelJ = module->moduleLabelToJson();
		complexAction->push(mc);

		for (ModuleWidget* w : toBeRemoved) {
			GlueWidget* gw = dynamic_cast<GlueWidget*>(w);

			history::ModuleRemove* h = new history::ModuleRemove;
			h->setModule(w);
			complexAction->push(h);

			for (ModuleLabel* l : gw->module->moduleLabels) {
				module->moduleLabels.push_back(l);
			}

			gw->module->moduleLabels.clear();
			APP->scene->rack->removeModule(w);
			delete w;
		}

		mc->newLabelJ = module->moduleLabelToJson();

		APP->history->push(complexAction);
		module->resetRequested = true;
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
				menu->addChild(Rack::createPtrSlider(&module->defaultSize, LABEL_SIZE_MIN, LABEL_SIZE_MAX, LABEL_SIZE_DEFAULT, "Default size", "", 1.f, 160.0f));
				menu->addChild(Rack::createPtrSlider(&module->defaultWidth, LABEL_WIDTH_MIN, LABEL_WIDTH_MAX, LABEL_WIDTH_DEFAULT, "Default width", "", 1.f, 160.0f));
				menu->addChild(Rack::createPtrSlider(&module->defaultOpacity, LABEL_OPACITY_MIN, LABEL_OPACITY_MAX, 1.0f, "Default opacity", "%", 100.f, 160.0f));
				menu->addChild(new MenuSeparator);
				menu->addChild(createMenuLabel("Default rotation"));
				menu->addChild(Rack::createValuePtrMenuItem("0°", &module->defaultAngle, 0.f));
				menu->addChild(Rack::createValuePtrMenuItem("90°", &module->defaultAngle, 90.f));
				menu->addChild(Rack::createValuePtrMenuItem("270°", &module->defaultAngle, 270.f));
				menu->addChild(new MenuSeparator());
				menu->addChild(Rack::createColorSubmenuItem("Default color", &module->defaultColor, {
					{ LABEL_COLOR_YELLOW, "Yellow" },
					{ LABEL_COLOR_RED, "Red" },
					{ LABEL_COLOR_CYAN, "Cyan" },
					{ LABEL_COLOR_GREEN, "Green" },
					{ LABEL_COLOR_PINK, "Pink" },
					{ LABEL_COLOR_WHITE, "White" }
				}, true, true, nullptr));
				menu->addChild(new MenuSeparator());
				menu->addChild(createMenuLabel("Default font"));
				menu->addChild(Rack::createValuePtrMenuItem("Default", &module->defaultFont, 0));
				menu->addChild(Rack::createValuePtrMenuItem("Handwriting", &module->defaultFont, 1));
				menu->addChild(new MenuSeparator());
				menu->addChild(Rack::createColorSubmenuItem("Default font color", &module->defaultFontColor, {
					{ LABEL_FONTCOLOR_DEFAULT, "Black" },
					{ LABEL_FONTCOLOR_WHITE, "White" }
				}, true, true, nullptr));
				return menu;
			}
		};

		struct ModuleLabelMenuItem : MenuItem {
			LabelContainer* labelContainer;
			ModuleLabel* label;
			ModuleLabelMenuItem() {
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
				menu->addChild(createMenuItem("Delete", "", [=]() { labelContainer->removeLabelWidget(label); }));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<DefaultAppearanceMenuItem>(&MenuItem::text, "Label appearance", &DefaultAppearanceMenuItem::module, module));
		menu->addChild(createBoolPtrMenuItem("Skew labels", "", &module->skewLabels));

		if (module->moduleLabels.size() > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(createMenuItem("Consolidate GLUE", "", [=]() { consolidate(); }));
			menu->addChild(new MenuSeparator());
			menu->addChild(createMenuLabel("Module Labels"));

			for (ModuleLabel* l : module->moduleLabels) {
				menu->addChild(construct<ModuleLabelMenuItem>(&ModuleLabelMenuItem::labelContainer, labelContainer, &ModuleLabelMenuItem::label, l));
			}
		}

		if (module->cableLabels.size() > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(createMenuLabel("Cable Labels"));

			for (CableLabel* cl : module->cableLabels) {
				std::string text = cl->text.empty() ? "<empty>" : cl->text;
				menu->addChild(createSubmenuItem(text, "", [=](Menu* menu) {
					menu->addChild(createMenuItem("Delete", "", [=]() {
						labelContainer->removeCableLabelWidget(cl);
					}));
				}));
			}
		}
	}
};

} // namespace Glue
} // namespace StoermelderPackOne

Model* modelGlue = createModel<StoermelderPackOne::Glue::GlueModule, StoermelderPackOne::Glue::GlueWidget>("Glue");