#include "plugin.hpp"
#include "components/Knobs.hpp"
#include "components/LedTextDisplay.hpp"
#include "components/XySeqWidget.hpp"
#include <chrono>
#include <random>

namespace StoermelderPackOne {
namespace Arena {

enum MODMODE {
	RADIUS = 0,
	AMOUNT = 1,
	OFFSET_X = 2,
	OFFSET_Y = 3,
//	ROTATE = 6,
	WALK = 7
};

enum OUTPUTMODE {
	SCALE = 0,
	LIMIT = 1,
	CLIP_UNI = 2,
	CLIP_BI = 3,
	FOLD_UNI = 4,
	FOLD_BI = 5
};


template <int IN_PORTS, int MIX_PORTS>
struct ArenaModule : Module, XySeqModule<MIX_PORTS> {
	enum ParamIds {
		ENUMS(IN_X_POS, IN_PORTS),
		ENUMS(IN_Y_POS, IN_PORTS),
		ENUMS(IN_X_PARAM, IN_PORTS),
		ENUMS(IN_Y_PARAM, IN_PORTS),
		ENUMS(IN_X_CTRL, IN_PORTS),
		ENUMS(IN_Y_CTRL, IN_PORTS),
		ENUMS(MOD_PARAM, IN_PORTS),
		ENUMS(IN_PLUS_PARAM, IN_PORTS),
		ENUMS(IN_MINUS_PARAM, IN_PORTS),
		ENUMS(MIX_X_POS, MIX_PORTS),
		ENUMS(MIX_Y_POS, MIX_PORTS),
		ENUMS(MIX_X_PARAM, MIX_PORTS),
		ENUMS(MIX_Y_PARAM, MIX_PORTS),
		ENUMS(MIX_SEL_PARAM, MIX_PORTS),
		ENUMS(MIX_VOL_PARAM, MIX_PORTS),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(IN, IN_PORTS),
		ENUMS(IN_X_INPUT, IN_PORTS),
		ENUMS(IN_Y_INPUT, IN_PORTS),
		ENUMS(MOD_INPUT, IN_PORTS),
		ENUMS(MIX_X_INPUT, MIX_PORTS),
		ENUMS(MIX_Y_INPUT, MIX_PORTS),
		ENUMS(SEQ_INPUT, MIX_PORTS),
		ENUMS(SEQ_PH_INPUT, MIX_PORTS),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(MIX_OUTPUT, MIX_PORTS),
		ENUMS(OUT_OUTPUT, IN_PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(IN_SEL_LIGHT, IN_PORTS),
		ENUMS(MIX_SEL_LIGHT, MIX_PORTS),
		NUM_LIGHTS
	};

	typedef XySeqModule<MIX_PORTS> Seq;

	const int numInports = IN_PORTS;
	const int numMixports = MIX_PORTS;
	int selectedId = -1;
	int selectedType = -1;

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	float radius[IN_PORTS];
	float radiusUi[IN_PORTS];
	dsp::ExponentialFilter radiusFilter[IN_PORTS];
	/** [Stored to JSON] */
	float amount[IN_PORTS];
	/** [Stored to JSON] */
	MODMODE modMode[IN_PORTS];
	/** [Stored to JSON] */
	bool inputXBipolar[IN_PORTS];
	/** [Stored to JSON] */
	bool inputYBipolar[IN_PORTS];
	/** [Stored to JSON] */
	OUTPUTMODE outputMode[IN_PORTS];
	/** [Stored to JSON] */
	bool mixportXBipolar[MIX_PORTS];
	/** [Stored to JSON] */
	bool mixportYBipolar[MIX_PORTS];
	/** [Stored to JSON] */
	int inportsUsed = IN_PORTS;
	/** [Stored to JSON] */
	int mixportsUsed = MIX_PORTS;

	float dist[MIX_PORTS][IN_PORTS];
	float offsetX[IN_PORTS];
	float offsetY[IN_PORTS];

	//float lastInXpos[IN_PORTS];
	//float lastInYpos[IN_PORTS];
	//float lastMixXpos[MIX_PORTS];
	//float lastMixYpos[MIX_PORTS];

	dsp::ClockDivider lightDivider;

	ArenaModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// in-ports
		for (int i = 0; i < IN_PORTS; i++) {
			configInput(IN + i, string::f("Channel IN-%i", i + 1));
			configInput(IN_X_INPUT + i, string::f("Channel IN-%i x-pos CV", i + 1));
			configInput(IN_Y_INPUT + i, string::f("Channel IN-%i y-pos CV", i + 1));
			configInput(MOD_INPUT + i, string::f("Channel IN-%i modulation", i + 1));
			configOutput(OUT_OUTPUT + i, string::f("Channel IN-%i direct", i + 1));
			configParam(IN_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (IN_PORTS - 1)), string::f("Channel IN-%i x-pos", i + 1));
			configParam(IN_Y_POS + i, 0.0f, 1.0f, 0.1f, string::f("Channel IN-%i y-pos", i + 1));
			configParam(IN_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i x-pos attenuverter", i + 1), "x");
			configParam(IN_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i y-pos attenuverter", i + 1), "x");
			configParam(MOD_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i Mod attenuverter", i + 1), "x");
			radiusFilter[i].setTau(0.1f);
		}
		// mix-ports
		for (int i = 0; i < MIX_PORTS; i++) {
			configInput(MIX_X_INPUT + i, string::f("Channel MIX-%i x-pos", i + 1));
			configInput(MIX_Y_INPUT + i, string::f("Channel MIX-%i y-pos", i + 1));
			configInput(SEQ_INPUT + i, string::f("Channel MIX-%i sequence select", i + 1));
			configInput(SEQ_PH_INPUT + i, string::f("Channel MIX-%i sequence phase", i + 1));
			configOutput(MIX_OUTPUT + i, string::f("Channel MIX-%i", i + 1));
			configParam(MIX_VOL_PARAM + i, 0.0f, 2.0f, 1.0f, string::f("Channel MIX-%i volume", i + 1));
			configParam(MIX_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (MIX_PORTS - 1)), string::f("Channel MIX-%i x-pos", i + 1));
			configParam(MIX_Y_POS + i, 0.0f, 1.0f, 0.9f, string::f("Channel MIX-%i y-pos", i + 1));
			configParam(MIX_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel MIX-%i x-pos attenuverter", i + 1), "x");
			configParam(MIX_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel MIX-%i y-pos attenuverter", i + 1), "x");
		}
		onReset();
		lightDivider.setDivision(512);
	}

	void onReset() override {
		selectionReset();
		init();
		for (int i = 0; i < IN_PORTS; i++) {
			radius[i] = radiusUi[i] = 0.5f;
			modMode[i] = MODMODE::RADIUS;
			inputXBipolar[i] = false;
			inputYBipolar[i] = false;
			outputMode[i] = OUTPUTMODE::SCALE;
		}
		for (int i = 0; i < MIX_PORTS; i++) {
			mixportXBipolar[i] = false;
			mixportYBipolar[i] = false;
		}
		Seq::seqReset();
		Module::onReset();
	}

	void onRandomize() override {
		randomizeInputAmount();
		randomizeInputRadius();
		randomizeInputX();
		randomizeInputY();
		Module::onRandomize();
	}

	void process(const ProcessArgs& args) override {
		float inNorm[IN_PORTS] = {0.f};
		for (int j = 0; j < inportsUsed; j++) {
			radius[j] = radiusFilter[j].process(args.sampleTime, radiusUi[j]);

			offsetX[j] = 0.f;
			offsetY[j] = 0.f;
			switch (modMode[j]) {
				case MODMODE::RADIUS: {
					if (inputs[MOD_INPUT + j].isConnected()) {
						radius[j] = getOpInput(j);
					}
					break;
				}
				case MODMODE::AMOUNT: {
					if (inputs[MOD_INPUT + j].isConnected()) {
						amount[j] = getOpInput(j);
					}
					break;
				}
				case MODMODE::OFFSET_X: {
					offsetX[j] = getOpInput(j);
					break;
				}
				case MODMODE::OFFSET_Y: {
					offsetY[j] = getOpInput(j);
					break;
				}
				case MODMODE::WALK: {
					float v = getOpInput(j);
					offsetX[j] = random::normal() / 2000.f * v;
					offsetY[j] = random::normal() / 2000.f * v;
					break;
				}
			}

			float x = params[IN_X_POS + j].getValue();
			if (inputs[IN_X_INPUT + j].isConnected()) {
				float xd = inputs[IN_X_INPUT + j].getVoltage();
				xd *= params[IN_X_PARAM + j].getValue();
				xd += inputXBipolar[j] ? 5.f : 0.f;
				x = clamp(xd / 10.f, 0.f, 1.f);
			}
			x += offsetX[j];
			x = clamp(x, 0.f, 1.f);
			params[IN_X_POS + j].setValue(x);

			float y = params[IN_Y_POS + j].getValue();
			if (inputs[IN_Y_INPUT + j].isConnected()) {
				float yd = inputs[IN_Y_INPUT + j].getVoltage();
				yd *= params[IN_Y_PARAM + j].getValue();
				yd += inputYBipolar[j] ? 5.f : 0.f;
				y = clamp(yd / 10.f, 0.f, 1.f);
			}
			y += offsetY[j];
			y = clamp(y, 0.f, 1.f);
			params[IN_Y_POS + j].setValue(y);

			if (inputs[IN + j].isConnected()) {
				float sd = inputs[IN + j].getVoltage();
				sd = clamp(sd, -10.f, 10.f);
				sd *= amount[j];
				inNorm[j] = sd;
			}
		}

		float outNorm[IN_PORTS] = {0.f};
		for (int i = 0; i < mixportsUsed; i++) {
			if (inputs[SEQ_INPUT + i].isConnected()) {
				Seq::seqProcess(inputs[SEQ_INPUT + i], i);
			}

			if (inputs[SEQ_PH_INPUT + i].isConnected()) {
				float v = clamp(inputs[SEQ_PH_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
				Vec d = Seq::seqValue(i, v);
				params[MIX_X_POS + i].setValue(d.x);
				params[MIX_Y_POS + i].setValue(d.y);
			}

			if (inputs[MIX_X_INPUT + i].isConnected()) {
				float x = inputs[MIX_X_INPUT + i].getVoltage() / 10.f;
				x *= params[MIX_X_PARAM + i].getValue();
				x += mixportXBipolar[i] ? 0.5f : 0.f;
				x = clamp(x, 0.f, 1.f);
				params[MIX_X_POS + i].setValue(x);
			} 

			if (inputs[MIX_Y_INPUT + i].isConnected()) {
				float y = inputs[MIX_Y_INPUT + i].getVoltage() / 10.f;
				y *= params[MIX_Y_PARAM + i].getValue();
				y += mixportYBipolar[i] ? 0.5f : 0.f;
				y = clamp(y, 0.f, 1.f);
				params[MIX_Y_POS + i].setValue(y);
			}

			float mixX = params[MIX_X_POS + i].getValue();
			float mixY = params[MIX_Y_POS + i].getValue();
			Vec mixVec = Vec(mixX, mixY);

			float mix = 0.f;
			for (int j = 0; j < inportsUsed; j++) {
				float inX = params[IN_X_POS + j].getValue();
				float inY = params[IN_Y_POS + j].getValue();

				//if (mixX != lastMixXpos[i] || mixY != lastMixYpos[i] || inX != lastInXpos[j] || inY != lastInYpos[j]) {
					//lastInXpos[j] = inX;
					//lastInYpos[j] = inY;
					Vec inVec = Vec(inX, inY);
					dist[i][j] = inVec.minus(mixVec).norm();
				//}

				float r = radius[j];
				if (inputs[IN + j].isConnected() && dist[i][j] < r) {
					float s = std::min(1.0f, (r - dist[i][j]) / r * 1.1f);
					outNorm[j] += s;
					s *= inNorm[j];
					mix += s;
				}
			}

			//lastMixXpos[i] = mixX;
			//lastMixYpos[i] = mixY;
			mix *= params[MIX_VOL_PARAM + i].getValue();
			outputs[MIX_OUTPUT + i].setVoltage(mix);
		}

		for (int j = 0; j < inportsUsed; j++) {
			if (inputs[IN + j].isConnected() && outputs[OUT_OUTPUT + j].isConnected()) {
				float v = inputs[IN + j].getVoltage();
				switch (outputMode[j]) {
					case OUTPUTMODE::SCALE: {
						v *= outNorm[j] / MIX_PORTS;
						v = clamp(v, -10.f, 10.f);
						break;
					}
					case OUTPUTMODE::LIMIT: {
						v *= std::min(outNorm[j], 1.f);
						v = clamp(v, -10.f, 10.f);
						break;
					}
					case OUTPUTMODE::CLIP_UNI: {
						v *= outNorm[j];
						v = clamp(v, 0.f, 10.f);
						break;
					}
					case OUTPUTMODE::CLIP_BI: {
						v *= outNorm[j];
						v = clamp(v, -5.f, 5.f);
						break;
					}
					case OUTPUTMODE::FOLD_UNI: {
						v = clamp(v, 0.f, 10.f) / 10.f * outNorm[j];
						float intf;
						float frac = std::modf(v, &intf);
						v = int(intf) % 2 == 0 ? frac : (1.f - frac);
						v *= 10.f;
						break;
					}
					case OUTPUTMODE::FOLD_BI: {
						v = clamp(v, -5.f, 5.f) / 5.f * outNorm[j];
						float intf;
						float frac = std::modf(v, &intf);
						v = int(intf) % 2 == 0 ? frac : (frac >= 0.f ? (1.f - frac) : (-1.f - frac));
						v *= 5.f;
						break;
					}
				}
				outputs[OUT_OUTPUT + j].setVoltage(v);
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
		float v = inputs[MOD_INPUT + j].isConnected() ? inputs[MOD_INPUT + j].getVoltage() : 10.f;
		v *= params[MOD_PARAM + j].getValue();
		v = clamp(v / 10.f, -1.f, 1.f);
		return v;
	}

	inline void selectionSet(int type, int id) {
		if (type == 0 && id + 1 > inportsUsed) return;
		if (type == 1 && id + 1 > mixportsUsed) return;
		selectedType = type;
		selectedId = id;
	}

	inline bool selectionTest(int type, int id) {
		return selectedType == type && selectedId == id;
	}

	inline void selectionReset() {
		selectedType = -1;
		selectedId = -1;
	}

	bool seqPortUsed(int port) override {
		return port + 1 > mixportsUsed;
	}


	void init() {
		for (int i = 0; i < IN_PORTS; i++) {
			radius[i] = 0.5f;
			amount[i] = 1.f;
			paramQuantities[IN_X_POS + i]->setValue(paramQuantities[IN_X_POS + i]->getDefaultValue());
			paramQuantities[IN_Y_POS + i]->setValue(paramQuantities[IN_Y_POS + i]->getDefaultValue());
			//lastInXpos[i] = -1.f;
			//lastInYpos[i] = -1.f;
		}
		for (int i = 0; i < MIX_PORTS; i++) {
			paramQuantities[MIX_X_POS + i]->setValue(paramQuantities[MIX_X_POS + i]->getDefaultValue());
			paramQuantities[MIX_Y_POS + i]->setValue(paramQuantities[MIX_Y_POS + i]->getDefaultValue());
			//lastMixXpos[i] = -1.f;
			//lastMixYpos[i] = -1.f;
		}
		Seq::seqInit();
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
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* inportsJ = json_array();
		for (int i = 0; i < IN_PORTS; i++) {
			json_t* inportJ = json_object();
			json_object_set_new(inportJ, "amount", json_real(amount[i]));
			json_object_set_new(inportJ, "radius", json_real(radius[i]));
			json_object_set_new(inportJ, "modMode", json_integer(modMode[i]));
			json_object_set_new(inportJ, "inputXBipolar", json_boolean(inputXBipolar[i]));
			json_object_set_new(inportJ, "inputYBipolar", json_boolean(inputYBipolar[i]));
			json_object_set_new(inportJ, "outputMode", json_integer(outputMode[i]));
			json_array_append_new(inportsJ, inportJ);
		}
		json_object_set_new(rootJ, "inports", inportsJ);

		json_t* mixportsJ = json_array();
		for (int i = 0; i < MIX_PORTS; i++) {
			json_t* mixportJ = json_object();
			json_object_set_new(mixportJ, "mixportXBipolar", json_boolean(mixportXBipolar[i]));
			json_object_set_new(mixportJ, "mixportYBipolar", json_boolean(mixportYBipolar[i]));
			Seq::dataToJson(mixportJ, i);
			json_array_append_new(mixportsJ, mixportJ);
		}
		json_object_set_new(rootJ, "mixports", mixportsJ);

		json_object_set_new(rootJ, "inportsUsed", json_integer(inportsUsed));
		json_object_set_new(rootJ, "mixportsUsed", json_integer(mixportsUsed));

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* inportsJ = json_object_get(rootJ, "inports");
		json_t* inportJ;
		size_t inputIndex;
		json_array_foreach(inportsJ, inputIndex, inportJ) {
			amount[inputIndex] = json_real_value(json_object_get(inportJ, "amount"));
			radiusUi[inputIndex] = radius[inputIndex] = json_real_value(json_object_get(inportJ, "radius"));
			modMode[inputIndex] = (MODMODE)json_integer_value(json_object_get(inportJ, "modMode"));
			inputXBipolar[inputIndex] = json_boolean_value(json_object_get(inportJ, "inputXBipolar"));
			inputYBipolar[inputIndex] = json_boolean_value(json_object_get(inportJ, "inputYBipolar"));
			outputMode[inputIndex] = (OUTPUTMODE)json_integer_value(json_object_get(inportJ, "outputMode"));
		}

		json_t* mixportsJ = json_object_get(rootJ, "mixports");
		json_t* mixportJ;
		size_t mixputIndex;
		json_array_foreach(mixportsJ, mixputIndex, mixportJ) {
			mixportXBipolar[mixputIndex] = json_boolean_value(json_object_get(mixportJ, "mixportXBipolar"));
			mixportYBipolar[mixputIndex] = json_boolean_value(json_object_get(mixportJ, "mixportYBipolar"));
			Seq::dataFromJson(mixportJ, mixputIndex);
		}

		inportsUsed = json_integer_value(json_object_get(rootJ, "inportsUsed"));
		mixportsUsed = json_integer_value(json_object_get(rootJ, "mixportsUsed"));
	}
};


// Context menus

template <typename MODULE>
struct InputXMenuItem : MenuItem {
	InputXMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct InputXBipolarItem : MenuItem {
		MODULE* module;
		int id;

		void onAction(const event::Action& e) override {
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


template <typename MODULE>
struct InputYMenuItem : MenuItem {
	InputYMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct InputYBipolarItem : MenuItem {
		MODULE* module;
		int id;

		void onAction(const event::Action& e) override {
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


template <typename MODULE>
struct ModModeMenuItem : MenuItem {
	ModModeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct ModeModeItem : MenuItem {
		MODULE* module;
		MODMODE modMode;
		int id;
		
		void onAction(const event::Action& e) override {
			module->modMode[id] = modMode;
		}

		void step() override {
			rightText = module->modMode[id] == modMode ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Modulation target"));
		menu->addChild(construct<ModeModeItem>(&MenuItem::text, "Radius", &ModeModeItem::module, module, &ModeModeItem::id, id, &ModeModeItem::modMode, MODMODE::RADIUS));
		menu->addChild(construct<ModeModeItem>(&MenuItem::text, "Amount", &ModeModeItem::module, module, &ModeModeItem::id, id, &ModeModeItem::modMode, MODMODE::AMOUNT));
		menu->addChild(construct<ModeModeItem>(&MenuItem::text, "Offset x-pos", &ModeModeItem::module, module, &ModeModeItem::id, id, &ModeModeItem::modMode, MODMODE::OFFSET_X));
		menu->addChild(construct<ModeModeItem>(&MenuItem::text, "Offset y-pos", &ModeModeItem::module, module, &ModeModeItem::id, id, &ModeModeItem::modMode, MODMODE::OFFSET_Y));
		menu->addChild(construct<ModeModeItem>(&MenuItem::text, "Random walk", &ModeModeItem::module, module, &ModeModeItem::id, id, &ModeModeItem::modMode, MODMODE::WALK));
		return menu;
	}
};

template <typename MODULE>
struct OutputModeMenuItem : MenuItem {
	OutputModeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct OutputModeItem : MenuItem {
		MODULE* module;
		OUTPUTMODE outputMode;
		int id;
		
		void onAction(const event::Action& e) override {
			module->outputMode[id] = outputMode;
		}

		void step() override {
			rightText = module->outputMode[id] == outputMode ? "✔" : "";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Mix mode"));
		menu->addChild(construct<OutputModeItem>(&MenuItem::text, "Scale", &OutputModeItem::module, module, &OutputModeItem::id, id, &OutputModeItem::outputMode, OUTPUTMODE::SCALE));
		menu->addChild(construct<OutputModeItem>(&MenuItem::text, "Limit", &OutputModeItem::module, module, &OutputModeItem::id, id, &OutputModeItem::outputMode, OUTPUTMODE::LIMIT));
		menu->addChild(construct<OutputModeItem>(&MenuItem::text, "Clip 0..10V", &OutputModeItem::module, module, &OutputModeItem::id, id, &OutputModeItem::outputMode, OUTPUTMODE::CLIP_UNI));
		menu->addChild(construct<OutputModeItem>(&MenuItem::text, "Clip -5..5V", &OutputModeItem::module, module, &OutputModeItem::id, id, &OutputModeItem::outputMode, OUTPUTMODE::CLIP_BI));
		menu->addChild(construct<OutputModeItem>(&MenuItem::text, "Fold 0..10V", &OutputModeItem::module, module, &OutputModeItem::id, id, &OutputModeItem::outputMode, OUTPUTMODE::FOLD_UNI));
		menu->addChild(construct<OutputModeItem>(&MenuItem::text, "Fold -5..5V", &OutputModeItem::module, module, &OutputModeItem::id, id, &OutputModeItem::outputMode, OUTPUTMODE::FOLD_BI));
		return menu;
	}
};


template <typename MODULE>
struct RadiusChangeAction : history::ModuleAction {
	int inputId;
	float oldValue;
	float newValue;

	RadiusChangeAction() {
		name = "stoermelder ARENA radius change";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->radius[inputId] = oldValue;
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->radius[inputId] = newValue;
	}
};

template <typename MODULE>
struct RadiusSlider : ui::Slider {
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
	RadiusChangeAction<MODULE>* h;

	RadiusSlider(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		quantity = new RadiusQuantity(module, id);
	}
	~RadiusSlider() {
		delete quantity;
	}

	void onDragStart(const event::DragStart& e) override {
		// history
		h = new RadiusChangeAction<MODULE>;
		h->moduleId = module->id;
		h->inputId = id;
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
struct AmountChangeAction : history::ModuleAction {
	int inputId;
	float oldValue;
	float newValue;

	AmountChangeAction() {
		name = "stoermelder ARENA amount change";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->amount[inputId] = oldValue;
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->amount[inputId] = newValue;
	}
};

template <typename MODULE>
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

	MODULE* module;
	int id;
	AmountChangeAction<MODULE>* h;

	AmountSlider(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		quantity = new AmountQuantity(module, id);
	}
	~AmountSlider() {
		delete quantity;
	}

	void onDragStart(const event::DragStart& e) override {
		// history
		h = new AmountChangeAction<MODULE>;
		h->moduleId = module->id;
		h->inputId = id;
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


struct XYChangeAction : history::ModuleAction {
	int paramXId, paramYId;
	float oldX, oldY;
	float newX, newY;

	XYChangeAction() {
		name = "stoermelder ARENA x/y-change";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		mw->module->params[paramXId].setValue(oldX);
		mw->module->params[paramYId].setValue(oldY);
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		mw->module->params[paramXId].setValue(newX);
		mw->module->params[paramYId].setValue(newY);
	}
};


template <typename MODULE>
struct MixportXMenuItem : MenuItem {
	MixportXMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct MixportXBipolarItem : MenuItem {
		MODULE* module;
		int id;

		void onAction(const event::Action& e) override {
			module->mixportXBipolar[id] ^= true;
		}

		void step() override {
			rightText = module->mixportXBipolar[id] ? "-5V..5V" : "0V..10V";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<MixportXBipolarItem>(&MenuItem::text, "Voltage", &MixportXBipolarItem::module, module, &MixportXBipolarItem::id, id));
		return menu;
	}
};

template <typename MODULE>
struct MixportYMenuItem : MenuItem {
	MixportYMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct MixportYBipolarItem : MenuItem {
		MODULE* module;
		int id;

		void onAction(const event::Action& e) override {
			module->mixportYBipolar[id] ^= true;
		}

		void step() override {
			rightText = module->mixportYBipolar[id] ? "-5V..5V" : "0V..10V";
			MenuItem::step();
		}
	};

	MODULE* module;
	int id;
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<MixportYBipolarItem>(&MenuItem::text, "Voltage", &MixportYBipolarItem::module, module, &MixportYBipolarItem::id, id));
		return menu;
	}
};


// Screen widgets

template <typename MODULE>
struct ScreenDragWidget : OpaqueWidget {
	const float radius = 10.f;
	const float fontsize = 13.0f;

	MODULE* module;	
	ParamQuantity* paramQuantityX;
	ParamQuantity* paramQuantityY;
	NVGcolor color = nvgRGB(0x66, 0x66, 0x0);
	NVGcolor textColor = nvgRGB(0x66, 0x66, 0x0);
	int id = -1;
	int type = -1;
	
	float circleA = 1.f;
	math::Vec dragPos;
	XYChangeAction* dragAction;

	ScreenDragWidget() {
		box.size = Vec(2 * radius, 2 * radius);
	}

	void step() override {
		float posX = paramQuantityX->getValue() * (parent->box.size.x - box.size.x);
		box.pos.x = posX;
		float posY = paramQuantityY->getValue() * (parent->box.size.y - box.size.y);
		box.pos.y = posY;
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (!module) return;

		if (layer == 1) {
			Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);

			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

			if (module->selectionTest(type, id)) {
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
				module->selectionSet(type, id);
				e.consume(this);
			}
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				module->selectionSet(type, id);
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
		dragAction = new XYChangeAction;
		dragAction->moduleId = module->id;
		dragAction->paramXId = paramQuantityX->paramId;
		dragAction->paramYId = paramQuantityY->paramId;
		dragAction->oldX = paramQuantityX->getValue();
		dragAction->oldY = paramQuantityY->getValue();
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		dragAction->newX = paramQuantityX->getValue();
		dragAction->newY = paramQuantityY->getValue();
		APP->history->push(dragAction);
		dragAction = NULL;
	}

	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		math::Vec pos = APP->scene->rack->getMousePos().minus(dragPos);
		float x = pos.x / (parent->box.size.x - box.size.x);
		paramQuantityX->setValue(std::max(0.f, std::min(1.f, x)));
		float y = pos.y / (parent->box.size.y - box.size.y);
		paramQuantityY->setValue(std::max(0.f, std::min(1.f, y)));

		OpaqueWidget::onDragMove(e);
	}

	virtual void createContextMenu() {}
};


template <typename MODULE>
struct ScreenInportDragWidget : ScreenDragWidget<MODULE> {
	typedef ScreenDragWidget<MODULE> AW;

	ScreenInportDragWidget() {
		AW::color = color::WHITE;
		AW::type = 0;
	}

	void step() override {
		AW::circleA = AW::module->amount[AW::id];
		AW::step();
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (AW::id + 1 > AW::module->inportsUsed) return;

			if (AW::module->selectionTest(AW::type, AW::id)) {
				// Draw outer circle and fill
				Vec c = Vec(AW::box.size.x / 2.f, AW::box.size.y / 2.f);
				Rect b = Rect(AW::box.pos.mult(-1), AW::parent->box.size);
				nvgSave(args.vg);
				nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
				float sizeX = std::max(0.f, (AW::parent->box.size.x - 2 * AW::radius) * AW::module->radius[AW::id] - AW::radius);
				float sizeY = std::max(0.f, (AW::parent->box.size.y - 2 * AW::radius) * AW::module->radius[AW::id] - AW::radius);
				nvgBeginPath(args.vg);
				nvgEllipse(args.vg, c.x, c.y, sizeX, sizeY);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgStrokeColor(args.vg, color::mult(AW::color, 0.7f));
				nvgStrokeWidth(args.vg, 0.6f);
				nvgStroke(args.vg);
				nvgFillColor(args.vg, color::mult(AW::color, 0.1f));
				nvgFill(args.vg);
				nvgResetScissor(args.vg);
				nvgRestore(args.vg);

				AW::textColor = nvgRGBA(0, 16, 90, 200);
			}
			else {
				AW::textColor = AW::color;
			}
		}
		AW::drawLayer(args, layer);
	}

	void onButton(const event::Button& e) override {
		if (AW::id + 1 > AW::module->inportsUsed) return;
		ScreenDragWidget<MODULE>::onButton(e);
	}

	void createContextMenu() override {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Channel IN-%i", AW::id + 1).c_str()));

		AmountSlider<MODULE>* amountSlider = new AmountSlider<MODULE>(AW::module, AW::id);
		amountSlider->box.size.x = 200.0;
		menu->addChild(amountSlider);

		RadiusSlider<MODULE>* radiusSlider = new RadiusSlider<MODULE>(AW::module, AW::id);
		radiusSlider->box.size.x = 200.0;
		menu->addChild(radiusSlider);

		menu->addChild(construct<InputXMenuItem<MODULE>>(&MenuItem::text, "X-port", &InputXMenuItem<MODULE>::module, AW::module, &InputXMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<InputYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &InputYMenuItem<MODULE>::module, AW::module, &InputYMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<ModModeMenuItem<MODULE>>(&MenuItem::text, "MOD-port", &ModModeMenuItem<MODULE>::module, AW::module, &ModModeMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<OutputModeMenuItem<MODULE>>(&MenuItem::text, "OUT-port", &OutputModeMenuItem<MODULE>::module, AW::module, &OutputModeMenuItem<MODULE>::id, AW::id));
	}
};

template <typename MODULE>
struct ScreenMixportDragWidget : ScreenDragWidget<MODULE> {
	typedef ScreenDragWidget<MODULE> AW;

	ScreenMixportDragWidget() {
		AW::color = color::YELLOW;
		AW::type = 1;
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (AW::id + 1 > AW::module->mixportsUsed) return;
		if (layer == 1) {
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

			// Draw lines between inputs and mixputs
			Vec c = Vec(AW::box.size.x / 2.f, AW::box.size.y / 2.f);
			float sizeX = AW::parent->box.size.x;
			float sizeY = AW::parent->box.size.y;
			for (int i = 0; i < AW::module->inportsUsed; i++) {
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

			// Draw interpolated automation line if selected
			if (AW::module->selectionTest(AW::type, AW::id)) {
				float sizeX = AW::parent->box.size.x - AW::box.size.x;
				float sizeY = AW::parent->box.size.y - AW::box.size.y;
				Vec pos = AW::box.pos.mult(-1).plus(Vec(AW::radius, AW::radius));
				nvgBeginPath(args.vg);
				int segments = AW::module->seqLength(AW::id) * 5;
				float seg1 = 1.f / segments;
				for (int i = 0; i < segments; i++) {
					Vec p = AW::module->seqValue(AW::id, seg1 * i);
					if (i == 0)
						nvgMoveTo(args.vg, pos.x + sizeX * p.x, pos.y + sizeY * p.y);
					else
						nvgLineTo(args.vg, pos.x + sizeX * p.x, pos.y + sizeY * p.y);
				}
				nvgStrokeColor(args.vg, color::mult(AW::color, 0.4f));
				nvgLineCap(args.vg, NVG_ROUND);
				nvgMiterLimit(args.vg, 2.0);
				nvgStrokeWidth(args.vg, 1.0);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgStroke(args.vg);

				AW::textColor = nvgRGBA(0, 16, 90, 200);
			}
			else {
				AW::textColor = AW::color;
			}
		}
		AW::drawLayer(args, layer);
	}

	void onButton(const event::Button& e) override {
		if (AW::id + 1 > AW::module->mixportsUsed) return;
		AW::onButton(e);
	}

	void createContextMenu() override {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Channel MIX-%i", AW::id + 1)));
		menu->addChild(new MenuSeparator());
		menu->addChild(XySeqSlotMenuItem(AW::module, AW::id));
		menu->addChild(XySeqInterpolateMenuItem(AW::module, AW::id));
		menu->addChild(XySeqTriggerMenuItem(AW::module, AW::id));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MixportXMenuItem<MODULE>>(&MenuItem::text, "X-port", &MixportXMenuItem<MODULE>::module, AW::module, &MixportXMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<MixportYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &MixportYMenuItem<MODULE>::module, AW::module, &MixportYMenuItem<MODULE>::id, AW::id));
	}
};


template <typename MODULE>
struct ScreenWidget : OpaqueWidget {
	MODULE* module;

	ScreenWidget(MODULE* module, int inParamIdX, int inParamIdY, int mixParamIdX, int mixParamIdY) {
		this->module = module;
		if (module) {
			for (int i = 0; i < module->numInports; i++) {
				ScreenInportDragWidget<MODULE>* w = new ScreenInportDragWidget<MODULE>;
				w->module = module;
				w->paramQuantityX = module->paramQuantities[inParamIdX + i];
				w->paramQuantityY = module->paramQuantities[inParamIdY + i];
				w->id = i;
				addChild(w);
			}
			for (int i = 0; i < module->numMixports; i++) {
				ScreenMixportDragWidget<MODULE>* w = new ScreenMixportDragWidget<MODULE>;
				w->module = module;
				w->paramQuantityX = module->paramQuantities[mixParamIdX + i];
				w->paramQuantityY = module->paramQuantities[mixParamIdY + i];
				w->id = i;
				addChild(w);
			}
		}
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
				module->selectionReset();
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
		menu->addChild(createMenuLabel("Arena"));

		struct InitItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				// history::ModuleChange
				history::ModuleChange* h = new history::ModuleChange;
				h->name = "stoermelder ARENA initialize";
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
				XYChangeAction* actions[module->numInports];
				for (int i = 0; i < module->numInports; i++) {
					actions[i] = new XYChangeAction;
					actions[i]->moduleId = module->id;
					actions[i]->paramXId = MODULE::IN_X_POS + i;
					actions[i]->paramYId = MODULE::IN_Y_POS + i;
					actions[i]->oldX = module->params[MODULE::IN_X_POS + i].getValue();
					actions[i]->oldY = module->params[MODULE::IN_Y_POS + i].getValue();
				}

				module->randomizeInputX();
				module->randomizeInputY();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->numInports; i++) {
					actions[i]->newX = module->params[MODULE::IN_X_POS + i].getValue();
					actions[i]->newY = module->params[MODULE::IN_Y_POS + i].getValue();
					complexAction->push(actions[i]);
				}

				complexAction->name = "stoermelder ARENA randomize IN x-pos & y-pos";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeXItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				XYChangeAction* actions[module->numInports];
				for (int i = 0; i < module->numInports; i++) {
					actions[i] = new XYChangeAction;
					actions[i]->moduleId = module->id;
					actions[i]->paramXId = MODULE::IN_X_POS + i;
					actions[i]->paramYId = MODULE::IN_Y_POS + i;
					actions[i]->oldX = module->params[MODULE::IN_X_POS + i].getValue();
					actions[i]->oldY = module->params[MODULE::IN_Y_POS + i].getValue();
				}

				module->randomizeInputX();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->numInports; i++) {
					actions[i]->newX = module->params[MODULE::IN_X_POS + i].getValue();
					actions[i]->newY = module->params[MODULE::IN_Y_POS + i].getValue();
					complexAction->push(actions[i]);
				}

				complexAction->name = "stoermelder ARENA randomize IN x-pos";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeYItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				XYChangeAction* actions[module->numInports];
				for (int i = 0; i < module->numInports; i++) {
					actions[i] = new XYChangeAction;
					actions[i]->moduleId = module->id;
					actions[i]->paramXId = MODULE::IN_X_POS + i;
					actions[i]->paramYId = MODULE::IN_Y_POS + i;
					actions[i]->oldX = module->params[MODULE::IN_X_POS + i].getValue();
					actions[i]->oldY = module->params[MODULE::IN_Y_POS + i].getValue();
				}

				module->randomizeInputY();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->numInports; i++) {
					actions[i]->newX = module->params[MODULE::IN_X_POS + i].getValue();
					actions[i]->newY = module->params[MODULE::IN_Y_POS + i].getValue();
					complexAction->push(actions[i]);
				}

				complexAction->name = "stoermelder ARENA randomize IN y-pos";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeAmountItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				AmountChangeAction<MODULE>* actions[module->numInports];
				for (int i = 0; i < module->numInports; i++) {
					actions[i] = new AmountChangeAction<MODULE>;
					actions[i]->moduleId = module->id;
					actions[i]->inputId = i;
					actions[i]->oldValue = module->amount[i];
				}

				module->randomizeInputAmount();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->numInports; i++) {
					actions[i]->newValue = module->amount[i];
					complexAction->push(actions[i]);
				}

				complexAction->name = "stoermelder ARENA randomize IN amount";
				APP->history->push(complexAction);
			}
		};

		struct RandomizeRadiusItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				RadiusChangeAction<MODULE>* actions[module->numInports];
				for (int i = 0; i < module->numInports; i++) {
					actions[i] = new RadiusChangeAction<MODULE>;
					actions[i]->moduleId = module->id;
					actions[i]->inputId = i;
					actions[i]->oldValue = module->radius[i];
				}

				module->randomizeInputRadius();

				history::ComplexAction* complexAction = new history::ComplexAction;
				for (int i = 0; i < module->numInports; i++) {
					actions[i]->newValue = module->radius[i];
					complexAction->push(actions[i]);
				}

				complexAction->name = "stoermelder ARENA randomize IN radius";
				APP->history->push(complexAction);
			}
		};

		struct NumInportsMenuItem : MenuItem {
			NumInportsMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct NumInportsItem : MenuItem {
				MODULE* module;
				int inportsUsed;
				
				void onAction(const event::Action& e) override {
					module->inportsUsed = inportsUsed;
				}

				void step() override {
					rightText = module->inportsUsed == inportsUsed ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (int i = 0; i < module->numInports; i++) {
					menu->addChild(construct<NumInportsItem>(&MenuItem::text, string::f("%i", i + 1), &NumInportsItem::module, module, &NumInportsItem::inportsUsed, i + 1));
				}
				return menu;
			}
		};

		struct NumMixportsMenuItem : MenuItem {
			NumMixportsMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct NumMixportsItem : MenuItem {
				MODULE* module;
				int mixportsUsed;
				
				void onAction(const event::Action& e) override {
					module->mixportsUsed = mixportsUsed;
				}

				void step() override {
					rightText = module->mixportsUsed == mixportsUsed ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (int i = 0; i < module->numMixports; i++) {
					menu->addChild(construct<NumMixportsItem>(&MenuItem::text, string::f("%i", i + 1), &NumMixportsItem::module, module, &NumMixportsItem::mixportsUsed, i + 1));
				}
				return menu;
			}
		};

		menu->addChild(construct<InitItem>(&MenuItem::text, "Initialize", &InitItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<RandomizeXYItem>(&MenuItem::text, "Radomize IN x-pos & y-pos", &RandomizeXYItem::module, module));
		menu->addChild(construct<RandomizeXItem>(&MenuItem::text, "Radomize IN x-pos", &RandomizeXItem::module, module));
		menu->addChild(construct<RandomizeYItem>(&MenuItem::text, "Radomize IN y-pos", &RandomizeYItem::module, module));
		menu->addChild(construct<RandomizeAmountItem>(&MenuItem::text, "Radomize IN amount", &RandomizeAmountItem::module, module));
		menu->addChild(construct<RandomizeRadiusItem>(&MenuItem::text, "Radomize IN radius", &RandomizeRadiusItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<NumInportsMenuItem>(&MenuItem::text, "Number of IN-ports", &NumInportsMenuItem::module, module));
		menu->addChild(construct<NumMixportsMenuItem>(&MenuItem::text, "Number of MIX-ports", &NumMixportsMenuItem::module, module));
	}
};


template <typename MODULE>
struct OpLedDisplay : StoermelderLedDisplay {
	MODULE* module;
	int id;

	void step() override {
		if (module) {
			if (id + 1 > module->inportsUsed) {
				text = "";
				return;
			}
			switch (module->modMode[id]) {
				case MODMODE::RADIUS:
					text = "RAD"; break;
				case MODMODE::AMOUNT:
					text = "AMT"; break;
				case MODMODE::OFFSET_X:
					text = "O-X"; break;
				case MODMODE::OFFSET_Y:
					text = "O-Y"; break;
				case MODMODE::WALK:
					text = "WLK"; break;
			}
		}
		else {
			text = "-X-";
		}
		StoermelderLedDisplay::step();
	}

	void onButton(const event::Button& e) override {
		if (id + 1 > module->inportsUsed) return;
		if (e.button == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		StoermelderLedDisplay::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Channel IN-%i", id + 1)));

		AmountSlider<MODULE>* amountSlider = new AmountSlider<MODULE>(module, id);
		amountSlider->box.size.x = 200.0;
		menu->addChild(amountSlider);

		RadiusSlider<MODULE>* radiusSlider = new RadiusSlider<MODULE>(module, id);
		radiusSlider->box.size.x = 200.0;
		menu->addChild(radiusSlider);

		menu->addChild(construct<InputXMenuItem<MODULE>>(&MenuItem::text, "X-port", &InputXMenuItem<MODULE>::module, module, &InputXMenuItem<MODULE>::id, id));
		menu->addChild(construct<InputYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &InputYMenuItem<MODULE>::module, module, &InputYMenuItem<MODULE>::id, id));
		menu->addChild(construct<ModModeMenuItem<MODULE>>(&MenuItem::text, "MOD-port", &ModModeMenuItem<MODULE>::module, module, &ModModeMenuItem<MODULE>::id, id));
		menu->addChild(construct<OutputModeMenuItem<MODULE>>(&MenuItem::text, "OUT-port", &OutputModeMenuItem<MODULE>::module, module, &OutputModeMenuItem<MODULE>::id, id));
	}
};


struct ArenaXySeqLedDisplay : XySeqLedDisplay<ArenaModule<8, 4>> {
	typedef ArenaModule<8, 4> MODULE;
	
	std::string getPortName() override {
		return string::f("Channel MIX-%i", id + 1);
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MixportXMenuItem<MODULE>>(&MenuItem::text, "X-port", &MixportXMenuItem<MODULE>::module, module, &MixportXMenuItem<MODULE>::id, id));
		menu->addChild(construct<MixportYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &MixportYMenuItem<MODULE>::module, module, &MixportYMenuItem<MODULE>::id, id));
	}
};


// Module widget

struct DummyMapButton : ParamWidget {
	DummyMapButton() {
		this->box.size = Vec(5.f, 5.f);
	}
};

template <typename MODULE, typename LIGHT>
struct ClickableLight : MediumLight<LIGHT> {
	int id;
	int type;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			MODULE* m = dynamic_cast<MODULE*>(MediumLight<LIGHT>::module);
			if (m->selectionTest(type, id))
				m->selectionReset();
			else
				m->selectionSet(type, id);
		}
		MediumLight<LIGHT>::onButton(e);
	}
};


struct ArenaWidget : ThemedModuleWidget<ArenaModule<8, 4>> {
	static const int IN_PORTS = 8;
	static const int MIX_PORTS = 4;
	typedef ArenaModule<IN_PORTS, MIX_PORTS> MODULE;
	MODULE* module;

	ArenaWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Arena") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < IN_PORTS; i++) {
			float xs[] = { 24.1f, 604.7f };
			float x = xs[i >= IN_PORTS / 2] + (i % (IN_PORTS / 2)) * 30.433f;
			addInput(createInputCentered<StoermelderPort>(Vec(x, 61.1f), module, MODULE::IN + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 96.2f), module, MODULE::IN_X_INPUT + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 130.7f), module, MODULE::IN_X_PARAM + i));
			addParam(createParamCentered<DummyMapButton>(Vec(x, 115.3f), module, MODULE::IN_X_POS + i));
			ClickableLight<MODULE, WhiteLight>* l = createLightCentered<ClickableLight<MODULE, WhiteLight>>(Vec(x, 147.6f), module, MODULE::IN_SEL_LIGHT + i);
			l->id = i;
			l->type = 0;
			addChild(l);
			addParam(createParamCentered<DummyMapButton>(Vec(x, 179.8f), module, MODULE::IN_Y_POS + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 164.4f), module, MODULE::IN_Y_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 198.9f), module, MODULE::IN_Y_INPUT + i));

			OpLedDisplay<MODULE>* arenaOpDisplay = createWidgetCentered<OpLedDisplay<MODULE>>(Vec(x, 227.0f));
			arenaOpDisplay->module = module;
			arenaOpDisplay->id = i;
			addChild(arenaOpDisplay);

			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 282.5f), module, MODULE::MOD_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 255.1f), module, MODULE::MOD_INPUT + i));

			addOutput(createOutputCentered<StoermelderPort>(Vec(x, 327.7f), module, MODULE::OUT_OUTPUT + i));
		}

		ScreenWidget<MODULE>* screenWidget = new ScreenWidget<MODULE>(module, MODULE::IN_X_POS, MODULE::IN_Y_POS, MODULE::MIX_X_POS, MODULE::MIX_Y_POS);
		screenWidget->box.pos = Vec(213.2f, 42.1f);
		screenWidget->box.size = Vec(293.6f, 296.0f);
		addChild(screenWidget);

		XySeqEditWidget<MODULE>* seqEditWidget = new XySeqEditWidget<MODULE>(module, MODULE::MIX_X_POS, MODULE::MIX_Y_POS);
		seqEditWidget->box.pos = screenWidget->box.pos;
		seqEditWidget->box.size = screenWidget->box.size;
		addChild(seqEditWidget);

		for (int i = 0; i < MIX_PORTS; i++) {
			float xs[] = { 154.3f, 534.9f };
			float x = xs[i >= MIX_PORTS / 2] + (i % (MIX_PORTS / 2)) * 30.433f;
			addParam(createParamCentered<StoermelderSmallKnob>(Vec(x, 61.1f), module, MODULE::MIX_VOL_PARAM + i));

			addInput(createInputCentered<StoermelderPort>(Vec(x, 96.2f), module, MODULE::MIX_X_INPUT + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 130.7f), module, MODULE::MIX_X_PARAM + i));
			addParam(createParamCentered<DummyMapButton>(Vec(x, 115.3f), module, MODULE::MIX_X_POS + i));
			ClickableLight<MODULE, YellowLight>* l1 = createLightCentered<ClickableLight<MODULE, YellowLight>>(Vec(x, 147.6f), module, MODULE::MIX_SEL_LIGHT + i);
			l1->id = i;
			l1->type = 1;
			addChild(l1);
			addParam(createParamCentered<DummyMapButton>(Vec(x, 179.8f), module, MODULE::MIX_Y_POS + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 164.4f), module, MODULE::MIX_Y_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 198.9f), module, MODULE::MIX_Y_INPUT + i));

			addOutput(createOutputCentered<StoermelderPort>(Vec(x, 327.7f), module, MODULE::MIX_OUTPUT + i));

			addInput(createInputCentered<StoermelderPort>(Vec(x, 255.6f), module, MODULE::SEQ_INPUT + i));
			ArenaXySeqLedDisplay* arenaSeqDisplay1 = createWidgetCentered<ArenaXySeqLedDisplay>(Vec(x, 227.0f));
			arenaSeqDisplay1->module = module;
			arenaSeqDisplay1->id = i;
			addChild(arenaSeqDisplay1);
			addInput(createInputCentered<StoermelderPort>(Vec(x, 287.8f), module, MODULE::SEQ_PH_INPUT + i));
		}
	}
};

} // namespace Arena
} // namespace StoermelderPackOne

Model* modelArena = createModel<StoermelderPackOne::Arena::ArenaModule<8, 4>, StoermelderPackOne::Arena::ArenaWidget>("Arena");