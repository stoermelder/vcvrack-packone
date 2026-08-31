#include "../../plugin.hpp"
#include "../../vcv/api.hpp"
#include "../../components/MatrixButton.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../ui/InfoWindow.hpp"
#include "../../ui/ModuleSelectProcessor.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../../ui/CableOpacityState.hpp"
#include "../../utils/GuiTaskProcessor.hpp"
#include "../midi/MidiTrackingProcessor.hpp"
#include "SpliceKit.controllers.hpp"
#include <osdialog.h>
#include <array>

namespace StoermelderPackOne {
namespace SpliceKit {

// Each LED uses one SCHEME channel; mixing saturates green (SCHEME_BLUE has G=178/255) → teal/white.
static const float LED_BRIGHT = 1.f;
static const float LED_DIM = 0.65f;  		// assigned port, no cable attached
static const float LED_SCENE_DIM = 0.3f;    // inactive scene that has stored connections

static const NVGcolor LED_OFF        = nvgRGBf(0.f,        0.f,        0.f       );
static const NVGcolor LED_PENDING    = nvgRGBf(LED_BRIGHT, LED_BRIGHT, LED_BRIGHT);
static const NVGcolor LED_PORT_LEARN = nvgRGBf(0.7f,       0.7f,       0.7f      );
static const NVGcolor LED_MIDI_LEARN = nvgRGBf(0.7f,       0.7f,       1.f       );

// Four assignable color sets (Rack scheme palette): 0=red (OUTPUT), 1=blue (INPUT), 2=orange, 3=green.
static const int COLOR_SET_COUNT = 4;
struct ColorSet { NVGcolor color; const char* name; };
static const ColorSet COLOR_SETS[COLOR_SET_COUNT] = {
	{ SCHEME_RED,               "Red"    },
	{ nvgRGB(0x10, 0x60, 0xff), "Blue"   },
	{ SCHEME_ORANGE,            "Orange" },
	{ SCHEME_GREEN,             "Green"  }
};

// Per-color-set LED state lookups, named explicitly (not enum-stride arithmetic) so enum reordering is safe.
static const int LED_STATE_COLOR_DIM_BY_SET[COLOR_SET_COUNT] = {
	LED_STATE_COLOR0_DIM, LED_STATE_COLOR1_DIM, LED_STATE_COLOR2_DIM, LED_STATE_COLOR3_DIM
};
static const int LED_STATE_COLOR_BY_SET[COLOR_SET_COUNT] = {
	LED_STATE_COLOR0, LED_STATE_COLOR1, LED_STATE_COLOR2, LED_STATE_COLOR3
};
static const int LED_STATE_CONNECTED_BY_SET[COLOR_SET_COUNT] = {
	LED_STATE_CONNECTED0, LED_STATE_CONNECTED1, LED_STATE_CONNECTED2, LED_STATE_CONNECTED3
};

static const int TOTAL_MAPS = MATRIX_COUNT + SCENE_COUNT;  // 64 cells + 8 scenes

// One scene's connection bitmask (one entry per cell). std::array keeps it assignable/copyable for a std::vector.
using SceneConns = std::array<uint64_t, MATRIX_COUNT>;


struct PortAssignment {
	int64_t moduleId = -1;
	engine::Port::Type type = engine::Port::INPUT;
	int portId = -1;

	bool isValid() const { return moduleId >= 0 && portId >= 0; }
	void clear() { moduleId = -1; portId = -1; }
};

struct CellVisual {
	NVGcolor color;
	int stateId;
};

struct SceneVisual {
	float brightness;
	int stateId;
};

// Resolves which of two assignments is the output and which the input.
// Returns {nullptr, nullptr} when both share a direction or either is invalid.
std::pair<const PortAssignment*, const PortAssignment*>
resolveDirection(const PortAssignment& a, const PortAssignment& b) {
	if (!a.isValid() || !b.isValid()) return { nullptr, nullptr };
	if (a.type == engine::Port::OUTPUT && b.type == engine::Port::INPUT) return { &a, &b };
	if (a.type == engine::Port::INPUT && b.type == engine::Port::OUTPUT) return { &b, &a };
	return { nullptr, nullptr };
}

// Pure — diffs two scene topologies: reports cell pairs to remove (in `from`, absent in `to`) or add.
// Excludes unassigned/invalid-direction pairs (helpers re-check). Split from switchTo() for testability.
static void topologyDiff(const SceneConns& from, const SceneConns& to,
		const PortAssignment* ports,
		std::vector<std::pair<int, int>>& toRemove,
		std::vector<std::pair<int, int>>& toAdd) {
	toRemove.clear();
	toAdd.clear();
	for (int i = 0; i < MATRIX_COUNT; i++) {
		for (int j = i + 1; j < MATRIX_COUNT; j++) {
			bool was = (from[i] >> j) & 1;
			bool will = (to[i] >> j) & 1;
			if (was == will) continue;
			if (!ports[i].isValid() || !ports[j].isValid()) continue;
			if (!resolveDirection(ports[i], ports[j]).first) continue;
			if (was) toRemove.push_back({i, j});
			else toAdd.push_back({i, j});
		}
	}
}

// Formats a MIDI mapping as "CC N" / "Note N", or "" when unmapped. Callers add context.
std::string midiMapLabel(const MidiTrackingProcessor<TOTAL_MAPS>::RevMap& map) {
	if (map.type == MidiTrackingType::CC) return string::f("CC %d", map.param);
	if (map.type == MidiTrackingType::NOTE) return string::f("Note %d", map.param);
	return "";
}


// GUI thread only — every method touches APP->scene->rack or state that must not be read concurrently.
// Reaching this from the engine thread is a bug; go through GuiTaskProcessor.
struct SceneStore {
	thread::ThreadVerifier& verifier;

	std::vector<SceneConns> connections;
	int current = 0;

	// Non-owning view of the module's portAssignments (set once). The module owns the array;
	// the store only reads it to resolve cell ids into cable endpoints.
	const PortAssignment* ports = nullptr;

	// Fired by switchTo() after `current` changes, so the module can react (e.g. notify scene-link followers).
	std::function<void()> onSwitch;

	explicit SceneStore(thread::ThreadVerifier& verifier) : verifier(verifier) {}

	bool isConnected(int scene, int a, int b) const {
		return (connections[scene][a] >> b) & 1;
	}

	// Bitmask of cells connected to `cell` in `scene` — bit j set means cell is connected to j.
	uint64_t connectionMask(int scene, int cell) const {
		return connections[scene][cell];
	}

	// True if `scene` has any stored connection at all, in any cell.
	bool hasConnections(int scene) const {
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (connections[scene][i]) return true;
		}
		return false;
	}

	// The full stored connection topology for `scene`.
	const SceneConns& sceneConns(int scene) const {
		return connections[scene];
	}

	void setConnection(int scene, int a, int b, bool value) {
		assert(verifier.isUiOrWorker());
		if (value) {
			connections[scene][a] |= (1ULL << b);
			connections[scene][b] |= (1ULL << a);
		}
		else {
			connections[scene][a] &= ~(1ULL << b);
			connections[scene][b] &= ~(1ULL << a);
		}
	}

	// GUI thread — removes the cable between two cells; no-op if same direction or invalid.
	// Private: callers must go through disconnectLive() so the bitmask is cleared too.
	void removeCableBetween(int cellIdA, int cellIdB) {
		assert(verifier.isUiOrWorker());
		auto dir = resolveDirection(ports[cellIdA], ports[cellIdB]);
		const PortAssignment* outPd = dir.first;
		const PortAssignment* inPd = dir.second;
		if (!outPd) return;
		vcv::removeCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId, false);
	}

	// GUI thread — mirror of removeCableBetween(): same no-op rules, private (use connectLive()).
	// Skips when a cable already exists (Rack allows duplicates between the same two ports).
	void addCableBetween(int cellIdA, int cellIdB) {
		assert(verifier.isUiOrWorker());
		auto dir = resolveDirection(ports[cellIdA], ports[cellIdB]);
		const PortAssignment* outPd = dir.first;
		const PortAssignment* inPd = dir.second;
		if (!outPd) return;
		if (vcv::hasCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId)) return;
		vcv::addCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId, false);
	}

	// GUI thread — make/break a connection in the CURRENT scene, updating bitmask and cable together.
	// They must never diverge (stale bit recreates a cable; stale cable survives a switch), so these
	// are the only public way to change a live connection.
	void connectLive(int cellIdA, int cellIdB) {
		assert(verifier.isUiOrWorker());
		setConnection(current, cellIdA, cellIdB, true);
		addCableBetween(cellIdA, cellIdB);
	}

	void disconnectLive(int cellIdA, int cellIdB) {
		assert(verifier.isUiOrWorker());
		setConnection(current, cellIdA, cellIdB, false);
		removeCableBetween(cellIdA, cellIdB);
		clearAliasBits(cellIdA, cellIdB);
	}

	// True if the cable between cellIdA/cellIdB already exists in the patch. Used by toggleConnection()
	// instead of the bitmask bit, so cells aliased to the same port pair agree with the patch, not just each other.
	bool cableIsLive(int cellIdA, int cellIdB) const {
		auto dir = resolveDirection(ports[cellIdA], ports[cellIdB]);
		const PortAssignment* outPd = dir.first;
		const PortAssignment* inPd = dir.second;
		if (!outPd) return false;
		return vcv::hasCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId);
	}

	// After removing the cable, clear the current scene's bit for every OTHER pair resolving to the same
	// two ports (aliasing — two cells can share a port). Without this a surviving alias bit would make
	// switchTo() recreate the cable the user just deleted.
	void clearAliasBits(int cellIdA, int cellIdB) {
		assert(verifier.isUiOrWorker());
		auto dir = resolveDirection(ports[cellIdA], ports[cellIdB]);
		const PortAssignment* outPd = dir.first;
		const PortAssignment* inPd = dir.second;
		if (!outPd) return;

		// Stale aliases use a cell whose port matches outPd or inPd — gather those in one pass
		// instead of scanning all O(MATRIX_COUNT^2) pairs.
		auto samePort = [](const PortAssignment& p, const PortAssignment* ref) {
			return p.moduleId == ref->moduleId && p.portId == ref->portId;
		};
		int outAliases[MATRIX_COUNT], inAliases[MATRIX_COUNT];
		int outCount = 0, inCount = 0;
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (samePort(ports[i], outPd)) outAliases[outCount++] = i;
			else if (samePort(ports[i], inPd)) inAliases[inCount++] = i;
		}
		for (int oi = 0; oi < outCount; oi++) {
			for (int ii = 0; ii < inCount; ii++) {
				int i = outAliases[oi], j = inAliases[ii];
				if ((i == cellIdA && j == cellIdB) || (i == cellIdB && j == cellIdA)) continue;
				if (isConnected(current, i, j)) setConnection(current, i, j, false);
			}
		}
	}

	// GUI thread — rewrites connections[scene] to match the actual cables present for the assigned ports.
	// Used before switching scenes so manual cable changes are not lost.
	void capture(int scene) {
		assert(verifier.isUiOrWorker());
		connections[scene].fill(0);
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const PortAssignment& a = ports[i];
			if (!a.isValid() || a.type != engine::Port::OUTPUT) continue;
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if (j == i) continue;
				const PortAssignment& b = ports[j];
				if (!b.isValid() || b.type != engine::Port::INPUT) continue;
				if (vcv::hasCable(a.moduleId, a.portId, b.moduleId, b.portId)) {
					setConnection(scene, i, j, true);
				}
			}
		}
	}

	// GUI thread — reconciles the patch AND connections[current] to match newConns, starting from the
	// current state. Each differing pair's cable and bitmask are updated together; unchanged pairs are
	// skipped. Unlike switchTo(), always writes into `current` (used by reconcile/resetModuleState).
	void applyToCurrent(const SceneConns& newConns) {
		assert(verifier.isUiOrWorker());
		for (int i = 0; i < MATRIX_COUNT; i++) {
			for (int j = i + 1; j < MATRIX_COUNT; j++) {
				bool was = (connections[current][i] >> j) & 1;
				bool will = (newConns[i] >> j) & 1;
				if (was == will) continue;
				if (!ports[i].isValid() || !ports[j].isValid()) {
					// Unassigned cells have no cable to reconcile, but the bitmask still
					// has to track newConns or the difference would survive the call.
					setConnection(current, i, j, will);
					continue;
				}
				if (was) disconnectLive(i, j);
				else connectLive(i, j);
			}
		}
	}

	// GUI thread — applies newConns to an arbitrary scene, persisting it into connections[scene].
	// If it is the active scene, the patch cables are updated live (capture, then diff).
	void reconcile(int scene, const SceneConns& newConns) {
		assert(verifier.isUiOrWorker());
		if (scene == current) {
			capture(scene);
			applyToCurrent(newConns);
		}
		connections[scene] = newConns;
	}

	// GUI thread — captures the outgoing scene's cables, then realises the incoming scene's stored
	// topology in the patch. No-op if already current; fires onSwitch() last.
	// The diff runs while `current` still names the outgoing scene (its captured state is the "from"
	// baseline) and uses the bitmask-free helpers, so neither scene's stored bitmask is rewritten here
	// (outgoing keeps its captured state; incoming already holds the desired topology). Rules out applyToCurrent().
	void switchTo(int newScene) {
		assert(verifier.isUiOrWorker());
		if (newScene == current) return;
		capture(current);
		std::vector<std::pair<int, int>> toRemove, toAdd;
		topologyDiff(connections[current], connections[newScene], ports, toRemove, toAdd);
		for (const auto& p : toRemove) removeCableBetween(p.first, p.second);
		for (const auto& p : toAdd) addCableBetween(p.first, p.second);
		current = newScene;
		if (onSwitch) onSwitch();
	}

	// GUI thread — copies src's topology to dst (live via reconcile if dst is active).
	void copy(int src, int dst) {
		assert(verifier.isUiOrWorker());
		if (src == dst) return;
		if (src == current) capture(src);
		reconcile(dst, connections[src]);
	}

	// GUI thread — clears cellId's bit from every other cell's mask and its own, in every scene.
	// Does not touch current-scene cables; callers needing that removed too use removeCellConnections() first.
	void clearCell(int cellId) {
		assert(verifier.isUiOrWorker());
		for (int s = 0; s < SCENE_COUNT; s++) {
			uint64_t mask = connections[s][cellId];
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if ((mask >> j) & 1) connections[s][j] &= ~(1ULL << cellId);
			}
			connections[s][cellId] = 0;
		}
	}

	// GUI thread — removes cellId's current-scene cables, updating the patch and bitmask.
	// Used before a rebind/clear/move discards the old port, while it is still resolvable.
	void removeCellConnections(int cellId) {
		assert(verifier.isUiOrWorker());
		uint64_t mask = connections[current][cellId];
		for (int j = 0; j < MATRIX_COUNT; j++) {
			if (!((mask >> j) & 1)) continue;
			disconnectLive(cellId, j);
		}
	}

	// GUI thread — moves fromId's connections (every scene) onto toId, first tearing out toId's existing
	// connections. Mirrors moveCell()'s port/label/color handling; callers must removeCellConnections(toId)
	// first (needs the still-valid old port).
	void moveCellBits(int fromId, int toId) {
		assert(verifier.isUiOrWorker());
		for (int s = 0; s < SCENE_COUNT; s++) {
			uint64_t oldToMask = connections[s][toId];
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if ((oldToMask >> j) & 1) {
					connections[s][j] &= ~(1ULL << toId);
				}
			}
			connections[s][toId] = 0;

			uint64_t fromMask = connections[s][fromId];
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if (!((fromMask >> j) & 1)) continue;
				connections[s][j] &= ~(1ULL << fromId);
				if (j != toId) {
					connections[s][j] |= (1ULL << toId);
				}
			}
			// fromId's connections become toId's, minus any self-connection bit.
			connections[s][toId] = fromMask & ~(1ULL << toId);
			connections[s][fromId] = 0;
		}
	}
};


struct SpliceKitOutput : midi::Output {
	// Hook to the owner's invalidateLedStates(), set once. SpliceKitOutput has no back-pointer,
	// so a callback avoids duplicating the cache-invalidation logic.
	std::function<void()> onDeviceChanged;

	// lastSentMsg/prevSentMsg/sentCount are test-observation fields, only read by tests.
	midi::Message lastSentMsg;
	// The message before lastSentMsg — lets tests assert send ORDER (e.g. setState()'s off-before-on).
	midi::Message prevSentMsg;
	int sentCount = 0;

	SpliceKitOutput() {
		channel = -1;
	}

	// Shadows, not overrides (midi::Output::sendMessage is non-virtual) — safe only because every
	// call site holds a SpliceKitOutput, never a base-class ref/pointer.
	void sendMessage(const midi::Message& msg) {
		prevSentMsg = lastSentMsg;
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

	// Forces a full LED refresh after a device change, so the next process() tick re-sends everything.
	void setDeviceId(int deviceId) override {
		midi::Output::setDeviceId(deviceId);
		if (onDeviceChanged) onDeviceChanged();
	}
};


// Translates a cell/scene LED state id into a MIDI message per the active preset. FROM_SLOT note/CC
// resolution is injected as resolveFromSlot (MidiTrackingProcessor is a template).
// All sends run on the engine thread (midi::Output is unsynchronised, process() sends every tick).
// GUI-thread callers use queueFeedbackOff(), not sendFeedbackOff().
struct FeedbackSender {
	thread::ThreadVerifier& verifier;

	SpliceKitOutput midiOutput;

	/** [Stored to JSON] — raw JSON of the currently active preset (built-in or user-loaded); empty means feedback is off. */
	std::string activePresetJson;
	/** Parsed from activePresetJson; kept in sync whenever activePresetJson changes. */
	MidiOutPreset activePreset;

	// -1 forces a send on the first light-divider tick after load.
	int cellLedState[MATRIX_COUNT];
	std::vector<int> sceneLedState;

	// Any thread — resolves a FROM_SLOT spec's note/CC via the module's MidiTrackingProcessor; returns
	// false if unmapped. Set once. The getMap() read races with the GUI thread's setMap() (accepted, like
	// learningId): worst case one wrong LED until the next invalidate.
	std::function<bool(int cellId, MidiTrackingType& slotType, int& noteNum)> resolveFromSlot;

	// GUI produces, engine consumes. See queueFeedbackOff()/drainPendingOffs().
	dsp::RingBuffer<midi::Message, 16> pendingOffs;

	explicit FeedbackSender(thread::ThreadVerifier& verifier) : verifier(verifier) {
		sceneLedState.resize(SCENE_COUNT);
		invalidateLedStates();
		midiOutput.onDeviceChanged = [this]() { invalidateLedStates(); };
	}

	// Returns a pointer to the currently active preset, or nullptr when feedback is off.
	const MidiOutPreset* getActivePreset() const {
		if (activePresetJson.empty()) return nullptr;
		return &activePreset;
	}

	// True when a preset is active (feedback is on).
	bool isActive() const {
		return !activePresetJson.empty();
	}

	// True when the given raw preset JSON is the one currently active.
	bool isActivePreset(const std::string& json) const {
		return activePresetJson == json;
	}

	// Display name of the active preset, or the given fallback (meaningless when
	// !isActive()). No default value — callers want different fallbacks.
	std::string activePresetName(const std::string& fallback) const {
		return activePreset.name.empty() ? fallback : activePreset.name;
	}

	// Raw JSON of the active preset, for exporting via "Save preset to file...".
	const std::string& activePresetJsonText() const {
		return activePresetJson;
	}

	// Sets the active preset from raw JSON text (built-in or user-loaded); empty string turns feedback off.
	void setActivePresetJson(const std::string& json) {
		assert(verifier.isUiOrWorker());
		activePresetJson = json;
		activePreset = MidiOutPreset();
		if (!json.empty()) {
			json_error_t err;
			json_t* root = json_loads(json.c_str(), 0, &err);
			if (root) { activePreset.fromJson(root); json_decref(root); }
			else activePresetJson.clear();
		}
	}

	// Sets the active preset from a struct, serializing it so activePresetJson stays genuine/exportable.
	void setActivePreset(const MidiOutPreset& preset) {
		assert(verifier.isUiOrWorker());
		activePreset = preset;
		json_t* root = preset.toJson();
		char* text = json_dumps(root, JSON_INDENT(2));
		json_decref(root);
		activePresetJson = text;
		std::free(text);
	}

	// Any thread — forces a full MIDI LED refresh on the next process() tick (resets cached states to -1).
	void invalidateLedStates() {
		std::fill(cellLedState, cellLedState + MATRIX_COUNT, -1);
		std::fill(sceneLedState.begin(), sceneLedState.end(), -1);
	}

	// Resolves the note/CC number and slot type for a spec; returns false if the note mode has no resolvable number.
	bool resolveNote(const MidiOutSpec& spec, int cellId, int& noteNum, MidiTrackingType& slotType) {
		switch (spec.noteMode) {
			case MIDI_OUT_FROM_SLOT:
				return resolveFromSlot && resolveFromSlot(cellId, slotType, noteNum);
			case MIDI_OUT_FIXED: {
				noteNum = spec.note;
				slotType = MidiTrackingType::NONE;
				return true;
			}
			default: return false;
		}
	}

	// Any thread — builds (without sending) the note-off for a cell's previous LED state. Returns false when
	// no off is needed (invalid state, no preset, non-NOTE_ON spec). Split out so GUI callers resolve the note
	// against the mapping still in place and defer only the send (resolving later would use the new mapping).
	bool buildFeedbackOff(int cellId, int oldStateId, midi::Message& out) {
		if (oldStateId < 0) return false;
		const MidiOutPreset* preset = getActivePreset();
		if (!preset) return false;
		const MidiOutSpec& spec = preset->specs[oldStateId];
		if (spec.type == MIDI_OUT_NONE) return false;
		if (spec.type != MIDI_OUT_NOTE_ON && spec.type != MIDI_OUT_FROM_SLOT_TYPE) return false;
		MidiTrackingType slotType = MidiTrackingType::NONE;
		int noteNum;
		if (!resolveNote(spec, cellId, noteNum, slotType)) return false;
		if (spec.type == MIDI_OUT_FROM_SLOT_TYPE && slotType != MidiTrackingType::NOTE) return false;
		out.bytes[0] = 0x80 | (uint8_t)(spec.channel & 0x0F);
		out.bytes[1] = (uint8_t)(noteNum & 0x7F);
		out.bytes[2] = 0x00;
		return true;
	}

	// Engine thread — sends the note-off for the previous LED state before a transition (clears the
	// controller LED lit by a NOTE_ON spec). No-op for CC specs, off states, or no active preset.
	void sendFeedbackOff(int cellId, int oldStateId) {
		assert(verifier.isEngine());
		midi::Message msg;
		if (!buildFeedbackOff(cellId, oldStateId, msg)) return;
		msg.frame = APP->engine->getFrame() + 1;
		midiOutput.sendMessage(msg);
	}

	// GUI thread — deferred counterpart to sendFeedbackOff(): resolves the note-off against the current
	// mapping and leaves the send to the engine thread's next tick. Drops when full; every caller invalidates
	// the LED state, so the next tick re-sends the correct on-message — only the stale off is lost.
	void queueFeedbackOff(int cellId, int oldStateId) {
		assert(verifier.isUiOrWorker());
		midi::Message msg;
		if (!buildFeedbackOff(cellId, oldStateId, msg)) return;
		if (pendingOffs.full()) return;
		pendingOffs.push(msg);
	}

	// Engine thread — sends every note-off queued by the GUI thread since the last tick, before the light
	// loop's setState() calls, so a queued off always precedes the on-message for the state that replaced it.
	void drainPendingOffs() {
		assert(verifier.isEngine());
		while (!pendingOffs.empty()) {
			midi::Message msg = pendingOffs.shift();
			msg.frame = APP->engine->getFrame() + 1;
			midiOutput.sendMessage(msg);
		}
	}

	// Engine thread — sends one MIDI message to update the controller LED for mapId to stateId (only on change).
	// mapId 0–63: matrix cells; 64–71: scene buttons (MATRIX_COUNT + sceneId).
	void sendFeedback(int cellId, int stateId) {
		assert(verifier.isEngine());
		const MidiOutPreset* preset = getActivePreset();
		if (!preset) return;
		const MidiOutSpec& spec = preset->specs[stateId];
		if (spec.type == MIDI_OUT_NONE) return;
		MidiTrackingType slotType = MidiTrackingType::NONE;
		int noteNum;
		if (!resolveNote(spec, cellId, noteNum, slotType)) return;
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

	// Engine thread — transitions mapId to newState, emitting the outgoing note-off first (while still
	// addressable) then the on-message. No-op when unchanged. mapId 0–63: cells; 64–71: scene buttons.
	void setState(int mapId, int newState) {
		assert(verifier.isEngine());
		int* cache = mapId < MATRIX_COUNT ? &cellLedState[mapId] : &sceneLedState[mapId - MATRIX_COUNT];
		if (newState == *cache) return;
		sendFeedbackOff(mapId, *cache);
		*cache = newState;
		sendFeedback(mapId, newState);
	}

	// GUI thread — replaces all MIDI input mappings with the active preset's slot layout (no-op if none).
	// clearMap/setMap are injected callbacks so this struct need not name the MidiTrackingProcessor template.
	void applyPresetLayout(const std::function<void()>& clearMaps,
			const std::function<void(MidiTrackingType, int, uint16_t)>& setMap) {
		assert(verifier.isUiOrWorker());
		const MidiOutPreset* preset = getActivePreset();
		if (!preset || !preset->hasLayout()) return;
		clearMaps();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const auto& s = preset->cells[i];
			if (s.type != MidiTrackingType::NONE) {
				setMap(s.type, i, s.number);
			}
		}
		for (int i = 0; i < SCENE_COUNT; i++) {
			const auto& s = preset->scenes[i];
			if (s.type != MidiTrackingType::NONE) {
				setMap(s.type, MATRIX_COUNT + i, s.number);
			}
		}
		invalidateLedStates();
	}
};


struct SpliceKitModule : Module, MidiTrackingProcessorHandler, ModuleChangeListener {
	// Cross-instance pending state (initiator + its pending cell), shared across all SpliceKit instances
	// in the same Rack context. Cleared by the responder or when the initiator cancels/completes.
	struct CrossPendingState {
		SpliceKitModule* initiator = nullptr;
		int cellId = -1;
		PortAssignment port;

		// Far end of every cable on `port`, keyed (moduleId, portId). Published once by the initiator and
		// read by peers (deriving per peer would be N widget-tree walks a frame for one fact). A snapshot:
		// mid-gesture cable changes are not reflected until the gesture ends.
		std::set<std::pair<int64_t, int>> partners;

		bool isValid() const {
			return initiator != nullptr && cellId >= 0;
		}
		void clear() {
			initiator = nullptr;
			cellId = -1;
			port.clear();
			partners.clear();
		}
	};
	
	// Entries are never erased when a Context is destroyed — a small, bounded leak, not worth fixing.
	// Function-local static (not a class static): the test build both links the plugin binary and
	// #includes this .cpp, so a class static would exist twice (plugin.dylib + test TU) and a write through
	// one would be invisible to a read through the other — leaving a stale `initiator` pointing at a
	// destroyed module (caught as a heap-use-after-free in clearPendingLocal()). Same fix/rationale as
	// getInstances() below.
	static std::map<Context*, CrossPendingState>& crossPending() {
		static std::map<Context*, CrossPendingState> crossPending;
		return crossPending;
	}

	// All live instances in this Rack context (maintained by ctor/dtor). Cross-instance patching enumerates
	// peers every GUI frame; walking APP->engine->getModuleIds() would allocate + dynamic_cast every module
	// 60x/sec. Same keying and GUI-thread-only rule as crossPending().
	// Function-local static (not a class static): the test build both links the binary and #includes this
	// .cpp, so a class static would exist twice and the ctor would populate a different copy than
	// collectCableEndCandidates() reads.
	static std::map<Context*, std::set<SpliceKitModule*>>& getInstances() {
		static std::map<Context*, std::set<SpliceKitModule*>> instances;
		return instances;
	}

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
			std::string label = midiMapLabel(mm);
			return label.empty() ? "MIDI: (unmapped)" : "MIDI: " + label;
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
			std::string label = midiMapLabel(mm);
			return label.empty() ? "MIDI: (unmapped)" : "MIDI: " + label;
		}
	};

	enum ParamIds {
		ENUMS(PARAM_MATRIX, MATRIX_COUNT),
		ENUMS(PARAM_SCENE, SCENE_COUNT),
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
		ENUMS(LIGHT_SCENE, SCENE_COUNT),
		NUM_LIGHTS
	};

	// Defers GUI-thread work (cables, widget state) requested from the engine thread. SINGLE PRODUCER: only
	// the engine thread may enqueue() — the SPSC ring buffer would corrupt on a concurrent GUI-thread push,
	// so GUI-thread code must call the target directly.
	// Declared before verifier/feedback/sceneStore: verifier's ctor binds a lambda over taskProcessorUi and
	// feedback/sceneStore need a constructed ThreadVerifier&; members init in declaration order, so this is load-bearing.
	GuiTaskProcessor<16> taskProcessorUi;

	// This instance's own thread-assertion state — checks against taskProcessorUi's worker thread, not a
	// shared/global registry, so it can never be confused with another instance's worker.
	thread::ThreadVerifier verifier;

	/** [Stored to JSON] */
	int panelTheme = 0;

	bool midiLearnMode = false;
	int learningId = -1;
	bool portLearnMode = false;
	int lastClickedCell = 0;

	/** [Stored to JSON] */
	StoermelderPackOne::MidiTrackingProcessor<TOTAL_MAPS> trackingProcessor;

	/** [Stored to JSON] */
	FeedbackSender feedback;

	/** [Stored to JSON] */
	PortAssignment portAssignments[MATRIX_COUNT];

	// Scene topology (connections + current scene index). GUI thread only. [Stored to JSON] via sceneStore.
	SceneStore sceneStore;

	dsp::BooleanTrigger buttonTriggers[MATRIX_COUNT];
	dsp::BooleanTrigger sceneTriggers[SCENE_COUNT];

	ClockDividerEx processDivider;
	ClockDividerEx lightDivider;

	// Written by GUI thread (step), read by DSP thread (process) — accepted race for LEDs.
	bool portHasCable[MATRIX_COUNT] = {};

	// This cell shares a cross-instance cable with a peer's armed port (sceneStore can't answer this, since
	// such a cable is never stored in a scene). Same accepted race as portHasCable[]: written by the GUI
	// thread, read by the light loop. The engine thread must not read crossPending directly (operator[] can insert).
	bool peerConnected[MATRIX_COUNT] = {};

	float blinkPhase = 0.f;
	float slowBlinkPhase = 0.f;

	enum ButtonMode {
		BUTTON_TOGGLE,
		BUTTON_MOMENTARY
	};
	/** [Stored to JSON] */
	ButtonMode buttonMode = BUTTON_TOGGLE;

	// -1 = no pending; >=0 = first button pressed, awaiting second press.
	int pendingCellId = -1;
	bool pendingCellIsPhysical = false; // true = set by physical button, false = set by MIDI

	// Tracks the last MIDI-activated scene still awaiting a note-off/CC=0. A second activation while set
	// (no release between) is interpreted as a copy from pendingMidiSceneId → new sceneId.
	int pendingMidiSceneId = -1;

	int portLearningId = -1;
	StoermelderPackOne::PortSelectProcessor portSelectProcessor;

	/** [Stored to JSON — only non-empty entries are written] */
	std::string cellLabels[MATRIX_COUNT];

	/** [Stored to JSON — only non-default (-1) entries are written]
	 *  -1 = auto (OUTPUT→set 0/red, INPUT→set 1/blue); 0–3 = explicit color set override */
	int8_t cellColorSet[MATRIX_COUNT];

	/** [Stored to JSON] */
	bool overlayEnabled = true;
	/** [Stored to JSON] */
	bool crossInstanceEnabled = true;
	/** [Stored to JSON] — opt-in MIDI double-activation scene-copy gesture (see processMapUpdate). Off by
	 *  default: assumes the controller sends a release between selections; on a press-only controller every
	 *  second selection would otherwise become a destructive scene overwrite. */
	bool midiSceneCopyEnabled = false;

	int overlayMessageId = -1;
	OverlayMessageProvider::Message overlayMessage;

	SceneConns sceneClipboard{};
	bool sceneClipboardValid = false;

	// -1 = no scene link master; otherwise the engine module ID of another SpliceKit instance whose
	// currentScene this one follows (see process()'s notifyModuleListeners consumption).
	/** [Stored to JSON] */
	int64_t sceneLinkMasterId = -1;


	SpliceKitModule() :
			ModuleChangeListener{false},
			verifier(thread::makeVerifier([this]() {
				return taskProcessorUi.isWorkerThread();
			})),
			feedback(verifier), sceneStore(verifier) {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		memset(cellColorSet, -1, sizeof(cellColorSet));
		sceneStore.connections.resize(SCENE_COUNT);
		sceneStore.ports = portAssignments;
		sceneStore.onSwitch = []() {
			notifyModuleListeners("SpliceKit-SceneLink");
		};
		// Momentary button representations, not meaningful knob/switch values — excluded from Rack's default
		// param randomization. onRandomize() generates a random cable topology instead; randomizing these
		// directly would just spuriously trigger button presses on the next process() tick.
		for (int i = 0; i < MATRIX_COUNT; i++) {
			configParam<SpliceKitCellQuantity>(PARAM_MATRIX + i, 0.f, 1.f, 0.f)->randomizeEnabled = false;
		}
		for (int i = 0; i < SCENE_COUNT; i++) {
			configParam<SpliceKitSceneQuantity>(PARAM_SCENE + i, 0.f, 1.f, 0.f)->randomizeEnabled = false;
		}

		trackingProcessor.handler = this;
		trackingProcessor.enableCc();
		trackingProcessor.enableNotes();
		feedback.resolveFromSlot = [this](int cellId, MidiTrackingType& slotType, int& noteNum) {
			auto m = trackingProcessor.getMap(cellId);
			if (m.type == MidiTrackingType::NONE) return false;
			noteNum = m.param;
			slotType = m.type;
			return true;
		};
		// With no window there is no SpliceKitWidget::step() to refresh portHasCable[], so the worker takes
		// that over after each drain — otherwise the flag stays frozen and the edge-triggered MIDI feedback
		// in process() never sees a cell change state.
		taskProcessorUi.onWorkerDrained = [this]() {
			refreshPortHasCable();
			refreshPeerConnected();
		};
		processDivider.setDivision(256);
		registerModuleListener("SpliceKit-SceneLink", this);
		getInstances()[APP].insert(this);
	}

	// Runs on plain deletion too (unlike onRemove(), which only fires on engine removal) — this keeps the
	// static ModuleChangeListener registry and instance set free of dangling pointers regardless of lifetime end.
	~SpliceKitModule() {
		unregisterModuleListener("SpliceKit-SceneLink", this);
		auto& reg = getInstances();
		auto it = reg.find(APP);
		if (it != reg.end()) {
			it->second.erase(this);
			if (it->second.empty()) reg.erase(it);
		}
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onRemove() override {
		auto& cp = crossPending()[APP];
		if (cp.initiator == this) cp.clear();
	}

	// GUI thread — called by Rack on user reset (ModuleWidget::resetAction() → Engine::resetModule()),
	// synchronously, so the deferred work can run directly.
	void onReset() override {
		disableLearn();
		disablePortLearn();
		clearPendingGui();
		for (auto& l : cellLabels) l.clear();
		memset(cellColorSet, -1, sizeof(cellColorSet));
		resetModuleState();
	}

	// GUI thread — reached synchronously via Engine::randomizeModule(), so the ModuleWidget/PortWidget reads
	// in randomizePortAssignments() are safe. Reassigns every cell to a random port; for the current scene's
	// cable topology see randomizeCurrentScene() (scene button menu).
	void onRandomize() override {
		randomizePortAssignments();
	}


	// Resets only the local pending-cell selection. Safe from either thread — unlike clearPendingGui()/
	// clearPending()/clearPendingCrossGui(), it never touches crossPending. Callers needing the shared
	// cross-instance entry cleared want clearPendingGui() (GUI) or clearPending() (engine) instead.
	void clearPendingLocal() {
		pendingCellId = -1;
		pendingCellIsPhysical = false;
	}

	// GUI thread only — clears the global cross-instance pending state if this module is the current initiator.
	// crossPending is a static map shared across all instances; mutating it from the engine thread would race.
	void clearPendingCrossGui() {
		assert(verifier.isUiOrWorker());
		auto& cp = crossPending()[APP];
		if (cp.initiator == this) cp.clear();
	}

	// GUI thread only — full pending-selection reset: local state plus the shared cross-instance entry.
	// Both halves are needed (clearPendingLocal() only touches pendingCellId; crossPending is a static map
	// other instances consult in triggerCell()); leaving our entry behind lets another instance complete a
	// gesture against a cell we just cleared.
	void clearPendingGui() {
		assert(verifier.isUiOrWorker());
		clearPendingLocal();
		clearPendingCrossGui();
	}

	// GUI thread only — drops the pending selection if it points at cellId. Callers about to discard cellId's
	// port (assignPort/clearPort) need this: a selection against the old port is stale afterwards, and leaving
	// it strands the cell — resolveCellVisual() tests pendingCellId before `assigned` (cell keeps blinking) and
	// triggerCell() returns at its isValid() guard (pressing cannot cancel).
	// Scoped to one cell (not unconditional): rebinding A must not cancel an unrelated pending B. moveCell()
	// clears unconditionally because it rewrites two cells at once.
	void clearPendingForCell(int cellId) {
		assert(verifier.isUiOrWorker());
		if (pendingCellId != cellId) return;
		clearPendingGui();
	}

	// Engine thread only — same reset as clearPendingGui(), but the cross-instance half is deferred to the GUI
	// thread via taskProcessorUi (crossPending must not be touched from the engine thread). GUI-thread callers
	// must NOT use this: enqueueing from the GUI thread would make taskProcessorUi multi-producer (unsupported
	// by its SPSC queue). Call clearPendingGui() instead, which also cleans up immediately.
	void clearPending() {
		clearPendingLocal();
		taskProcessorUi.enqueue([this]() { clearPendingCrossGui(); });
	}


	// GUI thread — collects every port of every module in the rack (SpliceKit has none) as candidates for randomizePortAssignmentsFrom().
	void randomizePortAssignments() {
		assert(verifier.isUiOrWorker());
		std::vector<PortAssignment> candidates;
		for (ModuleWidget* mw : APP->scene->rack->getModules()) {
			if (!mw->module) continue;
			for (PortWidget* pw : mw->getPorts()) {
				PortAssignment pa;
				pa.moduleId = pw->module->getId();
				pa.portId = pw->portId;
				pa.type = pw->type;
				candidates.push_back(pa);
			}
		}
		randomizePortAssignmentsFrom(candidates);
	}

	// GUI thread — reassigns cells to a shuffled, non-repeating subset of candidates; surplus cells are left
	// cleared, surplus candidates unused. Scene connections are left as-is. Split from randomizePortAssignments()
	// for testability without ModuleWidget/PortWidget scaffolding.
	void randomizePortAssignmentsFrom(const std::vector<PortAssignment>& candidates) {
		assert(verifier.isUiOrWorker());
		// Same cleanup contract as assignPort(): a rebound cell's old cables and connection bitmasks must go
		// with it in every scene, or they orphan a cable / resurrect a stale connection to the wrong port on
		// the next switchScene() (see assignPort() above for the full rationale).
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (portAssignments[i].isValid()) {
				sceneStore.removeCellConnections(i);
				sceneStore.clearCell(i);
			}
			portAssignments[i].clear();
			cellLabels[i].clear();
		}
		clearPendingGui();
		if (candidates.empty()) return;

		std::vector<PortAssignment> shuffled = candidates;
		std::mt19937 rng(random::u32());
		std::shuffle(shuffled.begin(), shuffled.end(), rng);

		size_t count = std::min((size_t)MATRIX_COUNT, shuffled.size());
		for (size_t i = 0; i < count; i++) {
			portAssignments[i] = shuffled[i];
		}
		feedback.invalidateLedStates();
	}

	// GUI thread — replaces the current scene's connections with a random valid topology: outputs and inputs
	// are each shuffled independently, then paired one-to-one in order (respecting the out→in constraint).
	// Surplus ports on the larger side are left unconnected.
	void randomizeCurrentScene() {
		assert(verifier.isUiOrWorker());
		std::vector<int> outputs, inputs;
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (!portAssignments[i].isValid()) continue;
			(portAssignments[i].type == engine::Port::OUTPUT ? outputs : inputs).push_back(i);
		}
		std::mt19937 rng(random::u32());
		std::shuffle(outputs.begin(), outputs.end(), rng);
		std::shuffle(inputs.begin(), inputs.end(), rng);

		SceneConns newConns{};
		size_t pairs = std::min(outputs.size(), inputs.size());
		for (size_t i = 0; i < pairs; i++) {
			int a = outputs[i], b = inputs[i];
			newConns[a] |= (1ULL << b);
			newConns[b] |= (1ULL << a);
		}
		sceneStore.reconcile(sceneStore.current, newConns);
	}

	// GUI thread — replaces the current scene's connections with a random topology where every assigned port
	// gets at least one cable. Like randomizeCurrentScene() but, once the shorter side is exhausted, its ports
	// are reused (wrapping) so the longer side's remaining ports still get a partner — some ports fan out.
	void randomizeCurrentSceneFull() {
		assert(verifier.isUiOrWorker());
		std::vector<int> outputs, inputs;
		for (int i = 0; i < MATRIX_COUNT; i++) {
			if (!portAssignments[i].isValid()) continue;
			(portAssignments[i].type == engine::Port::OUTPUT ? outputs : inputs).push_back(i);
		}
		std::mt19937 rng(random::u32());
		std::shuffle(outputs.begin(), outputs.end(), rng);
		std::shuffle(inputs.begin(), inputs.end(), rng);

		SceneConns newConns{};
		if (!outputs.empty() && !inputs.empty()) {
			size_t n = std::max(outputs.size(), inputs.size());
			for (size_t i = 0; i < n; i++) {
				int a = outputs[i % outputs.size()];
				int b = inputs[i % inputs.size()];
				newConns[a] |= (1ULL << b);
				newConns[b] |= (1ULL << a);
			}
		}
		sceneStore.reconcile(sceneStore.current, newConns);
	}

	void processBypass(const ProcessArgs& args) override {
		assert(verifier.isEngine());
		trackingProcessor.processBypass(args.frame);
		Module::processBypass(args);
	}

	// Engine thread — if this instance follows a scene link master, picks up the master's current scene.
	// Enqueues the actual sceneStore.switchTo() on the GUI thread via taskProcessorUi (cables must not run here).
	void processSceneLinkMaster() {
		assert(verifier.isEngine());
		if (!moduleChangedFlag) return;
		if (sceneLinkMasterId < 0) {
			// No master configured — nothing to follow, so the notification is consumed.
			moduleChangedFlag = false;
			return;
		}
		auto* master = dynamic_cast<SpliceKitModule*>(APP->engine->getModule(sceneLinkMasterId));
		if (!master) {
			// Master was removed from the patch (or never existed) — stop following.
			sceneLinkMasterId = -1;
			moduleChangedFlag = false;
		}
		else if (master->sceneStore.current != sceneStore.current) {
			int targetScene = master->sceneStore.current;
			// Clear the flag only after a successful enqueue.
			if (taskProcessorUi.enqueue([this, targetScene]() { sceneStore.switchTo(targetScene); })) {
				moduleChangedFlag = false;
			}
		}
		else {
			// Master exists and already agrees — nothing to enqueue.
			moduleChangedFlag = false;
		}
	}

	// Engine thread — reads the physical matrix buttons for edges and routes them to learn-cancel, cell trigger, or momentary-release.
	void processCellButtons() {
		assert(verifier.isEngine());
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
	}

	// Engine thread — reads the physical scene buttons for rising edges and routes them to learn-cancel or a scene change.
	void processSceneButtons() {
		assert(verifier.isEngine());
		for (int i = 0; i < SCENE_COUNT; i++) {
			if (sceneTriggers[i].process(params[PARAM_SCENE + i].getValue() > 0.5f)) {
				if (learningId == MATRIX_COUNT + i) {
					disableLearn();
				}
				// Scene buttons are inert while following a scene link master — the active scene is driven
				// entirely by the master (see processSceneLinkMaster()), not by this instance's own buttons.
				else if (sceneLinkMasterId < 0) {
					requestSceneChange(i);
				}
			}
		}
	}

	// Engine thread — resolves and applies every cell/scene LED's color and blink state. Runs on lightDivider
	// (not processDivider) so refresh rate and MIDI feedback resend cadence don't scale with the button/trigger/scene-link logic rate.
	void processLights(float sampleTime) {
		assert(verifier.isEngine());
		// Advances the two LED blink phases by one lightDivider tick.
		blinkPhase += sampleTime * 4.f * lightDivider.division;
		if (blinkPhase >= 1.f) blinkPhase -= 1.f;
		slowBlinkPhase += sampleTime * 2.f * lightDivider.division;
		if (slowBlinkPhase >= 1.f) slowBlinkPhase -= 1.f;

		bool blinkOn = blinkPhase < 0.5f;
		bool slowBlinkOn = slowBlinkPhase < 0.5f;
		for (int i = 0; i < MATRIX_COUNT; i++) {
			CellVisual v = resolveCellVisual(i, blinkOn, slowBlinkOn);
			feedback.setState(i, v.stateId);
			float f = sampleTime * lightDivider.division;
			lights[LIGHT_MATRIX + i * 3 + 0].setBrightnessSmooth(v.color.r, f);
			lights[LIGHT_MATRIX + i * 3 + 1].setBrightnessSmooth(v.color.g, f);
			lights[LIGHT_MATRIX + i * 3 + 2].setBrightnessSmooth(v.color.b, f);
		}
		for (int s = 0; s < SCENE_COUNT; s++) {
			SceneVisual v = resolveSceneVisual(s, blinkOn);
			feedback.setState(MATRIX_COUNT + s, v.stateId);
			lights[LIGHT_SCENE + s].setBrightness(v.brightness);
		}
	}

	void process(const ProcessArgs& args) override {
		assert(verifier.isEngine());
		trackingProcessor.process(args.frame);

		if (processDivider.process()) {
			taskProcessorUi.process();
			processSceneLinkMaster();
			processCellButtons();
			processSceneButtons();
		}
		if (lightDivider.process()) {
			// Before any new on-message in processLights(): flush note-offs the GUI thread resolved against mappings
			// it has since rewritten (moveCell/assignPort/clearPort). Must run immediately before processLights() on
			// the same tick — moveCell etc. also call invalidateLedStates(), so the queued message is the only one
			// still addressing the OLD mapping.
			feedback.drainPendingOffs();
			processLights(args.sampleTime);
		}
	}

	// Returns the resolved color-set index for cell i (0–3). Auto mode maps by port direction.
	int getCellColorSet(int i) const {
		if (cellColorSet[i] >= 0) return cellColorSet[i];
		return (portAssignments[i].type == engine::Port::OUTPUT) ? 0 : 1;
	}

	// Pure — resolves one cell's LED colour and state id. Precedence: pending > connected-to-pending > port-learn > midi-learn > assigned > off.
	CellVisual resolveCellVisual(int i, bool blinkOn, bool slowBlinkOn) const {
		bool assigned = portAssignments[i].isValid();
		bool hasCable = portHasCable[i];
		bool connectedToPending = pendingCellId >= 0 && i != pendingCellId
			&& sceneStore.isConnected(sceneStore.current, pendingCellId, i);
		int cs = getCellColorSet(i);  // 0–3
		if (pendingCellId == i) {
			return { blinkOn ? LED_PENDING : LED_OFF, LED_STATE_PENDING };
		}
		if (connectedToPending) {
			NVGcolor col = slowBlinkOn ? COLOR_SETS[cs].color : nvgRGBf(0.f, 0.f, 0.f);
			return { col, LED_STATE_CONNECTED_BY_SET[cs] };
		}
		// Connected to a peer's armed port, so an armed cell reveals its partners on every instance. Reuses the
		// connected-to-pending state id on purpose: both mean "connected to the active selection", so every preset lights it as-is.
		if (peerConnected[i]) {
			NVGcolor col = slowBlinkOn ? COLOR_SETS[cs].color : nvgRGBf(0.f, 0.f, 0.f);
			return { col, LED_STATE_CONNECTED_BY_SET[cs] };
		}
		if (portLearningId == i) {
			return { blinkOn ? LED_PORT_LEARN : LED_OFF, LED_STATE_PORT_LEARN };
		}
		if (learningId == i) {
			return { blinkOn ? LED_MIDI_LEARN : LED_OFF, LED_STATE_MIDI_LEARN };
		}
		if (assigned) {
			NVGcolor col = color::mult(COLOR_SETS[cs].color, hasCable ? LED_BRIGHT : LED_DIM);
			int stateId = hasCable ? LED_STATE_COLOR_BY_SET[cs] : LED_STATE_COLOR_DIM_BY_SET[cs];
			return { col, stateId };
		}
		return { LED_OFF, LED_STATE_OFF };
	}

	// Pure — resolves one scene button's LED brightness and state id. Precedence: midi-learn > active > has-connections > off.
	SceneVisual resolveSceneVisual(int s, bool blinkOn) const {
		if (learningId == MATRIX_COUNT + s) {
			return { blinkOn ? LED_BRIGHT : 0.f, LED_STATE_MIDI_LEARN };
		}
		if (s == sceneStore.current) {
			return { LED_BRIGHT, LED_STATE_SCENE_ACTIVE };
		}
		bool hasConn = sceneStore.hasConnections(s);
		return { hasConn ? LED_SCENE_DIM : 0.f, hasConn ? LED_STATE_SCENE_DIM : LED_STATE_OFF };
	}

	// Adds this instance's assigned ports to out, keyed (moduleId, portId*2 + type) — the lookup form
	// SpliceKitWidget::step() uses to test whether a cable's far end lands on a cell. Split from step() for testability.
	void collectAssignedPorts(std::set<std::pair<int64_t, int>>& out) const {
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const PortAssignment& pa = portAssignments[i];
			if (pa.isValid()) {
				out.insert({pa.moduleId, pa.portId * 2 + (int)pa.type});
			}
		}
	}

	// GUI thread — collects the assigned ports of every SpliceKit instance whose cells may share a cable with
	// this one: this instance plus, when cross-instance patching is enabled, every other instance that also has it on.
	// A cross-instance cable lands on a *different* instance's port, so testing against this module's assignments
	// alone would report "no cable" and render the cell unconnected. Both ends must be resolved against the union.
	std::set<std::pair<int64_t, int>> collectCableEndCandidates() const {
		std::set<std::pair<int64_t, int>> ports;
		collectAssignedPorts(ports);
		if (!crossInstanceEnabled) return ports;

		// Iterates the instance registry rather than every module in the patch: this runs once per GUI frame
		// and the registry is already narrowed to SpliceKit instances.
		auto& reg = getInstances();
		auto it = reg.find(APP);
		if (it == reg.end()) return ports;
		for (SpliceKitModule* other : it->second) {
			if (other == this) continue;
			// Respect the other instance's opt-out: a module with cross-instance patching disabled never
			// participates, so its cells are not candidates.
			if (!other->crossInstanceEnabled) continue;
			other->collectAssignedPorts(ports);
		}
		return ports;
	}

	// Recomputes portHasCable[] for every cell, from the widget tree.
	// Normally driven by SpliceKitWidget::step(), which only runs while Rack is stepping widgets. When it is not
	// (editor closed, headless), the flag would stay frozen — and since the MIDI feedback loop in process() is
	// edge-triggered on the resolved stateId (which folds portHasCable[] in), a stale flag means activation/
	// deactivation feedback is never sent. The GuiTaskProcessor worker also calls this after each drain (safe:
	// both run off the engine thread, like step()).
	// Walks the widget tree rather than APP->engine->getCableIds(): the engine accessors each take a SharedLock,
	// so an engine-side scan would lock once per cable every tick; the widget tree is lock-free and carries the
	// same info. Only complete cables count (a cable being dragged has a null far end and is skipped), matching
	// what the engine would report.
	// Builds the candidate-end set once (collectCableEndCandidates()) so each lookup is O(log n) not O(n).
	void refreshPortHasCable() {
		assert(verifier.isUiOrWorker());
		std::set<std::pair<int64_t, int>> assignedPorts = collectCableEndCandidates();
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const PortAssignment& pa = portAssignments[i];
			if (!pa.isValid()) {
				portHasCable[i] = false;
				continue;
			}
			ModuleWidget* mw = APP->scene->rack->getModule(pa.moduleId);
			if (!mw) {
				portHasCable[i] = false;
				continue;
			}
			auto ports = (pa.type == engine::Port::OUTPUT) ? mw->getOutputs() : mw->getInputs();
			portHasCable[i] = false;
			for (PortWidget* pw : ports) {
				if (pw->portId != pa.portId) continue;
				for (CableWidget* cw : APP->scene->rack->getCablesOnPort(pw)) {
					PortWidget* other = (pa.type == engine::Port::OUTPUT) ? cw->inputPort : cw->outputPort;
					if (!other || !other->module) continue;
					if (assignedPorts.count({other->module->getId(), other->portId * 2 + (int)other->type})) {
						portHasCable[i] = true;
						break;
					}
				}
				break;
			}
		}
	}

	// GUI thread — recomputes peerConnected[] against the cable list the armed peer published. Never touches the
	// widget tree (the initiator resolved that once when it armed). Called alongside refreshPortHasCable(), from
	// widget step() or the GuiTaskProcessor worker.
	void refreshPeerConnected() {
		assert(verifier.isUiOrWorker());
		// Safe to read crossPending here (GUI thread), unlike from resolveCellVisual().
		auto& reg = crossPending();
		auto it = reg.find(APP);
		const CrossPendingState* cp = (it != reg.end()) ? &it->second : nullptr;
		// Nobody armed, we are the initiator (our own pendingCellId drives our LEDs), or we
		// opted out.
		if (!crossInstanceEnabled || !cp || !cp->isValid() || cp->initiator == this) {
			std::fill(peerConnected, peerConnected + MATRIX_COUNT, false);
			return;
		}
		for (int i = 0; i < MATRIX_COUNT; i++) {
			const PortAssignment& pa = portAssignments[i];
			// resolveDirection() also rejects same-direction and invalid pairs.
			peerConnected[i] = resolveDirection(cp->port, pa).first
				&& cp->partners.count({pa.moduleId, pa.portId}) > 0;
		}
	}

	// GUI thread — collects the far end of every complete cable on `pa`, keyed (moduleId, portId). Direction is
	// not part of the key (a far end is necessarily the opposite direction); callers re-check via resolveDirection().
	static void collectCablePartners(const PortAssignment& pa, std::set<std::pair<int64_t, int>>& out) {
		if (!pa.isValid()) return;
		ModuleWidget* mw = APP->scene->rack->getModule(pa.moduleId);
		if (!mw) return;
		bool isOutput = (pa.type == engine::Port::OUTPUT);
		for (PortWidget* pw : (isOutput ? mw->getOutputs() : mw->getInputs())) {
			if (pw->portId != pa.portId) continue;
			for (CableWidget* cw : APP->scene->rack->getCablesOnPort(pw)) {
				// Incomplete cables (one end still being dragged) have a null far end.
				PortWidget* far = isOutput ? cw->inputPort : cw->outputPort;
				if (!far || !far->module) continue;
				out.insert({far->module->getId(), far->portId});
			}
			break;
		}
	}

	// Engine thread — MidiTrackingProcessorHandler callback for every mapped MIDI message. Routes cell
	// triggers and scene changes into taskProcessorUi.
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
			// Scene buttons are inert while following a scene link master — see the matching guard in process()'s physical scene-button handling.
			if (sceneLinkMasterId >= 0) return;
			int sceneId = (int)(mapId - MATRIX_COUNT);
			if (value > 0) {
				if (midiSceneCopyEnabled && pendingMidiSceneId >= 0 && pendingMidiSceneId != sceneId) {
					// Two consecutive activations without a release: treat as scene copy. Opt-in (midiSceneCopyEnabled) —
					// assumes the controller sends a release between selections; with the gate off, the second activation
					// falls through to a normal scene selection below.
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

	// Engine thread — MidiTrackingProcessorHandler callback fired when a MIDI learn completes. Advances the cursor
	// in sequential learn mode, or clears it for single learn. NOTE: writes learningId/midiLearnMode which the GUI
	// thread also reads/writes (context menus) — the race is accepted (both sides only write simple scalars).
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

	// GUI thread — starts MIDI learn for a single cell (id < MATRIX_COUNT) or scene button (id >= MATRIX_COUNT); cancels any active learn first.
	void enableLearn(int id) {
		assert(verifier.isUiOrWorker());
		disableLearn();
		disablePortLearn();
		if (id < 0 || id >= TOTAL_MAPS) return;
		learningId = id;
		trackingProcessor.enableMapLearn(id);
	}

	// GUI thread — starts sequential MIDI learn from lastClickedCell through cell 63. Scene buttons are excluded
	// (assign individually or via applyPresetLayout()).
	void startGlobalLearn() {
		assert(verifier.isUiOrWorker());
		disableLearn();
		disablePortLearn();
		midiLearnMode = true;
		learningId = lastClickedCell;
		trackingProcessor.enableMapLearn(lastClickedCell);
	}

	// GUI thread — starts sequential port-assignment learn from lastClickedCell through cell 63. Uses
	// LEARN_MODE::MULTI so the owner widget stays focused across clicks (step() re-asserts selection); a single
	// persistent callback advances portLearningId in place.
	void startGlobalPortLearn(Widget* owner) {
		assert(verifier.isUiOrWorker());
		disableLearn();
		disablePortLearn();
		portLearnMode = true;
		portLearningId = lastClickedCell;
		portSelectProcessor.setOwner(owner);
		portSelectProcessor.startLearn(
			[=](PortWidget* pw, Vec) {
				if (!pw->module) return;
				assignPort(portLearningId, pw->module->getId(), pw->portId, pw->type);
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

	// Engine or GUI thread — cancels any active MIDI learn. Called from process() (engine) when the user presses
	// the blinking cell, and from the GUI thread via context menus and onReset(). The race on learningId is accepted.
	void disableLearn() {
		trackingProcessor.disableMapLearn();
		learningId = -1;
		midiLearnMode = false;
	}

	// GUI thread — starts port-assignment learn for a single cell; portSelectProcessor intercepts the next
	// port-widget click and writes portAssignments[id].
	void enablePortLearn(int id, Widget* owner) {
		assert(verifier.isUiOrWorker());
		if (id < 0 || id >= MATRIX_COUNT) return;
		disableLearn();
		disablePortLearn();
		portLearningId = id;
		portSelectProcessor.setOwner(owner);
		portSelectProcessor.startLearn(
			[=](PortWidget* pw, Vec) {
				if (!pw->module) return;
				assignPort(portLearningId, pw->module->getId(), pw->portId, pw->type);
				portLearningId = -1;
			}
		);
	}

	// GUI thread — cancels an active port-assignment learn.
	void disablePortLearn() {
		assert(verifier.isUiOrWorker());
		portSelectProcessor.disableLearn();
		portLearningId = -1;
		portLearnMode = false;
	}

	// GUI thread — returns true if the given cell is currently in port-learn mode.
	bool isPortLearning(int id) {
		return portLearningId == id && portSelectProcessor.isLearning();
	}

	// GUI thread — replaces all MIDI input mappings with the active feedback preset's slot layout (no-op if none).
	void applyPresetLayout() {
		assert(verifier.isUiOrWorker());
		feedback.applyPresetLayout(
			[this]() { trackingProcessor.clearMaps(); },
			[this](MidiTrackingType type, int id, uint16_t number) { trackingProcessor.setMap(type, id, number); }
		);
	}

	// GUI thread — serializes all persistent state to JSON (called on patch save / undo snapshot).
	json_t* dataToJson() override {
		assert(verifier.isUiOrWorker());
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "currentScene", json_integer(sceneStore.current));

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
		for (int s = 0; s < SCENE_COUNT; s++) {
			json_t* connJ = json_array();
			for (int a = 0; a < MATRIX_COUNT; a++) {
				for (int b = a + 1; b < MATRIX_COUNT; b++) {
					if (sceneStore.isConnected(s, a, b)) {
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
		json_object_set_new(rootJ, "buttonMode", json_integer((int)buttonMode));
		json_object_set_new(rootJ, "overlayEnabled", json_boolean(overlayEnabled));
		json_object_set_new(rootJ, "crossInstanceEnabled", json_boolean(crossInstanceEnabled));
		json_object_set_new(rootJ, "midiSceneCopyEnabled", json_boolean(midiSceneCopyEnabled));
		json_object_set_new(rootJ, "sceneLinkMasterId", json_integer(sceneLinkMasterId));
		if (feedback.isActive()) {
			json_object_set_new(rootJ, "activePreset", json_string(feedback.activePresetJsonText().c_str()));
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
		json_object_set_new(rootJ, "midiOutput", feedback.midiOutput.toJson());
		return rootJ;
	}

	// GUI thread — restores all persistent state from JSON (patch load / undo). clearMaps() is called first to
	// prevent duplicate vector entries if setMap() runs multiple times for the same slot (undo/redo).
	void dataFromJson(json_t* rootJ) override {
		assert(verifier.isUiOrWorker());
		// A pending selection or active learn made against the pre-load assignments is stale afterwards
		// (clearPendingForCell() documents the same stranding hazard).
		clearPendingGui();
		disableLearn();
		disablePortLearn();
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		// Clamped: an out-of-range value from a corrupted/hand-edited patch would otherwise be used unchecked to
		// index sceneStore.connections[current] (captureScene, switchScene, randomizeCurrentScene, cell menu),
		// reading/writing past the array.
		sceneStore.current = clamp((int)json_integer_value(json_object_get(rootJ, "currentScene")), 0, SCENE_COUNT - 1);
		json_t* buttonModeJ = json_object_get(rootJ, "buttonMode");
		if (buttonModeJ) buttonMode = (ButtonMode)json_integer_value(buttonModeJ);
		json_t* overlayEnabledJ = json_object_get(rootJ, "overlayEnabled");
		if (overlayEnabledJ) overlayEnabled = json_boolean_value(overlayEnabledJ);
		json_t* crossInstanceEnabledJ = json_object_get(rootJ, "crossInstanceEnabled");
		if (crossInstanceEnabledJ) crossInstanceEnabled = json_boolean_value(crossInstanceEnabledJ);
		json_t* midiSceneCopyEnabledJ = json_object_get(rootJ, "midiSceneCopyEnabled");
		midiSceneCopyEnabled = midiSceneCopyEnabledJ ? json_boolean_value(midiSceneCopyEnabledJ) : false;
		json_t* sceneLinkMasterIdJ = json_object_get(rootJ, "sceneLinkMasterId");
		sceneLinkMasterId = sceneLinkMasterIdJ ? json_integer_value(sceneLinkMasterIdJ) : -1;

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

		for (auto& pa : portAssignments) pa.clear();
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

		json_t* activePresetJ = json_object_get(rootJ, "activePreset");
		const char* activePresetStr = (activePresetJ && json_is_string(activePresetJ)) ? json_string_value(activePresetJ) : nullptr;
		feedback.setActivePresetJson(activePresetStr ? activePresetStr : "");
		feedback.invalidateLedStates();

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
		if (midiOutputJ) feedback.midiOutput.fromJson(midiOutputJ);

		for (auto& s : sceneStore.connections) s.fill(0);
		json_t* scenesJ = json_object_get(rootJ, "scenes");
		if (scenesJ) {
			const char* key;
			json_t* sceneJ;
			json_object_foreach(scenesJ, key, sceneJ) {
				int s = std::atoi(key);
				if (s < 0 || s >= SCENE_COUNT) continue;
				json_t* connJ = json_object_get(sceneJ, "connections");
				if (!connJ) continue;
				size_t k;
				json_t* pairJ;
				json_array_foreach(connJ, k, pairJ) {
					int a = json_integer_value(json_array_get(pairJ, 0));
					int b = json_integer_value(json_array_get(pairJ, 1));
					if (a >= 0 && a < MATRIX_COUNT && b >= 0 && b < MATRIX_COUNT && a != b) {
						sceneStore.setConnection(s, a, b, true);
					}
				}
			}
		}
	}

	// --- Cable manipulation (GUI thread only) ---
	// Low-level helpers (findCable, removeCable, addCableToPort) live in SpliceKit_cable.hpp; the scene-topology
	// bitmask and its patch-reconciliation logic live in SceneStore. The wrappers here add module-level side effects
	// (overlay messages, listener notification) that don't belong on a GUI-thread scene-storage class.

	// GUI thread — creates or removes the cable between cellIdA and cellIdB in the current scene, then updates
	// overlayMessage. One cell must be an output and the other an input; same-direction pairs are ignored.
	void toggleConnection(int cellIdA, int cellIdB) {
		assert(verifier.isUiOrWorker());
		const PortAssignment& a = portAssignments[cellIdA];
		const PortAssignment& b = portAssignments[cellIdB];

		if (!a.isValid() || !b.isValid()) {
			return;
		}
		auto dir = resolveDirection(a, b);
		const PortAssignment* outPd = dir.first;
		const PortAssignment* inPd = dir.second;
		if (!outPd) {
			bool bothOut = (a.type == engine::Port::OUTPUT && b.type == engine::Port::OUTPUT);
			setOverlayMessage(bothOut ? "Both ports are outputs" : "Both ports are inputs",
				portLabel(a), portLabel(b));
			return;
		}
		int outCell = (outPd == &a) ? cellIdA : cellIdB;
		int inCell = (inPd == &a) ? cellIdA : cellIdB;

		if (sceneStore.cableIsLive(outCell, inCell)) {
			sceneStore.disconnectLive(outCell, inCell);
			setOverlayMessage("Cable removed", portLabel(*outPd), portLabel(*inPd));
		}
		else {
			sceneStore.connectLive(outCell, inCell);
			setOverlayMessage("Cable created", portLabel(*outPd), portLabel(*inPd));
		}
	}


	// GUI thread — returns a human-readable label for a port assignment (e.g. "VCO · Out 1"); falls back to a
	// placeholder if the module widget is no longer in the rack.
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

	// Engine thread — handles a single cell activation (button press or MIDI trigger). First press sets
	// pendingCellId and defers all cross-instance logic to the GUI thread via taskProcessorUi (so crossPending is
	// only ever touched from one thread). Second press on a different cell enqueues toggleConnection; same cell cancels.
	void triggerCell(int id) {
		if (!portAssignments[id].isValid()) return;

		if (pendingCellId < 0) {
			// pendingCellId is set here on the engine thread, but the responder lambda only clears it via
			// clearPendingLocal() on the GUI thread a frame later.
			pendingCellId = id;
			taskProcessorUi.enqueue([this, id]() {
				// GUI thread — sole owner of crossPending. Re-check cp validity here because another
				// instance's lambda may have already consumed it.
				auto& cp = crossPending()[APP];
				if (crossInstanceEnabled && cp.isValid() && cp.initiator != this) {
					// Responder path: create the cable directly (we are on the GUI thread).
					SpliceKitModule* initiator = cp.initiator;
					PortAssignment iPort = cp.port;
					PortAssignment rPort = portAssignments[id];
					cp.clear();
					initiator->clearPendingLocal();
					clearPendingLocal();  // reset our own tentative pendingCellId
					auto dir = resolveDirection(iPort, rPort);
					const PortAssignment* outPd = dir.first;
					const PortAssignment* inPd = dir.second;
					if (outPd) {
						if (vcv::hasCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId)) {
							vcv::removeCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId, false);
							setOverlayMessage("Cable removed", portLabel(*outPd), portLabel(*inPd));
						}
						else {
							vcv::addCable(outPd->moduleId, outPd->portId, inPd->moduleId, inPd->portId, false);
							setOverlayMessage("Cable created", portLabel(*outPd), portLabel(*inPd));
						}
					}
					else {
						// Same-direction pair (both outputs or both inputs) — the peer's gesture cannot complete.
						bool bothOut = (iPort.type == engine::Port::OUTPUT && rPort.type == engine::Port::OUTPUT);
						setOverlayMessage(bothOut ? "Both ports are outputs" : "Both ports are inputs",
							portLabel(iPort), portLabel(rPort));
					}
				}
				else {
					// Initiator path (cp was already consumed, or no cross-pending).
					if (crossInstanceEnabled) {
						cp.initiator = this;
						cp.cellId = id;
						cp.port = portAssignments[id];
						// Resolve once here for every peer to read (see partners' declaration).
						cp.partners.clear();
						collectCablePartners(cp.port, cp.partners);
					}
					const std::string& lbl = cellLabels[id];
					if (!lbl.empty()) setOverlayMessage(lbl, portLabel(portAssignments[id]));
					else setOverlayMessage("Port selected", portLabel(portAssignments[id]));
				}
			});
		}
		else if (pendingCellId == id) {
			clearPending();
		}
		else {
			int a = pendingCellId, b = id;
			taskProcessorUi.enqueue([this, a, b]() { toggleConnection(a, b); });
			clearPending();
		}
	}

	// Engine thread — enqueues a sceneStore.switchTo call on the GUI thread via taskProcessorUi.
	void requestSceneChange(int i) {
		taskProcessorUi.enqueue([this, i]() { sceneStore.switchTo(i); });
	}

	// Engine thread — enqueues a sceneStore.copy call on the GUI thread via taskProcessorUi.
	void requestCopyScene(int src, int dst) {
		taskProcessorUi.enqueue([this, src, dst]() { sceneStore.copy(src, dst); });
	}

	// GUI thread — moves the port assignment, label, color, and all scene connections from fromId to toId.
	// toId's existing assignment is discarded. MIDI mappings stay on their original cells (tied to physical
	// button positions, not ports). fromId's physical cables are NOT touched (they connect fromId's port, which
	// toId inherits, and remain valid); only toId's existing cables are removed (its old port is discarded).
	void moveCell(int fromId, int toId) {
		assert(verifier.isUiOrWorker());
		if (fromId == toId) return;

		// Resolve both note-offs now, while each cell still has its own MIDI mapping; rewriting it first would make
		// the off address the wrong note. The send itself is deferred to the engine thread (drainPendingOffs).
		feedback.queueFeedbackOff(fromId, feedback.cellLedState[fromId]);
		feedback.queueFeedbackOff(toId, feedback.cellLedState[toId]);

		// Remove only toId's existing cables — fromId's cables stay because toId will inherit fromId's port
		// and those cables are already in the right place.
		sceneStore.removeCellConnections(toId);

		// Rewrite sceneStore's connections for every scene (Step A: tear out toId's existing connections;
		// Step B: redirect fromId's connections to toId).
		sceneStore.moveCellBits(fromId, toId);

		// Move port assignment, label, and color. MIDI mapping is intentionally kept on each cell (tied to the
		// physical button position, not the port) and must not follow the port when relocated.
		portAssignments[toId] = portAssignments[fromId];
		portAssignments[fromId].clear();

		cellLabels[toId] = std::move(cellLabels[fromId]);
		cellLabels[fromId] = "";
		cellColorSet[toId] = cellColorSet[fromId];
		cellColorSet[fromId] = -1;

		feedback.invalidateLedStates();
		setOverlayMessage("Moved cell", portLabel(portAssignments[toId]));
	}

	// GUI thread — (re)binds cellId to a module port. Used by all three assignment gestures: dropping a cable
	// end on a cell, single "Learn port", and sequential port learn.
	// Rebinding discards the cell's previous port, so — exactly as in moveCell() for the destination cell —
	// everything derived from that old port must go with it:
	//   * current-scene cables are removed while the old assignment is still in place (SceneStore resolves cable
	//     coordinates via its `ports` view of portAssignments, so this must happen before the overwrite or it
	//     would resolve the NEW port and orphan the old cables),
	//   * the connection bitmasks are cleared in *every* scene — a stale bit in an inactive scene would recreate a
	//     cable to the wrong port on the next switchScene(),
	//   * the label is dropped (it described the old port),
	//   * LED state is invalidated (the color set is derived from port direction — see getCellColorSet — and would
	//     otherwise keep the previous set's color),
	//   * a pending selection is dropped (made against the old port — see clearPendingForCell() for the stranding hazard).
	void assignPort(int cellId, int64_t moduleId, int portId, engine::Port::Type type) {
		assert(verifier.isUiOrWorker());
		if (cellId < 0 || cellId >= MATRIX_COUNT) return;

		clearPendingForCell(cellId);
		if (portAssignments[cellId].isValid()) {
			// Resolve the note-off while the old mapping and state are still addressable; the engine thread
			// sends it on its next tick.
			feedback.queueFeedbackOff(cellId, feedback.cellLedState[cellId]);
			sceneStore.removeCellConnections(cellId);
			sceneStore.clearCell(cellId);
			cellLabels[cellId].clear();
		}

		portAssignments[cellId].moduleId = moduleId;
		portAssignments[cellId].portId = portId;
		portAssignments[cellId].type = type;
		feedback.invalidateLedStates();
	}

	// GUI thread — discards cellId's port assignment entirely (menu "Clear"). Shares assignPort()'s cleanup
	// contract: a cleared port must be forgotten by the same state a rebound port forgets — current-scene cables,
	// every scene's connection bitmask, and the label.
	void clearPort(int cellId) {
		assert(verifier.isUiOrWorker());
		if (cellId < 0 || cellId >= MATRIX_COUNT) return;
		if (!portAssignments[cellId].isValid()) return;

		clearPendingForCell(cellId);
		// Resolve the note-off while the old mapping still addresses the right note.
		feedback.queueFeedbackOff(cellId, feedback.cellLedState[cellId]);
		sceneStore.removeCellConnections(cellId);
		sceneStore.clearCell(cellId);
		cellLabels[cellId].clear();
		portAssignments[cellId].clear();
		feedback.invalidateLedStates();
	}

	// GUI thread — full module reset: removes all current-scene cables, clears all stored scenes and port
	// assignments, resets scene/preset selection, and clears LED state. Touches cables and widget state, so
	// engine-thread callers must route through taskProcessorUi rather than calling this directly.
	void resetModuleState() {
		assert(verifier.isUiOrWorker());
		static const SceneConns empty{};
		sceneStore.capture(sceneStore.current);
		sceneStore.applyToCurrent(empty);
		for (auto& s : sceneStore.connections) s.fill(0);
		for (int i = 0; i < MATRIX_COUNT; i++) portAssignments[i].clear();
		for (int i = 0; i < TOTAL_MAPS; i++) trackingProcessor.clearMap(i);
		sceneStore.current = 0;
		sceneLinkMasterId = -1;
		feedback.setActivePresetJson("");
		feedback.invalidateLedStates();
		std::fill(portHasCable, portHasCable  + MATRIX_COUNT, false);
	}
};


// Overlay widget added directly to APP->scene->rack (rack coordinates). Activated by space; hides cables
// and draws cell→port assignment splines.
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

	// Deterministic value in [-1, +1] for a given cell — golden-ratio sequence gives an even, non-repeating spread.
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

	// Draws a short arc between two cell-button centers to show which cells are wired together in the active scene.
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
			? module->sceneStore.connectionMask(module->sceneStore.current, hoveredCell)
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

// Scene button with right-click context menu and drag-and-drop. Left-drag A→B copies scene A's connections to
// scene B; right-click opens the per-scene context menu.
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
			module->sceneStore.copy(src->sceneId, sceneId);
		}
	}

	void createSceneMenu();
};

// Matrix cell button with right-click context menu and drag-and-drop. Left-drag A→B toggles the connection;
// shift+left-drag A→B moves A's port/label/color/scene connections onto B (B's assignment discarded; MIDI
// mappings stay on the physical cell, not the port); right-click opens the per-cell context menu.
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
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			shiftDrag = (e.mods & RACK_MOD_SHIFT) != 0;
		}
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

	// Left-drag → toggle connection; shift+left-drag → move cell assignment; dropping an in-progress cable
	// (dragged from any port) → assign that port to this cell.
	void onDragDrop(const event::DragDrop& e) override {
		SvgSwitch::onDragDrop(e);
		if (!module) return;

		// Alternative to the modal "Learn port" gesture: dragging a cable's loose end onto a cell assigns that
		// port directly. The temporary cable is left untouched — PortWidget::onDragEnd() discards it (never completed).
		if (auto* pw = dynamic_cast<PortWidget*>(e.origin)) {
			if (e.button != GLFW_MOUSE_BUTTON_LEFT || !pw->module) return;
			module->assignPort(cellId, pw->module->getId(), pw->portId, pw->type);
			return;
		}

		auto* src = dynamic_cast<SpliceKitCellButton*>(e.origin);
		if (!src || src->module != module || src->cellId == cellId) return;
		int a = src->cellId, b = cellId;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			// Either gesture rewrites the cells it touches, so a pending selection against their previous state is
			// stale and dropped for both paths.
			module->clearPendingGui();
			// Already on the GUI thread — run directly. taskProcessorUi's queue is single-producer (engine only);
			// see GuiTaskProcessor.hpp.
			if (src->shiftDrag) module->moveCell(a, b);
			else module->toggleConnection(a, b);
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
		for (int i = 0; i < SCENE_COUNT; i++) {
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
			// Releases this instance's cable-opacity hold if still in viz mode (no-op otherwise), so a
			// widget deleted mid-viz-mode restores the cables.
			Rack::cableOpacityState().release(this);
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
		if (active) Rack::cableOpacityState().hide(this);
		else Rack::cableOpacityState().release(this);
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
		module->taskProcessorUi.step();

		// Update cable presence for each assigned cell (read by process() for light colors). Same scan the worker
		// runs when there is no window, so one piece of logic maintains the flag on both paths.
		module->refreshPortHasCable();
		// Likewise for the cross-instance "connected to the armed port" highlight.
		module->refreshPeerConnected();
	}

	// A MenuLabel with a fixed pixel width instead of growing to fit the text on one line. MenuLabel::step() sets
	// box.size.x to the unwrapped text width, so a long description would render as one very wide line; blendish's
	// nvgTextBox() already wraps to whatever width it is given, so fixing the width here yields word-wrapped text.
	struct WrappedMenuLabel : ui::MenuLabel {
		static constexpr float WRAP_WIDTH = 300.f;
		void step() override {
			box.size.x = WRAP_WIDTH;
			box.size.y = bndLabelHeight(APP->window->vg, -1, text.c_str(), WRAP_WIDTH);
			Widget::step();
		}
	};

	// MenuItem base that opens a submenu on hover containing a single disabled label with the given description.
	// Used as the TMenuItem base for createCheckMenuItem() below, so the item both selects the preset on click and
	// shows its description on hover (like "MIDI Preset" itself).
	struct DescriptionMenuItem : ui::MenuItem {
		std::string description;
		ui::Menu* createChildMenu() override {
			if (description.empty()) return nullptr;
			ui::Menu* menu = new ui::Menu;
			WrappedMenuLabel* label = new WrappedMenuLabel;
			label->text = description;
			menu->addChild(label);
			return menu;
		}
	};

	// Called right after a controller preset becomes active (picked from the submenu or loaded from file). If the
	// preset carries an input mapping layout, whether to apply it too (same as the menu item).
	void promptApplyPresetLayout() {
		SpliceKitModule* module = this->module;
		const MidiOutPreset* preset = module->feedback.getActivePreset();
		if (!preset || !preset->hasLayout()) return;

		widget::Widget* overlay = confirmOverlayCreate(
			"Apply input mapping",
			"This controller preset also defines a MIDI input mapping.\nApply it as well?",
			"Apply",
			[]() { /* cancel: keep existing input mapping */ },
			[module]() { module->applyPresetLayout(); }
		);
		addChild(overlay);
	}

	void appendContextMenu(Menu* menu) override {
		SpliceKitModule* module = this->module;
		if (!module) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(StoermelderPackOne::Rack::createStickyMidiMenuItem("MIDI Input",  &module->trackingProcessor.getInput()));
		menu->addChild(StoermelderPackOne::Rack::createStickyMidiMenuItem("MIDI Output", &module->feedback.midiOutput));
		menu->addChild(createSubmenuItem("MIDI Preset", "", [=](Menu* menu) {
			menu->addChild(createMenuLabel("MIDI feedback"));
			menu->addChild(createCheckMenuItem("No preset", "",
				[=]() {
					return !module->feedback.isActive();
				},
				[=]() {
					module->feedback.setActivePresetJson("");
					module->feedback.invalidateLedStates();
				}
			));
			bool isKnownPreset = !module->feedback.isActive();
			for (const LoadedPreset& lp : getLoadedPresets()) {
				std::string json = lp.json;
				if (module->feedback.isActivePreset(json)) isKnownPreset = true;
				auto* item = createCheckMenuItem<DescriptionMenuItem>(lp.preset.name, "",
					[=]() {
						return module->feedback.isActivePreset(json);
					},
					[=]() {
						module->feedback.setActivePresetJson(json);
						module->feedback.invalidateLedStates();
						promptApplyPresetLayout();
					}
				);
				item->description = lp.preset.description;
				menu->addChild(item);
			}
			if (!isKnownPreset) {
				std::string name = module->feedback.activePresetName("Custom preset");
				menu->addChild(createCheckMenuItem(name, "",
					[=]() { return true; },
					[=]() {}
				));
			}
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuLabel("MIDI mapping"));
			const MidiOutPreset* activeP = module->feedback.getActivePreset();
			bool hasLayout = activeP && activeP->hasLayout();
			menu->addChild(createMenuItem("Apply input mappings from preset", "",
				[=]() { module->applyPresetLayout(); },
				!hasLayout
			));
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuItem("Load preset from file...", "",
				[=]() {
					osdialog_filters* filters = osdialog_filters_parse("SpliceKit Preset:ctrl.json;JSON:json");
					char* pathC = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
					osdialog_filters_free(filters);
					if (!pathC) return;
					std::string path = pathC;
					free(pathC);
					std::vector<uint8_t> bytes = system::readFile(path);
					if (bytes.empty()) return;
					std::string text(bytes.begin(), bytes.end());
					module->feedback.setActivePresetJson(text);
					module->feedback.invalidateLedStates();
					promptApplyPresetLayout();
				}
			));
			bool canSave = module->feedback.isActive();
			menu->addChild(createMenuItem("Save preset to file...", "",
				[=]() {
					osdialog_filters* filters = osdialog_filters_parse("SpliceKit Preset:ctrl.json");
					std::string defName = module->feedback.activePresetName("");
					if (!defName.empty()) defName += ".ctrl.json";
					char* pathC = osdialog_file(OSDIALOG_SAVE, NULL,
						defName.empty() ? "preset.ctrl.json" : defName.c_str(), filters);
					osdialog_filters_free(filters);
					if (!pathC) return;
					std::string path = pathC;
					free(pathC);
					const std::string& text = module->feedback.activePresetJsonText();
					system::writeFile(path, std::vector<uint8_t>(text.begin(), text.end()));
				},
				!canSave
			));
		}));
		menu->addChild(createCheckMenuItem("MIDI scene-copy gesture", "",
			[=]() { return module->midiSceneCopyEnabled; },
			[=]() { module->midiSceneCopyEnabled = !module->midiSceneCopyEnabled; }
		));
		menu->addChild(new MenuSeparator);
		menu->addChild(createCheckMenuItem("Sequential MIDI learn", "",
			[=]() {
				return module->midiLearnMode;
			},
			[=]() {
				if (module->midiLearnMode) module->disableLearn();
				else module->startGlobalLearn();
			}
		));
		menu->addChild(createCheckMenuItem("Sequential port learn", "",
			[=]() {
				return module->portLearnMode;
			},
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
					module->clearPendingGui();
				}
			}
		));
		menu->addChild(createSubmenuItem("Button mode", "", [=](Menu* menu) {
			menu->addChild(createCheckMenuItem("Toggle", "",
				[=]() {
					return module->buttonMode == SpliceKitModule::BUTTON_TOGGLE;
				},
				[=]() {
					module->buttonMode = SpliceKitModule::BUTTON_TOGGLE;
					module->clearPendingGui();
				}
			));
			menu->addChild(createCheckMenuItem("Momentary", "",
				[=]() {
					return module->buttonMode == SpliceKitModule::BUTTON_MOMENTARY;
				},
				[=]() {
					module->buttonMode = SpliceKitModule::BUTTON_MOMENTARY;
					module->clearPendingGui();
				}
			));
		}));

		menu->addChild(new MenuSeparator);
		// Collect the other SpliceKit instances in a single pass, with whether each already follows another module
		// (chains disallowed, see sceneLinkCandidateIsFollower) and whether any follows this module. Uses the widget
		// tree rather than APP->engine, which the UI thread must not touch.
		bool isMaster = false;
		std::vector<std::pair<int64_t, bool>> otherIdsFollower;
		for (Widget* w : APP->scene->rack->getModules()) {
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			if (!mw || !mw->module || mw->module->id == module->id) continue;
			auto* m = dynamic_cast<SpliceKitModule*>(mw->module);
			if (!m) continue;
			otherIdsFollower.push_back({m->id, m->sceneLinkMasterId >= 0});
			if (m->sceneLinkMasterId == module->id) isMaster = true;
		}
		// The submenu is disabled when there are no other SpliceKit instances to link to
		// (or when this module already has followers).
		bool noOthers = otherIdsFollower.empty();
		menu->addChild(createSubmenuItem("Scene link master", "", [=](Menu* menu) {
			menu->addChild(createCheckMenuItem("None", "",
				[=]() { return module->sceneLinkMasterId < 0; },
				[=]() { module->sceneLinkMasterId = -1; }
			));
			if (!otherIdsFollower.empty()) menu->addChild(new MenuSeparator);
			for (auto& idFollower : otherIdsFollower) {
				int64_t id = idFollower.first;
				bool isFollower = idFollower.second;
				std::string label = rack::string::f("id %lld", (long long)id);
				menu->addChild(createCheckMenuItem(label, isFollower ? "(already follows)" : "",
					[=]() { return module->sceneLinkMasterId == id; },
					[=]() { module->sceneLinkMasterId = id; },
					isFollower
				));
			}
		}, isMaster || noOthers));
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
		SceneConns empty{};
		module->sceneStore.reconcile(sceneId, empty);
	}));
	menu->addChild(createMenuItem("Copy", "", [=]() {
		if (sceneId == module->sceneStore.current) module->sceneStore.capture(sceneId);
		module->sceneClipboard = module->sceneStore.sceneConns(sceneId);
		module->sceneClipboardValid = true;
	}));
	menu->addChild(createMenuItem("Paste", "", [=]() {
		if (!module->sceneClipboardValid) return;
		module->sceneStore.reconcile(sceneId, module->sceneClipboard);
	}, !module->sceneClipboardValid));

	// randomizeCurrentScene()/randomizeCurrentSceneFull() always target the active scene, not necessarily the one
	// clicked — only offer them here when they're the same scene.
	bool notCurrent = sceneId != module->sceneStore.current;
	menu->addChild(createSubmenuItem("Randomize", "", [=](Menu* menu) {
		menu->addChild(createMenuItem("Sparse", "", [=]() {
			module->randomizeCurrentScene();
		}, notCurrent));
		menu->addChild(createMenuItem("Full", "", [=]() {
			module->randomizeCurrentSceneFull();
		}, notCurrent));
	}, notCurrent));

	menu->addChild(new MenuSeparator);

	int mapId = MATRIX_COUNT + sceneId;
	auto& midiMap = module->trackingProcessor.getMap(mapId);
	std::string midiLabel = midiMapLabel(midiMap);

	std::string learnLabel = midiLabel.empty() ? "Learn MIDI" : string::f("Learn MIDI (%s)", midiLabel.c_str());
	menu->addChild(createCheckMenuItem(learnLabel, "",
		[=]() {
			return module->learningId == MATRIX_COUNT + sceneId;
		},
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
	// This makes the per-cell "Start sequential learn..." item begin at this cell rather than cell 0 — the
	// module-level context menu has no click to anchor on (the actual bug; see startGlobalLearn/startGlobalPortLearn).
	module->lastClickedCell = cellId;
	auto& pa = module->portAssignments[cellId];
	std::string label = SpliceKitModule::portLabel(pa);

	auto& midiMap = module->trackingProcessor.getMap(cellId);
	std::string midiLabel = midiMapLabel(midiMap);

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
	bool hasConns = module->sceneStore.connectionMask(module->sceneStore.current, cellId) != 0;
	menu->addChild(createSubmenuItem("Remove cable", "",
		[=](Menu* menu) {
			uint64_t mask = module->sceneStore.connectionMask(module->sceneStore.current, cellId);
			for (int j = 0; j < MATRIX_COUNT; j++) {
				if (!((mask >> j) & 1)) continue;
				std::string connLabel = SpliceKitModule::portLabel(module->portAssignments[j]);
				int cid = cellId, jid = j;
				menu->addChild(createMenuItem(connLabel, "", [=]() {
					module->sceneStore.disconnectLive(cid, jid);
				}));
			}
		},
		!hasConns
	));
	menu->addChild(createMenuItem("Remove all cables", "",
		[=]() { module->sceneStore.removeCellConnections(cellId); },
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
		module->clearPort(cellId);
	}, !pa.isValid()));

	menu->addChild(new MenuSeparator);
	menu->addChild(createMenuLabel("MIDI"));
	std::string learnMidiLabel = midiLabel.empty() ? "Learn" : string::f("Learn MIDI (%s)", midiLabel.c_str());
	menu->addChild(createCheckMenuItem(learnMidiLabel, "",
		[=]() {
			return module->learningId == cellId;
		},
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