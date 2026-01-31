#include "ModuleLabelWidget.hpp"
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Glue {

void ModuleLabelDrawWidget::draw(const Widget::DrawArgs& args) {
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


ModuleLabelWidget::ModuleLabelWidget(ModuleLabel* label) {
	this->label = label;

	widget = new ModuleLabelDrawWidget;
	widget->label = label;
	transformWidget = new TransformWidget;
	transformWidget->addChild(widget);
	addChild(transformWidget);
}

void ModuleLabelWidget::step() {
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

void ModuleLabelWidget::onButton(const event::Button& e) {
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

void ModuleLabelWidget::onDragStart(const event::DragStart& e) {
	if (editMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		dragPos = APP->scene->rack->getMousePos().minus(parent->box.pos);
		dragPos = dragPos.minus(Vec(label->x, label->y));
		e.consume(this);
	}
	TransparentWidget::onDragStart(e);
}

void ModuleLabelWidget::onDragMove(const event::DragMove& e) {
	if (editMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		math::Vec npos = APP->scene->rack->getMousePos().minus(parent->box.pos);
		math::Vec pos = npos.minus(dragPos);
		label->x = pos.x;
		label->y = pos.y;
		e.consume(this);
	}
	TransparentWidget::onDragMove(e);
}

void ModuleLabelWidget::createContextMenu() {
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

void ModuleLabelWidget::onHoverKey(const event::HoverKey& e) {
	if (editMode && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_X) {
		requestedDelete = true;
		e.consume(this);
	}
	TransparentWidget::onHoverKey(e);
}

} // namespace Glue
} // namespace StoermelderPackOne
