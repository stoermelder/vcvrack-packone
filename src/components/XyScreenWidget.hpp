#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

using namespace rack;

template <int INPUTS>
struct XyScreenModule {
	/** [Stored to JSON] */
	float radius[INPUTS];

	float radiusUi[INPUTS];
	dsp::ExponentialFilter radiusFilter[INPUTS];

	/** [Stored to JSON] */
	float amount[INPUTS];

	int selectedId = -1;
	int selectedType = -1;

	virtual void init() {}

	virtual inline engine::ParamQuantity* screenXpq(int type, int input) {
		return NULL;
	}

	virtual inline engine::ParamQuantity* screenYpq(int type, int input) {
		return NULL;
	}

	virtual inline int screenItemCount(int type = 0) {
		return type == 0 ? INPUTS : 0;
	}

	void screenInit() {
		for (int i = 0; i < INPUTS; i++) {
			radius[i] = radiusUi[i] = 0.5f;
			radiusFilter[i].setTau(0.1f);
			amount[i] = 1.f;
		};
	}

	void screenReset() {
		for (int i = 0; i < INPUTS; i++) {
			radius[i] = 0.5f;
		}
	}
	
	void screenProcess(float sampleTime) {
		for (int j = 0; j < INPUTS; j++) {
			radius[j] = radiusFilter[j].process(sampleTime, radiusUi[j]);
		}
	}

	inline void screenSelectionSet(int type, int id) {
		if (type == 0 && id + 1 > INPUTS) return;
		//if (type == 1 && id + 1 > mixportsUsed) return;
		selectedType = type;
		selectedId = id;
	}

	inline bool screenSelectionTest(int type, int id) {
		return selectedType == type && selectedId == id;
	}

	inline void screenSelectionReset() {
		selectedType = -1;
		selectedId = -1;
	}

	void screenRandAmount() {
		for (int i = 0; i < INPUTS; i++) {
			amount[i] = random::uniform();
		}
	}

	void screenRandRadius() {
		for (int i = 0; i < INPUTS; i++) {
			radius[i] = random::uniform();
		}
	}

	void screenRandX() {
		for (int i = 0; i < INPUTS; i++) {
			screenXpq(0, i)->getParam()->setValue(random::uniform());
		}
	}

	void screenRandY() {
		for (int i = 0; i < INPUTS; i++) {
			screenYpq(0, i)->getParam()->setValue(random::uniform());
		}
	}

	void dataToJson(json_t* dataJ, int input) {
		json_object_set_new(dataJ, "amount", json_real(amount[input]));
		json_object_set_new(dataJ, "radius", json_real(radius[input]));
	}

	void dataFromJson(json_t* dataJ, int input) {
		amount[input] = json_real_value(json_object_get(dataJ, "amount"));
		radiusUi[input] = radius[input] = json_real_value(json_object_get(dataJ, "radius"));
	}
};


template <typename MODULE>
struct XyScreenRadiusChangeAction : history::ModuleAction {
	int id;
	float oldValue;
	float newValue;

	XyScreenRadiusChangeAction(MODULE* m) {
		name = m->model->plugin->brand + " " + m->model->name + " radius change";
		moduleId = m->getId();
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->radius[id] = oldValue;
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->radius[id] = newValue;
	}
};


template <typename MODULE>
struct XyScreenRadiusSlider : ui::Slider {
	struct RadiusQuantity : Quantity {
		MODULE* module;
		int id;

		RadiusQuantity(MODULE* module, int id) {
			this->module = module;
			this->id = id;
		}
		void setValue(float value) override {
			value = clamp(value, 0.f, 1.f);
			module->radiusUi[id] = value;
		}
		float getValue() override {
			return module->radiusUi[id];
		}
		float getDefaultValue() override {
			return 0.5f;
		}
		float getDisplayValue() override {
			return getValue() * 100.f;
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue / 100.f);
		}
		std::string getLabel() override {
			return "Radius";
		}
		std::string getUnit() override {
			return "";
		}
	};

	MODULE* module;
	int id;
	XyScreenRadiusChangeAction<MODULE>* h;

	XyScreenRadiusSlider(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		quantity = new RadiusQuantity(module, id);
	}
	~XyScreenRadiusSlider() {
		delete quantity;
	}

	void onDragStart(const event::DragStart& e) override {
		// history
		h = new XyScreenRadiusChangeAction<MODULE>(module);
		h->moduleId = module->id;
		h->id = id;
		h->oldValue = module->radius[id];

		ui::Slider::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		h->newValue = module->radius[id];
		APP->history->push(h);
		h = NULL;

		ui::Slider::onDragEnd(e);
	}
};


template <typename MODULE>
struct XyScreenAmountChangeAction : history::ModuleAction {
	int id;
	float oldValue;
	float newValue;

	XyScreenAmountChangeAction(MODULE* m) {
		name = m->model->plugin->brand + " " + m->model->name + " amount change";
		moduleId = m->getId();
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->amount[id] = oldValue;
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->amount[id] = newValue;
	}
};


template <typename MODULE>
struct XyScreenAmountSlider : ui::Slider {
	struct AmountQuantity : Quantity {
		MODULE* module;
		int id;

		AmountQuantity(MODULE* module, int id) {
			this->module = module;
			this->id = id;
		}
		void setValue(float value) override {
			module->amount[id] = math::clamp(value, 0.f, 1.f);
		}
		float getValue() override {
			return module->amount[id];
		}
		float getDefaultValue() override {
			return 0.5;
		}
		float getDisplayValue() override {
			return getValue() * 100;
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue / 100);
		}
		std::string getLabel() override {
			return "Amount";
		}
		std::string getUnit() override {
			return "%";
		}
	};

	MODULE* module;
	int id;
	XyScreenAmountChangeAction<MODULE>* h;

	XyScreenAmountSlider(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		quantity = new AmountQuantity(module, id);
	}
	~XyScreenAmountSlider() {
		delete quantity;
	}

	void onDragStart(const event::DragStart& e) override {
		// history
		h = new XyScreenAmountChangeAction<MODULE>(module);
		h->moduleId = module->id;
		h->id = id;
		h->oldValue = module->amount[id];

		ui::Slider::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		h->newValue = module->amount[id];
		APP->history->push(h);
		h = NULL;

		ui::Slider::onDragEnd(e);
	}
};


template <typename MODULE>
struct XyScreenChangeAction : history::ModuleAction {
	int id;
	int type;
	float oldX, oldY;
	float newX, newY;

	XyScreenChangeAction(int64_t moduleId) {
		this->moduleId = moduleId;
		Model* m = APP->scene->rack->getModule(moduleId)->model;
		name = m->plugin->brand + " " + m->name + " x/y-change";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->screenXpq(type, id)->getParam()->setValue(oldX);
		m->screenYpq(type, id)->getParam()->setValue(oldY);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->screenXpq(type, id)->getParam()->setValue(newX);
		m->screenYpq(type, id)->getParam()->setValue(newY);
	}
};


template <typename MODULE>
struct XyScreenDragWidget : OpaqueWidget {
	const float radius = 10.f;
	const float fontsize = 13.0f;

	MODULE* module;
	NVGcolor color = nvgRGB(0x66, 0x66, 0x0);
	NVGcolor textColor = nvgRGB(0x66, 0x66, 0x0);
	int id = -1;
	int type = -1;
	
	float circleA = 1.f;
	math::Vec dragPos;
	XyScreenChangeAction<MODULE>* dragAction;

	XyScreenDragWidget() {
		box.size = Vec(2 * radius, 2 * radius);
	}

	void step() override {
		float posX = module->screenXpq(type, id)->getValue() * (parent->box.size.x - box.size.x);
		box.pos.x = posX;
		float posY = module->screenYpq(type, id)->getValue() * (parent->box.size.y - box.size.y);
		box.pos.y = posY;
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (!module) return;

		if (layer == 1) {
			Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);

			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

			if (module->screenSelectionTest(type, id)) {
				// Draw selection halo
				float oradius = 1.8f * radius;
				NVGpaint paint;
				NVGcolor icol = color::mult(color, 0.25f);
				NVGcolor ocol = nvgRGB(0, 0, 0);

				Rect b = Rect(box.pos.mult(-1), parent->box.size);
				nvgSave(args.vg);
				nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, oradius);
				paint = nvgRadialGradient(args.vg, c.x, c.y, radius, oradius, icol, ocol);
				nvgFillPaint(args.vg, paint);
				nvgFill(args.vg);
				nvgResetScissor(args.vg);
				nvgRestore(args.vg);
			}

			// Draw circle
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, radius - 2.f);
			nvgStrokeColor(args.vg, color);
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStroke(args.vg);
			nvgFillColor(args.vg, color::mult(color, 0.5f));
			nvgFill(args.vg);

			// Draw amount circle
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, radius);
			nvgStrokeColor(args.vg, color::mult(color, circleA));
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);

			nvgGlobalCompositeOperation(args.vg, NVG_ATOP);

			// Draw label
			std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			nvgFontSize(args.vg, fontsize);
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, textColor);
			nvgTextBox(args.vg, c.x - 3.f, c.y + 4.f, 120, string::f("%i", id + 1).c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}

	void onHover(const event::Hover& e) override {
		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onHover(e);
		}
	}

	void onButton(const event::Button& e) override {
		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onButton(e);
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				module->screenSelectionSet(type, id);
				e.consume(this);
			}
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				module->screenSelectionSet(type, id);
				createContextMenu();
				e.consume(this);
			}
		}
		else {
			OpaqueWidget::onButton(e);
		}
	}

	void onDragStart(const event::DragStart& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		dragPos = APP->scene->rack->getMousePos().minus(box.pos);

		// history
		dragAction = new XyScreenChangeAction<MODULE>(module->id);
		dragAction->id = id;
		dragAction->type = type;
		dragAction->oldX = module->screenXpq(type, id)->getParam()->getValue();
		dragAction->oldY = module->screenYpq(type, id)->getParam()->getValue();
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		dragAction->newX = module->screenXpq(type, id)->getParam()->getValue();
		dragAction->newY = module->screenYpq(type, id)->getParam()->getValue();
		APP->history->push(dragAction);
		dragAction = NULL;
	}

	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		math::Vec pos = APP->scene->rack->getMousePos().minus(dragPos);
		float x = pos.x / (parent->box.size.x - box.size.x);
		module->screenXpq(type, id)->setValue(std::max(0.f, std::min(1.f, x)));
		float y = pos.y / (parent->box.size.y - box.size.y);
		module->screenYpq(type, id)->setValue(std::max(0.f, std::min(1.f, y)));

		OpaqueWidget::onDragMove(e);
	}

	virtual void createContextMenu() {}
};


template <typename MODULE>
struct XyScreenWidget : OpaqueWidget {
	MODULE* module;

	XyScreenWidget(MODULE* module) {
		this->module = module;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			// Dim the display but don't darken it completely
			float b = std::max(0.4f, settings::rackBrightness);
			nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 1.f));

			float sizeX = box.size.x / 8.f;
			float sizeY = box.size.y / 8.f;

			// Draw background
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgFillColor(args.vg, nvgRGB(0, 16, 90));
			nvgFill(args.vg);

			// Draw grid
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgStrokeWidth(args.vg, 0.6f);
			for (int i = 1; i < 8; i++) {
				float a = 0.075f;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, sizeX * float(i), 0.f);
				nvgLineTo(args.vg, sizeX * float(i), box.size.y);
				nvgStrokeColor(args.vg, color::mult(color::WHITE, a));
				nvgStroke(args.vg);
			}
			for (int i = 1; i < 8; i++) {
				float a = 0.075f;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0.f, sizeY * float(i));
				nvgLineTo(args.vg, box.size.x, sizeY * float(i));
				nvgStrokeColor(args.vg, color::mult(color::WHITE, a));
				nvgStroke(args.vg);
			}

			// Draw outer rectangle
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgStrokeWidth(args.vg, 0.7f);
			nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.25f));
			nvgStroke(args.vg);
		}

		if (module && module->seqEdit < 0) {
			OpaqueWidget::drawLayer(args, layer);
		}
	}

	void onButton(const event::Button& e) override {
		if (module->seqEdit < 0) {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				module->screenSelectionReset();
			}
			OpaqueWidget::onButton(e);
			if (e.button == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && !e.isConsumed()) {
				createContextMenu();
				e.consume(this);
			}
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(module->model->name));

		struct InitItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				// history::ModuleChange
				history::ModuleChange* h = new history::ModuleChange;
				h->name = module->model->plugin->brand + " " + module->model->name + " initialize";
				h->moduleId = module->id;
				h->oldModuleJ = module->toJson();

				module->init();

				h->newModuleJ = module->toJson();
				APP->history->push(h);
			}
		};

		struct RandomizeXYItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				XyScreenChangeAction<MODULE>* actions[module->screenItemCount()];
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i] = new XyScreenChangeAction<MODULE>(module->id);
					actions[i]->id = i;
					actions[i]->oldX = module->screenXpq(0, i)->getParam()->getValue();
					actions[i]->oldY = module->screenYpq(0, i)->getParam()->getValue();
				}

				module->screenRandX();
				module->screenRandY();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i]->newX = module->screenXpq(0, i)->getParam()->getValue();
					actions[i]->newY = module->screenYpq(0, i)->getParam()->getValue();
					complexAction->push(actions[i]);
				}

				complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize x-pos & y-pos";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeXItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				XyScreenChangeAction<MODULE>* actions[module->screenItemCount()];
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i] = new XyScreenChangeAction<MODULE>(module->id);
					actions[i]->id = i;
					actions[i]->oldX = module->screenXpq(0, i)->getParam()->getValue();
					actions[i]->oldY = module->screenYpq(0, i)->getParam()->getValue();
				}

				module->screenRandX();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i]->newX = module->screenXpq(0, i)->getParam()->getValue();
					actions[i]->newY = module->screenYpq(0, i)->getParam()->getValue();
					complexAction->push(actions[i]);
				}

				complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize x-pos";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeYItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				XyScreenChangeAction<MODULE>* actions[module->screenItemCount()];
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i] = new XyScreenChangeAction<MODULE>(module->id);
					actions[i]->id = i;
					actions[i]->oldX = module->screenXpq(0, i)->getParam()->getValue();
					actions[i]->oldY = module->screenYpq(0, i)->getParam()->getValue();
				}

				module->screenRandY();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i]->newX = module->screenXpq(0, i)->getParam()->getValue();
					actions[i]->newY = module->screenYpq(0, i)->getParam()->getValue();
					complexAction->push(actions[i]);
				}

				complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize IN y-pos";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeAmountItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				XyScreenAmountChangeAction<MODULE>* actions[module->screenItemCount()];
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i] = new XyScreenAmountChangeAction<MODULE>(module);
					actions[i]->id = i;
					actions[i]->oldValue = module->amount[i];
				}

				module->screenRandAmount();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i]->newValue = module->amount[i];
					complexAction->push(actions[i]);
				}

				complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize amount";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeRadiusItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				XyScreenRadiusChangeAction<MODULE>* actions[module->screenItemCount()];
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i] = new XyScreenRadiusChangeAction<MODULE>(module);
					actions[i]->id = i;
					actions[i]->oldValue = module->radius[i];
				}

				module->screenRandRadius();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->screenItemCount(); i++) {
					actions[i]->newValue = module->radius[i];
					complexAction->push(actions[i]);
				}

				complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize radius";
				APP->history->push(complexAction);
			}
		};

		menu->addChild(construct<InitItem>(&MenuItem::text, "Initialize", &InitItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<RandomizeXYItem>(&MenuItem::text, "Radomize x-pos & y-pos", &RandomizeXYItem::module, module));
		menu->addChild(construct<RandomizeXItem>(&MenuItem::text, "Radomize x-pos", &RandomizeXItem::module, module));
		menu->addChild(construct<RandomizeYItem>(&MenuItem::text, "Radomize y-pos", &RandomizeYItem::module, module));
		menu->addChild(construct<RandomizeAmountItem>(&MenuItem::text, "Radomize amount", &RandomizeAmountItem::module, module));
		menu->addChild(construct<RandomizeRadiusItem>(&MenuItem::text, "Radomize radius", &RandomizeRadiusItem::module, module));

		appendContextMenu(menu);
	}

	virtual void appendContextMenu(Menu* menu) {}
};


} // namespace StoermelderPackOne