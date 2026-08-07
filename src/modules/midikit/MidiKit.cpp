#include "MidiScriptEngine.h"
#include "MidiScriptEngineLua.h"
#include "MidiScriptEngineQuickJs.h"
#include "../../components/Knobs.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../components/LedTextField.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include "tipsy-encoder/include/tipsy/tipsy.h"
#include <osdialog.h>
#include <fstream>
#include <queue>
#include <atomic>

namespace StoermelderPackOne {
namespace MidiKit {


enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

struct MidiOutput : midi::Output {
	struct FrameSchedule {
		midi::Message msg;
		bool operator<(const FrameSchedule& other) const {
			return msg.frame > other.msg.frame;
		}
	};
	
	struct TickSchedule {
		midi::Message msg;
		uint64_t tick;
		bool operator<(const TickSchedule& other) const {
			return tick > other.tick;
		}
	};

	std::priority_queue<FrameSchedule> frameQueue;
	std::priority_queue<TickSchedule> tickQueue;

	std::vector<int> getChannels() override {
		std::vector<int> channels;
		for (int c = -1; c < 16; c++) {
			channels.push_back(c);
		}
		return channels;
	}

	void reset() {
		Output::reset();
		while (!frameQueue.empty()) frameQueue.pop();
		while (!tickQueue.empty()) tickQueue.pop();
		channel = -1;
	}

	void send(midi::Message& msg, uint64_t tick) {
		if (tick != 0) {
			TickSchedule s;
			s.msg = msg;
			s.tick = tick;
			tickQueue.push(s);
			return;
		}

		if (msg.frame != -1) {
			FrameSchedule s;
			s.msg = msg;
			frameQueue.push(s);
			return;
		}

		sendMessage(msg);
	}

	void processFrame(int64_t frame) {
		while (true) {
			if (frameQueue.size() == 0) return;
			FrameSchedule s = frameQueue.top();
			// ">=" and not ">": s.msg.frame is the engine frame the message is
			// intended to be processed at (midi.hpp). With ">" a message due
			// exactly at the current frame is deferred to the next processFrame()
			// call — one divider period later. Mirrors the processTick() fix.
			if (frame >= s.msg.frame) {
				frameQueue.pop();
				s.msg.frame = -1;
				sendMessage(s.msg);
			}
			else {
				return;
			}
		}
	}

	void processTick(uint64_t tick) {
		while (true) {
			if (tickQueue.size() == 0) return;
			TickSchedule s = tickQueue.top();
			// ">=" and not "==": process() calls processTick() before draining the
			// engine's out-queue, so a script can schedule for a tick the counter has
			// already consumed. With "==" such a message is never sent and, since the
			// queue is ordered smallest-tick-first, it blocks every later one behind it.
			if (tick >= s.tick) {
				tickQueue.pop();
				sendMessage(s.msg);
			}
			else {
				return;
			}
		}
	}
};


// Returns the one shared async worker for all MidiKit modules.
// The weak_ptr lets it be destroyed when the last module is removed.
//
// No mutex guards the expired()/make_shared/assignment sequence below. This is
// safe because modules are constructed on the UI thread, which is a single
// thread, so defaultWorker() is never called concurrently -- the static
// weak_ptr is only ever touched from that one thread.
static std::shared_ptr<ITaskWorker> defaultWorker() {
	static std::weak_ptr<ITaskWorker> shared;
	if (shared.expired()) {
		auto worker = std::make_shared<MpmcTaskWorker>("MidiKit worker");
		shared = worker;
		return worker;
	}
	return shared.lock();
}

struct MidiKitModule : Module, MidiScript::MidiScriptEngineHandler {
	enum ParamIds {
		ENUMS(PARAM, 4),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, 4),
		INPUT_TRIG,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_TRIG,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to Json] */
	midi::InputQueue midiInput;
	/** [Stored to Json] */
	MidiOutput midiOutput;
	/** [Stored to Json] */
	std::string script = "";
	/** [Stored to Json] */
	std::string scriptConfigJson = "";

	// MPMC queue: midiLogMessages is pushed from the worker thread (writeLog)
	// and from the caller of loadScript/onReset, so it needs concurrent-producer
	// support rather than dsp::RingBuffer's single-producer contract.
	rigtorp::MPMCQueue<std::tuple<LOG_FORMAT, float, std::string>> midiLogMessages{512};

	dsp::RingBuffer<int, 8> overlayQueue;
	std::tuple<std::string, std::string, std::string> overlayMessage;

	// MIDI output queue, owned by the module rather than either engine, so its
	// contents outlive engine switches and clearScript() rather than being tied
	// to whichever engine happened to be active when they were queued. Written
	// by sendMidi() (worker thread), drained by process() (audio thread) and by
	// onRemove() at teardown.
	dsp::RingBuffer<std::tuple<int, MidiScript::Message, uint64_t>, 128> midiOutQueue;
	// Set (worker thread) when sendMidi() drops a group for lack of room;
	// cleared and reported once (audio thread) in process(). A saturated output
	// must not flood the log through the same bottleneck that is already
	// saturated.
	std::atomic<bool> midiOutOverflow{false};

	// ── Tipsy output ─────────────────────────────────────────────────────────
	// Module-owned for the same reason as midiOutQueue: the encoder drives the
	// trigger CV output, so it belongs to the hardware side rather than to
	// whichever engine happened to queue a message. Scripts reach it through
	// the handler's sendTipsyOut(), exactly like sendMidi().

	// Caps on Tipsy payloads: the queue entry is a fixed-size POD so the audio
	// thread's shift() never heap-allocates (mime matches
	// tipsy::kMaxMimeTypeSize).
	static constexpr size_t tipsyOutMaxMimeTypeSize = 256;
	static constexpr size_t tipsyOutMaxPayloadLength = 256;

	// A Tipsy message queued by sendTipsyOut() (worker) and consumed by
	// processTipsyOutput() (audio). Fixed-size so the SPSC RingBuffer copies it
	// without heap allocation.
	//
	// mimeSize == 0 marks a discard sentinel rather than a real message: it
	// carries no payload and exists only to mark where a stale run of messages
	// ends. sendTipsyOut() rejects an empty mime type so the two can never be
	// confused. (dataSize would not work as the marker — an empty payload with
	// a valid mime type is a legitimate message.)
	struct TipsyOutMessage {
		uint16_t mimeSize;                         // length without NUL
		uint16_t dataSize;
		char mime[tipsyOutMaxMimeTypeSize];
		unsigned char data[tipsyOutMaxPayloadLength];
	};

	// Encodes Tipsy messages onto the trigger CV output. Audio thread only.
	tipsy::ProtocolEncoder tipsyOutEncoder;

	// SPSC queue of pending Tipsy messages (worker → audio). sendTipsyOut() only
	// copies the payload; encoding happens in processTipsyOutput() on the audio
	// thread, which owns the encoder.
	//
	// Never cleared: clear() writes `start`, the consumer's index, so calling it
	// from the worker would break the single-consumer contract. Discarding goes
	// through tipsyOutDiscardCount + a queued sentinel instead.
	dsp::RingBuffer<TipsyOutMessage, 8> tipsyOutQueue;

	// Discard sentinels enqueued so far. Written by sendTipsyOutReset()
	// (worker), read by processTipsyOutput() (audio); tipsyOutDiscardSeen is the
	// audio thread's private count of the ones it has consumed.
	//
	// A counter rather than a flag: two reloads in quick succession must discard
	// both batches, and a bool cleared after the first sentinel would let the
	// second play. The queued sentinel supplies the position the counter lacks —
	// anything pushed after it is new and survives.
	std::atomic<uint32_t> tipsyOutDiscardCount{0};
	uint32_t tipsyOutDiscardSeen = 0;

	// The message the encoder is streaming out. The encoder holds pointers into
	// its buffers for the whole (multi-cycle) message, so it must outlive the
	// encoding — a member, not a local (shift() hands out copies). Only ever
	// overwritten while the encoder is dormant, so an in-flight message is never
	// pulled out from under it.
	TipsyOutMessage tipsyOutCurrentMessage;

	dsp::ClockDivider processDivider;
	dsp::Timer rateLimiterTimer;

	dsp::SchmittTrigger inputTrigger;
	uint64_t inputTriggerTick;
	bool outputTriggerActive[PORT_MAX_CHANNELS];
	dsp::PulseGenerator outputPulseGenerator[PORT_MAX_CHANNELS];

	uint64_t sample;
	float sampleRate;

	// ── MidiScriptEngineHandler ──────────────────────────────────────────────
	// The engines call back into the module through these methods for every
	// module-facing operation (log/overlay/input/trig/param).

	// MidiScriptEngineHandler
	void writeLog(const std::string& log, bool useTimestamp = true) override {
		float timestamp = sampleRate != 0.f ? float(sample) / sampleRate : 0.f;
		if (useTimestamp) {
			midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, log));
		}
		else {
			midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, timestamp, log));
		}
	}

	// MidiScriptEngineHandler
	void writeOverlay(const std::string& s1, const std::string& s2, const std::string& s3) override {
		overlayQueue.push(0);
		overlayMessage = std::make_tuple(s1, s2, s3);
	}

	// MidiScriptEngineHandler
	void enableInput(int i) override {
		reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = true;
	}

	// MidiScriptEngineHandler
	float getInputVoltage(int i, uint8_t ch) override {
		if (reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled)
			return inputs[INPUT + i].getVoltage(ch);
		return 0.f;
	}

	// MidiScriptEngineHandler
	float getTrigVoltage(int i, uint8_t ch) override {
		return inputs[INPUT_TRIG + i].getVoltage(ch);
	}

	// MidiScriptEngineHandler
	uint64_t getTrigTicks(int i) override {
		return inputTriggerTick;
	}

	// MidiScriptEngineHandler
	void enableParam(int i) override {
		reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = true;
	}

	// MidiScriptEngineHandler
	float getParamValue(int i) override {
		if (reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled)
			return params[PARAM + i].getValue();
		return 0.f;
	}

	// MidiScriptEngineHandler
	void setTrig(int i, uint8_t ch, float duration = 1e-3f) override {
		outputTriggerActive[ch] = true;
		outputPulseGenerator[ch].trigger(duration);
	}

	// MidiScriptEngineHandler
	void setTrigVoltage(int i, uint8_t ch, float voltage) override {
		outputTriggerActive[ch] = false;
		outputs[OUTPUT_TRIG].setVoltage(voltage, ch);
	}

	// MidiScriptEngineHandler
	bool sendMidi(int midiPort, const MidiScript::Message* msgs, size_t count, uint64_t tick) override {
		// Capacity is checked for the whole group, so an NRPN is never
		// half-emitted. dsp::RingBuffer::push() does not bounds-check: on a full
		// buffer it overwrites unread entries and leaves size() > capacity.
		if (midiOutQueue.capacity() < count) {
			midiOutOverflow.store(true, std::memory_order_relaxed);
			return false;
		}
		for (size_t i = 0; i < count; i++) {
			midiOutQueue.push(std::make_tuple(midiPort, msgs[i], tick));
		}
		return true;
	}

	// MidiScriptEngineHandler
	bool sendTipsyOut(const char* mimeType, const unsigned char* data, uint32_t dataBytes) override {
		if (!mimeType || !data || dataBytes > tipsyOutMaxPayloadLength) {
			writeLog("Tipsy: invalid parameters", false);
			return false;
		}
		size_t mimeSize = strlen(mimeType);
		// An empty mime type would be indistinguishable from a discard sentinel.
		if (mimeSize == 0) {
			writeLog("Tipsy: mime type must not be empty", false);
			return false;
		}
		if (mimeSize + 1 > tipsyOutMaxMimeTypeSize) {
			writeLog("Tipsy: mime type too long", false);
			return false;
		}
		// One slot is kept free so sendTipsyOutReset() can always enqueue its
		// sentinel. A dropped sentinel would leave tipsyOutDiscardCount permanently
		// ahead of the queue, discarding live messages from then on.
		if (tipsyOutQueue.capacity() <= 1) {
			writeLog("Tipsy: pending queue full", false);
			return false;
		}

		TipsyOutMessage p;
		p.mimeSize = (uint16_t)mimeSize;
		p.dataSize = (uint16_t)dataBytes;
		std::memcpy(p.mime, mimeType, mimeSize + 1);
		std::memcpy(p.data, data, dataBytes);
		tipsyOutQueue.push(p);
		return true;
	}

	// MidiScriptEngineHandler
	void sendTipsyOutReset() override {
		// Order matters: the sentinel goes in first, so the audio thread can
		// never see the raised count without the sentinel that bounds it.
		TipsyOutMessage p;
		p.mimeSize = 0;
		p.dataSize = 0;
		tipsyOutQueue.push(p);
		tipsyOutDiscardCount.fetch_add(1, std::memory_order_relaxed);
	}

	// Outputs the next Tipsy-encoded voltage on the trigger output (audio
	// thread). If the encoder is idle, drops any stale messages, starts the next
	// pending one, then drains one encoded float. Returns true if a voltage was
	// output.
	bool processTipsyOutput(uint8_t channel = 0) {
		if (tipsyOutEncoder.isDormant()) {
			// Everything ahead of an unconsumed sentinel was queued by a script
			// that has since been replaced. Only done while dormant, so a message
			// already going out still completes. Bounded by the queue size, and
			// Tipsy messages are rare, so draining the run in one call is fine.
			while (tipsyOutDiscardSeen < tipsyOutDiscardCount.load(std::memory_order_relaxed) && !tipsyOutQueue.empty()) {
				if (tipsyOutQueue.shift().mimeSize == 0) tipsyOutDiscardSeen++;
			}

			if (!tipsyOutQueue.empty()) {
				// Copy into the member the encoder points into for the whole message
				// (shift() returns a copy, so not a local).
				tipsyOutCurrentMessage = tipsyOutQueue.shift();
				auto initResult = tipsyOutEncoder.initiateMessage(tipsyOutCurrentMessage.mime, tipsyOutCurrentMessage.dataSize, tipsyOutCurrentMessage.data);
				if (tipsyOutEncoder.isError(initResult)) {
					writeLog("Tipsy encoder error: " + std::to_string(static_cast<int>(initResult)), false);
					return false;
				}
			}
		}

		if (tipsyOutEncoder.isDormant()) {
			return false;
		}

		float f;
		auto result = tipsyOutEncoder.getNextMessageFloat(f);
		if (tipsyOutEncoder.isError(result)) {
			writeLog("Tipsy encoding error", false);
			tipsyOutEncoder.terminateCurrentMessage();
			return false;
		}
		// Tipsy messages always go to the first trigger output (port 0).
		setTrigVoltage(0, channel, f);
		return true;
	}

	// Port/param counts injected into both engines at construction.
	static constexpr int engineInputCount = 4;
	static constexpr int engineInputTrigCount = 1;
	static constexpr int engineOutputTrigCount = 1;
	static constexpr int engineParamCount = 4;
	static constexpr int engineMidiInputCount = 1;
	static constexpr int engineMidiOutputCount = 1;

	MidiScript::Lua::MidiScriptEngineLua seLua;
	MidiScript::QuickJs::MidiScriptEngineQuickJs seQuickJs;
	MidiScript::MidiScriptEngine* activeEngine = nullptr;

	MidiKitModule() : MidiKitModule(defaultWorker()) {}
	explicit MidiKitModule(std::shared_ptr<ITaskWorker> worker)
		: seLua(this, engineInputCount, engineInputTrigCount, engineOutputTrigCount, engineParamCount, engineMidiInputCount, engineMidiOutputCount),
		  seQuickJs(this, engineInputCount, engineInputTrigCount, engineOutputTrigCount, engineParamCount, engineMidiInputCount, engineMidiOutputCount) {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_TRIG, "Trigger");
		configOutput(OUTPUT_TRIG, "Trigger");
		for (int i = 0; i < 4; i++) {
			configInput<MidiScript::MidiScriptEnginePortInfo>(INPUT + i)->se = &seQuickJs;
			configParam<MidiScript::MidiScriptEngineParamQuantity>(PARAM + i, 0.f, 1.f, 0.f)->se = &seQuickJs;
		}

		processDivider.setDivision(8);
		seLua.setWorker(worker);
		seQuickJs.setWorker(worker);
		onReset();
	}

	// Closes the active engine and drains whatever its onUnload() queued. Rack
	// dispatches this before the module leaves the engine and holds the engine
	// mutex across it, so process() cannot run concurrently.
	//
	// closeState() blocks, so the worker has stopped producing before the drain
	// — preserve that order, it is what makes the drain safe.
	void onRemove(const RemoveEvent& e) override {
		MidiScript::MidiScriptEngine* engine = activeEngine;
		activeEngine = nullptr;   // stop process() dispatching
		if (engine) engine->closeState();
		flushOutput();            // drains the module queue; no engine needed
	}

	// Sends whatever is left in the module's out-queue straight to the device,
	// ignoring frame/tick scheduling — teardown is the last chance to emit.
	void flushOutput() {
		while (!midiOutQueue.empty()) {
			auto t = midiOutQueue.shift();
			midi::Message msg = std::get<1>(t);
			msg.frame = -1;
			midiOutput.sendMessage(msg);
		}
	}

	void onReset() override {
		midiInput.reset();
		midiOutput.reset();
		sample = 0;
		inputTriggerTick = 0;
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = false;
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = false;
		}
		for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			outputTriggerActive[i] = true;
			outputPulseGenerator[i].reset();
		}
		// Only the previously active engine can have real state — loadScript()
		// maintains that invariant by only ever loading one engine at a time and
		// closing the other on switch — so that is the only one that needs
		// tearing down here. Blocking (closeState()) rather than fire-and-forget,
		// same reasoning as loadScript(): once this returns, activeEngine is
		// again the only engine that can have any outstanding worker task.
		MidiScript::MidiScriptEngine* prevEngine = activeEngine;
		activeEngine = nullptr;
		if (prevEngine) prevEngine->closeState();

		midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::RESET, 0.f, std::string("")));
		midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("No script")));
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
	}

	void processBypass(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			(void)0;
		}
		Module::processBypass(args);
	}

	void process(const ProcessArgs& args) override {
		/*
		// play it safe and limit MIDI output rate to 200 Hz.
		const float rateLimiterPeriod = 1 / 200.f;
		bool rateLimiterTriggered = (rateLimiterTimer.process(args.sampleTime) >= rateLimiterPeriod);
		if (rateLimiterTriggered)
			rateLimiterTimer.time -= rateLimiterPeriod;
		else
			return;
		*/

		if (activeEngine && inputTrigger.process(inputs[INPUT_TRIG].getVoltage())) {
			inputTriggerTick++;
			midiOutput.processTick(inputTriggerTick);
			activeEngine->processInTick(0);
		}

		if (processDivider.process()) {
			if (activeEngine) {
				midi::Message msg;
				while (midiInput.tryPop(&msg, args.frame)) {
					activeEngine->processInMessage(0, msg);
				}

				activeEngine->process();
			}

			// Drains the module's own out-queue regardless of activeEngine, so a
			// cleared script's onUnload() output (queued while activeEngine was
			// still set) still reaches the device even though activeEngine is now
			// null. Runs after dispatch so output produced by this tick's
			// activeEngine->process() above still drains this same tick.
			if (midiOutOverflow.exchange(false, std::memory_order_relaxed)) {
				float timestamp = sampleRate != 0.f ? float(sample) / sampleRate : 0.f;
				midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, timestamp, std::string("MIDI output queue full, message(s) dropped")));
			}
			while (!midiOutQueue.empty()) {
				auto t = midiOutQueue.shift();
				midi::Message msg = std::get<1>(t);
				midiOutput.send(msg, std::get<2>(t));
			}
			midiOutput.processFrame(args.frame);
		}

		if (outputs[OUTPUT_TRIG].isConnected()) {
			for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
				bool s = outputPulseGenerator[i].process(args.sampleTime);
				if (outputTriggerActive[i]) {
					outputs[OUTPUT_TRIG].setVoltage(s ? 10.f : 0.f, i);
				}
			}
			
			// Drains the Tipsy queue regardless of activeEngine, for the same
			// reason as the MIDI out-queue above: messages queued by a script's
			// onUnload() must still reach the output after the engine is gone.
			processTipsyOutput(0);
		}

		sample++;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		json_object_set_new(rootJ, "script", json_string(script.c_str()));

		// Refresh here rather than in onSave(): Rack's periodic autosave calls
		// saveAutosave() without dispatching onSave() first, so an onSave()-only
		// refresh would leave autosaves writing stale config. rack.onSave() is
		// side-effect-free by contract, so running it on every save is harmless.
		//
		// Only overwrite on success: false means the config couldn't be
		// determined (dispatch dropped or timed out), so keeping the last known
		// value writes slightly stale config instead of erasing the user's
		// settings. A script with no onSave() returns true with an empty string
		// and correctly clears any stale value.
		if (activeEngine) {
			std::string captured;
			if (activeEngine->captureConfig(captured)) {
				scriptConfigJson = captured;
			}
		}
		if (!scriptConfigJson.empty()) {
			json_t* configJ = json_loads(scriptConfigJson.c_str(), 0, NULL);
			if (configJ) {
				json_object_set_new(rootJ, "scriptConfig", configJ);
			}
		}
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ && json_is_object(midiInputJ)) midiInput.fromJson(midiInputJ);
		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ && json_is_object(midiOutputJ)) midiOutput.fromJson(midiOutputJ);

		json_t* scriptJ = json_object_get(rootJ, "script");
		if (scriptJ && json_is_string(scriptJ)) {
			// Restore any persisted script config alongside the script itself.
			json_t* configJ = json_object_get(rootJ, "scriptConfig");
			std::string configJson;
			if (configJ && json_is_object(configJ)) {
				char* s = json_dumps(configJ, JSON_COMPACT);
				if (s) {
					configJson = s;
					free(s);
				}
			}
			loadScript(json_string_value(scriptJ), configJson);
		}
	}

	void loadScript(std::string s, std::string configJson = "") {
		script = s;
		sample = 0;
		inputTriggerTick = 0;
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = false;
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = false;
		}
		midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::RESET, 0.f, std::string("")));

		MidiScript::MidiScriptEngine* prevEngine = activeEngine;
		activeEngine = nullptr;
		if (seLua.testScript(s)) activeEngine = &seLua;
		if (seQuickJs.testScript(s)) activeEngine = &seQuickJs;

		// Close the engine that is no longer active (silently — RESET was already
		// pushed). Blocking rather than the async loadScript("") this used to be:
		// once this call returns, activeEngine is again the only engine that can
		// have any state or outstanding worker task, which is the invariant
		// onRemove() (and onReset()) rely on to know what needs tearing down.
		if (prevEngine && prevEngine != activeEngine) {
			prevEngine->closeState();
		}

		// Keep port/param info pointers in sync with the active engine
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->se = activeEngine;
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->se = activeEngine;
		}

		scriptConfigJson = configJson;
		if (activeEngine) activeEngine->loadScript(script.c_str(), scriptConfigJson);
	}

	void clearScript() {
		loadScript("");
	}
};


struct LogDisplay : LedTextDisplay {
	std::list<std::tuple<LOG_FORMAT, float, std::string>>* buffer;
	bool dirty = true;

	LogDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		bgColor.a = 0.f;
		fontSize = 9.2f;
		textOffset.y += 2.f;
	}

	void step() override {
		LedTextDisplay::step();
		if (dirty) {
			text = "";
			size_t size = std::min(buffer->size(), static_cast<size_t>(box.size.x / fontSize) + 1);
			size_t i = 0;
			for (std::tuple<LOG_FORMAT, float, std::string> s : *buffer) {
				if (i >= size) break;
				LOG_FORMAT f = std::get<0>(s);
				float timestamp = std::get<1>(s);
				switch (f) {
					case LOG_FORMAT::TIMESTAMP:
						text += string::f("[%9.4f] %s\n", timestamp, std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::TEXT:
						text += string::f("%s\n", std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::INDENTED:
						text += string::f("     %s\n", std::get<2>(s).c_str());
						break;
					default:
						break;
				};
			}
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}
};

// Placeholder menu entry that builds the script-registered items
// (rack.registerContextMenu) asynchronously. getContextMenus() evaluates each
// item's onGetValue callback on the worker thread and then invokes its
// callback with the evaluated specs.
struct ScriptContextMenuItems : ui::MenuEntry {
	struct Context {
		std::vector<MidiScript::ScriptMenuItem> specs;
		std::atomic<bool> loaded{false};
	};
	MidiKitModule* module;
	std::shared_ptr<Context> ctx;
	bool built = false;

	ScriptContextMenuItems(MidiKitModule* module) : module(module) {
		ctx = std::make_shared<Context>();
		// Capture a local copy: Apple's Clang rejects capturing the data
		// member `ctx` by name in a capture list.
		std::shared_ptr<Context> c = ctx;
		module->activeEngine->getContextMenus([c](const std::vector<MidiScript::ScriptMenuItem>& specs) {
			// Runs on the worker thread once every onGetValue has been
			// evaluated. Only publishes the specs; the menu widgets are
			// constructed by step() on the UI thread.
			c->specs = specs;
			c->loaded.store(true, std::memory_order_release);
		});
	}

	void step() override {
		if (!built && ctx->loaded.load(std::memory_order_acquire)) {
			built = true;
			buildItems();
			requestDelete();
		}
		ui::MenuEntry::step();
	}

	void buildItems() {
		Menu* menu = dynamic_cast<Menu*>(parent);
		if (!menu) return;
		MidiKitModule* m = module;
		Widget* anchor = this;
		for (const MidiScript::ScriptMenuItem& spec : ctx->specs) {
			Widget* item;
			if (spec.type == MidiScript::ScriptMenuItem::Type::Boolean) {
				item = createMenuItem(spec.label, CHECKMARK(spec.checked), [m, spec]() {
					m->activeEngine->invokeContextMenuCallback(spec.callbackId, spec.checked ? 0 : 1);
				});
			}
			else {
				item = createSubmenuItem(spec.label, "", [m, spec](Menu* sub) {
					for (size_t i = 0; i < spec.options.size(); i++) {
						sub->addChild(createMenuItem(spec.options[i], CHECKMARK(i == static_cast<size_t>(spec.selected)), [m, spec, i]() {
							m->activeEngine->invokeContextMenuCallback(spec.callbackId, static_cast<int>(i));
						}));
					}
				});
			}
			menu->addChildAbove(item, anchor);
			anchor = item;
		}
	}
};

struct MidiKitWidget : ThemedModuleWidget<MidiKitModule>, OverlayMessageProvider {
	const size_t BUFFERSIZE = 800;
	LogDisplay* logDisplay;
	std::list<std::tuple<LOG_FORMAT, float, std::string>> buffer;
	std::string filename = "";

	MidiKitWidget(MidiKitModule *module)
		: ThemedModuleWidget<MidiKitModule>(module, "MidiKit") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget<>* display1 = createWidget<MidiWidget<>>(Vec(0.f, 36.4f));
		display1->box.size = Vec(180.0f, 44.6f);
		display1->setMidiPort(module ? &module->midiInput : NULL, "In");
		addChild(display1);

		LedDisplay* textDisplay = createWidget<LedDisplay>(Vec(0.f, 81.0f));
		textDisplay->box.size = Vec(180.f, 140.6f);
		addChild(textDisplay);

		logDisplay = createWidget<LogDisplay>(Vec());
		logDisplay->buffer = &buffer;
		logDisplay->box.size = textDisplay->box.size.minus(Vec(0.f, 6.f));
		logDisplay->fontSize = 7.2f;
		textDisplay->addChild(logDisplay);

		MidiWidget<>* display2 = createWidget<MidiWidget<>>(Vec(0.f, 221.6f));
		display2->box.size = Vec(180.0f, 44.6f);
		display2->setMidiPort(module ? &module->midiOutput : NULL, "Out");
		addChild(display2);

		addParam(createParamCentered<StoermelderTrimpot>(Vec(24.7f, 287.3f), module, MidiKitModule::PARAM + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(56.2f, 287.3f), module, MidiKitModule::PARAM + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(87.6f, 287.3f), module, MidiKitModule::PARAM + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(119.1f, 287.3f), module, MidiKitModule::PARAM + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(24.7f, 328.4f), module, MidiKitModule::INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(56.2f, 328.4f), module, MidiKitModule::INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(87.6f, 328.4f), module, MidiKitModule::INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(119.1f, 328.4f), module, MidiKitModule::INPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(156.f, 287.3f), module, MidiKitModule::OUTPUT_TRIG));

		addInput(createInputCentered<StoermelderPort>(Vec(156.f, 328.4f), module, MidiKitModule::INPUT_TRIG));

		if (module) {
			OverlayMessageWidget::registerProvider(this);
		}
	}

	~MidiKitWidget() {
		if (module) {
			OverlayMessageWidget::unregisterProvider(this);
		}
	}

	void step() override {
		ThemedModuleWidget<MidiKitModule>::step();
		if (!module) return;
		std::tuple<LOG_FORMAT, float, std::string> s;
		while (module->midiLogMessages.try_pop(s)) {
			if (buffer.size() == BUFFERSIZE) buffer.pop_back();
			if (std::get<0>(s) == LOG_FORMAT::RESET) {
				resetLog();
			}
			else {
				buffer.push_front(s);
				logDisplay->dirty = true;
			}
		}
	}

	void resetLog() {
		buffer.clear();
		logDisplay->reset();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiKitModule>::appendContextMenu(menu);

		if (module->activeEngine) {
			menu->addChild(new MenuSeparator());
			if (module->activeEngine == &module->seLua) {
				menu->addChild(createMenuLabel("Running Script (Lua)"));
				size_t used;
				if (module->seLua.getMemoryUsage(used)) {
					menu->addChild(createMenuLabel(string::f("RAM usage: %zu KB", used / 1024)));
				}
			}
			if (module->activeEngine == &module->seQuickJs) {
				menu->addChild(createMenuLabel("Running Script (QuickJs)"));
				size_t used, total;
				if (module->seQuickJs.getMemoryUsage(used, total)) {
					float pct = total > 0 ? 100.f * used / total : 0.f;
					menu->addChild(createMenuLabel(string::f("RAM usage: %zu / %zu KB (%.0f%%)", used / 1024, total / 1024, pct)));
				}
			}

			menu->addChild(new ScriptContextMenuItems(module));
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Script"));
		menu->addChild(createSubmenuItem("Examples (JavaScript)", "", [=](Menu* menu) {
			appendExampleItems(menu, asset::plugin(pluginInstance, "presets/MidiKit/JavaScript"), ".js");
		}));
		menu->addChild(createSubmenuItem("Examples (Lua)", "", [=](Menu* menu) {
			appendExampleItems(menu, asset::plugin(pluginInstance, "presets/MidiKit/Lua"), ".lua");
		}));
		menu->addChild(createMenuItem("Clear", "", [=]() { module->clearScript(); }));
		menu->addChild(createMenuItem("Paste from clipboard", RACK_MOD_ALT_NAME "+V", [=]() { pasteJsClipboard(); }));
		menu->addChild(createMenuItem("Copy to clipboard", RACK_MOD_ALT_NAME "+C", [=]() { copyJsClipboard(); }));
		menu->addChild(createMenuItem("Load", RACK_MOD_ALT_NAME "+L", [=]() { loadJsDialog(); }));
		menu->addChild(createMenuItem("Reload", RACK_MOD_ALT_NAME "+Y", [=]() { loadJs(filename); }, filename.empty()));
		menu->addChild(createMenuItem("Save as", "", [=]() { saveScriptDialog(); }));
	}

	int nextOverlayMessageId() override {
		if (module->overlayQueue.empty())
			return -1;
		return module->overlayQueue.shift();
	}

	void getOverlayMessage(int id, OverlayMessageProvider::Message& m) override {
		m.title = std::get<0>(module->overlayMessage);
		m.subtitle[0] = std::get<1>(module->overlayMessage);
		m.subtitle[1] = std::get<2>(module->overlayMessage);
	}

	void loadJsDialog() {
		osdialog_filters* filters = osdialog_filters_parse("MIDI-KIT file:js,lua");
		DEFER({
			osdialog_filters_free(filters);
		});

		char* path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		filename = path;
		loadJs(path);
	}

	void loadJs(std::string filename) {
		resetLog();

		// Read file
		std::ifstream file;
		file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		try {
			file.open(filename);
			std::stringstream buffer;
			buffer << file.rdbuf();
			std::string script = buffer.str();
			module->loadScript(script);
		}
		catch (const std::runtime_error& err) {
			// Fail silently
		}
	}

	// Returns true if dir (or any of its subfolders, recursively) contains at
	// least one script file with the given extension. Used to avoid creating
	// empty submenus for folders that hold no scripts of the active engine.
	bool hasExampleScripts(std::string dir, std::string ext) {
		if (!system::isDirectory(dir)) return false;
		for (std::string path : system::getEntries(dir)) {
			if (system::isDirectory(path)) {
				if (hasExampleScripts(path, ext)) return true;
			}
			else if (system::getExtension(path) == ext) {
				return true;
			}
		}
		return false;
	}

	// Lists .js/.lua example scripts bundled under src/modules/midikit/, sorted,
	// as clickable menu items (mirrors ModuleWidget's factory-preset submenu, but
	// for raw scripts). Subfolders become nested submenus, recursing arbitrarily
	// deep. All other file types in those folders (.cpp, .h, .md, ...) are
	// ignored. Subfolders are listed before files within each directory.
	void appendExampleItems(Menu* menu, std::string dir, std::string ext) {
		bool hasExamples = false;
		if (system::isDirectory(dir)) {
			std::vector<std::string> entries = system::getEntries(dir);
			std::sort(entries.begin(), entries.end());
			// Subfolders first (sorted)
			for (std::string path : entries) {
				if (!system::isDirectory(path)) continue;
				if (!hasExampleScripts(path, ext)) continue;
				hasExamples = true;
				std::string name = system::getFilename(path);
				menu->addChild(createSubmenuItem(name, "", [=](Menu* menu) {
					appendExampleItems(menu, path, ext);
				}));
			}
			// Files second (sorted)
			for (std::string path : entries) {
				if (system::isDirectory(path)) continue;
				if (system::getExtension(path) != ext) continue;
				hasExamples = true;
				std::string name = system::getStem(path);
				menu->addChild(createMenuItem(name, "", [=]() {
					filename = path;
					loadJs(path);
				}));
			}
		}
		if (!hasExamples) {
			menu->addChild(createMenuLabel("None found"));
		}
	}

	void saveScriptDialog() {
		if (module->script == "")
			return;

		std::string dir = asset::userDir;
		std::string filename = "script.js";
		char* newPathC = osdialog_file(OSDIALOG_SAVE, dir.c_str(), filename.c_str(), NULL);
		if (!newPathC) {
			return;
		}
		std::string newPath = newPathC;
		std::free(newPathC);
		// Add extension if user didn't specify one
		std::string newExt = system::getExtension(system::getFilename(newPath));
		if (newExt == "") newPath += ".js";

		// Write and close file
		{
			std::ofstream f(newPath);
			f << module->script;
		}
	}

	void onPathDrop(const event::PathDrop& e) override {
		if (module && e.paths.size() > 0) {
			loadJs(e.paths[0]);
			e.consume(this);
		}
		ThemedModuleWidget<MidiKitModule>::onPathDrop(e);
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_ALT) {
			if (e.keyName == "c") {
				copyJsClipboard();
				e.consume(this);
			}
			if (e.keyName == "v") {
				pasteJsClipboard();
				e.consume(this);
			}
			if (e.keyName == "l") {
				loadJsDialog();
				e.consume(this);
			}
			if (e.keyName == "y") {
				if (!filename.empty()) {
					loadJs(filename);
				}
				e.consume(this);
			}
		}
		ThemedModuleWidget<MidiKitModule>::onHoverKey(e);
	}

	void pasteJsClipboard() {
		const char* script = glfwGetClipboardString(APP->window->win);
		module->loadScript(script);
	}

	void copyJsClipboard() {
		const char* script = module->script.c_str();
		glfwSetClipboardString(APP->window->win, script);
	}
};


} // namespace MidiKit
} // namespace StoermelderPackOne

Model* modelMidiKit = createModel<StoermelderPackOne::MidiKit::MidiKitModule, StoermelderPackOne::MidiKit::MidiKitWidget>("MidiKit");