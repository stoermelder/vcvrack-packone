#include "plugin.hpp"
#include <thread>

namespace Arena {

static const int SEQ_COUNT = 16;
static const int SEQ_LENGTH = 128;

enum OPMODE {
	RADIUS = 0,
	AMOUNT = 1,
	OFFSET_X = 2,
	OFFSET_Y = 3,
//	ROTATE = 6,
	WALK = 7
};

enum SEQMODE {
	TRIG_FWD = 0,
	TRIG_REV = 1,
	TRIG_RANDOM = 2,
	VOLT = 4,
	C4 = 5,
};


template < int IN_PORTS, int MIX_PORTS >
struct ArenaModule : Module {
	enum ParamIds {
		ENUMS(IN_X_POS, IN_PORTS),
		ENUMS(IN_Y_POS, IN_PORTS),
		ENUMS(IN_X_PARAM, IN_PORTS),
		ENUMS(IN_Y_PARAM, IN_PORTS),
		ENUMS(IN_X_CTRL, IN_PORTS),
		ENUMS(IN_Y_CTRL, IN_PORTS),
		ENUMS(OP_PARAM, IN_PORTS),
		ENUMS(IN_PLUS_PARAM, IN_PORTS),
		ENUMS(IN_MINUS_PARAM, IN_PORTS),
		ENUMS(MIX_X_POS, MIX_PORTS),
		ENUMS(MIX_Y_POS, MIX_PORTS),
		ENUMS(MIX_X_PARAM, MIX_PORTS),
		ENUMS(MIX_Y_PARAM, MIX_PORTS),
		ENUMS(MIX_SEL_PARAM, MIX_PORTS),
		SEQ_MINUS_PARAM,
		SEQ_PLUS_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(IN, IN_PORTS),
		ENUMS(IN_X_INPUT, IN_PORTS),
		ENUMS(IN_Y_INPUT, IN_PORTS),
		ENUMS(OP_INPUT, IN_PORTS),
		ENUMS(MIX_X_INPUT, MIX_PORTS),
		ENUMS(MIX_Y_INPUT, MIX_PORTS),
		ENUMS(SEQ_INPUT, MIX_PORTS),
		ENUMS(SEQ_PH_INPUT, MIX_PORTS),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(MIX_OUTPUT, MIX_PORTS),
		ENUMS(OUT, IN_PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(IN_SEL_LIGHT, IN_PORTS),
		ENUMS(MIX_SEL_LIGHT, MIX_PORTS),
		NUM_LIGHTS
	};

	const int num_inports = IN_PORTS;
	const int num_outputs = MIX_PORTS;
	int selectedId = -1;
	int selectedType = -1;

	/** [Stored to JSON] */
	float radius[IN_PORTS];
	/** [Stored to JSON] */
	float amount[IN_PORTS];
	/** [Stored to JSON] */
	OPMODE opMode[IN_PORTS];
	/** [Stored to JSON] */
	bool opBipolar[IN_PORTS];
	/** [Stored to JSON] */
	bool inputXBipolar[IN_PORTS];
	/** [Stored to JSON] */
	bool inputYBipolar[IN_PORTS];

	struct SeqItem {
		float x[SEQ_LENGTH];
		float y[SEQ_LENGTH];
	};

	/** [Stored to JSON] */
	SeqItem seqData[MIX_PORTS][SEQ_COUNT];
	/** [Stored to JSON] */
	SEQMODE seqMode[MIX_PORTS];
	/** [Stored to JSON] */
	int seqSelected[MIX_PORTS];
	int seqRec;

	float dist[MIX_PORTS][IN_PORTS];
	float offsetX[IN_PORTS];
	float offsetY[IN_PORTS];

	dsp::SchmittTrigger seqTrigger[MIX_PORTS];
	dsp::ClockDivider lightDivider;

	ArenaModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// inputs
		for (int i = 0; i < IN_PORTS; i++) {
			configParam(IN_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (IN_PORTS - 1)), string::f("Ch %i x-pos", i + 1));
			configParam(IN_Y_POS + i, 0.0f, 1.0f, 0.1f, string::f("Ch %i y-pos", i + 1));
			configParam(IN_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Ch %i x-pos attenuverter", i + 1), "x");
			configParam(IN_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Ch %i y-pos attenuverter", i + 1), "x");

			configParam(OP_PARAM + i, -1.f, 1.f, 0.f, string::f("Ch %i Op attenuverter", i + 1), "x");
		}
		// outputs
		for (int i = 0; i < MIX_PORTS; i++) {
			configParam(MIX_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (MIX_PORTS - 1)), string::f("Mix%i x-pos", i + 1));
			configParam(MIX_Y_POS + i, 0.0f, 1.0f, 0.9f, string::f("Mix%i y-pos", i + 1));
			configParam(MIX_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Mix%i x-pos attenuverter", i + 1), "x");
			configParam(MIX_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Mix%i y-pos attenuverter", i + 1), "x");
		}
		configParam(SEQ_MINUS_PARAM, 0.f, 1.f, 0.f, "Previous sequence");
		configParam(SEQ_PLUS_PARAM, 0.f, 1.f, 0.f, "Next sequence");
		onReset();
		lightDivider.setDivision(512);
	}

	void onReset() override {
		resetSelection();
		for (int i = 0; i < IN_PORTS; i++) {
			radius[i] = 0.5f;
			amount[i] = 1.f;
			opMode[i] = OPMODE::RADIUS;
			opBipolar[i] = false;
			inputXBipolar[i] = false;
			inputYBipolar[i] = false;
			paramQuantities[IN_X_POS + i]->setValue(paramQuantities[IN_X_POS + i]->getDefaultValue());
			paramQuantities[IN_Y_POS + i]->setValue(paramQuantities[IN_Y_POS + i]->getDefaultValue());
		}
		for (int i = 0; i < MIX_PORTS; i++) {
			seqSelected[i] = 0;
			seqMode[i] = SEQMODE::TRIG_FWD;
			paramQuantities[MIX_X_POS + i]->setValue(paramQuantities[MIX_X_POS + i]->getDefaultValue());
			paramQuantities[MIX_Y_POS + i]->setValue(paramQuantities[MIX_Y_POS + i]->getDefaultValue());
		}
		seqRec = -1;
		Module::onReset();
	}

	void process(const ProcessArgs &args) override {
		for (int j = 0; j < IN_PORTS; j++) {
			offsetX[j] = 0.f;
			offsetY[j] = 0.f;
			switch (opMode[j]) {
				case OPMODE::RADIUS: {
					if (inputs[OP_INPUT + j].isConnected()) {
						radius[j] = getOpInput(j);
					}
					break;
				}
				case OPMODE::AMOUNT: {
					if (inputs[OP_INPUT + j].isConnected()) {
						amount[j] = getOpInput(j);
					}
					break;
				}
				case OPMODE::OFFSET_X: {
					offsetX[j] = getOpInput(j);
					break;
				}
				case OPMODE::OFFSET_Y: {
					offsetY[j] = getOpInput(j);
					break;
				}
				case OPMODE::WALK: {
					float v = getOpInput(j);
					offsetX[j] = random::normal() / 2000.f * v;
					offsetY[j] = random::normal() / 2000.f * v;
					break;
				}
			}

			float x = params[IN_X_POS + j].getValue();
			if (inputs[IN_X_INPUT + j].isConnected()) {
				float xd = inputs[IN_X_INPUT + j].getVoltage();
				xd += inputXBipolar[j] ? 5.f : 0.f;
				x = clamp(xd / 10.f, 0.f, 1.f);
				x *= params[IN_X_PARAM + j].getValue();
			}
			x += offsetX[j];
			x = clamp(x, 0.f, 1.f);
			params[IN_X_POS + j].setValue(x);

			float y = params[IN_Y_POS + j].getValue();
			if (inputs[IN_Y_INPUT + j].isConnected()) {
				float yd = inputs[IN_Y_INPUT + j].getVoltage();
				yd += inputYBipolar[j] ? 5.f : 0.f;
				y = clamp(yd / 10.f, 0.f, 1.f);
				y *= params[IN_Y_PARAM + j].getValue();
			}
			y += offsetY[j];
			y = clamp(y, 0.f, 1.f);
			params[IN_Y_POS + j].setValue(y);
		}

		float out[IN_PORTS];
		for (int i = 0; i < MIX_PORTS; i++) {
			if (inputs[MIX_X_INPUT + i].isConnected()) {
				float x = inputs[MIX_X_INPUT + i].getVoltage() / 10.f;
				x *= params[MIX_X_PARAM + i].getValue();
				x = clamp(x, 0.f, 1.f);
				params[MIX_X_POS + i].setValue(x);
			} 
			else {

			}

			if (inputs[MIX_Y_INPUT + i].isConnected()) {
				float y = inputs[MIX_Y_INPUT + i].getVoltage() / 10.f;
				y *= params[MIX_Y_PARAM + i].getValue();
				y = clamp(y, 0.f, 1.f);
				params[MIX_Y_POS + i].setValue(y);
			}
			else {

			}

			float x = params[MIX_X_POS + i].getValue();
			float y = params[MIX_Y_POS + i].getValue();
			Vec p = Vec(x, y);

			int c = 0;
			float mix = 0.f;
			for (int j = 0; j < IN_PORTS; j++) {
				float in_x = params[IN_X_POS + j].getValue();
				float in_y = params[IN_Y_POS + j].getValue();
				Vec in_p = Vec(in_x, in_y);
				dist[i][j] = in_p.minus(p).norm();

				float r = radius[j];
				if (inputs[IN + j].isConnected() && dist[i][j] < r) {
					float sd = inputs[IN + j].getVoltage();
					sd = clamp(sd, -10.f, 10.f);
					sd *= amount[j];
					float s = std::min(1.0f, (r - dist[i][j]) / r * 1.1f);
					s *= sd;
					mix += s;
					out[j] += s;
					c++;
				}
			}

			if (c > 0) mix /= c;
			outputs[MIX_OUTPUT + i].setVoltage(mix);

			for (int j = 0; j < IN_PORTS; j++) {
				float v = clamp(out[j], -10.f, 10.f);
				outputs[OUT + j].setVoltage(v);
			}
		}

		// Set lights infrequently
		if (lightDivider.process()) {
			for (int i = 0; i < IN_PORTS; i++) {
				lights[IN_SEL_LIGHT + i].setBrightness(selectedType == 0 && selectedId == i);
			}
			for (int i = 0; i < MIX_PORTS; i++) {
				lights[MIX_SEL_LIGHT + i].setBrightness(selectedType == 1 && selectedId == i);
			}
		}
	}

	inline float getOpInput(int j) {
		float v = inputs[OP_INPUT + j].isConnected() ? inputs[OP_INPUT + j].getVoltage() : 10.f;
		v += opBipolar[j] ? 5.f : 0.f;
		v = clamp(v / 10.f, 0.f, 1.f);
		v *= params[OP_PARAM + j].getValue();
		return v;
	}

	inline void setSelection(int type, int id) {
		selectedType = type;
		selectedId = id;
	}

	inline bool isSelected(int type, int id) {
		return selectedType == type && selectedId == id;
	}

	inline void resetSelection() {
		selectedType = -1;
		selectedId = -1;
	}

	void randomizeInputAmount() {
		for (int i = 0; i < IN_PORTS; i++) {
			amount[i] = random::uniform();
		}
	}

	void randomizeInputRadius() {
		for (int i = 0; i < IN_PORTS; i++) {
			radius[i] = random::uniform();
		}
	}

	void randomizeInputX() {
		for (int i = 0; i < IN_PORTS; i++) {
			params[IN_X_POS + i].setValue(random::uniform());
		}
	}

	void randomizeInputY() {
		for (int i = 0; i < IN_PORTS; i++) {
			params[IN_Y_POS + i].setValue(random::uniform());
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_t* inputsJ = json_array();
		for (int i = 0; i < IN_PORTS; i++) {
			json_t* inputJ = json_object();
			json_object_set_new(inputJ, "amount", json_real(amount[i]));
			json_object_set_new(inputJ, "radius", json_real(radius[i]));
			json_object_set_new(inputJ, "opMode", json_integer(opMode[i]));
			json_object_set_new(inputJ, "opBipolar", json_boolean(opBipolar[i]));
			json_object_set_new(inputJ, "inputXBipolar", json_boolean(inputXBipolar[i]));
			json_object_set_new(inputJ, "inputYBipolar", json_boolean(inputYBipolar[i]));
			json_array_append_new(inputsJ, inputJ);
		}
		json_object_set_new(rootJ, "inputs", inputsJ);

		json_t* mixputsJ = json_array();
		for (int i = 0; i < MIX_PORTS; i++) {
			json_t* mixputJ = json_object();
			json_object_set_new(mixputJ, "seqSelected", json_integer(seqSelected[i]));
			json_object_set_new(mixputJ, "seqMode", json_integer(seqMode[i]));
			json_array_append_new(mixputsJ, mixputJ);
		}
		json_object_set_new(rootJ, "mixputs", mixputsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* inputsJ = json_object_get(rootJ, "inputs");
		json_t* inputJ;
		size_t inputIndex;
		json_array_foreach(inputsJ, inputIndex, inputJ) {
			amount[inputIndex] = json_real_value(json_object_get(inputJ, "amount"));
			radius[inputIndex] = json_real_value(json_object_get(inputJ, "radius"));
			opMode[inputIndex] = (OPMODE)json_integer_value(json_object_get(inputJ, "opMode"));
			opBipolar[inputIndex] = json_boolean_value(json_object_get(inputJ, "opBipolar"));
			inputXBipolar[inputIndex] = json_boolean_value(json_object_get(inputJ, "inputXBipolar"));
			inputYBipolar[inputIndex] = json_boolean_value(json_object_get(inputJ, "inputYBipolar"));
		}

		json_t* mixputsJ = json_object_get(rootJ, "mixputs");
		json_t* mixputJ;
		size_t mixputIndex;
		json_array_foreach(mixputsJ, mixputIndex, mixputJ) {
			seqSelected[mixputIndex] = json_integer_value(json_object_get(mixputJ, "seqSelected"));
			seqMode[mixputIndex] = (SEQMODE)json_integer_value(json_object_get(mixputJ, "seqMode"));
		}
	}
};


// context menus

template < typename MODULE >
struct InputXMenuItem : MenuItem {
	InputXMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct InputXBipolarItem : MenuItem {
		MODULE* module;
		int id;

		void onAction(const event::Action &e) override {
			module->inputXBipolar[id] ^= true;
		}

		void step() override {
			rightText = module->inputXBipolar[id] ? "-5V..5V" : "0V..10V";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<InputXBipolarItem>(&MenuItem::text, "Voltage", &InputXBipolarItem::module, module, &InputXBipolarItem::id, id));
		return menu;
	}
};


template < typename MODULE >
struct InputYMenuItem : MenuItem {
	InputYMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct InputYBipolarItem : MenuItem {
		MODULE* module;
		int id;

		void onAction(const event::Action &e) override {
			module->inputYBipolar[id] ^= true;
		}

		void step() override {
			rightText = module->inputYBipolar[id] ? "-5V..5V" : "0V..10V";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<InputYBipolarItem>(&MenuItem::text, "Voltage", &InputYBipolarItem::module, module, &InputYBipolarItem::id, id));
		return menu;
	}
};


template < typename MODULE >
struct OpModeMenuItem : MenuItem {
	OpModeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct OpModeItem : MenuItem {
		MODULE* module;
		OPMODE opMode;
		int id;
		
		void onAction(const event::Action &e) override {
			module->opMode[id] = opMode;
		}

		void step() override {
			rightText = module->opMode[id] == opMode ? "✔" : "";
			MenuItem::step();
		}
	};

	struct OpBipolarItem : MenuItem {
		MODULE* module;
		int id;

		void onAction(const event::Action &e) override {
			module->opBipolar[id] ^= true;
		}

		void step() override {
			rightText = module->opBipolar[id] ? "-5V..5V" : "0V..10V";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Mode"));
		menu->addChild(construct<OpModeItem>(&MenuItem::text, "Radius", &OpModeItem::module, module, &OpModeItem::id, id, &OpModeItem::opMode, OPMODE::RADIUS));
		menu->addChild(construct<OpModeItem>(&MenuItem::text, "Amount", &OpModeItem::module, module, &OpModeItem::id, id, &OpModeItem::opMode, OPMODE::AMOUNT));
		menu->addChild(construct<OpModeItem>(&MenuItem::text, "Offset x-pos", &OpModeItem::module, module, &OpModeItem::id, id, &OpModeItem::opMode, OPMODE::OFFSET_X));
		menu->addChild(construct<OpModeItem>(&MenuItem::text, "Offset y-pos", &OpModeItem::module, module, &OpModeItem::id, id, &OpModeItem::opMode, OPMODE::OFFSET_Y));
		menu->addChild(construct<OpModeItem>(&MenuItem::text, "Random walk", &OpModeItem::module, module, &OpModeItem::id, id, &OpModeItem::opMode, OPMODE::WALK));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<OpBipolarItem>(&MenuItem::text, "Voltage", &OpBipolarItem::module, module, &OpBipolarItem::id, id));
		return menu;
	}
};


template < typename MODULE >
struct SeqMenuItem : MenuItem {
	SeqMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct SeqItem : MenuItem {
		MODULE* module;
		int id;
		int seq;
		
		void onAction(const event::Action &e) override {
			if (module->seqRec != id)
				module->seqSelected[id] = seq;
		}

		void step() override {
			rightText = module->seqSelected[id] == seq ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		for (int i = 0; i < SEQ_COUNT; i++) {
			menu->addChild(construct<SeqItem>(&MenuItem::text, string::f("%02u", i + 1), &SeqItem::module, module, &SeqItem::id, id, &SeqItem::seq, i));
		}
		return menu;
	}
};


template < typename MODULE >
struct SeqModeMenuItem : MenuItem {
	SeqModeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct SeqModeItem : MenuItem {
		MODULE* module;
		int id;
		SEQMODE seqMode;
		
		void onAction(const event::Action &e) override {
			if (module->seqRec != id)
				module->seqMode[id] = seqMode;
		}

		void step() override {
			rightText = module->seqMode[id] == seqMode ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<SeqModeItem>(&MenuItem::text, "Trigger forward", &SeqModeItem::module, module, &SeqModeItem::id, id, &SeqModeItem::seqMode, SEQMODE::TRIG_FWD));
		menu->addChild(construct<SeqModeItem>(&MenuItem::text, "Trigger reverse", &SeqModeItem::module, module, &SeqModeItem::id, id, &SeqModeItem::seqMode, SEQMODE::TRIG_REV));
		menu->addChild(construct<SeqModeItem>(&MenuItem::text, "Trigger random", &SeqModeItem::module, module, &SeqModeItem::id, id, &SeqModeItem::seqMode, SEQMODE::TRIG_RANDOM));
		menu->addChild(construct<SeqModeItem>(&MenuItem::text, "0..10V", &SeqModeItem::module, module, &SeqModeItem::id, id, &SeqModeItem::seqMode, SEQMODE::VOLT));
		menu->addChild(construct<SeqModeItem>(&MenuItem::text, "C4-F5", &SeqModeItem::module, module, &SeqModeItem::id, id, &SeqModeItem::seqMode, SEQMODE::C4));
		return menu;
	}
};


template < typename MODULE >
struct RadiusSlider : ui::Slider {
	struct RadiusQuantity : Quantity {
		MODULE* module;
		int id;

		RadiusQuantity(MODULE* module, int id) {
			this->module = module;
			this->id = id;
		}
		void setValue(float value) override {
			module->radius[id] = math::clamp(value, 0.f, 1.f);
		}
		float getValue() override {
			return module->radius[id];
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
			return "Radius";
		}
		std::string getUnit() override {
			return "";
		}
	};

	RadiusSlider(MODULE* module, int id) {
		quantity = new RadiusQuantity(module, id);
	}
	~RadiusSlider() {
		delete quantity;
	}
};


template < typename MODULE >
struct AmountSlider : ui::Slider {
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

	AmountSlider(MODULE* module, int id) {
		quantity = new AmountQuantity(module, id);
	}
	~AmountSlider() {
		delete quantity;
	}
};


// widgets

template < typename MODULE >
struct ArenaDragableWidget : OpaqueWidget {
	const float radius = 10.f;
	const float fontsize = 13.0f;

	MODULE* module;
	std::shared_ptr<Font> font;
	ParamQuantity* paramQuantityX;
	ParamQuantity* paramQuantityY;
	NVGcolor color = nvgRGB(0x66, 0x66, 0x0);
	int id = -1;
	int type = -1;
	
	float circleA = 1.f;
	math::Vec dragPos;

	ArenaDragableWidget() {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		box.size = Vec(2 * radius, 2 * radius);
	}

	void step() override {
		float posX = paramQuantityX->getValue() * (parent->box.size.x - box.size.x);
		box.pos.x = posX;
		float posY = paramQuantityY->getValue() * (parent->box.size.y - box.size.y);
		box.pos.y = posY;
	}

	void draw(const Widget::DrawArgs& args) override {
		Widget::draw(args);
		if (!module) return;

		Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);

		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

		if (module->isSelected(type, id)) {
			// selection halo
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

		// circle
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, radius - 2.f);
		nvgStrokeColor(args.vg, color);
		nvgStrokeWidth(args.vg, 1.0f);
		nvgStroke(args.vg);
		nvgFillColor(args.vg, color::mult(color, 0.5f));
		nvgFill(args.vg);

		// amount circle
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, radius);
		nvgStrokeColor(args.vg, color::mult(color, circleA));
		nvgStrokeWidth(args.vg, 0.8f);
		nvgStroke(args.vg);

		// label
		nvgFontSize(args.vg, fontsize);
		nvgFontFaceId(args.vg, font->handle);
		nvgFillColor(args.vg, color);
		nvgTextBox(args.vg, c.x - 3.f, c.y + 4.f, 120, string::f("%i", id + 1).c_str(), NULL);
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
				module->setSelection(type, id);
				e.consume(this);
			}
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				module->setSelection(type, id);
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

		dragPos = APP->scene->rack->mousePos.minus(box.pos);
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
	}


	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		math::Vec pos = APP->scene->rack->mousePos.minus(dragPos);
		float x = pos.x / (parent->box.size.x - box.size.x);
		paramQuantityX->setValue(std::max(0.f, std::min(1.f, x)));
		float y = pos.y / (parent->box.size.y - box.size.y);
		paramQuantityY->setValue(std::max(0.f, std::min(1.f, y)));

		OpaqueWidget::onDragMove(e);
	}

	virtual void createContextMenu() {}
};


template < typename MODULE >
struct ArenaInputWidget : ArenaDragableWidget<MODULE> {
	typedef ArenaDragableWidget<MODULE> AW;

	ArenaInputWidget() {
		AW::color = color::WHITE;
		AW::type = 0;
	}

	void step() override {
		AW::circleA = AW::module->amount[AW::id];
		AW::step();
	}

	void draw(const Widget::DrawArgs& args) override {
		if (AW::module->isSelected(AW::type, AW::id)) {
			// outer circle and fill
			Vec c = Vec(AW::box.size.x / 2.f, AW::box.size.y / 2.f);
			Rect b = Rect(AW::box.pos.mult(-1), AW::parent->box.size);
			nvgSave(args.vg);
			nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
			float sizeX = std::max(0.f, (AW::parent->box.size.x - 2 * AW::radius) * AW::module->radius[AW::id] - AW::radius);
			float sizeY = std::max(0.f, (AW::parent->box.size.y - 2 * AW::radius) * AW::module->radius[AW::id] - AW::radius);
			nvgBeginPath(args.vg);
			nvgEllipse(args.vg, c.x, c.y, sizeX, sizeY);
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgStrokeColor(args.vg, color::mult(AW::color, 0.8f));
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStroke(args.vg);
			nvgFillColor(args.vg, color::mult(AW::color, 0.1f));
			nvgFill(args.vg);
			nvgResetScissor(args.vg);
			nvgRestore(args.vg);
		}

		AW::draw(args);
	}

	void createContextMenu() override {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Input %i", AW::id + 1).c_str()));

		AmountSlider<MODULE>* amountSlider = new AmountSlider<MODULE>(AW::module, AW::id);
		amountSlider->box.size.x = 200.0;
		menu->addChild(amountSlider);

		RadiusSlider<MODULE>* radiusSlider = new RadiusSlider<MODULE>(AW::module, AW::id);
		radiusSlider->box.size.x = 200.0;
		menu->addChild(radiusSlider);

		menu->addChild(construct<InputXMenuItem<MODULE>>(&MenuItem::text, "X-port", &InputXMenuItem<MODULE>::module, AW::module, &InputXMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<InputYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &InputYMenuItem<MODULE>::module, AW::module, &InputYMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<OpModeMenuItem<MODULE>>(&MenuItem::text, "OP-port", &OpModeMenuItem<MODULE>::module, AW::module, &OpModeMenuItem<MODULE>::id, AW::id));
	}
};

template < typename MODULE >
struct ArenaMixWidget : ArenaDragableWidget<MODULE> {
	typedef ArenaDragableWidget<MODULE> AW;

	ArenaMixWidget() {
		AW::color = color::YELLOW;
		AW::type = 1;
	}

	void draw(const Widget::DrawArgs& args) override {
		AW::draw(args);

		Vec c = Vec(AW::box.size.x / 2.f, AW::box.size.y / 2.f);
		float sizeX = AW::parent->box.size.x;
		float sizeY = AW::parent->box.size.y;
		for (int i = 0; i < AW::module->num_inports; i++) {
			if (AW::module->dist[AW::id][i] < AW::module->radius[i]) {
				float x = AW::module->params[MODULE::IN_X_POS + i].getValue() * (sizeX - 2.f * AW::radius);
				float y = AW::module->params[MODULE::IN_Y_POS + i].getValue() * (sizeY - 2.f * AW::radius);
				Vec p = AW::box.pos.mult(-1).plus(Vec(x, y)).plus(c);
				Vec p_rad = p.minus(c).normalize().mult(AW::radius);
				Vec s = c.plus(p_rad);
				Vec t = p.minus(p_rad);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, s.x, s.y);
				nvgLineTo(args.vg, t.x, t.y);
				nvgStrokeColor(args.vg, color::mult(nvgRGB(0x29, 0xb2, 0xef), AW::module->amount[i]));
				nvgStrokeWidth(args.vg, 1.0f);
				nvgStroke(args.vg);
			}
		}
	}
};


template < typename MODULE, int IN_PORTS, int MIX_PORTS >
struct ArenaAreaWidget : OpaqueWidget {
	MODULE* module;
	ArenaInputWidget<MODULE>* inwidget[IN_PORTS];
	ArenaMixWidget<MODULE>* outwidget[MIX_PORTS];

	ArenaAreaWidget(MODULE* module, int inParamIdX, int inParamIdY, int outParamIdX, int outParamIdY) {
		this->module = module;
		if (module) {
			for (int i = 0; i < IN_PORTS; i++) {
				inwidget[i] = new ArenaInputWidget<MODULE>;
				inwidget[i]->module = module;
				inwidget[i]->paramQuantityX = module->paramQuantities[inParamIdX + i];
				inwidget[i]->paramQuantityY = module->paramQuantities[inParamIdY + i];
				inwidget[i]->id = i;
				addChild(inwidget[i]);
			}
			for (int i = 0; i < MIX_PORTS; i++) {
				outwidget[i] = new ArenaMixWidget<MODULE>;
				outwidget[i]->module = module;
				outwidget[i]->paramQuantityX = module->paramQuantities[outParamIdX + i];
				outwidget[i]->paramQuantityY = module->paramQuantities[outParamIdY + i];
				outwidget[i]->id = i;
				addChild(outwidget[i]);
			}
		}
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			module->resetSelection();
		}
		OpaqueWidget::onButton(e);
		if (e.button == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && !e.isConsumed()) {
			createContextMenu();
			e.consume(this);
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Arena"));

		struct RandomizeXYItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action &e) override {
				module->randomizeInputX();
				module->randomizeInputY();
			}
		};

		struct RandomizeXItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action &e) override {
				module->randomizeInputX();
			}
		};

		struct RandomizeYItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action &e) override {
				module->randomizeInputY();
			}
		};

		struct RandomizeAmountItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action &e) override {
				module->randomizeInputAmount();
			}
		};

		struct RandomizeRadiusItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action &e) override {
				module->randomizeInputRadius();
			}
		};

		menu->addChild(construct<RandomizeXYItem>(&MenuItem::text, "Radomize input x-pos & y-pos", &RandomizeXYItem::module, module));
		menu->addChild(construct<RandomizeXItem>(&MenuItem::text, "Radomize input x-pos", &RandomizeXItem::module, module));
		menu->addChild(construct<RandomizeYItem>(&MenuItem::text, "Radomize input y-pos", &RandomizeYItem::module, module));
		menu->addChild(construct<RandomizeAmountItem>(&MenuItem::text, "Radomize input amount", &RandomizeAmountItem::module, module));
		menu->addChild(construct<RandomizeRadiusItem>(&MenuItem::text, "Radomize input radius", &RandomizeRadiusItem::module, module));
	}
};


template < typename MODULE >
struct ArenaOpDisplay : LedDisplayChoice {
	MODULE* module;
	int id;

	ArenaOpDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		box.size = Vec(25.1f, 16.f);
		textOffset = Vec(4.f, 11.5f);
	}

	void step() override {
		if (module) {
			switch (module->opMode[id]) {
				case OPMODE::RADIUS:
					text = "RAD"; break;
				case OPMODE::AMOUNT:
					text = "AMT"; break;
				case OPMODE::OFFSET_X:
					text = "O-X"; break;
				case OPMODE::OFFSET_Y:
					text = "O-Y"; break;
				case OPMODE::WALK:
					text = "WLK"; break;
			}
		}
		LedDisplayChoice::step();
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		LedDisplayChoice::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Input %i", id + 1)));

		AmountSlider<MODULE>* amountSlider = new AmountSlider<MODULE>(module, id);
		amountSlider->box.size.x = 200.0;
		menu->addChild(amountSlider);

		RadiusSlider<MODULE>* radiusSlider = new RadiusSlider<MODULE>(module, id);
		radiusSlider->box.size.x = 200.0;
		menu->addChild(radiusSlider);

		menu->addChild(construct<InputXMenuItem<MODULE>>(&MenuItem::text, "X-port", &InputXMenuItem<MODULE>::module, module, &InputXMenuItem<MODULE>::id, id));
		menu->addChild(construct<InputYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &InputYMenuItem<MODULE>::module, module, &InputYMenuItem<MODULE>::id, id));
		menu->addChild(construct<OpModeMenuItem<MODULE>>(&MenuItem::text, "OP-port", &OpModeMenuItem<MODULE>::module, module, &OpModeMenuItem<MODULE>::id, id));
	}
};


template < typename MODULE >
struct ArenaSeqDisplay : LedDisplayChoice {
	MODULE* module;
	int id;

	ArenaSeqDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		box.size = Vec(16.9f, 16.f);
		textOffset = Vec(3.f, 11.5f);
	}

	void step() override {
		if (module) {
			text = string::f("%02d", module->seqSelected[id] + 1);
		}
		LedDisplayChoice::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (module->seqRec == id) {
				module->seqRec = -1;
				color = nvgRGB(0xf0, 0xf0, 0xf0);
			}
			else {
				module->seqRec = id;
				color = color::RED;
			}
			e.consume(this);
		}
		LedDisplayChoice::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		LedDisplayChoice::draw(args);
		if (module && module->seqRec == id) {
			drawHalo(args);
		}
	}

	void drawHalo(const DrawArgs &args) {
		float radiusX = box.size.x / 2.0;
		float radiusY = box.size.x / 2.0;
		float oradiusX = 2 * radiusX;
		float oradiusY = 2 * radiusY;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, radiusX - oradiusX, radiusY - oradiusY, 2 * oradiusX, 2 * oradiusY);

		NVGpaint paint;
		NVGcolor icol = color::mult(color, 0.65f);
		NVGcolor ocol = nvgRGB(0, 0, 0);

		paint = nvgRadialGradient(args.vg, radiusX, radiusY, radiusX, oradiusY, icol, ocol);
		nvgFillPaint(args.vg, paint);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFill(args.vg);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Mix%i", id + 1)));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SeqMenuItem<MODULE>>(&MenuItem::text, "Sequence", &SeqMenuItem<MODULE>::module, module, &SeqMenuItem<MODULE>::id, id));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SeqModeMenuItem<MODULE>>(&MenuItem::text, "SEQ-port", &SeqModeMenuItem<MODULE>::module, module, &SeqModeMenuItem<MODULE>::id, id));
	}
};


struct DummyMapButton : ParamWidget {
	DummyMapButton() {
		this->box.size = Vec(5.f, 5.f);
	}

	void draw(const Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, 2.5f, 2.5f, 2.0f);
		nvgStrokeColor(args.vg, nvgRGB(0x90, 0x90, 0x90));
		nvgStrokeWidth(args.vg, 1.0f);
		nvgStroke(args.vg);
		ParamWidget::draw(args);
	}
};

template < typename LIGHT >
struct ClickableSmallLight : SmallLight<LIGHT> {
	int id;
	int type;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			ArenaModule<8, 2>* m = dynamic_cast<ArenaModule<8, 2>*>(SmallLight<LIGHT>::module);
			if (m->isSelected(type, id))
				m->resetSelection();
			else
				m->setSelection(type, id);
		}
		SmallLight<LIGHT>::onButton(e);
	}
};


struct ArenaWidget : ModuleWidget {
	typedef ArenaModule<8, 2> MODULE;
	MODULE* module;

	ArenaWidget(MODULE* module) {
		setModule(module);
		this->module = module;
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Arena.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < 8; i++) {
			float xs[] = { 25.9f, 497.6f };
			float x = xs[i >= 4] + (i % 4) * 30.433f;
			int s = i >= 4 ? -1 : 1;
			addInput(createInputCentered<StoermelderPort>(Vec(x, 65.6f), module, MODULE::IN + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 96.4f), module, MODULE::IN_X_INPUT + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 121.8f), module, MODULE::IN_X_PARAM + i));
			addParam(createParamCentered<DummyMapButton>(Vec(x + s * 7.8f, 136.1f), module, MODULE::IN_X_POS + i));
			ClickableSmallLight<GreenLight>* l = createLightCentered<ClickableSmallLight<GreenLight>>(Vec(x, 139.6f), module, MODULE::IN_SEL_LIGHT + i);
			l->id = i;
			l->type = 0;
			addChild(l);
			addParam(createParamCentered<DummyMapButton>(Vec(x - s * 7.8f, 143.1f), module, MODULE::IN_Y_POS + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 157.4f), module, MODULE::IN_Y_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 182.8f), module, MODULE::IN_Y_INPUT + i));

			ArenaOpDisplay<MODULE>* arenaOpDisplay = createWidgetCentered<ArenaOpDisplay<MODULE>>(Vec(x, 210.8f));
			arenaOpDisplay->module = module;
			arenaOpDisplay->id = i;
			addChild(arenaOpDisplay);

			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 230.7f), module, MODULE::OP_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 253.9f), module, MODULE::OP_INPUT + i));

			addOutput(createOutputCentered<StoermelderPort>(Vec(x, 284.9f), module, MODULE::OUT + i));
		}

		ArenaAreaWidget<MODULE, 8, 2>* area = new ArenaAreaWidget<MODULE, 8, 2>(module, MODULE::IN_X_POS, MODULE::IN_Y_POS, MODULE::MIX_X_POS, MODULE::MIX_Y_POS);
		area->box.pos = Vec(162.5f, 54.5f);
		area->box.size = Vec(290.0f, 241.4f);
		addChild(area);

		// MIX1
		addInput(createInputCentered<StoermelderPort>(Vec(25.9f, 323.4f), module, MODULE::MIX_X_INPUT + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(53.9f, 323.4f), module, MODULE::MIX_X_PARAM + 0));
		addParam(createParamCentered<DummyMapButton>(Vec(68.4f, 331.0f), module, MODULE::MIX_X_POS + 0));
		ClickableSmallLight<GreenLight>* l1 = createLightCentered<ClickableSmallLight<GreenLight>>(Vec(71.7f, 323.4f), module, MODULE::MIX_SEL_LIGHT + 0);
		l1->id = 0;
		l1->type = 1;
		addChild(l1);
		addParam(createParamCentered<DummyMapButton>(Vec(75.4f, 315.8f), module, MODULE::MIX_Y_POS + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(89.4f, 323.4f), module, MODULE::MIX_Y_PARAM + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(117.2f, 323.4f), module, MODULE::MIX_Y_INPUT + 0));

		addInput(createInputCentered<StoermelderPort>(Vec(173.9f, 323.4f), module, MODULE::SEQ_INPUT + 0));
		ArenaSeqDisplay<MODULE>* arenaSeqDisplay1 = createWidgetCentered<ArenaSeqDisplay<MODULE>>(Vec(206.5, 325.4f));
		arenaSeqDisplay1->module = module;
		arenaSeqDisplay1->id = 0;
		addChild(arenaSeqDisplay1);
		addInput(createInputCentered<StoermelderPort>(Vec(239.0f, 323.4f), module, MODULE::SEQ_PH_INPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(288.0f, 323.4f), module, MODULE::MIX_OUTPUT + 0));

		// MIX2
		addOutput(createOutputCentered<StoermelderPort>(Vec(327.5f, 323.4f), module, MODULE::MIX_OUTPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(376.0f, 323.4f), module, MODULE::SEQ_PH_INPUT + 1));
		ArenaSeqDisplay<MODULE>* arenaSeqDisplay2 = createWidgetCentered<ArenaSeqDisplay<MODULE>>(Vec(408.6, 325.4f));
		arenaSeqDisplay2->module = module;
		arenaSeqDisplay2->id = 1;
		addChild(arenaSeqDisplay2);
		addInput(createInputCentered<StoermelderPort>(Vec(441.1f, 323.4f), module, MODULE::SEQ_INPUT + 1));

		addInput(createInputCentered<StoermelderPort>(Vec(497.7f, 323.4f), module, MODULE::MIX_X_INPUT + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(525.6f, 323.4f), module, MODULE::MIX_X_PARAM + 1));
		addParam(createParamCentered<DummyMapButton>(Vec(539.9f, 315.8f), module, MODULE::MIX_X_POS + 1));
		ClickableSmallLight<GreenLight>* l2 = createLightCentered<ClickableSmallLight<GreenLight>>(Vec(543.4f, 323.4f), module, MODULE::MIX_SEL_LIGHT + 1);
		l2->id = 1;
		l2->type = 1;
		addChild(l2);
		addParam(createParamCentered<DummyMapButton>(Vec(546.9f, 331.0f), module, MODULE::MIX_Y_POS + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(561.2f, 323.4f), module, MODULE::MIX_Y_PARAM + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(589.2f, 323.4f), module, MODULE::MIX_Y_INPUT + 1));
	}

	void appendContextMenu(Menu* menu) override {
		struct ManualItem : MenuItem {
			void onAction(const event::Action &e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Arena.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		//menu->addChild(new MenuSeparator());
	}
};

} // namespace Arena

Model* modelArena = createModel<Arena::ArenaModule<8, 2>, Arena::ArenaWidget>("Arena");