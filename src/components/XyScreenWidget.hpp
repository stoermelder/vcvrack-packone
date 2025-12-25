#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

using namespace rack;


struct XyScreenParamQuantity : ParamQuantity {
	bool hasHandle = false;
};


template <uint8_t INPUTS>
struct XyScreenModule {
	/** [Stored to JSON] */
	float radius[INPUTS];
	/** [Stored to JSON] */
	float amount[INPUTS];

	float radiusUi[INPUTS];
	dsp::ExponentialFilter radiusFilter[INPUTS];
	float amountUi[INPUTS];

	float inputUiX[INPUTS];
	dsp::ExponentialFilter inputXfilter[INPUTS];
	float inputUiY[INPUTS];
	dsp::ExponentialFilter inputYfilter[INPUTS];

	int8_t selectedId = -1;
	int8_t selectedType = -1;

	virtual ~XyScreenModule() { }

	virtual inline engine::ParamQuantity* scGetPqX(uint8_t type, uint8_t id) {
		return NULL;
	}

	virtual inline engine::ParamQuantity* scGetPqY(uint8_t type, uint8_t id) {
		return NULL;
	}

	virtual inline uint8_t scGetItemCount(uint8_t type) {
		return type == 0 ? INPUTS : 0;
	}

	virtual inline uint8_t scGetItemCountActive(uint8_t type) { return 0; }

	virtual inline bool scIsActive(uint8_t type, uint8_t id) {
		return id < scGetItemCountActive(type);
	}

	void scInit() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			inputXfilter[i].setTau(0.05f);
			inputYfilter[i].setTau(0.05f);
			radiusFilter[i].setTau(0.05f);
		};
		scInitItems();
		scReset();
	}

	virtual void scInitItems() {}

	void scReset() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			scSetXyImmediate(0, i, scGetPqX(0, i)->getDefaultValue(), scGetPqY(0, i)->getDefaultValue());
			scSetRadiusImmediate(i, 0.5f);
			scSetAmountImmediate(i, 1.f);
		}
	}
	
	inline void scSetXyFiltered(uint8_t type, uint8_t id, float x, float y) {
		if (type == 0) {
			inputUiX[id] = x;
			inputUiY[id] = y;
		}
		else {
			scSetItemFiltered(type, id, x, y);
		}
	}

	virtual inline void scSetItemFiltered(uint8_t type, uint8_t id, float x, float y) {}

	inline void scSetXyImmediate(uint8_t type, uint8_t id, float x, float y) {
		if (type == 0) {
			scGetPqX(type, id)->getParam()->setValue(x);
			inputXfilter[id].out = inputUiX[id] = x;
			scGetPqY(type, id)->getParam()->setValue(y);
			inputYfilter[id].out = inputUiY[id] = y;
		}
		else {
			scSetItemImmediate(type, id, x, y);
		}
	}

	virtual void scSetItemImmediate(uint8_t type, uint8_t id, float x, float y) {}

	virtual inline float scGetXFinal(uint8_t type, uint8_t id) { return 0; }

	inline float scGetXFiltered(uint8_t id, float sampleTime) {
		return inputXfilter[id].process(sampleTime, inputUiX[id]);
	}

	virtual inline float scGetYFinal(uint8_t type, uint8_t id) { return 0; }

	inline float scGetYFiltered(uint8_t id, float sampleTime) {
		return inputYfilter[id].process(sampleTime, inputUiY[id]);
	}

	void scRandomizeXAll() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			inputXfilter[i].out = inputUiX[i] = random::uniform();
			scGetPqX(0, i)->getParam()->setValue(inputUiX[i]);
		}
	}

	void scRandomizeYAll() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			inputYfilter[i].out = inputUiY[i] = random::uniform();
			scGetPqY(0, i)->getParam()->setValue(inputUiY[i]);
		}
	}

	virtual inline float scGetRadiusFinal(uint8_t id) { 
		return radius[id];
	}

	inline void scSetRadiusFinal(uint8_t id, float a) { 
		radius[id] = a;
	}

	inline float scGetRadiusFiltered(uint8_t id, float sampleTime) {
		return radiusFilter[id].process(sampleTime, radiusUi[id]);
	}

	inline void scSetRadiusFiltered(uint8_t id, float r) {
		radiusUi[id] = r;
	}

	inline void scSetRadiusImmediate(uint8_t id, float r) {
		radiusFilter[id].out = radiusUi[id] = r;
	}

	void scRandomizeRadiusAll() {
		for (uint8_t i = 0; i < INPUTS; i++) {
			scSetRadiusImmediate(i, random::uniform());
		}
	}

	virtual inline float scGetAmountFinal(uint8_t id) { 
		return amount[id];
	}

	inline void scSetAmountFinal(uint8_t id, float a) { 
		amount[id] = a;
	}

	inline float scGetAmountFiltered(uint8_t id, float sampleTime) {
		return amountUi[id];
	}

	inline void scSetAmountImmediate(uint8_t id, float a) {
		amountUi[id] = a;
	}

	inline void scSetAmountFiltered(uint8_t id, float r) {
		amountUi[id] = r;
	}

	void scRandomizeAmountAll() {
		for (size_t i = 0; i < INPUTS; i++) {
			scSetAmountImmediate(i, random::uniform());
		}
	}

	inline void scSetSelection(uint8_t type, uint8_t id) {
		if (type == 0 && id + 1 > INPUTS) return;
		//if (type == 1 && id + 1 > mixportsUsed) return;
		selectedType = type;
		selectedId = id;
	}

	inline bool scIsSelected(uint8_t type, uint8_t id) {
		return selectedType == type && selectedId == int8_t(id);
	}

	inline void scResetSelection() {
		selectedType = -1;
		selectedId = -1;
	}

	virtual inline float scGetDistance(uint8_t typeSource, uint8_t idSoruce, uint8_t typeDest, uint8_t idDest) { return 0.f; }

	void dataToJson(json_t* dataJ, size_t id) {
		json_object_set_new(dataJ, "radius", json_real(scGetRadiusFinal(id)));
		json_object_set_new(dataJ, "amount", json_real(scGetAmountFinal(id)));
	}

	void dataFromJson(json_t* dataJ, size_t id) {
		scSetRadiusImmediate(id, json_real_value(json_object_get(dataJ, "radius")));
		scSetAmountImmediate(id, json_real_value(json_object_get(dataJ, "amount")));
	}

	std::string getModuleName() {
		Module* m = dynamic_cast<Module*>(this);
		return m->model->plugin->brand + " " + m->model->name;
	}
};


struct XyScreenDummyModule : XyScreenModule<0> {
	std::map<uint8_t, size_t> typeCount;
	std::map<uint8_t, float> radius;
	std::map<std::tuple<uint8_t, uint8_t>, float> x, y;
	std::map<std::tuple<uint8_t, uint8_t, uint8_t>, float> dist;

	int64_t getId() {
		return 0;
	}

	void initType(uint8_t type, uint8_t count) {
		typeCount[type] = count;
		if (type == 0) {
			for (uint8_t i = 0; i < count; i++) {
				x[std::make_tuple(type, i)] = random::uniform();
				y[std::make_tuple(type, i)] = 0.1f + random::uniform() * 0.5f;
				radius[i] = 0.2f + random::uniform() * 0.3f;
			}
			selectedType = 0;
			selectedId = 0;
		}
		else {
			for (uint8_t i = 0; i < count; i++) {
				x[std::make_tuple(type, i)] = random::uniform();
				y[std::make_tuple(type, i)] = 0.4f + random::uniform() * 0.5f;
				Vec mixVec = Vec(x[std::make_tuple(type, i)], y[std::make_tuple(type, i)]);
				for (uint8_t j = 0; j < typeCount[0]; j++) {
					Vec inVec = Vec(x[std::make_tuple(0, j)], y[std::make_tuple(0, j)]);
					dist[std::make_tuple(type, i, j)] = inVec.minus(mixVec).norm();
				}
			}
		}
	}

	uint8_t scGetItemCountActive(uint8_t type) override {
		return typeCount[type];
	}

	bool scIsActive(uint8_t type, uint8_t id) override {
		return true;
	}

	float scGetXFinal(uint8_t type, uint8_t id) override {
		return x[std::make_tuple(type, id)];
	}

	float scGetYFinal(uint8_t type, uint8_t id) override {
		return y[std::make_tuple(type, id)];
	}
	
	float scGetAmountFinal(uint8_t id) override {
		return 1.f;
	}

	float scGetRadiusFinal(uint8_t id) override {
		return radius[id];
	}

	float scGetDistance(uint8_t typeSource, uint8_t idSource, uint8_t typeDest, uint8_t idDest) override {
		return dist[std::make_tuple(typeSource, idSource, idDest)];
	}
};

template <typename MODULE>
struct XyScreenRadiusChangeAction : history::ModuleAction {
	uint8_t id;
	float oldValue;
	float newValue;

	XyScreenRadiusChangeAction(MODULE* m) {
		name = m->getModuleName() + " radius change";
		moduleId = m->getId();
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->scSetRadiusImmediate(id, oldValue);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->scSetRadiusImmediate(id, newValue);
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
			module->scSetRadiusFiltered(id, value);
		}
		float getValue() override {
			return module->scGetRadiusFinal(id);
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
		h->moduleId = module->getId();
		h->id = id;
		h->oldValue = module->scGetRadiusFinal(id);
		ui::Slider::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		h->newValue = module->scGetRadiusFinal(id);
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
		name = m->getModuleName() + " amount change";
		moduleId = m->getId();
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->scSetAmountImmediate(id, oldValue);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->scSetAmountImmediate(id, newValue);
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
			module->scSetAmountFiltered(id, clamp(value));
		}
		float getValue() override {
			return module->scGetAmountFinal(id);
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
		h->oldValue = module->scGetAmountFinal(id);
		ui::Slider::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		h->newValue = module->scGetAmountFinal(id);
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
		m->scSetXyImmediate(type, id, oldX, oldY);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->scSetXyImmediate(type, id, newX, newY);
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
		if (type == 0) {
			circleA = module->scGetAmountFinal(id);
		}

		float posX = module->scGetXFinal(type, id) * (parent->box.size.x - box.size.x);
		box.pos.x = posX;
		float posY = module->scGetYFinal(type, id) * (parent->box.size.y - box.size.y);
		box.pos.y = posY;
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (!module)
			return;

		if (layer == 1) {
			if (!module->scIsActive(type, id))
				return;

			if (type == 0) {
				// Radius is only used for default type
				if (module->scIsSelected(type, id)) {
					// Draw circle and fill for radius-property
					Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);
					Rect b = Rect(box.pos.mult(-1), parent->box.size);
					nvgSave(args.vg);
					nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
					float sizeX = std::max(0.f, (parent->box.size.x - 2 * radius) * module->scGetRadiusFinal(id) - radius);
					float sizeY = std::max(0.f, (parent->box.size.y - 2 * radius) * module->scGetRadiusFinal(id) - radius);
					nvgBeginPath(args.vg);
					nvgEllipse(args.vg, c.x, c.y, sizeX, sizeY);
					nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
					nvgStrokeColor(args.vg, color::mult(color, 0.7f));
					nvgStrokeWidth(args.vg, 0.6f);
					nvgStroke(args.vg);
					nvgFillColor(args.vg, color::mult(color, 0.1f));
					nvgFill(args.vg);
					nvgResetScissor(args.vg);
					nvgRestore(args.vg);

					textColor = nvgRGBA(0, 16, 90, 200);
				}
				else {
					textColor = color;
				}
			}

			if (type > 0) {
				// Draw lines between type 0 and other items
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);
				float sizeX = parent->box.size.x;
				float sizeY = parent->box.size.y;
				for (uint8_t i = 0; i < module->scGetItemCountActive(0); i++) {
					if (module->scGetDistance(type, id, 0, i) < module->scGetRadiusFinal(i)) {
						float x = module->scGetXFinal(0, i) * (sizeX - 2.f * radius);
						float y = module->scGetYFinal(0, i) * (sizeY - 2.f * radius);
						Vec p = box.pos.mult(-1).plus(Vec(x, y)).plus(c);
						Vec p_rad = p.minus(c).normalize().mult(radius);
						Vec s = c.plus(p_rad);
						Vec t = p.minus(p_rad);
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, s.x, s.y);
						nvgLineTo(args.vg, t.x, t.y);
						nvgStrokeColor(args.vg, color::mult(nvgRGB(0x29, 0xb2, 0xef), module->scGetAmountFinal(i)));
						nvgStrokeWidth(args.vg, 1.0f);
						nvgStroke(args.vg);
					}
				}
			}

			Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

			if (module->scIsSelected(type, id)) {
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

				textColor = nvgRGBA(0, 16, 90, 200);
			}
			else {
				textColor = color;
			}

			// Draw inner circle
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

			// Draw text label
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
		if (!module->scIsActive(type, id))
			return;

		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onButton(e);
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				module->scSetSelection(type, id);
				e.consume(this);
			}
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				module->scSetSelection(type, id);
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

		dragAction = new XyScreenChangeAction<MODULE>(module->getId());
		dragAction->id = id;
		dragAction->type = type;
		dragAction->oldX = module->scGetXFinal(type, id);
		dragAction->oldY = module->scGetYFinal(type, id);
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		dragAction->newX = module->scGetXFinal(type, id);
		dragAction->newY = module->scGetYFinal(type, id);
		APP->history->push(dragAction);
		dragAction = NULL;
	}

	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		math::Vec pos = APP->scene->rack->getMousePos().minus(dragPos);
		float x = pos.x / (parent->box.size.x - box.size.x);
		float y = pos.y / (parent->box.size.y - box.size.y);
		module->scSetXyFiltered(type, id, clamp(x), clamp(y));

		OpaqueWidget::onDragMove(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(getItemName()));
		if (type == 0) {
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
	XyScreenDummyModule* dummyModule = NULL;

	XyScreenWidget(MODULE* module) {
		this->module = module;
	}

	~XyScreenWidget() {
		if (dummyModule) {
			delete dummyModule;
		}
	}

	template <typename WIDGET>
	void createDragWidgets(MODULE* module, uint8_t type, uint8_t count, NVGcolor color) {
		// This is some over-complicated code for drawing something on the display in the module browser.
		// We "inject" some nodes using a dummy module, satisfying the nodes methods for the display.
		if (!module && !dummyModule) {
			dummyModule = new XyScreenDummyModule;
		}
		if (dummyModule) {
			dummyModule->initType(type, count);
		}

		for (uint8_t i = 0; i < count; i++) {
			if (module) {
				XyScreenDragWidget<MODULE>* w = new WIDGET;
				w->module = module;
				w->id = i;
				w->type = type;
				w->color = color;
				addChild(w);
			}
			else {
				XyScreenDragWidget<XyScreenDummyModule>* w = new XyScreenDragWidget<XyScreenDummyModule>;
				w->module = dummyModule;
				w->id = i;
				w->type = type;
				w->color = color;
				addChild(w);
			}
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			// Dim the display but don't darken it completely
			float b = std::max(0.2f, settings::rackBrightness);
			nvgGlobalAlpha(args.vg, b);

			float sizeX = box.size.x / 8.f;
			float sizeY = box.size.y / 8.f;

			// Draw background
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgFillColor(args.vg, nvgRGB(0, 16, 90));
			nvgFill(args.vg);

			// Draw gradient
			math::Rect r = box.zeroPos();
			nvgBeginPath(args.vg);
			nvgRect(args.vg, RECT_ARGS(r));
			NVGcolor topColor = nvgRGBA(200, 200, 200, 40);
			NVGcolor bottomColor = nvgRGBA(200, 200, 200, 0);
			nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, 0.f, 0.f, 80.f, topColor, bottomColor));
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

		if (!module || module->seqEdit < 0) {
			OpaqueWidget::drawLayer(args, layer);
		}
	}

	void onButton(const event::Button& e) override {
		if (module->seqEdit < 0) {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				module->scResetSelection();
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

			module->scInit();

			h->newModuleJ = module->toJson();
			APP->history->push(h);
		}));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Radomize x-pos & y-pos", "", [=] {
			XyScreenChangeAction<MODULE>* actions[module->scGetItemCount(0)];
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i] = new XyScreenChangeAction<MODULE>(module->id);
				actions[i]->id = i;
				actions[i]->oldX = module->scGetXFinal(0, i);
				actions[i]->oldY = module->scGetYFinal(0, i);
			}

			module->scRandomizeXAll();
			module->scRandomizeYAll();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i]->newX = module->scGetXFinal(0, i);
				actions[i]->newY = module->scGetYFinal(0, i);
				complexAction->push(actions[i]);
			}
			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize x-pos & y-pos";
			APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize x-pos", "", [=] {
			XyScreenChangeAction<MODULE>* actions[module->scGetItemCount(0)];
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i] = new XyScreenChangeAction<MODULE>(module->id);
				actions[i]->id = i;
				actions[i]->oldX = module->scGetXFinal(0, i);
				actions[i]->oldY = module->scGetYFinal(0, i);
			}

			module->scRandomizeXAll();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i]->newX = module->scGetXFinal(0, i);
				actions[i]->newY = module->scGetYFinal(0, i);
				complexAction->push(actions[i]);
			}
			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize x-pos";
			APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize y-pos", "", [=] {
				XyScreenChangeAction<MODULE>* actions[module->scGetItemCount(0)];
				for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
					actions[i] = new XyScreenChangeAction<MODULE>(module->id);
					actions[i]->id = i;
					actions[i]->oldX = module->scGetXFinal(0, i);
					actions[i]->oldY = module->scGetYFinal(0, i);
				}

				module->scRandomizeYAll();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
					actions[i]->newX = module->scGetXFinal(0, i);
					actions[i]->newY = module->scGetYFinal(0, i);
					complexAction->push(actions[i]);
				}
				complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize IN y-pos";
				APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize amount", "", [=] {
			XyScreenAmountChangeAction<MODULE>* actions[module->scGetItemCount(0)];
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i] = new XyScreenAmountChangeAction<MODULE>(module);
				actions[i]->id = i;
				actions[i]->oldValue = module->scGetAmountFinal(i);
			}

			module->scRandomizeAmountAll();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i]->newValue = module->scGetAmountFinal(i);
				complexAction->push(actions[i]);
			}
			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize amount";
			APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize radius", "", [=] {
			XyScreenRadiusChangeAction<MODULE>* actions[module->scGetItemCount(0)];
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i] = new XyScreenRadiusChangeAction<MODULE>(module);
				actions[i]->id = i;
				actions[i]->oldValue = module->scGetRadiusFinal(i);
			}

			module->scRandomizeRadiusAll();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->scGetItemCount(0); i++) {
				actions[i]->newValue = module->scGetRadiusFinal(i);
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