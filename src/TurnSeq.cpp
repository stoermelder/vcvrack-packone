#include "plugin.hpp"
#include <thread>

namespace TurnSeq {

enum GRIDSTATE {
	OFF = 0,
	ON = 1,
	RANDOM = 2
};

template < int SIZE, int NUM_PORTS >
struct TurnSeqModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(CLK_INPUT, NUM_PORTS),
		ENUMS(RESET_INPUT, NUM_PORTS),
		ENUMS(TURN_INPUT, NUM_PORTS),
		RAND_INPUT,
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
	int usedSize;
	/** [Stored to JSON] */
	GRIDSTATE grid[SIZE][SIZE];

	/** [Stored to JSON] */
	int xDir[SIZE];
	/** [Stored to JSON] */
	int yDir[SIZE];
	/** [Stored to JSON] */
	int xPos[SIZE];
	/** [Stored to JSON] */
	int yPos[SIZE];

	dsp::SchmittTrigger clockTrigger[NUM_PORTS];
	bool clockTrigger0;
	dsp::SchmittTrigger resetTrigger[NUM_PORTS];
	bool resetTrigger0;
	dsp::SchmittTrigger turnTrigger[NUM_PORTS];
	bool turnTrigger0;
	dsp::Timer resetTimer[NUM_PORTS];
	float resetTimer0;
	dsp::PulseGenerator outPulse[NUM_PORTS];

	dsp::SchmittTrigger randTrigger;

	bool active[NUM_PORTS];
	bool changed[NUM_PORTS];

	TurnSeqModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void onReset() override {
		usedSize = 8;
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				grid[i][j] = GRIDSTATE::OFF;
			}
		}
		for (int i = 0; i < NUM_PORTS; i++) {
			xPos[i] = 0;
			yPos[i] = usedSize / NUM_PORTS * i;
			xDir[i] = 1;
			yDir[i] = 0;
			resetTimer[i].reset();
		}
		Module::onReset();
	}

	void onRandomize() override {
		gridRandomize();
	}

	void process(const ProcessArgs &args) override {
		if (randTrigger.process(inputs[RAND_INPUT].getVoltage())) {
			gridRandomize();
		}

		for (int i = 0; i < NUM_PORTS; i++) {
			if (processResetTrigger(i)) {
				xPos[i] = 0;
				yPos[i] = usedSize / NUM_PORTS * i;
				xDir[i] = 1;
				yDir[i] = 0;
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

	bool processResetTrigger(int port) {
		if (port == 0) {
			resetTrigger0 = resetTrigger[0].process(inputs[RESET_INPUT].getVoltage());
			if (resetTrigger0) resetTimer[0].reset();
			return resetTrigger0;
		}
		else {
			if (inputs[RESET_INPUT + port].isConnected()) {
				bool r = resetTrigger[port].process(inputs[RESET_INPUT + port].getVoltage());
				if (r) resetTimer[port].reset();
				return r;
			}
			else {
				return resetTrigger0;
			}
		}
	}

	bool processClockTrigger(int port, float sampleTime) {
		if (port == 0) {
			resetTimer0 = resetTimer[0].process(sampleTime);
			clockTrigger0 = resetTimer0 >= 1e-3f && clockTrigger[0].process(inputs[CLK_INPUT].getVoltage());
			return clockTrigger0;
		}
		else {
			bool r = resetTimer0 >= 1e-3f;
			if (inputs[RESET_INPUT + port].isConnected()) r = resetTimer[port].process(sampleTime) >= 1e-3f;
			if (inputs[CLK_INPUT + port].isConnected()) {
				return r && clockTrigger[port].process(inputs[CLK_INPUT + port].getVoltage());
			}
			else {
				return clockTrigger0;
			}
		}
	}

	bool processTurnTrigger(int port) {
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

	void gridSetSize(int size) {
		usedSize = size;
		for (int i = 0; i < NUM_PORTS; i++) {
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
struct SizeMenuItem : MenuItem {
	SizeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct SizeItem : MenuItem {
		MODULE* module;
		int usedSize;
		
		void onAction(const event::Action &e) override {
			module->gridSetSize(usedSize);
		}

		void step() override {
			rightText = module->usedSize == usedSize ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<SizeItem>(&MenuItem::text, "4", &SizeItem::module, module, &SizeItem::usedSize, 4));
		menu->addChild(construct<SizeItem>(&MenuItem::text, "8", &SizeItem::module, module, &SizeItem::usedSize, 8));
		menu->addChild(construct<SizeItem>(&MenuItem::text, "16", &SizeItem::module, module, &SizeItem::usedSize, 16));
		menu->addChild(construct<SizeItem>(&MenuItem::text, "32", &SizeItem::module, module, &SizeItem::usedSize, 32));
		return menu;
	}
};


// Widgets

template < typename MODULE >
struct TurnSeqScreenWidget : OpaqueWidget {
	MODULE* module;

	void draw(const DrawArgs& args) override {
		if (!module) return;

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
						nvgFillColor(args.vg, color::mult(color::WHITE, 0.45f));
						nvgFill(args.vg);
						break;
					case GRIDSTATE::RANDOM:
						nvgBeginPath(args.vg);
						nvgRect(args.vg, i * sizeX + stroke, j * sizeY + stroke, sizeX - stroke * 2.f, sizeY - stroke * 2.f);
						nvgStrokeWidth(args.vg, stroke);
						nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.4f));
						nvgStroke(args.vg);
						nvgBeginPath(args.vg);
						nvgRect(args.vg, i * sizeX + sizeX * 0.25f, j * sizeY + sizeY * 0.25f, sizeX * 0.5f, sizeY * 0.5f);
						nvgFillColor(args.vg, color::mult(color::WHITE, 0.25f));
						nvgFill(args.vg);
						break;
					case GRIDSTATE::OFF:
						break;
				}
			}
		}

		float r = box.size.y / module->usedSize / 2.f;
		NVGcolor colors[] = { color::YELLOW, color::RED, color::CYAN, color::BLUE };

		for (int i = 0; i < module->numPorts; i++) {
			if (module->active[i]) {
				Vec c = Vec(module->xPos[i] * sizeX + r, module->yPos[i] * sizeY + r);

				// Inner circle
				nvgGlobalCompositeOperation(args.vg, NVG_ATOP);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, r * 0.75f);
				nvgFillColor(args.vg, color::mult(colors[i], 0.4f));
				nvgFill(args.vg);

				// Outer cirlce
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, r - 0.7f);
				nvgStrokeColor(args.vg, color::mult(colors[i], 0.8f));
				nvgStrokeWidth(args.vg, 0.7f);
				nvgStroke(args.vg);
			}
		}
		for (int i = 0; i < module->numPorts; i++) {
			if (module->active[i]) {
				Vec c = Vec(module->xPos[i] * sizeX + r, module->yPos[i] * sizeY + r);
				// Halo
				NVGpaint paint;
				NVGcolor icol = color::mult(colors[i], 0.25f);
				NVGcolor ocol = nvgRGB(0, 0, 0);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, r * 2);
				paint = nvgRadialGradient(args.vg, c.x, c.y, r, r * 2, icol, ocol);
				nvgFillPaint(args.vg, paint);
				nvgFill(args.vg);
			}
		}

		OpaqueWidget::draw(args);
	}

	void onButton(const event::Button& e) override {
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

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<SizeMenuItem<MODULE>>(&MenuItem::text, "Size", &SizeMenuItem<MODULE>::module, module));
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

		TurnSeqScreenWidget<MODULE>* turnWidget = new TurnSeqScreenWidget<MODULE>;
		turnWidget->module = module;
		turnWidget->box.pos = Vec(8.7f, 39.3f);
		turnWidget->box.size = Vec(162.6f, 162.6f);
		addChild(turnWidget);

		addInput(createInputCentered<StoermelderPort>(Vec(24.2f, 230.8f), module, MODULE::CLK_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(54.6f, 230.8f), module, MODULE::CLK_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(125.6f, 230.8f), module, MODULE::CLK_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(156.0f, 230.8f), module, MODULE::CLK_INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(24.2f, 258.4f), module, MODULE::RESET_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(54.6f, 258.4f), module, MODULE::RESET_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(125.6f, 258.4f), module, MODULE::RESET_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(156.0f, 258.4f), module, MODULE::RESET_INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(24.2f, 290.2f), module, MODULE::TURN_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(54.6f, 290.2f), module, MODULE::TURN_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(125.6f, 290.2f), module, MODULE::TURN_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(156.0f, 290.2f), module, MODULE::TURN_INPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(24.2f, 327.3f), module, MODULE::OUTPUT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(54.6f, 327.3f), module, MODULE::OUTPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(125.6f, 327.3f), module, MODULE::OUTPUT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(156.0f, 327.3f), module, MODULE::OUTPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(90.0f, 327.3f), module, MODULE::RAND_INPUT));
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