#include "plugin.hpp"
#include "digital.hpp"
#include <thread>
#include <random>


namespace Maze {

enum GRIDSTATE {
	OFF = 0,
	ON = 1,
	RANDOM = 2
};

enum TURNMODE {
	NINETY = 0,
	ONEEIGHTY = 1
};

enum OUTMODE {
	BI_5V = 0,
	UNI_5V = 1,
	UNI_3V = 2,
	UNI_1V = 3
};

enum MODULESTATE {
	GRID = 0,
	EDIT = 1
};

template < int SIZE, int NUM_PORTS >
struct MazeModule : Module {
	enum ParamIds {
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(CLK_INPUT, NUM_PORTS),
		ENUMS(RESET_INPUT, NUM_PORTS),
		ENUMS(TURN_INPUT, NUM_PORTS),
		SHIFT_R_INPUT,
		SHIFT_L_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(TRIG_OUTPUT, NUM_PORTS),
		ENUMS(CV_OUTPUT, NUM_PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	const int numPorts = NUM_PORTS;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::geometric_distribution<int>* geoDist = NULL;

	/** [Stored to JSON] */
	int usedSize = 8;
	/** [Stored to JSON] */
	GRIDSTATE grid[SIZE][SIZE];
	/** [Stored to JSON] */
	float gridCv[SIZE][SIZE];

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

	/** [Stored to JSON] */
	TURNMODE turnMode[NUM_PORTS];
	/** [Stored to JSON] */
	OUTMODE outMode[NUM_PORTS];

	/** [Stored to JSON] */
	bool ratchetingEnabled;
	/** [Stored to JSON] */
	float ratchetingProb;

	dsp::SchmittTrigger clockTrigger[NUM_PORTS];
	bool clockTrigger0;
	dsp::SchmittTrigger resetTrigger[NUM_PORTS];
	bool resetTrigger0;
	dsp::SchmittTrigger turnTrigger[NUM_PORTS];
	bool turnTrigger0;
	dsp::Timer resetTimer[NUM_PORTS];
	float resetTimer0;
	dsp::PulseGenerator outPulse[NUM_PORTS];
	ClockMultiplier multiplier[NUM_PORTS];

	dsp::SchmittTrigger shiftRTrigger;
	dsp::SchmittTrigger shiftLTrigger;

	bool active[NUM_PORTS];
	MODULESTATE currentState = MODULESTATE::GRID;
	bool gridDirty = true;

	MazeModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	~MazeModule() {
		delete geoDist;
	}

	void onReset() override {
		gridClear();
		for (int i = 0; i < NUM_PORTS; i++) {
			xPos[i] = xStartPos[i] = 0;
			yPos[i] = yStartPos[i] = usedSize / NUM_PORTS * i;
			xDir[i] = xStartDir[i] = 1;
			yDir[i] = yStartDir[i] = 0;
			turnMode[i] = TURNMODE::NINETY;
			outMode[i] = OUTMODE::UNI_3V;
			resetTimer[i].reset();
		}
		ratchetingEnabled = true;
		ratchetingSetProb();
		gridDirty = true;
		Module::onReset();
	}

	void process(const ProcessArgs& args) override {
		if (shiftRTrigger.process(inputs[SHIFT_R_INPUT].getVoltage())) {
			for (int i = 0; i < NUM_PORTS; i++) {
				xPos[i] = (xPos[i] + -1 * yDir[i] + usedSize) % usedSize;
				yPos[i] = (yPos[i] +  1 * xDir[i] + usedSize) % usedSize;
			}
		}
		if (shiftLTrigger.process(inputs[SHIFT_L_INPUT].getVoltage())) {
			for (int i = 0; i < NUM_PORTS; i++) {
				xPos[i] = (xPos[i] +  1 * yDir[i] + usedSize) % usedSize;
				yPos[i] = (yPos[i] + -1 * xDir[i] + usedSize) % usedSize;
			}
		}

		for (int i = 0; i < NUM_PORTS; i++) {
			active[i] = outputs[TRIG_OUTPUT + i].isConnected() || outputs[CV_OUTPUT + i].isConnected();
			bool doPulse = false;

			if (processResetTrigger(i)) {
				xPos[i] = xStartPos[i];
				yPos[i] = yStartPos[i];
				xDir[i] = xStartDir[i];
				yDir[i] = yStartDir[i];
				multiplier[i].reset();
			}

			if (processClockTrigger(i, args.sampleTime)) {
				xPos[i] = (xPos[i] + xDir[i] + usedSize) % usedSize;
				yPos[i] = (yPos[i] + yDir[i] + usedSize) % usedSize;
				multiplier[i].tick();

				switch (grid[xPos[i]][yPos[i]]) {
					case GRIDSTATE::OFF:
						break;
					case GRIDSTATE::ON:
						doPulse = true;
						break;
					case GRIDSTATE::RANDOM:
						if (ratchetingEnabled) {
							if (geoDist) 
								multiplier[i].trigger((*geoDist)(randGen));
						}
						else {
							doPulse = random::uniform() >= 0.5f;
						}
						break;
				}
			}

			if (processTurnTrigger(i)) {
				if (xDir[i] == 1 && yDir[i] == 0) {
					xDir[i] = turnMode[i] == TURNMODE::NINETY ? 0 : -1;
					yDir[i] = turnMode[i] == TURNMODE::NINETY ? 1 : 0;
				}
				else if (xDir[i] == 0 && yDir[i] == 1) {
					xDir[i] = turnMode[i] == TURNMODE::NINETY ? -1 : 0;
					yDir[i] = turnMode[i] == TURNMODE::NINETY ? 0 : -1;
				}
				else if (xDir[i] == -1 && yDir[i] == 0) {
					xDir[i] = turnMode[i] == TURNMODE::NINETY ? 0 : 1;
					yDir[i] = turnMode[i] == TURNMODE::NINETY ? -1 : 0;
				}
				else {
					xDir[i] = turnMode[i] == TURNMODE::NINETY ? 1 : 0;
					yDir[i] = turnMode[i] == TURNMODE::NINETY ? 0 : 1;
				}
			}

			float outGate = 0.f;
			float outCv = outputs[CV_OUTPUT + i].getVoltage();

			if (multiplier[i].process() || doPulse) {
				outPulse[i].trigger();
				switch (outMode[i]) {
					case OUTMODE::BI_5V:
						outCv = rescale(gridCv[xPos[i]][yPos[i]], 0.f, 1.f, -5.f, 5.f);
						break;
					case OUTMODE::UNI_5V:
						outCv = rescale(gridCv[xPos[i]][yPos[i]], 0.f, 1.f, 0.f, 5.f);
						break;
					case OUTMODE::UNI_3V:
						outCv = rescale(gridCv[xPos[i]][yPos[i]], 0.f, 1.f, 0.f, 3.f);
						break;
					case OUTMODE::UNI_1V:
						outCv = gridCv[xPos[i]][yPos[i]];
						break;
				}
			}

			if (outPulse[i].process(args.sampleTime))
				outGate = 10.f;

			outputs[TRIG_OUTPUT + i].setVoltage(outGate);
			outputs[CV_OUTPUT + i].setVoltage(outCv);
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
				gridCv[i][j] = 0.f;
			}
		}
		gridDirty = true;
	}

	void gridResize(int size) {
		if (size == usedSize) return;
		usedSize = size;
		for (int i = 0; i < NUM_PORTS; i++) {
			xStartPos[i] = 0;
			yStartPos[i] = usedSize / NUM_PORTS * i;
			xPos[i] = (xPos[i] + usedSize) % usedSize;
			yPos[i] = (yPos[i] + usedSize) % usedSize;
		}
		gridDirty = true;
	}

	void gridRandomize(bool useRandom = true) {
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				float r = random::uniform();
				if (r > 0.8f) {
					grid[i][j] = useRandom ? GRIDSTATE::RANDOM : GRIDSTATE::ON;
					gridCv[i][j] = random::uniform();
				}
				else if (r > 0.6f) {
					grid[i][j] = GRIDSTATE::ON;
					gridCv[i][j] = random::uniform();
				}
				else {
					grid[i][j] = GRIDSTATE::OFF;
					gridCv[i][j] = 0.f;
				}
			}
		}
		gridDirty = true;
	}

	void gridNextState(int i, int j) {
		grid[i][j] = (GRIDSTATE)((grid[i][j] + 1) % 3);
		if (grid[i][j] == GRIDSTATE::ON) gridCv[i][j] = random::uniform();
		gridDirty = true;
	}

	void gridSetState(int i, int j, GRIDSTATE s, float cv) {
		grid[i][j] = s;
		gridCv[i][j] = cv;
		gridDirty = true;
	}

	void ratchetingSetProb(float prob = 0.35f) {
		if (geoDist) delete geoDist;
		geoDist = new std::geometric_distribution<int>(prob);
		ratchetingProb = prob;
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

		json_t* gridCvJ = json_array();
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				json_array_append_new(gridCvJ, json_real(gridCv[i][j]));
			}
		}
		json_object_set_new(rootJ, "gridCv", gridCvJ);

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
			json_object_set_new(portJ, "turnMode", json_integer(turnMode[i]));
			json_object_set_new(portJ, "outMode", json_integer(outMode[i]));
			json_array_append_new(portsJ, portJ);
		}
		json_object_set_new(rootJ, "ports", portsJ);

		json_object_set_new(rootJ, "usedSize", json_integer(usedSize));
		json_object_set_new(rootJ, "ratchetingEnabled", json_boolean(ratchetingEnabled));
		json_object_set_new(rootJ, "ratchetingProb", json_real(ratchetingProb));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* gridJ = json_object_get(rootJ, "grid");
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				grid[i][j] = (GRIDSTATE)json_integer_value(json_array_get(gridJ, i * SIZE + j));
			}
		}
		
		json_t* gridCvJ = json_object_get(rootJ, "gridCv");
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				gridCv[i][j] = json_real_value(json_array_get(gridCvJ, i * SIZE + j));
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
			turnMode[portIndex] = (TURNMODE)json_integer_value(json_object_get(portJ, "turnMode"));
			outMode[portIndex] = (OUTMODE)json_integer_value(json_object_get(portJ, "outMode"));
		}

		usedSize = json_integer_value(json_object_get(rootJ, "usedSize"));
		ratchetingEnabled = json_boolean_value(json_object_get(rootJ, "ratchetingEnabled"));
		ratchetingProb = json_real_value(json_object_get(rootJ, "ratchetingProb"));
		gridDirty = true;
	}
};


// Context menus

template < typename MODULE >
struct ModuleStateMenuItem : MenuItem {
	MODULE* module;
	
	void onAction(const event::Action &e) override {
		module->currentState = module->currentState == MODULESTATE::GRID ? MODULESTATE::EDIT : MODULESTATE::GRID;
		module->gridDirty = true;
	}
};

template < typename MODULE >
struct GridCellChangeAction : history::ModuleAction {
	int x, y;
	GRIDSTATE oldGrid, newGrid;
	float oldGridCv, newGridCv;

	GridCellChangeAction() {
		name = "stoermelder MAZE cell";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		assert(m);
		m->gridSetState(x, y, oldGrid, oldGridCv);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		assert(m);
		m->gridSetState(x, y, newGrid, newGridCv);
	}
};


template < typename MODULE >
struct GridSizeSlider : ui::Slider {
	struct GridSizeQuantity : Quantity {
		MODULE* module;
		float v = -1.f;

		GridSizeQuantity(MODULE* module) {
			this->module = module;
		}
		void setValue(float value) override {
			v = clamp(value, 2.f, 32.f);
			module->gridResize(int(v));
		}
		float getValue() override {
			if (v < 0.f) v = module->usedSize;
			return v;
		}
		float getDefaultValue() override {
			return 8.f;
		}
		float getMinValue() override {
			return 2.f;
		}
		float getMaxValue() override {
			return 32.f;
		}
		float getDisplayValue() override {
			return getValue();
		}
		std::string getDisplayValueString() override {
			int i = int(getValue());
			return string::f("%i x %i", i, i);
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue);
		}
		std::string getLabel() override {
			return "Dimension";
		}
		std::string getUnit() override {
			return "";
		}
	};

	GridSizeSlider(MODULE* module) {
		quantity = new GridSizeQuantity(module);
	}
	~GridSizeSlider() {
		delete quantity;
	}
	void onDragMove(const event::DragMove& e) override {
		if (quantity) {
			quantity->moveScaledValue(0.002f * e.mouseDelta.x);
		}
	}
};

template < typename MODULE >
struct GridRandomizeMenuItem : MenuItem {
	MODULE* module;
	bool useRandom = true;
	
	void onAction(const event::Action& e) override {
		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "stoermelder MAZE grid randomize";
		h->moduleId = module->id;
		h->oldModuleJ = module->toJson();

		module->gridRandomize(useRandom);

		h->newModuleJ = module->toJson();
		APP->history->push(h);
	}
};

template < typename MODULE >
struct GridClearMenuItem : MenuItem {
	MODULE* module;
	
	void onAction(const event::Action& e) override {
		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "stoermelder MAZE grid clear";
		h->moduleId = module->id;
		h->oldModuleJ = module->toJson();

		module->gridClear();

		h->newModuleJ = module->toJson();
		APP->history->push(h);
	}
};

template < typename MODULE >
struct RatchetingMenuItem : MenuItem {
	MODULE* module;

	void onAction(const event::Action& e) override {
		module->ratchetingEnabled ^= true;
	}

	void step() override {
		rightText = module->ratchetingEnabled ? "✔" : "";
		MenuItem::step();
	}
};

template < typename MODULE >
struct RatchetingProbMenuItem : MenuItem {
	RatchetingProbMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct RatchetingProbItem : MenuItem {
		MODULE* module;
		float ratchetingProb;
		
		void onAction(const event::Action& e) override {
			module->ratchetingSetProb(ratchetingProb);
		}

		void step() override {
			rightText = module->ratchetingProb == ratchetingProb ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "50%", &RatchetingProbItem::module, module, &RatchetingProbItem::ratchetingProb, 0.5f));
		menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "60%", &RatchetingProbItem::module, module, &RatchetingProbItem::ratchetingProb, 0.4f));
		menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "65%", &RatchetingProbItem::module, module, &RatchetingProbItem::ratchetingProb, 0.35f));
		menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "70%", &RatchetingProbItem::module, module, &RatchetingProbItem::ratchetingProb, 0.3f));
		menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "80%", &RatchetingProbItem::module, module, &RatchetingProbItem::ratchetingProb, 0.2f));
		menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "90%", &RatchetingProbItem::module, module, &RatchetingProbItem::ratchetingProb, 0.1f));
		return menu;
	}
};


// Widgets

template < typename MODULE >
struct MazeGridWidget : FramebufferWidget {
	struct MazeGridDrawWidget : OpaqueWidget {
		MODULE* module;
		NVGcolor gridColor = color::WHITE;

		MazeGridDrawWidget(MODULE* module) {
			this->module = module;
		}

		void draw(const Widget::DrawArgs& args) override {
			if (!module) return;
			float sizeX = box.size.x / module->usedSize;
			float sizeY = box.size.y / module->usedSize;

			// Draw background
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgFillColor(args.vg, nvgRGB(0, 16, 90));
			nvgFill(args.vg);

			// Draw grid
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgStrokeWidth(args.vg, 0.6f);
			for (int i = 1; i < module->usedSize; i++) {
				float a = 0.075f;
				if (module->usedSize % 4 == 0) { if (i % 4 == 0) a = 0.2f; }
				else if (module->usedSize % 3 == 0) { if (i % 3 == 0) a = 0.2f; }
				else if (module->usedSize % 5 == 0) { if (i % 5 == 0) a = 0.2f; }
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, sizeX * i, 0.f);
				nvgLineTo(args.vg, sizeX * i, box.size.y);
				nvgStrokeColor(args.vg, color::mult(color::WHITE, a));
				nvgStroke(args.vg);
			}
			for (int i = 1; i < module->usedSize; i++) {
				float a = 0.075f;
				if (module->usedSize % 4 == 0) { if (i % 4 == 0) a = 0.2f; }
				else if (module->usedSize % 3 == 0) { if (i % 3 == 0) a = 0.2f; }
				else if (module->usedSize % 5 == 0) { if (i % 5 == 0) a = 0.2f; }
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0.f, sizeY * i);
				nvgLineTo(args.vg, box.size.x, sizeY * i);
				nvgStrokeColor(args.vg, color::mult(color::WHITE, a));
				nvgStroke(args.vg);
			}

			// Draw grid cells
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
		}
	};

	MODULE* module;
	MazeGridDrawWidget* w;
	
	MazeGridWidget(MODULE* module) {
		this->module = module;
		w = new MazeGridDrawWidget(module);
		addChild(w);
	}

	void step() override{
		if (module && module->gridDirty) {
			FramebufferWidget::dirty = true;
			w->box.size = box.size;
			w->gridColor = module->currentState == MODULESTATE::EDIT ? color::mult(color::WHITE, 0.35f) : color::WHITE;
			module->gridDirty = false;
		}
		FramebufferWidget::step();
	}
};


template < typename MODULE >
struct MazeDrawHelper {
	MODULE* module;
	int* xpos;
	int* ypos;

	NVGcolor colors[4] = { color::YELLOW, color::RED, color::CYAN, color::BLUE };

	void draw(const Widget::DrawArgs& args, Rect box) {
		float sizeX = box.size.x / module->usedSize;
		float sizeY = box.size.y / module->usedSize;
		float r = box.size.y / module->usedSize / 2.f;

		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
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
struct MazeStartPosEditWidget : OpaqueWidget, MazeDrawHelper<MODULE> {
	MODULE* module;
	std::shared_ptr<Font> font;
	int selectedId = -1;
	math::Vec dragPos;

	MazeStartPosEditWidget(MODULE* module) {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		this->module = module;
		MazeDrawHelper<MODULE>::module = module;
		MazeDrawHelper<MODULE>::xpos = module->xStartPos;
		MazeDrawHelper<MODULE>::ypos = module->yStartPos;
	}

	void draw(const DrawArgs& args) override {
		if (module && module->currentState == MODULESTATE::EDIT) {
			NVGcolor c = color::mult(color::WHITE, 0.7f);
			float stroke = 1.f;
			nvgGlobalCompositeOperation(args.vg, NVG_ATOP);

			// Outer border
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgStrokeWidth(args.vg, stroke);
			nvgStrokeColor(args.vg, c);
			nvgStroke(args.vg);

			// Draw "EDIT" text
			nvgFontSize(args.vg, 22);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -2.2);
			nvgFillColor(args.vg, c);
			nvgTextBox(args.vg, box.size.x - 40.f, box.size.y - 6.f, 120, "EDIT", NULL);

			MazeDrawHelper<MODULE>::draw(args, box);

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
		ui::Menu* menu = createMenu();

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

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Start direction"));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Right", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, 1, &DirectionItem::ydir, 0));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Down", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, 0, &DirectionItem::ydir, 1));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Left", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, -1, &DirectionItem::ydir, 0));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "Up", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::xdir, 0, &DirectionItem::ydir, -1));

		struct TurnModeItem : MenuItem {
			MODULE* module;
			TURNMODE turnMode;
			int id;

			void onAction(const event::Action &e) override {
				module->turnMode[id] = turnMode;
			}

			void step() override {
				rightText = module->turnMode[id] == turnMode ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Turn mode"));
		menu->addChild(construct<TurnModeItem>(&MenuItem::text, "Ninety", &TurnModeItem::module, module, &TurnModeItem::id, selectedId, &TurnModeItem::turnMode, TURNMODE::NINETY));
		menu->addChild(construct<TurnModeItem>(&MenuItem::text, "One-Eighty", &TurnModeItem::module, module, &TurnModeItem::id, selectedId, &TurnModeItem::turnMode, TURNMODE::ONEEIGHTY));

		struct OutModeItem : MenuItem {
			MODULE* module;
			OUTMODE outMode;
			int id;

			void onAction(const event::Action &e) override {
				module->outMode[id] = outMode;
			}

			void step() override {
				rightText = module->outMode[id] == outMode ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "CV mode"));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "-5..5V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::BI_5V));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "0..5V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::UNI_5V));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "0..3V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::UNI_3V));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "0..1V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::UNI_1V));
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<ModuleStateMenuItem<MODULE>>(&MenuItem::text, "Exit Edit-mode", &ModuleStateMenuItem<MODULE>::module, module));
	}
};


template < typename MODULE >
struct MazeScreenWidget : OpaqueWidget, MazeDrawHelper<MODULE> {
	MODULE* module;

	MazeScreenWidget(MODULE* module) {
		this->module = module;
		MazeDrawHelper<MODULE>::module = module;
		MazeDrawHelper<MODULE>::xpos = module->xPos;
		MazeDrawHelper<MODULE>::ypos = module->yPos;
	}

	void draw(const DrawArgs& args) override {
		if (module && module->currentState == MODULESTATE::GRID) {
			MazeDrawHelper<MODULE>::draw(args, box);
			OpaqueWidget::draw(args);
		}
	}

	void onButton(const event::Button& e) override {
		if (module && module->currentState == MODULESTATE::GRID) {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				int x = (int)std::floor((e.pos.x / box.size.x) * module->usedSize);
				int y = (int)std::floor((e.pos.y / box.size.y) * module->usedSize);

				// History
				GridCellChangeAction<MODULE>* h = new GridCellChangeAction<MODULE>;
				h->moduleId = module->id;
				h->x = x;
				h->y = y;
				h->oldGrid = module->grid[x][y];
				h->oldGridCv = module->gridCv[x][y];

				module->gridNextState(x, y);
				
				h->newGrid = module->grid[x][y];
				h->newGridCv = module->gridCv[x][y];
				APP->history->push(h);

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
		menu->addChild(construct<RatchetingMenuItem<MODULE>>(&MenuItem::text, "Ratcheting", &RatchetingMenuItem<MODULE>::module, module));
		menu->addChild(construct<RatchetingProbMenuItem<MODULE>>(&MenuItem::text, "Ratcheting probability", &RatchetingProbMenuItem<MODULE>::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Grid"));
		GridSizeSlider<MODULE>* sizeSlider = new GridSizeSlider<MODULE>(module);
		sizeSlider->box.size.x = 200.0;
		menu->addChild(sizeSlider);

		menu->addChild(construct<GridRandomizeMenuItem<MODULE>>(&MenuItem::text, "Randomize", &GridRandomizeMenuItem<MODULE>::module, module));
		menu->addChild(construct<GridRandomizeMenuItem<MODULE>>(&MenuItem::text, "Randomize certainty", &GridRandomizeMenuItem<MODULE>::module, module, &GridRandomizeMenuItem<MODULE>::useRandom, false));
		menu->addChild(construct<GridClearMenuItem<MODULE>>(&MenuItem::text, "Clear", &GridClearMenuItem<MODULE>::module, module));
	}
};


struct MazeWidget32 : ModuleWidget {
	typedef MazeModule<32, 4> MODULE;
	MazeWidget32(MODULE* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Maze.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MazeGridWidget<MODULE>* gridWidget = new MazeGridWidget<MODULE>(module);
		gridWidget->box.pos = Vec(51.5f, 40.3f);
		gridWidget->box.size = Vec(227.f, 227.f);
		addChild(gridWidget);

		MazeScreenWidget<MODULE>* turnWidget = new MazeScreenWidget<MODULE>(module);
		turnWidget->box.pos = gridWidget->box.pos;
		turnWidget->box.size = gridWidget->box.size;
		addChild(turnWidget);

		MazeStartPosEditWidget<MODULE>* resetEditWidget = new MazeStartPosEditWidget<MODULE>(module);
		resetEditWidget->box.pos = turnWidget->box.pos;
		resetEditWidget->box.size = turnWidget->box.size;
		addChild(resetEditWidget);

		addInput(createInputCentered<StoermelderPort>(Vec(23.8f, 256.0f), module, MODULE::SHIFT_L_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(306.2f, 256.0f), module, MODULE::SHIFT_R_INPUT));

		addInput(createInputCentered<StoermelderPort>(Vec(116.8f, 292.2f), module, MODULE::CLK_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(116.8f, 327.6f), module, MODULE::CLK_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(213.6f, 292.2f), module, MODULE::CLK_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(213.6f, 327.6f), module, MODULE::CLK_INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(144.0f, 292.2f), module, MODULE::RESET_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(144.0f, 327.6f), module, MODULE::RESET_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(186.5f, 292.2f), module, MODULE::RESET_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(186.5f, 327.6f), module, MODULE::RESET_INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(84.1f, 292.2f), module, MODULE::TURN_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(84.1f, 327.6f), module, MODULE::TURN_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(245.7f, 292.2f), module, MODULE::TURN_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(245.7f, 327.6f), module, MODULE::TURN_INPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(51.9f, 292.2f), module, MODULE::TRIG_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(51.9f, 327.6f), module, MODULE::TRIG_OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(277.8f, 292.2f), module, MODULE::TRIG_OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(277.8f, 327.6f), module, MODULE::TRIG_OUTPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(24.7f, 292.2f), module, MODULE::CV_OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(24.7f, 327.6f), module, MODULE::CV_OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(304.9f, 292.2f), module, MODULE::CV_OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(304.9f, 327.6f), module, MODULE::CV_OUTPUT + 3));
	}

	void appendContextMenu(Menu* menu) override {
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Maze.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
	}
};

} // namespace Maze

Model* modelMaze = createModel<Maze::MazeModule<32, 4>, Maze::MazeWidget32>("Maze");