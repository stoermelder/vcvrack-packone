#include "plugin.hpp"

namespace StoermelderPackOne {

template <typename MODULE>
struct MapParamQuantity : ParamQuantity {
	MODULE* module;
	int id = 0;
	std::string getParamName() {
		if (!module)
			return "";
		ParamHandle* paramHandle = &module->paramHandles[id];
		if (paramHandle->moduleId < 0)
			return "";
		ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return "";
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module* m = mw->module;
		if (!m)
			return "";
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return "";
		ParamQuantity* paramQuantity = m->paramQuantities[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += paramQuantity->label;
		return s;
	}

	std::string getDisplayValueString() override {
		std::string name = getParamName();
		return name != "" ? "\"" + name + "\"" : "Unmapped";
	}
};

template <typename MODULE>
struct MapButton : LEDBezel {
	MODULE* module;
	int id = 0;

	void setModule(MODULE* module) {
		this->module = module;
	}

	void step() override {
		LEDBezel::step();
		if (module && !module->paramHandles[id].module) {
			module->clearMap(id);
		}
	}

	void onButton(const event::Button& e) override {
		e.stopPropagating();
		if (!module)
			return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			e.consume(this);

			if (module->paramHandles[id].moduleId >= 0) {
				ui::Menu* menu = createMenu();
				std::string header = "Parameter \"" + getParamName() + "\"";
				menu->addChild(createMenuLabel(header));

				struct UnmapItem : MenuItem {
					MODULE* module;
					int id;
					void onAction(const event::Action& e) override {
						module->clearMap(id);
					}
				};
				menu->addChild(construct<UnmapItem>(&MenuItem::text, "Unmap", &UnmapItem::module, module, &UnmapItem::id, id));

				struct IndicateItem : MenuItem {
					MODULE* module;
					int id;
					void onAction(const event::Action& e) override {
						ParamHandle* paramHandle = &module->paramHandles[id];
						ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
						module->paramHandleIndicator[id].indicate(mw);
					}
				};
				menu->addChild(construct<IndicateItem>(&MenuItem::text, "Locate and indicate", &IndicateItem::module, module, &IndicateItem::id, id));
				appendContextMenu(menu);
			} 
		}
	}

	void onSelect(const event::Select& e) override {
		if (!module) return;

		// Reset touchedParam
		APP->scene->rack->touchedParam = NULL;
		module->enableLearn(id);

		GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		glfwSetCursor(APP->window->win, cursor);
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module) return;
		// Check if a ParamWidget was touched
		ParamWidget* touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam && touchedParam->paramQuantity->module != module) {
			APP->scene->rack->touchedParam = NULL;
			int moduleId = touchedParam->paramQuantity->module->id;
			int paramId = touchedParam->paramQuantity->paramId;
			module->learnParam(id, moduleId, paramId);
		} 
		else {
			module->disableLearn(id);
		}
		glfwSetCursor(APP->window->win, NULL);
	}

	std::string getParamName() {
		if (!module)
			return "";
		if (id >= module->mapLen)
			return "<ERROR>";
		ParamHandle* paramHandle = &module->paramHandles[id];
		if (paramHandle->moduleId < 0)
			return "<ERROR>";
		ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return "<ERROR>";
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module* m = mw->module;
		if (!m)
			return "<ERROR>";
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return "<ERROR>";
		ParamQuantity* paramQuantity = m->paramQuantities[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += paramQuantity->label;
		return s;
	}

	virtual void appendContextMenu(Menu* menu) {}
};

template <typename BASE>
struct MapLight : BASE {
	MapLight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

} // namespace StoermelderPackOne