#include "plugin.hpp"
#include "components/Knobs.hpp"
#include "components/LedTextDisplay.hpp"
#include "components/XySeqWidget.hpp"
#include "components/XyScreenWidget.hpp"
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


template <uint8_t IN_PORTS, uint8_t MIX_PORTS>
struct ArenaModule : Module, XyScreenModule<IN_PORTS>, XySeqModule<MIX_PORTS> {
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

	typedef XyScreenModule<IN_PORTS> Sc;
	typedef XySeqModule<MIX_PORTS> Seq;

	const uint8_t numInports = IN_PORTS;
	const uint8_t numMixports = MIX_PORTS;

	/** [Stored to JSON] */
	int panelTheme = 0;

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

	float mixUiX[MIX_PORTS];
	dsp::ExponentialFilter mixXfilter[MIX_PORTS];
	float mixUiY[MIX_PORTS];
	dsp::ExponentialFilter mixYfilter[MIX_PORTS];

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
			configParam<XyScreenParamQuantity>(IN_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (IN_PORTS - 1)), string::f("Channel IN-%i x-pos", i + 1));
			configParam<XyScreenParamQuantity>(IN_Y_POS + i, 0.0f, 1.0f, 0.1f, string::f("Channel IN-%i y-pos", i + 1));
			configParam(IN_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i x-pos attenuverter", i + 1), "x");
			configParam(IN_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i y-pos attenuverter", i + 1), "x");
			configParam(MOD_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i Mod attenuverter", i + 1), "x");
		}
		// mix-ports
		for (int i = 0; i < MIX_PORTS; i++) {
			configInput(MIX_X_INPUT + i, string::f("Channel MIX-%i x-pos", i + 1));
			configInput(MIX_Y_INPUT + i, string::f("Channel MIX-%i y-pos", i + 1));
			configInput(SEQ_INPUT + i, string::f("Channel MIX-%i sequence select", i + 1));
			configInput(SEQ_PH_INPUT + i, string::f("Channel MIX-%i sequence phase", i + 1));
			configOutput(MIX_OUTPUT + i, string::f("Channel MIX-%i", i + 1));
			configParam(MIX_VOL_PARAM + i, 0.0f, 2.0f, 1.0f, string::f("Channel MIX-%i volume", i + 1));
			configParam<XyScreenParamQuantity>(MIX_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (MIX_PORTS - 1)), string::f("Channel MIX-%i x-pos", i + 1));
			configParam<XyScreenParamQuantity>(MIX_Y_POS + i, 0.0f, 1.0f, 0.9f, string::f("Channel MIX-%i y-pos", i + 1));
			configParam(MIX_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel MIX-%i x-pos attenuverter", i + 1), "x");
			configParam(MIX_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel MIX-%i y-pos attenuverter", i + 1), "x");
		}
		onReset();
		lightDivider.setDivision(512);
	}

	void onReset() override {
		Sc::screenSelectionReset();
		init();
		for (size_t i = 0; i < IN_PORTS; i++) {
			modMode[i] = MODMODE::RADIUS;
			inputXBipolar[i] = false;
			inputYBipolar[i] = false;
			outputMode[i] = OUTPUTMODE::SCALE;
		}
		for (size_t i = 0; i < MIX_PORTS; i++) {
			mixportXBipolar[i] = false;
			mixportYBipolar[i] = false;
		}
		Sc::screenReset();
		Seq::seqReset();
		Module::onReset();
	}

	void onRandomize() override {
		Sc::screenRandAmount();
		Sc::screenRandRadius();
		Sc::screenRandX();
		Sc::screenRandY();
		Module::onRandomize();
	}

	void init() override {
		for (size_t i = 0; i < MIX_PORTS; i++) {
			screenItemImmediate(1, i, paramQuantities[MIX_X_POS + i]->getDefaultValue(), paramQuantities[MIX_Y_POS + i]->getDefaultValue());
			mixXfilter[i].setTau(0.05f);
			mixYfilter[i].setTau(0.05f);
		}
		Sc::screenInit();
		Seq::seqInit();
	}

	void process(const ProcessArgs& args) override {
		float inNorm[IN_PORTS] = {0.f};
		Sc::screenProcess(args.sampleTime);

		for (uint8_t j = 0; j < inportsUsed; j++) {
			offsetX[j] = 0.f;
			offsetY[j] = 0.f;
			switch (modMode[j]) {
				case MODMODE::RADIUS: {
					if (inputs[MOD_INPUT + j].isConnected()) {
						Sc::radius[j] = getOpInput(j);
					}
					break;
				}
				case MODMODE::AMOUNT: {
					if (inputs[MOD_INPUT + j].isConnected()) {
						Sc::amount[j] = getOpInput(j);
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
				sd *= Sc::amount[j];
				inNorm[j] = sd;
			}
		}

		processItem(args.sampleTime);

		float outNorm[IN_PORTS] = {0.f};
		for (uint8_t i = 0; i < mixportsUsed; i++) {
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

				Vec inVec = Vec(inX, inY);
				dist[i][j] = inVec.minus(mixVec).norm();

				float r = Sc::radius[j];
				if (inputs[IN + j].isConnected() && dist[i][j] < r) {
					float s = std::min(1.0f, (r - dist[i][j]) / r * 1.1f);
					outNorm[j] += s;
					s *= inNorm[j];
					mix += s;
				}
			}

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
				lights[IN_SEL_LIGHT + i].setBrightness(Sc::selectedType == 0 && Sc::selectedId == i);
			}
			for (int i = 0; i < MIX_PORTS; i++) {
				lights[MIX_SEL_LIGHT + i].setBrightness(Sc::selectedType == 1 && Sc::selectedId == i);
			}
		}
	}

	void processItem(float sampleTime) {
		for (uint8_t i = 0; i < MIX_PORTS; i++) {
			XyScreenParamQuantity* px = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[MIX_X_POS + i]);
			if (!px->hasHandle) {
				px->getParam()->setValue(mixXfilter[i].process(sampleTime, mixUiX[i]));
			}
			else {
				mixXfilter[id].out = mixUiX[id] = px->getParam()->getValue();
			}
			XyScreenParamQuantity* py = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[MIX_Y_POS + i]);
			if (!py->hasHandle) {
				py->getParam()->setValue(mixYfilter[i].process(sampleTime, mixUiY[i]));
			}
			else {
				mixYfilter[i].out = mixUiY[i] = py->getParam()->getValue();
			}
		}
	}

	inline float getOpInput(int j) {
		float v = inputs[MOD_INPUT + j].isConnected() ? inputs[MOD_INPUT + j].getVoltage() : 10.f;
		v *= params[MOD_PARAM + j].getValue();
		v = clamp(v / 10.f, -1.f, 1.f);
		return v;
	}

	bool seqPortUsed(int port) override {
		return port + 1 > mixportsUsed;
	}

	engine::ParamQuantity* screenXpq(uint8_t type, uint8_t id) override {
		if (type == 0)
			return paramQuantities[IN_X_POS + id];
		else
			return paramQuantities[MIX_X_POS + id];
	}

	engine::ParamQuantity* screenYpq(uint8_t type, uint8_t id) override {
		if (type == 0)
			return paramQuantities[IN_Y_POS + id];
		else
			return paramQuantities[MIX_Y_POS + id];
	}

	uint8_t screenItemCount(uint8_t type = 0) override {
		 return type == 0 ? IN_PORTS : MIX_PORTS;
	}

	inline void screenItemFiltered(uint8_t type, uint8_t id, float x, float y) override {
		if (type == 1) {
			mixUiX[id] = x;
			mixUiY[id] = y;
		}
	}

	inline void screenItemImmediate(uint8_t type, uint8_t id, float x, float y) override {
		if (type == 1) {
			paramQuantities[MIX_X_POS + id]->getParam()->setValue(x);
			mixXfilter[id].out = mixUiX[id] = x;
			paramQuantities[MIX_X_POS + id]->getParam()->setValue(y);
			mixYfilter[id].out = mixUiY[id] = y;
		}
	}


	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* inportsJ = json_array();
		for (uint8_t i = 0; i < IN_PORTS; i++) {
			json_t* inportJ = json_object();
			Sc::dataToJson(inportJ, i);
			json_object_set_new(inportJ, "modMode", json_integer(modMode[i]));
			json_object_set_new(inportJ, "inputXBipolar", json_boolean(inputXBipolar[i]));
			json_object_set_new(inportJ, "inputYBipolar", json_boolean(inputYBipolar[i]));
			json_object_set_new(inportJ, "outputMode", json_integer(outputMode[i]));
			json_array_append_new(inportsJ, inportJ);
		}
		json_object_set_new(rootJ, "inports", inportsJ);

		json_t* mixportsJ = json_array();
		for (uint8_t i = 0; i < MIX_PORTS; i++) {
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
		uint8_t inputIndex;
		json_array_foreach(inportsJ, inputIndex, inportJ) {
			Sc::dataFromJson(inportJ, inputIndex);
			modMode[inputIndex] = (MODMODE)json_integer_value(json_object_get(inportJ, "modMode"));
			inputXBipolar[inputIndex] = json_boolean_value(json_object_get(inportJ, "inputXBipolar"));
			inputYBipolar[inputIndex] = json_boolean_value(json_object_get(inportJ, "inputYBipolar"));
			outputMode[inputIndex] = (OUTPUTMODE)json_integer_value(json_object_get(inportJ, "outputMode"));
		}

		json_t* mixportsJ = json_object_get(rootJ, "mixports");
		json_t* mixportJ;
		uint8_t mixputIndex;
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
struct ArenaInputXMenuItem : MenuItem {
	ArenaInputXMenuItem() {
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
struct ArenaInputYMenuItem : MenuItem {
	ArenaInputYMenuItem() {
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
struct ArenaModModeMenuItem : MenuItem {
	ArenaModModeMenuItem() {
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
struct ArenaOutputModeMenuItem : MenuItem {
	ArenaOutputModeMenuItem() {
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
struct ArenaMixportXMenuItem : MenuItem {
	ArenaMixportXMenuItem() {
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
struct ArenaMixportYMenuItem : MenuItem {
	ArenaMixportYMenuItem() {
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
struct ArenaInportDragWidget : XyScreenDragWidget<MODULE> {
	typedef XyScreenDragWidget<MODULE> AW;

	ArenaInportDragWidget() {
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

			if (AW::module->screenSelectionTest(AW::type, AW::id)) {
				// Draw outer circle and fill
				Vec c = Vec(AW::box.size.x / 2.f, AW::box.size.y / 2.f);
				Rect b = Rect(AW::box.pos.mult(-1), AW::parent->box.size);
				nvgSave(args.vg);
				nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
				float sizeX = std::max(0.f, (AW::parent->box.size.x - 2 * AW::radius) * AW::module->screenRadius(AW::id) - AW::radius);
				float sizeY = std::max(0.f, (AW::parent->box.size.y - 2 * AW::radius) * AW::module->screenRadius(AW::id) - AW::radius);
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
		AW::onButton(e);
	}

 	std::string getItemName() override {
		return string::f("Channel IN-%i", AW::id + 1);
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(construct<ArenaInputXMenuItem<MODULE>>(&MenuItem::text, "X-port", &ArenaInputXMenuItem<MODULE>::module, AW::module, &ArenaInputXMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<ArenaInputYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &ArenaInputYMenuItem<MODULE>::module, AW::module, &ArenaInputYMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<ArenaModModeMenuItem<MODULE>>(&MenuItem::text, "MOD-port", &ArenaModModeMenuItem<MODULE>::module, AW::module, &ArenaModModeMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<ArenaOutputModeMenuItem<MODULE>>(&MenuItem::text, "OUT-port", &ArenaOutputModeMenuItem<MODULE>::module, AW::module, &ArenaOutputModeMenuItem<MODULE>::id, AW::id));
	}
};

template <typename MODULE>
struct ArenaMixportDragWidget : XyScreenDragWidget<MODULE> {
	typedef XyScreenDragWidget<MODULE> AW;

	ArenaMixportDragWidget() {
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
			if (AW::module->screenSelectionTest(AW::type, AW::id)) {
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

	void appendContextMenu(Menu* menu) override {
		menu->addChild(createMenuLabel(string::f("Channel MIX-%i", AW::id + 1)));
		menu->addChild(new MenuSeparator());
		menu->addChild(XySeqSlotMenuItem(AW::module, AW::id));
		menu->addChild(XySeqInterpolateMenuItem(AW::module, AW::id));
		menu->addChild(XySeqTriggerMenuItem(AW::module, AW::id));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ArenaMixportXMenuItem<MODULE>>(&MenuItem::text, "X-port", &ArenaMixportXMenuItem<MODULE>::module, AW::module, &ArenaMixportXMenuItem<MODULE>::id, AW::id));
		menu->addChild(construct<ArenaMixportYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &ArenaMixportYMenuItem<MODULE>::module, AW::module, &ArenaMixportYMenuItem<MODULE>::id, AW::id));
	}
};


template <typename MODULE>
struct ArenaScreenWidget : XyScreenWidget<MODULE> {
	ArenaScreenWidget(MODULE* module, int inParamIdX, int inParamIdY, int mixParamIdX, int mixParamIdY) : XyScreenWidget<MODULE>(module) {
		if (module) {
			for (uint8_t i = 0; i < module->numInports; i++) {
				ArenaInportDragWidget<MODULE>* w = new ArenaInportDragWidget<MODULE>;
				w->module = module;
				w->id = i;
				XyScreenWidget<MODULE>::addChild(w);
			}
			for (uint8_t i = 0; i < module->numMixports; i++) {
				ArenaMixportDragWidget<MODULE>* w = new ArenaMixportDragWidget<MODULE>;
				w->module = module;
				w->id = i;
				XyScreenWidget<MODULE>::addChild(w);
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		struct NumInportsMenuItem : MenuItem {
			NumInportsMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct NumInportsItem : MenuItem {
				MODULE* module;
				uint8_t inportsUsed;
				
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
				for (uint8_t i = 0; i < module->numInports; i++) {
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

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<NumInportsMenuItem>(&MenuItem::text, "Number of IN-ports", &NumInportsMenuItem::module, XyScreenWidget<MODULE>::module));
		menu->addChild(construct<NumMixportsMenuItem>(&MenuItem::text, "Number of MIX-ports", &NumMixportsMenuItem::module, XyScreenWidget<MODULE>::module));
	}
};


template <typename MODULE>
struct ArenaOpLedDisplay : StoermelderLedDisplay {
	MODULE* module;
	uint8_t id;

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

		XyScreenAmountSlider<MODULE>* amountSlider = new XyScreenAmountSlider<MODULE>(module, id);
		amountSlider->box.size.x = 200.0;
		menu->addChild(amountSlider);

		XyScreenRadiusSlider<MODULE>* radiusSlider = new XyScreenRadiusSlider<MODULE>(module, id);
		radiusSlider->box.size.x = 200.0;
		menu->addChild(radiusSlider);

		menu->addChild(construct<ArenaInputXMenuItem<MODULE>>(&MenuItem::text, "X-port", &ArenaInputXMenuItem<MODULE>::module, module, &ArenaInputXMenuItem<MODULE>::id, id));
		menu->addChild(construct<ArenaInputYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &ArenaInputYMenuItem<MODULE>::module, module, &ArenaInputYMenuItem<MODULE>::id, id));
		menu->addChild(construct<ArenaModModeMenuItem<MODULE>>(&MenuItem::text, "MOD-port", &ArenaModModeMenuItem<MODULE>::module, module, &ArenaModModeMenuItem<MODULE>::id, id));
		menu->addChild(construct<ArenaOutputModeMenuItem<MODULE>>(&MenuItem::text, "OUT-port", &ArenaOutputModeMenuItem<MODULE>::module, module, &ArenaOutputModeMenuItem<MODULE>::id, id));
	}
};


struct ArenaXySeqLedDisplay : XySeqLedDisplay<ArenaModule<8, 4>> {
	typedef ArenaModule<8, 4> MODULE;
	
	std::string getPortName() override {
		return string::f("Channel MIX-%i", id + 1);
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ArenaMixportXMenuItem<MODULE>>(&MenuItem::text, "X-port", &ArenaMixportXMenuItem<MODULE>::module, module, &ArenaMixportXMenuItem<MODULE>::id, id));
		menu->addChild(construct<ArenaMixportYMenuItem<MODULE>>(&MenuItem::text, "Y-port", &ArenaMixportYMenuItem<MODULE>::module, module, &ArenaMixportYMenuItem<MODULE>::id, id));
	}
};


// Module widget

template <typename MODULE, typename LIGHT>
struct ClickableLight : MediumLight<LIGHT> {
	uint8_t id;
	uint8_t type;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			MODULE* m = dynamic_cast<MODULE*>(MediumLight<LIGHT>::module);
			if (m->screenSelectionTest(type, id))
				m->screenSelectionReset();
			else
				m->screenSelectionSet(type, id);
		}
		MediumLight<LIGHT>::onButton(e);
	}
};


struct ArenaWidget : ThemedModuleWidget<ArenaModule<8, 4>> {
	static const uint8_t IN_PORTS = 8;
	static const uint8_t MIX_PORTS = 4;
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
			addParam(createParamCentered<XyScreenDummyMapButton>(Vec(x, 115.3f), module, MODULE::IN_X_POS + i));
			ClickableLight<MODULE, WhiteLight>* l = createLightCentered<ClickableLight<MODULE, WhiteLight>>(Vec(x, 147.6f), module, MODULE::IN_SEL_LIGHT + i);
			l->id = i;
			l->type = 0;
			addChild(l);
			addParam(createParamCentered<XyScreenDummyMapButton>(Vec(x, 179.8f), module, MODULE::IN_Y_POS + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 164.4f), module, MODULE::IN_Y_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 198.9f), module, MODULE::IN_Y_INPUT + i));

			ArenaOpLedDisplay<MODULE>* arenaOpDisplay = createWidgetCentered<ArenaOpLedDisplay<MODULE>>(Vec(x, 227.0f));
			arenaOpDisplay->module = module;
			arenaOpDisplay->id = i;
			addChild(arenaOpDisplay);

			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 282.5f), module, MODULE::MOD_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 255.1f), module, MODULE::MOD_INPUT + i));

			addOutput(createOutputCentered<StoermelderPort>(Vec(x, 327.7f), module, MODULE::OUT_OUTPUT + i));
		}

		ArenaScreenWidget<MODULE>* screenWidget = new ArenaScreenWidget<MODULE>(module, MODULE::IN_X_POS, MODULE::IN_Y_POS, MODULE::MIX_X_POS, MODULE::MIX_Y_POS);
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
			addParam(createParamCentered<XyScreenDummyMapButton>(Vec(x, 115.3f), module, MODULE::MIX_X_POS + i));
			ClickableLight<MODULE, YellowLight>* l1 = createLightCentered<ClickableLight<MODULE, YellowLight>>(Vec(x, 147.6f), module, MODULE::MIX_SEL_LIGHT + i);
			l1->id = i;
			l1->type = 1;
			addChild(l1);
			addParam(createParamCentered<XyScreenDummyMapButton>(Vec(x, 179.8f), module, MODULE::MIX_Y_POS + i));
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