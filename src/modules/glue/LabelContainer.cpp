#include "LabelContainer.hpp"
#include "GlueWidget.hpp"
#include "../../utils/cursor.hpp"
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Glue {

void PortWidgetContextExtender::step() {
	if (!module || !labelContainer) return;
	if (!labelContainer->editMode) return;

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

void PortWidgetContextExtender::extendPortWidgetContextMenu(PortWidget* pw, Menu* menu) {
	if (!pw || !pw->module) return;

	// Get cables connected to this port
	std::vector<CableWidget*> cws = APP->scene->rack->getCompleteCablesOnPort(pw);
	if (cws.empty()) return;

	struct AddCableLabelItem : MenuItem {
		CableWidget* cw;
		bool atInput;
		CableLabel* existingLabel = NULL;
		GlueModule* module = NULL;
		LabelContainer* labelContainer = NULL;

		Menu* createChildMenu() override {
			Menu* menu = new Menu;
			menu->addChild(createMenuLabel("Cable Label"));

			if (!existingLabel) {
				menu->addChild(createMenuItem("Add", "", [=]() {
					if (!module || !labelContainer || !cw || !cw->cable) return;
					
					// Create cable label
					CableLabelWidget* clw = labelContainer->addCableLabelWidget(cw->cable->id, atInput);
					
					// Set default text
					PortWidget* pw = atInput ? cw->inputPort : cw->outputPort;
					if (pw) {
						clw->cableLabel->text = pw->getPortInfo()->getName();
					}
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
				GlueModule* module = NULL;

				CableAppearanceItem() {
					rightText = RIGHT_ARROW;
				}
		
				// Helper function to create a slider that invalidates cache
				inline ui::Slider* createCableLabelSlider(float* value, float minValue, float maxValue, float defaultValue, std::string title = "", std::string unit = "", float displayBase = 1.f, float displayMultiplier = 1.f) {
					struct CacheInvalidatingSlider : ui::Slider {
						CableLabel* cableLabel;
						void onDragMove(const DragMoveEvent& e) override {
							ui::Slider::onDragMove(e);
							if (cableLabel) {
								cableLabel->cacheValid = false;
							}
						}
					};

					auto s = Rack::createPtrSliderT<CacheInvalidatingSlider>(value, minValue, maxValue, defaultValue, title, unit, displayBase, displayMultiplier);
					s->cableLabel = cableLabel;
					return s;
				}

				Menu* createChildMenu() override {
					Menu* menu = new Menu;
					menu->addChild(createCableLabelSlider(&cableLabel->size, LABEL_SIZE_MIN, LABEL_SIZE_MAX, LABEL_SIZE_DEFAULT, "Size", "", 1.f, 140.0f));
					menu->addChild(createCableLabelSlider(&cableLabel->width, LABEL_WIDTH_MIN, LABEL_WIDTH_MAX, LABEL_WIDTH_DEFAULT, "Width", "", 1.f, 140.0f));
					menu->addChild(createCableLabelSlider(&cableLabel->distance, 10.f, 200.f, 40.f, "Distance", "px", 1.f, 140.0f));
					menu->addChild(new MenuSeparator);
					menu->addChild(createMenuLabel("Font"));
					menu->addChild(Rack::createValuePtrMenuItem("Default", &cableLabel->font, 0));
					menu->addChild(Rack::createValuePtrMenuItem("Handwriting", &cableLabel->font, 1));
					menu->addChild(new MenuSeparator);
					menu->addChild(createMenuLabel("Position"));
					
					// Check if labels exist at other positions on this cable
					bool inputLabelExists = false;
					bool outputLabelExists = false;
					if (module) {
						for (CableLabel* cl : module->cableLabels) {
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
			CableAppearanceItem* appearanceItem = construct<CableAppearanceItem>(&CableAppearanceItem::text, "Appearance", &CableAppearanceItem::cableLabel, existingLabel, &CableAppearanceItem::textSelected, &labelField->textSelected);
			appearanceItem->module = module;
			menu->addChild(appearanceItem);
			menu->addChild(createMenuItem("Delete", "", [=]() {
				// Mark this cable label for deletion
				if (module) {
					module->cableLabelsToDelete.push_back(existingLabel);
				}
			}));
			return menu;
		}
	};

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
		if (module) {
			for (CableLabel* cl : module->cableLabels) {
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
		item->module = module;
		item->labelContainer = labelContainer;
		menu->addChild(item);
	}
}


void LabelContainer::step() {
	Widget::step();
	if (!module) return;

	// Set up the port extender with references
	portExtender.module = module;
	portExtender.labelContainer = this;

	// Step the port extender to catch context menus
	portExtender.step();

	// Check if cable tension has changed - if so, invalidate all cable label caches
	if (lastCableTension != settings::cableTension) {
		lastCableTension = settings::cableTension;
		for (CableLabel* cl : module->cableLabels) {
			cl->cacheValid = false;
		}
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
		struct DoubleUndoAction : history::ModuleAction {
			void undo() override {
				APP->history->undo();
			}
			void redo() override {
				APP->history->redo();
			}
		};

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

void LabelContainer::draw(const DrawArgs& args) {
	if (!hideMode) Widget::draw(args);
}

void LabelContainer::drawLayer(const DrawArgs& args, int layer) {
	if (!hideMode) Widget::drawLayer(args, layer);
}

ModuleLabelWidget* LabelContainer::getModuleLabelWidget(ModuleLabel* l) {
	for (Widget* w : children) {
		ModuleLabelWidget* lw = dynamic_cast<ModuleLabelWidget*>(w);
		if (!lw) continue;
		if (lw->label == l) return lw;
	}
	return NULL;
}

ModuleLabelWidget* LabelContainer::addModuleLabelWidget() {
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

void LabelContainer::removeLabelWidget(ModuleLabel* l) {
	ModuleLabelWidget* lw = getModuleLabelWidget(l);
	if (!lw) return;
	removeChild(lw);
	delete lw;
	module->removeModuleLabel(l);
}

CableLabelWidget* LabelContainer::getCableLabelWidget(CableLabel* cl) {
	for (Widget* w : children) {
		CableLabelWidget* clw = dynamic_cast<CableLabelWidget*>(w);
		if (clw && clw->cableLabel == cl) return clw;
	}
	return NULL;
}

CableLabelWidget* LabelContainer::addCableLabelWidget(int64_t cableId, bool atInput) {
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

void LabelContainer::removeCableLabelWidget(CableLabel* cl) {
	CableLabelWidget* clw = getCableLabelWidget(cl);
	if (!clw) return;
	removeChild(clw);
	delete clw;
	module->removeCableLabel(cl);
}

void LabelContainer::addLabelAtMousePos(Widget* w) {
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
	cursor::resetCursor();
}

void LabelContainer::toggleLearnMode() {
	if (!hideMode) learnMode ^= true;
	cursor::setLearnCursor(learnMode);
}

void LabelContainer::toggleEditMode() {
	if (!hideMode) editMode ^= true;
}

void LabelContainer::toggleHideMode(bool doHide) {
	hideMode = doHide;
	if (hideMode) {
		editMode = false;
		learnMode = false;
	}
}

void LabelContainer::onHoverKey(const event::HoverKey& e) {
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

} // namespace Glue
} // namespace StoermelderPackOne
