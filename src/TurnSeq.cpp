#include "plugin.hpp"
#include <thread>

namespace TurnSeq {

enum GRIDSTATE {
	OFF = 0,
	ON = 1,
	RANDOM = 2
};

enum MODULESTATE {
	GRID = 0,
	EDIT = 1
};


template < int SIZE, int NUM_PORTS >
struct TurnSeqModule : Module {
	enum ParamIds {
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(CLK_INPUT, NUM_PORTS),
		ENUMS(RESET_INPUT, NUM_PORTS),
		ENUMS(TURN_INPUT, NUM_PORTS),
		SHIFT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, NUM_PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	const int numPorts = NUM_PORTS;

	/** [Stored to JSON] */
	int usedSize = 8;
	/** [Stored to JSON] */
	GRIDSTATE grid[SIZE][SIZE];

	/** [Stored to JSON] */
	int xStartDir[NUM_PORTS];
	/** [Stored to JSON] */
	int yStartDir[NUM_PORTS];
	/** [Stored to JSON] */
	int xStartPos[NUM_PORTS];
	/** [Stored to JSON] */
	int yStartPos[NUM_PORTS];
	/** [Stored to JSON] */
	int xDir[NUM_PORTS];
	/** [Stored to JSON] */
	int yDir[NUM_PORTS];
	/** [Stored to JSON] */
	int xPos[NUM_PORTS];
	/** [Stored to JSON] */
	int yPos[NUM_PORTS];

	dsp::SchmittTrigger clockTrigger[NUM_PORTS];
	bool clockTrigger0;
	dsp::SchmittTrigger resetTrigger[NUM_PORTS];
	bool resetTrigger0;
	dsp::SchmittTrigger turnTrigger[NUM_PORTS];
	bool turnTrigger0;
	dsp::Timer resetTimer[NUM_PORTS];
	float resetTimer0;
	dsp::PulseGenerator outPulse[NUM_PORTS];

	dsp::SchmittTrigger shiftTrigger;

	bool active[NUM_PORTS];
	bool changed[NUM_PORTS];

	MODULESTATE currentState = MODULESTATE::GRID;

	TurnSeqModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void onReset() override {
		gridClear();
		for (int i = 0; i < NUM_PORTS; i++) {
			xPos[i] = xStartPos[i] = 0;
			yPos[i] = yStartPos[i] = usedSize / NUM_PORTS * i;
			xDir[i] = xStartDir[i] = 1;
			yDir[i] = yStartDir[i] = 0;
			resetTimer[i].reset();
		}
		Module::onReset();
	}

	void onRandomize() override {
		gridRandomize();
		Module::onRandomize();
	}

	void process(const ProcessArgs& args) override {
		if (shiftTrigger.process(inputs[SHIFT_INPUT].getVoltage())) {
			for (int i = 0; i < NUM_PORTS; i++) {
				xPos[i] = (xPos[i] + -1 * yDir[i] + usedSize) % usedSize;
				yPos[i] = (yPos[i] + xDir[i] + usedSize) % usedSize;
			}
		}

		for (int i = 0; i < NUM_PORTS; i++) {
			if (processResetTrigger(i)) {
				xPos[i] = xStartPos[i];
				yPos[i] = yStartPos[i];
				xDir[i] = xStartDir[i];
				yDir[i] = yStartDir[i];
			}

			if (processClockTrigger(i, args.sampleTime)) {
				xPos[i] = (xPos[i] + xDir[i] + usedSize) % usedSize;
				yPos[i] = (yPos[i] + yDir[i] + usedSize) % usedSize;
				changed[i] = true;
			}

			if (processTurnTrigger(i)) {
				if (xDir[i] == 1 && yDir[i] == 0) {
					xDir[i] = 0; yDir[i] = 1;
				}
				else if (xDir[i] == 0 && yDir[i] == 1) {
					xDir[i] = -1; yDir[i] = 0;
				}
				else if (xDir[i] == -1 && yDir[i] == 0) {
					xDir[i] = 0; yDir[i] = -1;
				}
				else {
					xDir[i] = 1; yDir[i] = 0;
				}
			}

			if (changed[i]) {
				switch (grid[xPos[i]][yPos[i]]) {
					case GRIDSTATE::OFF:
						break;
					case GRIDSTATE::ON:
						outPulse[i].trigger();
						break;
					case GRIDSTATE::RANDOM:
						if (random::uniform() >= 0.5f) outPulse[i].trigger();
						break;
				}
				changed[i] = false;
			}

			active[i] = outputs[OUTPUT + i].isConnected();
			outputs[OUTPUT + i].setVoltage(outPulse[i].process(args.sampleTime) ? 10.f : 0.f);
		}
	}

	inline bool processResetTrigger(int port) {
		if (port == 0) {
			resetTrigger0 = resetTrigger[0].process(inputs[RESET_INPUT].getVoltage() + params[RESET_PARAM].getValue());
			if (resetTrigger0) resetTimer[0].reset();
			return resetTrigger0;
		}
		else {
			if (inputs[RESET_INPUT + port].isConnected()) {
				bool r = resetTrigger[port].process(inputs[RESET_INPUT + port].getVoltage() + params[RESET_PARAM].getValue());
				if (r) resetTimer[port].reset();
				return r;
			}
			else {
				return resetTrigger0;
			}
		}
	}

	inline bool processClockTrigger(int port, float sampleTime) {
		if (port == 0) {
			resetTimer0 = resetTimer[0].process(sampleTime);
			clockTrigger0 = resetTimer0 >= 1e-3f && clockTrigger[0].process(inputs[CLK_INPUT].getVoltage());
			return clockTrigger0;
		}
		else {
			bool r = resetTimer0 >= 1e-3f;
			if (inputs[RESET_INPUT + port].isConnected()) {
				r = resetTimer[port].process(sampleTime) >= 1e-3f;
			}
			if (inputs[CLK_INPUT + port].isConnected()) {
				return r && clockTrigger[port].process(inputs[CLK_INPUT + port].getVoltage());
			}
			else {
				return clockTrigger0;
			}
		}
	}

	inline bool processTurnTrigger(int port) {
		if (port == 0) {
			turnTrigger0 = turnTrigger[0].process(inputs[TURN_INPUT].getVoltage());
			return turnTrigger0;
		}
		else {
			if (inputs[TURN_INPUT + port].isConnected()) {
				return turnTrigger[port].process(inputs[TURN_INPUT + port].getVoltage());
			}
			else {
				return turnTrigger0;
			}
		}
	}

	void gridClear() {
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				grid[i][j] = GRIDSTATE::OFF;
			}
		}
	}

	void gridResize(int size) {
		usedSize = size;
		for (int i = 0; i < NUM_PORTS; i++) {
			xStartPos[i] = 0;
			yStartPos[i] = usedSize / NUM_PORTS * i;
			xPos[i] = (xPos[i] + usedSize) % usedSize;
			yPos[i] = (yPos[i] + usedSize) % usedSize;
		}
	}

	void gridRandomize() {
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				float r = random::uniform();
				if (r > 0.8f)
					grid[i][j] = GRIDSTATE::RANDOM;
				else if (r > 0.6f)
					grid[i][j] = GRIDSTATE::ON;
				else
					grid[i][j] = GRIDSTATE::OFF;
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_t* gridJ = json_array();
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				json_array_append_new(gridJ, json_integer(grid[i][j]));
			}
		}
		json_object_set_new(rootJ, "grid", gridJ);

		json_t* portsJ = json_array();
		for (int i = 0; i < NUM_PORTS; i++) {
			json_t* portJ = json_object();
			json_object_set_new(portJ, "xStartPos", json_integer(xStartPos[i]));
			json_object_set_new(portJ, "yStartPos", json_integer(yStartPos[i]));
			json_object_set_new(portJ, "xStartDir", json_integer(xStartDir[i]));
			json_object_set_new(portJ, "yStartDir", json_integer(yStartDir[i]));
			json_object_set_new(portJ, "xPos", json_integer(xPos[i]));
			json_object_set_new(portJ, "yPos", json_integer(yPos[i]));
			json_object_set_new(portJ, "xDir", json_integer(xDir[i]));
			json_object_set_new(portJ, "yDir", json_integer(yDir[i]));
			json_array_append_new(portsJ, portJ);
		}
		json_object_set_new(rootJ, "ports", portsJ);

		json_object_set_new(rootJ, "usedSize", json_integer(usedSize));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* gridJ = json_object_get(rootJ, "grid");
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				grid[i][j] = (GRIDSTATE)json_integer_value(json_array_get(gridJ, i * SIZE + j));
			}
		}
		
		json_t* portsJ = json_object_get(rootJ, "ports");
		json_t* portJ;
		size_t portIndex;
		json_array_foreach(portsJ, portIndex, portJ) {
			xStartPos[portIndex] = json_integer_value(json_object_get(portJ, "xStartPos"));
			yStartPos[portIndex] = json_integer_value(json_object_get(portJ, "yStartPos"));
			xStartDir[portIndex] = json_integer_value(json_object_get(portJ, "xStartDir"));
			yStartDir[portIndex] = json_integer_value(json_object_get(portJ, "yStartDir"));
			xPos[portIndex] = json_integer_value(json_object_get(portJ, "xPos"));
			yPos[portIndex] = json_integer_value(json_object_get(portJ, "yPos"));
			xDir[portIndex] = json_integer_value(json_object_get(portJ, "xDir"));
			yDir[portIndex] = json_integer_value(json_object_get(portJ, "yDir"));
		}

		usedSize = json_integer_value(json_object_get(rootJ, "usedSize"));
	}
};


// Context menus

template < typename MODULE >
struct ModuleStateMenuItem : MenuItem {
	MODULE* module;
	
	void onAction(const event::Action &e) override {
		module->currentState = module->currentState == MODULESTATE::GRID ? MODULESTATE::EDIT : MODULESTATE::GRID;
	}
};

template < typename MODULE >
struct SizeMenuItem : MenuItem {
	SizeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct SizeItem : MenuItem {
		MODULE* module;
		int usedSize;
		
		void onAction(const event::Action &e) override {
			module->gridResize(usedSize);
		}

		void step() override {
			rightText = module->usedSize == usedSize ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<SizeItem>(&MenuItem::text, "4 x 4", &SizeItem::module, module, &SizeItem::usedSize, 4));
		menu->addChild(construct<SizeItem>(&MenuItem::text, "8 x 8", &SizeItem::module, module, &SizeItem::usedSize, 8));
		menu->addChild(construct<SizeItem>(&MenuItem::text, "16 x 16", &SizeItem::module, module, &SizeItem::usedSize, 16));
		menu->addChild(construct<SizeItem>(&MenuItem::text, "32 x 32", &SizeItem::module, module, &SizeItem::usedSize, 32));
		return menu;
	}
};

template < typename MODULE >
struct RandomizeMenuItem : MenuItem {
	MODULE* module;
	
	void onAction(const event::Action &e) override {
		module->gridRandomize();
	}
};

template < typename MODULE >
struct ClearMenuItem : MenuItem {
	MODULE* module;
	
	void onAction(const event::Action &e) override {
		module->gridClear();
	}
};


// Widgets

template < typename MODULE >
struct TurnSeqDrawHelper {
	MODULE* module;
	int* xpos;
	int* ypos;

	NVGcolor gridColor = color::WHITE;
	NVGcolor colors[4] = { color::YELLOW, color::RED, color::CYAN, color::BLUE };

	void draw(const Widget::DrawArgs& args, Rect box) {
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

		float sizeX = box.size.x / module->usedSize;
		float sizeY = box.size.y / module->usedSize;

		float stroke = 0.7f;
		for (int i = 0; i < module->usedSize; i++) {
			for (int j = 0; j < module->usedSize; j++) {
				switch (module->grid[i][j]) {
					case GRIDSTATE::ON:
						nvgBeginPath(args.vg);
						nvgRect(args.vg, i * sizeX + stroke / 2.f, j * sizeY + stroke / 2.f, sizeX - stroke, sizeY - stroke);
						nvgFillColor(args.vg, color::mult(gridColor, 0.55f));
						nvgFill(args.vg);
						break;
					case GRIDSTATE::RANDOM:
						nvgBeginPath(args.vg);
						nvgRect(args.vg, i * sizeX + stroke, j * sizeY + stroke, sizeX - stroke * 2.f, sizeY - stroke * 2.f);
						nvgStrokeWidth(args.vg, stroke);
						nvgStrokeColor(args.vg, color::mult(gridColor, 0.45f));
						nvgStroke(args.vg);
						nvgBeginPath(args.vg);
						nvgRect(args.vg, i * sizeX + sizeX * 0.25f, j * sizeY + sizeY * 0.25f, sizeX * 0.5f, sizeY * 0.5f);
						nvgFillColor(args.vg, color::mult(gridColor, 0.3f));
						nvgFill(args.vg);
						break;
					case GRIDSTATE::OFF:
						break;
				}
			}
		}

		float r = box.size.y / module->usedSize / 2.f;

		for (int i = 0; i < module->numPorts; i++) {
			if (module->currentState == MODULESTATE::EDIT || module->active[i]) {
				Vec c = Vec(xpos[i] * sizeX + r, ypos[i] * sizeY + r);

				// Inner circle
				nvgGlobalCompositeOperation(args.vg, NVG_ATOP);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, r * 0.75f);
				nvgFillColor(args.vg, color::mult(colors[i], 0.35f));
				nvgFill(args.vg);

				// Outer cirlce
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, r - 0.7f);
				nvgStrokeColor(args.vg, color::mult(colors[i], 0.9f));
				nvgStrokeWidth(args.vg, 0.7f);
				nvgStroke(args.vg);
			}
		}
		for (int i = 0; i < module->numPorts; i++) {
			if (module->currentState == MODULESTATE::EDIT || module->active[i]) {
				Vec c = Vec(xpos[i] * sizeX + r, ypos[i] * sizeY + r);
				// Halo
				NVGpaint paint;
				NVGcolor icol = color::mult(colors[i], 0.25f);
				NVGcolor ocol = nvgRGB(0, 0, 0);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, r * 1.5f);
				paint = nvgRadialGradient(args.vg, c.x, c.y, r, r * 1.5f, icol, ocol);
				nvgFillPaint(args.vg, paint);
				nvgFill(args.vg);
			}
		}
	}
};


template < typename MODULE >
struct TurnSeqStartPosEditWidget : OpaqueWidget, TurnSeqDrawHelper<MODULE> {
	MODULE* module;
	std::shared_ptr<Font> font;
	int selectedId = -1;
	math::Vec dragPos;

	TurnSeqStartPosEditWidget(MODULE* module) {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		this->module = module;
		TurnSeqDrawHelper<MODULE>::module = module;
		TurnSeqDrawHelper<MODULE>::xpos = module->xStartPos;
		TurnSeqDrawHelper<MODULE>::ypos = module->yStartPos;
		TurnSeqDrawHelper<MODULE>::gridColor = color::mult(color::WHITE, 0.4f);
	}

	void draw(const DrawArgs& args) override {
		if (module && module->currentState == MODULESTATE::EDIT) {
			NVGcolor c = color::mult(color::WHITE, 0.7f);
			float stroke = 1.f;
			nvgGlobalCompositeOperation(args.vg, NVG_ATOP);

			// Outer border
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f + stroke, 0.f + stroke, box.size.x - 2 * stroke, box.size.y - 2 * stroke);
			nvgStrokeWidth(args.vg, stroke);
			nvgStrokeColor(args.vg, c);
			nvgStroke(args.vg);

			// Draw "EDIT" text
			nvgFontSize(args.vg, 22);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -2.2);
			nvgFillColor(args.vg, c);
			nvgTextBox(args.vg, box.size.x - 40.f, box.size.y - 6.f, 120, "EDIT", NULL);

			TurnSeqDrawHelper<MODULE>::draw(args, box);

			float r = box.size.y / module->usedSize / 2.f;
			float rS = r * 0.75f;
			float sizeX = box.size.x / module->usedSize;
			float sizeY = box.size.y / module->usedSize;

			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			for (int i = 0; i < module->numPorts; i++) {
				// Direction triangle
				Vec c = Vec(module->xStartPos[i] * sizeX + r, module->yStartPos[i] * sizeY + r);
				Vec p1 = Vec(c.x + module->yStartDir[i] * rS, c.y - module->xStartDir[i] * rS);
				Vec p2 = Vec(c.x + module->xStartDir[i] * rS, c.y + module->yStartDir[i] * rS);
				Vec p3 = Vec(c.x - module->yStartDir[i] * rS, c.y + module->xStartDir[i] * rS);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, p1.x, p1.y);
				nvgLineTo(args.vg, p2.x, p2.y);
				nvgLineTo(args.vg, p3.x, p3.y);
				nvgClosePath(args.vg);
				nvgFillColor(args.vg, color::mult(color::WHITE, 0.9f));
				nvgFill(args.vg);
			}

			OpaqueWidget::draw(args);
		}
	}

	void onButton(const event::Button& e) override {
		if (module && module->currentState == MODULESTATE::EDIT) {
			if (e.action == GLFW_PRESS) {
				selectedId = -1;
				int x = (int)std::floor((e.pos.x / box.size.x) * module->usedSize);
				int y = (int)std::floor((e.pos.y / box.size.y) * module->usedSize);
				for (int i = 0; i < module->numPorts; i++) {
					if (module->xStartPos[i] == x && module->yStartPos[i] == y) {
						selectedId = i;
						break;
					}
				}

				if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
					dragPos = APP->scene->rack->mousePos.minus(e.pos);
					e.consume(this);
				}
				if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
					if (selectedId == -1) 
						createContextMenu();
					else 
						createDirectionContextMenu();
					e.consume(this);
				}
			} 
			OpaqueWidget::onButton(e);
		}
	}

	void onDragMove(const event::DragMove& e) override {
		if (module && module->currentState == MODULESTATE::EDIT) {
			if (e.button != GLFW_MOUSE_BUTTON_LEFT)
				return;
			if (selectedId == -1)
				return;

			math::Vec pos = APP->scene->rack->mousePos.minus(dragPos);
			int x = (int)std::floor((pos.x / box.size.x) * module->usedSize);
			int y = (int)std::floor((pos.y / box.size.y) * module->usedSize);
			module->xStartPos[selectedId] = std::max(0, std::min(x, module->usedSize - 1));
			module->yStartPos[selectedId] = std::max(0, std::min(y, module->usedSize - 1));
		}
	}

	void createDirectionContextMenu() {
		struct DirectionItem : MenuItem {
			MODULE* module;
			int xdir, ydir;
			int id;

			void onAction(const event::Action &e) override {
				module->xStartDir[id] = xdir;
				module->yStartDir[id] = ydir;
			}

			void step() override {
				bool s = module->xStartDir[id] == xdir && module->yStartDir[id] == ydir;
				rightText = s ? "✔" : "";
				MenuItem::step();
			}
		};

		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Start direction"));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Right", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, 1, &DirectionItem::ydir, 0));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Down", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, 0, &DirectionItem::ydir, 1));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Left", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, -1, &DirectionItem::ydir, 0));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Up", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, 0, &DirectionItem::ydir, -1));
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<ModuleStateMenuItem<MODULE>>(&MenuItem::text, "Exit Edit-mode", &ModuleStateMenuItem<MODULE>::module, module));
	}
};


template < typename MODULE >
struct TurnSeqScreenWidget : OpaqueWidget, TurnSeqDrawHelper<MODULE> {
	MODULE* module;

	TurnSeqScreenWidget(MODULE* module) {
		this->module = module;
		TurnSeqDrawHelper<MODULE>::module = module;
		TurnSeqDrawHelper<MODULE>::xpos = module->xPos;
		TurnSeqDrawHelper<MODULE>::ypos = module->yPos;
	}

	void draw(const DrawArgs& args) override {
		if (module && module->currentState == MODULESTATE::GRID) {
			TurnSeqDrawHelper<MODULE>::draw(args, box);
			OpaqueWidget::draw(args);
		}
	}

	void onButton(const event::Button& e) override {
		if (module && module->currentState == MODULESTATE::GRID) {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				int x = (int)std::floor((e.pos.x / box.size.x) * module->usedSize);
				int y = (int)std::floor((e.pos.y / box.size.y) * module->usedSize);
				module->grid[x][y] = (GRIDSTATE)((module->grid[x][y] + 1) % 3);
				e.consume(this);
			}
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				createContextMenu();
				e.consume(this);
			}
			OpaqueWidget::onButton(e);
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<ModuleStateMenuItem<MODULE>>(&MenuItem::text, "Enter Edit-mode", &ModuleStateMenuItem<MODULE>::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SizeMenuItem<MODULE>>(&MenuItem::text, "Dimension", &SizeMenuItem<MODULE>::module, module));
		menu->addChild(construct<RandomizeMenuItem<MODULE>>(&MenuItem::text, "Randomize", &RandomizeMenuItem<MODULE>::module, module));
		menu->addChild(construct<ClearMenuItem<MODULE>>(&MenuItem::text, "Clear", &ClearMenuItem<MODULE>::module, module));
	}
};


struct TurnSeqWidget32 : ModuleWidget {
	typedef TurnSeqModule<32, 4> MODULE;
	TurnSeqWidget32(MODULE* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/TurnSeq.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		TurnSeqScreenWidget<MODULE>* turnWidget = new TurnSeqScreenWidget<MODULE>(module);
		turnWidget->box.pos = Vec(8.7f, 36.6f);
		turnWidget->box.size = Vec(162.6f, 162.6f);
		addChild(turnWidget);

		TurnSeqStartPosEditWidget<MODULE>* resetEditWidget = new TurnSeqStartPosEditWidget<MODULE>(module);
		resetEditWidget->box.pos = turnWidget->box.pos;
		resetEditWidget->box.size = turnWidget->box.size;
		addChild(resetEditWidget);

		addInput(createInputCentered<StoermelderPort>(Vec(24.1f, 227.5f), module, MODULE::CLK_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(54.6f, 227.5f), module, MODULE::CLK_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(125.5f, 227.5f), module, MODULE::CLK_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(155.9f, 227.5f), module, MODULE::CLK_INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(24.1f, 253.5f), module, MODULE::RESET_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(54.6f, 253.5f), module, MODULE::RESET_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(125.5f, 253.5f), module, MODULE::RESET_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(155.9f, 253.5f), module, MODULE::RESET_INPUT + 3));

		addParam(createParamCentered<TL1105>(Vec(90.0f, 262.2f), module, MODULE::RESET_PARAM));

		addInput(createInputCentered<StoermelderPort>(Vec(24.1f, 292.1f), module, MODULE::TURN_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(54.6f, 292.1f), module, MODULE::TURN_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(125.5f, 292.1f), module, MODULE::TURN_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(155.9f, 292.1f), module, MODULE::TURN_INPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(24.1f, 327.3f), module, MODULE::OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(54.6f, 327.3f), module, MODULE::OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(125.5f, 327.3f), module, MODULE::OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(155.9f, 327.3f), module, MODULE::OUTPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(90.0f, 327.6f), module, MODULE::SHIFT_INPUT));
	}

	void appendContextMenu(Menu* menu) override {
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action &e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/TurnSeq.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
	}
};

} // namespace TurnSeq

Model* modelTurnSeq = createModel<TurnSeq::TurnSeqModule<32, 4>, TurnSeq::TurnSeqWidget32>("TurnSeq");