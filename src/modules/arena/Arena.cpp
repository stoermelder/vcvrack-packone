#include "../../plugin.hpp"
#include "../../components/Knobs.hpp"
#include "../../components/LedTextDisplay.hpp"
#include "../../components/XySeqWidget.hpp"
#include "../../components/XyScreenWidget.hpp"
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
struct ArenaModule : Module, XyScreenModule<IN_PORTS>, XyScreenCursor, XySeqModule<MIX_PORTS> {
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

	float inputInX[IN_PORTS];
	float inputInY[IN_PORTS];

	float mixUiX[MIX_PORTS], mixInX[MIX_PORTS];
	dsp::ExponentialFilter mixXfilter[MIX_PORTS];
	float mixUiY[MIX_PORTS], mixInY[MIX_PORTS];
	dsp::ExponentialFilter mixYfilter[MIX_PORTS];

	ClockDividerEx lightDivider;

	ArenaModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// in-ports
		for (int i = 0; i < IN_PORTS; i++) {
			configInput(IN + i, string::f("Channel IN-%i", i + 1));
			inputInfos[IN + i]->description = "Monophonic audio source for this channel.";
			configInput(IN_X_INPUT + i, string::f("Channel IN-%i x-pos CV", i + 1));
			inputInfos[IN_X_INPUT + i]->description = "CV modulation of the channel's x-position on the XY grid (summed with the knob).";
			configInput(IN_Y_INPUT + i, string::f("Channel IN-%i y-pos CV", i + 1));
			inputInfos[IN_Y_INPUT + i]->description = "CV modulation of the channel's y-position on the XY grid (summed with the knob).";
			configInput(MOD_INPUT + i, string::f("Channel IN-%i modulation", i + 1));
			inputInfos[MOD_INPUT + i]->description = "Modulation signal for the channel; behavior depends on the Mod Mode selected on the context menu.";
			configOutput(OUT_OUTPUT + i, string::f("Channel IN-%i direct", i + 1));
			outputInfos[OUT_OUTPUT + i]->description = "Direct output of the channel's input audio.";
			configParam<XyScreenParamQuantity>(IN_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (IN_PORTS - 1)), string::f("Channel IN-%i x-pos", i + 1));
			paramQuantities[IN_X_POS + i]->description = "Position of this channel on the XY field's x-axis.";
			configParam<XyScreenParamQuantity>(IN_Y_POS + i, 0.0f, 1.0f, 0.1f, string::f("Channel IN-%i y-pos", i + 1));
			paramQuantities[IN_Y_POS + i]->description = "Position of this channel on the XY field's y-axis.";
			configParam(IN_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i x-pos attenuverter", i + 1), "x");
			paramQuantities[IN_X_PARAM + i]->description = "Attenuverter for the x-pos CV input (-1 inverts, 0 mutes, 1 passes through).";
			configParam(IN_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i y-pos attenuverter", i + 1), "x");
			paramQuantities[IN_Y_PARAM + i]->description = "Attenuverter for the y-pos CV input (-1 inverts, 0 mutes, 1 passes through).";
			configParam(MOD_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel IN-%i Mod attenuverter", i + 1), "x");
			paramQuantities[MOD_PARAM + i]->description = "Attenuverter for the modulation CV input (-1 inverts, 0 mutes, 1 passes through).";
		}
		// mix-ports
		for (int i = 0; i < MIX_PORTS; i++) {
			configInput(MIX_X_INPUT + i, string::f("Channel MIX-%i x-pos", i + 1));
			inputInfos[MIX_X_INPUT + i]->description = "CV modulation of this mix channel's x-position on the XY grid.";
			configInput(MIX_Y_INPUT + i, string::f("Channel MIX-%i y-pos", i + 1));
			inputInfos[MIX_Y_INPUT + i]->description = "CV modulation of this mix channel's y-position on the XY grid.";
			configInput(SEQ_INPUT + i, string::f("Channel MIX-%i sequence select", i + 1));
			inputInfos[SEQ_INPUT + i]->description = "Selects the active sequence step for this mix channel.";
			configInput(SEQ_PH_INPUT + i, string::f("Channel MIX-%i sequence phase", i + 1));
			inputInfos[SEQ_PH_INPUT + i]->description = "Phases the sequence of this mix channel by the input voltage.";
			configOutput(MIX_OUTPUT + i, string::f("Channel MIX-%i", i + 1));
			outputInfos[MIX_OUTPUT + i]->description = "Mix of all IN channels routed into this output's XY position on the field.";
			configParam(MIX_VOL_PARAM + i, 0.0f, 2.0f, 1.0f, string::f("Channel MIX-%i volume", i + 1));
			paramQuantities[MIX_VOL_PARAM + i]->description = "Output gain of this mix channel (0..2, default 1).";
			configParam<XyScreenParamQuantity>(MIX_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (MIX_PORTS - 1)), string::f("Channel MIX-%i x-pos", i + 1));
			paramQuantities[MIX_X_POS + i]->description = "Position of this output on the XY field's x-axis.";
			configParam<XyScreenParamQuantity>(MIX_Y_POS + i, 0.0f, 1.0f, 0.9f, string::f("Channel MIX-%i y-pos", i + 1));
			paramQuantities[MIX_Y_POS + i]->description = "Position of this output on the XY field's y-axis.";
			configParam(MIX_X_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel MIX-%i x-pos attenuverter", i + 1), "x");
			paramQuantities[MIX_X_PARAM + i]->description = "Attenuverter for the x-pos CV input (-1 inverts, 0 mutes, 1 passes through).";
			configParam(MIX_Y_PARAM + i, -1.f, 1.f, 0.f, string::f("Channel MIX-%i y-pos attenuverter", i + 1), "x");
			paramQuantities[MIX_Y_PARAM + i]->description = "Attenuverter for the y-pos CV input (-1 inverts, 0 mutes, 1 passes through).";
		}

		ResetEvent re;
		onReset(re);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onReset(const ResetEvent& e) override {
		Sc::selection = XyScreenSelection();
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
		Sc::resetNodes();
		Seq::seqReset();
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		Sc::nodes.randomizeAmountAll();
		Sc::nodes.randomizeRadiusAll();
		Sc::nodes.randomizeXAll();
		Sc::nodes.randomizeYAll();
		Module::onRandomize(e);
	}

	void init() {
		initExtra();
		Sc::initNodes();
		Seq::seqInit();
	}

	void process(const ProcessArgs& args) override {
		float inNorm[IN_PORTS] = {0.f};

		for (uint8_t j = 0; j < inportsUsed; j++) {
			XyScreenParamQuantity* px = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[IN_X_POS + j]);
			inputInX[j] = px->hasHandle ? px->getParam()->getValue() : Sc::nodes.getXFiltered(j, args.sampleTime);
			XyScreenParamQuantity* py = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[IN_Y_POS + j]);
			inputInY[j] = py->hasHandle ? py->getParam()->getValue() : Sc::nodes.getYFiltered(j, args.sampleTime);

			offsetX[j] = 0.f;
			offsetY[j] = 0.f;
			switch (modMode[j]) {
				case MODMODE::RADIUS: {
					Sc::nodes.setRadius(j, inputs[MOD_INPUT + j].isConnected() ? getModInput(j, 0.f, 1.f) : Sc::nodes.getRadiusFiltered(j, args.sampleTime));
					Sc::nodes.setAmount(j, Sc::nodes.getAmountFiltered(j, args.sampleTime));
					break;
				}
				case MODMODE::AMOUNT: {
					Sc::nodes.setRadius(j, Sc::nodes.getRadiusFiltered(j, args.sampleTime));
					Sc::nodes.setAmount(j, inputs[MOD_INPUT + j].isConnected() ? getModInput(j, 0.f, 1.f) : Sc::nodes.getAmountFiltered(j, args.sampleTime));
					break;
				}
				case MODMODE::OFFSET_X: {
					Sc::nodes.setRadius(j, Sc::nodes.getRadiusFiltered(j, args.sampleTime));
					Sc::nodes.setAmount(j, Sc::nodes.getAmountFiltered(j, args.sampleTime));
					offsetX[j] = getModInput(j);
					break;
				}
				case MODMODE::OFFSET_Y: {
					Sc::nodes.setRadius(j, Sc::nodes.getRadiusFiltered(j, args.sampleTime));
					Sc::nodes.setAmount(j, Sc::nodes.getAmountFiltered(j, args.sampleTime));
					offsetY[j] = getModInput(j);
					break;
				}
				case MODMODE::WALK: {
					Sc::nodes.setRadius(j, Sc::nodes.getRadiusFiltered(j, args.sampleTime));
					Sc::nodes.setAmount(j, Sc::nodes.getAmountFiltered(j, args.sampleTime));
					float v = getModInput(j);
					offsetX[j] = random::normal() / 2000.f * v;
					offsetY[j] = random::normal() / 2000.f * v;
					break;
				}
			}

			float x = inputInX[j]; // params[IN_X_POS + j].getValue();
			if (inputs[IN_X_INPUT + j].isConnected()) {
				float xd = inputs[IN_X_INPUT + j].getVoltage();
				xd *= params[IN_X_PARAM + j].getValue();
				xd += inputXBipolar[j] ? 5.f : 0.f;
				x = clamp(xd / 10.f, 0.f, 1.f);
			}
			x += offsetX[j];
			x = clamp(x, 0.f, 1.f);
			params[IN_X_POS + j].setValue(x);

			float y = inputInY[j]; // params[IN_Y_POS + j].getValue();
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
				sd *= Sc::getNodeAmountFinal(j);
				inNorm[j] = sd;
			}
		}

		float outNorm[IN_PORTS] = {0.f};
		for (uint8_t i = 0; i < mixportsUsed; i++) {
			XyScreenParamQuantity* px = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[MIX_X_POS + i]);
			if (!px->hasHandle) {
				mixInX[i] = mixXfilter[i].process(args.sampleTime, mixUiX[i]);
			}
			else {
				mixInX[i] = px->getParam()->getValue();
			}
			XyScreenParamQuantity* py = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[MIX_Y_POS + i]);
			if (!py->hasHandle) {
				mixInY[i] = mixYfilter[i].process(args.sampleTime, mixUiY[i]);
			}
			else {
				mixInY[i] = py->getParam()->getValue();
			}

			if (inputs[SEQ_INPUT + i].isConnected()) {
				Seq::seqProcess(inputs[SEQ_INPUT + i], i);
			}

			bool setX = false, setY = false;

			if (inputs[SEQ_PH_INPUT + i].isConnected()) {
				float v = clamp(inputs[SEQ_PH_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
				Vec d = Seq::seqValue(i, v);
				params[MIX_X_POS + i].setValue(d.x);
				setX = true;
				params[MIX_Y_POS + i].setValue(d.y);
				setY = true;
			}

			if (!setX && inputs[MIX_X_INPUT + i].isConnected()) {
				float x = inputs[MIX_X_INPUT + i].getVoltage() / 10.f;
				x *= params[MIX_X_PARAM + i].getValue();
				x += mixportXBipolar[i] ? 0.5f : 0.f;
				x = clamp(x, 0.f, 1.f);
				params[MIX_X_POS + i].setValue(x);
				setX = true;
			} 

			if (!setY && inputs[MIX_Y_INPUT + i].isConnected()) {
				float y = inputs[MIX_Y_INPUT + i].getVoltage() / 10.f;
				y *= params[MIX_Y_PARAM + i].getValue();
				y += mixportYBipolar[i] ? 0.5f : 0.f;
				y = clamp(y, 0.f, 1.f);
				params[MIX_Y_POS + i].setValue(y);
				setY = true;
			}

			if (!setX) {
				params[MIX_X_POS + i].setValue(mixInX[i]);
			}
			if (!setY) {
				params[MIX_Y_POS + i].setValue(mixInY[i]);
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

				float r = Sc::getNodeRadiusFinal(j);
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
						// Divided by the *active* mix count, not MIX_PORTS: each active
						// MIX-port contributes at most 1/n so the sum reaches 100% at most.
						v *= outNorm[j] / std::max<uint8_t>(1, mixportsUsed);
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
				lights[IN_SEL_LIGHT + i].setBrightness(Sc::selection.isNode(i));
			}
			for (int i = 0; i < MIX_PORTS; i++) {
				lights[MIX_SEL_LIGHT + i].setBrightness(Sc::selection.isCursor(i));
			}
		}
	}

	inline float getModInput(int j, float min = -1.f, float max = 1.f) {
		float v = inputs[MOD_INPUT + j].isConnected() ? inputs[MOD_INPUT + j].getVoltage() : 10.f;
		v *= params[MOD_PARAM + j].getValue();
		v = clamp(v / 10.f, min, max);
		return v;
	}

	/** XySeqModule: MIX-0 is reserved for the master sequence, never a per-port one. */
	bool seqPortUsed(int port) override {
		return port + 1 > mixportsUsed;
	}

	/** XyScreenModule: one-time setup for the MIX (cursor) ports, called from initNodes(). */
	void initExtra() override {
		for (size_t i = 0; i < MIX_PORTS; i++) {
			setCursorXyImmediate(i, paramQuantities[MIX_X_POS + i]->getDefaultValue(), paramQuantities[MIX_Y_POS + i]->getDefaultValue());
			mixXfilter[i].setTau(0.05f);
			mixYfilter[i].setTau(0.05f);
		}
	}

	/** XyScreenModule: how many of the IN_PORTS nodes are currently active. */
	inline uint8_t nodeCountActive() override {
		return inportsUsed;
	}

	/** XyScreenModule: the node (IN port) x-position param. */
	engine::ParamQuantity* getNodePqX(uint8_t id) override {
		return paramQuantities[IN_X_POS + id];
	}

	/** XyScreenModule: the node (IN port) y-position param. */
	engine::ParamQuantity* getNodePqY(uint8_t id) override {
		return paramQuantities[IN_Y_POS + id];
	}

	/** XyScreenCursor: the MIX ports. */
	uint8_t cursorCount() const override {
		return MIX_PORTS;
	}

	/** XyScreenCursor: how many of the MIX_PORTS cursors are currently active. */
	uint8_t cursorCountActive() const override {
		return mixportsUsed;
	}

	/** XyScreenCursor: the param-backed x-position a MIX cursor widget draws. */
	float getCursorXFinal(uint8_t id) const override {
		return paramQuantities[MIX_X_POS + id]->getParam()->getValue();
	}

	/** XyScreenCursor: the param-backed y-position a MIX cursor widget draws. */
	float getCursorYFinal(uint8_t id) const override {
		return paramQuantities[MIX_Y_POS + id]->getParam()->getValue();
	}

	/** XyScreenCursor: write a MIX port's position immediately (drag end, undo/redo).
	 * Out-of-range id is a silent no-op, matching XyScreenNodes's bounds
	 * checks and the rest of the codebase's convention for bad indices. */
	void setCursorXyImmediate(uint8_t id, float x, float y) override {
		if (id >= MIX_PORTS) return;
		paramQuantities[MIX_X_POS + id]->getParam()->setValue(x);
		mixXfilter[id].out = mixUiX[id] = x;
		paramQuantities[MIX_Y_POS + id]->getParam()->setValue(y);
		mixYfilter[id].out = mixUiY[id] = y;
	}

	/** XyScreenCursor: write a MIX port's position through the UI filter (live drag). */
	void setCursorXyFiltered(uint8_t id, float x, float y) override {
		if (id >= MIX_PORTS) return;
		mixUiX[id] = x;
		mixUiY[id] = y;
	}

	/** XyScreenModule: distance from a MIX cursor to an IN node, for the connector-line draw. */
	inline float getCursorToNodeDistance(uint8_t cursorId, uint8_t nodeId) override {
		return dist[cursorId][nodeId];
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* inportsJ = json_array();
		for (uint8_t i = 0; i < IN_PORTS; i++) {
			json_t* inportJ = json_object();
			json_object_set_new(inportJ, "modMode", json_integer(modMode[i]));
			json_object_set_new(inportJ, "inputXBipolar", json_boolean(inputXBipolar[i]));
			json_object_set_new(inportJ, "inputYBipolar", json_boolean(inputYBipolar[i]));
			json_object_set_new(inportJ, "outputMode", json_integer(outputMode[i]));
			Sc::nodes.dataToJson(inportJ, i);
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
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* inportsJ = json_object_get(rootJ, "inports");
		if (inportsJ) {
			// Bounded to the fixed-size destinations: hand-edited or corrupted
			// patches may contain more ports than the [IN_PORTS]/[MIX_PORTS]
			// members hold.
			size_t maxInports = std::min((size_t)IN_PORTS, json_array_size(inportsJ));
			for (size_t inputIndex = 0; inputIndex < maxInports; inputIndex++) {
				json_t* inportJ = json_array_get(inportsJ, inputIndex);
				json_t* modModeJ = json_object_get(inportJ, "modMode");
				if (modModeJ) modMode[inputIndex] = (MODMODE)json_integer_value(modModeJ);
				json_t* inputXBipolarJ = json_object_get(inportJ, "inputXBipolar");
				if (inputXBipolarJ) inputXBipolar[inputIndex] = json_boolean_value(inputXBipolarJ);
				json_t* inputYBipolarJ = json_object_get(inportJ, "inputYBipolar");
				if (inputYBipolarJ) inputYBipolar[inputIndex] = json_boolean_value(inputYBipolarJ);
				json_t* outputModeJ = json_object_get(inportJ, "outputMode");
				if (outputModeJ) outputMode[inputIndex] = (OUTPUTMODE)json_integer_value(outputModeJ);
				Sc::nodes.dataFromJson(inportJ, inputIndex);
			}
		}

		json_t* mixportsJ = json_object_get(rootJ, "mixports");
		if (mixportsJ) {
			size_t maxMixports = std::min((size_t)MIX_PORTS, json_array_size(mixportsJ));
			for (size_t mixputIndex = 0; mixputIndex < maxMixports; mixputIndex++) {
				json_t* mixportJ = json_array_get(mixportsJ, mixputIndex);
				json_t* mixportXBipolarJ = json_object_get(mixportJ, "mixportXBipolar");
				if (mixportXBipolarJ) mixportXBipolar[mixputIndex] = json_boolean_value(mixportXBipolarJ);
				json_t* mixportYBipolarJ = json_object_get(mixportJ, "mixportYBipolar");
				if (mixportYBipolarJ) mixportYBipolar[mixputIndex] = json_boolean_value(mixportYBipolarJ);
				Seq::dataFromJson(mixportJ, mixputIndex);
			}
		}

		json_t* inportsUsedJ = json_object_get(rootJ, "inportsUsed");
		if (inportsUsedJ) inportsUsed = json_integer_value(inportsUsedJ);
		json_t* mixportsUsedJ = json_object_get(rootJ, "mixportsUsed");
		if (mixportsUsedJ) mixportsUsed = json_integer_value(mixportsUsedJ);

		// Rack's own Module::fromJson() already restored MIX_X_POS/MIX_Y_POS via
		// paramsFromJson() (which runs before dataFromJson()). Without this, the
		// UI shadow (mixUiX/mixUiY) and filter (mixXfilter/mixYfilter) stay at
		// whatever the constructor left them at, and the first process() call
		// would overwrite the just-restored params with that stale shadow.
		for (uint8_t i = 0; i < MIX_PORTS; i++) {
			setCursorXyImmediate(i, paramQuantities[MIX_X_POS + i]->getValue(), paramQuantities[MIX_Y_POS + i]->getValue());
		}
	}
};


// Context menus

MenuItem* ArenaVoltageSubMenuItem(std::string text, bool* ptr) {
	return createSubmenuItem(text, *ptr ? "-5V..5V" : "0V..10V",
		[=](Menu* menu) {
			using StoermelderPackOne::Rack::createValuePtrMenuItem;
			menu->addChild(createMenuLabel("Voltage"));
			menu->addChild(createValuePtrMenuItem("-5V..5V", ptr, true));
			menu->addChild(createValuePtrMenuItem("0V..10V", ptr, false));
		}
	);
}

template <typename MODULE>
struct ArenaModModeMenuItem : MenuItem {
	MODULE* module;
	int id;
	const std::map<MODMODE, std::string> labels = {
		{ MODMODE::RADIUS, "Radius" },
		{ MODMODE::AMOUNT, "Amount" },
		{ MODMODE::OFFSET_X, "Offset x-pos" },
		{ MODMODE::OFFSET_Y, "Offset y-pos" },
		{ MODMODE::WALK, "Random walk" }
	};

	ArenaModModeMenuItem(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		text = "MOD-port";
		rightText = labels.at(module->modMode[id]) + "  " + RIGHT_ARROW;
	}

	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(createMenuLabel("Modulation target"));
		for (const auto& i : labels) {
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(i.second, &module->modMode[id], i.first));
		}
		return menu;
	}
};

template <typename MODULE>
struct ArenaOutputModeMenuItem : MenuItem {
	MODULE* module;
	int id;
	const std::map<OUTPUTMODE, std::string> labels = {
		{ OUTPUTMODE::SCALE, "Scale" },
		{ OUTPUTMODE::LIMIT, "Limit" },
		{ OUTPUTMODE::CLIP_UNI, "Clip 0..10V" },
		{ OUTPUTMODE::CLIP_BI, "Clip -5..5V" },
		{ OUTPUTMODE::FOLD_UNI, "Fold 0..10V" },
		{ OUTPUTMODE::FOLD_BI, "Fold 0..10V" }
	};

	ArenaOutputModeMenuItem(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		text = "OUT-port";
		rightText = labels.at(module->outputMode[id]) + "  " + RIGHT_ARROW;
	}

	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Mix mode"));
		for (const auto& i : labels) {
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(i.second, &module->outputMode[id], i.first));
		}
		return menu;
	}
};


// Screen widgets

template <typename MODULE>
struct ArenaInportDragWidget : XyScreenNodeDragWidget<MODULE> {
	typedef XyScreenNodeDragWidget<MODULE> B;

	/** XyScreenDragWidgetBase: label shown in this node's context menu and tooltip. */
 	std::string getItemName() override {
		return string::f("Channel IN-%i", B::id + 1);
	}

	/** XyScreenDragWidgetBase: extra items appended to this node's context menu. */
	void appendContextMenu(Menu* menu) override {
		menu->addChild(ArenaVoltageSubMenuItem("X-port", &B::module->inputXBipolar[B::id]));
		menu->addChild(ArenaVoltageSubMenuItem("Y-port", &B::module->inputYBipolar[B::id]));
		menu->addChild(new ArenaModModeMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new ArenaOutputModeMenuItem<MODULE>(B::module, B::id));
	}
};

template <typename MODULE>
struct ArenaMixportDragWidget : XyScreenCursorDragWidget<MODULE> {
	typedef XyScreenCursorDragWidget<MODULE> B;

	/** XyScreenDragWidgetBase: label shown in this cursor's context menu and tooltip. */
 	std::string getItemName() override {
		return string::f("Channel MIX-%i", B::id + 1);
	}

	/** XyScreenDragWidgetBase: extra items appended to this cursor's context menu. */
	void appendContextMenu(Menu* menu) override {
		menu->addChild(ArenaVoltageSubMenuItem("X-port", &B::module->mixportXBipolar[B::id]));
		menu->addChild(ArenaVoltageSubMenuItem("Y-port", &B::module->mixportYBipolar[B::id]));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Motion-Sequence"));
		menu->addChild(new XySeqSlotMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqInterpolateMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqTriggerMenuItem<MODULE>(B::module, B::id));
	}
};

template <typename MODULE>
struct ArenaXyScreenWidget : XyScreenWidget<MODULE> {
	ArenaXyScreenWidget(MODULE* module, int inParamIdX, int inParamIdY, int mixParamIdX, int mixParamIdY) : XyScreenWidget<MODULE>(module) {
		uint8_t t0 = module ? module->nodeCount() : 8;
		this->template createNodeWidgets<ArenaInportDragWidget<MODULE>>(module, t0);
		uint8_t t1 = module ? module->cursorCount() : 4;
		this->template createCursorWidgets<ArenaMixportDragWidget<MODULE>>(module, t1);
	}

	void step() override {
		if (this->module) {
			// Preview interpolated automation line if mixport is selected
			this->module->seqPreview = -1;
			for (uint8_t i = 0; i < this->module->cursorCountActive(); i++) {
				if (this->module->selection.isCursor(i))
					this->module->seqPreview = i;
			}
		}
		XyScreenWidget<MODULE>::step();
	}

	/** XyScreenWidget: extra items appended to the whole screen's context menu. */
	void appendContextMenu(Menu* menu) override {
		using StoermelderPackOne::Rack::createValuePtrMenuItem;
		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Number of IN-ports", string::f("%i", this->module->inportsUsed),
			[=](Menu* menu) {
				for (int i = 0; i < this->module->nodeCount(); i++) {
					menu->addChild(createValuePtrMenuItem(string::f("%i", i + 1), &this->module->inportsUsed, i + 1));
				}
			}
		));
		menu->addChild(createSubmenuItem("Number of MIX-ports", string::f("%i", this->module->mixportsUsed),
			[=](Menu* menu) {
				for (int i = 0; i < this->module->cursorCount(); i++) {
					menu->addChild(createValuePtrMenuItem(string::f("%i", i + 1), &this->module->mixportsUsed, i + 1));
				}
			}
		));
	}
};


struct ArenaXySeqLedDisplay : XySeqLedDisplay<ArenaModule<8, 4>> {
	typedef XySeqLedDisplay<ArenaModule<8, 4>> B;

	/** XySeqLedDisplay: label shown for this port's motion-sequence editor. */
	std::string getPortName() override {
		return string::f("Channel MIX-%i", id + 1);
	}

	/** XySeqLedDisplay: extra items appended to this port's context menu. */
	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(ArenaVoltageSubMenuItem("X-port", &B::module->mixportXBipolar[B::id]));
		menu->addChild(ArenaVoltageSubMenuItem("Y-port", &B::module->mixportYBipolar[B::id]));
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
		menu->addChild(new XyScreenAmountSlider<MODULE>(module, id));
		menu->addChild(new XyScreenRadiusSlider<MODULE>(module, id));
		menu->addChild(ArenaVoltageSubMenuItem("X-port", &module->inputXBipolar[id]));
		menu->addChild(ArenaVoltageSubMenuItem("Y-port", &module->inputYBipolar[id]));
		menu->addChild(new ArenaModModeMenuItem<MODULE>(module, id));
		menu->addChild(new ArenaOutputModeMenuItem<MODULE>(module, id));
	}
};


// Module widget

template <typename MODULE, typename LIGHT>
struct ClickableLight : MediumLight<LIGHT> {
	uint8_t id;
	XyScreenSelection::Kind kind;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			MODULE* m = dynamic_cast<MODULE*>(MediumLight<LIGHT>::module);
			bool alreadySelected = kind == XyScreenSelection::Kind::NODE ? m->selection.isNode(id) : m->selection.isCursor(id);
			if (alreadySelected)
				m->selection = XyScreenSelection();
			else
				m->selection = { kind, id };
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

		for (uint8_t i = 0; i < IN_PORTS; i++) {
			float xs[] = { 24.1f, 604.7f };
			float x = xs[i >= IN_PORTS / 2] + (i % (IN_PORTS / 2)) * 30.433f;
			addInput(createInputCentered<StoermelderPort>(Vec(x, 61.1f), module, MODULE::IN + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 96.2f), module, MODULE::IN_X_INPUT + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 130.7f), module, MODULE::IN_X_PARAM + i));
			addParam(createParamCentered<XyScreenDummyMapButton>(Vec(x, 115.3f), module, MODULE::IN_X_POS + i));
			ClickableLight<MODULE, WhiteLight>* l = createLightCentered<ClickableLight<MODULE, WhiteLight>>(Vec(x, 147.6f), module, MODULE::IN_SEL_LIGHT + i);
			l->id = i;
			l->kind = XyScreenSelection::Kind::NODE;
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

		ArenaXyScreenWidget<MODULE>* screenWidget = new ArenaXyScreenWidget<MODULE>(module, MODULE::IN_X_POS, MODULE::IN_Y_POS, MODULE::MIX_X_POS, MODULE::MIX_Y_POS);
		screenWidget->box.pos = Vec(213.2f, 42.1f);
		screenWidget->box.size = Vec(293.6f, 296.0f);
		addChild(screenWidget);

		XySeqEditWidget<MODULE>* seqEditWidget = new XySeqEditWidget<MODULE>(module, MODULE::MIX_X_POS, MODULE::MIX_Y_POS);
		seqEditWidget->box.pos = screenWidget->box.pos;
		seqEditWidget->box.size = screenWidget->box.size;
		addChild(seqEditWidget);

		for (uint8_t i = 0; i < MIX_PORTS; i++) {
			float xs[] = { 154.3f, 534.9f };
			float x = xs[i >= MIX_PORTS / 2] + (i % (MIX_PORTS / 2)) * 30.433f;
			addParam(createParamCentered<StoermelderSmallKnob>(Vec(x, 61.1f), module, MODULE::MIX_VOL_PARAM + i));

			addInput(createInputCentered<StoermelderPort>(Vec(x, 96.2f), module, MODULE::MIX_X_INPUT + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 130.7f), module, MODULE::MIX_X_PARAM + i));
			addParam(createParamCentered<XyScreenDummyMapButton>(Vec(x, 115.3f), module, MODULE::MIX_X_POS + i));
			ClickableLight<MODULE, YellowLight>* l1 = createLightCentered<ClickableLight<MODULE, YellowLight>>(Vec(x, 147.6f), module, MODULE::MIX_SEL_LIGHT + i);
			l1->id = i;
			l1->kind = XyScreenSelection::Kind::CURSOR;
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