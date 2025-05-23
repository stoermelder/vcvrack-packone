#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

using namespace rack;


struct XyScreenParamQuantity : ParamQuantity {
	bool hasHandle = false;
};


template <uint8_t INPUTS>
struct XyScreenModule {
	float radiusUi[INPUTS];
	dsp::ExponentialFilter radiusFilter[INPUTS];
	/** [Stored to JSON] */
	float radius[INPUTS];

	float inputUiX[INPUTS];
	dsp::ExponentialFilter inputXfilter[INPUTS];
	float inputUiY[INPUTS];
	dsp::ExponentialFilter inputYfilter[INPUTS];

	/** [Stored to JSON] */
	float amount[INPUTS];

	int8_t selectedId = -1;
	int8_t selectedType = -1;

	virtual void init() {}

	virtual inline engine::ParamQuantity* screenXpq(uint8_t type, uint8_t id) {
		return NULL;
	}

	virtual inline engine::ParamQuantity* screenYpq(uint8_t type, uint8_t id) {
		return NULL;
	}

	virtual inline uint8_t screenItemCount(uint8_t type = 0) {
		return type == 0 ? INPUTS : 0;
	}

	void screenInit() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			screenXyImmediate(0, i, screenXpq(0, i)->getDefaultValue(), screenYpq(0, i)->getDefaultValue());
			inputXfilter[i].setTau(0.05f);
			inputYfilter[i].setTau(0.05f);
			screenRadiusImmediate(i, 0.5f);
			radiusFilter[i].setTau(0.05f);
			screenAmountImmediate(i, 1.f);
		};
	}

	void screenReset() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			radius[i] = 0.5f;
		}
	}
	
	void screenProcess(float sampleTime) {
		for (uint8_t i = 0; i < INPUTS; i++) {
			XyScreenParamQuantity* px = reinterpret_cast<XyScreenParamQuantity*>(screenXpq(0, i));
			if (!px->hasHandle) {
				px->getParam()->setValue(inputXfilter[i].process(sampleTime, inputUiX[i]));
			}
			else {
				inputXfilter[i].out = inputUiX[i] = px->getParam()->getValue();
			}
			XyScreenParamQuantity* py = reinterpret_cast<XyScreenParamQuantity*>(screenYpq(0, i));
			if (!py->hasHandle) {
				py->getParam()->setValue(inputYfilter[i].process(sampleTime, inputUiY[i]));
			}
			else {
				inputYfilter[i].out = inputUiY[i] = py->getParam()->getValue();
			}
			radius[i] = radiusFilter[i].process(sampleTime, radiusUi[i]);
		}
	}

	inline void screenXyFiltered(uint8_t type, uint8_t id, float x, float y) {
		if (type == 0) {
			inputUiX[id] = x;
			inputUiY[id] = y;
		}
		else {
			screenItemFiltered(type, id, x, y);
		}
	}

	virtual inline void screenItemFiltered(uint8_t type, uint8_t id, float x, float y) {}

	inline void screenXyImmediate(uint8_t type, uint8_t id, float x, float y) {
		if (type == 0) {
			screenXpq(type, id)->getParam()->setValue(x);
			inputXfilter[id].out = inputUiX[id] = x;
			screenYpq(type, id)->getParam()->setValue(y);
			inputYfilter[id].out = inputUiY[id] = y;
		}
		else {
			screenItemImmediate(type, id, x, y);
		}
	}

	virtual void screenItemImmediate(uint8_t type, uint8_t id, float x, float y) {}

	inline float screenX(uint8_t type, uint8_t id) {
		return screenXpq(type, id)->getParam()->getValue();
	}

	inline float screenY(uint8_t type, uint8_t id) {
		return screenYpq(type, id)->getParam()->getValue();
	}

	void screenRandX() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			inputXfilter[i].out = inputUiX[i] = random::uniform();
			screenXpq(0, i)->getParam()->setValue(inputUiX[i]);
		}
	}

	void screenRandY() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			inputYfilter[i].out = inputUiY[i] = random::uniform();
			screenYpq(0, i)->getParam()->setValue(inputUiY[i]);
		}
	}

	inline float screenRadius(uint8_t id) {
		return radiusUi[id];
	}

	inline void screenRadiusFiltered(uint8_t id, float r) {
		radiusUi[id] = r;
	}

	inline void screenRadiusImmediate(uint8_t id, float r) {
		radiusFilter[id].out = radius[id] = radiusUi[id] = r;
	}

	void screenRandRadius() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			screenRadiusImmediate(i, random::uniform());
		}
	}

	inline float screenAmount(uint8_t id) {
		return amount[id];
	}

	inline void screenAmountImmediate(uint8_t id, float a) {
		amount[id] = a;
	}

	void screenRandAmount() {
		for (size_t i = 0; i < INPUTS; i++) {
			screenAmountImmediate(i, random::uniform());
		}
	}

	inline void screenSelectionSet(uint8_t type, uint8_t id) {
		if (type == 0 && id + 1 > INPUTS) return;
		//if (type == 1 && id + 1 > mixportsUsed) return;
		selectedType = type;
		selectedId = id;
	}

	inline bool screenSelectionTest(uint8_t type, uint8_t id) {
		return selectedType == type && selectedId == int8_t(id);
	}

	inline void screenSelectionReset() {
		selectedType = -1;
		selectedId = -1;
	}

	void dataToJson(json_t* dataJ, size_t id) {
		json_object_set_new(dataJ, "amount", json_real(amount[id]));
		json_object_set_new(dataJ, "radius", json_real(radius[id]));
	}

	void dataFromJson(json_t* dataJ, size_t id) {
		screenRadiusImmediate(id, json_real_value(json_object_get(dataJ, "radius")));
		screenAmountImmediate(id, json_real_value(json_object_get(dataJ, "amount")));
	}
};


template <typename MODULE>
struct XyScreenRadiusChangeAction : history::ModuleAction {
	uint8_t id;
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
		m->screenRadiusImmediate(id, oldValue);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->screenRadiusImmediate(id, newValue);
	}
};


template <typename MODULE>
struct XyScreenRadiusSlider : ui::Slider {
	struct RadiusQuantity : Quantity {
		MODULE* module;
		uint8_t id;

		RadiusQuantity(MODULE* module, uint8_t id) {
			this->module = module;
			this->id = id;
		}
		void setValue(float value) override {
			value = clamp(value);
			module->screenRadiusFiltered(id, value);
		}
		float getValue() override {
			return module->screenRadius(id);
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
		box.size.x = 200.0;
		quantity = new RadiusQuantity(module, id);
	}
	~XyScreenRadiusSlider() {
		delete quantity;
	}

	void onDragStart(const event::DragStart& e) override {
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
	uint8_t id;
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
		m->screenAmountImmediate(id, oldValue);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->screenAmountImmediate(id, newValue);
	}
};


template <typename MODULE>
struct XyScreenAmountSlider : ui::Slider {
	struct AmountQuantity : Quantity {
		MODULE* module;
		uint8_t id;

		AmountQuantity(MODULE* module, uint8_t id) {
			this->module = module;
			this->id = id;
		}
		void setValue(float value) override {
			module->screenAmountImmediate(id, clamp(value));
		}
		float getValue() override {
			return module->screenAmount(id);
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
		box.size.x = 200.0;
		quantity = new AmountQuantity(module, id);
	}
	~XyScreenAmountSlider() {
		delete quantity;
	}

	void onDragStart(const event::DragStart& e) override {
		h = new XyScreenAmountChangeAction<MODULE>(module);
		h->id = id;
		h->oldValue = module->screenAmount(id);
		ui::Slider::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		h->newValue = module->screenAmount(id);
		APP->history->push(h);
		h = NULL;
		ui::Slider::onDragEnd(e);
	}
};


template <typename MODULE>
struct XyScreenChangeAction : history::ModuleAction {
	uint8_t id;
	uint8_t type;
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
		m->screenXyImmediate(type, id, oldX, oldY);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->screenXyImmediate(type, id, newX, newY);
	}
};


template <typename MODULE>
struct XyScreenDragWidget : OpaqueWidget {
	const float radius = 10.f;
	const float fontsize = 13.0f;

	MODULE* module;
	NVGcolor color = nvgRGB(0x66, 0x66, 0x0);
	NVGcolor textColor = nvgRGB(0x66, 0x66, 0x0);
	uint8_t id;
	uint8_t type;
	
	float circleA = 1.f;
	math::Vec dragPos;
	XyScreenChangeAction<MODULE>* dragAction;

	XyScreenDragWidget() {
		box.size = Vec(2 * radius, 2 * radius);
	}

	void step() override {
		float posX = module->screenX(type, id) * (parent->box.size.x - box.size.x);
		box.pos.x = posX;
		float posY = module->screenY(type, id) * (parent->box.size.y - box.size.y);
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

		dragAction = new XyScreenChangeAction<MODULE>(module->id);
		dragAction->id = id;
		dragAction->type = type;
		dragAction->oldX = module->screenX(type, id);
		dragAction->oldY = module->screenY(type, id);
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		dragAction->newX = module->screenX(type, id);
		dragAction->newY = module->screenY(type, id);
		APP->history->push(dragAction);
		dragAction = NULL;
	}

	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		math::Vec pos = APP->scene->rack->getMousePos().minus(dragPos);
		float x = pos.x / (parent->box.size.x - box.size.x);
		float y = pos.y / (parent->box.size.y - box.size.y);
		module->screenXyFiltered(type, id, clamp(x), clamp(y));

		OpaqueWidget::onDragMove(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		if (type == 0) {
			menu->addChild(createMenuLabel(getItemName()));
			menu->addChild(new XyScreenAmountSlider<MODULE>(module, id));
			menu->addChild(new XyScreenRadiusSlider<MODULE>(module, id));
		}

		appendContextMenu(menu);
	}

	virtual std::string getItemName() { return string::f("Item %i", id + 1); }
	virtual void appendContextMenu(Menu* menu) {}
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
			for (uint8_t i = 1; i < 8; i++) {
				float a = 0.075f;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, sizeX * float(i), 0.f);
				nvgLineTo(args.vg, sizeX * float(i), box.size.y);
				nvgStrokeColor(args.vg, color::mult(color::WHITE, a));
				nvgStroke(args.vg);
			}
			for (uint8_t i = 1; i < 8; i++) {
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
		menu->addChild(createMenuItem("Initialize", "", [=] {
			history::ModuleChange* h = new history::ModuleChange;
			h->name = module->model->plugin->brand + " " + module->model->name + " initialize";
			h->moduleId = module->id;
			h->oldModuleJ = module->toJson();

			module->init();

			h->newModuleJ = module->toJson();
			APP->history->push(h);
		}));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Radomize x-pos & y-pos", "", [=] {
			XyScreenChangeAction<MODULE>* actions[module->screenItemCount()];
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i] = new XyScreenChangeAction<MODULE>(module->id);
				actions[i]->id = i;
				actions[i]->oldX = module->screenX(0, i);
				actions[i]->oldY = module->screenY(0, i);
			}

			module->screenRandX();
			module->screenRandY();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i]->newX = module->screenX(0, i);
				actions[i]->newY = module->screenY(0, i);
				complexAction->push(actions[i]);
			}

			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize x-pos & y-pos";
			APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize x-pos", "", [=] {
			XyScreenChangeAction<MODULE>* actions[module->screenItemCount()];
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i] = new XyScreenChangeAction<MODULE>(module->id);
				actions[i]->id = i;
				actions[i]->oldX = module->screenX(0, i);
				actions[i]->oldY = module->screenY(0, i);
			}

			module->screenRandX();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i]->newX = module->screenX(0, i);
				actions[i]->newY = module->screenY(0, i);
				complexAction->push(actions[i]);
			}

			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize x-pos";
			APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize y-pos", "", [=] {
				XyScreenChangeAction<MODULE>* actions[module->screenItemCount()];
				for (uint8_t i = 0; i < module->screenItemCount(); i++) {
					actions[i] = new XyScreenChangeAction<MODULE>(module->id);
					actions[i]->id = i;
					actions[i]->oldX = module->screenX(0, i);
					actions[i]->oldY = module->screenY(0, i);
				}

				module->screenRandY();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (uint8_t i = 0; i < module->screenItemCount(); i++) {
					actions[i]->newX = module->screenX(0, i);
					actions[i]->newY = module->screenY(0, i);
					complexAction->push(actions[i]);
				}

				complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize IN y-pos";
				APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize amount", "", [=] {
			XyScreenAmountChangeAction<MODULE>* actions[module->screenItemCount()];
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i] = new XyScreenAmountChangeAction<MODULE>(module);
				actions[i]->id = i;
				actions[i]->oldValue = module->screenAmount(i);
			}

			module->screenRandAmount();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i]->newValue = module->screenAmount(i);
				complexAction->push(actions[i]);
			}

			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize amount";
			APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize radius", "", [=] {
			XyScreenRadiusChangeAction<MODULE>* actions[module->screenItemCount()];
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i] = new XyScreenRadiusChangeAction<MODULE>(module);
				actions[i]->id = i;
				actions[i]->oldValue = module->screenRadius(i);
			}

			module->screenRandRadius();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->screenItemCount(); i++) {
				actions[i]->newValue = module->screenRadius(i);
				complexAction->push(actions[i]);
			}

			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize radius";
			APP->history->push(complexAction);
		}));

		appendContextMenu(menu);
	}

	virtual void appendContextMenu(Menu* menu) {}
};


struct XyScreenDummyMapButton : ParamWidget {
	XyScreenDummyMapButton() {
		this->box.size = Vec(5.f, 5.f);
	}

	void draw(const DrawArgs& args) override {
		if (module) {
			ParamHandle* paramHandle = APP->engine->getParamHandle(module->getId(), paramId);
			reinterpret_cast<XyScreenParamQuantity*>(getParamQuantity())->hasHandle = paramHandle != NULL;
		}
		ParamWidget::draw(args);
	}
};

} // namespace StoermelderPackOne