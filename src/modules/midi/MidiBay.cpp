#include "../../plugin.hpp"
#include "../../components/MatrixButton.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../ui/ModuleSelectProcessor.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "MidiTrackingProcessor.hpp"
#include "MidiBay_controllers.hpp"

namespace StoermelderPackOne {
namespace MidiBay {

// Each LED uses a single SCHEME channel in isolation. Mixing channels is avoided
// because SCHEME_BLUE (#29b2ef) already contains G=178/255, which saturates the
// green channel and produces teal/white when combined with the green channel.
struct LedColor { float r, g, b; };

static const float LED_BRIGHT      = 1.f;
static const float LED_DIM         = 0.25f;  // assigned port, no cable attached
static const float LED_SCENE_DIM   = 0.25f;  // inactive scene that has stored connections

// R=red-orange (SCHEME_RED), G=yellow-green (SCHEME_GREEN), B=sky-blue (SCHEME_BLUE)
static const LedColor LED_OFF         = {0.f,        0.f,        0.f       };
static const LedColor LED_OUTPUT      = {LED_BRIGHT, 0.f,        0.f       };
static const LedColor LED_OUTPUT_DIM  = {LED_DIM,    0.f,        0.f       };
static const LedColor LED_INPUT       = {0.f,        0.f,        LED_BRIGHT};
static const LedColor LED_INPUT_DIM   = {0.f,        0.f,        LED_DIM   };
static const LedColor LED_PENDING     = {LED_BRIGHT, LED_BRIGHT, LED_BRIGHT};
static const LedColor LED_PORT_LEARN  = {0.f,        0.f,        LED_BRIGHT};
static const LedColor LED_MIDI_LEARN  = {0.f,        LED_BRIGHT, 0.f       };

struct PortAssignment {
	int64_t moduleId = -1;
	engine::Port::Type type = engine::Port::INPUT;
	int portId = -1;

	bool isValid() const { return moduleId >= 0 && portId >= 0; }
	void clear() { moduleId = -1; portId = -1; }
};

static const int TOTAL_MAPS = MATRIX_COUNT + MATRIX_SIZE;  // 64 cells + 8 scenes

struct MidiBayModule : Module, MidiTrackingProcessorHandler {
	struct MidiBayCellQuantity : ParamQuantity {
		std::string getLabel() override {
			if (!module) return ParamQuantity::getLabel();
			auto* m = static_cast<MidiBayModule*>(module);
			int cellId = paramId - PARAM_MATRIX;
			const PortAssignment& pa = m->portAssignments[cellId];
			if (pa.isValid()) return portLabel(pa);
			return string::f("Cell %d", cellId + 1);
		}
		std::string getDisplayValueString() override { return ""; }
		std::string getDescription() override {
			if (!module) return "";
			auto* m = static_cast<MidiBayModule*>(module);
			int cellId = paramId - PARAM_MATRIX;
			auto& mm = m->trackingProcessor.getMap(cellId);
			if (mm.type == MidiTrackingType::CC)   return string::f("MIDI: CC %d",   mm.param);
			if (mm.type == MidiTrackingType::NOTE)  return string::f("MIDI: Note %d", mm.param);
			return "MIDI: (unmapped)";
		}
	};

	struct MidiBaySceneQuantity : ParamQuantity {
		std::string getLabel() override {
			if (!module) return ParamQuantity::getLabel();
			int sceneId = paramId - PARAM_SCENE;
			return string::f("Scene %d", sceneId + 1);
		}
		std::string getDisplayValueString() override { return ""; }
		std::string getDescription() override {
			if (!module) return "";
			auto* m = static_cast<MidiBayModule*>(module);
			int sceneId = paramId - PARAM_SCENE;
			auto& mm = m->trackingProcessor.getMap(MATRIX_COUNT + sceneId);
			if (mm.type == MidiTrackingType::CC)   return string::f("MIDI: CC %d",   mm.param);
			if (mm.type == MidiTrackingType::NOTE)  return string::f("MIDI: Note %d", mm.param);
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

	struct MidiBayOutput : midi::Output {
		int* ledState = nullptr;

		std::vector<int> getChannels() override {
			std::vector<int> channels = midi::Output::getChannels();
			channels.emplace(channels.begin(), -1);
			return channels;
		}

		void setDeviceId(int deviceId) override {
			midi::Output::setDeviceId(deviceId);
			if (ledState) std::fill(ledState, ledState + MATRIX_COUNT, -1);
		}
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	bool midiLearnMode = false;
	int learningId = -1;

	/** [Stored to JSON] */
	int currentScene = 0;

	/** [Stored to JSON] */
	StoermelderPackOne::MidiTrackingProcessor<TOTAL_MAPS> trackingProcessor;

	/** [Stored to JSON] */
	MidiBayOutput midiOutput;

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

	// -1 = no pending; >=0 = first button pressed, awaiting second press
	int pendingCellId = -1;

	int portLearningId = -1;
	StoermelderPackOne::PortSelectProcessor portSelectProcessor;

	/** [Stored to JSON] */
	int feedbackPreset = 0;
	// -1 forces a send on the first light-divider tick after load.
	int cellLedState[MATRIX_COUNT];

	/** [Stored to Json] */
	bool overlayEnabled;
	int overlayMessageId = -1;
	OverlayMessageProvider::Message overlayMessage;

	MidiBayModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		std::fill(cellLedState, cellLedState + MATRIX_COUNT, -1);
		for (int i = 0; i < MATRIX_COUNT; i++) {
			configParam<MidiBayCellQuantity>(PARAM_MATRIX + i, 0.f, 1.f, 0.f);
		}
		for (int i = 0; i < MATRIX_SIZE; i++) {
			configParam<MidiBaySceneQuantity>(PARAM_SCENE + i, 0.f, 1.f, 0.f);
		}

		trackingProcessor.handler = this;
		trackingProcessor.enableCc();
		trackingProcessor.enableNotes();
		midiOutput.ledState = cellLedState;
		processDivider.setDivision(256);
	}

	void onReset() override {
		disableLearn();
		disablePortLearn();
		pendingCellId = -1;
		requestReset();
	}

	void process(const ProcessArgs& args) override {
		trackingProcessor.process(args.frame);

		if (processDivider.process()) {
			for (int i = 0; i < MATRIX_COUNT; i++) {
				if (buttonTriggers[i].process(params[PARAM_MATRIX + i].getValue() > 0.5f)) {
					if (learningId == i) {
						disableLearn();
					} 
					else if (portLearningId == i) {
						disablePortLearn();
					} 
					else {
						triggerCell(i);
					}
					blinkPhase = 0.f;
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

			bool blinkOn = blinkPhase < 0.5f;
			for (int i = 0; i < MATRIX_COUNT; i++) {
				bool assigned = portAssignments[i].isValid();
				bool isOutput = portAssignments[i].type == engine::Port::OUTPUT;
				bool hasCable = portHasCable[i];
				LedColor col;
				int stateId;
				if (pendingCellId == i) {
					col = blinkOn ? LED_PENDING : LED_OFF;
					stateId = LED_STATE_PENDING;
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
					if (isOutput) {
						if (hasCable) { col = LED_OUTPUT;     stateId = LED_STATE_OUT;     }
						else          { col = LED_OUTPUT_DIM; stateId = LED_STATE_OUT_DIM; }
					} 
					else {
						if (hasCable) { col = LED_INPUT;     stateId = LED_STATE_IN;     }
						else          { col = LED_INPUT_DIM; stateId = LED_STATE_IN_DIM; }
					}
				} 
				else {
					col = LED_OFF;
					stateId = LED_STATE_OFF;
				}
				if (stateId != cellLedState[i]) {
					cellLedState[i] = stateId;
					sendFeedback(i, stateId);
				}
				lights[LIGHT_MATRIX + i * 3 + 0].setBrightness(col.r);
				lights[LIGHT_MATRIX + i * 3 + 1].setBrightness(col.g);
				lights[LIGHT_MATRIX + i * 3 + 2].setBrightness(col.b);
			}
			for (int s = 0; s < MATRIX_SIZE; s++) {
				float bright;
				if (learningId == MATRIX_COUNT + s) {
					bright = blinkOn ? LED_BRIGHT : 0.f;
				}
				else if (s == currentScene) {
					bright = LED_BRIGHT;
				}
				else {
					bool hasConn = false;
					for (int i = 0; i < MATRIX_COUNT; i++) {
						if (sceneConnections[s][i]) { hasConn = true; break; }
					}
					bright = hasConn ? LED_SCENE_DIM : 0.f;
				}
				lights[LIGHT_SCENE + s].setBrightness(bright);
			}
		}
	}

	bool isConnected(int scene, int a, int b) {
		return (sceneConnections[scene][a] >> b) & 1;
	}

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

	// MidiTrackingProcessorHandler
	void processMapUpdate(StoermelderPackOne::MidiTrackingType type, uint16_t mapId, uint16_t value) override {
		if (value == 0) return;
		if (mapId < (uint16_t)MATRIX_COUNT) {
			triggerCell(mapId);
		} 
		else if (mapId < (uint16_t)TOTAL_MAPS) {
			requestSceneChange(mapId - MATRIX_COUNT);
		}
	}

	// MidiTrackingProcessorHandler
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

	// Learn MIDI for a single cell or scene button (id >= MATRIX_COUNT). Cancels any active learn first.
	void enableLearn(int id) {
		disableLearn();
		if (id < 0 || id >= TOTAL_MAPS) return;
		learningId = id;
		trackingProcessor.enableMapLearn(id);
	}

	// Start sequential global learn: assigns cells 0..MATRIX_COUNT-1 in order.
	void startGlobalLearn() {
		disableLearn();
		midiLearnMode = true;
		learningId = 0;
		trackingProcessor.enableMapLearn(0);
	}

	void disableLearn() {
		trackingProcessor.disableMapLearn();
		learningId = -1;
		midiLearnMode = false;
	}

	void enablePortLearn(int id, Widget* owner) {
		if (id < 0 || id >= MATRIX_COUNT) return;
		portLearningId = id;
		portSelectProcessor.setOwner(owner);
		portSelectProcessor.startLearn([=](PortWidget* pw, Vec) {
			if (!pw->module) return;
			portAssignments[portLearningId].moduleId = pw->module->getId();
			portAssignments[portLearningId].type = pw->type;
			portAssignments[portLearningId].portId = pw->portId;
			portLearningId = -1;
		});
	}

	void disablePortLearn() {
		portSelectProcessor.disableLearn();
		portLearningId = -1;
	}

	bool isPortLearning(int id) {
		return portLearningId == id && portSelectProcessor.isLearning();
	}

	void sendFeedback(int cellId, int stateId) {
		static auto& presets = getPresets();
		if (feedbackPreset <= 0 || feedbackPreset >= (int)presets.size()) return;
		const MidiOutPreset& preset = presets[feedbackPreset];
		const MidiOutSpec& spec = preset.specs[stateId];
		if (spec.type == MIDI_OUT_NONE) return;
		int noteNum;
		switch (spec.noteMode) {
			case MIDI_OUT_FROM_SLOT: {
				auto m = trackingProcessor.getMap(cellId);
				if (m.type == MidiTrackingType::NONE) return;
				noteNum = m.param;
				break;
			}
			case MIDI_OUT_FIXED:
				noteNum = spec.note;
				break;
			default: return;
		}
		uint8_t status;
		switch (spec.type) {
			case MIDI_OUT_NOTE_ON:  status = 0x90; break;
			case MIDI_OUT_NOTE_OFF: status = 0x80; break;
			case MIDI_OUT_CC:       status = 0xB0; break;
			default: return;
		}
		midi::Message msg;
		msg.bytes[0] = status | (uint8_t)(spec.channel & 0x0F);
		msg.bytes[1] = (uint8_t)(noteNum  & 0x7F);
		msg.bytes[2] = (uint8_t)(spec.value & 0x7F);
		midiOutput.sendMessage(msg);
	}

	void applyPresetLayout() {
		static auto& presets = getPresets();
		if (feedbackPreset <= 0 || feedbackPreset >= (int)presets.size()) return;
		const MidiOutPreset& preset = presets[feedbackPreset];
		if (!preset.hasLayout()) return;
		trackingProcessor.clearMaps();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const auto& s = preset.cells[i];
			if (s.type != MidiTrackingType::NONE && s.number > 0)
				trackingProcessor.setMap(s.type, i, s.number);
		}
		for (int i = 0; i < MATRIX_SIZE; i++) {
			const auto& s = preset.scenes[i];
			if (s.type != MidiTrackingType::NONE && s.number > 0)
				trackingProcessor.setMap(s.type, MATRIX_COUNT + i, s.number);
		}
		std::fill(cellLedState, cellLedState + MATRIX_COUNT, -1);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "currentScene", json_integer(currentScene));

		json_t* mapsJ = json_array();
		for (int i = 0; i < TOTAL_MAPS; i++) {
			auto m = trackingProcessor.getMap(i);
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "type", json_integer((int)m.type));
			json_object_set_new(mapJ, "param", json_integer(m.param));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		json_t* portsJ = json_array();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			json_t* portJ = json_object();
			json_object_set_new(portJ, "moduleId", json_integer(portAssignments[i].moduleId));
			json_object_set_new(portJ, "type", json_integer((int)portAssignments[i].type));
			json_object_set_new(portJ, "portId", json_integer(portAssignments[i].portId));
			json_array_append_new(portsJ, portJ);
		}
		json_object_set_new(rootJ, "ports", portsJ);

		json_t* scenesJ = json_array();
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
			json_t* sceneJ = json_object();
			json_object_set_new(sceneJ, "connections", connJ);
			json_array_append_new(scenesJ, sceneJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);
		json_object_set_new(rootJ, "feedbackPreset", json_integer(feedbackPreset));
		json_object_set_new(rootJ, "midiInput",  trackingProcessor.getInput().toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		currentScene = json_integer_value(json_object_get(rootJ, "currentScene"));

		trackingProcessor.clearMaps();
		json_t* mapsJ = json_object_get(rootJ, "maps");
		if (mapsJ) {
			size_t i;
			json_t* mapJ;
			json_array_foreach(mapsJ, i, mapJ) {
				if (i >= (size_t)TOTAL_MAPS) break;
				auto type = (StoermelderPackOne::MidiTrackingType)json_integer_value(json_object_get(mapJ, "type"));
				auto param = (uint16_t)json_integer_value(json_object_get(mapJ, "param"));
				if (type != MidiTrackingType::NONE) {
					trackingProcessor.setMap(type, i, param);
				}
			}
		}

		json_t* portsJ = json_object_get(rootJ, "ports");
		if (portsJ) {
			size_t i;
			json_t* portJ;
			json_array_foreach(portsJ, i, portJ) {
				if (i >= MATRIX_COUNT) break;
				portAssignments[i].moduleId = json_integer_value(json_object_get(portJ, "moduleId"));
				portAssignments[i].type = (engine::Port::Type)json_integer_value(json_object_get(portJ, "type"));
				portAssignments[i].portId = json_integer_value(json_object_get(portJ, "portId"));
			}
		}

		feedbackPreset = json_integer_value(json_object_get(rootJ, "feedbackPreset"));
		feedbackPreset = clamp(feedbackPreset, 0, (int)getPresets().size() - 1);
		std::fill(cellLedState, cellLedState + MATRIX_COUNT, -1);

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ) trackingProcessor.getInput().fromJson(midiInputJ);
		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ) midiOutput.fromJson(midiOutputJ);

		memset(sceneConnections, 0, sizeof(sceneConnections));
		json_t* scenesJ = json_object_get(rootJ, "scenes");
		if (scenesJ) {
			size_t s;
			json_t* sceneJ;
			json_array_foreach(scenesJ, s, sceneJ) {
				if (s >= MATRIX_SIZE) break;
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

	static CableWidget* findCable(int64_t outputModuleId, int outputPortId, int64_t inputModuleId, int inputPortId) {
		ModuleWidget* outputMw = APP->scene->rack->getModule(outputModuleId);
		if (!outputMw) return nullptr;
		for (PortWidget* outPort : outputMw->getOutputs()) {
			if (outPort->portId != outputPortId) continue;
			for (CableWidget* cw : APP->scene->rack->getCablesOnPort(outPort)) {
				if (cw->inputPort && cw->inputPort->module &&
					cw->inputPort->module->getId() == inputModuleId &&
					cw->inputPort->portId == inputPortId) {
					return cw;
				}
			}
			break;
		}
		return nullptr;
	}

	static void removeCable(CableWidget* cw) {
		history::CableRemove* h = new history::CableRemove;
		h->setCable(cw);
		APP->history->push(h);
		APP->scene->rack->removeCable(cw);
		delete cw;
	}

	void addCableToPort(const PortAssignment* outPd, const PortAssignment* inPd) {
		ModuleWidget* outputMw = APP->scene->rack->getModule(outPd->moduleId);
		ModuleWidget* inputMw  = APP->scene->rack->getModule(inPd->moduleId);
		if (!outputMw || !inputMw) return;

		engine::Cable* c = new engine::Cable;
		c->outputId     = outPd->portId;
		c->outputModule = outputMw->module;
		c->inputId      = inPd->portId;
		c->inputModule  = inputMw->module;
		APP->engine->addCable(c);

		CableWidget* cw = new CableWidget;
		cw->color = APP->scene->rack->getNextCableColor();
		cw->setCable(c);
		APP->scene->rack->addCable(cw);
		history::CableAdd* h = new history::CableAdd;
		h->setCable(cw);
		APP->history->push(h);
	}

	void toggleConnection(int cellIdA, int cellIdB) {
		const PortAssignment& a = portAssignments[cellIdA];
		const PortAssignment& b = portAssignments[cellIdB];

		const PortAssignment* outPd = nullptr;
		const PortAssignment* inPd  = nullptr;
		int outCell, inCell;
		if (a.type == engine::Port::OUTPUT && b.type == engine::Port::INPUT) {
			outPd = &a; inPd = &b; outCell = cellIdA; inCell = cellIdB;
		} 
		else if (a.type == engine::Port::INPUT && b.type == engine::Port::OUTPUT) {
			outPd = &b; inPd = &a; outCell = cellIdB; inCell = cellIdA;
		} 
		else {
			return;
		}

		if (isConnected(currentScene, outCell, inCell)) {
			setConnection(currentScene, outCell, inCell, false);
			CableWidget* cw = findCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId);
			if (cw) removeCable(cw);

			overlayMessage.title = "Removed cable";
			overlayMessageId = 0;
			overlayMessage.subtitle[0] = portLabel(*outPd);
			overlayMessage.subtitle[1] = portLabel(*inPd);
		} 
		else {
			setConnection(currentScene, outCell, inCell, true);
			addCableToPort(outPd, inPd);
			overlayMessage.title = "Added cable";
			overlayMessageId = 0;
			overlayMessage.subtitle[0] = portLabel(*outPd);
			overlayMessage.subtitle[1] = portLabel(*inPd);
		}
	}

	void captureScene(int scene) {
		memset(sceneConnections[scene], 0, sizeof(sceneConnections[scene]));
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const PortAssignment& a = portAssignments[i];
			if (!a.isValid() || a.type != engine::Port::OUTPUT) continue;
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if (j == i) continue;
				const PortAssignment& b = portAssignments[j];
				if (!b.isValid() || b.type != engine::Port::INPUT) continue;
				if (findCable(a.moduleId, a.portId, b.moduleId, b.portId))
					setConnection(scene, i, j, true);
			}
		}
	}

	void applyConnectionDiff(const uint64_t* oldConns, const uint64_t* newConns) {
		for (int i = 0; i < MATRIX_COUNT; i++) {
			for (int j = i + 1; j < MATRIX_COUNT; j++) {
				bool was  = (oldConns[i] >> j) & 1;
				bool will = (newConns[i] >> j) & 1;
				if (was == will) continue;
				const PortAssignment& a = portAssignments[i];
				const PortAssignment& b = portAssignments[j];
				if (!a.isValid() || !b.isValid()) continue;
				const PortAssignment* outPd = nullptr;
				const PortAssignment* inPd  = nullptr;
				if (a.type == engine::Port::OUTPUT && b.type == engine::Port::INPUT) {
					outPd = &a; inPd = &b;
				} 
				else if (a.type == engine::Port::INPUT && b.type == engine::Port::OUTPUT) {
					outPd = &b; inPd = &a;
				} 
				else {
					continue;
				}
				if (was) {
					CableWidget* cw = findCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId);
					if (cw) removeCable(cw);
				} 
				else {
					addCableToPort(outPd, inPd);
				}
			}
		}
	}

	void reconcileScene(int scene, const uint64_t* newConns) {
		if (scene == currentScene) {
			captureScene(scene);
			applyConnectionDiff(sceneConnections[scene], newConns);
		}
		memcpy(sceneConnections[scene], newConns, MATRIX_COUNT * sizeof(uint64_t));
	}

	void switchScene(int newScene) {
		if (newScene == currentScene) return;
		captureScene(currentScene);
		applyConnectionDiff(sceneConnections[currentScene], sceneConnections[newScene]);
		currentScene = newScene;
	}

	static std::string portLabel(const PortAssignment& pa) {
		if (!pa.isValid()) return "";
		ModuleWidget* mw = APP->scene->rack->getModule(pa.moduleId);
		if (!mw) return string::f("(missing module, port %d)", pa.portId + 1);
		std::string dir = (pa.type == engine::Port::OUTPUT) ? "Out" : "In";
		return string::f("%s \xc2\xb7 %s %d", mw->model->name.c_str(), dir.c_str(), pa.portId + 1);
	}

	void triggerCell(int id) {
		if (!portAssignments[id].isValid()) return;
		if (pendingCellId < 0) {
			pendingCellId = id;
		} 
		else if (pendingCellId == id) {
			pendingCellId = -1;
		} 
		else {
			int a = pendingCellId, b = id;
			if (!guiQueue.full())
				guiQueue.push([this, a, b]() { toggleConnection(a, b); });
			pendingCellId = -1;
		}
	}

	void requestSceneChange(int i) {
		if (!guiQueue.full()) {
			guiQueue.push([this, i]() { switchScene(i); });
		}
	}

	void requestReset() {
		if (!guiQueue.full()) {
			guiQueue.push([this]() {
				static const uint64_t empty[MATRIX_COUNT] = {};
				captureScene(currentScene);
				applyConnectionDiff(sceneConnections[currentScene], empty);
				memset(sceneConnections, 0, sizeof(sceneConnections));
				for (int i = 0; i < MATRIX_COUNT; i++) portAssignments[i].clear();
				for (int i = 0; i < MATRIX_COUNT; i++) trackingProcessor.clearMap(i);
				currentScene   = 0;
				feedbackPreset = 0;
				std::fill(cellLedState, cellLedState + MATRIX_COUNT, -1);
				std::fill(portHasCable, portHasCable  + MATRIX_COUNT, false);
			});
		}
	}
};


struct MidiBayWidget;

struct MidiBaySceneButton : app::SvgSwitch {
	MidiBayModule* module = nullptr;
	MidiBayWidget* mw = nullptr;
	int sceneId = -1;

	MidiBaySceneButton() {
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

	void createSceneMenu();
};

// Matrix cell button with right-click context menu for port assignment.
struct MidiBayCellButton : app::SvgSwitch {
	MidiBayModule* module = nullptr;
	MidiBayWidget* mw = nullptr;
	int cellId = -1;

	MidiBayCellButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/MatrixButton1.svg")));
		fb->removeChild(shadow);
		delete shadow;
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && module) {
			e.consume(this);
			createCellMenu();
			return;
		}
		SvgSwitch::onButton(e);
	}

	void createCellMenu();
};


struct MidiBayWidget : ThemedModuleWidget<MidiBayModule>, OverlayMessageProvider {
	MidiBayWidget(MidiBayModule* module) : ThemedModuleWidget(module, "MidiBay") {
		setModule(module);

		for (int r = 0; r < MATRIX_SIZE; r++) {
			for (int c = 0; c < MATRIX_SIZE; c++) {
				float x = 24.3f + c * (245.7f - 24.3f) / 7.f;
				float y = 54.5f + r * (277.4f - 54.5f) / 7.f;
				int cellId = r * MATRIX_SIZE + c;
				auto* btn = createParam<MidiBayCellButton>(Vec(x - 13.25f, y - 13.25f), module, MidiBayModule::PARAM_MATRIX + cellId);
				btn->module = module;
				btn->mw = this;
				btn->cellId = cellId;
				addParam(btn);
				addChild(createLightCentered<MatrixButtonLight<RedGreenBlueLight, MidiBayModule>>(Vec(x, y), module, MidiBayModule::LIGHT_MATRIX + cellId * 3));
			}
		}
		for (int i = 0; i < MATRIX_SIZE; i++) {
			float x = 24.3f + i * (245.7f - 24.3f) / 7.f;
			auto* sb = createParam<MidiBaySceneButton>(Vec(x - 13.25f, 320.6f - 13.25f), module, MidiBayModule::PARAM_SCENE + i);
			sb->module = module;
			sb->mw = this;
			sb->sceneId = i;
			addParam(sb);
			addChild(createLightCentered<MatrixButtonLight<WhiteLight, MidiBayModule>>(Vec(x, 320.6f), module, MidiBayModule::LIGHT_SCENE + i));
		}

		OverlayMessageWidget::registerProvider(this);
	}

	~MidiBayWidget() {
		OverlayMessageWidget::unregisterProvider(this);
	}

	int nextOverlayMessageId() override {
		if (module->overlayMessageId == 0) {
			module->overlayMessageId = -1;
			return 0;
		}
		return -1;
	}

	void getOverlayMessage(int id, Message& m) override {
		if (id != 0) return;
		m = module->overlayMessage;
	}

	static uint64_t sceneClipboard[MATRIX_COUNT];
	static bool sceneClipboardValid;

	void onDeselect(const event::Deselect& e) override {
		ThemedModuleWidget<MidiBayModule>::onDeselect(e);
		if (module) module->portSelectProcessor.processDeselect();
	}

	void step() override {
		ThemedModuleWidget<MidiBayModule>::step();
		if (!module) return;

		// Execute actions queued from the engine thread
		while (module->guiQueue.size() > 0) {
			module->guiQueue.shift()();
		}

		// Update cable presence for each assigned cell (read by process() for light colors)
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
				if (pw->portId == pa.portId) {
					module->portHasCable[i] = !APP->scene->rack->getCablesOnPort(pw).empty();
					break;
				}
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		MidiBayModule* module = this->module;
		if (!module) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("MIDI Input", "", [=](Menu* menu) { appendMidiMenu(menu, &module->trackingProcessor.getInput()); }));
		menu->addChild(createSubmenuItem("MIDI Output", "", [=](Menu* menu) { appendMidiMenu(menu, &module->midiOutput); }));
		menu->addChild(createSubmenuItem("MIDI Feedback", "", [=](Menu* menu) {
			auto& presets = getPresets();
			for (int i = 0; i < (int)presets.size(); i++) {
				int preset = i;
				std::string name = presets[i].name;
				menu->addChild(createCheckMenuItem(name, "",
					[=]() { return module->feedbackPreset == preset; },
					[=]() {
						module->feedbackPreset = preset;
						std::fill(module->cellLedState, module->cellLedState + MATRIX_COUNT, -1);
					}
				));
			}
			menu->addChild(new MenuSeparator);
			bool hasLayout = module->feedbackPreset > 0 &&
			                 module->feedbackPreset < (int)presets.size() &&
			                 presets[module->feedbackPreset].hasLayout();
			menu->addChild(createMenuItem("Apply note layout as MIDI input mappings", "",
				[=]() { module->applyPresetLayout(); },
				!hasLayout
			));
		}));
		menu->addChild(createCheckMenuItem("Sequential MIDI learn", "",
			[=]() { return module->midiLearnMode; },
			[=]() {
				if (module->midiLearnMode) module->disableLearn();
				else module->startGlobalLearn();
			}
		));
	}
};


uint64_t MidiBayWidget::sceneClipboard[MATRIX_COUNT] = {};
bool MidiBayWidget::sceneClipboardValid = false;


void MidiBaySceneButton::createSceneMenu() {
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(string::f("Scene %d", sceneId + 1)));
	menu->addChild(new MenuSeparator);

	menu->addChild(createMenuItem("Clear", "", [=]() {
		uint64_t empty[MATRIX_COUNT] = {};
		module->reconcileScene(sceneId, empty);
	}));
	menu->addChild(createMenuItem("Copy", "", [=]() {
		if (sceneId == module->currentScene) module->captureScene(sceneId);
		memcpy(MidiBayWidget::sceneClipboard, module->sceneConnections[sceneId], MATRIX_COUNT * sizeof(uint64_t));
		MidiBayWidget::sceneClipboardValid = true;
	}));
	menu->addChild(createMenuItem("Paste", "", [=]() {
		if (!MidiBayWidget::sceneClipboardValid) return;
		module->reconcileScene(sceneId, MidiBayWidget::sceneClipboard);
	}, !MidiBayWidget::sceneClipboardValid));

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


void MidiBayCellButton::createCellMenu() {
	auto& pa = module->portAssignments[cellId];
	std::string label = MidiBayModule::portLabel(pa);

	auto& midiMap = module->trackingProcessor.getMap(cellId);
	std::string midiLabel;
	if (midiMap.type == MidiTrackingType::CC) {
		midiLabel = string::f("CC %d", midiMap.param);
	} else if (midiMap.type == MidiTrackingType::NOTE) {
		midiLabel = string::f("Note %d", midiMap.param);
	}

	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(label.empty() ? string::f("Cell %d (unassigned)", cellId + 1) : label));
	menu->addChild(new MenuSeparator);

	menu->addChild(createMenuItem("Learn port", "", [=]() {
		module->enablePortLearn(cellId, mw);
	}));

	if (pa.isValid()) {
		int cid = cellId;
		MidiBayModule* mod = module;
		menu->addChild(createMenuItem("Clear port", "", [=]() {
			mod->portAssignments[cid].clear();
		}));
	}

	menu->addChild(new MenuSeparator);

	std::string learnMidiLabel = midiLabel.empty() ? "Learn MIDI" : string::f("Learn MIDI (%s)", midiLabel.c_str());
	int cid = cellId;
	MidiBayModule* mod = module;
	menu->addChild(createCheckMenuItem(learnMidiLabel, "",
		[=]() { return mod->learningId == cid; },
		[=]() {
			if (mod->learningId == cid) mod->disableLearn();
			else mod->enableLearn(cid);
		}
	));

	if (!midiLabel.empty()) {
		menu->addChild(createMenuItem("Clear MIDI", "", [=]() {
			mod->trackingProcessor.clearMap(cid);
		}));
	}
}


} // namespace MidiBay
} // namespace StoermelderPackOne

Model* modelMidiBay = createModel<StoermelderPackOne::MidiBay::MidiBayModule, StoermelderPackOne::MidiBay::MidiBayWidget>("MidiBay");
