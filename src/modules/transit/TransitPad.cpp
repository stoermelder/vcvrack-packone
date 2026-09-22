#include <atomic>
#include "../../plugin.hpp"
#include "../../components/XyScreenWidget.hpp"
#include "../../components/XySeqWidget.hpp"
#include "TransitBase.hpp"

namespace StoermelderPackOne {
namespace Transit {

static const std::vector<std::pair<NVGcolor, std::string>> colors = {
	{ color::GREEN, "Green" },
	{ color::MAGENTA, "Magenta" },
	{ color::BLUE, "Blue" },
	{ color::YELLOW, "Yellow" },
	{ color::CYAN, "Cyan" },
	{ color::WHITE, "White" },
	{ color::RED, "Red" },
	{ color::mult(color::WHITE, 0.45f), "Grey" }
};


enum class SETCVMODE {
	OFF = -1,
	TRIG_FWD = 0,
	VOLT = 1,
	C4 = 2
};

enum class NODEPOSMODE {
	OFF = 0,
	STORE = 1,
	AUTO = 2
};

/** True for every NODEPOSMODE value a valid preset can contain. An unknown
 * value must not be stored: changeSet() tests `!= OFF` and `== AUTO`, so it
 * would silently behave as Store while no context-menu entry shows a
 * checkmark, leaving the user no way to see or change the active mode. */
inline bool isValidNodePosMode(int mode) {
	return mode == (int)NODEPOSMODE::OFF
		|| mode == (int)NODEPOSMODE::STORE
		|| mode == (int)NODEPOSMODE::AUTO;
}

template <uint8_t SNAPSHOTS = 8, uint8_t SETS = 8>
struct TransitPadModule : Module, TransitPadInterface, XyScreenModule<SNAPSHOTS>, XyScreenCursor, XySeqModule<1> {
	struct TransitPadSetParamQuantity : SwitchQuantity {
		TransitPadModule<SNAPSHOTS, SETS>* tpModule = NULL;
		int id = -1;

		std::string getLabel() override {
			if (tpModule && id >= 0) return tpModule->getSetLabel(id);
			return name;
		}
	};

	enum ParamIds {
		ENUMS(SNAPSHOT_X_POS, SNAPSHOTS),
		ENUMS(SNAPSHOT_Y_POS, SNAPSHOTS),
		OUT_X_POS,
		OUT_Y_POS,
		ENUMS(SET_PARAM, SETS),
		NUM_PARAMS
	};
	enum InputIds {
		OUT_X_INPUT,
		OUT_Y_INPUT,
		OUT_SEQ_INPUT,
		OUT_SEQ_PH_INPUT,
		SET_CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(SET_LIGHT, SETS),
		NUM_LIGHTS
	};

	static uint8_t getSetCount() {
		return SETS;
	}

	typedef XyScreenModule<SNAPSHOTS> Sc;
	typedef XySeqModule<1> Seq;

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON]
	 *  Written from the UI thread (context menu via createValuePtrMenuItem, dataFromJson)
	 *  and read from the engine thread (process())
	 */
	std::atomic<int> snapshotsUsed{SNAPSHOTS};

	float dist[SNAPSHOTS];

	float inputInX[SNAPSHOTS];
	float inputInY[SNAPSHOTS];

	float outUiX, outInX;
	dsp::ExponentialFilter outXfilter;
	float outUiY, outInY;
	dsp::ExponentialFilter outYfilter;

	/** [Stored to JSON]
	 *  Written by the engine thread (process(): set-CV and the button scan) and
	 *  the UI thread (dataFromJson); read by the UI thread (context menus,
	 *  drawLayer, getItemLabel) and by TRANSIT's engine-side presetProcessXyPad
	 *  through getPadFactors(). The most actively written of the shared fields,
	 *  so it is atomic like snapshotsUsed and setCvMode.
	 */
	std::atomic<int> currentSet{0};
	/** [Stored to JSON]
	 *  Written from the UI thread (context menu via createValuePtrMenuItem, dataFromJson)
	 *  and read from the engine thread (process()).
	 */
	std::atomic<SETCVMODE> setCvMode{SETCVMODE::TRIG_FWD};
	dsp::SchmittTrigger setCvTrigger;
	/** [Stored to JSON] written from the UI thread (context menu, dataFromJson),
	 *  read from the engine thread (process(), on set change) and the UI thread. */
	std::atomic<NODEPOSMODE> nodePosMode{NODEPOSMODE::OFF};
	std::vector<TransitPadSource> snapshots[SETS];
	/** [Stored to JSON] per-set Mix-cursor position; used only when nodePosMode != OFF. */
	float mixX[SETS], mixY[SETS];
	NVGcolor setColor[SETS];
	/** [Stored to JSON] per-set custom label; empty string means "use default" */
	std::string setLabel[SETS];

	/** [Stored to JSON] when true, pad drag and drop-binding are disabled */
	bool locked = false;

	ClockDividerEx buttonDivider;
	ClockDividerEx lightDivider;
	// Rising-edge detection per set-button, so a re-press of the already-active
	// set is still detected (unlike a currentSet != s comparison).
	dsp::BooleanTrigger setButtonTrigger[SETS];

	// Index of the pad point the user is currently hovering over, or -1.
	// Set by TransitPadSnapshotDragWidget::onEnter / onLeave.
	int vizHoveredId = -1;
	bool vizMode = false;

	TransitPadModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		// Seed LOW, not the trigger's default UNINITIALIZED, since SET_PARAM
		// always starts at 0.f (see configSwitch below).
		for (uint8_t s = 0; s < SETS; s++) {
			setButtonTrigger[s].s = dsp::BooleanTrigger::LOW;
		}

		for (uint8_t s = 0; s < SETS; s++) {
			TransitPadSetParamQuantity* q = configSwitch<TransitPadSetParamQuantity>(SET_PARAM + s, 0.0f, 1.0f, 0.0f, string::f("Snapshot-set #%i", s + 1));
			q->tpModule = this;
			q->id = s;
		}

		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 0, 0.0f, 1.0f, 0.0f, "Snapshot A x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 0, 0.0f, 1.0f, 0.0f, "Snapshot A y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 1, 0.0f, 1.0f, 1.0f, "Snapshot B x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 1, 0.0f, 1.0f, 0.0f, "Snapshot B y-pos");	
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 2, 0.0f, 1.0f, 1.0f, "Snapshot C x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 2, 0.0f, 1.0f, 1.0f, "Snapshot C y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 3, 0.0f, 1.0f, 0.0f, "Snapshot D x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 3, 0.0f, 1.0f, 1.0f, "Snapshot D y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 4, 0.0f, 1.0f, 0.3f, "Snapshot E x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 4, 0.0f, 1.0f, 0.3f, "Snapshot E y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 5, 0.0f, 1.0f, 0.7f, "Snapshot F x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 5, 0.0f, 1.0f, 0.3f, "Snapshot F y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 6, 0.0f, 1.0f, 0.7f, "Snapshot G x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 6, 0.0f, 1.0f, 0.7f, "Snapshot G y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 7, 0.0f, 1.0f, 0.3f, "Snapshot H x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 7, 0.0f, 1.0f, 0.7f, "Snapshot H y-pos");

		configInput(OUT_X_INPUT, "Mix x-pos");
		configInput(OUT_Y_INPUT, "Mix y-pos");
		configInput(OUT_SEQ_INPUT, "Mix sequence select");
		configInput(OUT_SEQ_PH_INPUT, "Mix sequence phase");
		configInput(SET_CV_INPUT, "Snapshot-set select CV");
		configParam<XyScreenParamQuantity>(OUT_X_POS, 0.0f, 1.0f, 0.5f, "Mix x-pos");
		configParam<XyScreenParamQuantity>(OUT_Y_POS, 0.0f, 1.0f, 0.5f, "Mix y-pos");

		for (uint8_t s = 0; s < SETS; s++) {
			snapshots[s].resize(SNAPSHOTS);
		}
		ResetEvent re;
		onReset(re);
	}

	void onSampleRateChange(const Module::SampleRateChangeEvent& e) override {
		buttonDivider.setDivision(e.sampleRate / 1000.f);
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		masterModule = nullptr;
		notifyModuleListeners("Transit");
	}

	void onReset(const ResetEvent& e) override {
		Sc::selection = XyScreenSelection();
		init();
		snapshotsUsed = 4;
		currentSet = 0;
		nodePosMode.store(NODEPOSMODE::OFF, std::memory_order_relaxed);
		locked = false;

		for (uint8_t s = 0; s < SETS; s++) {
			setLabel[s] = "";
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

	/** TransitPadInterface: the snapshot weights Transit reads to blend presets. */
	const std::vector<TransitPadSource>& getPadFactors() override {
		return snapshots[currentSet];
	}

	// Capture the live pad-point geometry and Mix cursor into set s.
	void storeNodePositions(uint8_t s) {
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			snapshots[s][i].x = Sc::nodes.getXFinal(i);
			snapshots[s][i].y = Sc::nodes.getYFinal(i);
			snapshots[s][i].radius = Sc::nodes.getRadiusRaw(i, 0.f);
			snapshots[s][i].amount = Sc::nodes.getAmountFiltered(i, 0.f);
		}
		mixX[s] = getCursorXFinal(0);
		mixY[s] = getCursorYFinal(0);
	}

	// Apply set s's stored layout to the live pad points and Mix cursor. If
	// the Mix cursor is actively CV/param-handle/sequence-driven, process()
	// overrides this write again on the next tick, same as any other source.
	void loadNodePositions(uint8_t s) {
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			Sc::nodes.setXyImmediate(i, snapshots[s][i].x, snapshots[s][i].y);
			Sc::nodes.setRadiusImmediate(i, snapshots[s][i].radius);
			Sc::nodes.setAmountImmediate(i, snapshots[s][i].amount);
		}
		setCursorXyImmediate(0, mixX[s], mixY[s]);
	}

	// Reset every set's stored layout to defaults, so switching mode to Off
	// doesn't leave stale geometry a later Store/Auto could resurrect.
	void clearNodePositions() {
		for (uint8_t s = 0; s < SETS; s++) {
			for (uint8_t i = 0; i < SNAPSHOTS; i++) {
				snapshots[s][i].x = getNodePqX(i)->getDefaultValue();
				snapshots[s][i].y = getNodePqY(i)->getDefaultValue();
				snapshots[s][i].radius = getNodeRadiusDefault(i);
				snapshots[s][i].amount = Sc::getNodeAmountDefault(i);
			}
			mixX[s] = paramQuantities[OUT_X_POS]->getDefaultValue();
			mixY[s] = paramQuantities[OUT_Y_POS]->getDefaultValue();
		}
	}

	// Switches the active set. A no-op when newSet == currentSet — the
	// VOLT/C4 CV paths call this every tick and rely on that to avoid
	// reloading on every sample. Use reloadCurrentSet() to force a reload
	// of the set that's already active.
	void changeSet(int newSet) {
		if (newSet == currentSet) return;
		NODEPOSMODE m = nodePosMode.load(std::memory_order_relaxed);
		if (m == NODEPOSMODE::AUTO) storeNodePositions(currentSet);
		currentSet = newSet;
		if (m != NODEPOSMODE::OFF) loadNodePositions(currentSet);
	}

	// Reloads the current set's stored layout without changing currentSet or
	// capturing first, so unsaved pad edits are discarded on a re-press.
	void reloadCurrentSet() {
		if (nodePosMode.load(std::memory_order_relaxed) != NODEPOSMODE::OFF) loadNodePositions(currentSet);
	}

	void process(const ProcessArgs& args) override {
		// Snapshot once so the whole block sees a coherent value; relaxed
		// since we only need atomicity, not synchronisation.
		const int n = snapshotsUsed.load(std::memory_order_relaxed);
		const SETCVMODE mode = setCvMode.load(std::memory_order_relaxed);

		if (inputs[SET_CV_INPUT].isConnected()) {
			switch (mode) {
				case SETCVMODE::OFF:
					break;
				case SETCVMODE::TRIG_FWD:
					if (setCvTrigger.process(inputs[SET_CV_INPUT].getVoltage())) {
						changeSet((currentSet + 1) % SETS);
					}
					break;
				case SETCVMODE::VOLT: {
					float v = clamp(inputs[SET_CV_INPUT].getVoltage(), 0.f, 10.f);
					int s = int(v / 10.f * SETS);
					changeSet(std::min(s, (int)SETS - 1));
					break;
				}
				case SETCVMODE::C4:
					changeSet(clamp((int)std::round(inputs[SET_CV_INPUT].getVoltage() * 12.f), 0, (int)SETS - 1));
					break;
			}
		}
		if (buttonDivider.process()) {
			for (uint8_t s = 0; s < SETS; s++) {
				if (setButtonTrigger[s].process(params[SET_PARAM + s].getValue() > 0.5f)) {
					if (s == currentSet) reloadCurrentSet();
					else changeSet(s);
					break;
				}
			}
		}

		for (uint8_t j = 0; j < n; j++) {
			inputInX[j] = Sc::nodes.getXFiltered(j, args.sampleTime);
			inputInY[j] = Sc::nodes.getYFiltered(j, args.sampleTime);

			Sc::nodes.setRadius(j, Sc::nodes.getRadiusRaw(j, args.sampleTime));
			Sc::nodes.setAmount(j, Sc::nodes.getAmountFiltered(j, args.sampleTime));

			float x = inputInX[j];
			x = clamp(x, 0.f, 1.f);
			params[SNAPSHOT_X_POS + j].setValue(x);

			float y = inputInY[j];
			y = clamp(y, 0.f, 1.f);
			params[SNAPSHOT_Y_POS + j].setValue(y);
		}

		XyScreenParamQuantity* px = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[OUT_X_POS]);
		outInX = px->hasHandle ? px->getParam()->getValue() : outXfilter.process(args.sampleTime, outUiX);
		XyScreenParamQuantity* py = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[OUT_Y_POS]);
		outInY = py->hasHandle ? py->getParam()->getValue() : outYfilter.process(args.sampleTime, outUiY);

		if (inputs[OUT_SEQ_INPUT].isConnected()) {
			Seq::seqProcess(inputs[OUT_SEQ_INPUT], 0);
		}

		bool setX = false, setY = false;

		if (inputs[OUT_SEQ_PH_INPUT].isConnected()) {
			float v = clamp(inputs[OUT_SEQ_PH_INPUT].getVoltage() / 10.f, 0.f, 1.f);
			Vec d = Seq::seqValue(0, v);
			params[OUT_X_POS].setValue(d.x);
			setX = true;
			params[OUT_Y_POS].setValue(d.y);
			setY = true;
		}

		if (!setX && inputs[OUT_X_INPUT].isConnected()) {
			float x = inputs[OUT_X_INPUT].getVoltage() / 10.f;
			x += 0.5f;
			x = clamp(x, 0.f, 1.f);
			params[OUT_X_POS].setValue(x);
			setX = true;
		} 

		if (!setY && inputs[OUT_Y_INPUT].isConnected()) {
			float y = inputs[OUT_Y_INPUT].getVoltage() / 10.f;
			y += 0.5f;
			y = clamp(y, 0.f, 1.f);
			params[OUT_Y_POS].setValue(y);
			setY = true;
		}

		if (!setX) {
			params[OUT_X_POS].setValue(outInX);
		}
		if (!setY) {
			params[OUT_Y_POS].setValue(outInY);
		}

		float outX = params[OUT_X_POS].getValue();
		float outY = params[OUT_Y_POS].getValue();
		Vec outVec = Vec(outX, outY);

		for (int j = 0; j < n; j++) {
			float inX = params[SNAPSHOT_X_POS + j].getValue();
			float inY = params[SNAPSHOT_Y_POS + j].getValue();

			Vec inVec = Vec(inX, inY);
			dist[j] = inVec.minus(outVec).norm();

			float r = Sc::getNodeRadiusFinal(j);
			if (dist[j] < r) {
				float f = (r - dist[j]) / r * Sc::getNodeAmountFinal(j);
				snapshots[currentSet][j].weight = std::min(1.0f, f * 1.1f);
			}
			else {
				snapshots[currentSet][j].weight = 0.f;
			}
		}

		// Snapshots above the active count are not drawn and not draggable, so they
		// must not keep contributing either: without this, lowering "Number of
		// snapshots" (or loading a patch with a lower count) leaves whatever weight
		// they last earned in place, and TRANSIT keeps blending a pad point the user
		// can no longer see or move.
		for (int j = n; j < SNAPSHOTS; j++) {
			dist[j] = std::numeric_limits<float>::infinity();
			snapshots[currentSet][j].weight = 0.f;
		}

		if (lightDivider.process()) {
			for (uint8_t s = 0; s < SETS; s++) {
				lights[SET_LIGHT + s].setBrightness(currentSet == s ? 1.0f : 0.0f);
			}
		}
	}

	/** XySeqModule: the only motion-sequence port is the Out cursor's (index 0). */
	bool seqPortHidden(int port) override {
		return port != 0;
	}

	/** XyScreenModule: one-time setup for the Out (cursor) point, called from initNodes(). */
	void initExtra() override {
		setCursorXyImmediate(0, paramQuantities[OUT_X_POS]->getDefaultValue(), paramQuantities[OUT_Y_POS]->getDefaultValue());
		outXfilter.setTau(0.05f);
		outYfilter.setTau(0.05f);
		for (uint8_t s = 0; s < SETS; s++) {
			for (uint8_t i = 0; i < SNAPSHOTS; i++) {
				dist[i] = std::numeric_limits<float>::infinity();
				snapshots[s][i].id = i < 4 ? i : -1;
				snapshots[s][i].weight = 0.f;
				// Sc::nodes isn't reset yet here, so seed from the defaults directly.
				snapshots[s][i].x = getNodePqX(i)->getDefaultValue();
				snapshots[s][i].y = getNodePqY(i)->getDefaultValue();
				snapshots[s][i].radius = getNodeRadiusDefault(i);
				snapshots[s][i].amount = Sc::getNodeAmountDefault(i);
			}
			mixX[s] = paramQuantities[OUT_X_POS]->getDefaultValue();
			mixY[s] = paramQuantities[OUT_Y_POS]->getDefaultValue();
			setColor[s] = colors[s % colors.size()].first;
			setLabel[s] = "";
		}
	}

	/** XyScreenModule: how many of the SNAPSHOTS nodes are currently active. */
	inline uint8_t nodeCountActive() override {
		return (uint8_t)snapshotsUsed.load(std::memory_order_relaxed);
	}

	/** XyScreenModule: the node (snapshot) x-position param. */
	engine::ParamQuantity* getNodePqX(uint8_t id) override {
		return paramQuantities[SNAPSHOT_X_POS + id];
	}

	/** XyScreenModule: the node (snapshot) y-position param. */
	engine::ParamQuantity* getNodePqY(uint8_t id) override {
		return paramQuantities[SNAPSHOT_Y_POS + id];
	}

	/** XyScreenCursor: the single Out cursor. */
	uint8_t cursorCount() const override {
		return 1;
	}

	/** XyScreenCursor: the param-backed x-position the Out cursor widget draws. */
	float getCursorXFinal(uint8_t id) const override {
		return paramQuantities[OUT_X_POS]->getParam()->getValue();
	}

	/** XyScreenCursor: the param-backed y-position the Out cursor widget draws. */
	float getCursorYFinal(uint8_t id) const override {
		return paramQuantities[OUT_Y_POS]->getParam()->getValue();
	}

	/** XyScreenCursor: write the Out cursor's position immediately (drag end, undo/redo).
	 * Out-of-range id is a silent no-op, matching XyScreenNodes's bounds
	 * checks and the rest of the codebase's convention for bad indices.
	 * (There is only one cursor here, always at id 0, so the array-overrun
	 * risk this guards against elsewhere doesn't apply — but an id != 0
	 * still shouldn't silently act as if it addressed the Out cursor.) */
	void setCursorXyImmediate(uint8_t id, float x, float y) override {
		if (id >= 1) return;
		paramQuantities[OUT_X_POS]->getParam()->setValue(x);
		outXfilter.out = outUiX = x;
		paramQuantities[OUT_Y_POS]->getParam()->setValue(y);
		outYfilter.out = outUiY = y;
	}

	/** XyScreenCursor: write the Out cursor's position through the UI filter (live drag). */
	void setCursorXyFiltered(uint8_t id, float x, float y) override {
		if (id >= 1) return;
		outUiX = x;
		outUiY = y;
	}

	/** XyScreenModule: distance from the Out cursor to a snapshot node, for the connector-line draw. */
	inline float getCursorToNodeDistance(uint8_t cursorId, uint8_t nodeId) override {
		return dist[nodeId];
	}

	/** XyScreenModule: default radius for a new snapshot node. */
	inline float getNodeRadiusDefault(uint8_t id) override {
		return 1.f;
	}

	/** XyScreenModule: color a snapshot node is drawn with — the active set's color. */
	virtual inline NVGcolor getNodeColor(uint8_t id) override {
		return setColor[currentSet];
	}

	/** XyScreenCursor: color the Out cursor is drawn with. */
	virtual inline NVGcolor getCursorColor(uint8_t id) const override {
		return color::WHITE;
	}

	std::string getSetLabel(uint8_t s) {
		if (setLabel[s].empty()) return string::f("Snapshot-set #%i", s + 1);
		return setLabel[s];
	}

	bool isLocked() const {
		return locked;
	}

	// Bind the pad point at the given snapshot index within the current set
	// to a Transit snapshot slot. Pass -1 to unbind.
	void bindSnapshot(int snapshotId, int slotId) {
		snapshots[currentSet][snapshotId].id = slotId;
	}

	std::string getItemLabel(uint8_t s, uint8_t id) {
		if (masterModule == nullptr)
			return "<No TRANSIT module>";
		if (snapshots[s][id].id >= 0) {
			std::string custom = masterModule->getSlotLabel(snapshots[s][id].id);
			if (custom != "")
				return string::f("Snapshot #%i: %s", snapshots[s][id].id + 1, custom.c_str());
			else
				return string::f("Snapshot #%i", snapshots[s][id].id + 1);
		}
		else {
			return "No snapshot";
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "snapshotsUsed", json_integer(snapshotsUsed));
		json_object_set_new(rootJ, "setCvMode", json_integer((int)setCvMode.load(std::memory_order_relaxed)));
		json_object_set_new(rootJ, "nodePosMode", json_integer((int)nodePosMode.load(std::memory_order_relaxed)));
		json_object_set_new(rootJ, "currentSet", json_integer(currentSet));
		json_object_set_new(rootJ, "locked", json_boolean(locked));

		// Live pad-point layout, independent of any set.
		json_t* nodesJ = json_array();
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			json_t* nodeJ = json_object();
			Sc::nodes.dataToJson(nodeJ, i);
			json_array_append_new(nodesJ, nodeJ);
		}
		json_object_set_new(rootJ, "nodes", nodesJ);

		bool storeNodePos = nodePosMode.load(std::memory_order_relaxed) != NODEPOSMODE::OFF;
		json_t* setsJ = json_array();
		for (uint8_t s = 0; s < SETS; s++) {
			json_t* setJ = json_object();
			json_t* snapshotsJ = json_array();
			for (uint8_t i = 0; i < SNAPSHOTS; i++) {
				json_t* snapshotJ = json_object();
				json_object_set_new(snapshotJ, "id", json_integer(snapshots[s][i].id));
				if (storeNodePos) {
					json_object_set_new(snapshotJ, "x", json_real(snapshots[s][i].x));
					json_object_set_new(snapshotJ, "y", json_real(snapshots[s][i].y));
					json_object_set_new(snapshotJ, "radius", json_real(snapshots[s][i].radius));
					json_object_set_new(snapshotJ, "amount", json_real(snapshots[s][i].amount));
				}
				json_array_append_new(snapshotsJ, snapshotJ);
			}
			json_object_set_new(setJ, "snapshots", snapshotsJ);
			if (storeNodePos) {
				json_object_set_new(setJ, "mixX", json_real(mixX[s]));
				json_object_set_new(setJ, "mixY", json_real(mixY[s]));
			}
			json_object_set_new(setJ, "color", json_string(color::toHexString(setColor[s]).c_str()));
			if (!setLabel[s].empty()) {
				json_object_set_new(setJ, "label", json_string(setLabel[s].c_str()));
			}
			json_array_append_new(setsJ, setJ);
		}
		json_object_set_new(rootJ, "sets", setsJ);

		json_t* outputJ = json_object();
		Seq::dataToJson(outputJ, 0);
		json_object_set_new(rootJ, "output", outputJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* setCvModeJ = json_object_get(rootJ, "setCvMode");
		if (setCvModeJ) setCvMode.store((SETCVMODE)json_integer_value(setCvModeJ), std::memory_order_relaxed);

		json_t* nodePosModeJ = json_object_get(rootJ, "nodePosMode");
		if (nodePosModeJ) {
			int m = json_integer_value(nodePosModeJ);
			nodePosMode.store(isValidNodePosMode(m) ? (NODEPOSMODE)m : NODEPOSMODE::OFF, std::memory_order_relaxed);
		}

		json_t* currentSetJ = json_object_get(rootJ, "currentSet");
		if (currentSetJ) currentSet = std::max(0, std::min((int)json_integer_value(currentSetJ), (int)SETS - 1));

		json_t* lockedJ = json_object_get(rootJ, "locked");
		if (lockedJ) locked = json_is_true(lockedJ);

		int su = json_integer_value(json_object_get(rootJ, "snapshotsUsed"));
		snapshotsUsed = std::max(1, std::min(su, (int)SNAPSHOTS));

		json_t* nodesJ = json_object_get(rootJ, "nodes");
		size_t maxNodes = std::min((size_t)SNAPSHOTS, json_array_size(nodesJ));
		for (size_t i = 0; i < maxNodes; ++i) {
			Sc::nodes.dataFromJson(json_array_get(nodesJ, i), i);
		}

		json_t* setsJ = json_object_get(rootJ, "sets");
		size_t maxs = std::min((size_t)SETS, json_array_size(setsJ));
		for (size_t s = 0; s < maxs; ++s) {
			json_t* setJ = json_array_get(setsJ, s);
			json_t* snapshotsJ = json_object_get(setJ, "snapshots");
			if (json_is_array(snapshotsJ)) {
				size_t n = json_array_size(snapshotsJ);
				size_t maxn = std::min((size_t)SNAPSHOTS, n);
				for (size_t i = 0; i < maxn; ++i) {
					json_t* snapshotJ = json_array_get(snapshotsJ, i);
					snapshots[s][i].id = json_integer_value(json_object_get(snapshotJ, "id"));
					// Absent when node-position mode is off; keep the seeded defaults.
					json_t* xJ = json_object_get(snapshotJ, "x");
					if (xJ) snapshots[s][i].x = json_real_value(xJ);
					json_t* yJ = json_object_get(snapshotJ, "y");
					if (yJ) snapshots[s][i].y = json_real_value(yJ);
					json_t* radiusJ = json_object_get(snapshotJ, "radius");
					if (radiusJ) snapshots[s][i].radius = json_real_value(radiusJ);
					json_t* amountJ = json_object_get(snapshotJ, "amount");
					if (amountJ) snapshots[s][i].amount = json_real_value(amountJ);
				}
			}
			json_t* mixXJ = json_object_get(setJ, "mixX");
			if (mixXJ) mixX[s] = json_real_value(mixXJ);
			json_t* mixYJ = json_object_get(setJ, "mixY");
			if (mixYJ) mixY[s] = json_real_value(mixYJ);
			json_t* colorJ = json_object_get(setJ, "color");
			if (const char* color = json_string_value(colorJ)) setColor[s] = color::fromHexString(color);
			json_t* labelJ = json_object_get(setJ, "label");
			if (const char* label = json_string_value(labelJ)) setLabel[s] = label;
		}

		json_t* outputJ = json_object_get(rootJ, "output");
		Seq::dataFromJson(outputJ, 0);

		// Resync the UI-shadow cursor state (outUiX/outXfilter) that process()
		// reads instead of the param; Rack's own param restore doesn't touch it.
		bool nodePos = nodePosMode.load(std::memory_order_relaxed) != NODEPOSMODE::OFF;
		float x = nodePos ? mixX[currentSet] : paramQuantities[OUT_X_POS]->getParam()->getValue();
		float y = nodePos ? mixY[currentSet] : paramQuantities[OUT_Y_POS]->getParam()->getValue();
		setCursorXyImmediate(0, x, y);
	}
};


template <typename MODULE>
struct TransitPadSnapshotDragWidget : XyScreenNodeDragWidget<MODULE> {
	typedef XyScreenNodeDragWidget<MODULE> AW;
	ui::Tooltip* tooltip = NULL;
	// True while a drag from a TransitLedButton is hovering over this node,
	// so the draw code can highlight it as a valid drop target.
	bool dropArmed = false;

	~TransitPadSnapshotDragWidget() {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
		}
	}

	/** XyScreenDragWidgetBase: single-character label drawn inside this node. */
	char getItemChar() override {
		return 'A' + AW::id;
	}

	/** XyScreenDragWidgetBase: label shown in this node's context menu and tooltip. */
 	std::string getItemName() override {
		return AW::module->getItemLabel(AW::module->currentSet, AW::id);
	}

	/** XyScreenDragWidgetBase: items prepended to this node's context menu. */
	void prependContextMenu(Menu* menu) override {
		menu->addChild(createMenuItem("Bind snapshot", "", [=]() {
			// Re-check masterModule inside the lambda; Transit may have been
			// disconnected between menu construction and click.
			if (AW::module->masterModule) {
				AW::module->bindSnapshot(AW::id, AW::module->masterModule->getSelectedSlot());
			}
		}, AW::module->isLocked() || AW::module->masterModule == nullptr || AW::module->masterModule->getSelectedSlot() == -1));
		menu->addChild(createMenuItem("Unbind snapshot", "", [=]() {
			AW::module->bindSnapshot(AW::id, -1);
		}, AW::module->isLocked()));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Current set"));
		menu->addChild(Rack::createColorSubmenuItem("Color", &AW::module->setColor[AW::module->currentSet], colors));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("All sets"));
	}

	void onEnter(const event::Enter& e) override {
		if (!AW::module->isNodeActive(AW::id)) return;
		this->module->vizHoveredId = this->id;
		if (settings::tooltips && !tooltip) {
			tooltip = new ui::Tooltip;
			tooltip->text = getItemName();
			APP->scene->addChild(tooltip);
		}
		AW::onEnter(e);
	}

	void onLeave(const event::Leave& e) override {
		if (!AW::module->isNodeActive(AW::id)) return;
		if (this->module->vizHoveredId == this->id) {
			this->module->vizHoveredId = -1;
		}
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
			tooltip = NULL;
		}
		AW::onLeave(e);
	}

	void onDragEnter(const event::DragEnter& e) override {
		// Highlight as a valid drop target when a TransitLedButton is being dragged.
		if (dynamic_cast<TransitSnapshotButton*>(e.origin) != nullptr) {
			dropArmed = true;
			e.consume(this);
		}
		AW::onDragEnter(e);
	}

	void onDragLeave(const event::DragLeave& e) override {
		dropArmed = false;
		AW::onDragLeave(e);
	}

	void onDragDrop(const event::DragDrop& e) override {
		if (!AW::module->isNodeActive(AW::id)) return;
		// Bind the pad point to the slot of the dropped TransitLedButton.
		TransitSnapshotButton* src = dynamic_cast<TransitSnapshotButton*>(e.origin);
		if (src && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			int slot = src->getSlotIndex();
			if (slot >= 0 && !this->module->isLocked()) {
				this->module->bindSnapshot(this->id, slot);
			}
			dropArmed = false;
			e.consume(this);
		}
		AW::onDragDrop(e);
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		AW::drawLayer(args, layer);
		if (!AW::module->isNodeActive(AW::id)) return;
		if (layer != 1 || !dropArmed) return;

		// Bright halo around the node while a snapshot button is being dragged over it,
		// so the user can see at a glance that this is a valid drop target.
		Vec c = Vec(this->box.size.x / 2.f, this->box.size.y / 2.f);
		float r = this->box.size.x / 2.f;

		// Bright outer ring.
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, r + 1.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.95f));
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);
	}
};


template <typename MODULE>
struct TransitPadOutDragWidget : XyScreenCursorDragWidget<MODULE> {
	typedef XyScreenCursorDragWidget<MODULE> B;

	/** XyScreenDragWidgetBase: label shown in this cursor's context menu and tooltip. */
 	std::string getItemName() override {
		return "Mix";
	}

	/** XyScreenDragWidgetBase: single-character label drawn inside this cursor. */
	char getItemChar() override {
		return '+';
	}

	/** XyScreenDragWidgetBase: extra items appended to this cursor's context menu. */
	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Motion-Sequence"));
		menu->addChild(new XySeqSlotMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqInterpolateMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqTriggerMenuItem<MODULE>(B::module, B::id));
	}
};

template <typename MODULE>
struct TransitPadXyScreenWidget : XyScreenWidget<MODULE> {
	TransitPadXyScreenWidget(MODULE* module, int inParamIdX, int inParamIdY, int mixParamIdX, int mixParamIdY) : XyScreenWidget<MODULE>(module) {
		uint8_t t0 = module ? module->nodeCount() : 4;
		this->template createNodeWidgets<TransitPadSnapshotDragWidget<MODULE>>(module, t0);
		uint8_t t1 = module ? module->cursorCount() : 1;
		this->template createCursorWidgets<TransitPadOutDragWidget<MODULE>>(module, t1);
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

	void onButton(const event::Button& e) override {
		if (this->module->isLocked() && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			return;
		}
		XyScreenWidget<MODULE>::onButton(e);
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		XyScreenWidget<MODULE>::drawLayer(args, layer);
		if (layer != 1 || !this->module || !this->module->isLocked()) return;
		// Small padlock badge in the top-right corner of the screen so the
		// user can see at a glance that dragging/binding is disabled.
		NVGcontext* vg = args.vg;
		float cx = this->box.size.x - 9.f;
		float cy = 9.f;

		// Shackle (arc on top of the body)
		nvgBeginPath(vg);
		nvgStrokeColor(vg, nvgRGBAf(1.f, 1.f, 1.f, 0.85f));
		nvgStrokeWidth(vg, 1.4f);
		nvgArc(vg, cx, cy - 1.5f, 2.5f, M_PI * 0.85f, M_PI * 0.15f, NVG_CW);
		nvgStroke(vg);

		// Body (rounded rect)
		nvgBeginPath(vg);
		nvgFillColor(vg, nvgRGBAf(1.f, 1.f, 1.f, 0.85f));
		nvgRoundedRect(vg, cx - 3.5f, cy - 0.5f, 7.f, 6.f, 1.f);
		nvgFill(vg);
	}

	/** XyScreenWidget: extra items appended to the whole screen's context menu. */
	void appendContextMenu(Menu* menu) override {
		using StoermelderPackOne::Rack::createAtomicValuePtrMenuItem;
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Visualize", "Space", &this->module->vizMode));
		menu->addChild(createSubmenuItem("Number of snapshots", string::f("%i", this->module->snapshotsUsed.load(std::memory_order_relaxed)),
			[=](Menu* menu) {
				for (int i = 0; i < this->module->nodeCount(); i++) {
					const int target = i + 1;
					menu->addChild(createAtomicValuePtrMenuItem(string::f("%i", target), &this->module->snapshotsUsed, target));
				}
			}
		));
		menu->addChild(createSubmenuItem("Snapshot-set CV mode", "",
			[=](Menu* menu) {
				menu->addChild(createAtomicValuePtrMenuItem("Off", &this->module->setCvMode, SETCVMODE::OFF));
				menu->addChild(new MenuSeparator);
				menu->addChild(createAtomicValuePtrMenuItem("Trigger forward", &this->module->setCvMode, SETCVMODE::TRIG_FWD));
				menu->addChild(createAtomicValuePtrMenuItem("0..10V", &this->module->setCvMode, SETCVMODE::VOLT));
				menu->addChild(createAtomicValuePtrMenuItem("C4", &this->module->setCvMode, SETCVMODE::C4));
			}
		));
		menu->addChild(createSubmenuItem("Snapshot-set node positions", "",
			[=](Menu* menu) {
				MODULE* m = this->module;
				bool isOff = m->nodePosMode.load(std::memory_order_relaxed) == NODEPOSMODE::OFF;
				menu->addChild(createMenuItem("Off", CHECKMARK(isOff), [=]() {
					m->nodePosMode.store(NODEPOSMODE::OFF, std::memory_order_relaxed);
					m->clearNodePositions();
				}));
				menu->addChild(new MenuSeparator);
				menu->addChild(createAtomicValuePtrMenuItem("Store (manual)", &m->nodePosMode, NODEPOSMODE::STORE));
				menu->addChild(createAtomicValuePtrMenuItem("Auto (on set change)", &m->nodePosMode, NODEPOSMODE::AUTO));
			}
		));
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Lock pad", "", &this->module->locked));
	}
};


template <typename MODULE>
struct TransitPadXySeqLedDisplay : XySeqLedDisplay<MODULE> {
	/** XySeqLedDisplay: label shown for this port's motion-sequence editor. */
	std::string getPortName() override {
		return "Mix";
	}
};


// Square snapshot-set button, custom-drawn with nanovg so it sits flush in
// the screen panel's bottom edge instead of a separate round button above it.
template <typename MODULE>
struct TransitPadSetButton : app::Switch {
	MODULE* module;
	size_t setIndex;

	TransitPadSetButton() {
		momentary = true;
	}

	// Everything lives on layer 1 so it paints on top of the parent row's
	// layer-1 background: layer 0 and 1 are separate full passes, so a
	// layer-0 fill here would end up hidden under it instead.
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			bool lit = module && module->lights[MODULE::SET_LIGHT + setIndex].getBrightness() > 0.5f;
			NVGcolor col = module ? module->setColor[setIndex] : color::WHITE;
			float w = box.size.x, h = box.size.y;

			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

			// Flat fill tiling with neighboring buttons into one continuous
			// bar. Inactive sets stay very faint.
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, w, h);
			nvgFillColor(args.vg, color::mult(col, lit ? 0.6f : 0.08f));
			nvgFill(args.vg);

			// Off-center highlight, inset so the fade-out stays inside the cell.
			NVGcolor icol = nvgRGBAf(col.r, col.g, col.b, lit ? 0.3f : 0.06f);
			NVGcolor ocol = nvgRGBAf(col.r, col.g, col.b, 0.0f);
			float inset = h * 0.35f;
			float yOffset = h * 0.15f;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, w, h);
			nvgFillPaint(args.vg, nvgBoxGradient(args.vg, inset, inset + yOffset, w - 2.f * inset, h - 2.f * inset, 0.f, h * 0.6f, icol, ocol));
			nvgFill(args.vg);

			if (lit) {
				// LED-style glow, matching PulseLedButton::drawHalo.
				Vec c = Vec(w / 2.f, h / 2.f);
				float radius = std::min(w, h) / 2.f;
				float oradius = 2.5f * radius;
				NVGcolor hicol = color::mult(col, 0.07f);
				NVGcolor hocol = nvgRGB(0, 0, 0);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, c.x - oradius, c.y - oradius, 2.f * oradius, 2.f * oradius);
				nvgFillPaint(args.vg, nvgRadialGradient(args.vg, c.x, c.y, radius, oradius, hicol, hocol));
				nvgFill(args.vg);
			}

			// Vertical separator only; the row draws the top/bottom edges.
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, w, 0.f);
			nvgLineTo(args.vg, w, h);
			nvgStrokeColor(args.vg, lit ? col : nvgRGBAf(1.f, 1.f, 1.f, 0.08f));
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);

			nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);

			ParamWidget::drawLayer(args, layer);
			ParamWidget::draw(args);
		}
	}

	struct LabelField : ui::TextField {
		MODULE* module;
		size_t setIndex;
		void onSelectKey(const event::SelectKey& e) override {
			if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
				module->setLabel[setIndex] = text;

				ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
				overlay->requestDelete();
				e.consume(this);
			}

			if (!e.getTarget()) {
				ui::TextField::onSelectKey(e);
			}
		}

		void step() override {
			// Keep selected
			APP->event->setSelectedWidget(this);
			TextField::step();
		}
	};

	virtual void appendContextMenu(ui::Menu* menu) override {
		if (!module) return;
		menu->addChild(new MenuSeparator());
		menu->addChild(Rack::createColorSubmenuItem("Color", &module->setColor[setIndex], colors));
		MODULE* m = module;
		size_t s = setIndex;
		menu->addChild(createSubmenuItem("Label", "", [=](Menu* menu) {
			LabelField* labelField = new LabelField;
			labelField->placeholder = "Set label";
			labelField->text = m->setLabel[s];
			labelField->box.size.x = 180;
			labelField->module = m;
			labelField->setIndex = s;
			menu->addChild(labelField);

			menu->addChild(createMenuItem("Reset", "", [=]() { m->setLabel[s] = ""; }));
		}));
		NODEPOSMODE nodePosMode = module->nodePosMode.load(std::memory_order_relaxed);
		if (nodePosMode != NODEPOSMODE::OFF) {
			// Disabled outside manual Store mode: Auto already captures on switch.
			menu->addChild(createMenuItem("Store positions", "", [=]() { m->storeNodePositions(s); }, nodePosMode != NODEPOSMODE::STORE));
		}
		menu->addChild(new MenuSeparator());
		for (size_t i = 0; i < module->nodeCountActive(); i++) {
			menu->addChild(createMenuLabel(module->getItemLabel(setIndex, i)));
		}
	}
};


// Bottom extension of the screen panel holding the 8 snapshot-set buttons.
// Drawn in drawLayer(1), matching XyScreenWidget's own background (which
// only ever draws on layer 1), so both panels read as one continuous tone.
template <typename MODULE>
struct TransitPadButtonRow : widget::Widget {
	MODULE* module;

	TransitPadButtonRow(MODULE* module) {
		this->module = module;
	}

	// One grid cell per button, matching XyScreenWidget's 8-column grid above.
	// Buttons are sized to tile the row edge-to-edge, so the row reads as one
	// continuous flat bar rather than a strip of spaced-out controls.
	void createButtons() {
		uint8_t count = module ? MODULE::getSetCount() : 8;
		float cell = box.size.x / count;

		for (uint8_t s = 0; s < count; s++) {
			TransitPadSetButton<MODULE>* button = createParam<TransitPadSetButton<MODULE>>(Vec(cell * s, 0.f), module, MODULE::SET_PARAM + s);
			button->box.size = Vec(cell, box.size.y);
			button->module = module;
			button->setIndex = s;
			addChild(button);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			float b = std::max(0.2f, settings::rackBrightness);
			float b_inv = 1.f + std::max(b - settings::rackBrightness, 0.f) * 8.f;
			nvgGlobalAlpha(args.vg, b);

			// Same 3px bleed as XyScreenWidget's background, so both reach
			// the same panel edges left/right.
			math::Rect r = box.zeroPos().grow(Vec(3.f, 3.f));
			NVGcolor bottomColor = color::mult(nvgRGB(0x12, 0x12, 0x12), b_inv);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, RECT_ARGS(r));
			nvgFillColor(args.vg, bottomColor);
			nvgFill(args.vg);

			// Bottom bevel highlight, matching XyScreenWidget's own. No top
			// highlight here — the row sits with a visible gap below the
			// screen rather than flush against it, so pairing both lines
			// would read as a second sunken screen.
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, r.pos.x, r.size.y + 2 * r.pos.y + 0.5);
			nvgLineTo(args.vg, r.size.x + r.pos.x, r.size.y + 2 * r.pos.y + 0.5);
			nvgStrokeColor(args.vg, nvgRGBAf(1, 1, 1, 0.25));
			nvgStrokeWidth(args.vg, 1.0);
			nvgStroke(args.vg);

			// Black border.
			math::Rect rBorder = r.shrink(math::Vec(1, 1));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, RECT_ARGS(rBorder));
			nvgStrokeColor(args.vg, bottomColor);
			nvgStrokeWidth(args.vg, 2.0);
			nvgStroke(args.vg);

			nvgGlobalAlpha(args.vg, 1.f);
		}
		Widget::drawLayer(args, layer);
	}
};


// Overlay widget added directly to APP->scene->rack — drawn in rack coordinates.
// Activated by the space key; draws a spline from each pad point to the snapshot
// button on the host TRANSIT (or +T expander) it is bound to.
struct TransitPadVizOverlay : TransparentWidget {
	TransitPadModule<>* module = nullptr;
	// Non-owning pointer to the host widget (for absolute position).
	Widget* hostWidget = nullptr;
	// Non-owning pointer to the pad's screen widget, so the drawn geometry always
	// matches its actual box instead of an independent copy of the same literals.
	Widget* screenWidget = nullptr;

	void step() override {
		// Track parent size so NVG scissor doesn't clip our drawings.
		if (parent) { box.pos = Vec(0.f, 0.f); box.size = parent->box.size; }
		TransparentWidget::step();
	}

	// Resolves the absolute rack-space center of a TRANSIT/-T snapshot button.
	// Returns Vec() if the slot's owner module or its button widget cannot be found.
	Vec getButtonPos(int slotIndex) {
		if (!module || !module->masterModule) return Vec();
		Module* ownerModule;
		int localIndex;
		if (!module->masterModule->getSlotOwner(slotIndex, ownerModule, localIndex)) return Vec();
		ModuleWidget* ownerMw = APP->scene->rack->getModule(ownerModule->id);
		if (!ownerMw) return Vec();
		// TransitSnapshotButton is used by both TRANSIT and +T to render the per-slot
		// LED button. Walk the owner's param widgets, find the one whose
		// TransitLedButton carries the matching local id, and use its center.
		for (ParamWidget* pw : ownerMw->getParams()) {
			auto* btn = dynamic_cast<TransitSnapshotButton*>(pw);
			if (btn && btn->id == localIndex) {
				return ownerMw->box.pos.plus(pw->box.getCenter());
			}
		}
		return Vec();
	}

	// Draws a Bezier curve between two points with a glow + core pass.
	void drawSpline(NVGcontext* vg, Vec a, Vec b, NVGcolor col, bool highlighted = false, Vec aDir = Vec(0.f, 0.f)) {
		float dist = a.minus(b).norm();
		Vec dir  = b.minus(a).normalize();
		Vec perp = Vec(-dir.y, dir.x);  // 90° CCW — always lateral to the connection
		float tang  = dist * 0.35f;
		float bulge = dist * 0.45f;
		Vec cp1 = (aDir.norm() > 0.001f)
			? a.plus(aDir.normalize().mult(tang))
			: a.plus(dir.mult(tang)).plus(perp.mult(bulge));
		Vec cp2 = b.minus(dir.mult(tang)).plus(perp.mult(bulge));

		nvgBeginPath(vg);
		nvgMoveTo(vg, a.x, a.y);
		nvgBezierTo(vg, cp1.x, cp1.y, cp2.x, cp2.y, b.x, b.y);
		nvgLineCap(vg, NVG_ROUND);
		// Glow pass
		nvgStrokeColor(vg, nvgRGBAf(col.r, col.g, col.b, highlighted ? 0.45f : 0.25f));
		nvgStrokeWidth(vg, highlighted ? 12.f : 6.f);
		nvgStroke(vg);
		// Core pass
		nvgStrokeColor(vg, nvgRGBAf(col.r, col.g, col.b, 1.f));
		nvgStrokeWidth(vg, highlighted ? 3.f : 1.5f);
		nvgStroke(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !visible || !module || !hostWidget || !screenWidget) return;
		NVGcontext* vg = args.vg;

		Vec origin = hostWidget->box.pos;
		uint8_t currentSet = module->currentSet;
		bool anyHover = (module->vizHoveredId >= 0);

		// Derived from the actual screen widget so moving it can't silently
		// misalign the visualisation splines. Each pad point is a 20x20 square
		// centered in that area.
		const float screenX = screenWidget->box.pos.x;
		const float screenY = screenWidget->box.pos.y;
		const float screenSize = screenWidget->box.size.x;
		const float pointSize = 20.f;

		// Two passes when hovering: dim unrelated connectors first, then draw the
		// hovered connector at full opacity on top.
		auto drawConnection = [&](uint8_t i, bool highlighted, bool dimmed) {
			const auto& src = module->snapshots[currentSet][i];
			if (src.id < 0) return;

			Vec buttonPos = getButtonPos(src.id);
			if (buttonPos == Vec()) return;

			// Pad-point center in module-local coords.
			float x = module->params[TransitPadModule<>::SNAPSHOT_X_POS + i].getValue();
			float y = module->params[TransitPadModule<>::SNAPSHOT_Y_POS + i].getValue();
			Vec padPos = origin.plus(Vec(
				screenX + x * (screenSize - pointSize) + pointSize * 0.5f,
				screenY + y * (screenSize - pointSize) + pointSize * 0.5f
			));

			NVGcolor col = module->setColor[currentSet];
			// Start the spline at the edge of the XY drag circle (radius 10px), not its center.
			Vec dir = buttonPos.minus(padPos).normalize();
			Vec padEdge = padPos.plus(dir.mult(10.f));

			if (dimmed) {
				// Cheap dim: lower alpha on the same glow + core passes.
				nvgSave(vg);
				nvgGlobalAlpha(vg, 0.18f);
				drawSpline(vg, padEdge, buttonPos, col, false, dir);
				nvgRestore(vg);
				nvgBeginPath(vg);
				nvgCircle(vg, buttonPos.x, buttonPos.y, 2.5f);
				nvgFillColor(vg, nvgRGBAf(col.r, col.g, col.b, 0.4f));
				nvgFill(vg);
			}
			else {
				drawSpline(vg, padEdge, buttonPos, col, highlighted, dir);

				// End-point dot at the button.
				nvgBeginPath(vg);
				nvgCircle(vg, buttonPos.x, buttonPos.y, highlighted ? 4.5f : 3.5f);
				nvgFillColor(vg, nvgRGBAf(col.r, col.g, col.b, highlighted ? 1.f : 0.9f));
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGBAf(1.f, 1.f, 1.f, highlighted ? 0.9f : 0.6f));
				nvgStrokeWidth(vg, highlighted ? 1.2f : 0.8f);
				nvgStroke(vg);
			}
		};

		const int snapshotsUsed = module->snapshotsUsed.load(std::memory_order_relaxed);
		if (anyHover) {
			// Pass 1: draw all non-hovered connectors dimmed.
			for (uint8_t i = 0; i < snapshotsUsed; i++) {
				if (i == (uint8_t)module->vizHoveredId) continue;
				drawConnection(i, false, true);
			}
			// Pass 2: draw the hovered connector highlighted.
			drawConnection((uint8_t)module->vizHoveredId, true, false);
		}
		else {
			// No hover — draw every connector at normal opacity.
			for (uint8_t i = 0; i < snapshotsUsed; i++) {
				drawConnection(i, false, false);
			}
		}
	}
};


struct TransitPadWidget : ThemedModuleWidget<TransitPadModule<>> {
	typedef TransitPadModule<> MODULE;
	TransitPadVizOverlay* vizOverlay = nullptr;
	TransitPadXyScreenWidget<MODULE>* screenWidget = nullptr;

	TransitPadWidget(MODULE* module) : ThemedModuleWidget<MODULE>(module, "TransitPad") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(21.0f, 327.0f), module, MODULE::OUT_SEQ_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(112.5f, 327.0f), module, MODULE::OUT_SEQ_PH_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(84.5f, 327.0f), module, MODULE::OUT_X_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(140.5f, 327.0f), module, MODULE::OUT_Y_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(204.3f, 327.0f), module, MODULE::SET_CV_INPUT));

		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(77.6f, 309.8f), module, MODULE::OUT_X_POS));
		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(147.4f, 309.8f), module, MODULE::OUT_Y_POS));

		// +3: compensates for XyScreenWidget's background bleeding 3px past its
		// own box, or it lands flush against the header.
		screenWidget = new TransitPadXyScreenWidget<MODULE>(module, MODULE::SNAPSHOT_X_POS, MODULE::SNAPSHOT_Y_POS, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		screenWidget->box.pos = Vec(3.f, 39.4f);
		screenWidget->box.size = Vec(225.f - 6.f, 225.f - 6.f);
		addChild(screenWidget);

		XySeqEditWidget<MODULE>* seqEditWidget = new XySeqEditWidget<MODULE>(module, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		seqEditWidget->box.pos = screenWidget->box.pos;
		seqEditWidget->box.size = screenWidget->box.size;
		addChild(seqEditWidget);

		// +3: lines up with the bottom bevel XyScreenWidget draws 3px below its
		// box; +4 on top of that is a visible gap between the screen and the row.
		TransitPadButtonRow<MODULE>* buttonRow = new TransitPadButtonRow<MODULE>(module);
		buttonRow->box.pos = Vec(screenWidget->box.pos.x, screenWidget->box.pos.y + screenWidget->box.size.y + 3.f + 4.f);
		buttonRow->box.size = Vec(screenWidget->box.size.x, 32.f * (2.f / 3.f));
		buttonRow->createButtons();
		addChild(buttonRow);

		TransitPadXySeqLedDisplay<MODULE>* seqDisplay1 = createWidget<TransitPadXySeqLedDisplay<MODULE>>(Vec(41.5f, 329.8f));
		seqDisplay1->box.size = Vec(20.4f, 13.2f);
		seqDisplay1->module = module;
		seqDisplay1->id = 0;
		addChild(seqDisplay1);

		if (module) {
			vizOverlay = new TransitPadVizOverlay;
			vizOverlay->module = module;
			vizOverlay->hostWidget = this;
			vizOverlay->screenWidget = screenWidget;
			vizOverlay->visible = false;
			APP->scene->rack->addChild(vizOverlay);
		}
	}

	~TransitPadWidget() {
		if (vizOverlay) {
			APP->scene->rack->removeChild(vizOverlay);
			delete vizOverlay;
			vizOverlay = nullptr;
		}
	}

	void step() override {
		if (vizOverlay && module) vizOverlay->visible = module->vizMode;
		ThemedModuleWidget<TransitPadModule<>>::step();
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (module && e.key == GLFW_KEY_SPACE && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			module->vizMode = !module->vizMode;
			e.consume(this);
			return;
		}
		ThemedModuleWidget<MODULE>::onHoverKey(e);
	}

	// Snapshot-set options (number of snapshots, set CV mode, node-position
	// mode, lock) live on the screen widget's own context menu, but a user
	// right-clicking the module elsewhere shouldn't have to find the screen
	// first -- so mirror them here too.
	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		if (module && screenWidget) screenWidget->appendContextMenu(menu);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitPad = createModel<StoermelderPackOne::Transit::TransitPadModule<8>, StoermelderPackOne::Transit::TransitPadWidget>("TransitPad");