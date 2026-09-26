#include <atomic>
#include "../../plugin.hpp"
#include "../../components/XyScreenWidget.hpp"
#include "../../components/XySeqWidget.hpp"
#include "../../components/Knobs.hpp"
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

// True for every NODEPOSMODE value a valid preset can contain. An unknown
// value must not be stored: changeSet() tests `!= OFF` and `== AUTO`, so it
// would silently behave as Store while no context-menu entry shows a
// checkmark, leaving the user no way to see or change the active mode.
inline bool isValidNodePosMode(int mode) {
	return mode == (int)NODEPOSMODE::OFF
		|| mode == (int)NODEPOSMODE::STORE
		|| mode == (int)NODEPOSMODE::AUTO;
}

template <uint8_t SNAPSHOTS = 8, uint8_t SETS = 8>
struct TransitPadModule : Module, TransitPadInterface, XyScreenModule<SNAPSHOTS>, XyScreenCursor, XySeqModule<1>, ModuleChangeListener {
	struct TransitPadSetParamQuantity : SwitchQuantity {
		TransitPadModule<SNAPSHOTS, SETS>* tpModule = NULL;
		int id = -1;

		std::string getLabel() override {
			if (tpModule && id >= 0) return tpModule->getSetLabel(id);
			return name;
		}

		std::string getDisplayValueString() override {
			if (tpModule && id >= 0 && tpModule->currentSet == id) return "Active";
			return "";
		}
	};

	enum ParamIds {
		ENUMS(SNAPSHOT_X_POS, SNAPSHOTS),
		ENUMS(SNAPSHOT_Y_POS, SNAPSHOTS),
		OUT_X_POS,
		OUT_Y_POS,
		ENUMS(SET_PARAM, SETS),
		ON_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		MIX_X_INPUT,
		MIX_Y_INPUT,
		SEQ_INPUT,
		SEQ_PH_INPUT,
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

	// [Stored to JSON]
	int panelTheme = 0;

	// [Stored to JSON] Written from the UI thread (context menu via
	// createValuePtrMenuItem, dataFromJson) and read from the engine thread (process())
	std::atomic<int> snapshotsUsed{SNAPSHOTS};

	float dist[SNAPSHOTS];

	float inputInX[SNAPSHOTS];
	float inputInY[SNAPSHOTS];

	float outUiX, outInX;
	dsp::ExponentialFilter outXfilter;
	float outUiY, outInY;
	dsp::ExponentialFilter outYfilter;

	// [Stored to JSON] Written by the engine thread (process(): set-CV and
	// the button scan) and the UI thread (dataFromJson); read by the UI thread
	// (context menus, drawLayer, getItemLabel) and by TRANSIT's engine-side
	// presetProcessXyPad through getPadFactors(). The most actively written of
	// the shared fields, so it is atomic like snapshotsUsed and setCvMode.
	std::atomic<int> currentSet{0};
	// [Stored to JSON] Written from the UI thread (context menu via 
	// createValuePtrMenuItem, dataFromJson) and read from the engine thread (process()).
	std::atomic<SETCVMODE> setCvMode{SETCVMODE::TRIG_FWD};
	dsp::SchmittTrigger setCvTrigger;
	// Set last selected by the VOLT/C4 CV, or -1 to apply the CV on the next tick.
	int setCvLast = -1;
	// [Stored to JSON] written from the UI thread (context menu, dataFromJson),
	// read from the engine thread (process(), on set change) and the UI thread.
	std::atomic<NODEPOSMODE> nodePosMode{NODEPOSMODE::OFF};
	std::vector<TransitPadSource> snapshots[SETS];
	// [Stored to JSON] per-set Mix-cursor position; used only when nodePosMode != OFF.
	float mixX[SETS], mixY[SETS];
	NVGcolor setColor[SETS];
	// [Stored to JSON] per-set custom label; empty string means "use default"
	std::string setLabel[SETS];
	// Set last copied via the "Copy" context-menu item, or -1. Not persisted.
	int setCopy = -1;

	// [Stored to JSON] when true, pad drag and drop-binding are disabled.
	bool locked = false;

	// [Stored to JSON] written from the UI thread (context menu, dataFromJson),
	// read from the engine thread (process(), on set change) and the UI thread.
	// When true, the selected motion sequence (Seq::seqSelected[0]) is captured
	// per set on changeSet(), same as nodePosMode's AUTO behaviour for pad
	// geometry.
	bool seqSwitchMode = false;
	// [Stored to JSON] per-set selected motion sequence; used only when
	// seqSwitchMode is true.
	int setSeqSelected[SETS];

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

		registerModuleListener("Transit", this);
		moduleChangedFlag = true;

		for (uint8_t s = 0; s < SETS; s++) {
			setButtonTrigger[s].s = dsp::BooleanTrigger::LOW;
		}

		for (uint8_t s = 0; s < SETS; s++) {
			TransitPadSetParamQuantity* q = configSwitch<TransitPadSetParamQuantity>(SET_PARAM + s, 0.0f, 1.0f, 0.0f, string::f("Snapshot-set #%i", s + 1));
			q->tpModule = this;
			q->id = s;
			q->randomizeEnabled = false;
		}

		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 0, 0.0f, 1.0f, 0.0f, "Snapshot A x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 0, 0.0f, 1.0f, 0.0f, "Snapshot A y-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 1, 0.0f, 1.0f, 1.0f, "Snapshot B x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 1, 0.0f, 1.0f, 0.0f, "Snapshot B y-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 2, 0.0f, 1.0f, 1.0f, "Snapshot C x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 2, 0.0f, 1.0f, 1.0f, "Snapshot C y-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 3, 0.0f, 1.0f, 0.0f, "Snapshot D x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 3, 0.0f, 1.0f, 1.0f, "Snapshot D y-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 4, 0.0f, 1.0f, 0.3f, "Snapshot E x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 4, 0.0f, 1.0f, 0.3f, "Snapshot E y-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 5, 0.0f, 1.0f, 0.7f, "Snapshot F x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 5, 0.0f, 1.0f, 0.3f, "Snapshot F y-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 6, 0.0f, 1.0f, 0.7f, "Snapshot G x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 6, 0.0f, 1.0f, 0.7f, "Snapshot G y-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 7, 0.0f, 1.0f, 0.3f, "Snapshot H x-pos")->randomizeEnabled = false;
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 7, 0.0f, 1.0f, 0.7f, "Snapshot H y-pos")->randomizeEnabled = false;

		configSwitch(ON_PARAM, 0.f, 1.f, 1.f, "Pad active", {"Off", "On"})->randomizeEnabled = false;
		paramQuantities[ON_PARAM]->description = "Set it to Off for changing snapshots on TRANSIT (Space).";
		PortInfo* pi;
		pi = configInput(MIX_X_INPUT, "Mix x-pos");
		pi->description = "Sets the x-position of the Mix node by CV (-5..+5V).";
		pi = configInput(MIX_Y_INPUT, "Mix y-pos");
		pi->description = "Sets the y-position of the Mix node by CV (-5..+5V).";
		pi = configInput(SEQ_INPUT, "Mix sequence select");
		pi->description = "Configure the behavior using the context menu.";
		pi = configInput(SEQ_PH_INPUT, "Mix sequence phase");
		pi->description = "Use this to scan a sequence from start the end using CV (0..10V).";
		pi = configInput(SET_CV_INPUT, "Snapshot-set select CV");
		pi->description = "Configure the behavior using the context menu.";
		configParam<XyScreenParamQuantity>(OUT_X_POS, 0.0f, 1.0f, 0.5f, "Mix x-pos");
		paramQuantities[OUT_X_POS]->description = "Intended for MIDI-mapping.";
		configParam<XyScreenParamQuantity>(OUT_Y_POS, 0.0f, 1.0f, 0.5f, "Mix y-pos");
		paramQuantities[OUT_Y_POS]->description = "Intended for MIDI-mapping.";

		for (uint8_t s = 0; s < SETS; s++) {
			snapshots[s].resize(SNAPSHOTS);
		}
		ResetEvent re;
		onReset(re);
	}

	~TransitPadModule() {
		unregisterModuleListener("Transit", this);
	}

	void onSampleRateChange(const Module::SampleRateChangeEvent& e) override {
		buttonDivider.setDivision(e.sampleRate / 1000.f);
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		// Sets moduleChangedFlag on this pad too, triggering updateMasterModule()
		// on the next process() tick.
		notifyModuleListeners("Transit");
	}

	void onReset(const ResetEvent& e) override {
		Sc::selection = XyScreenSelection();
		init();
		snapshotsUsed = 4;
		currentSet = 0;
		setCvMode.store(SETCVMODE::TRIG_FWD, std::memory_order_relaxed);
		nodePosMode.store(NODEPOSMODE::OFF, std::memory_order_relaxed);
		locked = false;
		seqSwitchMode = false;

		for (uint8_t s = 0; s < SETS; s++) {
			setLabel[s] = "";
			setSeqSelected[s] = 0;
		}

		Sc::resetNodes();
		Seq::seqReset();
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		// Only the active pad points, matching the rest of the module's
		// convention that anything at id >= snapshotsUsed is neither drawn nor
		// draggable and must not be touched (see setSnapshotsUsed()).
		const int n = snapshotsUsed.load(std::memory_order_relaxed);
		for (int i = 0; i < n; i++) {
			Sc::nodes.setXyImmediate(i, random::uniform(), random::uniform());
			Sc::nodes.setRadiusImmediate(i, random::uniform());
			Sc::nodes.setAmountImmediate(i, random::uniform());
		}

		// Also rebind each active point to one of the host TRANSIT's used slots
		// (or a +T's), so a randomize actually reshuffles which presets the pad
		// blends between -- not just where on the screen the same bindings sit.
		// Without a connected master, or with nothing saved anywhere in the
		// chain, there is nothing sensible to bind to, so bindings are left as
		// they are.
		if (masterModule) {
			std::vector<int> usedSlots;
			int slotCount = masterModule->getSlotCount();
			for (int i = 0; i < slotCount; i++) {
				if (masterModule->isSlotUsed(i)) usedSlots.push_back(i);
			}
			if (!usedSlots.empty()) {
				for (int i = 0; i < n; i++) {
					int pick = usedSlots[random::u32() % usedSlots.size()];
					bindSnapshot(i, pick);
				}
			}
		}

		Module::onRandomize(e);
	}

	// Walks left through any chain of +T expanders to find the host TRANSIT.
	// Needed because removing a Transit two or more hops away only notifies
	// its direct +T neighbour via onExpanderChange, not this pad, so
	// masterModule must be re-resolved rather than cleared by the destructor.
	void updateMasterModule() {
		Module* m = leftExpander.module;
		int c = 0;
		while (m) {
			if (m->model == modelTransit) {
				masterModule = dynamic_cast<TransitPadMaster*>(m);
				return;
			}
			if (m->model != modelTransitEx) break;
			m = m->leftExpander.module;
			c++;
			if (c > 15) break;
		}
		masterModule = nullptr;
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

	/** TransitPadInterface: the "Pad active" switch. */
	bool isPadActive() override {
		return params[ON_PARAM].getValue() > 0.5f;
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

	// Resets snapshot i of set s to its factory pad-point geometry (x/y/radius/
	// amount), the part shared by every "reset to defaults" call site below.
	void resetSnapshotGeometry(uint8_t s, uint8_t i) {
		snapshots[s][i].x = getNodePqX(i)->getDefaultValue();
		snapshots[s][i].y = getNodePqY(i)->getDefaultValue();
		snapshots[s][i].radius = getNodeRadiusDefault(i);
		snapshots[s][i].amount = Sc::getNodeAmountDefault(i);
	}

	// Resets snapshot i of set s to full factory defaults: binding (A-D ->
	// slots 0-3, the rest unbound), weight, and pad-point geometry.
	void resetSnapshotDefaults(uint8_t s, uint8_t i) {
		snapshots[s][i].id = i < 4 ? i : -1;
		snapshots[s][i].weight = 0.f;
		resetSnapshotGeometry(s, i);
	}

	// Copies set s's snapshot bindings into set t -- not color or label, which
	// stay per-set identity, not part of the "content" of a set. The pad-point
	// geometry and Mix cursor (x/y/radius/amount, mixX/mixY) are only
	// meaningful while node-position mode is on, so they're copied along with
	// the bindings in that case and left untouched otherwise. Same for the
	// stored motion-sequence selection under seqSwitchMode.
	void copySet(uint8_t s, uint8_t t) {
		bool copyPositions = nodePosMode.load(std::memory_order_relaxed) != NODEPOSMODE::OFF;
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			snapshots[t][i].id = snapshots[s][i].id;
			if (copyPositions) {
				snapshots[t][i].x = snapshots[s][i].x;
				snapshots[t][i].y = snapshots[s][i].y;
				snapshots[t][i].radius = snapshots[s][i].radius;
				snapshots[t][i].amount = snapshots[s][i].amount;
			}
		}
		if (copyPositions) {
			mixX[t] = mixX[s];
			mixY[t] = mixY[s];
		}
		if (seqSwitchMode) {
			setSeqSelected[t] = setSeqSelected[s];
		}

		if (t == currentSet) {
			if (copyPositions) loadNodePositions(t);
			if (seqSwitchMode) Seq::seqSelected[0] = setSeqSelected[t];
		}
	}

	// Resets set s back to the same factory defaults initExtra() seeds a
	// freshly constructed module with: snapshot bindings (A-D -> slots 0-3,
	// the rest unbound), pad-point geometry, the set's Mix cursor position,
	// its default palette color, its label, and its stored motion-sequence
	// selection.
	void resetSet(uint8_t s) {
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			resetSnapshotDefaults(s, i);
		}
		mixX[s] = paramQuantities[OUT_X_POS]->getDefaultValue();
		mixY[s] = paramQuantities[OUT_Y_POS]->getDefaultValue();
		setColor[s] = colors[s % colors.size()].first;
		setLabel[s] = "";
		setSeqSelected[s] = 0;

		if (s == currentSet) {
			if (nodePosMode.load(std::memory_order_relaxed) != NODEPOSMODE::OFF) loadNodePositions(s);
			if (seqSwitchMode) Seq::seqSelected[0] = setSeqSelected[s];
		}
	}

	// Changes the number of active snapshot points. Newly-activated ones (in
	// every set) reset to defaults; deactivated ones keep their geometry but
	// have their weight cleared, since nothing else re-derives it.
	void setSnapshotsUsed(int n) {
		int oldUsed = snapshotsUsed.load(std::memory_order_relaxed);
		snapshotsUsed = n;
		for (int i = oldUsed; i < n; i++) {
			for (uint8_t s = 0; s < SETS; s++) {
				resetSnapshotDefaults(s, i);
			}
		}
		for (int i = n; i < oldUsed; i++) {
			for (uint8_t s = 0; s < SETS; s++) {
				snapshots[s][i].weight = 0.f;
			}
		}
	}

	// Reset every set's stored layout to defaults, so switching mode to Off
	// doesn't leave stale geometry a later Store/Auto could resurrect.
	void clearNodePositions() {
		for (uint8_t s = 0; s < SETS; s++) {
			for (uint8_t i = 0; i < SNAPSHOTS; i++) {
				resetSnapshotGeometry(s, i);
			}
			mixX[s] = paramQuantities[OUT_X_POS]->getDefaultValue();
			mixY[s] = paramQuantities[OUT_Y_POS]->getDefaultValue();
		}
	}

	// Toggles seqSwitchMode. Enabling it seeds every set's stored selection
	// from whatever sequence is live right now, so switching this on doesn't
	// yank unrelated (not-yet-visited) sets back to sequence 0 the moment the
	// user first changes sets.
	void setSeqSwitchMode(bool on) {
		if (on) {
			for (uint8_t s = 0; s < SETS; s++) {
				setSeqSelected[s] = Seq::seqSelected[0];
			}
		}
		seqSwitchMode = on;
	}

	// Switches the active set. A no-op when newSet == currentSet, so a
	// repeated request never reloads the stored layout. Use reloadCurrentSet()
	// to force a reload of the set that's already active.
	void changeSet(int newSet) {
		if (newSet == currentSet) return;
		NODEPOSMODE m = nodePosMode.load(std::memory_order_relaxed);
		if (m == NODEPOSMODE::AUTO) storeNodePositions(currentSet);
		if (seqSwitchMode) setSeqSelected[currentSet] = Seq::seqSelected[0];
		currentSet = newSet;
		if (m != NODEPOSMODE::OFF) loadNodePositions(currentSet);
		if (seqSwitchMode) Seq::seqSelected[0] = setSeqSelected[currentSet];
	}

	// VOLT/C4 CV paths: follow the CV only when the set it selects changes, so a
	// set-button press sticks until the CV moves on to another set instead of
	// being undone on the very next sample.
	void changeSetByCv(int newSet) {
		if (newSet == setCvLast) return;
		setCvLast = newSet;
		changeSet(newSet);
	}

	// Reloads the current set's stored layout without changing currentSet or
	// capturing first, so unsaved pad edits (and, under seqSwitchMode, an
	// unsaved sequence-selection change) are discarded on a re-press.
	void reloadCurrentSet() {
		if (nodePosMode.load(std::memory_order_relaxed) != NODEPOSMODE::OFF) loadNodePositions(currentSet);
		if (seqSwitchMode) Seq::seqSelected[0] = setSeqSelected[currentSet];
	}

	void process(const ProcessArgs& args) override {
		if (moduleChangedFlag) {
			updateMasterModule();
			moduleChangedFlag = false;
		}

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
					changeSetByCv(std::min(s, (int)SETS - 1));
					break;
				}
				case SETCVMODE::C4:
					changeSetByCv(clamp((int)std::round(inputs[SET_CV_INPUT].getVoltage() * 12.f), 0, (int)SETS - 1));
					break;
			}
		}
		else {
			setCvLast = -1;
		}
		if (mode != SETCVMODE::VOLT && mode != SETCVMODE::C4) {
			setCvLast = -1;
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

		if (inputs[SEQ_INPUT].isConnected()) {
			Seq::seqProcess(inputs[SEQ_INPUT], 0);
		}

		bool setX = false, setY = false;

		if (inputs[SEQ_PH_INPUT].isConnected()) {
			float v = clamp(inputs[SEQ_PH_INPUT].getVoltage() / 10.f, 0.f, 1.f);
			Vec d = Seq::seqValue(0, v);
			params[OUT_X_POS].setValue(d.x);
			setX = true;
			params[OUT_Y_POS].setValue(d.y);
			setY = true;
		}

		if (!setX && inputs[MIX_X_INPUT].isConnected()) {
			float x = inputs[MIX_X_INPUT].getVoltage() / 10.f;
			x += 0.5f;
			x = clamp(x, 0.f, 1.f);
			params[OUT_X_POS].setValue(x);
			setX = true;
		} 

		if (!setY && inputs[MIX_Y_INPUT].isConnected()) {
			float y = inputs[MIX_Y_INPUT].getVoltage() / 10.f;
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

		// Snapshots above the active count are not drawn and not draggable, so
		// their connector-line distance must not read as in-range either. Their
		// weight is cleared once, when they're deactivated -- see setSnapshotsUsed().
		for (int j = n; j < SNAPSHOTS; j++) {
			dist[j] = std::numeric_limits<float>::infinity();
		}

		if (lightDivider.process()) {
			for (uint8_t s = 0; s < SETS; s++) {
				lights[SET_LIGHT + s].setBrightness(currentSet == s ? 1.0f : 0.0f);
			}
		}
	}

	// XySeqModule: the only motion-sequence port is the Out cursor's (index 0).
	bool seqPortHidden(int port) override {
		return port != 0;
	}

	// XyScreenModule: one-time setup for the Out (cursor) point, called from initNodes().
	void initExtra() override {
		setCursorXyImmediate(0, paramQuantities[OUT_X_POS]->getDefaultValue(), paramQuantities[OUT_Y_POS]->getDefaultValue());
		outXfilter.setTau(0.05f);
		outYfilter.setTau(0.05f);
		for (uint8_t s = 0; s < SETS; s++) {
			for (uint8_t i = 0; i < SNAPSHOTS; i++) {
				dist[i] = std::numeric_limits<float>::infinity();
				// Sc::nodes isn't reset yet here, so seed from the defaults directly.
				resetSnapshotDefaults(s, i);
			}
			mixX[s] = paramQuantities[OUT_X_POS]->getDefaultValue();
			mixY[s] = paramQuantities[OUT_Y_POS]->getDefaultValue();
			setColor[s] = colors[s % colors.size()].first;
			setLabel[s] = "";
		}
	}

	// XyScreenModule: how many of the SNAPSHOTS nodes are currently active. */
	inline uint8_t nodeCountActive() override {
		return (uint8_t)snapshotsUsed.load(std::memory_order_relaxed);
	}

	// XyScreenModule: the node (snapshot) x-position param.
	engine::ParamQuantity* getNodePqX(uint8_t id) override {
		return paramQuantities[SNAPSHOT_X_POS + id];
	}

	// XyScreenModule: the node (snapshot) y-position param.
	engine::ParamQuantity* getNodePqY(uint8_t id) override {
		return paramQuantities[SNAPSHOT_Y_POS + id];
	}

	// XyScreenCursor: the single Out cursor.
	uint8_t cursorCount() const override {
		return 1;
	}

	// XyScreenCursor: the param-backed x-position the Out cursor widget draws.
	float getCursorXFinal(uint8_t id) const override {
		return paramQuantities[OUT_X_POS]->getParam()->getValue();
	}

	// XyScreenCursor: the param-backed y-position the Out cursor widget draws.
	float getCursorYFinal(uint8_t id) const override {
		return paramQuantities[OUT_Y_POS]->getParam()->getValue();
	}

	// XyScreenCursor: write the Out cursor's position immediately (drag end,
	// undo/redo). Only id 0 (the one cursor) is valid; anything else is a
	// silent no-op, matching the codebase's convention for bad indices.
	void setCursorXyImmediate(uint8_t id, float x, float y) override {
		if (id >= 1) return;
		paramQuantities[OUT_X_POS]->getParam()->setValue(x);
		outXfilter.out = outUiX = x;
		paramQuantities[OUT_Y_POS]->getParam()->setValue(y);
		outYfilter.out = outUiY = y;
	}

	// XyScreenCursor: write the Out cursor's position through the UI filter (live drag).
	void setCursorXyFiltered(uint8_t id, float x, float y) override {
		if (id >= 1) return;
		outUiX = x;
		outUiY = y;
	}

	// XyScreenModule: distance from the Out cursor to a snapshot node, for the connector-line draw.
	inline float getCursorToNodeDistance(uint8_t cursorId, uint8_t nodeId) override {
		return dist[nodeId];
	}

	// XyScreenModule: default radius for a new snapshot node.
	inline float getNodeRadiusDefault(uint8_t id) override {
		return 1.f;
	}

	// XyScreenModule: color a snapshot node is drawn with — the active set's color.
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

	// The Transit snapshot slot the given pad point is bound to within the
	// current set, or -1 if unbound.
	int getBoundSlot(int snapshotId) {
		return snapshots[currentSet][snapshotId].id;
	}

	std::string getItemLabel(uint8_t s, uint8_t id) {
		if (masterModule == nullptr) {
			return "<No TRANSIT module>";
		}
		if (snapshots[s][id].id >= 0) {
			std::string custom = masterModule->getSlotLabel(snapshots[s][id].id);
			if (custom != "") {
				return string::f("Snapshot #%i: %s", snapshots[s][id].id + 1, custom.c_str());
			}
			else {
				return string::f("Snapshot #%i", snapshots[s][id].id + 1);
			}
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
		json_object_set_new(rootJ, "seqSwitchMode", json_boolean(seqSwitchMode));

		// Live pad-point layout, independent of any set. Only the active points
		// are meaningful -- anything at or beyond snapshotsUsed is neither drawn
		// nor draggable, and setSnapshotsUsed() resets it to defaults whenever it
		// becomes active again, so persisting it would only inflate the patch.
		int used = snapshotsUsed.load(std::memory_order_relaxed);
		json_t* nodesJ = json_array();
		for (uint8_t i = 0; i < used; i++) {
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
			for (uint8_t i = 0; i < used; i++) {
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
			if (seqSwitchMode) {
				json_object_set_new(setJ, "seqSelected", json_integer(setSeqSelected[s]));
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

		json_t* seqSwitchModeJ = json_object_get(rootJ, "seqSwitchMode");
		if (seqSwitchModeJ) seqSwitchMode = json_is_true(seqSwitchModeJ);

		int su = json_integer_value(json_object_get(rootJ, "snapshotsUsed"));
		setSnapshotsUsed(std::max(1, std::min(su, (int)SNAPSHOTS)));

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
			json_t* seqSelectedJ = json_object_get(setJ, "seqSelected");
			if (seqSelectedJ) setSeqSelected[s] = json_integer_value(seqSelectedJ);
		}

		json_t* outputJ = json_object_get(rootJ, "output");
		Seq::dataFromJson(outputJ, 0);

		// Like the pad-point layout, Seq::seqSelected[0] as restored above is
		// XySeqModule's own single global value; with seqSwitchMode on, the
		// active set's stored selection must win, same as loadNodePositions()
		// wins for the cursor/nodes.
		if (seqSwitchMode) Seq::seqSelected[0] = setSeqSelected[currentSet];

		// Resync the UI-shadow cursor state (outUiX/outXfilter) that process()
		// reads instead of the param; Rack's own param restore doesn't touch it.
		// Always from the restored param, in every nodePosMode: like the pad
		// points (restored from "nodes"), the cursor must come back where it was
		// saved. mixX[currentSet] is only the last capture — in Auto mode it's
		// written on leaving the set, so it's stale whenever the cursor moved since.
		float x = paramQuantities[OUT_X_POS]->getParam()->getValue();
		float y = paramQuantities[OUT_Y_POS]->getParam()->getValue();
		setCursorXyImmediate(0, x, y);
	}
};


// Fixed-position tooltip anchored to a node/cursor's bottom-right corner,
// same as ParamWidget/PortWidget's tooltips (ParamTooltip/PortTooltip in
// Rack's own app/ sources) -- unlike a plain ui::Tooltip, which re-centers
// on the mouse every frame and so drifts while the cursor moves within the
// widget's circular hit area. Shared by every drag widget on the pad screen
// (snapshot nodes and the Mix cursor).
struct TransitPadNodeTooltip : ui::Tooltip {
	widget::Widget* anchor;
	void step() override {
		Tooltip::step();
		box.pos = anchor->getAbsoluteOffset(anchor->box.size).round();
		assert(parent);
		box = box.nudge(parent->box.zeroPos());
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

	// XyScreenDragWidgetBase: single-character label drawn inside this node.
	char getItemChar() override {
		return 'A' + AW::id;
	}

	// XyScreenDragWidgetBase: the base class's default (a dark navy) reads
	// poorly against the pad's own set colors; pick black or white by the
	// node color's own luminance so it stays legible against all of them,
	// including white/bright set colors where a fixed white would not.
	NVGcolor getSelectedTextColor(NVGcolor cc) override {
		float brightness = cc.r * 0.299f + cc.g * 0.587f + cc.b * 0.114f;
		return brightness > 0.5f ? nvgRGB(0x08, 0x08, 0x08) : nvgRGB(0xf0, 0xf0, 0xf0);
	}

	// XyScreenDragWidgetBase: label shown in this node's context menu and tooltip.
 	std::string getItemName() override {
		return AW::module->getItemLabel(AW::module->currentSet, AW::id);
	}

	// XyScreenDragWidgetBase: items prepended to this node's context menu.
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

		int boundSlot = AW::module->getBoundSlot(AW::id);
		bool canLoad = AW::module->masterModule != nullptr && boundSlot >= 0 && AW::module->masterModule->isSlotUsed(boundSlot);
		menu->addChild(createMenuItem("Load snapshot", "", [=]() {
			// Re-check inside the lambda; the chain may have changed between
			// menu construction and click.
			if (!AW::module->masterModule) return;
			int slot = AW::module->getBoundSlot(AW::id);
			if (slot < 0 || !AW::module->masterModule->isSlotUsed(slot)) return;
			// The pad continuously re-drives every bound parameter from its own
			// blend while active, immediately overwriting whatever the load just
			// wrote -- switch it off first so the loaded values actually stick.
			AW::module->params[MODULE::ON_PARAM].setValue(0.f);
			AW::module->masterModule->loadSlot(slot);
		}, !canLoad));

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
			auto* t = new TransitPadNodeTooltip;
			t->anchor = this;
			t->text = getItemName();
			APP->scene->addChild(t);
			tooltip = t;
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
	ui::Tooltip* tooltip = NULL;

	~TransitPadOutDragWidget() {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
		}
	}

	// XyScreenDragWidgetBase: label shown in this cursor's context menu and tooltip.
 	std::string getItemName() override {
		return "Mix";
	}

	// XyScreenDragWidgetBase: single-character label drawn inside this cursor.
	char getItemChar() override {
		return '+';
	}

	// XyScreenDragWidgetBase: extra items appended to this cursor's context menu.
	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Motion-Sequence"));
		menu->addChild(new XySeqSlotMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqInterpolateMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqTriggerMenuItem<MODULE>(B::module, B::id));
		menu->addChild(createMenuItem("Seq-Edit", "", [=]() { B::module->seqEdit = B::id; }));
	}

	void onEnter(const event::Enter& e) override {
		if (!B::isActive()) return;
		if (settings::tooltips && !tooltip) {
			auto* t = new TransitPadNodeTooltip;
			t->anchor = this;
			t->text = getItemName();
			APP->scene->addChild(t);
			tooltip = t;
		}
		B::onEnter(e);
	}

	void onLeave(const event::Leave& e) override {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
			tooltip = NULL;
		}
		B::onLeave(e);
	}
};

template <typename MODULE>
struct TransitPadXyScreenWidget : XyScreenWidget<MODULE> {
	TransitPadXyScreenWidget(MODULE* module, int inParamIdX, int inParamIdY, int mixParamIdX, int mixParamIdY) : XyScreenWidget<MODULE>(module) {
		uint8_t t0 = module ? module->nodeCount() : 4;
		this->template createNodeWidgets<TransitPadSnapshotDragWidget<MODULE>>(module, t0);
		uint8_t t1 = module ? module->cursorCount() : 1;
		this->template createCursorWidgets<TransitPadOutDragWidget<MODULE>>(module, t1);
		// The surrounding TransitPadScreenBevel takes the space of the usual bleed.
		this->bleed = 3.f;
	}

	// Thin border like the LED window of TransitPadSetButton, layered on top
	// of the base class's usual bevel strokes (the same ones Arena's screen
	// draws) -- the surrounding TransitPadScreenBevel is a separate, outer rim
	// and doesn't replace this inner one.
	void drawFrame(const Widget::DrawArgs& args, math::Rect r, NVGcolor bottomColor) override {
		XyScreenWidget<MODULE>::drawFrame(args, r, bottomColor);

		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		nvgStrokeWidth(args.vg, 0.5f);
		nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 53));
		nvgStroke(args.vg);
	}

	void step() override {
		if (this->module) {
			// Preview interpolated automation line if mixport is selected
			this->module->seqPreview = -1;
			for (uint8_t i = 0; i < this->module->cursorCountActive(); i++) {
				if (this->module->selection.isCursor(i)) {
					this->module->seqPreview = i;
				}
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
		if (layer != 1) return;

		// Corner vignette
		math::Rect r = this->box.zeroPos().grow(Vec(this->bleed, this->bleed));
		NVGpaint vignette = nvgRadialGradient(args.vg,
			r.size.x * 0.5f, r.size.y * 0.5f,
			r.size.x * 0.35f, r.size.x * 0.75f,
			nvgRGBAf(0.f, 0.f, 0.f, 0.0f),
			nvgRGBAf(0.f, 0.f, 0.f, 0.15f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		nvgFillPaint(args.vg, vignette);
		nvgFill(args.vg);

		if (!this->module) return;

		if (this->module->isLocked()) {
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

		if (!this->module->isPadActive()) {
			// Dim the whole screen and show an "OFF" label in the same spot and
			// font as XySeqWidget's "SEQ-EDIT" label, so it's clear at a glance
			// that pad edits currently don't reach TRANSIT's param processing.
			NVGcontext* vg = args.vg;

			nvgBeginPath(vg);
			nvgRect(vg, 0.f, 0.f, this->box.size.x, this->box.size.y);
			nvgFillColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.55f));
			nvgFill(vg);

			NVGcolor c = color::mult(color::WHITE, 0.7f);
			std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			nvgFontSize(vg, 22);
			nvgFontFaceId(vg, font->handle);
			nvgTextLetterSpacing(vg, -2.2);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
			nvgFillColor(vg, c);
			nvgTextBox(vg, 8.f, this->box.size.y - 6.f, 120, "OFF", NULL);
		}
	}

	// XyScreenWidget: extra items appended to the whole screen's context menu.
	void appendContextMenu(Menu* menu) override {
		using StoermelderPackOne::Rack::createAtomicValuePtrMenuItem;
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Snapshot-sets"));
		menu->addChild(createSubmenuItem("Number of snapshots", string::f("%i", this->module->snapshotsUsed.load(std::memory_order_relaxed)),
			[=](Menu* menu) {
				MODULE* m = this->module;
				for (int i = 0; i < m->nodeCount(); i++) {
					const int target = i + 1;
					bool checked = m->snapshotsUsed.load(std::memory_order_relaxed) == target;
					menu->addChild(createMenuItem(string::f("%i", target), CHECKMARK(checked), [=]() { m->setSnapshotsUsed(target); }));
				}
			}
		));
		auto setCvModeLabel = [](SETCVMODE m) {
			switch (m) {
				case SETCVMODE::OFF: return "Off";
				case SETCVMODE::TRIG_FWD: return "Trigger forward";
				case SETCVMODE::VOLT: return "0..10V";
				case SETCVMODE::C4: return "C4";
				default: return "";
			}
		};
		menu->addChild(createSubmenuItem("CV port mode", setCvModeLabel(this->module->setCvMode.load(std::memory_order_relaxed)),
			[=](Menu* menu) {
				menu->addChild(createAtomicValuePtrMenuItem(setCvModeLabel(SETCVMODE::OFF), &this->module->setCvMode, SETCVMODE::OFF));
				menu->addChild(new MenuSeparator);
				menu->addChild(createAtomicValuePtrMenuItem(setCvModeLabel(SETCVMODE::TRIG_FWD), &this->module->setCvMode, SETCVMODE::TRIG_FWD));
				menu->addChild(createAtomicValuePtrMenuItem(setCvModeLabel(SETCVMODE::VOLT), &this->module->setCvMode, SETCVMODE::VOLT));
				menu->addChild(createAtomicValuePtrMenuItem(setCvModeLabel(SETCVMODE::C4), &this->module->setCvMode, SETCVMODE::C4));
			}
		));
		auto nodePosModeLabel = [](NODEPOSMODE m) {
			switch (m) {
				case NODEPOSMODE::OFF: return "Off";
				case NODEPOSMODE::STORE: return "Store (manual)";
				case NODEPOSMODE::AUTO: return "Auto (on set change)";
				default: return "";
			}
		};
		menu->addChild(createSubmenuItem("Store node positions", nodePosModeLabel(this->module->nodePosMode.load(std::memory_order_relaxed)),
			[=](Menu* menu) {
				MODULE* m = this->module;
				bool isOff = m->nodePosMode.load(std::memory_order_relaxed) == NODEPOSMODE::OFF;
				menu->addChild(createMenuItem(nodePosModeLabel(NODEPOSMODE::OFF), CHECKMARK(isOff), [=]() {
					m->nodePosMode.store(NODEPOSMODE::OFF, std::memory_order_relaxed);
					m->clearNodePositions();
				}));
				menu->addChild(new MenuSeparator);
				menu->addChild(createAtomicValuePtrMenuItem(nodePosModeLabel(NODEPOSMODE::STORE), &m->nodePosMode, NODEPOSMODE::STORE));
				menu->addChild(createAtomicValuePtrMenuItem(nodePosModeLabel(NODEPOSMODE::AUTO), &m->nodePosMode, NODEPOSMODE::AUTO));
			}
		));
		menu->addChild(createBoolMenuItem("Store motion-sequence", "",
			[=]() { return this->module->seqSwitchMode; },
			[=](bool on) { this->module->setSeqSwitchMode(on); }
		));
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Visualize", "Shift+Space", &this->module->vizMode));
		menu->addChild(createBoolPtrMenuItem("Lock pad", RACK_MOD_SHIFT_NAME "+L", &this->module->locked));
	}
};


// Bevel around the screen, drawn like the body of TransitPadSetButton (rim,
// bezel and cap of VCVButton) with the screen taking the place of the LED.
// Drawn on the panel layer so it dims with the room lights like the panel.
struct TransitPadScreenBevel : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		math::Rect r = box.zeroPos();

		// Outer black rim
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(r), 3.5f);
		nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
		nvgFill(args.vg);

		// Bezel, lit from the top
		math::Rect rb = r.shrink(Vec(0.75f, 0.75f));
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(rb), 2.9f);
		nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, rb.pos.y, 0.f, rb.pos.y + rb.size.y, nvgRGB(0x80, 0x7c, 0x7e), nvgRGB(0x0a, 0x0a, 0x0a)));
		nvgFill(args.vg);

		// Cap
		math::Rect rc = r.shrink(Vec(1.63f, 1.63f));
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(rc), 2.3f);
		nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, rc.pos.y, 0.f, rc.pos.y + rc.size.y, nvgRGB(0x4a, 0x47, 0x47), nvgRGB(0x1f, 0x1f, 0x1f)));
		nvgFill(args.vg);

		Widget::draw(args);
	}
};


template <typename MODULE>
struct TransitPadXySeqLedDisplay : XySeqLedDisplay<MODULE> {
	ui::Tooltip* tooltip = NULL;

	~TransitPadXySeqLedDisplay() {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
		}
	}

	// XySeqLedDisplay: label shown for this port's motion-sequence editor.
	std::string getPortName() override {
		return "Mix";
	}

	// StoermelderLedDisplay derives from LightWidget/TransparentWidget, whose
	// onHover() is a no-op that never consumes the event -- so onEnter/onLeave
	// (and with them, the tooltip) are never dispatched here without this
	// override consuming it, the same way OpaqueWidget::onHover() does.
	void onHover(const event::Hover& e) override {
		Widget::onHover(e);
		e.stopPropagating();
		if (!e.isConsumed()) e.consume(this);
	}

	void onEnter(const event::Enter& e) override {
		if (settings::tooltips && !tooltip) {
			auto* t = new TransitPadNodeTooltip;
			t->anchor = this;
			t->text = "Mix motion-sequence slot\nClick to enter Seq-Edit for sequence editing.";
			APP->scene->addChild(t);
			tooltip = t;
		}
		XySeqLedDisplay<MODULE>::onEnter(e);
	}

	void onLeave(const event::Leave& e) override {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
			tooltip = NULL;
		}
		XySeqLedDisplay<MODULE>::onLeave(e);
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(createMenuItem("Seq-Edit", "", [=]() { 
			XySeqLedDisplay<MODULE>::module->seqEdit = XySeqLedDisplay<MODULE>::id;
		}));
	}
};


// Rectangular snapshot-set button, custom-drawn with nanovg in the style of
// TRANSIT's snapshot buttons (VCVButton plus a 3mm LED): same bezel and cap
// gradients, with the round LED stretched into a rectangular window.
template <typename MODULE>
struct TransitPadSetButton : app::Switch {
	MODULE* module;
	size_t setIndex;

	TransitPadSetButton() {
		momentary = true;
	}

	// Outline of the button body, leaving a small gap to the neighboring cells.
	math::Rect getBodyRect() {
		return math::Rect(Vec(0.f, 0.f), box.size).shrink(Vec(1.2f, 0.f));
	}

	// LED window, inset on all sides by the margin of the 3mm LED on the 18px
	// VCVButton. The margin stays fixed so the LED grows with the button
	// instead of the bezel and cap getting thicker.
	math::Rect getLedRect() {
		const float margin = (18.f - mm2px(3.f)) / 2.f;
		return getBodyRect().shrink(Vec(margin, margin));
	}

	void draw(const DrawArgs& args) override {
		ParamQuantity* pq = getParamQuantity();
		bool pressed = pq && pq->getValue() > 0.5f;
		math::Rect r = getBodyRect();

		// Outer black rim
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(r), 3.5f);
		nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
		nvgFill(args.vg);

		// Bezel, lit from the top
		math::Rect rb = r.shrink(Vec(0.75f, 0.75f));
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(rb), 2.9f);
		nvgFillPaint(args.vg, pressed ?
			nvgLinearGradient(args.vg, 0.f, rb.pos.y, 0.f, rb.pos.y + rb.size.y, nvgRGB(0x2b, 0x2b, 0x2b), nvgRGB(0x0d, 0x0c, 0x0c)) :
			nvgLinearGradient(args.vg, 0.f, rb.pos.y, 0.f, rb.pos.y + rb.size.y, nvgRGB(0x80, 0x7c, 0x7e), nvgRGB(0x0a, 0x0a, 0x0a)));
		nvgFill(args.vg);

		// Cap
		math::Rect rc = r.shrink(Vec(1.63f, 1.63f));
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(rc), 2.3f);
		if (pressed) {
			nvgFillColor(args.vg, nvgRGB(0x26, 0x26, 0x26));
		}
		else {
			nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, rc.pos.y, 0.f, rc.pos.y + rc.size.y, nvgRGB(0x4a, 0x47, 0x47), nvgRGB(0x1f, 0x1f, 0x1f)));
		}
		nvgFill(args.vg);

		// LED background, same colors as GrayModuleLightWidget
		math::Rect rl = getLedRect();
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, RECT_ARGS(rl), 1.5f);
		nvgFillColor(args.vg, nvgRGB(0x33, 0x33, 0x33));
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, 0.5f);
		nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 53));
		nvgStroke(args.vg);

		ParamWidget::draw(args);
	}

	// The LED itself is drawn on the light layer, like any LightWidget, so it
	// stays bright when the room lights are dimmed.
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			// Inactive sets stay faintly visible in their own color.
			float brightness = module ? module->lights[MODULE::SET_LIGHT + setIndex].getBrightness() : 1.f;
			NVGcolor col = module ? module->setColor[setIndex] : colors[setIndex % colors.size()].first;
			col = color::mult(col, 0.12f + 0.88f * brightness);
			col.a = 1.f;
			math::Rect rl = getLedRect();

			// Same blending as LightWidget::drawLayer
			nvgGlobalCompositeBlendFunc(args.vg, NVG_ONE_MINUS_DST_COLOR, NVG_ONE);

			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, RECT_ARGS(rl), 1.5f);
			nvgFillColor(args.vg, col);
			nvgFill(args.vg);

			// Halo, following LightWidget::drawHalo but shaped by the rectangle
			const float halo = settings::haloBrightness;
			if (!args.fb && halo > 0.f) {
				float feather = std::min(rl.size.y * 2.5f, 30.f);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, RECT_ARGS(rl.grow(Vec(feather, feather))));
				nvgFillPaint(args.vg, nvgBoxGradient(args.vg, RECT_ARGS(rl), 1.5f, feather, color::mult(col, halo), nvgRGBA(0, 0, 0, 0)));
				nvgFill(args.vg);
			}

			nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);
		}
		ParamWidget::drawLayer(args, layer);
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

			menu->addChild(createMenuItem("Reset label", "", [=]() { m->setLabel[s] = ""; }));
		}));
		NODEPOSMODE nodePosMode = module->nodePosMode.load(std::memory_order_relaxed);
		if (nodePosMode != NODEPOSMODE::OFF) {
			// Disabled outside manual Store mode: Auto already captures on switch.
			menu->addChild(createMenuItem("Store positions", "", [=]() { m->storeNodePositions(s); }, nodePosMode != NODEPOSMODE::STORE));
		}
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Copy", "", [=]() { m->setCopy = s; }));

		struct PasteItem : MenuItem {
			MODULE* module;
			size_t setIndex;
			void step() override {
				int i = module->setCopy;
				rightText = i >= 0 ? string::f("Set %d", i + 1) : "";
				disabled = i < 0 || (size_t)i == setIndex;
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				module->copySet(module->setCopy, setIndex);
			}
		};
		menu->addChild(construct<PasteItem>(&MenuItem::text, "Paste", &PasteItem::module, m, &PasteItem::setIndex, s));
		menu->addChild(createMenuItem("Reset", "", [=]() { m->resetSet(s); }));
		menu->addChild(new MenuSeparator());
		for (size_t i = 0; i < module->nodeCountActive(); i++) {
			menu->addChild(createMenuLabel(module->getItemLabel(setIndex, i)));
		}
	}
};


// Row of the 8 snapshot-set buttons below the screen.
template <typename MODULE>
struct TransitPadButtonRow : widget::Widget {
	MODULE* module;

	TransitPadButtonRow(MODULE* module) {
		this->module = module;
	}

	// One grid cell per button, matching XyScreenWidget's 8-column grid above.
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

		addParam(createParamCentered<CKSSH>(Vec(21.7f, 292.3f), module, MODULE::ON_PARAM));

		addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 327.0f), module, MODULE::SET_CV_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(203.0f, 327.0f), module, MODULE::SEQ_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(112.5f, 327.0f), module, MODULE::SEQ_PH_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(84.5f, 327.0f), module, MODULE::MIX_X_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(140.5f, 327.0f), module, MODULE::MIX_Y_INPUT));

		addParam(createParamCentered<XyScreenMapWidget<StoermelderTrimpot>>(Vec(60.5f, 327.0f), module, MODULE::OUT_X_POS));
		addParam(createParamCentered<XyScreenMapWidget<StoermelderTrimpot>>(Vec(164.5f, 327.0f), module, MODULE::OUT_Y_POS));

		screenWidget = new TransitPadXyScreenWidget<MODULE>(module, MODULE::SNAPSHOT_X_POS, MODULE::SNAPSHOT_Y_POS, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		screenWidget->box.pos = Vec(8.8f, 40.0f);
		screenWidget->box.size = Vec(207.4f, 207.4f);
		screenWidget->box = screenWidget->box.shrink(4.f);

		// Button row on top, starting where the screen's bevel used to start,
		// with the screen moved down below it.
		const float bevel = 7.f;
		const float gap = 7.f;
		TransitPadButtonRow<MODULE>* buttonRow = new TransitPadButtonRow<MODULE>(module);
		buttonRow->box.pos = Vec(screenWidget->box.pos.x, screenWidget->box.pos.y - bevel);
		buttonRow->box.size = Vec(screenWidget->box.size.x, 20.f);
		screenWidget->box.pos.y = buttonRow->box.pos.y + buttonRow->box.size.y + gap + bevel;

		TransitPadScreenBevel* screenBevel = new TransitPadScreenBevel;
		screenBevel->box = screenWidget->box.grow(Vec(bevel, bevel));
		addChild(screenBevel);
		addChild(screenWidget);

		XySeqEditWidget<MODULE>* seqEditWidget = new XySeqEditWidget<MODULE>(module, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		seqEditWidget->box.pos = screenWidget->box.pos;
		seqEditWidget->box.size = screenWidget->box.size;
		addChild(seqEditWidget);

		buttonRow->createButtons();
		addChild(buttonRow);

		TransitPadXySeqLedDisplay<MODULE>* seqDisplay1 = createWidget<TransitPadXySeqLedDisplay<MODULE>>(Vec(192.8f, 285.7f));
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
		// Hidden while seq-edit is active: the splines would otherwise clutter
		// the pad while it shows the recorded motion-sequence path instead.
		if (vizOverlay && module) vizOverlay->visible = module->vizMode && module->seqEdit < 0;
		ThemedModuleWidget<TransitPadModule<>>::step();
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (module && e.key == GLFW_KEY_SPACE && e.action == GLFW_PRESS) {
			if ((e.mods & RACK_MOD_MASK) == 0) {
				module->params[MODULE::ON_PARAM].setValue(module->isPadActive() ? 0.f : 1.f);
				e.consume(this);
				return;
			}
			if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
				module->vizMode = !module->vizMode;
				e.consume(this);
				return;
			}
		}
		if (module && e.key == GLFW_KEY_L && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			module->locked = !module->locked;
			e.consume(this);
			return;
		}
		if (module && e.key >= GLFW_KEY_1 && e.key <= GLFW_KEY_8 && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			module->changeSet(e.key - GLFW_KEY_1);
			e.consume(this);
			return;
		}
		ThemedModuleWidget<MODULE>::onHoverKey(e);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		if (module && screenWidget) screenWidget->appendContextMenu(menu);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitPad = createModel<StoermelderPackOne::Transit::TransitPadModule<8>, StoermelderPackOne::Transit::TransitPadWidget>("TransitPad");