#include "../../plugin.hpp"
#include "GlueWidget.hpp"

namespace StoermelderPackOne {
namespace Glue {

GlueModule::GlueModule() {
	panelTheme = pluginSettings.panelThemeDefault;
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	configSwitch(PARAM_UNLOCK, 0.f, 1.f, 0.f, "Unlock labels for editing (" RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+G");
	configSwitch(PARAM_ADD_LABEL, 0.f, 1.f, 0.f, "Add module label (" RACK_MOD_CTRL_NAME "+G)");
	configSwitch(PARAM_OPACITY_PLUS, 0.f, 1.f, 0.f, string::f("Increase overall opacity by %i%%", int(LABEL_OPACITY_STEP * 100)));
	configSwitch(PARAM_OPACITY_MINUS, 0.f, 1.f, 0.f, string::f("Decrease overall opacity by %i%%", int(LABEL_OPACITY_STEP * 100)));
	configSwitch(PARAM_HIDE, 0.f, 1.f, 0.f, "Hide labels");

	ResetEvent re;
	onReset(re);
}

GlueModule::~GlueModule() {
	clearLabels();
	clearCableLabels();
}

void GlueModule::onReset(const ResetEvent& e) {
	Module::onReset(e);
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

ModuleLabel* GlueModule::addModuleLabel() {
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

void GlueModule::removeModuleLabel(ModuleLabel* l) {
	// Make sure the widget is deleted before!
	moduleLabels.remove(l);
	delete l;
}

void GlueModule::clearLabels() {
	for (ModuleLabel* l : moduleLabels) {
		delete l;
	}
	moduleLabels.clear();
	resetRequested = true;
}

CableLabel* GlueModule::addCableLabel() {
	CableLabel* cl = new CableLabel;
	cl->size = defaultSize;
	cl->width = defaultWidth;
	cl->font = defaultFont;
	cl->distance = rescale(rack::random::uniform(), 0.f, 1.f, 20.f, 40.f);
	// color and fontColor are auto-set from cable
	cableLabels.push_back(cl);
	return cl;
}

void GlueModule::removeCableLabel(CableLabel* cl) {
	cableLabels.remove(cl);
	delete cl;
}

void GlueModule::clearCableLabels() {
	for (CableLabel* cl : cableLabels) {
		delete cl;
	}
	cableLabels.clear();
	resetRequested = true;
}

json_t* GlueModule::dataToJson() {
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

json_t* GlueModule::moduleLabelToJson() {
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

json_t* GlueModule::cableLabelToJson() {
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

void GlueModule::dataFromJson(json_t* rootJ) {
	json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
	if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

	json_t* defaultSizeJ = json_object_get(rootJ, "defaultSize");
	if (defaultSizeJ) defaultSize = json_real_value(defaultSizeJ);
	json_t* defaultWidthJ = json_object_get(rootJ, "defaultWidth");
	if (defaultWidthJ) defaultWidth = json_real_value(defaultWidthJ);
	json_t* defaultAngleJ = json_object_get(rootJ, "defaultAngle");
	if (defaultAngleJ) defaultAngle = json_real_value(defaultAngleJ);
	json_t* defaultOpacityJ = json_object_get(rootJ, "defaultOpacity");
	if (defaultOpacityJ) defaultOpacity = json_real_value(defaultOpacityJ);
	json_t* defaultColorJ = json_object_get(rootJ, "defaultColor");
	if (defaultColorJ && json_is_string(defaultColorJ)) defaultColor = color::fromHexString(json_string_value(defaultColorJ));
	json_t* defaultFontJ = json_object_get(rootJ, "defaultFont");
	if (defaultFontJ) defaultFont = json_integer_value(defaultFontJ);
	json_t* defaultFontColorJ = json_object_get(rootJ, "defaultFontColor");
	if (defaultFontColorJ && json_is_string(defaultFontColorJ)) defaultFontColor = color::fromHexString(json_string_value(defaultFontColorJ));
	json_t* skewLabelsJ = json_object_get(rootJ, "skewLabels");
	if (skewLabelsJ) skewLabels = json_boolean_value(skewLabelsJ);

	json_t* labelsJ = json_object_get(rootJ, "labels");
	if (labelsJ && json_is_array(labelsJ)) moduleLabelFromJson(labelsJ);

	json_t* cableLabelsJ = json_object_get(rootJ, "cableLabels");
	if (cableLabelsJ) cableLabelFromJson(cableLabelsJ);

	idFixClearMap();
	params[PARAM_UNLOCK].setValue(0.f);
}

void GlueModule::moduleLabelFromJson(json_t* labelsJ) {
	clearLabels();
	if (labelsJ) {
		size_t labelIdx;
		json_t* labelJ;
		json_array_foreach(labelsJ, labelIdx, labelJ) {
			json_t* moduleIdJ = json_object_get(labelJ, "moduleId");
			if (!moduleIdJ) continue;
			int64_t moduleId = json_integer_value(moduleIdJ);
			moduleId = idFix(moduleId);
			if (moduleId < 0) continue;
			
			ModuleLabel* l = addModuleLabel();
			l->moduleId = moduleId;
			json_t* xJ = json_object_get(labelJ, "x");
			if (xJ) l->x = json_real_value(xJ);
			json_t* yJ = json_object_get(labelJ, "y");
			if (yJ) l->y = json_real_value(yJ);
			json_t* angleJ = json_object_get(labelJ, "angle");
			if (angleJ) l->angle = json_real_value(angleJ);
			json_t* skewJ = json_object_get(labelJ, "skew");
			if (skewJ) l->skew = json_real_value(skewJ);
			json_t* opacityJ = json_object_get(labelJ, "opacity");
			if (opacityJ) l->opacity = json_real_value(opacityJ);
			json_t* widthJ = json_object_get(labelJ, "width");
			if (widthJ) l->width = json_real_value(widthJ);
			json_t* sizeJ = json_object_get(labelJ, "size");
			if (sizeJ) l->size = json_real_value(sizeJ);
			json_t* textJ = json_object_get(labelJ, "text");
			if (textJ) l->text = json_string_value(textJ);
			json_t* colorJ = json_object_get(labelJ, "color");
			if (colorJ) l->color = color::fromHexString(json_string_value(colorJ));
			json_t* fontJ = json_object_get(labelJ, "font");
			if (fontJ) l->font = json_integer_value(fontJ);
			json_t* fontColorJ = json_object_get(labelJ, "fontColor");
			if (fontColorJ) l->fontColor = color::fromHexString(json_string_value(fontColorJ));
		}
	}
}

void GlueModule::cableLabelFromJson(json_t* cableLabelsJ) {
	clearCableLabels();
	if (cableLabelsJ) {
		size_t labelIdx;
		json_t* cableLabelJ;
		json_array_foreach(cableLabelsJ, labelIdx, cableLabelJ) {
			json_t* cableIdJ = json_object_get(cableLabelJ, "cableId");
			if (!cableIdJ) continue;
			int64_t cableId = json_integer_value(cableIdJ);
			if (cableId < 0) continue;
			
			CableLabel* cl = addCableLabel();
			cl->cableId = cableId;
			json_t* atInputJ = json_object_get(cableLabelJ, "atInput");
			if (atInputJ) cl->atInput = json_boolean_value(atInputJ);
			json_t* widthJ = json_object_get(cableLabelJ, "width");
			if (widthJ) cl->width = json_real_value(widthJ);
			json_t* sizeJ = json_object_get(cableLabelJ, "size");
			if (sizeJ) cl->size = json_real_value(sizeJ);
			json_t* distanceJ = json_object_get(cableLabelJ, "distance");
			if (distanceJ) cl->distance = json_real_value(distanceJ);
			json_t* textJ = json_object_get(cableLabelJ, "text");
			if (textJ) cl->text = json_string_value(textJ);
			json_t* fontJ = json_object_get(cableLabelJ, "font");
			if (fontJ) cl->font = json_integer_value(fontJ);
		}
	}
}


// GlueWidget button implementations

void LabelButton::onButton(const event::Button& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		labelContainer->toggleLearnMode();
	}
	TL1105::onButton(e);
}

void LockButton::onButton(const event::Button& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		labelContainer->toggleEditMode();
	}
	TL1105::onButton(e);
}

void OpacityPlusButton::onButton(const event::Button& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		for (ModuleLabel* l : module->moduleLabels)
			l->opacity = std::min(l->opacity + LABEL_OPACITY_STEP, LABEL_OPACITY_MAX);
	}
	TL1105::onButton(e);
}

void OpacityMinusButton::onButton(const event::Button& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		for (ModuleLabel* l : module->moduleLabels)
			l->opacity = std::max(l->opacity - LABEL_OPACITY_STEP, LABEL_OPACITY_MIN);
	}
	TL1105::onButton(e);
}

void HideSwitch::step() {
	if (labelContainer) labelContainer->toggleHideMode(getParamQuantity()->getValue() > 0.f);
	CKSS::step();
}


// GlueWidget implementation

GlueWidget::GlueWidget(GlueModule* module)
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

GlueWidget::~GlueWidget() {
	if (labelContainer) {
		APP->scene->rack->removeChild(labelContainer);
		delete labelContainer;
	}
}

void GlueWidget::consolidate() {
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

void GlueWidget::appendContextMenu(Menu* menu) {
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

// Explicit template instantiation
template struct ModuleLabelRemoveAction<GlueWidget>;

// Undo action implementations
template <>
void ModuleLabelRemoveAction<GlueWidget>::undo() {
	ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
	assert(mw);
	GlueWidget* w = dynamic_cast<GlueWidget*>(mw);
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

template <>
void ModuleLabelRemoveAction<GlueWidget>::redo() {
	// Nothing to do here, it's handled as any module removal by LabelContainer
}

} // namespace Glue
} // namespace StoermelderPackOne

Model* modelGlue = createModel<StoermelderPackOne::Glue::GlueModule, StoermelderPackOne::Glue::GlueWidget>("Glue");