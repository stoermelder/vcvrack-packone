#include "../../plugin.hpp"
#include "../../components/MatrixButton.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../ui/ModuleSelectProcessor.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../../utils/vcv_cables.hpp"
#include "MidiTrackingProcessor.hpp"
#include "SpliceKit_controllers.hpp"
#include <osdialog.h>

namespace StoermelderPackOne {
namespace SpliceKit {

// Each LED uses a single SCHEME channel in isolation. Mixing channels is avoided
// because SCHEME_BLUE (#29b2ef) already contains G=178/255, which saturates the
// green channel and produces teal/white when combined with the green channel.
static const float LED_BRIGHT = 1.f;
static const float LED_DIM = 0.65f;  		// assigned port, no cable attached
static const float LED_SCENE_DIM = 0.3f;    // inactive scene that has stored connections

static const NVGcolor LED_OFF        = nvgRGBf(0.f,        0.f,        0.f       );
static const NVGcolor LED_PENDING    = nvgRGBf(LED_BRIGHT, LED_BRIGHT, LED_BRIGHT);
static const NVGcolor LED_PORT_LEARN = nvgRGBf(0.7f,       0.7f,       0.7f      );
static const NVGcolor LED_MIDI_LEARN = nvgRGBf(0.7f,       0.7f,       1.f       );

// Four assignable color sets using Rack's standard scheme palette.
//   set 0: red    (default for OUTPUT ports)
//   set 1: blue   (default for INPUT ports)
//   set 2: orange
//   set 3: green
static const int COLOR_SET_COUNT = 4;
struct ColorSet { NVGcolor color; const char* name; };
static const ColorSet COLOR_SETS[COLOR_SET_COUNT] = {
	{ SCHEME_RED,               "Red"    },
	{ nvgRGB(0x10, 0x60, 0xff), "Blue"   },
	{ SCHEME_ORANGE,            "Orange" },
	{ SCHEME_GREEN,             "Green"  }
};

// Per-color-set LED state lookups, named explicitly rather than computed via
// enum-stride arithmetic (e.g. LED_STATE_COLOR0 + cs * 2) — safe against any
// future reordering/insertion in the LED_STATE_* enum.
static const int LED_STATE_COLOR_DIM_BY_SET[COLOR_SET_COUNT] = {
	LED_STATE_COLOR0_DIM, LED_STATE_COLOR1_DIM, LED_STATE_COLOR2_DIM, LED_STATE_COLOR3_DIM
};
static const int LED_STATE_COLOR_BY_SET[COLOR_SET_COUNT] = {
	LED_STATE_COLOR0, LED_STATE_COLOR1, LED_STATE_COLOR2, LED_STATE_COLOR3
};
static const int LED_STATE_CONNECTED_BY_SET[COLOR_SET_COUNT] = {
	LED_STATE_CONNECTED0, LED_STATE_CONNECTED1, LED_STATE_CONNECTED2, LED_STATE_CONNECTED3
};


struct PortAssignment {
	int64_t moduleId = -1;
	engine::Port::Type type = engine::Port::INPUT;
	int portId = -1;

	bool isValid() const { return moduleId >= 0 && portId >= 0; }
	void clear() { moduleId = -1; portId = -1; }
};

static const int TOTAL_MAPS = MATRIX_COUNT + MATRIX_SIZE;  // 64 cells + 8 scenes

struct SpliceKitModule : Module, MidiTrackingProcessorHandler {
	// Cross-instance pending state: the initiator module + its pending cell, shared across all
	// SpliceKit instances in the same Rack context. Cleared by the responder or when the
	// initiator cancels/completes its local pending.
	struct CrossPendingState {
		SpliceKitModule* initiator = nullptr;
		int cellId = -1;
		PortAssignment port;

		bool isValid() const { return initiator != nullptr && cellId >= 0; }
		void clear() { initiator = nullptr; cellId = -1; port.clear(); }
	};
	static std::map<Context*, CrossPendingState> crossPending;

	struct SpliceKitCellQuantity : ParamQuantity {
		// Returns the assigned port label, or "Cell N" if unassigned.
		std::string getLabel() override {
			if (!module) return ParamQuantity::getLabel();
			auto* m = static_cast<SpliceKitModule*>(module);
			int cellId = paramId - PARAM_MATRIX;
			if (!m->cellLabels[cellId].empty()) { 
				return m->cellLabels[cellId];
			}
			const PortAssignment& pa = m->portAssignments[cellId];
			if (pa.isValid()) {
				return portLabel(pa);
			}
			return string::f("Cell %d", cellId + 1);
		}
		// Suppressed — the numeric button value (0/1) is not meaningful to the user.
		std::string getDisplayValueString() override {
			return "";
		}
		// Returns the current MIDI mapping (CC N / Note N / unmapped) as the tooltip subtitle.
		std::string getDescription() override {
			if (!module) return "";
			auto* m = static_cast<SpliceKitModule*>(module);
			int cellId = paramId - PARAM_MATRIX;
			auto& mm = m->trackingProcessor.getMap(cellId);
			if (mm.type == MidiTrackingType::CC) {
				return string::f("MIDI: CC %d", mm.param);
			}
			if (mm.type == MidiTrackingType::NOTE) {
				return string::f("MIDI: Note %d", mm.param);
			}
			return "MIDI: (unmapped)";
		}
	};

	struct SpliceKitSceneQuantity : ParamQuantity {
		// Returns "Scene N".
		std::string getLabel() override {
			if (!module) return ParamQuantity::getLabel();
			int sceneId = paramId - PARAM_SCENE;
			return string::f("Scene %d", sceneId + 1);
		}
		// Suppressed — the numeric button value (0/1) is not meaningful to the user.
		std::string getDisplayValueString() override {
			return "";
		}
		// Returns the scene button's MIDI mapping as the tooltip subtitle.
		std::string getDescription() override {
			if (!module) return "";
			auto* m = static_cast<SpliceKitModule*>(module);
			int sceneId = paramId - PARAM_SCENE;
			auto& mm = m->trackingProcessor.getMap(MATRIX_COUNT + sceneId);
			if (mm.type == MidiTrackingType::CC) {
				return string::f("MIDI: CC %d", mm.param);
			}
			if (mm.type == MidiTrackingType::NOTE) {
				return string::f("MIDI: Note %d", mm.param);
			}
			return "MIDI: (unmapped)";
		}
	};

	enum ParamIds {
		ENUMS(PARAM_MATRIX, MATRIX_COUNT),
		ENUMS(PARAM_SCENE, MATRIX_SIZE),
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_MATRIX, MATRIX_COUNT * 3),
		ENUMS(LIGHT_SCENE, MATRIX_SIZE),
		NUM_LIGHTS
	};

	struct SpliceKitOutput : midi::Output {
		// Hook to the owning module's invalidateLedStates(), set once by the module.
		// SpliceKitOutput has no module back-pointer of its own, so a callback is used
		// instead of duplicating the cache-invalidation logic here.
		std::function<void()> onDeviceChanged;

		midi::Message lastSentMsg;
		int sentCount = 0;

		SpliceKitOutput() {
			channel = -1;
		}

		void sendMessage(const midi::Message& msg) {
			lastSentMsg = msg;
			sentCount++;
			midi::Output::sendMessage(msg);
		}

		// Prepends a "All channels" entry (-1) to the device channel list.
		std::vector<int> getChannels() override {
			std::vector<int> channels = midi::Output::getChannels();
			channels.emplace(channels.begin(), -1);
			return channels;
		}

		// Forces a full LED refresh after the device changes, which makes the next
		// process() tick re-send every cell and scene button.
		void setDeviceId(int deviceId) override {
			midi::Output::setDeviceId(deviceId);
			if (onDeviceChanged) onDeviceChanged();
		}
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	bool midiLearnMode = false;
	int learningId = -1;
	bool portLearnMode = false;
	int lastClickedCell = 0;

	/** [Stored to JSON] */
	int currentScene = 0;

	/** [Stored to JSON] */
	StoermelderPackOne::MidiTrackingProcessor<TOTAL_MAPS> trackingProcessor;

	/** [Stored to JSON] */
	SpliceKitOutput midiOutput;

	/** [Stored to JSON] */
	PortAssignment portAssignments[MATRIX_COUNT];

	// Per-scene connection bitmasks. sceneConnections[scene][cellA] has bit cellB set
	// when cellA and cellB are connected in that scene.
	/** [Stored to JSON] */
	uint64_t sceneConnections[MATRIX_SIZE][MATRIX_COUNT] = {};

	dsp::BooleanTrigger buttonTriggers[MATRIX_COUNT];
	dsp::BooleanTrigger sceneTriggers[MATRIX_SIZE];
	dsp::RingBuffer<std::function<void()>, 16> guiQueue;
	ClockDividerEx processDivider;
	// Written by GUI thread (step), read by DSP thread (process) — accepted race for LEDs
	bool portHasCable[MATRIX_COUNT] = {};
	float blinkPhase = 0.f;
	float slowBlinkPhase = 0.f;

	enum ButtonMode {
		BUTTON_TOGGLE,
		BUTTON_MOMENTARY
	};
	/** [Stored to JSON] */
	ButtonMode buttonMode = BUTTON_TOGGLE;

	// -1 = no pending; >=0 = first button pressed, awaiting second press
	int pendingCellId = -1;
	bool pendingCellIsPhysical = false; // true = set by physical button, false = set by MIDI

	// Tracks the last MIDI-activated scene that has not yet received a note-off / CC=0.
	// When a second scene activation arrives while this is set (no release in between),
	// the pair is interpreted as a copy from pendingMidiSceneId → new sceneId.
	int pendingMidiSceneId = -1;

	// Resets the local pending-cell selection only. Safe to call from either thread —
	// unlike clearPending()/clearPendingCrossGui(), it never touches crossPending.
	void clearPendingLocal() {
		pendingCellId = -1;
		pendingCellIsPhysical = false;
	}

	// GUI thread only — clears the global cross-instance pending state if this module
	// is the current initiator. crossPending is a static map shared across all module
	// instances; mutating it from the engine thread would race with GUI-thread access.
	void clearPendingCrossGui() {
		auto& cp = crossPending[APP];
		if (cp.initiator == this) cp.clear();
	}

	// Resets the pending-cell selection (called on cancel, completion, mode switch, and reset).
	// Callable from the engine thread: the local reset happens immediately, while the
	// cross-instance cleanup (if this module was the initiator) is deferred to the GUI
	// thread via guiQueue. GUI-thread callers that need the cross-instance cleanup to
	// happen immediately should call clearPendingLocal() + clearPendingCrossGui() instead.
	void clearPending() {
		clearPendingLocal();
		if (!guiQueue.full()) {
			guiQueue.push([this]() { clearPendingCrossGui(); });
		}
	}

	int portLearningId = -1;
	StoermelderPackOne::PortSelectProcessor portSelectProcessor;

	/** [Stored to JSON] */
	int feedbackPreset = 0;
	/** [Stored to JSON] — raw JSON string for the user-loaded custom preset (non-empty when feedbackPreset == PRESET_IDX_CUSTOM) */
	std::string customPresetJson;
	MidiOutPreset customPreset;

	// -1 forces a send on the first light-divider tick after load.
	int cellLedState[MATRIX_COUNT];
	int sceneLedState[MATRIX_SIZE];

	/** [Stored to JSON — only non-empty entries are written] */
	std::string cellLabels[MATRIX_COUNT];

	/** [Stored to JSON — only non-default (-1) entries are written]
	 *  -1 = auto (OUTPUT → set 0 / red, INPUT → set 1 / blue)
	 *   0–3 = explicit color set override */
	int8_t cellColorSet[MATRIX_COUNT];

	/** [Stored to JSON] */
	bool overlayEnabled = true;
	/** [Stored to JSON] */
	bool crossInstanceEnabled = true;
	int overlayMessageId = -1;
	OverlayMessageProvider::Message overlayMessage;

	uint64_t sceneClipboard[MATRIX_COUNT] = {};
	bool sceneClipboardValid = false;

	SpliceKitModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		memset(cellColorSet, -1, sizeof(cellColorSet));
		invalidateLedStates();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			configParam<SpliceKitCellQuantity>(PARAM_MATRIX + i, 0.f, 1.f, 0.f);
		}
		for (int i = 0; i < MATRIX_SIZE; i++) {
			configParam<SpliceKitSceneQuantity>(PARAM_SCENE + i, 0.f, 1.f, 0.f);
		}

		trackingProcessor.handler = this;
		trackingProcessor.enableCc();
		trackingProcessor.enableNotes();
		midiOutput.onDeviceChanged = [this]() { invalidateLedStates(); };
		processDivider.setDivision(256);
	}

	void onRemove() override {
		auto& cp = crossPending[APP];
		if (cp.initiator == this) cp.clear();
	}

	// Engine thread — called by Rack when the user resets the module.
	void onReset() override {
		disableLearn();
		disablePortLearn();
		clearPending();
		for (auto& l : cellLabels) l.clear();
		memset(cellColorSet, -1, sizeof(cellColorSet));
		requestReset();
	}

	void processBypass(const ProcessArgs& args) override {
		trackingProcessor.processBypass(args.frame);
		Module::processBypass(args);
	}

	void process(const ProcessArgs& args) override {
		trackingProcessor.process(args.frame);

		if (processDivider.process()) {
			for (int i = 0; i < MATRIX_COUNT; i++) {
				bool high = params[PARAM_MATRIX + i].getValue() > 0.5f;
				if (buttonTriggers[i].process(high)) {
					if (learningId == i) {
						disableLearn();
					}
					else if (portLearningId == i) {
						disablePortLearn();
					}
					else {
						triggerCell(i);
						pendingCellIsPhysical = true;
					}
					blinkPhase = 0.f;
				}
				else if (buttonMode == BUTTON_MOMENTARY && pendingCellIsPhysical
					  && pendingCellId == i && !high) {
					clearPending();
				}
			}

			for (int i = 0; i < MATRIX_SIZE; i++) {
				if (sceneTriggers[i].process(params[PARAM_SCENE + i].getValue() > 0.5f)) {
					if (learningId == MATRIX_COUNT + i) {
						disableLearn();
					} 
					else {
						requestSceneChange(i);
					}
				}
			}

			blinkPhase += args.sampleTime * 4.f * processDivider.division;
			if (blinkPhase >= 1.f) blinkPhase -= 1.f;
			slowBlinkPhase += args.sampleTime * 2.f * processDivider.division;
			if (slowBlinkPhase >= 1.f) slowBlinkPhase -= 1.f;

			bool blinkOn = blinkPhase < 0.5f;
			bool slowBlinkOn = slowBlinkPhase < 0.5f;
			for (int i = 0; i < MATRIX_COUNT; i++) {
				bool assigned = portAssignments[i].isValid();
				bool hasCable = portHasCable[i];
				bool connectedToPending = pendingCellId >= 0 && i != pendingCellId
					&& isConnected(currentScene, pendingCellId, i);
				int cs = getCellColorSet(i);  // 0–3
				NVGcolor col;
				int stateId;
				if (pendingCellId == i) {
					col = blinkOn ? LED_PENDING : LED_OFF;
					stateId = LED_STATE_PENDING;
				}
				else if (connectedToPending) {
					col = slowBlinkOn ? COLOR_SETS[cs].color : nvgRGBf(0.f, 0.f, 0.f);
					stateId = LED_STATE_CONNECTED_BY_SET[cs];
				}
				else if (portLearningId == i) {
					col = blinkOn ? LED_PORT_LEARN : LED_OFF;
					stateId = LED_STATE_PORT_LEARN;
				}
				else if (learningId == i) {
					col = blinkOn ? LED_MIDI_LEARN : LED_OFF;
					stateId = LED_STATE_MIDI_LEARN;
				}
				else if (assigned) {
					col = color::mult(COLOR_SETS[cs].color, hasCable ? LED_BRIGHT : LED_DIM);
					stateId = hasCable ? LED_STATE_COLOR_BY_SET[cs] : LED_STATE_COLOR_DIM_BY_SET[cs];
				}
				else {
					col = LED_OFF;
					stateId = LED_STATE_OFF;
				}
				if (stateId != cellLedState[i]) {
					sendFeedbackOff(i, cellLedState[i]);
					cellLedState[i] = stateId;
					sendFeedback(i, stateId);
				}
				float f = args.sampleTime * processDivider.division;
				lights[LIGHT_MATRIX + i * 3 + 0].setBrightnessSmooth(col.r, f);
				lights[LIGHT_MATRIX + i * 3 + 1].setBrightnessSmooth(col.g, f);
				lights[LIGHT_MATRIX + i * 3 + 2].setBrightnessSmooth(col.b, f);
			}
			for (int s = 0; s < MATRIX_SIZE; s++) {
				float bright;
				int sceneState;
				if (learningId == MATRIX_COUNT + s) {
					bright = blinkOn ? LED_BRIGHT : 0.f;
					sceneState = LED_STATE_MIDI_LEARN;
				}
				else if (s == currentScene) {
					bright = LED_BRIGHT;
					sceneState = LED_STATE_SCENE_ACTIVE;
				}
				else {
					bool hasConn = false;
					for (int i = 0; i < MATRIX_COUNT; i++) {
						if (sceneConnections[s][i]) { hasConn = true; break; }
					}
					bright = hasConn ? LED_SCENE_DIM : 0.f;
					sceneState = hasConn ? LED_STATE_SCENE_DIM : LED_STATE_OFF;
				}
				if (sceneState != sceneLedState[s]) {
					sendFeedbackOff(MATRIX_COUNT + s, sceneLedState[s]);
					sceneLedState[s] = sceneState;
					sendFeedback(MATRIX_COUNT + s, sceneState);
				}
				lights[LIGHT_SCENE + s].setBrightness(bright);
			}
		}
	}

	// Any thread — pure bitmask read; no side effects.
	bool isConnected(int scene, int a, int b) {
		return (sceneConnections[scene][a] >> b) & 1;
	}

	// Any thread — updates both symmetric bitmask entries for the (a, b) pair.
	void setConnection(int scene, int a, int b, bool value) {
		if (value) {
			sceneConnections[scene][a] |= (1ULL << b);
			sceneConnections[scene][b] |= (1ULL << a);
		} 
		else {
			sceneConnections[scene][a] &= ~(1ULL << b);
			sceneConnections[scene][b] &= ~(1ULL << a);
		}
	}

	// Returns the resolved color-set index for cell i (0–3). Auto mode maps by port direction.
	int getCellColorSet(int i) const {
		if (cellColorSet[i] >= 0) return cellColorSet[i];
		return (portAssignments[i].type == engine::Port::OUTPUT) ? 0 : 1;
	}

	// Engine thread — MidiTrackingProcessorHandler callback, fired for every mapped
	// MIDI message. Routes matrix cell triggers and scene changes into guiQueue.
	void processMapUpdate(StoermelderPackOne::MidiTrackingType type, uint16_t mapId, uint16_t value) override {
		if (mapId < (uint16_t)MATRIX_COUNT) {
			if (value > 0) {
				pendingCellIsPhysical = false;
				triggerCell(mapId);
			}
			else if (buttonMode == BUTTON_MOMENTARY && (int)mapId == pendingCellId) {
				clearPending();
			}
		}
		else if (mapId < (uint16_t)TOTAL_MAPS) {
			int sceneId = (int)(mapId - MATRIX_COUNT);
			if (value > 0) {
				if (pendingMidiSceneId >= 0 && pendingMidiSceneId != sceneId) {
					// Two consecutive activations without a release: treat as scene copy.
					int src = pendingMidiSceneId;
					pendingMidiSceneId = -1;
					requestCopyScene(src, sceneId);
				}
				else {
					// Normal scene selection.
					requestSceneChange(sceneId);
					pendingMidiSceneId = sceneId;
				}
			}
			else {
				if (pendingMidiSceneId == sceneId) pendingMidiSceneId = -1;
			}
		}
	}

	// Engine thread — MidiTrackingProcessorHandler callback, fired when a MIDI learn
	// completes. Advances the cursor in sequential learn mode, or clears it for single learn.
	// NOTE: writes learningId and midiLearnMode which are also read/written by the GUI thread
	// (context menus). The race is accepted — both sides only write simple scalars.
	void processMapLearn(StoermelderPackOne::MidiTrackingType type, uint16_t mapId) override {
		if (midiLearnMode) {
			// Sequential: advance to next cell
			int nextId = learningId + 1;
			learningId = -1;
			if (nextId < MATRIX_COUNT) {
				learningId = nextId;
				trackingProcessor.enableMapLearn(nextId);
			} 
			else {
				midiLearnMode = false; // All cells learned
			}
		} 
		else {
			learningId = -1;
		}
	}

	// Returns a pointer to the currently active preset, or nullptr when feedback is off.
	const MidiOutPreset* getActivePreset() const {
		static auto& presets = getPresets();
		if (feedbackPreset == PRESET_IDX_CUSTOM) return &customPreset;
		if (feedbackPreset > 0 && feedbackPreset < CONTROLLER_PRESET_COUNT) {
			return &presets[feedbackPreset];
		}
		return nullptr;
	}

	// Any thread — forces a full MIDI LED refresh on the next process() tick by resetting
	// all cached LED states to -1, causing every cell and scene button to be re-sent.
	void invalidateLedStates() {
		std::fill(cellLedState, cellLedState + MATRIX_COUNT, -1);
		std::fill(sceneLedState, sceneLedState + MATRIX_SIZE, -1);
	}

	// GUI thread — starts MIDI learn for a single cell (id < MATRIX_COUNT) or scene button
	// (id >= MATRIX_COUNT). Cancels any previously active learn first.
	void enableLearn(int id) {
		disableLearn();
		disablePortLearn();
		if (id < 0 || id >= TOTAL_MAPS) return;
		learningId = id;
		trackingProcessor.enableMapLearn(id);
	}

	// GUI thread — starts sequential MIDI learn from lastClickedCell through cell 63.
	// Scene buttons are excluded; assign them individually or via applyPresetLayout().
	void startGlobalLearn() {
		disableLearn();
		disablePortLearn();
		midiLearnMode = true;
		learningId = lastClickedCell;
		trackingProcessor.enableMapLearn(lastClickedCell);
	}

	// GUI thread — starts sequential port-assignment learn from lastClickedCell through cell 63.
	// Uses LEARN_MODE::MULTI so the owner widget stays focused across clicks (step() keeps
	// re-asserting selection). A single persistent callback advances portLearningId in place.
	void startGlobalPortLearn(Widget* owner) {
		disableLearn();
		disablePortLearn();
		portLearnMode = true;
		portLearningId = lastClickedCell;
		portSelectProcessor.setOwner(owner);
		portSelectProcessor.startLearn(
			[=](PortWidget* pw, Vec) {
				if (!pw->module) return;
				portAssignments[portLearningId].moduleId = pw->module->getId();
				portAssignments[portLearningId].type = pw->type;
				portAssignments[portLearningId].portId = pw->portId;
				if (portLearningId + 1 < MATRIX_COUNT) {
					portLearningId++;
				} 
				else {
					portLearnMode = false;
					portLearningId = -1;
					portSelectProcessor.disableLearn();
				}
			},
			PortSelectProcessor::LEARN_MODE::MULTI,
			[=]() {
				portLearnMode = false;
				portLearningId = -1;
			}
		);
	}

	// Engine or GUI thread — cancels any active MIDI learn.
	// Called from process() (engine thread) when the user presses the blinking cell,
	// and from GUI thread via context menus and onReset(). The race on learningId is accepted.
	void disableLearn() {
		trackingProcessor.disableMapLearn();
		learningId = -1;
		midiLearnMode = false;
	}

	// GUI thread — starts port-assignment learn for a single cell. The portSelectProcessor
	// intercepts the next port-widget click and writes portAssignments[id].
	void enablePortLearn(int id, Widget* owner) {
		if (id < 0 || id >= MATRIX_COUNT) return;
		disableLearn();
		disablePortLearn();
		portLearningId = id;
		portSelectProcessor.setOwner(owner);
		portSelectProcessor.startLearn(
			[=](PortWidget* pw, Vec) {
				if (!pw->module) return;
				portAssignments[portLearningId].moduleId = pw->module->getId();
				portAssignments[portLearningId].type = pw->type;
				portAssignments[portLearningId].portId = pw->portId;
				portLearningId = -1;
			}
		);
	}

	// GUI thread — cancels an active port-assignment learn.
	void disablePortLearn() {
		portSelectProcessor.disableLearn();
		portLearningId = -1;
		portLearnMode = false;
	}

	// GUI thread — returns true if the given cell is currently in port-learn mode.
	bool isPortLearning(int id) {
		return portLearningId == id && portSelectProcessor.isLearning();
	}

	// Engine thread — sends a MIDI note-off for the previous LED state before a transition,
	// clearing the controller LED that was lit by a NOTE_ON-based spec. No-op for CC specs,
	// off states, or when no preset is active.
	void sendFeedbackOff(int cellId, int oldStateId) {
		if (oldStateId < 0) return;
		const MidiOutPreset* preset = getActivePreset();
		if (!preset) return;
		const MidiOutSpec& spec = preset->specs[oldStateId];
		if (spec.type == MIDI_OUT_NONE) return;
		if (spec.type != MIDI_OUT_NOTE_ON && spec.type != MIDI_OUT_FROM_SLOT_TYPE) return;
		MidiTrackingType slotType = MidiTrackingType::NONE;
		int noteNum;
		switch (spec.noteMode) {
			case MIDI_OUT_FROM_SLOT: {
				auto m = trackingProcessor.getMap(cellId);
				if (m.type == MidiTrackingType::NONE) return;
				noteNum  = m.param;
				slotType = m.type;
				break;
			}
			case MIDI_OUT_FIXED: {
				noteNum = spec.note;
				break;
			}
			default: return;
		}
		if (spec.type == MIDI_OUT_FROM_SLOT_TYPE && slotType != MidiTrackingType::NOTE) return;
		midi::Message msg;
		msg.bytes[0] = 0x80 | (uint8_t)(spec.channel & 0x0F);
		msg.bytes[1] = (uint8_t)(noteNum & 0x7F);
		msg.bytes[2] = 0x00;
		msg.frame = APP->engine->getFrame() + 1;
		midiOutput.sendMessage(msg);
	}

	// Engine thread — sends a single MIDI message to the output device to update the
	// controller LED for mapId to the given stateId. Called only when the state changes.
	// mapId 0–63: matrix cells; mapId 64–71: scene buttons (MATRIX_COUNT + sceneId).
	void sendFeedback(int cellId, int stateId) {
		const MidiOutPreset* preset = getActivePreset();
		if (!preset) return;
		const MidiOutSpec& spec = preset->specs[stateId];
		if (spec.type == MIDI_OUT_NONE) return;
		MidiTrackingType slotType = MidiTrackingType::NONE;
		int noteNum;
		switch (spec.noteMode) {
			case MIDI_OUT_FROM_SLOT: {
				auto m = trackingProcessor.getMap(cellId);
				if (m.type == MidiTrackingType::NONE) return;
				noteNum  = m.param;
				slotType = m.type;
				break;
			}
			case MIDI_OUT_FIXED: {
				noteNum = spec.note;
				break;
			}
			default: return;
		}
		uint8_t status;
		switch (spec.type) {
			case MIDI_OUT_NOTE_ON:
				status = 0x90;
				break;
			case MIDI_OUT_NOTE_OFF:
				status = 0x80;
				break;
			case MIDI_OUT_CC:
				status = 0xB0;
				break;
			case MIDI_OUT_FROM_SLOT_TYPE:
				if (slotType == MidiTrackingType::NOTE) status = 0x90;
				else if (slotType == MidiTrackingType::CC) status = 0xB0;
				else return;
				break;
			default: return;
		}
		midi::Message msg;
		msg.bytes[0] = status | (uint8_t)(spec.channel & 0x0F);
		msg.bytes[1] = (uint8_t)(noteNum  & 0x7F);
		msg.bytes[2] = (uint8_t)(spec.value & 0x7F);
		msg.frame = APP->engine->getFrame() + 2;
		midiOutput.sendMessage(msg);
	}

	// GUI thread — replaces all MIDI input mappings with those defined by the currently
	// selected feedback preset's slot layout. No-op if the preset has no layout.
	void applyPresetLayout() {
		const MidiOutPreset* preset = getActivePreset();
		if (!preset || !preset->hasLayout()) return;
		trackingProcessor.clearMaps();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const auto& s = preset->cells[i];
			if (s.type != MidiTrackingType::NONE) {
				trackingProcessor.setMap(s.type, i, s.number);
			}
		}
		for (int i = 0; i < MATRIX_SIZE; i++) {
			const auto& s = preset->scenes[i];
			if (s.type != MidiTrackingType::NONE) {
				trackingProcessor.setMap(s.type, MATRIX_COUNT + i, s.number);
			}
		}
		invalidateLedStates();
	}

	// GUI thread — serializes all persistent state to JSON (called on patch save / undo snapshot).
	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "currentScene", json_integer(currentScene));

		json_t* mapsJ = json_object();
		for (int i = 0; i < TOTAL_MAPS; i++) {
			auto m = trackingProcessor.getMap(i);
			if (m.type == MidiTrackingType::NONE) continue;
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "type", json_integer((int)m.type));
			json_object_set_new(mapJ, "param", json_integer(m.param));
			json_object_set_new(mapsJ, std::to_string(i).c_str(), mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		json_t* portsJ = json_object();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (!portAssignments[i].isValid()) continue;
			json_t* portJ = json_object();
			json_object_set_new(portJ, "moduleId", json_integer(portAssignments[i].moduleId));
			json_object_set_new(portJ, "type", json_integer((int)portAssignments[i].type));
			json_object_set_new(portJ, "portId", json_integer(portAssignments[i].portId));
			json_object_set_new(portsJ, std::to_string(i).c_str(), portJ);
		}
		json_object_set_new(rootJ, "ports", portsJ);

		json_t* scenesJ = json_object();
		for (int s = 0; s < MATRIX_SIZE; s++) {
			json_t* connJ = json_array();
			for (int a = 0; a < MATRIX_COUNT; a++) {
				for (int b = a + 1; b < MATRIX_COUNT; b++) {
					if (isConnected(s, a, b)) {
						json_t* pairJ = json_array();
						json_array_append_new(pairJ, json_integer(a));
						json_array_append_new(pairJ, json_integer(b));
						json_array_append_new(connJ, pairJ);
					}
				}
			}
			if (json_array_size(connJ) > 0) {
				json_t* sceneJ = json_object();
				json_object_set_new(sceneJ, "connections", connJ);
				json_object_set_new(scenesJ, std::to_string(s).c_str(), sceneJ);
			}
			else {
				json_decref(connJ);
			}
		}
		json_object_set_new(rootJ, "scenes", scenesJ);
		json_object_set_new(rootJ, "feedbackPreset", json_integer(feedbackPreset));
		json_object_set_new(rootJ, "buttonMode", json_integer((int)buttonMode));
		json_object_set_new(rootJ, "overlayEnabled", json_boolean(overlayEnabled));
		json_object_set_new(rootJ, "crossInstanceEnabled", json_boolean(crossInstanceEnabled));
		if (feedbackPreset == PRESET_IDX_CUSTOM && !customPresetJson.empty()) {
			json_object_set_new(rootJ, "customPreset", json_string(customPresetJson.c_str()));
		}
		json_t* labelsJ = json_object();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (!cellLabels[i].empty()) {
				json_object_set_new(labelsJ, std::to_string(i).c_str(), json_string(cellLabels[i].c_str()));
			}
		}
		if (json_object_size(labelsJ) > 0) {
			json_object_set_new(rootJ, "cellLabels", labelsJ);
		}
		else {
			json_decref(labelsJ);
		}
		json_t* colorSetsJ = json_object();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (cellColorSet[i] >= 0) {
				json_object_set_new(colorSetsJ, std::to_string(i).c_str(), json_integer(cellColorSet[i]));
			}
		}
		if (json_object_size(colorSetsJ) > 0) {
			json_object_set_new(rootJ, "cellColorSets", colorSetsJ);
		}
		else {
			json_decref(colorSetsJ);
		}
		json_object_set_new(rootJ, "midiInput",  trackingProcessor.getInput().toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		return rootJ;
	}

	// GUI thread — restores all persistent state from JSON (called on patch load / undo).
	// clearMaps() is called before restoring maps to prevent duplicate vector entries
	// that would occur if setMap() is called multiple times for the same slot (undo/redo).
	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		currentScene = json_integer_value(json_object_get(rootJ, "currentScene"));
		json_t* buttonModeJ = json_object_get(rootJ, "buttonMode");
		if (buttonModeJ) buttonMode = (ButtonMode)json_integer_value(buttonModeJ);
		json_t* overlayEnabledJ = json_object_get(rootJ, "overlayEnabled");
		if (overlayEnabledJ) overlayEnabled = json_boolean_value(overlayEnabledJ);
		json_t* crossInstanceEnabledJ = json_object_get(rootJ, "crossInstanceEnabled");
		if (crossInstanceEnabledJ) crossInstanceEnabled = json_boolean_value(crossInstanceEnabledJ);

		trackingProcessor.clearMaps();
		json_t* mapsJ = json_object_get(rootJ, "maps");
		if (mapsJ) {
			const char* key;
			json_t* mapJ;
			json_object_foreach(mapsJ, key, mapJ) {
				int i = std::atoi(key);
				if (i < 0 || i >= TOTAL_MAPS) continue;
				auto type = (StoermelderPackOne::MidiTrackingType)json_integer_value(json_object_get(mapJ, "type"));
				auto param = (uint16_t)json_integer_value(json_object_get(mapJ, "param"));
				if (type != MidiTrackingType::NONE) {
					trackingProcessor.setMap(type, i, param);
				}
			}
		}

		json_t* portsJ = json_object_get(rootJ, "ports");
		if (portsJ) {
			const char* key;
			json_t* portJ;
			json_object_foreach(portsJ, key, portJ) {
				int i = std::atoi(key);
				if (i < 0 || i >= MATRIX_COUNT) continue;
				portAssignments[i].moduleId = json_integer_value(json_object_get(portJ, "moduleId"));
				portAssignments[i].type = (engine::Port::Type)json_integer_value(json_object_get(portJ, "type"));
				portAssignments[i].portId = json_integer_value(json_object_get(portJ, "portId"));
			}
		}

		feedbackPreset = json_integer_value(json_object_get(rootJ, "feedbackPreset"));
		if (feedbackPreset != PRESET_IDX_CUSTOM) {
			feedbackPreset = clamp(feedbackPreset, 0, CONTROLLER_PRESET_COUNT - 1);
		}
		if (feedbackPreset == PRESET_IDX_CUSTOM) {
			json_t* cpJ = json_object_get(rootJ, "customPreset");
			const char* cpStr = cpJ ? json_string_value(cpJ) : nullptr;
			if (cpStr) {
				customPresetJson = cpStr;
				json_error_t err;
				json_t* root = json_loads(customPresetJson.c_str(), 0, &err);
				if (root) { customPreset.fromJson(root); json_decref(root); }
				else feedbackPreset = 0;
			}
			else {
				feedbackPreset = 0;
			}
		}
		invalidateLedStates();

		for (auto& l : cellLabels) l.clear();
		json_t* labelsJ = json_object_get(rootJ, "cellLabels");
		if (labelsJ) {
			const char* key;
			json_t* val;
			json_object_foreach(labelsJ, key, val) {
				int i = std::atoi(key);
				const char* s = json_string_value(val);
				if (i >= 0 && i < MATRIX_COUNT && s) {
					cellLabels[i] = s;
				}
			}
		}
		memset(cellColorSet, -1, sizeof(cellColorSet));
		json_t* colorSetsJ = json_object_get(rootJ, "cellColorSets");
		if (colorSetsJ) {
			const char* key;
			json_t* val;
			json_object_foreach(colorSetsJ, key, val) {
				int i = std::atoi(key);
				if (i >= 0 && i < MATRIX_COUNT) {
					int cs = (int)json_integer_value(val);
					cellColorSet[i] = (int8_t)clamp(cs, 0, COLOR_SET_COUNT - 1);
				}
			}
		}
		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ) trackingProcessor.getInput().fromJson(midiInputJ);
		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ) midiOutput.fromJson(midiOutputJ);

		memset(sceneConnections, 0, sizeof(sceneConnections));
		json_t* scenesJ = json_object_get(rootJ, "scenes");
		if (scenesJ) {
			const char* key;
			json_t* sceneJ;
			json_object_foreach(scenesJ, key, sceneJ) {
				int s = std::atoi(key);
				if (s < 0 || s >= MATRIX_SIZE) continue;
				json_t* connJ = json_object_get(sceneJ, "connections");
				if (!connJ) continue;
				size_t k;
				json_t* pairJ;
				json_array_foreach(connJ, k, pairJ) {
					int a = json_integer_value(json_array_get(pairJ, 0));
					int b = json_integer_value(json_array_get(pairJ, 1));
					if (a >= 0 && a < MATRIX_COUNT && b >= 0 && b < MATRIX_COUNT) {
						setConnection(s, a, b, true);
					}
				}
			}
		}
	}

	// --- Cable manipulation (GUI thread only) ---
	// Low-level helpers (findCable, removeCable, addCableToPort) live in SpliceKit_cable.hpp.

	// GUI thread — resolves port directions for the two cells and removes the cable between
	// them. No-op if both ports are the same direction or either assignment is invalid.
	void removeCableBetween(int cellIdA, int cellIdB) {
		const PortAssignment& a = portAssignments[cellIdA];
		const PortAssignment& b = portAssignments[cellIdB];
		if (!a.isValid() || !b.isValid()) return;
		const PortAssignment* outPd = nullptr;
		const PortAssignment* inPd = nullptr;
		if (a.type == engine::Port::OUTPUT && b.type == engine::Port::INPUT) { outPd = &a; inPd = &b; }
		else if (a.type == engine::Port::INPUT  && b.type == engine::Port::OUTPUT) { outPd = &b; inPd = &a; }
		else return;
		CableWidget* cw = vcv::findCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId);
		if (cw) vcv::removeCable(cw, false);
	}

	// GUI thread — creates or removes the cable between cellIdA and cellIdB in the current
	// scene, then updates overlayMessage so the overlay bar shows what changed.
	// One cell must be an output and the other an input; same-direction pairs are ignored.
	void toggleConnection(int cellIdA, int cellIdB) {
		const PortAssignment& a = portAssignments[cellIdA];
		const PortAssignment& b = portAssignments[cellIdB];

		const PortAssignment* outPd = nullptr;
		const PortAssignment* inPd = nullptr;
		int outCell, inCell;
		if (!a.isValid() || !b.isValid()) {
			return;
		}
		if (a.type == engine::Port::OUTPUT && b.type == engine::Port::INPUT) {
			outPd = &a; inPd = &b; outCell = cellIdA; inCell = cellIdB;
		} 
		else if (a.type == engine::Port::INPUT && b.type == engine::Port::OUTPUT) {
			outPd = &b; inPd = &a; outCell = cellIdB; inCell = cellIdA;
		} 
		else {
			bool bothOut = (a.type == engine::Port::OUTPUT && b.type == engine::Port::OUTPUT);
			setOverlayMessage(bothOut ? "Both ports are outputs" : "Both ports are inputs",
				portLabel(a), portLabel(b));
			return;
		}

		if (isConnected(currentScene, outCell, inCell)) {
			setConnection(currentScene, outCell, inCell, false);
			removeCableBetween(outCell, inCell);
			setOverlayMessage("Cable removed", portLabel(*outPd), portLabel(*inPd));
		} 
		else {
			setConnection(currentScene, outCell, inCell, true);
			vcv::addCableToPort(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId, false);
			setOverlayMessage("Cable created", portLabel(*outPd), portLabel(*inPd));
		}
	}

	// GUI thread — rewrites sceneConnections[scene] to match the actual cables currently
	// present in the patch for the assigned ports. Used before switching scenes so that
	// any manual cable changes the user made are not lost.
	void captureScene(int scene) {
		memset(sceneConnections[scene], 0, sizeof(sceneConnections[scene]));
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const PortAssignment& a = portAssignments[i];
			if (!a.isValid() || a.type != engine::Port::OUTPUT) continue;
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if (j == i) continue;
				const PortAssignment& b = portAssignments[j];
				if (!b.isValid() || b.type != engine::Port::INPUT) continue;
				if (vcv::findCable(a.moduleId, a.portId, b.moduleId, b.portId)) {
					setConnection(scene, i, j, true);
				}
			}
		}
	}

	// GUI thread — reconciles the patch so it matches newConns, starting from oldConns.
	// For each pair (i,j): if a connection was present in old but not new the cable is removed;
	// if it is present in new but not old a cable is added. Pairs that haven't changed are skipped.
	void applyConnectionDiff(const uint64_t* oldConns, const uint64_t* newConns) {
		for (int i = 0; i < MATRIX_COUNT; i++) {
			for (int j = i + 1; j < MATRIX_COUNT; j++) {
				bool was = (oldConns[i] >> j) & 1;
				bool will = (newConns[i] >> j) & 1;
				if (was == will) continue;
				const PortAssignment& a = portAssignments[i];
				const PortAssignment& b = portAssignments[j];
				if (!a.isValid() || !b.isValid()) continue;
				if (was) {
					removeCableBetween(i, j);
				}
				else {
					const PortAssignment* outPd = nullptr;
					const PortAssignment* inPd  = nullptr;
					if (a.type == engine::Port::OUTPUT && b.type == engine::Port::INPUT) { outPd = &a; inPd = &b; }
					else if (a.type == engine::Port::INPUT  && b.type == engine::Port::OUTPUT) { outPd = &b; inPd = &a; }
					else continue;
					vcv::addCableToPort(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId, false);
				}
			}
		}
	}

	// GUI thread — applies a new connection topology to an arbitrary scene.
	// If the target scene is the currently active one, the patch cables are updated live
	// (capture current state first, then diff against newConns). Always persists newConns
	// into sceneConnections[scene].
	void reconcileScene(int scene, const uint64_t* newConns) {
		if (scene == currentScene) {
			captureScene(scene);
			applyConnectionDiff(sceneConnections[scene], newConns);
		}
		memcpy(sceneConnections[scene], newConns, MATRIX_COUNT * sizeof(uint64_t));
	}

	// GUI thread — switches to newScene: captures the outgoing scene's current cable state,
	// diffs it against the incoming scene's stored topology, and updates patch cables accordingly.
	void switchScene(int newScene) {
		if (newScene == currentScene) return;
		captureScene(currentScene);
		applyConnectionDiff(sceneConnections[currentScene], sceneConnections[newScene]);
		currentScene = newScene;
	}

	// GUI thread — returns a human-readable label for a port assignment, e.g. "VCO · Out 1".
	// Falls back to a placeholder string if the module widget is no longer in the rack.
	static std::string portLabel(const PortAssignment& pa) {
		if (!pa.isValid()) return "";
		ModuleWidget* mw = APP->scene->rack->getModule(pa.moduleId);
		if (!mw) return string::f("(missing module, port %d)", pa.portId + 1);
		std::string dir = (pa.type == engine::Port::OUTPUT) ? "Out" : "In";
		return string::f("%s \xc2\xb7 %s %d", mw->model->name.c_str(), dir.c_str(), pa.portId + 1);
	}

	// GUI thread — posts a two-line overlay message (no-op when overlay is disabled).
	void setOverlayMessage(const std::string& title, const std::string& sub0, const std::string& sub1 = "") {
		if (!overlayEnabled) return;
		overlayMessage.title = title;
		overlayMessage.subtitle[0] = sub0;
		overlayMessage.subtitle[1] = sub1;
		overlayMessageId = 0;
	}

	// Engine thread — handles a single cell activation (button press or MIDI trigger).
	// First press sets pendingCellId and defers all cross-instance logic to the GUI thread
	// via guiQueue, so that crossPending is only ever touched from one thread. Second press
	// on a different cell (same instance) enqueues toggleConnection. Same cell cancels.
	void triggerCell(int id) {
		if (!portAssignments[id].isValid()) return;

		if (pendingCellId < 0) {
			pendingCellId = id;
			if (!guiQueue.full()) {
				guiQueue.push([this, id]() {
					// GUI thread — sole owner of crossPending. Re-check cp validity here
					// because another instance's lambda may have already consumed it.
					auto& cp = crossPending[APP];
					if (crossInstanceEnabled && cp.isValid() && cp.initiator != this) {
						// Responder path: create the cable directly (we are on the GUI thread).
						SpliceKitModule* initiator = cp.initiator;
						PortAssignment iPort = cp.port;
						PortAssignment rPort = portAssignments[id];
						cp.clear();
						initiator->clearPendingLocal();
						clearPendingLocal();  // reset our own tentative pendingCellId
						const PortAssignment* outPd = nullptr;
						const PortAssignment* inPd  = nullptr;
						if (iPort.type == engine::Port::OUTPUT && rPort.type == engine::Port::INPUT) {
							outPd = &iPort; inPd = &rPort;
						}
						else if (iPort.type == engine::Port::INPUT && rPort.type == engine::Port::OUTPUT) {
							outPd = &rPort; inPd = &iPort;
						}
						if (outPd) {
							CableWidget* cw = vcv::findCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId);
							if (!cw) {
								vcv::addCableToPort(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId, false);
								setOverlayMessage("Cable created", portLabel(*outPd), portLabel(*inPd));
							}
							else {
								vcv::removeCable(cw, false);
								setOverlayMessage("Cable removed", portLabel(*outPd), portLabel(*inPd));
							}
						}
					}
					else {
						// Initiator path (cp was already consumed, or no cross-pending).
						if (crossInstanceEnabled) {
							cp.initiator = this;
							cp.cellId = id;
							cp.port = portAssignments[id];
						}
						const std::string& lbl = cellLabels[id];
						if (!lbl.empty()) setOverlayMessage(lbl, portLabel(portAssignments[id]));
						else setOverlayMessage("Port selected", portLabel(portAssignments[id]));
					}
				});
			}
		}
		else if (pendingCellId == id) {
			clearPending();
		}
		else {
			int a = pendingCellId, b = id;
			if (!guiQueue.full()) {
				guiQueue.push([this, a, b]() { toggleConnection(a, b); });
			}
			clearPending();
		}
	}

	// GUI thread — removes all cables connected to cellId in the current scene.
	void removeCellConnections(int cellId) {
		uint64_t mask = sceneConnections[currentScene][cellId];
		for (int j = 0; j < MATRIX_COUNT; j++) {
			if (!((mask >> j) & 1)) continue;
			setConnection(currentScene, cellId, j, false);
			removeCableBetween(cellId, j);
		}
	}

	// Engine thread — enqueues a switchScene call on the GUI thread via guiQueue.
	void requestSceneChange(int i) {
		if (!guiQueue.full()) {
			guiQueue.push([this, i]() { switchScene(i); });
		}
	}

	// GUI thread — copies scene src's connection topology to scene dst.
	// If dst is the active scene, cables are updated live via reconcileScene.
	void copyScene(int src, int dst) {
		if (src == dst) return;
		if (src == currentScene) captureScene(src);
		reconcileScene(dst, sceneConnections[src]);
		setOverlayMessage("Scene copied", string::f("%d \xe2\x86\x92 %d", src + 1, dst + 1));
	}

	// Engine thread — enqueues a copyScene call on the GUI thread via guiQueue.
	void requestCopyScene(int src, int dst) {
		if (!guiQueue.full()) {
			guiQueue.push([this, src, dst]() { copyScene(src, dst); });
		}
	}

	// GUI thread — moves the port assignment, label, color, and all scene
	// connections from fromId to toId. Any existing assignment on toId is
	// discarded. MIDI mappings stay on their original cells (they are tied to
	// physical button positions, not to ports). fromId's physical cables are NOT
	// touched: they connect fromId's port (which toId inherits) and remain valid.
	// Only toId's existing cables are removed because its old port is discarded.
	void moveCell(int fromId, int toId) {
		if (fromId == toId) return;

		// Turn off both LEDs now, while each cell still has its own MIDI mapping.
		// sendFeedbackOff uses the mapping to address the right note/CC; clearing
		// the mappings first would cause the off-message to be lost.
		sendFeedbackOff(fromId, cellLedState[fromId]);
		sendFeedbackOff(toId, cellLedState[toId]);

		// Remove only toId's existing cables — fromId's cables stay, because toId
		// will inherit fromId's port and those cables are already in the right place.
		removeCellConnections(toId);

		// Rewrite sceneConnections for every scene:
		//   Step A — tear out toId's existing connections from its neighbours.
		//   Step B — redirect fromId's connections to toId.
		for (int s = 0; s < MATRIX_SIZE; s++) {
			uint64_t oldToMask = sceneConnections[s][toId];
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if ((oldToMask >> j) & 1) {
					sceneConnections[s][j] &= ~(1ULL << toId);
				}
			}
			sceneConnections[s][toId] = 0;

			uint64_t fromMask = sceneConnections[s][fromId];
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if (!((fromMask >> j) & 1)) continue;
				sceneConnections[s][j] &= ~(1ULL << fromId);
				if (j != toId) {
					sceneConnections[s][j] |= (1ULL << toId);
				}
			}
			// fromId's connections become toId's, minus any self-connection bit.
			sceneConnections[s][toId]   = fromMask & ~(1ULL << toId);
			sceneConnections[s][fromId] = 0;
		}

		// Move port assignment, label, and color. MIDI mapping is intentionally
		// kept on each cell: it is tied to the physical button position on the
		// controller and must not follow the port when it is relocated.
		portAssignments[toId] = portAssignments[fromId];
		portAssignments[fromId].clear();

		cellLabels[toId] = std::move(cellLabels[fromId]);
		cellLabels[fromId] = "";
		cellColorSet[toId] = cellColorSet[fromId];
		cellColorSet[fromId] = -1;

		invalidateLedStates();
		setOverlayMessage("Moved cell", portLabel(portAssignments[toId]));
	}

	// Engine thread — enqueues a full module reset on the GUI thread via guiQueue.
	// Removes all current-scene cables, clears all stored scenes and port assignments,
	// resets scene and preset selection, and clears LED state.
	void requestReset() {
		if (!guiQueue.full()) {
			guiQueue.push([this]() {
				static const uint64_t empty[MATRIX_COUNT] = {};
				captureScene(currentScene);
				applyConnectionDiff(sceneConnections[currentScene], empty);
				memset(sceneConnections, 0, sizeof(sceneConnections));
				for (int i = 0; i < MATRIX_COUNT; i++) portAssignments[i].clear();
				for (int i = 0; i < TOTAL_MAPS; i++) trackingProcessor.clearMap(i);
				currentScene   = 0;
				feedbackPreset = 0;
				invalidateLedStates();
				std::fill(portHasCable, portHasCable  + MATRIX_COUNT, false);
			});
		}
	}
};

// Static field to communicate a pending state accross instances
std::map<Context*, SpliceKitModule::CrossPendingState> SpliceKitModule::crossPending;


// Overlay widget added directly to APP->scene->rack — drawn in rack coordinates.
// Activated by the space key; hides cables and draws cell→port assignment splines.
struct SpliceKitVizOverlay : TransparentWidget {
	SpliceKitModule* module = nullptr;
	// Non-owning pointer to the host widget (for position).
	Widget* hostWidget = nullptr;
	int hoveredCellId = -1;

	void step() override {
		// Track parent size so NVG scissor doesn't clip our drawings.
		if (parent) { box.pos = Vec(0.f, 0.f); box.size = parent->box.size; }
		TransparentWidget::step();
	}

	static float cellCenterX(int col) {
		return 24.3f + col * (245.7f - 24.3f) / 7.f;
	}

	static Vec cellCenter(int cellId) {
		int r = cellId / MATRIX_SIZE;
		int c = cellId % MATRIX_SIZE;
		return Vec(cellCenterX(c), 54.5f + r * (277.4f - 54.5f) / 7.f);
	}

	// Scene-button row shares the matrix's column spacing/x-positions.
	static Vec sceneCenter(int sceneId) {
		return Vec(cellCenterX(sceneId), 324.3f);
	}

	// Deterministic value in [-1, +1] for a given cell — golden-ratio sequence
	// gives an even spread with no visible repetition pattern.
	static float cellJitter(int cellId) {
		return std::fmod(cellId * 0.618033988f, 1.f) * 2.f - 1.f;
	}

	void drawSpline(NVGcontext* vg, Vec a, Vec b, NVGcolor col, float jitter, bool highlighted = false) {
		float dx = b.x - a.x;
		float dist = a.minus(b).norm();
		float bulge = dist * 0.18f * jitter;
		Vec cp1 = a.plus(Vec(dx * 0.5f,  bulge));
		Vec cp2 = b.plus(Vec(-dx * 0.5f, bulge));

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

	// Draws a short arc between two cell-button centers on the module face.
	// Used to show which cells are wired together in the active scene.
	void drawCellArc(NVGcontext* vg, Vec a, Vec b) {
		Vec diff = b.minus(a);
		Vec perp = Vec(-diff.y, diff.x).mult(0.12f);  // ~12% of distance, perpendicular
		Vec cp = a.plus(diff.mult(0.5f)).plus(perp);

		nvgBeginPath(vg);
		nvgMoveTo(vg, a.x, a.y);
		nvgQuadTo(vg, cp.x, cp.y, b.x, b.y);
		nvgLineCap(vg, NVG_ROUND);
		// Glow
		nvgStrokeColor(vg, nvgRGBAf(1.f, 0.9f, 0.4f, 0.25f));
		nvgStrokeWidth(vg, 6.f);
		nvgStroke(vg);
		// Core
		nvgStrokeColor(vg, nvgRGBAf(1.f, 0.95f, 0.5f, 0.9f));
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !visible || !module || !hostWidget) return;
		NVGcontext* vg = args.vg;

		Vec origin = hostWidget->box.pos;
		int hoveredCell = hoveredCellId;
		bool anyHover = (hoveredCell >= 0 && module->portAssignments[hoveredCell].isValid());

		// Build bitmask of cells connected to the hovered cell in the current scene.
		uint64_t connectedMask = anyHover
			? module->sceneConnections[module->currentScene][hoveredCell]
			: 0;

		// Helper: resolve a cell's port widget, or return nullptr.
		auto getPortWidget = [&](int i) -> PortWidget* {
			const PortAssignment& pa = module->portAssignments[i];
			if (!pa.isValid()) return nullptr;
			ModuleWidget* mw = APP->scene->rack->getModule(pa.moduleId);
			if (!mw) return nullptr;
			auto list = (pa.type == engine::Port::OUTPUT) ? mw->getOutputs() : mw->getInputs();
			for (PortWidget* pw : list) {
				if (pw->portId == pa.portId) return pw;
			}
			return nullptr;
		};

		// Helper: draw the spline + endpoint dots for cell i.
		auto drawCell = [&](int i, bool highlighted) {
			PortWidget* portPw = getPortWidget(i);
			if (!portPw) return;
			if (module->pendingCellId == i && module->blinkPhase >= 0.5f) return;

			ModuleWidget* portMw = APP->scene->rack->getModule(module->portAssignments[i].moduleId);
			if (!portMw) return;

			Vec cellPos = origin.plus(cellCenter(i));
			Vec portPos = portMw->box.pos.plus(portPw->box.getCenter());

			int cs = module->getCellColorSet(i);
			NVGcolor col = COLOR_SETS[cs].color;

			drawSpline(vg, cellPos, portPos, col, cellJitter(i), highlighted);

			float cellR = highlighted ? 4.f  : 2.5f;
			float portR = highlighted ? 5.5f : 3.5f;

			// Cell-side dot (white)
			nvgBeginPath(vg);
			nvgCircle(vg, cellPos.x, cellPos.y, cellR);
			nvgFillColor(vg, nvgRGBAf(1.f, 1.f, 1.f, 0.9f));
			nvgFill(vg);

			// Port-side dot (colored with white ring)
			nvgBeginPath(vg);
			nvgCircle(vg, portPos.x, portPos.y, portR);
			nvgFillColor(vg, nvgRGBAf(col.r, col.g, col.b, 0.9f));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBAf(1.f, 1.f, 1.f, 0.6f));
			nvgStrokeWidth(vg, highlighted ? 1.4f : 0.8f);
			nvgStroke(vg);
		};

		if (anyHover) {
			// Pass 1: draw unrelated cells dimmed.
			nvgGlobalAlpha(vg, 0.18f);
			for (int i = 0; i < MATRIX_COUNT; i++) {
				if (i == hoveredCell || ((connectedMask >> i) & 1)) continue;
				drawCell(i, false);
			}
			nvgGlobalAlpha(vg, 1.f);

			// Pass 2: draw connected cells at full opacity (not bold).
			for (int i = 0; i < MATRIX_COUNT; i++) {
				if (i == hoveredCell || !((connectedMask >> i) & 1)) continue;
				drawCell(i, false);
			}

			// Pass 3: draw hovered cell highlighted on top.
			drawCell(hoveredCell, true);

			// Pass 4: draw cell-to-cell arcs for each active connection.
			Vec hovPos = origin.plus(cellCenter(hoveredCell));
			for (int i = 0; i < MATRIX_COUNT; i++) {
				if (!((connectedMask >> i) & 1)) continue;
				drawCellArc(vg, hovPos, origin.plus(cellCenter(i)));
			}
		}
		else {
			// No hover — draw everything at normal opacity.
			for (int i = 0; i < MATRIX_COUNT; i++) {
				drawCell(i, false);
			}
		}
	}
};


struct SpliceKitWidget;

// Scene button with right-click context menu and drag-and-drop support.
//   Left-drag  A → B : copy scene A's connections to scene B.
//   Right-click      : open the per-scene context menu.
struct SpliceKitSceneButton : app::SvgSwitch {
	SpliceKitModule* module = nullptr;
	SpliceKitWidget* mw = nullptr;
	int sceneId = -1;
	bool dragging = false;

	SpliceKitSceneButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton1.svg")));
		fb->removeChild(shadow);
		delete shadow;
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && module) {
			e.consume(this);
			createSceneMenu();
			return;
		}
		SvgSwitch::onButton(e);
	}

	void onDragStart(const event::DragStart& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) dragging = true;
		SvgSwitch::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		dragging = false;
		SvgSwitch::onDragEnd(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		SvgSwitch::drawLayer(args, layer);
		if (layer == 1 && dragging) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, RECT_ARGS(box.zeroPos().grow(2.f)), 3.8f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.4f));
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);
		}
	}

	// Left-drag A → B copies scene A's connections to scene B.
	void onDragDrop(const event::DragDrop& e) override {
		SvgSwitch::onDragDrop(e);
		if (!module) return;
		auto* src = dynamic_cast<SpliceKitSceneButton*>(e.origin);
		if (!src || src->module != module || src->sceneId == sceneId) return;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			module->copyScene(src->sceneId, sceneId);
		}
	}

	void createSceneMenu();
};

// Matrix cell button with right-click context menu and drag-and-drop support.
//   Left-drag        A → B : toggle connection between A and B.
//   Shift+left-drag  A → B : move A's port, label, color, and scene connections
//                            onto B (B's assignment is discarded). MIDI mappings
//                            stay on their physical cell positions, not the port.
//   Right-click            : open the per-cell context menu.
struct SpliceKitCellButton : app::SvgSwitch {
	SpliceKitModule* module = nullptr;
	SpliceKitWidget* mw = nullptr;
	int cellId = -1;
	bool shiftDrag = false;
	bool dragging  = false;

	SpliceKitCellButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton1.svg")));
		fb->removeChild(shadow);
		delete shadow;
	}

	void onEnter(const event::Enter& e) override;
	void onLeave(const event::Leave& e) override;

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
			shiftDrag = (e.mods & RACK_MOD_SHIFT) != 0;
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			if (e.action == GLFW_PRESS && module) {
				createCellMenu();
				e.consume(this);
			}
			return;
		}
		SvgSwitch::onButton(e);
	}

	// Shift+left-drag: suppress cell activation so the drag gesture is a pure move.
	void onDragStart(const event::DragStart& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			dragging = true;
			if (shiftDrag) return;
		}
		SvgSwitch::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		dragging = false;
		SvgSwitch::onDragEnd(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		SvgSwitch::drawLayer(args, layer);
		if (layer == 1 && dragging) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, RECT_ARGS(box.zeroPos().grow(2.f)), 3.8f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.4f));
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);
		}
	}

	// Left-drag → toggle connection; shift+left-drag → move cell assignment.
	void onDragDrop(const event::DragDrop& e) override {
		SvgSwitch::onDragDrop(e);
		if (!module) return;
		auto* src = dynamic_cast<SpliceKitCellButton*>(e.origin);
		if (!src || src->module != module || src->cellId == cellId) return;
		int a = src->cellId, b = cellId;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (src->shiftDrag) {
				if (!module->guiQueue.full()) {
					module->guiQueue.push([=]() { module->moveCell(a, b); });
				}
			}
			else {
				module->clearPendingLocal();
				module->clearPendingCrossGui();
				if (!module->guiQueue.full()) {
					module->guiQueue.push([=]() { module->toggleConnection(a, b); });
				}
			}
		}
	}

	void createCellMenu();
};


struct SpliceKitWidget : ThemedModuleWidget<SpliceKitModule>, OverlayMessageProvider {
	SpliceKitVizOverlay* vizOverlay = nullptr;
	bool vizMode = false;

	SpliceKitWidget(SpliceKitModule* module) : ThemedModuleWidget(module, "SpliceKit") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int r = 0; r < MATRIX_SIZE; r++) {
			for (int c = 0; c < MATRIX_SIZE; c++) {
				int cellId = r * MATRIX_SIZE + c;
				Vec pos = SpliceKitVizOverlay::cellCenter(cellId);
				auto* btn = createParam<SpliceKitCellButton>(pos.minus(Vec(13.25f, 13.25f)), module, SpliceKitModule::PARAM_MATRIX + cellId);
				btn->module = module;
				btn->mw = this;
				btn->cellId = cellId;
				addParam(btn);
				addChild(createLightCentered<SaturatedMatrixButtonLight<SpliceKitModule>>(pos, module, SpliceKitModule::LIGHT_MATRIX + cellId * 3));
			}
		}
		for (int i = 0; i < MATRIX_SIZE; i++) {
			Vec pos = SpliceKitVizOverlay::sceneCenter(i);
			auto* sb = createParamCentered<SpliceKitSceneButton>(pos, module, SpliceKitModule::PARAM_SCENE + i);
			sb->module = module;
			sb->mw = this;
			sb->sceneId = i;
			addParam(sb);
			addChild(createLightCentered<MatrixButtonLight<WhiteLight, SpliceKitModule>>(pos, module, SpliceKitModule::LIGHT_SCENE + i));
		}

		if (module) {
			OverlayMessageWidget::registerProvider(this);

			vizOverlay = new SpliceKitVizOverlay;
			vizOverlay->module = module;
			vizOverlay->hostWidget = this;
			vizOverlay->visible = false;
			APP->scene->rack->addChild(vizOverlay);
		}
	}

	~SpliceKitWidget() {
		if (vizOverlay) {
			APP->scene->rack->removeChild(vizOverlay);
			delete vizOverlay;
			vizOverlay = nullptr;
		}
		if (module) {
			APP->scene->rack->getCableContainer()->visible = true;
			OverlayMessageWidget::unregisterProvider(this);
		}
	}

	int nextOverlayMessageId() override {
		if (module && module->overlayMessageId == 0) {
			module->overlayMessageId = -1;
			return 0;
		}
		return -1;
	}

	void getOverlayMessage(int id, Message& m) override {
		if (id != 0) return;
		m = module->overlayMessage;
	}

	void onDeselect(const event::Deselect& e) override {
		ThemedModuleWidget<SpliceKitModule>::onDeselect(e);
		if (module) module->portSelectProcessor.processDeselect();
	}

	SpliceKitCellButton* findCellButton(int cellId) {
		for (Widget* w : children) {
			auto* btn = dynamic_cast<SpliceKitCellButton*>(w);
			if (btn && btn->cellId == cellId) return btn;
		}
		return nullptr;
	}

	void setVizMode(bool active) {
		int hovered = vizOverlay ? vizOverlay->hoveredCellId : -1;
		vizMode = active;
		if (vizOverlay) vizOverlay->visible = active;
		APP->scene->rack->getCableContainer()->visible = !active;
		if (hovered >= 0 && hovered < MATRIX_COUNT) {
			SpliceKitCellButton* btn = findCellButton(hovered);
			if (btn) {
				if (active) btn->destroyTooltip();
				else btn->createTooltip();
			}
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.key == GLFW_KEY_SPACE && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			setVizMode(!vizMode);
			e.consume(this);
			return;
		}
		ThemedModuleWidget<SpliceKitModule>::onHoverKey(e);
	}

	void step() override {
		ThemedModuleWidget<SpliceKitModule>::step();
		if (!module) return;

		module->portSelectProcessor.step();

		// Execute actions queued from the engine thread
		while (module->guiQueue.size() > 0) {
			module->guiQueue.shift()();
		}

		// Update cable presence for each assigned cell (read by process() for light colors).
		// Build a set of all assigned (moduleId, portId*2+type) pairs once so each cable-end
		// lookup is O(log n) instead of an O(n) scan over all assignments.
		std::set<std::pair<int64_t, int>> assignedPorts;
		for (int j = 0; j < MATRIX_COUNT; j++) {
			const PortAssignment& pb = module->portAssignments[j];
			if (pb.isValid()) {
				assignedPorts.insert({pb.moduleId, pb.portId * 2 + (int)pb.type});
			}
		}
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const PortAssignment& pa = module->portAssignments[i];
			if (!pa.isValid()) {
				module->portHasCable[i] = false;
				continue;
			}
			ModuleWidget* mw = APP->scene->rack->getModule(pa.moduleId);
			if (!mw) {
				module->portHasCable[i] = false;
				continue;
			}
			auto ports = (pa.type == engine::Port::OUTPUT) ? mw->getOutputs() : mw->getInputs();
			module->portHasCable[i] = false;
			for (PortWidget* pw : ports) {
				if (pw->portId != pa.portId) continue;
				for (CableWidget* cw : APP->scene->rack->getCablesOnPort(pw)) {
					PortWidget* other = (pa.type == engine::Port::OUTPUT) ? cw->inputPort : cw->outputPort;
					if (!other || !other->module) continue;
					if (assignedPorts.count({other->module->getId(), other->portId * 2 + (int)other->type})) {
						module->portHasCable[i] = true;
						break;
					}
				}
				break;
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		SpliceKitModule* module = this->module;
		if (!module) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(StoermelderPackOne::Rack::createStickyMidiMenuItem("MIDI Input",  &module->trackingProcessor.getInput()));
		menu->addChild(StoermelderPackOne::Rack::createStickyMidiMenuItem("MIDI Output", &module->midiOutput));
		menu->addChild(createSubmenuItem("MIDI Preset", "", [=](Menu* menu) {
			auto& presets = getPresets();
			for (int i = 0; i < (int)presets.size(); i++) {
				int preset = i;
				std::string name = presets[i].name;
				menu->addChild(createCheckMenuItem(name, "",
					[=]() { return module->feedbackPreset == preset; },
					[=]() {
						module->feedbackPreset = preset;
						module->invalidateLedStates();
					}
				));
			}
			if (module->feedbackPreset == PRESET_IDX_CUSTOM) {
				std::string name = module->customPreset.name.empty() ? "Custom preset" : module->customPreset.name;
				menu->addChild(createCheckMenuItem(name, "",
					[=]() { return true; },
					[=]() {}
				));
			}
			menu->addChild(new MenuSeparator);
			const MidiOutPreset* activeP = module->getActivePreset();
			bool hasLayout = activeP && activeP->hasLayout();
			menu->addChild(createMenuItem("Apply note layout as MIDI input mappings", "",
				[=]() { module->applyPresetLayout(); },
				!hasLayout
			));
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuItem("Load preset from file...", "",
				[=]() {
					osdialog_filters* filters = osdialog_filters_parse("JSON:json");
					char* pathC = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
					osdialog_filters_free(filters);
					if (!pathC) return;
					std::string path = pathC;
					free(pathC);
					std::vector<uint8_t> bytes = system::readFile(path);
					if (bytes.empty()) return;
					std::string text(bytes.begin(), bytes.end());
					json_error_t err;
					json_t* root = json_loads(text.c_str(), 0, &err);
					if (!root) return;
					MidiOutPreset p;
					p.fromJson(root);
					json_decref(root);
					module->customPreset     = p;
					module->customPresetJson = text;
					module->feedbackPreset   = PRESET_IDX_CUSTOM;
					module->invalidateLedStates();
				}
			));
			bool canSave = module->feedbackPreset > 0;
			menu->addChild(createMenuItem("Save preset to file...", "",
				[=]() {
					osdialog_filters* filters = osdialog_filters_parse("JSON:json");
					std::string defName = (module->feedbackPreset == PRESET_IDX_CUSTOM)
						? module->customPreset.name
						: getPresets()[module->feedbackPreset].name;
					if (!defName.empty()) defName += ".json";
					char* pathC = osdialog_file(OSDIALOG_SAVE, NULL,
						defName.empty() ? "preset.json" : defName.c_str(), filters);
					osdialog_filters_free(filters);
					if (!pathC) return;
					std::string path = pathC;
					free(pathC);
					std::string text = (module->feedbackPreset == PRESET_IDX_CUSTOM)
						? module->customPresetJson
						: CONTROLLER_PRESET_JSON[module->feedbackPreset];
					system::writeFile(path, std::vector<uint8_t>(text.begin(), text.end()));
				},
				!canSave
			));
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createCheckMenuItem("Sequential MIDI learn", "",
			[=]() { return module->midiLearnMode; },
			[=]() {
				if (module->midiLearnMode) module->disableLearn();
				else module->startGlobalLearn();
			}
		));
		menu->addChild(createCheckMenuItem("Sequential port learn", "",
			[=]() { return module->portLearnMode; },
			[=]() {
				if (module->portLearnMode) module->disablePortLearn();
				else module->startGlobalPortLearn(this);
			}
		));
		menu->addChild(new MenuSeparator);
		menu->addChild(createCheckMenuItem("Visualize", "Space",
			[=]() { return vizMode; },
			[=]() { setVizMode(!vizMode); }
		));
		menu->addChild(createCheckMenuItem("Show overlay messages", "",
			[=]() { return module->overlayEnabled; },
			[=]() { module->overlayEnabled = !module->overlayEnabled; }
		));
		menu->addChild(createCheckMenuItem("Cross-instance patching", "",
			[=]() { return module->crossInstanceEnabled; },
			[=]() {
				module->crossInstanceEnabled = !module->crossInstanceEnabled;
				if (!module->crossInstanceEnabled) {
					module->clearPendingLocal();
					module->clearPendingCrossGui();
				}
			}
		));
		menu->addChild(createSubmenuItem("Button mode", "", [=](Menu* menu) {
			menu->addChild(createCheckMenuItem("Toggle", "",
				[=]() { return module->buttonMode == SpliceKitModule::BUTTON_TOGGLE; },
				[=]() {
					module->buttonMode = SpliceKitModule::BUTTON_TOGGLE;
					module->clearPendingLocal();
					module->clearPendingCrossGui();
				}
			));
			menu->addChild(createCheckMenuItem("Momentary", "",
				[=]() { return module->buttonMode == SpliceKitModule::BUTTON_MOMENTARY; },
				[=]() {
					module->buttonMode = SpliceKitModule::BUTTON_MOMENTARY;
					module->clearPendingLocal();
					module->clearPendingCrossGui();
				}
			));
		}));
	}
};


void SpliceKitCellButton::onEnter(const event::Enter& e) {
	if (mw && mw->vizOverlay) mw->vizOverlay->hoveredCellId = cellId;
	SvgSwitch::onEnter(e);
	if (mw && mw->vizMode) destroyTooltip();
}

void SpliceKitCellButton::onLeave(const event::Leave& e) {
	if (mw && mw->vizOverlay && mw->vizOverlay->hoveredCellId == cellId) mw->vizOverlay->hoveredCellId = -1;
	SvgSwitch::onLeave(e);
}


void SpliceKitSceneButton::createSceneMenu() {
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(string::f("Scene %d", sceneId + 1)));
	menu->addChild(new MenuSeparator);

	menu->addChild(createMenuItem("Clear", "", [=]() {
		uint64_t empty[MATRIX_COUNT] = {};
		module->reconcileScene(sceneId, empty);
	}));
	menu->addChild(createMenuItem("Copy", "", [=]() {
		if (sceneId == module->currentScene) module->captureScene(sceneId);
		memcpy(module->sceneClipboard, module->sceneConnections[sceneId], MATRIX_COUNT * sizeof(uint64_t));
		module->sceneClipboardValid = true;
	}));
	menu->addChild(createMenuItem("Paste", "", [=]() {
		if (!module->sceneClipboardValid) return;
		module->reconcileScene(sceneId, module->sceneClipboard);
	}, !module->sceneClipboardValid));

	menu->addChild(new MenuSeparator);

	int mapId = MATRIX_COUNT + sceneId;
	auto& midiMap = module->trackingProcessor.getMap(mapId);
	std::string midiLabel;
	if (midiMap.type == MidiTrackingType::CC)   midiLabel = string::f("CC %d",   midiMap.param);
	if (midiMap.type == MidiTrackingType::NOTE)  midiLabel = string::f("Note %d", midiMap.param);

	std::string learnLabel = midiLabel.empty() ? "Learn MIDI" : string::f("Learn MIDI (%s)", midiLabel.c_str());
	menu->addChild(createCheckMenuItem(learnLabel, "",
		[=]() { return module->learningId == MATRIX_COUNT + sceneId; },
		[=]() {
			if (module->learningId == MATRIX_COUNT + sceneId) module->disableLearn();
			else module->enableLearn(MATRIX_COUNT + sceneId);
		}
	));
	if (!midiLabel.empty()) {
		menu->addChild(createMenuItem("Clear MIDI", "", [=]() {
			module->trackingProcessor.clearMap(MATRIX_COUNT + sceneId);
		}));
	}
}

void SpliceKitCellButton::createCellMenu() {
	// Intended: this is what makes the per-cell "Start sequential learn..." item begin at
	// this cell rather than cell 0 — the module-level context menu has no such click to
	// anchor on, which is the actual bug (see startGlobalLearn/startGlobalPortLearn).
	module->lastClickedCell = cellId;
	auto& pa = module->portAssignments[cellId];
	std::string label = SpliceKitModule::portLabel(pa);

	auto& midiMap = module->trackingProcessor.getMap(cellId);
	std::string midiLabel;
	if (midiMap.type == MidiTrackingType::CC) {
		midiLabel = string::f("CC %d", midiMap.param);
	}
	else if (midiMap.type == MidiTrackingType::NOTE) {
		midiLabel = string::f("Note %d", midiMap.param);
	}

	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(label.empty() ? string::f("Cell %d (unassigned)", cellId + 1) : label));

	struct LabelField : ui::TextField {
		SpliceKitModule* module;
		int id;
		void onSelectKey(const event::SelectKey& e) override {
			if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
				module->cellLabels[id] = text;
				ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
				overlay->requestDelete();
				e.consume(this);
			}
			if (!e.getTarget()) {
				ui::TextField::onSelectKey(e);
			}
		}
		void step() override {
			APP->event->setSelectedWidget(this);
			TextField::step();
		}
	};

	menu->addChild(new MenuSeparator);
	auto* lf = new LabelField;
	lf->module = module;
	lf->id = cellId;
	lf->text = module->cellLabels[cellId];
	lf->placeholder = "Custom label...";
	lf->box.size.x = 100;
	menu->addChild(lf);

	menu->addChild(createSubmenuItem("Color", "", [=](Menu* menu) {
		int id = cellId;
		SpliceKitModule* mod = module;
		menu->addChild(createCheckMenuItem("Auto", "",
			[=]() { return mod->cellColorSet[id] < 0; },
			[=]() { mod->cellColorSet[id] = -1; }
		));
		menu->addChild(new MenuSeparator);
		for (int cs = 0; cs < COLOR_SET_COUNT; cs++) {
			auto* item = createCheckMenuItem<ui::ColorDotMenuItem>(COLOR_SETS[cs].name, "",
				[=]() { return mod->cellColorSet[id] == cs; },
				[=]() { mod->cellColorSet[id] = (int8_t)cs; }
			);
			item->color = COLOR_SETS[cs].color;
			menu->addChild(item);
		}
	}));

	menu->addChild(new MenuSeparator);
	bool hasConns = module->sceneConnections[module->currentScene][cellId] != 0;
	menu->addChild(createSubmenuItem("Remove cable", "",
		[=](Menu* menu) {
			uint64_t mask = module->sceneConnections[module->currentScene][cellId];
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if (!((mask >> j) & 1)) continue;
				std::string connLabel = SpliceKitModule::portLabel(module->portAssignments[j]);
				int cid = cellId, jid = j;
				SpliceKitModule* mod = module;
				menu->addChild(createMenuItem(connLabel, "", [=]() {
					mod->setConnection(mod->currentScene, cid, jid, false);
					mod->removeCableBetween(cid, jid);
				}));
			}
		},
		!hasConns
	));
	menu->addChild(createMenuItem("Remove all cables", "",
		[=]() { module->removeCellConnections(cellId); },
		!hasConns
	));

	menu->addChild(new MenuSeparator);
	menu->addChild(createMenuLabel("Module port"));
	menu->addChild(createMenuItem("Learn", "", [=]() {
		module->enablePortLearn(cellId, mw);
	}));
	menu->addChild(createMenuItem("Start sequential learn...", "", [=]() {
		module->startGlobalPortLearn(mw);
	}));
	menu->addChild(createMenuItem("Clear", "", [=]() {
		module->portAssignments[cellId].clear();
	}, !pa.isValid()));

	menu->addChild(new MenuSeparator);
	menu->addChild(createMenuLabel("MIDI"));
	std::string learnMidiLabel = midiLabel.empty() ? "Learn" : string::f("Learn MIDI (%s)", midiLabel.c_str());
	menu->addChild(createCheckMenuItem(learnMidiLabel, "",
		[=]() { return module->learningId == cellId; },
		[=]() {
			if (module->learningId == cellId) module->disableLearn();
			else module->enableLearn(cellId);
		}
	));
	menu->addChild(createMenuItem("Start sequential learn...", "", [=]() {
		module->startGlobalLearn();
	}));
	menu->addChild(createMenuItem("Clear", "", [=]() {
		module->trackingProcessor.clearMap(cellId);
	}, midiLabel.empty()));
}


} // namespace SpliceKit
} // namespace StoermelderPackOne

Model* modelSpliceKit = createModel<StoermelderPackOne::SpliceKit::SpliceKitModule, StoermelderPackOne::SpliceKit::SpliceKitWidget>("SpliceKit");