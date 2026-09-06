#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

using namespace rack;


struct XyScreenParamQuantity : ParamQuantity {
	bool hasHandle = false;
};


/** Which kind of XyScreen item is selected, replacing the (type, id) pair with
 * a value that states its own kind instead of encoding it as an integer. */
struct XyScreenSelection {
	enum class Kind : uint8_t { NONE, NODE, CURSOR };
	Kind kind = Kind::NONE;
	uint8_t id = 0;

	XyScreenSelection() {}
	XyScreenSelection(Kind kind, uint8_t id) : kind(kind), id(id) {}

	bool isNode(uint8_t i) const {
		return kind == Kind::NODE && id == i;
	}
	bool isCursor(uint8_t i) const {
		return kind == Kind::CURSOR && id == i;
	}
};


/** The driven point of an XyScreen (formerly "type 1"). Deliberately not a
 * mirror of XyScreenNodes: it has no radius and no amount, because those were
 * never stored for it — only ever read/written through the node (type-0)
 * path. It is an interface, not a struct, because the position genuinely
 * lives in the owning module (driven by params, CV, or a motion sequence)
 * rather than in storage this component could own. */
struct XyScreenCursor {
	virtual ~XyScreenCursor() = default;
	virtual uint8_t cursorCount() const = 0;
	/** How many of [0, cursorCount()) are currently active/visible. Defaults
	 * to the full count, matching a single always-active cursor (TransitPad's
	 * Out); a module with a variable cursor count (Arena's MIX ports) overrides it. */
	virtual uint8_t cursorCountActive() const { return cursorCount(); }
	virtual bool isCursorActive(uint8_t id) const { return id < cursorCountActive(); }
	/** The param-backed position — what process() actually drives (from CV,
	 * the XY sequencer, or a ParamHandle) and what the widget draws. Mirrors
	 * XyScreenNodes::getXFinal/getYFinal; the UI shadow variable a module
	 * keeps alongside it (mixUiX, outUiX, ...) is written by mouse drag and
	 * setCursorXyImmediate/Filtered but not by those other inputs, so it must
	 * not be what the widget reads. */
	virtual float getCursorXFinal(uint8_t id) const = 0;
	virtual float getCursorYFinal(uint8_t id) const = 0;
	virtual void setCursorXyImmediate(uint8_t id, float x, float y) = 0;
	virtual void setCursorXyFiltered(uint8_t id, float x, float y) = 0;
	virtual NVGcolor getCursorColor(uint8_t id) const { return color::YELLOW; }
};


// Forward-declared so XyScreenNodes can hold a pointer back to its owning
// XyScreenModule before that template is defined below.
template <uint8_t INPUTS>
struct XyScreenModule;


/** Storage and accessors for the static XyScreen targets (formerly "type 0").
 * Every member here already means "node", so `type` is dropped entirely from
 * the API. Position is authoritatively a Rack ParamQuantity, reached through
 * a back-pointer to the owning XyScreenModule set once via init() —
 * XyScreenNodes is a plain contained member, not a base class, so it cannot
 * resolve a per-module virtual on itself the way XyScreenModule can. The
 * pointer resolves getNodePqX/getNodePqY/getNodeRadiusDefault/getNodeAmountDefault
 * through XyScreenModule's own virtual dispatch — a direct call, not a
 * type-erased std::function, since XyScreenModule already declares exactly
 * the virtuals this needs and every real owner derives from it. */
template <uint8_t COUNT>
struct XyScreenNodes {
	static constexpr uint8_t count = COUNT;

	/** [Stored to JSON] */
	float radius[COUNT];
	/** [Stored to JSON] */
	float amount[COUNT];

	float radiusUi[COUNT];
	dsp::ExponentialFilter radiusFilter[COUNT];
	float amountUi[COUNT];

	float uiX[COUNT];
	dsp::ExponentialFilter xFilter[COUNT];
	float uiY[COUNT];
	dsp::ExponentialFilter yFilter[COUNT];

	XyScreenModule<COUNT>* owner = nullptr;

	void init(XyScreenModule<COUNT>* owner) {
		this->owner = owner;
		for (uint8_t i = 0; i < COUNT; i++) {
			xFilter[i].setTau(0.05f);
			yFilter[i].setTau(0.05f);
			radiusFilter[i].setTau(0.05f);
		}
	}

	void reset();

	inline float getX(uint8_t id) const {
		return uiX[id];
	}
	inline float getY(uint8_t id) const {
		return uiY[id];
	}

	inline void setXyFiltered(uint8_t id, float x, float y) {
		if (id >= COUNT) return;
		uiX[id] = x;
		uiY[id] = y;
	}

	inline void setXyImmediate(uint8_t id, float x, float y);

	inline float getXFinal(uint8_t id) const;

	inline float getXFiltered(uint8_t id, float sampleTime) {
		return xFilter[id].process(sampleTime, uiX[id]);
	}

	inline float getYFinal(uint8_t id) const;

	inline float getYFiltered(uint8_t id, float sampleTime) {
		return yFilter[id].process(sampleTime, uiY[id]);
	}

	void randomizeXAll();
	void randomizeYAll();

	inline float getRadius(uint8_t id) const {
		return radius[id];
	}
	inline void setRadius(uint8_t id, float r) {
		radius[id] = r;
	}
	inline float getRadiusRaw(uint8_t id, float sampleTime) const {
		return radiusUi[id];
	}
	inline float getRadiusFiltered(uint8_t id, float sampleTime) {
		return radiusFilter[id].process(sampleTime, radiusUi[id]);
	}
	inline void setRadiusFiltered(uint8_t id, float r) {
		if (id >= COUNT) return;
		radiusUi[id] = r;
	}
	inline void setRadiusImmediate(uint8_t id, float r) {
		if (id >= COUNT) return;
		radiusFilter[id].out = radiusUi[id] = r;
	}
	void randomizeRadiusAll() {
		for (uint8_t i = 0; i < COUNT; i++) {
			setRadiusImmediate(i, random::uniform());
		}
	}

	inline float getAmount(uint8_t id) const {
		return amount[id];
	}
	inline void setAmount(uint8_t id, float a) {
		amount[id] = a;
	}
	inline float getAmountFiltered(uint8_t id, float sampleTime) const {
		return amountUi[id];
	}
	inline void setAmountImmediate(uint8_t id, float a) {
		if (id >= COUNT) return;
		amountUi[id] = a;
	}
	inline void setAmountFiltered(uint8_t id, float r) {
		if (id >= COUNT) return;
		amountUi[id] = r;
	}
	void randomizeAmountAll() {
		for (uint8_t i = 0; i < COUNT; i++) {
			setAmountImmediate(i, random::uniform());
		}
	}

	void dataToJson(json_t* dataJ, uint8_t id) {
		json_object_set_new(dataJ, "radius", json_real(getRadius(id)));
		json_object_set_new(dataJ, "amount", json_real(getAmount(id)));
	}

	void dataFromJson(json_t* dataJ, uint8_t id) {
		setRadiusImmediate(id, json_real_value(json_object_get(dataJ, "radius")));
		setAmountImmediate(id, json_real_value(json_object_get(dataJ, "amount")));
		setXyImmediate(id, getXFinal(id), getYFinal(id));
	}
};


template <uint8_t INPUTS>
struct XyScreenModule {
	XyScreenNodes<INPUTS> nodes;

	XyScreenSelection selection;

	virtual ~XyScreenModule() { }

	virtual inline engine::ParamQuantity* getNodePqX(uint8_t id) {
		return NULL;
	}

	virtual inline engine::ParamQuantity* getNodePqY(uint8_t id) {
		return NULL;
	}

	virtual inline uint8_t nodeCount() {
		return INPUTS;
	}

	virtual inline uint8_t nodeCountActive() {
		return 0;
	}

	virtual inline bool isNodeActive(uint8_t id) {
		return id < nodeCountActive();
	}

	void initNodes() {
		nodes.init(this);
		initExtra();
		resetNodes();
	}

	virtual void initExtra() {}

	void resetNodes() {
		nodes.reset();
	}

	virtual inline float getNodeXFinal(uint8_t id) {
		return nodes.getXFinal(id);
	}
	virtual inline float getNodeYFinal(uint8_t id) {
		return nodes.getYFinal(id);
	}

	virtual inline float getNodeRadiusDefault(uint8_t id) {
		return 0.5f;
	}
	virtual inline float getNodeRadiusFinal(uint8_t id) {
		return nodes.getRadius(id);
	}

	virtual inline float getNodeAmountDefault(uint8_t id) {
		return 1.f;
	}
	virtual inline float getNodeAmountFinal(uint8_t id) {
		return nodes.getAmount(id);
	}

	virtual inline NVGcolor getNodeColor(uint8_t id) {
		return color::WHITE;
	}

	virtual inline float getCursorToNodeDistance(uint8_t cursorId, uint8_t nodeId) {
		return 0.f;
	}

	std::string getModuleName() {
		Module* m = dynamic_cast<Module*>(this);
		return m->model->plugin->brand + " " + m->model->name;
	}
};


// XyScreenNodes methods that call through `owner` — defined out-of-line
// because they need XyScreenModule's complete type (not just the forward
// declaration above) to resolve its virtuals.

template <uint8_t COUNT>
inline void XyScreenNodes<COUNT>::reset() {
	for (uint8_t i = 0; i < COUNT; i++) {
		setXyImmediate(i, owner->getNodePqX(i)->getDefaultValue(), owner->getNodePqY(i)->getDefaultValue());
		setRadiusImmediate(i, owner->getNodeRadiusDefault(i));
		setAmountImmediate(i, owner->getNodeAmountDefault(i));
	}
}

template <uint8_t COUNT>
inline void XyScreenNodes<COUNT>::setXyImmediate(uint8_t id, float x, float y) {
	if (id >= COUNT) return;
	owner->getNodePqX(id)->getParam()->setValue(x);
	xFilter[id].out = uiX[id] = x;
	owner->getNodePqY(id)->getParam()->setValue(y);
	yFilter[id].out = uiY[id] = y;
}

template <uint8_t COUNT>
inline float XyScreenNodes<COUNT>::getXFinal(uint8_t id) const {
	return owner->getNodePqX(id)->getParam()->getValue();
}

template <uint8_t COUNT>
inline float XyScreenNodes<COUNT>::getYFinal(uint8_t id) const {
	return owner->getNodePqY(id)->getParam()->getValue();
}

template <uint8_t COUNT>
inline void XyScreenNodes<COUNT>::randomizeXAll() {
	for (uint8_t i = 0; i < COUNT; i++) {
		xFilter[i].out = uiX[i] = random::uniform();
		owner->getNodePqX(i)->getParam()->setValue(uiX[i]);
	}
}

template <uint8_t COUNT>
inline void XyScreenNodes<COUNT>::randomizeYAll() {
	for (uint8_t i = 0; i < COUNT; i++) {
		yFilter[i].out = uiY[i] = random::uniform();
		owner->getNodePqY(i)->getParam()->setValue(uiY[i]);
	}
}


/** Browser-preview scaffolding: draws plausible-looking nodes/cursors on the
 * module-browser thumbnail, where there is no real module to read state from.
 * Has no persistence, so unlike XyScreenModule/XyScreenNodes it can use a
 * fixed maximum size and plain arrays instead of runtime-sized storage. */
struct XyScreenDummyModule : XyScreenModule<32>, XyScreenCursor {
	static constexpr uint8_t MAX = 32;

	uint8_t nodesActive = 0;
	float nodeX[MAX], nodeY[MAX], nodeRadius[MAX];

	uint8_t cursorsActive = 0;
	float cursorPosX[MAX], cursorPosY[MAX];
	float cursorToNodeDist[MAX][MAX];

	int64_t getId() {
		return 0;
	}

	void initNodes(uint8_t count) {
		nodesActive = count;
		for (uint8_t i = 0; i < count; i++) {
			nodeX[i] = random::uniform();
			nodeY[i] = 0.1f + random::uniform() * 0.5f;
			nodeRadius[i] = 0.2f + random::uniform() * 0.3f;
		}
		selection.kind = XyScreenSelection::Kind::NODE;
		selection.id = 0;
	}

	void initCursors(uint8_t count) {
		cursorsActive = count;
		for (uint8_t i = 0; i < count; i++) {
			cursorPosX[i] = random::uniform();
			cursorPosY[i] = 0.4f + random::uniform() * 0.5f;
			Vec cursorVec = Vec(cursorPosX[i], cursorPosY[i]);
			for (uint8_t j = 0; j < nodesActive; j++) {
				Vec nodeVec = Vec(nodeX[j], nodeY[j]);
				cursorToNodeDist[i][j] = nodeVec.minus(cursorVec).norm();
			}
		}
	}

	uint8_t nodeCountActive() override {
		return nodesActive;
	}

	bool isNodeActive(uint8_t id) override {
		return true;
	}

	float getNodeXFinal(uint8_t id) override {
		return nodeX[id];
	}

	float getNodeYFinal(uint8_t id) override {
		return nodeY[id];
	}

	float getNodeAmountFinal(uint8_t id) override {
		return 1.f;
	}

	float getNodeRadiusFinal(uint8_t id) override {
		return nodeRadius[id];
	}

	uint8_t cursorCount() const override {
		return cursorsActive;
	}

	bool isCursorActive(uint8_t id) const override {
		return true;
	}

	float getCursorXFinal(uint8_t id) const override {
		return cursorPosX[id];
	}

	float getCursorYFinal(uint8_t id) const override {
		return cursorPosY[id];
	}

	void setCursorXyImmediate(uint8_t id, float x, float y) override {}
	void setCursorXyFiltered(uint8_t id, float x, float y) override {}

	float getCursorToNodeDistance(uint8_t cursorId, uint8_t nodeId) override {
		return cursorToNodeDist[cursorId][nodeId];
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
		m->nodes.setRadiusImmediate(id, oldValue);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->nodes.setRadiusImmediate(id, newValue);
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
			module->nodes.setRadiusFiltered(id, value);
		}
		float getValue() override {
			return module->getNodeRadiusFinal(id);
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
		h->oldValue = module->getNodeRadiusFinal(id);
		ui::Slider::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		h->newValue = module->getNodeRadiusFinal(id);
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
		m->nodes.setAmountImmediate(id, oldValue);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->nodes.setAmountImmediate(id, newValue);
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
			module->nodes.setAmountFiltered(id, clamp(value));
		}
		float getValue() override {
			return module->getNodeAmountFinal(id);
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
		h->oldValue = module->getNodeAmountFinal(id);
		ui::Slider::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		h->newValue = module->getNodeAmountFinal(id);
		APP->history->push(h);
		h = NULL;
		ui::Slider::onDragEnd(e);
	}
};


template <typename MODULE>
struct XyScreenNodeChangeAction : history::ModuleAction {
	uint8_t id;
	float oldX, oldY;
	float newX, newY;

	XyScreenNodeChangeAction(int64_t moduleId) {
		this->moduleId = moduleId;
		Model* m = APP->scene->rack->getModule(moduleId)->model;
		name = m->plugin->brand + " " + m->name + " x/y-change";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->nodes.setXyImmediate(id, oldX, oldY);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->nodes.setXyImmediate(id, newX, newY);
	}
};


template <typename MODULE>
struct XyScreenCursorChangeAction : history::ModuleAction {
	uint8_t id;
	float oldX, oldY;
	float newX, newY;

	XyScreenCursorChangeAction(int64_t moduleId) {
		this->moduleId = moduleId;
		Model* m = APP->scene->rack->getModule(moduleId)->model;
		name = m->plugin->brand + " " + m->name + " x/y-change";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->setCursorXyImmediate(id, oldX, oldY);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->getModule());
		m->setCursorXyImmediate(id, newX, newY);
	}
};


/** Shared geometry, dragging, and context-menu skeleton for one draggable
 * point on the XY screen. Subclasses (node vs. cursor) supply how to read/
 * write position and selection, and what extra drawing happens per-kind —
 * the radius halo for a node, the connector lines to nearby nodes for a
 * cursor. This deletes the `if (type == ...)` branches that used to carry
 * both bodies in one widget, rather than relocating them. */
template <typename MODULE>
struct XyScreenDragWidgetBase : OpaqueWidget {
	const float radius = 10.f;
	const float fontsize = 13.0f;

	MODULE* module = NULL;
	NVGcolor textColor = nvgRGB(0x66, 0x66, 0x0);
	uint8_t id = 0;

	float circleA = 1.f;
	math::Vec dragPos = Vec();

	XyScreenDragWidgetBase() {
		box.size = Vec(2 * radius, 2 * radius);
	}

	virtual float getX() = 0;
	virtual float getY() = 0;
	virtual void setXyFiltered(float x, float y) = 0;
	virtual void setXyImmediate(float x, float y) = 0;
	virtual bool isActive() = 0;
	virtual bool isSelected() = 0;
	virtual void setSelected() = 0;
	virtual NVGcolor getColor() = 0;
	/** Extra layer-1 drawing specific to node or cursor, run before the
	 * shared selection halo / amount circle / label. */
	virtual void drawExtra(const Widget::DrawArgs& args, NVGcolor cc) {}
	/** Label color while selected. A node darkens its label (it sits inside
	 * its own radius halo); a cursor keeps the normal color. */
	virtual NVGcolor getSelectedTextColor(NVGcolor cc) { return cc; }
	virtual void pushChangeAction(float oldX, float oldY, float newX, float newY) = 0;

	void step() override {
		float posX = getX() * (parent->box.size.x - box.size.x);
		box.pos.x = posX;
		float posY = getY() * (parent->box.size.y - box.size.y);
		box.pos.y = posY;
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (!module) return;

		if (layer == 1) {
			if (!isActive()) return;

			NVGcolor cc = getColor();

			drawExtra(args, cc);

			Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

			if (isSelected()) {
				// Draw selection halo
				float oradius = 1.8f * radius;
				NVGpaint paint;
				NVGcolor icol = color::mult(cc, 0.25f);
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

				textColor = getSelectedTextColor(cc);
			}
			else {
				textColor = cc;
			}

			// Draw amount circle
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, radius);
			nvgStrokeColor(args.vg, color::mult(cc, circleA));
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);

			nvgGlobalCompositeOperation(args.vg, NVG_ATOP);

			// Draw text label
			std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			nvgFontSize(args.vg, fontsize);
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, textColor);
			char buf[2] = { getItemChar(), '\0' };
			nvgTextBox(args.vg, c.x - 3.f, c.y + 4.f, 120, buf, NULL);
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
		if (!isActive())
			return;

		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onButton(e);
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				setSelected();
				e.consume(this);
			}
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				setSelected();
				createContextMenu();
				e.consume(this);
			}
		}
		else {
			OpaqueWidget::onButton(e);
		}
	}

	float dragOldX, dragOldY;

	void onDragStart(const event::DragStart& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		dragPos = APP->scene->rack->getMousePos().minus(box.pos);
		dragOldX = getX();
		dragOldY = getY();
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		pushChangeAction(dragOldX, dragOldY, getX(), getY());
	}

	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		math::Vec pos = APP->scene->rack->getMousePos().minus(dragPos);
		float x = pos.x / (parent->box.size.x - box.size.x);
		float y = pos.y / (parent->box.size.y - box.size.y);
		setXyFiltered(clamp(x), clamp(y));

		OpaqueWidget::onDragMove(e);
	}

	virtual void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(getItemName()));
		prependContextMenu(menu);
		appendContextMenu(menu);
	}

	virtual inline std::string getItemName() {
		return string::f("Item %i", id + 1);
	}

	virtual inline char getItemChar() {
		return '1' + id;
	}

	virtual void prependContextMenu(Menu* menu) {}
	virtual void appendContextMenu(Menu* menu) {}
};


template <typename MODULE>
struct XyScreenNodeDragWidget : XyScreenDragWidgetBase<MODULE> {
	typedef XyScreenDragWidgetBase<MODULE> B;

	void step() override {
		B::circleA = B::module->getNodeAmountFinal(B::id);
		B::step();
	}

	float getX() override {
		return B::module->getNodeXFinal(B::id);
	}
	float getY() override {
		return B::module->getNodeYFinal(B::id);
	}
	void setXyFiltered(float x, float y) override {
		B::module->nodes.setXyFiltered(B::id, x, y);
	}
	void setXyImmediate(float x, float y) override {
		B::module->nodes.setXyImmediate(B::id, x, y);
	}
	bool isActive() override {
		return B::module->isNodeActive(B::id);
	}
	bool isSelected() override {
		return B::module->selection.isNode(B::id);
	}
	void setSelected() override {
		B::module->selection = { XyScreenSelection::Kind::NODE, B::id };
	}
	NVGcolor getColor() override {
		return B::module->getNodeColor(B::id);
	}
	NVGcolor getSelectedTextColor(NVGcolor cc) override {
		return nvgRGBA(0, 16, 90, 200);
	}

	void pushChangeAction(float oldX, float oldY, float newX, float newY) override {
		auto* h = new XyScreenNodeChangeAction<MODULE>(B::module->getId());
		h->id = B::id;
		h->oldX = oldX; h->oldY = oldY;
		h->newX = newX; h->newY = newY;
		APP->history->push(h);
	}

	void drawExtra(const Widget::DrawArgs& args, NVGcolor cc) override {
		Vec c = Vec(B::box.size.x / 2.f, B::box.size.y / 2.f);

		if (B::module->selection.isNode(B::id)) {
			// Draw circle and fill for radius-property
			Rect b = Rect(B::box.pos.mult(-1), B::parent->box.size);
			nvgSave(args.vg);
			nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
			float sizeX = std::max(0.f, (B::parent->box.size.x - 2 * B::radius) * B::module->getNodeRadiusFinal(B::id) - B::radius);
			float sizeY = std::max(0.f, (B::parent->box.size.y - 2 * B::radius) * B::module->getNodeRadiusFinal(B::id) - B::radius);
			nvgBeginPath(args.vg);
			nvgEllipse(args.vg, c.x, c.y, sizeX, sizeY);
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgStrokeColor(args.vg, color::mult(cc, 0.7f));
			nvgStrokeWidth(args.vg, 0.6f);
			nvgStroke(args.vg);
			nvgFillColor(args.vg, color::mult(cc, 0.1f));
			nvgFill(args.vg);
			nvgResetScissor(args.vg);
			nvgRestore(args.vg);
		}

		// Draw inner circle, marking this widget as a node rather than a cursor
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, B::radius - 2.f);
		nvgStrokeColor(args.vg, cc);
		nvgStrokeWidth(args.vg, 1.0f);
		nvgStroke(args.vg);
		nvgFillColor(args.vg, color::mult(cc, 0.5f));
		nvgFill(args.vg);
	}

	void createContextMenu() override {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(this->getItemName()));
		this->prependContextMenu(menu);
		menu->addChild(new XyScreenAmountSlider<MODULE>(B::module, B::id));
		menu->addChild(new XyScreenRadiusSlider<MODULE>(B::module, B::id));
		this->appendContextMenu(menu);
	}
};


template <typename MODULE>
struct XyScreenCursorDragWidget : XyScreenDragWidgetBase<MODULE> {
	typedef XyScreenDragWidgetBase<MODULE> B;

	float getX() override {
		return B::module->getCursorXFinal(B::id);
	}
	float getY() override {
		return B::module->getCursorYFinal(B::id);
	}
	void setXyFiltered(float x, float y) override {
		B::module->setCursorXyFiltered(B::id, x, y);
	}
	void setXyImmediate(float x, float y) override {
		B::module->setCursorXyImmediate(B::id, x, y);
	}
	bool isActive() override {
		return B::module->isCursorActive(B::id);
	}
	bool isSelected() override {
		return B::module->selection.isCursor(B::id);
	}
	void setSelected() override {
		B::module->selection = { XyScreenSelection::Kind::CURSOR, B::id };
	}
	NVGcolor getColor() override {
		return B::module->getCursorColor(B::id);
	}

	void pushChangeAction(float oldX, float oldY, float newX, float newY) override {
		auto* h = new XyScreenCursorChangeAction<MODULE>(B::module->getId());
		h->id = B::id;
		h->oldX = oldX; h->oldY = oldY;
		h->newX = newX; h->newY = newY;
		APP->history->push(h);
	}

	void drawExtra(const Widget::DrawArgs& args, NVGcolor cc) override {
		// Draw lines between this cursor and every node within its radius
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		Vec c = Vec(B::box.size.x / 2.f, B::box.size.y / 2.f);
		float sizeX = B::parent->box.size.x;
		float sizeY = B::parent->box.size.y;
		for (uint8_t i = 0; i < B::module->nodeCountActive(); i++) {
			if (B::module->getCursorToNodeDistance(B::id, i) < B::module->getNodeRadiusFinal(i)) {
				float x = B::module->getNodeXFinal(i) * (sizeX - 2.f * B::radius);
				float y = B::module->getNodeYFinal(i) * (sizeY - 2.f * B::radius);
				Vec p = B::box.pos.mult(-1).plus(Vec(x, y)).plus(c);
				Vec p_rad = p.minus(c).normalize().mult(B::radius);
				Vec s = c.plus(p_rad);
				Vec t = p.minus(p_rad);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, s.x, s.y);
				nvgLineTo(args.vg, t.x, t.y);
				nvgStrokeColor(args.vg, color::mult(nvgRGB(0x29, 0xb2, 0xef), B::module->getNodeAmountFinal(i)));
				nvgStrokeWidth(args.vg, 1.0f);
				nvgStroke(args.vg);
			}
		}
	}
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
	void createNodeWidgets(MODULE* module, uint8_t count) {
		// This is some over-complicated code for drawing something on the display in the module browser.
		// We "inject" some nodes using a dummy module, satisfying the nodes methods for the display.
		if (!module && !dummyModule) {
			dummyModule = new XyScreenDummyModule;
		}
		if (dummyModule) {
			dummyModule->initNodes(count);
		}

		for (uint8_t i = 0; i < count; i++) {
			if (module) {
				XyScreenDragWidgetBase<MODULE>* w = new WIDGET;
				w->module = module;
				w->id = i;
				addChild(w);
			}
			else {
				XyScreenNodeDragWidget<XyScreenDummyModule>* w = new XyScreenNodeDragWidget<XyScreenDummyModule>;
				w->module = dummyModule;
				w->id = i;
				addChild(w);
			}
		}
	}

	template <typename WIDGET>
	void createCursorWidgets(MODULE* module, uint8_t count) {
		if (!module && !dummyModule) {
			dummyModule = new XyScreenDummyModule;
		}
		if (dummyModule) {
			dummyModule->initCursors(count);
		}

		for (uint8_t i = 0; i < count; i++) {
			if (module) {
				XyScreenDragWidgetBase<MODULE>* w = new WIDGET;
				w->module = module;
				w->id = i;
				addChild(w);
			}
			else {
				XyScreenCursorDragWidget<XyScreenDummyModule>* w = new XyScreenCursorDragWidget<XyScreenDummyModule>;
				w->module = dummyModule;
				w->id = i;
				addChild(w);
			}
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			// Dim the display but don't darken it completely
			float b = std::max(0.2f, settings::rackBrightness);
			float b_inv = 1.f + std::max(b - settings::rackBrightness, 0.f) * 8.f;
			nvgGlobalAlpha(args.vg, b);

			float sizeX = box.size.x / 8.f;
			float sizeY = box.size.y / 8.f;

			math::Rect r = box.zeroPos().grow(Vec(3.f, 3.f));

			// Black background
			nvgBeginPath(args.vg);
			nvgRect(args.vg, RECT_ARGS(r));
			NVGcolor topColor = color::mult(nvgRGB(0x22, 0x22, 0x22), b_inv);
			NVGcolor bottomColor = color::mult(nvgRGB(0x12, 0x12, 0x12), b_inv);
			nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.0, 0.0, 0.0, 25.0, topColor, bottomColor));
			// nvgFillColor(args.vg, bottomColor);
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

			nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);

			// Outer strokes
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, r.pos.x, r.pos.y - 0.5);
			nvgLineTo(args.vg, r.size.x + r.pos.x, r.pos.y - 0.5);
			nvgStrokeColor(args.vg, nvgRGBAf(0, 0, 0, 0.24));
			nvgStrokeWidth(args.vg, 1.0);
			nvgStroke(args.vg);

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, r.pos.x, r.size.y + 2 * r.pos.y + 0.5);
			nvgLineTo(args.vg, r.size.x + r.pos.x, r.size.y + 2 * r.pos.y + 0.5);
			nvgStrokeColor(args.vg, nvgRGBAf(1, 1, 1, 0.25));
			nvgStrokeWidth(args.vg, 1.0);
			nvgStroke(args.vg);

			// Inner strokes
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, r.pos.x, r.pos.y + 2.5);
			nvgLineTo(args.vg, r.size.x + r.pos.x, r.pos.y + 2.5);
			nvgStrokeColor(args.vg, nvgRGBAf(1, 1, 1, 0.20));
			nvgStrokeWidth(args.vg, 1.0);
			nvgStroke(args.vg);

			// Black border
			math::Rect rBorder = r.shrink(math::Vec(1, 1));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, RECT_ARGS(rBorder));
			nvgStrokeColor(args.vg, bottomColor);
			nvgStrokeWidth(args.vg, 2.0);
			nvgStroke(args.vg);
		}

		if (!module || module->seqEdit < 0) {
			OpaqueWidget::drawLayer(args, layer);
		}
	}

	void onButton(const event::Button& e) override {
		if (module->seqEdit < 0) {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				module->selection = XyScreenSelection();
			}
			OpaqueWidget::onButton(e);
			if (e.button == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && !e.isConsumed()) {
				createContextMenu();
				e.consume(this);
			}
		}
	}

	// Captures node x/y before a randomize-all call, runs it, then wraps the
	// per-node before/after deltas into one undoable ComplexAction. Shared by
	// the three position-randomize menu items below (x, y, or both) — they
	// differ only in which randomizeAll functions run.
	void randomizeXy(const std::string& actionName, bool doX, bool doY) {
		std::vector<XyScreenNodeChangeAction<MODULE>*> actions(module->nodeCount());
		for (uint8_t i = 0; i < module->nodeCount(); i++) {
			actions[i] = new XyScreenNodeChangeAction<MODULE>(module->id);
			actions[i]->id = i;
			actions[i]->oldX = module->getNodeXFinal(i);
			actions[i]->oldY = module->getNodeYFinal(i);
		}

		if (doX) module->nodes.randomizeXAll();
		if (doY) module->nodes.randomizeYAll();

		history::ComplexAction* complexAction = new history::ComplexAction;
		for (uint8_t i = 0; i < module->nodeCount(); i++) {
			actions[i]->newX = module->getNodeXFinal(i);
			actions[i]->newY = module->getNodeYFinal(i);
			complexAction->push(actions[i]);
		}
		complexAction->name = module->model->plugin->brand + " " + module->model->name + " " + actionName;
		APP->history->push(complexAction);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(module->model->name));
		prependContextMenu(menu);

		menu->addChild(createMenuItem("Initialize", "", [=] {
			history::ModuleChange* h = new history::ModuleChange;
			h->name = module->model->plugin->brand + " " + module->model->name + " initialize";
			h->moduleId = module->id;
			h->oldModuleJ = module->toJson();

			module->initNodes();

			h->newModuleJ = module->toJson();
			APP->history->push(h);
		}));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Radomize x-pos & y-pos", "", [=] {
			randomizeXy("randomize x-pos & y-pos", true, true);
		}));
		menu->addChild(createMenuItem("Radomize x-pos", "", [=] {
			randomizeXy("randomize x-pos", true, false);
		}));
		menu->addChild(createMenuItem("Radomize y-pos", "", [=] {
			randomizeXy("randomize IN y-pos", false, true);
		}));
		menu->addChild(createMenuItem("Radomize amount", "", [=] {
			std::vector<XyScreenAmountChangeAction<MODULE>*> actions(module->nodeCount());
			for (uint8_t i = 0; i < module->nodeCount(); i++) {
				actions[i] = new XyScreenAmountChangeAction<MODULE>(module);
				actions[i]->id = i;
				actions[i]->oldValue = module->getNodeAmountFinal(i);
			}

			module->nodes.randomizeAmountAll();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->nodeCount(); i++) {
				actions[i]->newValue = module->getNodeAmountFinal(i);
				complexAction->push(actions[i]);
			}
			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize amount";
			APP->history->push(complexAction);
		}));
		menu->addChild(createMenuItem("Radomize radius", "", [=] {
			std::vector<XyScreenRadiusChangeAction<MODULE>*> actions(module->nodeCount());
			for (uint8_t i = 0; i < module->nodeCount(); i++) {
				actions[i] = new XyScreenRadiusChangeAction<MODULE>(module);
				actions[i]->id = i;
				actions[i]->oldValue = module->getNodeRadiusFinal(i);
			}

			module->nodes.randomizeRadiusAll();

			history::ComplexAction* complexAction = new history::ComplexAction;
			for (uint8_t i = 0; i < module->nodeCount(); i++) {
				actions[i]->newValue = module->getNodeRadiusFinal(i);
				complexAction->push(actions[i]);
			}
			complexAction->name = module->model->plugin->brand + " " + module->model->name + " randomize radius";
			APP->history->push(complexAction);
		}));

		appendContextMenu(menu);
	}

	virtual void prependContextMenu(Menu* menu) {}
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